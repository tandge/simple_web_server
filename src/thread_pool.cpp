#include "thread_pool.h"
#include "logger.h"

ThreadPool::ThreadPool(int num_threads)
    : running_(false), num_threads_(num_threads) {
    if (num_threads_ <= 0) num_threads_ = 4;
    if (num_threads_ > 16) num_threads_ = 16;
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    running_ = true;
    for (int i = 0; i < num_threads_; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
    LOG_INFO("ThreadPool started with %d threads", num_threads_);
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    cond_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

void ThreadPool::submit(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        tasks_.push(std::move(task));
    }
    cond_.notify_one();
}

void ThreadPool::workerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this]() {
                return !running_ || !tasks_.empty();
            });
            if (!running_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        if (task) task();
    }
}
