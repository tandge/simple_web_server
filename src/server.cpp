#include "server.h"
#include "channel.h"
#include "event_loop.h"
#include "http_connection.h"
#include "logger.h"
#include "mime_types.h"
#include "socket_utils.h"
#include "timer.h"

#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #define STAT_FUNC _stat
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #define STAT_FUNC stat
#endif

static const int kDefaultTimeout = 5000;       // 普通连接超时5秒
static const int kKeepAliveTimeout = 300000;   // Keep-Alive超时5分钟

Server::Server(EventLoop* loop, ThreadPool* pool, int port,
               const std::string& root_dir)
    : loop_(loop),
      pool_(pool),
      listen_fd_(-1),
      port_(port),
      root_dir_(root_dir),
      timer_mgr_(new TimerManager()) {}

Server::~Server() {
    if (listen_fd_ >= 0) {
        sockutil::closeSocket(static_cast<sock_fd_t>(listen_fd_));
    }
}

void Server::start() {
    listen_fd_ = static_cast<int>(sockutil::createListener(port_));
    if (listen_fd_ < 0) {
        LOG_ERROR("Failed to listen on port %d", port_);
        return;
    }
    sockutil::setNonBlocking(static_cast<sock_fd_t>(listen_fd_));
    sockutil::ignoreSigpipe();

    accept_channel_ = std::make_unique<Channel>(loop_, listen_fd_);
    accept_channel_->setReadCallback([this]() { handleAccept(); });
    accept_channel_->enableReading();

    LOG_INFO("Server listening on port %d, root=%s", port_, root_dir_.c_str());
}

void Server::registerApi(const std::string& path, ApiHandler handler) {
    api_handlers_[path] = std::move(handler);
}

void Server::handleAccept() {
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        std::memset(&client_addr, 0, sizeof(client_addr));

        sock_fd_t conn_fd = ::accept(
            static_cast<sock_fd_t>(listen_fd_),
            reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);

        if (conn_fd == SOCK_INVALID) {
            break;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        int client_port = ntohs(client_addr.sin_port);
        LOG_INFO("New connection from %s:%d", ip, client_port);

        sockutil::setNonBlocking(conn_fd);
        sockutil::setNoDelay(conn_fd);

        onNewConnection(static_cast<int>(conn_fd));
    }
}

void Server::onNewConnection(int fd) {
    // 构造路由回调，捕获api_handlers_和root_dir_
    auto handlers = api_handlers_;
    std::string root = root_dir_;

    HttpConnection::RequestHandler router =
        [handlers = std::move(handlers), root = std::move(root)](
            const HttpRequest& req, HttpResponse* resp) {
            // 先查API路由
            auto it = handlers.find(req.path);
            if (it != handlers.end()) {
                it->second(req, resp);
                return;
            }

            // 再查静态文件
            std::string file_path = root + req.path;

            struct STAT_FUNC file_stat;
            if (STAT_FUNC(file_path.c_str(), &file_stat) != 0) {
                *resp = HttpResponse::makeError(404, "Not Found");
                return;
            }

            if (!(file_stat.st_mode & S_IFREG)) {
                *resp = HttpResponse::makeError(404, "Not Found");
                return;
            }

            FILE* fp = std::fopen(file_path.c_str(), "rb");
            if (!fp) {
                *resp = HttpResponse::makeError(404, "Not Found");
                return;
            }

            std::string body;
            body.resize(static_cast<size_t>(file_stat.st_size));
            size_t readn = std::fread(&body[0], 1, body.size(), fp);
            std::fclose(fp);

            if (readn != body.size()) {
                *resp = HttpResponse::makeError(500, "Internal Server Error");
                return;
            }

            // MIME类型
            size_t dot_pos = req.path.rfind('.');
            if (dot_pos != std::string::npos) {
                resp->setContentType(mime::getType(req.path.substr(dot_pos)));
            } else {
                resp->setContentType("application/octet-stream");
            }

            resp->setStatusCode(200);
            resp->setStatusMessage("OK");
            resp->setBody(body);
        };

    auto conn = std::make_shared<HttpConnection>(loop_, pool_, fd, std::move(router));
    conn->init();
    connections_[fd] = conn;

    timer_mgr_->addTimer(fd, conn, kDefaultTimeout);
}
