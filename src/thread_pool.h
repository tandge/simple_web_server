#pragma once
/**
 * 固定线程数的线程池
 * 任务队列 + 条件变量调度
 */

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(int num_threads);
    ~ThreadPool();

    /** 启动所有工作线程 */
    void start();

    /** 停止线程池，等待所有工作线程结束 */
    void stop();

    /** 提交一个任务 */
    void submit(Task task);

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool running_;
    int num_threads_;
};
