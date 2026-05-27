#pragma once
/**
 * 事件循环 (Reactor核心)
 * 单线程驱动，支持跨线程投递任务
 */

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "socket_utils.h"

class Poller;
class Channel;

class EventLoop {
public:
    using Task = std::function<void()>;

    EventLoop();
    ~EventLoop();

    /** 运行事件循环，阻塞直到quit */
    void loop();

    /** 通知事件循环退出 */
    void quit();

    /** 在事件循环中执行任务（若已在循环线程则直接执行，否则入队） */
    void runInLoop(Task task);

    /** 将任务投递到事件循环队列，保证异步执行 */
    void queueInLoop(Task task);

    /** 判断是否在事件循环线程中 */
    bool isInLoopThread() const;

    /** 以下方法转发给Poller */
    void addChannel(Channel* ch);
    void updateChannel(Channel* ch);
    void removeChannel(Channel* ch);

private:
    void doPendingTasks();
    void handleWakeupRead();

    std::unique_ptr<Poller> poller_;
    std::vector<Task> pending_tasks_;
    std::mutex mutex_;
    bool quit_;
    bool calling_pending_;

    // 跨线程唤醒机制
    sock_fd_t wakeup_read_fd_;
    sock_fd_t wakeup_write_fd_;
    std::unique_ptr<Channel> wakeup_channel_;

    // 记录事件循环所在线程
    std::thread::id thread_id_;
};
