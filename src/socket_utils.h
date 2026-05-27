#pragma once
/**
 * 跨平台socket工具函数
 * Linux使用POSIX API，Windows使用Winsock2
 */

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using sock_fd_t = SOCKET;
    inline constexpr sock_fd_t SOCK_INVALID = INVALID_SOCKET;
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <signal.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using sock_fd_t = int;
    inline constexpr sock_fd_t SOCK_INVALID = -1;
#endif

#include <cstddef>
#include <string>

namespace sockutil {

/** 初始化网络库（Windows需要WSAStartup） */
bool init();

/** 清理网络库（Windows需要WSACleanup） */
void cleanup();

/** 创建TCP监听socket，绑定并监听指定端口，返回监听fd */
sock_fd_t createListener(int port);

/** 设置socket非阻塞 */
bool setNonBlocking(sock_fd_t fd);

/** 禁用Nagle算法 */
void setNoDelay(sock_fd_t fd);

/** 忽略SIGPIPE（仅Linux有效） */
void ignoreSigpipe();

/** 关闭socket */
void closeSocket(sock_fd_t fd);

/**
 * 非阻塞读取，数据追加到buf末尾
 * @param fd    socket描述符
 * @param buf   接收缓冲区
 * @param zero  输出参数，对端关闭时为true
 * @return 读取字节数，出错返回-1
 */
ssize_t recvAll(sock_fd_t fd, std::string& buf, bool& zero);

/**
 * 非阻塞写入，已写数据从buf头部移除
 * @param fd  socket描述符
 * @param buf 发送缓冲区
 * @return 写入字节数，出错返回-1
 */
ssize_t sendAll(sock_fd_t fd, std::string& buf);

/** 创建用于唤醒的管道/socket对，返回读写端fd */
bool createWakeupPipe(sock_fd_t& read_fd, sock_fd_t& write_fd);

/** 唤醒：向写端发送一个字节 */
void wakeup(sock_fd_t write_fd);

} // namespace sockutil
