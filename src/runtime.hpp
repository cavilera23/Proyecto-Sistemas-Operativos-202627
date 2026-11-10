#pragma once

#include "model.hpp"
#include <pthread.h>

namespace sim {
struct MonitorSnapshot {
    Model model;
    bool active;
    int milliseconds;
};

// Owns the shared model. No caller receives references into shared memory.
class Runtime {
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void launch();
    void shutdown();
    void join();
    int create(const std::string& name, Tick burst, int priority);
    std::size_t load(const std::vector<WorkloadEntry>& entries);
    void set_policy(Policy policy, Tick quantum);
    void set_speed(int milliseconds);
    void start();
    void pause();
    Model snapshot() const;
    MonitorSnapshot monitor_snapshot() const;
    std::string error() const;

private:
    mutable pthread_mutex_t mutex_;
    pthread_cond_t condition_;
    pthread_t engine_{};
    bool launched_ = false; // Only the owning main thread accesses this flag.
    bool active_ = false;
    bool stopping_ = false;
    int speed_ = 1000;
    std::uint64_t generation_ = 0;
    std::string error_;
    Model model_;

    static void* entry(void* context);
    void run();
};
} // namespace sim
