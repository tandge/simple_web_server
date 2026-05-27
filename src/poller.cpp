#include "poller.h"
#include "channel.h"
#include "logger.h"

#include <cerrno>
#include <cstring>
#include <unordered_map>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/epoll.h>
    #include <unistd.h>
#endif

/* ==================== Linux: epoll ==================== */
#ifndef _WIN32

static const int kEpollInitSize = 1024;

Poller::Poller()
    : epoll_fd_(epoll_create1(EPOLL_CLOEXEC)),
      events_(kEpollInitSize) {
    if (epoll_fd_ < 0) {
        LOG_ERROR("epoll_create1 failed: %s", std::strerror(errno));
    }
}

Poller::~Poller() {
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
}

void Poller::addChannel(Channel* ch) {
    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.data.ptr = ch;
    if (ch->events() & Channel::kRead)  ev.events |= EPOLLIN;
    if (ch->events() & Channel::kWrite) ev.events |= EPOLLOUT;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ch->fd(), &ev) < 0) {
        LOG_ERROR("epoll_add fd=%d err: %s", ch->fd(), std::strerror(errno));
    }
}

void Poller::updateChannel(Channel* ch) {
    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.data.ptr = ch;
    if (ch->events() & Channel::kRead)  ev.events |= EPOLLIN;
    if (ch->events() & Channel::kWrite) ev.events |= EPOLLOUT;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, ch->fd(), &ev) < 0) {
        // 如果MOD失败可能是因为还没ADD过，尝试ADD
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ch->fd(), &ev) < 0) {
            LOG_ERROR("epoll_mod fd=%d err: %s", ch->fd(), std::strerror(errno));
        }
    }
}

void Poller::removeChannel(Channel* ch) {
    // DEL的时候event参数可以为null(Linux >= 2.6.9)，但某些旧内核需要非null
    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ch->fd(), &ev);
}

std::vector<Channel*> Poller::poll(int timeout_ms) {
    int n = epoll_wait(epoll_fd_, events_.data(),
                       static_cast<int>(events_.size()), timeout_ms);
    if (n < 0) {
        if (errno != EINTR) {
            LOG_ERROR("epoll_wait err: %s", std::strerror(errno));
        }
        return {};
    }

    std::vector<Channel*> active;
    for (int i = 0; i < n; ++i) {
        auto* ch = static_cast<Channel*>(events_[i].data.ptr);
        if (ch) {
            // 将epoll事件翻译回Channel内部事件
            int revents = 0;
            uint32_t ev = events_[i].events;
            if (ev & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) revents |= Channel::kRead;
            if (ev & EPOLLOUT) revents |= Channel::kWrite;
            if (ev & EPOLLERR) revents |= Channel::kError;
            if (ev & EPOLLHUP) revents |= Channel::kHangup;
            ch->setRevents(revents);
            active.push_back(ch);
        }
    }

    // 自动扩容
    if (static_cast<size_t>(n) == events_.size()) {
        events_.resize(events_.size() * 2);
    }

    return active;
}

#else /* _WIN32 —— select实现 */

Poller::Poller() {}

Poller::~Poller() {}

void Poller::addChannel(Channel* ch) {
    channels_[ch->fd()] = ch;
}

void Poller::updateChannel(Channel* ch) {
    channels_[ch->fd()] = ch;
}

void Poller::removeChannel(Channel* ch) {
    channels_.erase(ch->fd());
}

std::vector<Channel*> Poller::poll(int timeout_ms) {
    fd_set read_set, write_set, err_set;
    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_ZERO(&err_set);

    int max_fd = 0;
    for (auto& kv : channels_) {
        int fd = kv.first;
        Channel* ch = kv.second;
        if (ch->isReading()) FD_SET(fd, &read_set);
        if (ch->isWriting()) FD_SET(fd, &write_set);
        FD_SET(fd, &err_set);
        if (fd > max_fd) max_fd = fd;
    }

    struct timeval tv;
    struct timeval* tvp = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }

    int n = select(max_fd + 1, &read_set, &write_set, &err_set, tvp);
    if (n < 0) {
        int err = WSAGetLastError();
        if (err != WSAEINTR) {
            LOG_ERROR("select err: %d", err);
        }
        return {};
    }

    std::vector<Channel*> active;
    for (auto& kv : channels_) {
        int fd = kv.first;
        Channel* ch = kv.second;
        int revents = 0;

        if (FD_ISSET(fd, &read_set))  revents |= Channel::kRead;
        if (FD_ISSET(fd, &write_set)) revents |= Channel::kWrite;
        if (FD_ISSET(fd, &err_set))   revents |= Channel::kError;

        if (revents != 0) {
            ch->setRevents(revents);
            active.push_back(ch);
        }
    }
    return active;
}

#endif // _WIN32
