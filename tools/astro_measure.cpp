// astro_measure: runs a program and records what it cost (Workspace profiler).
// Usage: astro_measure <program> [args...]
// If ASTRO_MEASURE_OUT is set, writes one JSON object there after the program ends:
//   {"wall_ms","user_ms","sys_ms","max_rss_kb","minflt","majflt","nvcsw","nivcsw","exit","signal"}
// Exits with the program's own exit code (128+signal when it was killed), so scripts behave the same.
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: astro_measure <program> [args...]\n"); return 2; }
    const char* out_path = std::getenv("ASTRO_MEASURE_OUT");
    const std::string out = out_path ? out_path : "";
    unsetenv("ASTRO_MEASURE_OUT");   // the measured program should not see it

    const auto start = std::chrono::steady_clock::now();
    const pid_t pid = fork();
    if (pid < 0) { std::perror("astro_measure: fork"); return 127; }
    if (pid == 0) {
        execvp(argv[1], argv + 1);
        std::fprintf(stderr, "astro_measure: cannot run %s: %s\n", argv[1], std::strerror(errno));
        _exit(127);
    }
    int status = 0;
    rusage ru{};
    while (wait4(pid, &status, 0, &ru) < 0) {
        if (errno != EINTR) { std::perror("astro_measure: wait4"); return 127; }
    }
    const long long wall = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    const auto ms = [](const timeval& t) { return static_cast<long long>(t.tv_sec) * 1000 + t.tv_usec / 1000; };
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const int sig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;

    if (!out.empty()) {
        if (FILE* f = std::fopen(out.c_str(), "w")) {
            std::fprintf(f, "{\"wall_ms\":%lld,\"user_ms\":%lld,\"sys_ms\":%lld,\"max_rss_kb\":%ld,\"minflt\":%ld,\"majflt\":%ld,\"nvcsw\":%ld,\"nivcsw\":%ld,\"exit\":%d,\"signal\":%d}\n",
                         wall, ms(ru.ru_utime), ms(ru.ru_stime), ru.ru_maxrss, ru.ru_minflt, ru.ru_majflt, ru.ru_nvcsw, ru.ru_nivcsw, code, sig);
            std::fclose(f);
        }
    }
    return sig ? 128 + sig : code;
}
