#include "http_connection.h"
#include "channel.h"
#include "event_loop.h"
#include "logger.h"
#include "socket_utils.h"
#include "thread_pool.h"

HttpConnection::HttpConnection(EventLoop* loop, ThreadPool* pool, int fd,
                               RequestHandler handler)
    : loop_(loop),
      pool_(pool),
      fd_(fd),
      request_handler_(std::move(handler)),
      keep_alive_(false),
      connected_(true) {
    channel_ = std::make_unique<Channel>(loop, fd);
}

HttpConnection::~HttpConnection() {
    if (fd_ >= 0) {
        sockutil::closeSocket(static_cast<sock_fd_t>(fd_));
    }
}

Channel* HttpConnection::getChannel() {
    return channel_.get();
}

void HttpConnection::init() {
    auto self = shared_from_this();
    channel_->setReadCallback([self]() { self->handleRead(); });
    channel_->setWriteCallback([self]() { self->handleWrite(); });
    channel_->setErrorCallback([self]() { self->handleError(); });
    channel_->setCloseCallback([self]() { self->handleClose(); });
    channel_->enableReading();
}

void HttpConnection::handleRead() {
    if (!connected_) return;

    bool zero = false;
    ssize_t n = sockutil::recvAll(static_cast<sock_fd_t>(fd_), input_buf_, zero);
    if (n < 0) {
        handleError();
        return;
    }
    if (zero) {
        handleClose();
        return;
    }

    // 尝试解析请求
    auto result = parser_.parse(input_buf_.data(), input_buf_.size());
    if (result.code == HttpRequestParser::COMPLETE) {
        keep_alive_ = parser_.request().keepAlive();
        std::string req_data = std::move(input_buf_);
        input_buf_.clear();
        parser_.reset();

        // 提交到线程池处理
        auto self = shared_from_this();
        pool_->submit([self, data = std::move(req_data)]() {
            self->processRequest(data);
        });
    } else if (result.code == HttpRequestParser::PARSE_ERROR) {
        HttpResponse resp = HttpResponse::makeError(400, "Bad Request");
        output_buf_ = resp.serialize();
        channel_->enableWriting();
        parser_.reset();
        input_buf_.clear();
    }
}

void HttpConnection::handleWrite() {
    if (!connected_) return;

    ssize_t n = sockutil::sendAll(static_cast<sock_fd_t>(fd_), output_buf_);
    if (n < 0) {
        handleClose();
        return;
    }

    if (output_buf_.empty()) {
        channel_->disableWriting();
        if (!keep_alive_) {
            handleClose();
        }
    }
}

void HttpConnection::handleError() {
    HttpResponse resp = HttpResponse::makeError(500, "Internal Server Error");
    output_buf_ = resp.serialize();
    if (connected_) {
        channel_->enableWriting();
    }
}

void HttpConnection::handleClose() {
    if (!connected_) return;
    connected_ = false;
    channel_->disableAll();
    loop_->removeChannel(channel_.get());
}

void HttpConnection::processRequest(const std::string& input_data) {
    HttpRequestParser parser;
    auto result = parser.parse(input_data.data(), input_data.size());
    if (result.code != HttpRequestParser::COMPLETE) {
        auto self = shared_from_this();
        loop_->queueInLoop([self]() { self->handleError(); });
        return;
    }

    const HttpRequest& req = parser.request();
    HttpResponse resp;
    resp.setKeepAlive(req.keepAlive());

    // 调用Server注入的路由回调
    if (request_handler_) {
        request_handler_(req, &resp);
    } else {
        resp = HttpResponse::makeError(500, "No request handler");
    }

    std::string response_data = resp.serialize();

    // 回到事件循环线程发送响应
    auto self = shared_from_this();
    loop_->queueInLoop([self, data = std::move(response_data)]() {
        if (!self->connected_) return;
        self->output_buf_ += data;
        if (!self->channel_->isWriting()) {
            self->channel_->enableWriting();
        }
    });
}
