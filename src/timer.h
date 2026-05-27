#pragma once
/**
 * 连接超时定时器
 * 使用两个map实现快速查找和过期扫描
 */

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

class HttpConnection;

class TimerManager {
public:
    /** 添加或更新某个fd的超时定时器 */
    void addTimer(int fd, std::weak_ptr<HttpConnection> conn, int timeout_ms);

    /** 移除某个fd的定时器 */
    void removeTimer(int fd);

    /** 扫描并关闭所有超时连接 */
    void handleExpired();

private:
    struct TimerNode {
        int fd;
        std::weak_ptr<HttpConnection> conn;
        uint64_t expire_time;
    };

    // fd -> 过期时间，用于快速更新/删除
    std::map<int, uint64_t> fd_to_expire_;
    // 过期时间 -> 节点列表，用于快速扫描过期
    std::multimap<uint64_t, TimerNode> expire_queue_;

    /** 获取当前时间（毫秒） */
    static uint64_t nowMs();
};
