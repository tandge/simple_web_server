#include "timer.h"
#include "http_connection.h"
#include "logger.h"

#include <chrono>
#include <cstdio>

uint64_t TimerManager::nowMs() {
    auto tp = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch());
    return static_cast<uint64_t>(ms.count());
}

void TimerManager::addTimer(int fd, std::weak_ptr<HttpConnection> conn,
                            int timeout_ms) {
    // 如果已有旧定时器，先删除
    removeTimer(fd);

    uint64_t expire = nowMs() + static_cast<uint64_t>(timeout_ms);
    fd_to_expire_[fd] = expire;
    expire_queue_.insert({expire, TimerNode{fd, conn, expire}});
}

void TimerManager::removeTimer(int fd) {
    auto it = fd_to_expire_.find(fd);
    if (it == fd_to_expire_.end()) return;

    uint64_t expire = it->second;
    fd_to_expire_.erase(it);

    // 从expire_queue_中删除对应节点
    auto range = expire_queue_.equal_range(expire);
    for (auto i = range.first; i != range.second; ++i) {
        if (i->second.fd == fd) {
            expire_queue_.erase(i);
            break;
        }
    }
}

void TimerManager::handleExpired() {
    uint64_t now = nowMs();
    while (!expire_queue_.empty()) {
        auto it = expire_queue_.begin();
        if (it->first > now) break; // 没有过期

        int fd = it->second.fd;
        auto conn = it->second.conn.lock();

        // 从两个map中删除
        fd_to_expire_.erase(fd);
        expire_queue_.erase(it);

        // 连接还活着就关闭
        if (conn) {
            LOG_INFO("Timer expired, closing fd=%d", fd);
            conn->handleClose();
        }
    }
}
