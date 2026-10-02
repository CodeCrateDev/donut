#include <donut/exec.hpp>

#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace donut::exec {

int run(std::span<const std::string> cmd) {
    if (cmd.empty()) return -1;

    std::vector<char*> argv;
    argv.reserve(cmd.size() + 1);
    for (const auto& arg : cmd) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        execvp(argv[0], argv.data());
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

}  // namespace donut::exec
