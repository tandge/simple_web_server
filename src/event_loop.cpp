#include "event_loop.h"
#include "poller.h"
#include "channel.h"
#include "logger.h"

#include <algorithm>
#include <thread>

EventLoop::EventLoop()
    : poller_(new Poller()),
      quit_(false),
      calling_pending_(false),
      wakeup_read_fd_(SOCK_INVALID),
      wakeup_write_fd_(SOCK_INVALID),
      thread_id_(std::this_thread::get_id()) {

    // 创建唤醒管道
    if (!sockutil::createWakeupPipe(wakeup_read_fd_, wakeup_write_fd_)) {
        LOG_ERROR("createWakeupPipe failed");
        return;
    }
    sockutil::setNonBlocking(wakeup_read_fd_);

    // 注册唤醒channel
    wakeup_channel_ = std::make_unique<Channel>(this, static_cast<int>(wakeup_read_fd_));
    wakeup_channel_->setReadCallback([this]() { handleWakeupRead(); });
    wakeup_channel_->enableReading();
}

EventLoop::~EventLoop() {
    wakeup_channel_->disableAll();
    removeChannel(wakeup_channel_.get());
    sockutil::closeSocket(wakeup_read_fd_);
    sockutil::closeSocket(wakeup_write_fd_);
}

bool EventLoop::isInLoopThread() const {
    return std::this_thread::get_id() == thread_id_;
}

void EventLoop::loop() {
    while (!quit_) {
        auto active = poller_->poll(100); // 最多等100ms
        for (auto* ch : active) {
            ch->handleEvent();
        }
        doPendingTasks();
    }
}

void EventLoop::quit() {
    quit_ = true;
    // 不在循环线程则唤醒它
    if (!isInLoopThread()) {
        sockutil::wakeup(wakeup_write_fd_);
    }
}

void EventLoop::runInLoop(Task task) {
    if (isInLoopThread()) {
        task();
    } else {
        queueInLoop(std::move(task));
    }
}

void EventLoop::queueInLoop(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_tasks_.push_back(std::move(task));
    }
    if (!isInLoopThread()) {
        sockutil::wakeup(wakeup_write_fd_);
    }
}

void EventLoop::addChannel(Channel* ch) {
    poller_->addChannel(ch);
}

void EventLoop::updateChannel(Channel* ch) {
    poller_->updateChannel(ch);
}

void EventLoop::removeChannel(Channel* ch) {
    poller_->removeChannel(ch);
}

void EventLoop::doPendingTasks() {
    std::vector<Task> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(pending_tasks_);
    }

    calling_pending_ = true;
    for (auto& t : tasks) {
        t();
    }
    calling_pending_ = false;
}

void EventLoop::handleWakeupRead() {
    char buf[64];
    while (true) {
        ssize_t n = ::recv(wakeup_read_fd_, buf, sizeof(buf), 0);
        if (n <= 0) break;
    }
}
