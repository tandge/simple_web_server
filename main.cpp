/**
 * LightWebServer 主入口
 * 基于epoll + Reactor + 线程池的轻量级HTTP服务器
 *
 * 用法: webserver [-p port] [-t threads] [-r root_dir]
 */

#include "src/event_loop.h"
#include "src/logger.h"
#include "src/server.h"
#include "src/socket_utils.h"
#include "src/thread_pool.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  -p <port>      Listen port (default: 8080)\n"
              << "  -t <threads>   Thread pool size (default: 4)\n"
              << "  -r <root_dir>  Static file root (default: ./www)\n"
              << "  -l <log_file>  Log file path (default: stdout)\n"
              << "  -h             Show this help\n";
}

int main(int argc, char* argv[]) {
    int port = 8080;
    int threads = 4;
    std::string root_dir = "./www";
    std::string log_file;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            threads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            root_dir = argv[++i];
        } else if (std::strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }

    // 参数校验
    if (port <= 0 || port > 65535) {
        std::cerr << "Invalid port: " << port << std::endl;
        return 1;
    }
    if (threads <= 0 || threads > 64) {
        std::cerr << "Invalid thread count: " << threads << std::endl;
        return 1;
    }

    // 初始化网络库
    if (!sockutil::init()) {
        std::cerr << "Network init failed" << std::endl;
        return 1;
    }

    // 配置日志
    if (!log_file.empty()) {
        Logger::setOutputFile(log_file);
    }
    Logger::setLevel(Logger::LINFO);

    LOG_INFO("LightWebServer starting...");
    LOG_INFO("  port=%d, threads=%d, root=%s", port, threads, root_dir.c_str());

    // 创建核心组件
    EventLoop loop;
    ThreadPool pool(threads);
    pool.start();

    Server server(&loop, &pool, port, root_dir);

    // 注册一个示例API
    server.registerApi("/api/hello", [](const HttpRequest& req, HttpResponse* resp) {
        resp->setStatusCode(200);
        resp->setStatusMessage("OK");
        resp->setContentType("application/json");
        resp->setKeepAlive(req.keepAlive());
        resp->setBody("{\"status\":\"ok\",\"message\":\"Hello from LightWebServer\"}");
    });

    server.registerApi("/api/echo", [](const HttpRequest& req, HttpResponse* resp) {
        resp->setStatusCode(200);
        resp->setStatusMessage("OK");
        resp->setContentType("text/plain");
        resp->setKeepAlive(req.keepAlive());
        resp->setBody(req.body);
    });

    // 启动服务
    server.start();

    // 进入事件循环（阻塞）
    loop.loop();

    // 清理
    pool.stop();
    sockutil::cleanup();

    LOG_INFO("LightWebServer stopped.");
    return 0;
}
