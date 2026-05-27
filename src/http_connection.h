#pragma once
/**
 * HTTP连接处理
 * 每个客户端连接对应一个HttpConnection
 * 负责读写和HTTP协议处理
 */

#include <functional>
#include <memory>
#include <string>

#include "http_request.h"
#include "http_response.h"
#include "socket_utils.h"

class EventLoop;
class Channel;
class ThreadPool;

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    /** 请求处理回调：由Server层注入路由逻辑 */
    using RequestHandler = std::function<void(const HttpRequest&, HttpResponse*)>;

    HttpConnection(EventLoop* loop, ThreadPool* pool, int fd,
                   RequestHandler handler);
    ~HttpConnection();

    Channel* getChannel();
    void init();

    /** 关闭连接（由定时器或错误处理调用） */
    void handleClose();

private:
    void handleRead();
    void handleWrite();
    void handleError();

    /** 在线程池中处理请求 */
    void processRequest(const std::string& input_data);

    EventLoop* loop_;
    ThreadPool* pool_;
    int fd_;
    RequestHandler request_handler_;
    std::unique_ptr<Channel> channel_;
    HttpRequestParser parser_;
    std::string input_buf_;
    std::string output_buf_;
    bool keep_alive_;
    bool connected_;
};
