#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sim {
using Tick = std::int64_t;
enum class State { New, Ready, Running, Paused, Terminated };
enum class Policy { RR, SRTF };
const char* state_name(State state);

struct PCB {
    int pid;
    std::string name;
    int priority;
    Tick burst;
    Tick remaining;
    Tick arrival;
    Tick waiting = 0;
    std::optional<Tick> first_run;
    std::optional<Tick> completion;
    State state = State::New;
};

struct WorkloadEntry {
    std::string name;
    Tick burst;
    int priority;
    Tick arrival;
};

struct Segment {
    Tick begin;
    Tick end;
    int pid; // 0 identifica IDLE.
};

class Model {
public:
    int create(const std::string& name, Tick burst, int priority = 3);
    std::size_t load(const std::vector<WorkloadEntry>& entries);
    void set_policy(Policy policy, Tick quantum = 2);
    void pause();
    void resume();
    void step();

    Tick clock() const { return clock_; }
    Tick busy_ticks() const { return busy_; }
    Policy policy() const { return policy_; }
    Tick quantum() const { return quantum_; }
    const std::map<int, PCB>& processes() const { return processes_; }
    const std::vector<Segment>& history() const { return history_; }

private:
    std::map<int, PCB> processes_;
    std::deque<int> ready_;
    std::optional<int> current_;
    std::vector<Segment> history_;
    std::int64_t next_pid_ = 1;
    Tick clock_ = 0;
    Tick busy_ = 0;
    Tick quantum_ = 2;
    Tick slice_ = 0;
    Policy policy_ = Policy::RR;

    bool shorter(int lhs, int rhs) const;
    void dispatch();
    void admit_arrivals();
};
} // namespace sim
