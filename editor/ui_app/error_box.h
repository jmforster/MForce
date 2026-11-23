#pragma once
#include <string>

namespace mforce::ui {

// Shows `message` under `title` and waits until it is dismissed: a message
// box on Windows, where the program has no console; the error stream elsewhere.
void show_error_box(const std::string& title, const std::string& message);

}  // namespace mforce::ui
