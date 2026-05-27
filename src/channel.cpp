#include "channel.h"
#include "event_loop.h"

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0) {}

void Channel::update() {
    loop_->updateChannel(this);
}

void Channel::enableReading() {
    events_ |= kRead;
    update();
}

void Channel::enableWriting() {
    events_ |= kWrite;
    update();
}

void Channel::disableWriting() {
    events_ &= ~kWrite;
    update();
}

void Channel::disableAll() {
    events_ = kNone;
    update();
}

void Channel::handleEvent() {
    // 对端挂起且没有数据可读
    if ((revents_ & kHangup) && !(revents_ & kRead)) {
        if (close_cb_) close_cb_();
        return;
    }

    // 错误
    if (revents_ & kError) {
        if (error_cb_) error_cb_();
        return;
    }

    // 可读 / 对端关闭
    if (revents_ & kRead) {
        if (read_cb_) read_cb_();
    }

    // 可写
    if (revents_ & kWrite) {
        if (write_cb_) write_cb_();
    }
}
