#pragma once
/**
 * I/O事件通道
 * 封装一个fd及其关注的事件和回调
 */

#include <functional>

class EventLoop;

class Channel {
public:
    using Callback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel() = default;

    int fd() const { return fd_; }
    int events() const { return events_; }
    void setRevents(int ev) { revents_ = ev; }

    void enableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();

    bool isNoneEvent() const { return events_ == kNone; }
    bool isWriting() const { return (events_ & kWrite) != 0; }
    bool isReading() const { return (events_ & kRead) != 0; }

    void setReadCallback(Callback cb)  { read_cb_ = std::move(cb); }
    void setWriteCallback(Callback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(Callback cb) { error_cb_ = std::move(cb); }
    void setCloseCallback(Callback cb) { close_cb_ = std::move(cb); }

    /** 根据revents分发事件 */
    void handleEvent();

private:
    void update();

    EventLoop* loop_;
    int fd_;
    int events_;   // 关注的事件
    int revents_;  // poller返回的就绪事件

    Callback read_cb_;
    Callback write_cb_;
    Callback error_cb_;
    Callback close_cb_;

public:
    static const int kNone  = 0;
    static const int kRead  = 1;
    static const int kWrite = 2;
    static const int kError = 0x08;
    static const int kHangup = 0x20;
};
