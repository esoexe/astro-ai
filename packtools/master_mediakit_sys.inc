// master_mediakit_sys.inc - the system layer under the edit skill's media kit (master-skills edit/tools/mediakit.py):
// running ffmpeg/ffprobe with stdout and stderr kept apart (subprocess.run(capture_output=True) and Popen streaming),
// shutil.which, os.path.abspath, os.stat().st_mtime, fractions.Fraction (the parts the tools use), random.Random
// (Mersenne Twister seeded like CPython, so a --seed gives the same wobble as the original) and shlex.join.
#if !defined(_WIN32)
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <atomic>
#include <exception>
#include <functional>
#include <thread>
#include <numeric>

namespace packtools::master_mk {
using namespace pyrt;

// ------------------------------------------------------------------ subprocess
struct Proc {
    int code = -1;               // returncode (negative signal number when killed, like Python)
    std::string out, err;        // raw bytes; text callers decode with errors="replace"
    bool not_found = false;      // FileNotFoundError: the program (or the cwd) does not exist
    int os_errno = 0;            // the OSError errno when the program could not be started
    bool timed_out = false;
};
using OutSink = std::function<void(const char*, std::size_t)>;

#if !defined(_WIN32)
// fork/exec with three pipes: stdout (streamed to `sink` when given), stderr, and a close-on-exec pipe that carries
// the child's errno when chdir/exec fails (so "cannot run" is told apart from "ran and failed").
inline Proc run_proc(const std::vector<std::string>& args, const std::string& cwd = "", const OutSink& sink = {}, int timeout_s = 0) {
    Proc r;
    if (args.empty()) { r.not_found = true; r.os_errno = ENOENT; return r; }
    int po[2], pe[2], px[2];
    if (pipe(po) != 0) { r.os_errno = errno; return r; }
    if (pipe(pe) != 0) { r.os_errno = errno; close(po[0]); close(po[1]); return r; }
    if (pipe(px) != 0) { r.os_errno = errno; close(po[0]); close(po[1]); close(pe[0]); close(pe[1]); return r; }
    fcntl(px[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> argv; for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str())); argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0) {
        r.os_errno = errno;
        for (int fd : {po[0], po[1], pe[0], pe[1], px[0], px[1]}) close(fd);
        return r;
    }
    if (pid == 0) {
        close(po[0]); close(pe[0]); close(px[0]);
        dup2(po[1], 1); dup2(pe[1], 2); close(po[1]); close(pe[1]);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); close(devnull); }
        signal(SIGPIPE, SIG_DFL);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) { const int e = errno; if (write(px[1], &e, sizeof e) < 0) {} _exit(127); }
        execvp(argv[0], argv.data());
        const int e = errno; if (write(px[1], &e, sizeof e) < 0) {}
        _exit(127);
    }
    close(po[1]); close(pe[1]); close(px[1]);
    int child_errno = 0;
    {
        int e = 0; ssize_t got;
        while ((got = read(px[0], &e, sizeof e)) < 0 && errno == EINTR) {}
        if (got == static_cast<ssize_t>(sizeof e)) child_errno = e;
        close(px[0]);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s > 0 ? timeout_s : 0);
    bool open_o = true, open_e = true; char buf[65536];
    while (open_o || open_e) {
        pollfd pf[2]; nfds_t n = 0;
        if (open_o) pf[n++] = pollfd{po[0], POLLIN, 0};
        if (open_e) pf[n++] = pollfd{pe[0], POLLIN, 0};
        int wait_ms = 1000;
        if (timeout_s > 0) {
            const long long left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) { r.timed_out = true; break; }
            wait_ms = static_cast<int>(std::min<long long>(left, 1000));
        }
        const int pr = poll(pf, n, wait_ms);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) continue;
        for (nfds_t k = 0; k < n; ++k) {
            if (!(pf[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t got = read(pf[k].fd, buf, sizeof buf);
            if (got < 0 && errno == EINTR) continue;
            const bool is_out = pf[k].fd == po[0];
            if (got <= 0) { (is_out ? open_o : open_e) = false; continue; }
            if (is_out) { if (sink) sink(buf, static_cast<std::size_t>(got)); else r.out.append(buf, static_cast<std::size_t>(got)); }
            else r.err.append(buf, static_cast<std::size_t>(got));
        }
    }
    close(po[0]); close(pe[0]);
    if (r.timed_out) kill(pid, SIGKILL);
    int st = 0; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (child_errno) { r.os_errno = child_errno; r.not_found = child_errno == ENOENT; r.code = -1; return r; }
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? -WTERMSIG(st) : -1;
    return r;
}
#else
inline std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
inline std::wstring win_quote(const std::wstring& a) {   // subprocess.list2cmdline
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
inline Proc run_proc(const std::vector<std::string>& args, const std::string& cwd = "", const OutSink& sink = {}, int timeout_s = 0) {
    Proc r;
    if (args.empty()) { r.not_found = true; r.os_errno = 2; return r; }
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
    HANDLE or_ = nullptr, ow = nullptr, er = nullptr, ew = nullptr;
    if (!CreatePipe(&or_, &ow, &sa, 0)) { r.os_errno = static_cast<int>(GetLastError()); return r; }
    if (!CreatePipe(&er, &ew, &sa, 0)) { r.os_errno = static_cast<int>(GetLastError()); CloseHandle(or_); CloseHandle(ow); return r; }
    SetHandleInformation(or_, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(er, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul; si.hStdOutput = ow; si.hStdError = ew;
    std::wstring cmd; for (const auto& a : args) { if (!cmd.empty()) cmd += L' '; cmd += win_quote(widen(a)); }
    const std::wstring wcwd = widen(cwd);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                   cwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    const DWORD start_err = ok ? 0 : GetLastError();
    CloseHandle(ow); CloseHandle(ew); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(or_); CloseHandle(er);
        r.os_errno = static_cast<int>(start_err);
        r.not_found = start_err == ERROR_FILE_NOT_FOUND || start_err == ERROR_PATH_NOT_FOUND || start_err == ERROR_DIRECTORY;
        return r;
    }
    std::string err_buf;
    std::thread err_reader([&] { char b[65536]; DWORD got = 0; while (ReadFile(er, b, sizeof b, &got, nullptr) && got) err_buf.append(b, got); });
    std::atomic<bool> done{false};
    std::thread killer;
    if (timeout_s > 0) killer = std::thread([&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
        while (!done && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!done) { r.timed_out = true; TerminateProcess(pi.hProcess, 1); }
    });
    { char b[65536]; DWORD got = 0; while (ReadFile(or_, b, sizeof b, &got, nullptr) && got) { if (sink) sink(b, got); else r.out.append(b, got); } }
    WaitForSingleObject(pi.hProcess, INFINITE);
    done = true;
    if (killer.joinable()) killer.join();
    err_reader.join();
    r.err = std::move(err_buf);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); r.code = static_cast<int>(code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(or_); CloseHandle(er);
    return r;
}
#endif

// ------------------------------------------------------------------ paths
inline std::string env(const char* name) { const char* v = std::getenv(name); return v ? std::string(v) : std::string(); }
inline std::string which(const std::string& name) {   // shutil.which
    auto runnable = [](const std::string& p) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(fspath(p), ec)) return false;
#if !defined(_WIN32)
        return access(p.c_str(), X_OK) == 0;
#else
        return true;
#endif
    };
#if defined(_WIN32)
    std::vector<std::string> exts{""};
    { std::string pe = env("PATHEXT"); if (pe.empty()) pe = ".COM;.EXE;.BAT;.CMD"; for (const auto& e : split(pe, ";")) if (!e.empty()) exts.push_back(lower(e)); }
    // os.path.splitext(name)[1] != "" (no master:: dependency: the modder tools share this layer)
    const std::string leaf = name.substr(name.find_last_of("/\\") == std::string::npos ? 0 : name.find_last_of("/\\") + 1);
    const auto dot = leaf.rfind('.');
    const bool has_ext = dot != std::string::npos && leaf.find_first_not_of('.') < dot;
    auto try_one = [&](const std::string& base) -> std::string {
        if (has_ext && runnable(base)) return base;
        for (std::size_t k = 1; k < exts.size(); ++k) if (runnable(base + exts[k])) return base + exts[k];
        return "";
    };
    if (name.find_first_of("/\\") != std::string::npos) return try_one(name);
    for (const auto& dir : split(env("PATH"), ";")) { if (dir.empty()) continue; const std::string hit = try_one(path_join(dir, name)); if (!hit.empty()) return hit; }
    return "";
#else
    if (name.find('/') != std::string::npos) return runnable(name) ? name : "";
    for (const auto& dir : split(env("PATH"), ":")) {
        const std::string p = path_join(dir.empty() ? "." : dir, name);
        if (runnable(p)) return p;
    }
    return "";
#endif
}
inline std::string abspath(const std::string& p) {   // os.path.abspath: absolute and normalized, no trailing separator
    std::error_code ec;
    std::filesystem::path a = std::filesystem::absolute(fspath(p.empty() ? std::string(".") : p), ec);
    std::string s = a.lexically_normal().generic_string();
#if defined(_WIN32)
    s = replace_all(s, "/", "\\");
    while (s.size() > 3 && s.back() == '\\') s.pop_back();
#else
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#endif
    return s;
}
inline double mtime_of(const std::string& p) {   // os.stat(p).st_mtime
#if !defined(_WIN32)
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return 0.0;
#  if defined(__APPLE__)
    return static_cast<double>(st.st_mtimespec.tv_sec) + static_cast<double>(st.st_mtimespec.tv_nsec) * 1e-9;
#  else
    return static_cast<double>(st.st_mtim.tv_sec) + static_cast<double>(st.st_mtim.tv_nsec) * 1e-9;
#  endif
#else
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(widen(p).c_str(), GetFileExInfoStandard, &fa)) return 0.0;
    const unsigned long long t = (static_cast<unsigned long long>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
    const long long secs = static_cast<long long>(t / 10000000ULL) - 11644473600LL; const long long ns = static_cast<long long>(t % 10000000ULL) * 100;
    return static_cast<double>(secs) + static_cast<double>(ns) * 1e-9;
#endif
}
inline long long size_of(const std::string& p) { std::error_code ec; const auto n = std::filesystem::file_size(fspath(p), ec); return ec ? 0 : static_cast<long long>(n); }
inline std::string platform_terse() {   // platform.platform(terse=True), close enough: system-release
#if defined(_WIN32)
    return "Windows";
#else
    struct utsname u{};
    if (uname(&u) != 0) return "unknown";
    return std::string(u.sysname) + "-" + u.release;
#endif
}

// tempfile.TemporaryDirectory(): created now, removed with everything in it when it goes out of scope
struct TempDir {
    std::string path;
    explicit TempDir(const std::string& prefix = "tmp") {
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        const std::string root = ec ? std::string(".") : base.string();
#if !defined(_WIN32)
        std::string tmpl = path_join(root, prefix + "XXXXXX");
        if (mkdtemp(tmpl.data())) { path = tmpl; return; }
#endif
        static std::atomic<unsigned> counter{0};
        for (int k = 0; k < 100 && path.empty(); ++k) {
            const std::string cand = path_join(root, prefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007) + "-" + std::to_string(counter++));
            if (std::filesystem::create_directory(fspath(cand), ec)) path = cand;
        }
        if (path.empty()) throw OSError(ec ? ec.value() : 17, root);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() { std::error_code ec; if (!path.empty()) std::filesystem::remove_all(fspath(path), ec); }
};

// ------------------------------------------------------------------ shlex.join
inline std::string shlex_quote(const std::string& s) {
    if (s.empty()) return "''";
    bool safe = true;
    for (const unsigned char c : s) if (!(std::isalnum(c) || std::strchr("@%+=:,./-_", static_cast<char>(c)))) { safe = false; break; }
    if (safe) return s;
    return "'" + replace_all(s, "'", "'\"'\"'") + "'";
}
inline std::string shlex_join(const std::vector<std::string>& v) { std::vector<std::string> q; for (const auto& s : v) q.push_back(shlex_quote(s)); return join(q, " "); }

// ------------------------------------------------------------------ fractions.Fraction (positive rates, small terms)
struct Frac {
    long long n = 0, d = 1;
    Frac() = default;
    Frac(long long num, long long den = 1) : n(num), d(den) { norm(); }
    void norm() {
        if (d < 0) { n = -n; d = -d; }
        const long long g = std::gcd(n < 0 ? -n : n, d);
        if (g > 1) { n /= g; d /= g; }
    }
    double f() const { return static_cast<double>(n) / static_cast<double>(d); }   // float(Fraction): n / d, correctly rounded for small terms
    std::string str() const { return d == 1 ? std::to_string(n) : std::to_string(n) + "/" + std::to_string(d); }
    bool positive() const { return n > 0; }
};
inline Frac operator-(const Frac& a, const Frac& b) { return Frac(a.n * b.d - b.n * a.d, a.d * b.d); }
inline Frac operator+(const Frac& a, const Frac& b) { return Frac(a.n * b.d + b.n * a.d, a.d * b.d); }
inline Frac operator/(const Frac& a, const Frac& b) { return Frac(a.n * b.d, a.d * b.n); }
inline bool operator<=(const Frac& a, const Frac& b) { return a.n * b.d <= b.n * a.d; }
// Fraction(str(value)) for the forms ffprobe writes ("30000/1001", "30", "29.97"); nullopt for ValueError/ZeroDivisionError
inline std::optional<Frac> parse_frac(const std::string& text) {
    const auto& m = re::fullmatch(R"(\s*([-+]?)(?=\d|\.\d)(\d*)(?:(?:/(\d+))?|(?:\.(\d*))?(?:[eE]([-+]?\d+))?)\s*)", text);
    if (!m.ok) return std::nullopt;
    const std::string sign = m.group(1), num = m.group(2), den = m.group(3), dec = m.group(4), ex = m.group(5);
    auto too_long = [](const std::string& s) { return s.size() > 15; };
    if (too_long(num) || too_long(den) || too_long(dec) || ex.size() > 3) return std::nullopt;
    long long n = num.empty() ? 0 : std::stoll(num), d = 1;
    if (!den.empty()) { d = std::stoll(den); if (d == 0) return std::nullopt; }
    else {
        if (!dec.empty()) { for (std::size_t k = 0; k < dec.size(); ++k) { n = n * 10; d *= 10; } n += std::stoll(dec); }
        if (!ex.empty()) { const int e = std::stoi(ex); for (int k = 0; k < std::abs(e); ++k) { if (e > 0) n *= 10; else d *= 10; } }
    }
    if (sign == "-") n = -n;
    return Frac(n, d);
}
inline Frac limit_denominator(const Frac& x, long long max_d) {   // Fraction.limit_denominator (CPython 3.12+ tie rule)
    if (x.d <= max_d) return x;
    long long p0 = 0, q0 = 1, p1 = 1, q1 = 0, n = x.n, d = x.d;
    for (;;) {
        const long long a = n / d, q2 = q0 + a * q1;
        if (q2 > max_d) break;
        const long long np1 = p0 + a * p1;
        p0 = p1; q0 = q1; p1 = np1; q1 = q2;
        const long long nd = n - a * d; n = d; d = nd;
    }
    const long long k = (max_d - q0) / q1;
    if (2 * d * (q0 + k * q1) <= x.d) return Frac(p1, q1);
    return Frac(p0 + k * p1, q0 + k * q1);
}

// ------------------------------------------------------------------ random.Random (MT19937, CPython seeding)
struct PyRandom {
    uint32_t mt[624]; int mti = 625;
    explicit PyRandom(long long seed) {
        unsigned long long a = static_cast<unsigned long long>(seed < 0 ? -seed : seed);
        std::vector<uint32_t> key;
        if (a == 0) key.push_back(0);
        while (a) { key.push_back(static_cast<uint32_t>(a & 0xffffffffULL)); a >>= 32; }
        init_by_array(key);
    }
    void init_genrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < 624; mti++) mt[mti] = 1812433253U * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + static_cast<uint32_t>(mti);
    }
    void init_by_array(const std::vector<uint32_t>& key) {
        init_genrand(19650218U);
        std::size_t i = 1, j = 0; const std::size_t len = key.size();
        for (std::size_t k = std::max<std::size_t>(624, len); k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<uint32_t>(j);
            i++; j++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
            if (j >= len) j = 0;
        }
        for (std::size_t k = 623; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - static_cast<uint32_t>(i);
            i++;
            if (i >= 624) { mt[0] = mt[623]; i = 1; }
        }
        mt[0] = 0x80000000U;
    }
    uint32_t genrand() {
        static const uint32_t mag01[2] = {0x0U, 0x9908b0dfU};
        uint32_t y;
        if (mti >= 624) {
            int kk = 0;
            for (; kk < 624 - 397; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            for (; kk < 623; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            y = (mt[623] & 0x80000000U) | (mt[0] & 0x7fffffffU); mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 0x1U];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11); y ^= (y << 7) & 0x9d2c5680U; y ^= (y << 15) & 0xefc60000U; y ^= (y >> 18);
        return y;
    }
    double random() { const uint32_t a = genrand() >> 5, b = genrand() >> 6; return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0); }
    double uniform(double a, double b) { return a + (b - a) * random(); }
};
}  // namespace packtools::master_mk
