#pragma once

#include <atomic>
#include "runtime.hpp"

#ifdef SIM_WITH_QT
#include <memory>
class QWidget;
#endif

namespace sim {
// The CLI only posts an atomic request; all Qt objects live on the main thread.
void request_gui();
void gui_event_loop(Runtime& runtime, const std::atomic<bool>& cli_finished);
#ifdef SIM_WITH_QT
std::unique_ptr<QWidget> create_gui_window(Runtime& runtime);
#endif
} // namespace sim
