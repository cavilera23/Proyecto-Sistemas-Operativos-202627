#include "model.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace sim {
const char* state_name(State state) {
    switch (state) {
    case State::New: return "NEW";
    case State::Ready: return "READY";
    case State::Running: return "RUNNING";
    case State::Paused: return "PAUSED";
    case State::Terminated: return "TERMINATED";
    }
    return "UNKNOWN";
}

int Model::create(const std::string& name, Tick burst, int priority) {
    if (name.empty() || std::any_of(name.begin(), name.end(), [](unsigned char c) {
            return std::isspace(c);
        })) throw std::invalid_argument("El nombre debe ser una cadena sin espacios.");
    if (burst <= 0) throw std::invalid_argument("La rafaga debe ser un entero positivo.");
    if (priority < 1 || priority > 5)
        throw std::invalid_argument("La prioridad debe estar entre 1 y 5.");
    if (next_pid_ > std::numeric_limits<int>::max())
        throw std::overflow_error("Se agotaron los PID disponibles.");
    const int pid = static_cast<int>(next_pid_);
    PCB pcb{pid, name, priority, burst, burst, clock_, 0, {}, {}, State::New};
    processes_.emplace(pid, std::move(pcb));
    try {
        ready_.push_back(pid);
    } catch (...) {
        processes_.erase(pid);
        throw;
    }
    processes_.at(pid).state = State::Ready;
    ++next_pid_;
    return pid;
}

std::size_t Model::load(const std::vector<WorkloadEntry>& entries) {
    Model staged = *this;
    for (const auto& entry : entries) {
        if (entry.arrival < clock_)
            throw std::invalid_argument("La llegada no puede ser anterior al reloj actual.");
        const int pid = staged.create(entry.name, entry.burst, entry.priority);
        auto& pcb = staged.processes_.at(pid);
        pcb.arrival = entry.arrival;
        if (entry.arrival > clock_) {
            staged.ready_.pop_back();
            pcb.state = State::New;
        }
    }
    *this = std::move(staged);
    return entries.size();
}

void Model::admit_arrivals() {
    for (auto& [pid, pcb] : processes_) {
        if (pcb.state == State::New && pcb.arrival <= clock_) {
            ready_.push_back(pid);
            pcb.state = State::Ready;
        }
    }
}

void Model::set_policy(Policy policy, Tick quantum) {
    if (quantum <= 0) throw std::invalid_argument("El quantum debe ser positivo.");
    policy_ = policy;
    quantum_ = quantum;
    slice_ = 0;
}

void Model::pause() {
    if (current_) processes_.at(*current_).state = State::Paused;
}

void Model::resume() {
    admit_arrivals();
    dispatch();
}

bool Model::shorter(int lhs, int rhs) const {
    const auto& a = processes_.at(lhs);
    const auto& b = processes_.at(rhs);
    return std::tie(a.remaining, a.arrival, a.pid) <
           std::tie(b.remaining, b.arrival, b.pid);
}

void Model::dispatch() {
    if (policy_ == Policy::SRTF && !ready_.empty()) {
        auto best = std::min_element(ready_.begin(), ready_.end(),
                                    [this](int a, int b) { return shorter(a, b); });
        if (!current_ || processes_.at(*best).remaining < processes_.at(*current_).remaining) {
            const int selected = *best;
            ready_.erase(best);
            if (current_) {
                processes_.at(*current_).state = State::Ready;
                ready_.push_back(*current_);
            }
            current_ = selected;
            slice_ = 0;
        }
    }
    if (!current_ && !ready_.empty()) {
        current_ = ready_.front();
        ready_.pop_front();
        slice_ = 0;
    }
    if (current_) processes_.at(*current_).state = State::Running;
}

void Model::step() {
    if (clock_ == std::numeric_limits<Tick>::max())
        throw std::overflow_error("Se agoto el reloj simulado.");
    admit_arrivals();
    dispatch();
    const int executed = current_.value_or(0);
    for (int pid : ready_) ++processes_.at(pid).waiting;
    if (current_) {
        auto& pcb = processes_.at(*current_);
        if (!pcb.first_run) pcb.first_run = clock_;
        --pcb.remaining;
        ++busy_;
        ++slice_;
    }
    if (!history_.empty() && history_.back().pid == executed)
        history_.back().end = clock_ + 1;
    else
        history_.push_back({clock_, clock_ + 1, executed});
    ++clock_;
    admit_arrivals();
    if (current_) {
        auto& pcb = processes_.at(*current_);
        if (pcb.remaining == 0) {
            pcb.state = State::Terminated;
            pcb.completion = clock_;
            current_.reset();
            slice_ = 0;
        } else if (policy_ == Policy::RR && slice_ >= quantum_) {
            pcb.state = State::Ready;
            ready_.push_back(*current_);
            current_.reset();
            slice_ = 0;
        }
    }
    dispatch();
}
} // namespace sim
