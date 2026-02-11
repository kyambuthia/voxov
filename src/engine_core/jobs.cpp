#include "engine_core/jobs.hpp"

void JobSystem::init(uint32_t worker_count) {
    running = true;
    for (uint32_t i = 0; i < worker_count; ++i) {
        workers.emplace_back([this]() {
            while (true) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    cv.wait(lock, [this]() { return !queue.empty() || !running; });
                    if (!running && queue.empty()) {
                        return;
                    }
                    job = std::move(queue.front());
                    queue.pop();
                    active++;
                }
                job();
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    active--;
                }
                cv.notify_all();
            }
        });
    }
}

void JobSystem::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }
    cv.notify_all();
    for (auto &t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers.clear();
}

void JobSystem::schedule(const std::function<void()> &job) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(job);
    }
    cv.notify_one();
}

void JobSystem::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this]() { return queue.empty() && active == 0; });
}
