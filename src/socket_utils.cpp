#include "socket_utils.h"

#include <cerrno>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
    #include <io.h>
#else
    #include <sys/types.h>
#endif

namespace sockutil {

static const int kReadBufSize = 4096;

#ifdef _WIN32
static bool wsa_initialized = false;
#endif

bool init() {
#ifdef _WIN32
    if (wsa_initialized) return true;
    WSADATA data;
    int ret = WSAStartup(MAKEWORD(2, 2), &data);
    if (ret != 0) return false;
    wsa_initialized = true;
#endif
    return true;
}

void cleanup() {
#ifdef _WIN32
    if (wsa_initialized) {
        WSACleanup();
        wsa_initialized = false;
    }
#endif
}

void closeSocket(sock_fd_t fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

bool setNonBlocking(sock_fd_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

void setNoDelay(sock_fd_t fd) {
    int val = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&val), sizeof(val));
}

void ignoreSigpipe() {
#ifndef _WIN32
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, nullptr);
#endif
}

sock_fd_t createListener(int port) {
    sock_fd_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) return SOCK_INVALID;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(fd);
        return SOCK_INVALID;
    }

    if (listen(fd, 2048) != 0) {
        closeSocket(fd);
        return SOCK_INVALID;
    }

    return fd;
}

ssize_t recvAll(sock_fd_t fd, std::string& buf, bool& zero) {
    ssize_t total = 0;
    zero = false;

    while (true) {
        char tmp[kReadBufSize];
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            buf.append(tmp, static_cast<size_t>(n));
            total += n;
        } else if (n == 0) {
            zero = true;
            break;
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR) continue;
            if (err == WSAEWOULDBLOCK) break;
#else
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
            return -1; // 真正的错误
        }
    }
    return total;
}

ssize_t sendAll(sock_fd_t fd, std::string& buf) {
    size_t remaining = buf.size();
    ssize_t total = 0;
    const char* ptr = buf.data();

    while (remaining > 0) {
        ssize_t n = ::send(fd, ptr, remaining, 0);
        if (n > 0) {
            total += n;
            remaining -= static_cast<size_t>(n);
            ptr += n;
        } else if (n == 0) {
            break;
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR) continue;
            if (err == WSAEWOULDBLOCK) break;
#else
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
#endif
            return -1;
        }
    }

    // 移除已发送的部分
    if (static_cast<size_t>(total) == buf.size()) {
        buf.clear();
    } else if (total > 0) {
        buf.erase(0, static_cast<size_t>(total));
    }
    return total;
}

bool createWakeupPipe(sock_fd_t& read_fd, sock_fd_t& write_fd) {
#ifdef _WIN32
    // Windows: 创建一对回环连接的TCP socket
    sock_fd_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == SOCK_INVALID) return false;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(server_fd);
        return false;
    }
    if (listen(server_fd, 1) != 0) {
        closeSocket(server_fd);
        return false;
    }

    socklen_t addr_len = sizeof(addr);
    getsockname(server_fd, reinterpret_cast<struct sockaddr*>(&addr), &addr_len);

    sock_fd_t client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == SOCK_INVALID) {
        closeSocket(server_fd);
        return false;
    }
    if (connect(client_fd, reinterpret_cast<struct sockaddr*>(&addr), addr_len) != 0) {
        closeSocket(client_fd);
        closeSocket(server_fd);
        return false;
    }

    read_fd = accept(server_fd, nullptr, nullptr);
    closeSocket(server_fd);
    if (read_fd == SOCK_INVALID) {
        closeSocket(client_fd);
        return false;
    }
    write_fd = client_fd;
#else
    // Linux: 用pipe
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;
    read_fd = pipefd[0];
    write_fd = pipefd[1];
#endif
    return true;
}

void wakeup(sock_fd_t write_fd) {
    char c = 1;
    ::send(write_fd, &c, 1, 0);
}

} // namespace sockutil
