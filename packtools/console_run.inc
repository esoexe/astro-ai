// console_run.inc - console-tester/run: a C++ port of console-skill/console-tester/run.py (Jake Schincariol, MIT).
// `boot` starts the runner (ps2EntryRunner <ELF>) in its own session (POSIX) or job object (Windows), captures stdout
// and stderr into console/runs/<UTC stamp>.log (the first and last --keep lines when huge), kills the whole process
// tree on timeout, and reports PASS, HANG, CRASH or FAIL, writing console/runs/latest.json. `--input` sets
// PS2X_INPUT_SCRIPT for the controller replay; `soak` repeats a boot N times and sums up; `last` reprints latest.json.
// One deliberate difference: Ctrl-C kills the runner's tree before printing "interrupted" (the original left it running).
#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#include <atomic>
#include <chrono>
#include <csignal>
#include <deque>
#include <thread>

namespace packtools::console_run {
using namespace pyrt;
using namespace packtools::console_common;

struct UsageError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Interrupted {};

const char* const FATAL_MARKERS[] = {
    "Error during program execution:", "[main] fatal exception", "[terminate]", "Failed to initialize PS2 runtime",
    "Failed to initialize PS2 memory", "Failed to load ELF file", "Failed to open ELF file",
};
const std::pair<const char*, const char*> ISSUE_MARKERS[] = {
    {"stub", "Warning: Unimplemented PS2 stub called"}, {"syscall", "Warning: Unimplemented PS2 syscall called"},
    {"missing", "Error: No exact recompiled function"}, {"missing_target", "[guest-branch:missing-target]"},
    {"iop", "[IOP/RPC trace:unhandled]"},
};
constexpr int NKINDS = 5;
const char* const RESULTS[] = {"PASS", "HANG", "CRASH", "FAIL"};

using Clock = std::chrono::steady_clock;
inline double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

inline std::string utc_stamp() {
    const std::time_t t = std::time(nullptr); std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[32]; std::strftime(b, sizeof b, "%Y%m%dT%H%M%SZ", &tm);
    return b;
}
inline std::string unique_log_path(const std::string& out_dir, const std::string& stamp) {
    std::string path = path_join(out_dir, stamp + ".log");
    for (int n = 2; exists(path); ++n) path = path_join(out_dir, cfmt("%s-%d.log", stamp.c_str(), n));
    return path;
}

inline std::string signal_name(long long code) {   // signal.Signals(-code).name, canonical names only
#if !defined(_WIN32)
    static const std::pair<int, const char*> NAMES[] = {
        {SIGHUP, "SIGHUP"}, {SIGINT, "SIGINT"}, {SIGQUIT, "SIGQUIT"}, {SIGILL, "SIGILL"}, {SIGTRAP, "SIGTRAP"}, {SIGABRT, "SIGABRT"},
        {SIGBUS, "SIGBUS"}, {SIGFPE, "SIGFPE"}, {SIGKILL, "SIGKILL"}, {SIGUSR1, "SIGUSR1"}, {SIGSEGV, "SIGSEGV"}, {SIGUSR2, "SIGUSR2"},
        {SIGPIPE, "SIGPIPE"}, {SIGALRM, "SIGALRM"}, {SIGTERM, "SIGTERM"}, {SIGCHLD, "SIGCHLD"}, {SIGCONT, "SIGCONT"}, {SIGSTOP, "SIGSTOP"},
        {SIGTSTP, "SIGTSTP"}, {SIGTTIN, "SIGTTIN"}, {SIGTTOU, "SIGTTOU"}, {SIGURG, "SIGURG"}, {SIGXCPU, "SIGXCPU"}, {SIGXFSZ, "SIGXFSZ"},
        {SIGVTALRM, "SIGVTALRM"}, {SIGPROF, "SIGPROF"}, {SIGWINCH, "SIGWINCH"}, {SIGIO, "SIGIO"}, {SIGSYS, "SIGSYS"},
#  if defined(SIGSTKFLT)
        {SIGSTKFLT, "SIGSTKFLT"},
#  endif
#  if defined(SIGPWR)
        {SIGPWR, "SIGPWR"},
#  endif
#  if defined(SIGEMT)
        {SIGEMT, "SIGEMT"},
#  endif
#  if defined(SIGINFO) && (!defined(SIGPWR) || SIGINFO != SIGPWR)
        {SIGINFO, "SIGINFO"},
#  endif
    };
    for (const auto& [n, name] : NAMES) if (n == -code) return name;
#  if defined(SIGRTMIN)
    if (-code == SIGRTMIN) return "SIGRTMIN";
    if (-code == SIGRTMAX) return "SIGRTMAX";
#  endif
#endif
    return cfmt("signal %lld", -code);
}

// ---------------------------------------------------------------- capture
struct Capture {
    long long keep; std::vector<std::string> head; std::deque<std::string> tail; long long total = 0;
    std::vector<std::pair<std::string, const re::Regex*>> expects;   // in order, repeats kept
    std::vector<std::pair<std::string, Val>> seen;                  // one per distinct pattern: None or {line, text}
    Val first_fatal = Val::none(), first_issue = Val::none();
    long long counts[NKINDS] = {0, 0, 0, 0, 0};
    Val replay_loaded = Val::none(), replay_problem = Val::none();
    Clock::time_point last_output = Clock::now();
    bool all_seen = false;
    Capture(long long k, const std::vector<std::string>& pats) : keep(k) {
        for (const auto& p : pats) {
            expects.emplace_back(p, &re::compile(p));
            bool dup = false; for (const auto& s : seen) if (s.first == p) dup = true;
            if (!dup) seen.emplace_back(p, Val::none());
        }
        all_seen = expects.empty();
    }
    Val& seen_of(const std::string& p) { for (auto& s : seen) if (s.first == p) return s.second; throw std::logic_error(p); }
    bool every_seen() const { for (const auto& s : seen) if (s.second.is_none()) return false; return true; }
    void add(const std::string& line) {
        ++total; const long long number = total;
        if (static_cast<long long>(head.size()) < keep) head.push_back(line);
        else { tail.push_back(line); if (static_cast<long long>(tail.size()) > keep) tail.pop_front(); }
        last_output = Clock::now();
        for (const auto& [pattern, rx] : expects) {
            Val& v = seen_of(pattern);
            if (v.is_none() && rx->search(line)) v = dict_of({{"line", Val::integer(number)}, {"text", Val::str(cpslice(line, 0, 300))}});
        }
        if (!expects.empty() && every_seen()) all_seen = true;
        if (first_fatal.is_none()) for (const char* m : FATAL_MARKERS) if (line.find(m) != std::string::npos) { first_fatal = dict_of({{"line", Val::integer(number)}, {"text", Val::str(cpslice(line, 0, 300))}}); break; }
        for (int k = 0; k < NKINDS; ++k) {
            if (line.find(ISSUE_MARKERS[k].second) == std::string::npos) continue;
            ++counts[k];
            if (first_issue.is_none()) first_issue = dict_of({{"line", Val::integer(number)}, {"kind", Val::str(ISSUE_MARKERS[k].first)}, {"text", Val::str(cpslice(line, 0, 300))}});
        }
        if (line.find("[pad-replay] loaded") != std::string::npos) replay_loaded = Val::str(strip(line));
        else if (line.find("[pad-replay]") != std::string::npos && (line.find("replay disabled") != std::string::npos || line.find("cannot open") != std::string::npos))
            replay_problem = Val::str(strip(line));
    }
    std::pair<std::vector<std::string>, long long> lines() const {
        const long long omitted = total - static_cast<long long>(head.size()) - static_cast<long long>(tail.size());
        std::vector<std::string> o(head.begin(), head.end());
        if (omitted > 0) o.push_back(cfmt("[run.py] ... %lld lines omitted (kept the first %zu and the last %zu) ...", omitted, head.size(), tail.size()));
        o.insert(o.end(), tail.begin(), tail.end());
        return {o, std::max(omitted, 0LL)};
    }
    Val counts_dict() const { Val d = Val::dict(); for (int k = 0; k < NKINDS; ++k) d.d->set(ISSUE_MARKERS[k].first, Val::integer(counts[k])); return d; }
};

inline std::pair<std::string, std::string> classify(bool timed_out, bool stopped_on_pass, std::optional<long long> returncode, Capture& c, bool expects_given) {
    const bool ended_itself = !(timed_out || stopped_on_pass);
    const bool all_seen = c.every_seen();
    auto never = [&] { std::vector<std::string> m; for (const auto& s : c.seen) if (s.second.is_none()) m.push_back(s.first); return join(m, ", "); };
    if (!c.first_fatal.is_none()) return {"CRASH", cfmt("fatal line %lld: ", get(c.first_fatal, "line").i) + get(c.first_fatal, "text").s};
    if (ended_itself && returncode && *returncode != 0) {
        if (*returncode < 0) return {"CRASH", "killed by " + signal_name(*returncode)};
        return {"CRASH", cfmt("exited with code %lld", *returncode)};
    }
    if (ended_itself && (c.counts[2] || c.counts[3])) return {"CRASH", "stopped after jumping to a function the recompiler never emitted"};
    if (expects_given && all_seen) return {"PASS", "every expected line appeared"};
    if (!expects_given && !ended_itself) return {"PASS", "ran the whole timeout without a crash"};
    if (timed_out) return {"HANG", "timeout with no crash; never saw: " + never()};
    if (expects_given) return {"FAIL", "exited by itself (code 0) without: " + never()};
    return {"FAIL", "exited by itself (code 0) before the timeout"};
}

// ---------------------------------------------------------------- the child process
inline volatile std::sig_atomic_t interrupted = 0;
#if !defined(_WIN32)
inline void on_sigint(int) { interrupted = 1; }
struct Child {
    pid_t pid = -1; int fd = -1; bool reaped = false; int status = 0;
    void start(const std::vector<std::string>& cmd, const std::vector<std::string>& envv, const std::optional<std::string>& cwd, const std::string& runner) {
        int po[2], px[2];
        if (pipe(po) != 0) throw UsageError("cannot start " + runner + ": " + OSError(errno, cmd[0]).what());
        if (pipe(px) != 0) { const int e = errno; close(po[0]); close(po[1]); throw UsageError("cannot start " + runner + ": " + OSError(e, cmd[0]).what()); }
        for (int f : {po[0], px[0], px[1]}) fcntl(f, F_SETFD, FD_CLOEXEC);
        std::vector<char*> av, ev;
        for (const auto& s : cmd) av.push_back(const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        for (const auto& s : envv) ev.push_back(const_cast<char*>(s.c_str()));
        ev.push_back(nullptr);
        pid = fork();
        if (pid < 0) { const int e = errno; close(po[0]); close(po[1]); close(px[0]); close(px[1]); throw UsageError("cannot start " + runner + ": " + OSError(e, cmd[0]).what()); }
        if (pid == 0) {
            setsid();
            signal(SIGINT, SIG_DFL); signal(SIGPIPE, SIG_DFL);
            const int nul = open("/dev/null", O_RDONLY);
            if (nul >= 0) dup2(nul, 0);
            dup2(po[1], 1); dup2(po[1], 2);
            int msg[2] = {0, 0};
            if (cwd && chdir(cwd->c_str()) != 0) msg[0] = 1;
            else { execve(av[0], av.data(), ev.data()); msg[0] = 2; }
            msg[1] = errno;
            ssize_t w = write(px[1], msg, sizeof msg); (void)w;
            _exit(255);
        }
        close(po[1]); close(px[1]);
        int msg[2] = {0, 0}; ssize_t n;
        do { n = read(px[0], msg, sizeof msg); } while (n < 0 && errno == EINTR);
        close(px[0]);
        if (n == static_cast<ssize_t>(sizeof msg)) {
            int st = 0; waitpid(pid, &st, 0); close(po[0]);
            throw UsageError("cannot start " + runner + ": " + OSError(msg[1], msg[0] == 1 ? *cwd : cmd[0]).what());
        }
        fd = po[0];
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    }
    // wait up to `ms` for output; false once the pipe is at EOF
    template <class F> bool read_some(int ms, F&& feed) {
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, ms) <= 0) return true;
        char buf[65536];
        const ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) { feed(buf, static_cast<std::size_t>(n)); return true; }
        if (n == 0) return false;
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
    bool exited() {
        if (reaped) return true;
        if (waitpid(pid, &status, WNOHANG) == pid) reaped = true;
        return reaped;
    }
    void kill_tree() {
        killpg(pid, SIGTERM);
        const auto t0 = Clock::now();
        while (!exited() && since(t0) < 2.0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        killpg(pid, SIGKILL);
    }
    long long wait() {
        while (!reaped) { if (waitpid(pid, &status, 0) == pid) reaped = true; else if (errno != EINTR) break; }
        return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? -WTERMSIG(status) : -1;
    }
    void kill_rest() { killpg(pid, SIGKILL); }   // anything the runner left behind
    void close_pipe() { if (fd >= 0) close(fd); fd = -1; }
};
struct SigGuard {   // Ctrl-C stops the run (and its tree) instead of the tool
    struct sigaction old{};
    SigGuard() { struct sigaction sa{}; sa.sa_handler = on_sigint; sigemptyset(&sa.sa_mask); sigaction(SIGINT, &sa, &old); }
    ~SigGuard() { sigaction(SIGINT, &old, nullptr); }
};
inline std::vector<std::string> child_env(const std::string& script, const std::string& record) {
    std::vector<std::string> v;
    for (char** e = environ; e && *e; ++e) {
        const std::string kv = *e;
        if (kv.rfind("PS2X_INPUT_SCRIPT=", 0) == 0 || kv.rfind("PS2X_INPUT_RECORD=", 0) == 0) continue;
        v.push_back(kv);
    }
    if (!script.empty()) v.push_back("PS2X_INPUT_SCRIPT=" + script);
    if (!record.empty()) v.push_back("PS2X_INPUT_RECORD=" + record);
    return v;
}
#else
inline BOOL WINAPI on_ctrl(DWORD t) { if (t == CTRL_C_EVENT || t == CTRL_BREAK_EVENT) { interrupted = 1; return TRUE; } return FALSE; }
struct SigGuard { SigGuard() { SetConsoleCtrlHandler(on_ctrl, TRUE); } ~SigGuard() { SetConsoleCtrlHandler(on_ctrl, FALSE); } };
inline std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
inline std::wstring quote_arg(const std::wstring& a) {   // subprocess.list2cmdline
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring o = L"\""; std::size_t bs = 0;
    for (const wchar_t c : a) {
        if (c == L'\\') { ++bs; continue; }
        if (c == L'"') { o.append(bs * 2 + 1, L'\\'); o += L'"'; bs = 0; continue; }
        o.append(bs, L'\\'); bs = 0; o += c;
    }
    o.append(bs * 2, L'\\'); o += L'"';
    return o;
}
inline std::string win_error(DWORD e) {   // str(OSError) for a WinError
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::string s;
    if (msg) { const int n = WideCharToMultiByte(CP_UTF8, 0, msg, -1, nullptr, 0, nullptr, nullptr); s.resize(n > 0 ? static_cast<std::size_t>(n - 1) : 0); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, msg, -1, s.data(), n, nullptr, nullptr); LocalFree(msg); }
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return cfmt("[WinError %lu] ", static_cast<unsigned long>(e)) + s;
}
struct Child {
    HANDLE proc = nullptr, job = nullptr, rd = nullptr;
    void start(const std::vector<std::string>& cmd, const std::vector<std::string>& envv, const std::optional<std::string>& cwd, const std::string& runner) {
        // envv holds the two replay variables: "" removes one from what the runner inherits
        SetEnvironmentVariableW(L"PS2X_INPUT_SCRIPT", envv[0].empty() ? nullptr : wide(envv[0]).c_str());
        SetEnvironmentVariableW(L"PS2X_INPUT_RECORD", envv[1].empty() ? nullptr : wide(envv[1]).c_str());
        job = CreateJobObjectW(nullptr, nullptr);
        if (job) { JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{}; li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li); }
        SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
        HANDLE wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0)) throw UsageError("cannot start " + runner + ": " + win_error(GetLastError()));
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul; si.hStdOutput = wr; si.hStdError = wr;
        std::wstring line; for (const auto& a : cmd) { if (!line.empty()) line += L' '; line += quote_arg(wide(a)); }
        const std::wstring wcwd = cwd ? wide(*cwd) : std::wstring();
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                       nullptr, cwd ? wcwd.c_str() : nullptr, &si, &pi);
        const DWORD err = ok ? 0 : GetLastError();
        CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!ok) { CloseHandle(rd); rd = nullptr; throw UsageError("cannot start " + runner + ": " + win_error(err)); }
        if (job) AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread); CloseHandle(pi.hThread);
        proc = pi.hProcess;
    }
    template <class F> bool read_some(int ms, F&& feed) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail == 0) { Sleep(static_cast<DWORD>(std::min(ms, 10))); return true; }
        char buf[65536]; DWORD got = 0;
        if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr)) return false;
        if (got) feed(buf, static_cast<std::size_t>(got));
        return true;
    }
    bool exited() { return WaitForSingleObject(proc, 0) == WAIT_OBJECT_0; }
    void kill_tree() { if (job) TerminateJobObject(job, 1); else TerminateProcess(proc, 1); }   // taskkill /T /F: exit code 1
    long long wait() { WaitForSingleObject(proc, INFINITE); DWORD c = 1; GetExitCodeProcess(proc, &c); return static_cast<long long>(c); }
    void kill_rest() { if (job) TerminateJobObject(job, 1); }
    void close_pipe() { if (rd) CloseHandle(rd); rd = nullptr; if (proc) CloseHandle(proc); proc = nullptr; if (job) CloseHandle(job); job = nullptr; }
};
inline std::vector<std::string> child_env(const std::string& script, const std::string& record) { return {script, record}; }
#endif

// ---------------------------------------------------------------- one run
struct Opts {
    std::string runner, elf; double seconds = 15.0; std::vector<std::string> expects; std::string input, record, out_dir;
    std::vector<std::string> extra; std::optional<std::string> cwd; long long keep = 2000; bool until_expect = false;
};
struct RunResult { Val summary; std::vector<std::string> tail; };

inline void write_text(const std::string& path, const std::string& text) {
    std::FILE* f = open_file(path, "wb");
    if (!f) throw OSError(errno, path);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

inline RunResult run_once(const Opts& o, const Val& label) {
    makedirs(o.out_dir);
    const std::string stamp = utc_stamp();
    const std::string log_path = unique_log_path(o.out_dir, stamp);
    const std::string script = o.input.empty() ? "" : abspath(o.input), record = o.record.empty() ? "" : abspath(o.record);
    std::vector<std::string> cmd{abspath(o.runner), abspath(o.elf)};
    cmd.insert(cmd.end(), o.extra.begin(), o.extra.end());
    Capture cap(o.keep, o.expects);
    const auto started = Clock::now();
    Child child;
    child.start(cmd, child_env(script, record), o.cwd, o.runner);
    SigGuard guard;
    std::string pend;
    auto feed = [&](const char* b, std::size_t n) {
        pend.append(b, n);
        std::size_t a = 0, nl;
        while ((nl = pend.find('\n', a)) != std::string::npos) {
            std::string line = decode_utf8_replace(pend.substr(a, nl - a));
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            cap.add(line);
            a = nl + 1;
        }
        pend.erase(0, a);
    };
    bool timed_out = false, stopped_on_pass = false, eof = false;
    while (true) {
        if (interrupted) break;
        if (child.exited()) break;
        const double now = since(started);
        if (now >= o.seconds) { timed_out = true; break; }
        if (o.until_expect && !cap.expects.empty() && cap.all_seen) { stopped_on_pass = true; break; }
        const int pause = static_cast<int>(std::ceil(std::min(0.05, std::max(0.0, o.seconds - now)) * 1000.0));
        if (!eof) eof = !child.read_some(pause, feed);
        else std::this_thread::sleep_for(std::chrono::milliseconds(pause));
    }
    if (interrupted) { child.kill_tree(); child.wait(); child.kill_rest(); child.close_pipe(); interrupted = 0; throw Interrupted{}; }
    if (timed_out || stopped_on_pass) child.kill_tree();
    const long long returncode = child.wait();
    child.kill_rest();
    {   // the reader's last lines (reader.join(timeout=5))
        const auto t0 = Clock::now();
        while (!eof && since(t0) < 5.0) eof = !child.read_some(50, feed);
        if (!pend.empty()) { std::string line = decode_utf8_replace(pend); while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back(); cap.add(line); pend.clear(); }
    }
    child.close_pipe();
    const double elapsed = since(started);

    const auto [lines, omitted] = cap.lines();
    std::string text; for (const auto& l : lines) { text += l; text += '\n'; }
    write_text(log_path, text);
    write_text(path_join(o.out_dir, "latest.log"), text);

    const bool killed = timed_out || stopped_on_pass;
    const auto [result, reason] = classify(timed_out, stopped_on_pass, returncode, cap, !o.expects.empty());
    List extra; for (const auto& a : o.extra) extra.push_back(Val::str(a));
    List expects;
    for (const auto& [p, rx] : cap.expects) {
        (void)rx; const Val& v = cap.seen_of(p);
        expects.push_back(dict_of({{"pattern", Val::str(p)}, {"seen", Val::boolean(!v.is_none())},
            {"line", v.is_none() ? Val::none() : get(v, "line")}, {"text", v.is_none() ? Val::none() : get(v, "text")}}));
    }
    Val s = dict_of({{"result", Val::str(result)}, {"reason", Val::str(reason)}, {"label", label}, {"stamp", Val::str(stamp)},
        {"runner", Val::str(cmd[0])}, {"elf", Val::str(cmd[1])}, {"args", Val::list(extra)}, {"seconds", Val::flt(o.seconds)},
        {"elapsed", Val::flt(round_n(elapsed, 3))}, {"timed_out", Val::boolean(timed_out)}, {"stopped_on_pass", Val::boolean(stopped_on_pass)},
        {"exit_code", killed ? Val::none() : Val::integer(returncode)},
        {"signal", returncode < 0 && !killed ? Val::str(signal_name(returncode)) : Val::none()},
        {"expects", Val::list(expects)}, {"first_fatal", cap.first_fatal}, {"first_issue", cap.first_issue}, {"counts", cap.counts_dict()},
        {"input", script.empty() ? Val::none() : Val::str(script)}, {"replay", cap.replay_loaded}, {"replay_problem", cap.replay_problem},
        {"record", record.empty() ? Val::none() : Val::str(record)}, {"log", Val::str(abspath(log_path))},
        {"lines", Val::integer(cap.total)}, {"omitted", Val::integer(omitted)},
        {"silent_for", timed_out ? Val::flt(round_n(since(cap.last_output), 3)) : Val::none()}});
    List notes;
    if (!script.empty() && cap.replay_loaded.is_none())
        notes.push_back(Val::str("--input was given but the pad never said '[pad-replay] loaded': the runtime's "
                                 "ps2_pad.cpp was not generated with padmap.py gen --replay, or the game never read the pad"));
    if (!cap.replay_problem.is_none()) notes.push_back(cap.replay_problem);
    s.d->set("notes", Val::list(notes));
    write_text(path_join(o.out_dir, "latest.json"), dumps2(s) + "\n");
    const std::size_t from = lines.size() > 200 ? lines.size() - 200 : 0;
    return {s, std::vector<std::string>(lines.begin() + static_cast<long>(from), lines.end())};
}

// ---------------------------------------------------------------- output
inline std::string tail_text(const std::vector<std::string>& lines, long long chars) {
    std::string text = join(lines, "\n");
    const long long n = static_cast<long long>(cps(text).size());
    if (n > chars) {
        text = cpslice(text, n - chars, n);
        const auto nl = text.find('\n');
        if (nl != std::string::npos) text = text.substr(nl + 1);
    }
    return text;
}
inline double num(const Val& v) { return v.t == Val::FLOAT ? v.f : static_cast<double>(v.i); }
inline void print_summary(const Val& s, long long tail_chars, const std::vector<std::string>& tail) {
    out(pystr(get(s, "result")) + "  " + pystr(get(s, "reason")));
    out("  runner  " + pystr(get(s, "runner")) + " " + pystr(get(s, "elf")));
    const bool killed = truthy(get(s, "timed_out")) || truthy(get(s, "stopped_on_pass"));
    out("  run     " + (killed ? cfmt("killed by run.py after %.1f s", num(get(s, "elapsed")))
                               : "exit code " + pystr(truthy(get(s, "signal")) ? get(s, "signal") : get(s, "exit_code")) + cfmt(" after %.1f s", num(get(s, "elapsed")))));
    if (get(s, "expects").t == Val::LIST)
        for (const auto& e : *get(s, "expects").l) {
            const bool seen = truthy(get(e, "seen"));
            out(cfmt("  expect  %-4s ", seen ? "yes" : "NO") + pystr(get(e, "pattern")) + (seen ? cfmt("  (line %lld)", get(e, "line").i) : std::string()));
        }
    const Val c = get(s, "counts");
    out(cfmt("  issues  stubs %lld, syscalls %lld, missing functions %lld, IOP %lld", get(c, "stub").i, get(c, "syscall").i,
             get(c, "missing").i + get(c, "missing_target").i, get(c, "iop").i));
    if (truthy(get(s, "first_fatal"))) out(cfmt("  fatal   line %lld: ", get(get(s, "first_fatal"), "line").i) + pystr(get(get(s, "first_fatal"), "text")));
    if (truthy(get(s, "input"))) out("  input   " + pystr(get(s, "input")) + " (" + (truthy(get(s, "replay")) ? pystr(get(s, "replay")) : std::string("replay not confirmed")) + ")");
    if (get(s, "notes").t == Val::LIST) for (const auto& n : *get(s, "notes").l) out("  note    " + pystr(n));
    out("  log     " + pystr(get(s, "log")) + cfmt(" (%lld lines", get(s, "lines").i) + (truthy(get(s, "omitted")) ? cfmt(", %lld omitted", get(s, "omitted").i) : std::string()) + ")");
    if (tail_chars > 0 && !tail.empty()) {
        out(cfmt("  ---- last %lld characters ----", tail_chars));
        out(tail_text(tail, tail_chars));
    }
}

// ---------------------------------------------------------------- commands
inline Opts opts_of(const Val& a) {
    Opts o;
    o.runner = S(a, "runner"); o.elf = S(a, "elf"); o.seconds = get(a, "seconds").f;
    for (const auto& v : *get(a, "expect").l) o.expects.push_back(v.s);
    o.input = get(a, "input").is_none() ? "" : S(a, "input"); o.record = get(a, "record").is_none() ? "" : S(a, "record");
    o.out_dir = S(a, "out");
    for (const auto& v : *get(a, "arg").l) o.extra.push_back(v.s);
    if (!get(a, "cwd").is_none()) o.cwd = S(a, "cwd");
    o.keep = get(a, "keep").i; o.until_expect = B(a, "until_expect");
    return o;
}
inline void validate(const Opts& o) {
    if (!is_file(o.runner)) throw UsageError("runner not found: " + o.runner + " (build it first; see /console-anywhere plan)");
#if !defined(_WIN32)
    if (access(o.runner.c_str(), X_OK) != 0) throw UsageError("runner is not executable: " + o.runner);
#endif
    if (!is_file(o.elf)) throw UsageError("ELF not found: " + o.elf + " (the game's main ELF in your extracted disc folder)");
    if (!(o.seconds > 0)) throw UsageError("--seconds must be above 0");
    for (const auto& p : o.expects) {
        try { re::compile(p); }
        catch (const PyErr& e) { throw UsageError("--expect " + str_repr(p) + " is not a valid regex: " + e.what()); }
    }
    if (!o.input.empty() && !is_file(o.input)) throw UsageError("input script not found: " + o.input);
    if (!o.input.empty() && !o.record.empty()) throw UsageError("use --input or --record, not both");
    if (o.keep < 10) throw UsageError("--keep must be at least 10");
}

inline int cmd_boot(const Val& a) {
    const Opts o = opts_of(a);
    validate(o);
    const RunResult r = run_once(o, Val::none());
    if (B(a, "json")) out(dumps2(r.summary));
    else print_summary(r.summary, get(a, "tail_chars").i, r.tail);
    return get(r.summary, "result").s == "PASS" ? 0 : 1;
}

inline int cmd_soak(const Val& a) {
    const Opts o = opts_of(a);
    validate(o);
    const long long want = get(a, "runs").i;
    if (want < 1) throw UsageError("--runs must be at least 1");
    std::vector<Val> runs;
    for (long long i = 0; i < want; ++i) {
        const Val s = run_once(o, Val::str(cfmt("soak %lld/%lld", i + 1, want))).summary;
        runs.push_back(s);
        if (!B(a, "json")) out(cfmt("run %3lld/%lld  %-5s %5.1f s  ", i + 1, want, get(s, "result").s.c_str(), get(s, "elapsed").f) + cpslice(get(s, "reason").s, 0, 100));
        if (B(a, "stop_on_crash") && get(s, "result").s == "CRASH") break;
    }
    Val totals = Val::dict(); std::set<std::string> kinds;
    for (const char* r : RESULTS) { long long n = 0; for (const auto& s : runs) if (get(s, "result").s == r) ++n; totals.d->set(r, Val::integer(n)); }
    for (const auto& s : runs) kinds.insert(get(s, "result").s);
    std::vector<std::pair<std::string, long long>> fatals;   // Counter in first-seen order, then most_common
    for (const auto& s : runs) {
        if (!truthy(get(s, "first_fatal"))) continue;
        const std::string t = get(get(s, "first_fatal"), "text").s; bool hit = false;
        for (auto& f : fatals) if (f.first == t) { ++f.second; hit = true; }
        if (!hit) fatals.emplace_back(t, 1);
    }
    std::stable_sort(fatals.begin(), fatals.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
    double mn = get(runs[0], "elapsed").f, mx = mn;
    for (const auto& s : runs) { mn = std::min(mn, get(s, "elapsed").f); mx = std::max(mx, get(s, "elapsed").f); }
    List fl; for (const auto& [t, n] : fatals) fl.push_back(dict_of({{"text", Val::str(t)}, {"count", Val::integer(n)}}));
    List results;
    for (const auto& s : runs) results.push_back(dict_of({{"stamp", get(s, "stamp")}, {"result", get(s, "result")}, {"reason", get(s, "reason")}, {"elapsed", get(s, "elapsed")}, {"log", get(s, "log")}}));
    Val summary = dict_of({{"runs", Val::integer(static_cast<long long>(runs.size()))}, {"requested", Val::integer(want)}, {"totals", totals},
        {"flaky", Val::boolean(kinds.size() > 1)}, {"elapsed_min", Val::flt(mn)}, {"elapsed_max", Val::flt(mx)},
        {"fatal_lines", Val::list(fl)}, {"results", Val::list(results)}});
    const std::string path = path_join(o.out_dir, "soak-" + utc_stamp() + ".json");
    write_text(path, dumps2(summary) + "\n");
    summary.d->set("file", Val::str(abspath(path)));
    const long long pass = get(totals, "PASS").i;
    if (B(a, "json")) out(dumps2(summary));
    else {
        out("");
        std::vector<std::string> parts; for (const char* r : RESULTS) parts.push_back(cfmt("%s %lld", r, get(totals, r).i));
        out(cfmt("soak: %zu run(s): ", runs.size()) + join(parts, ", "));
        if (kinds.size() > 1)
            out("  FLAKY: the same boot gave different results. Timing-dependent bugs (threads, vsync, IOP) show up like this; triage the odd run's log first.");
        for (const auto& [t, n] : fatals) out(cfmt("  x%lld  ", n) + cpslice(t, 0, 140));
        out("  summary " + abspath(path));
        out("  Same boot, same input, many times: it finds crashes that come and go. It does not play the game for you.");
    }
    return pass == static_cast<long long>(runs.size()) && pass == want ? 0 : 1;
}

// UnicodeDecodeError text for bytes that are not UTF-8 (strict decoding, as open(..., encoding="utf-8") reads)
inline std::optional<std::string> utf8_error(const std::string& b) {
    const std::size_t n = b.size();
    auto byte = [&](std::size_t i) { return static_cast<unsigned char>(b[i]); };
    for (std::size_t i = 0; i < n;) {
        const unsigned c = byte(i);
        if (c < 0x80) { ++i; continue; }
        int need; unsigned lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else return cfmt("'utf-8' codec can't decode byte 0x%02x in position %zu: invalid start byte", c, i);
        std::size_t k = 1;
        for (; k <= static_cast<std::size_t>(need); ++k) {
            if (i + k >= n) {
                return k == 1 ? cfmt("'utf-8' codec can't decode byte 0x%02x in position %zu: unexpected end of data", c, i)
                              : cfmt("'utf-8' codec can't decode bytes in position %zu-%zu: unexpected end of data", i, i + k - 1);
            }
            const unsigned d = byte(i + k);
            const bool ok = k == 1 ? (d >= lo && d <= hi) : (d >= 0x80 && d <= 0xBF);
            if (!ok) {
                return k == 1 ? cfmt("'utf-8' codec can't decode byte 0x%02x in position %zu: invalid continuation byte", c, i)
                              : cfmt("'utf-8' codec can't decode bytes in position %zu-%zu: invalid continuation byte", i, i + k - 1);
            }
        }
        i += k;
    }
    return std::nullopt;
}

inline int cmd_last(const Val& a) {
    const std::string path = path_join(S(a, "out"), "latest.json");
    std::FILE* f = is_dir(path) ? nullptr : open_file(path, "rb");
    if (!f) {
        const int e = is_dir(path) ? EISDIR : errno;
        if (e == ENOENT) throw UsageError("no " + path + " yet: run boot first");
        throw UsageError("cannot read " + path + ": " + OSError(e, path).what());
    }
    std::string raw; char buf[65536]; std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.append(buf, n);
    std::fclose(f);
    if (const auto bad = utf8_error(raw)) throw UsageError("cannot read " + path + ": " + *bad);
    Val s;
    try { s = loads(raw); }
    catch (const PyErr& e) { throw UsageError("cannot read " + path + ": " + e.what()); }
    if (B(a, "json")) out(dumps2(s));
    if (s.t != Val::DICT) throw PyErr("AttributeError", "'" + std::string(s.t == Val::LIST ? "list" : s.t == Val::STR ? "str" : "object") + "' object has no attribute 'get'");
    if (!B(a, "json")) print_summary(s, 0, {});
    const Val r = get(s, "result");
    return r.t == Val::STR && r.s == "PASS" ? 0 : 1;
}

inline void common(Parser& sp) {
    sp.opt({"--runner"}).required().help("ps2EntryRunner (from your build folder)");
    sp.opt({"--elf"}).required().help("the game's main ELF in your extracted disc folder");
    sp.opt({"--seconds"}).number().def_float(15.0).help("hard timeout (default 15)");
    sp.opt({"--expect"}).append().def(Val::list({})).metavar("REGEX").help("a log line that must appear (repeatable)");
    sp.opt({"--until-expect"}).store_true().help("stop as soon as every --expect has appeared");
    sp.opt({"--input"}).help("replay script for the pad (sets PS2X_INPUT_SCRIPT)");
    sp.opt({"--record"}).help("record live pad input to this file (sets PS2X_INPUT_RECORD)");
    sp.opt({"--out"}).def(Val::str(path_join("console", "runs"))).help("log folder (default console/runs)");
    sp.opt({"--arg"}).append().def(Val::list({})).help("extra argument for the runner");
    sp.opt({"--cwd"}).help("working folder for the runner");
    sp.opt({"--keep"}).integer().def(2000).help("lines kept at each end of a huge log");
    sp.opt({"--json"}).store_true();
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("run", "Run a PS2Recomp port with a hard timeout, capture the log, judge the run.");
    p.epilog = "Results: PASS (every --expect seen, or no crash for the whole timeout), HANG (timeout before the expected lines), "
               "CRASH (fatal line, non-zero exit or signal), FAIL (exited by itself without the expected lines). Exit 0 only on PASS.";
    p.subcommands("cmd", false); p.sub_metavar = "command";
    { Parser& s = p.sub("boot"); s.help_line = "one run with a hard timeout"; common(s);
      s.opt({"--tail-chars"}).integer().def(5000).help("characters of the log tail to print"); }
    { Parser& s = p.sub("soak"); s.help_line = "the same boot N times, summed up"; common(s);
      s.opt({"--runs"}).integer().def(10); s.opt({"--stop-on-crash"}).store_true(); }
    { Parser& s = p.sub("last"); s.help_line = "print console/runs/latest.json";
      s.opt({"--out"}).def(Val::str(path_join("console", "runs"))); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "boot") return cmd_boot(a);
        if (cmd == "soak") return cmd_soak(a);
        return cmd_last(a);
    } catch (const UsageError& e) {
        flush(); std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    } catch (const Interrupted&) {
        std::fflush(stdout); std::fprintf(stderr, "interrupted\n");
        return 1;
    }
}
PACKTOOL("console-tester/run", main);
}  // namespace packtools::console_run
