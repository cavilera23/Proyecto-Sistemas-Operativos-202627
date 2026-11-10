#pragma once
#include "runtime.hpp"
#include <iosfwd>
#include <atomic>
#include <string>

namespace sim {

bool execute_command(const std::string& line, Runtime& runtime, std::ostream& out);
struct Shell {
    Runtime& runtime;
    std::istream& input;
    std::ostream& output;
    std::atomic<bool> finished{false};
    int status = 0;
};
void* cli_entry(void* context);
} // namespace sim
