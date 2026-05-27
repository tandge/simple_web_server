#pragma once
/**
 * I/O多路复用抽象
 * Linux使用epoll，Windows使用select
 */

#include <unordered_map>
#include <vector>

class Channel;

class Poller {
public:
    Poller();
    ~Poller();

    /** 添加新channel到监控集合 */
    void addChannel(Channel* ch);

    /** 更新已有channel的事件 */
    void updateChannel(Channel* ch);

    /** 移除channel */
    void removeChannel(Channel* ch);

    /**
     * 等待事件就绪
     * @param timeout_ms 超时毫秒，-1表示无限等待
     * @return 就绪的channel列表
     */
    std::vector<Channel*> poll(int timeout_ms);

private:
#ifdef _WIN32
    // select实现：用fd到channel的映射维护所有channel
    std::unordered_map<int, Channel*> channels_;
#else
    int epoll_fd_;
    std::vector<struct epoll_event> events_;
#endif

    /** 把Channel的事件转换为epoll_event并调用epoll_ctl */
    void epollCtrl(int operation, Channel* ch);
};
