#pragma once
/**
 * HTTP服务器
 * 负责监听、接受连接、路由分发
 */

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "http_request.h"
#include "http_response.h"

class EventLoop;
class ThreadPool;
class Channel;
class TimerManager;

class Server {
public:
    using ApiHandler = std::function<void(const HttpRequest&, HttpResponse*)>;

    Server(EventLoop* loop, ThreadPool* pool, int port,
           const std::string& root_dir = "./www");
    ~Server();

    /** 启动服务器 */
    void start();

    /** 注册API路由 */
    void registerApi(const std::string& path, ApiHandler handler);

private:
    void handleAccept();
    void onNewConnection(int fd);
    void handleApiRequest(std::shared_ptr<class HttpConnection> conn,
                          const HttpRequest& req, HttpResponse* resp);

    EventLoop* loop_;
    ThreadPool* pool_;
    int listen_fd_;
    int port_;
    std::string root_dir_;
    std::unique_ptr<Channel> accept_channel_;
    std::unique_ptr<TimerManager> timer_mgr_;
    std::map<int, std::weak_ptr<HttpConnection>> connections_;
    std::map<std::string, ApiHandler> api_handlers_;
};
