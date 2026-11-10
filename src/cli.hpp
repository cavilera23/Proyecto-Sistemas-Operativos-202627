#pragma once
#include "runtime.hpp"
#include <iosfwd>
#include <string>

namespace sim {
// Returns false for exit; validation errors leave the shell running.
bool execute_command(const std::string& line, Runtime& runtime, std::ostream& out);
struct Shell {
    Runtime& runtime;
    std::istream& input;
    std::ostream& output;
    int status = 0; // Read by main only after pthread_join.
};
void* cli_entry(void* context);
} // namespace sim
