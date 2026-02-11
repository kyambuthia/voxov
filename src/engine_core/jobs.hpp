#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <vector>

class JobSystem {
public:
    void init(uint32_t worker_count);
    void shutdown();
    void schedule(const std::function<void()> &job);
    void wait_idle();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> queue;
    std::mutex mutex;
    std::condition_variable cv;
    bool running = false;
    uint32_t active = 0;
};
