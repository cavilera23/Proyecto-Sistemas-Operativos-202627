#pragma once

#include <atomic>
#include "runtime.hpp"

#ifdef SIM_WITH_QT
#include <memory>
class QWidget;
#endif

namespace sim {
void request_gui();
void gui_event_loop(Runtime& runtime, const std::atomic<bool>& cli_finished, const char* executable);
int gui_monitor_main();
#ifdef SIM_WITH_QT
std::unique_ptr<QWidget> create_gui_window(Runtime& runtime);
#endif
} // namespace sim
