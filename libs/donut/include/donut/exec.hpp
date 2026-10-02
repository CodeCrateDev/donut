#pragma once

#include <span>
#include <string>

namespace donut::exec {

// Runs cmd[0] with the given argument vector (cmd[0] is the program name,
// no shell is involved). Returns the command's exit status, or -1 if the
// command could not be started or terminated abnormally.
int run(std::span<const std::string> cmd);

}  // namespace donut::exec
