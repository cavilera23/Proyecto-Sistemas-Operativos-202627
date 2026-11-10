#include "runtime.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace sim {
namespace {
void check(int result, const char* operation) {
    if (result) throw std::system_error(result, std::generic_category(), operation);
}

class Lock {
public:
    explicit Lock(pthread_mutex_t& mutex) : mutex_(mutex) {
        check(pthread_mutex_lock(&mutex_), "pthread_mutex_lock");
    }
    ~Lock() { pthread_mutex_unlock(&mutex_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
private:
    pthread_mutex_t& mutex_;
};
}

Runtime::Runtime() {
    check(pthread_mutex_init(&mutex_, nullptr), "pthread_mutex_init");
    const int result = pthread_cond_init(&condition_, nullptr);
    if (result) {
        pthread_mutex_destroy(&mutex_);
        check(result, "pthread_cond_init");
    }
}

Runtime::~Runtime() {
    shutdown();
    join();
    pthread_cond_destroy(&condition_);
    pthread_mutex_destroy(&mutex_);
}

void Runtime::launch() {
    if (launched_) throw std::logic_error("El motor ya fue creado.");
    check(pthread_create(&engine_, nullptr, entry, this), "pthread_create motor");
    launched_ = true;
}

void Runtime::shutdown() {
    Lock lock(mutex_);
    stopping_ = true;
    active_ = false;
    ++generation_;
    pthread_cond_broadcast(&condition_);
}

void Runtime::join() {
    if (launched_) {
        check(pthread_join(engine_, nullptr), "pthread_join motor");
        launched_ = false;
    }
}

int Runtime::create(const std::string& name, Tick burst, int priority) {
    Lock lock(mutex_);
    const int pid = model_.create(name, burst, priority);
    if (active_) model_.resume();
    return pid;
}

std::size_t Runtime::load(const std::vector<WorkloadEntry>& entries) {
    Lock lock(mutex_);
    const auto count = model_.load(entries);
    if (active_) model_.resume();
    return count;
}

void Runtime::set_policy(Policy policy, Tick quantum) {
    Lock lock(mutex_);
    model_.set_policy(policy, quantum);
    if (active_) model_.resume();
}

void Runtime::set_speed(int milliseconds) {
    if (milliseconds < 50 || milliseconds > 5000)
        throw std::invalid_argument("La velocidad debe estar entre 50 y 5000 ms.");
    Lock lock(mutex_);
    speed_ = milliseconds;
}

void Runtime::start() {
    Lock lock(mutex_);
    if (stopping_) throw std::logic_error("El motor esta detenido definitivamente.");
    if (!active_) {
        model_.resume();
        active_ = true;
        ++generation_;
        pthread_cond_signal(&condition_);
    }
}

void Runtime::pause() {
    Lock lock(mutex_);
    if (active_) {
        active_ = false;
        ++generation_;
        model_.pause();
    }
}

Model Runtime::snapshot() const {
    Lock lock(mutex_);
    return model_;
}

MonitorSnapshot Runtime::monitor_snapshot() const {
    Lock lock(mutex_);
    return {model_, active_, speed_};
}

std::string Runtime::error() const {
    Lock lock(mutex_);
    return error_;
}

void* Runtime::entry(void* context) {
    auto& runtime = *static_cast<Runtime*>(context);
    try {
        runtime.run();
    } catch (const std::exception& ex) {
        Lock lock(runtime.mutex_);
        runtime.error_ = ex.what();
        runtime.stopping_ = true;
        runtime.active_ = false;
    }
    return nullptr;
}

void Runtime::run() {
    using Clock = std::chrono::steady_clock;
    while (true) {
        int milliseconds;
        std::uint64_t generation;
        {
            Lock lock(mutex_);
            while (!active_ && !stopping_)
                check(pthread_cond_wait(&condition_, &mutex_), "pthread_cond_wait");
            if (stopping_) return;
            milliseconds = speed_;
            generation = generation_;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        while (true) {
            std::this_thread::sleep_for(std::min(deadline - Clock::now(),
                std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(10))));
            Lock lock(mutex_);
            if (stopping_) return;
            if (!active_ || generation != generation_) break;
            if (Clock::now() >= deadline) {
                model_.step();
                break;
            }
        }
    }
}
} // namespace sim
