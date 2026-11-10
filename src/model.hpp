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

// All durations are simulated ticks, independent of the real execution speed.
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

struct Segment {
    Tick begin;
    Tick end;
    int pid; // 0 identifies IDLE.
};

// Deterministic state machine. Runtime serializes access; tests use it directly.
class Model {
public:
    int create(const std::string& name, Tick burst, int priority = 3);
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
};
} // namespace sim
