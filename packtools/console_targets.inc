// console_targets.inc - console-anywhere/targets: a C++ port of console-skill/console-anywhere/targets.py (Jake Schincariol, MIT).
// Builds and packages a PS2Recomp port for other devices: doctor checks this machine's toolchain for a target, plan prints
// the build steps, package builds a player folder (runner + libraries, an EMPTY game/ folder, PLAYERS.md, a launch script,
// .recomp.json) and refuses if anything that looks like game data is inside, guard runs that check on any folder, and
// recomp-json writes the optional .recomp.json.
// Test hook: PACKTOOLS_TARGETS_PROBE=<json file> stands in for this machine in doctor (the upstream tests' FakeProbe):
//   {"system": "Darwin", "machine": "arm64", "tools": [names found as /usr/bin/<name>],
//    "runs": {"<command prefix>": [exit code or null, output]}, "paths": [paths that exist], "env": {...}, "files": {...}}
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#include <chrono>
#include <ctime>
#include <map>
#include <set>
#include <thread>

namespace packtools::targets {
using namespace pyrt;
using namespace packtools::console_common;

inline const std::string PROG = "targets";
struct UsageError : std::runtime_error { using std::runtime_error::runtime_error; };

inline const std::vector<std::string> TARGETS = {"windows", "macos", "linux", "steamdeck", "android", "vita", "web"};
inline const std::vector<std::string> PACKAGE_TARGETS = {"windows", "macos", "linux", "steamdeck", "android", "vita"};
inline std::string target_title(const std::string& t) {
    if (t == "windows") return "Windows (x64)";
    if (t == "macos") return "macOS (Apple silicon or Intel)";
    if (t == "linux") return "Linux (x86_64)";
    if (t == "steamdeck") return "Steam Deck (SteamOS, Linux x86_64)";
    if (t == "android") return "Android phone, tablet or TV box";
    if (t == "vita") return "PS Vita (homebrew)";
    return "web browser (snuri00/ps2-web-recomp, a separate toolchain)";
}
inline const std::vector<std::string> STATUSES = {"exploring", "in-progress", "playable", "released", "complete", "paused"};
inline std::string region_of(const std::string& prefix) {
    static const std::vector<std::pair<std::string, std::string>> R = {
        {"SLUS", "USA"}, {"SCUS", "USA"}, {"SLES", "Europe"}, {"SCES", "Europe"}, {"SLPS", "Japan"},
        {"SLPM", "Japan"}, {"SCPS", "Japan"}, {"SLKA", "Korea"}, {"SCKA", "Korea"}, {"SCAJ", "Asia"}};
    for (const auto& [k, v] : R)
        if (k == prefix) return v;
    return "";
}
inline const std::string ANDROID_FILES = "/storage/emulated/0/Android/data/com.ps2x.runner/files";
inline const std::string ANDROID_NDK = "28.2.13676358";
inline const std::string VITA_DEFAULT_DIR = "ux0:data/ps2x";
inline constexpr long long MAX_FILE = 512LL * 1024 * 1024;
inline const std::string DIST_MARKER = ".console-dist.json";
inline const std::string PLACEHOLDER = "PUT-YOUR-GAME-HERE.txt";

inline bool has_s(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }
inline bool ends_with(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }
inline bool flag(const Val& a, const std::string& k) { return truthy(get(a, k)); }
inline bool at(const std::string& b, std::size_t off, const std::string& lit) { return b.size() >= off + lit.size() && b.compare(off, lit.size(), lit) == 0; }
// os.path.join and os.path.basename of this OS
inline std::string pj(const std::string& a, const std::string& b) {
#if defined(_WIN32)
    if (!b.empty() && (b[0] == '/' || b[0] == '\\' || (b.size() > 1 && b[1] == ':'))) return b;
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\' || (a.size() == 2 && a[1] == ':')) return a + b;
    return a + "\\" + b;
#else
    if (!b.empty() && b[0] == '/') return b;
    if (a.empty() || a.back() == '/') return a + b;
    return a + "/" + b;
#endif
}
inline std::string pj(const std::string& a, const std::string& b, const std::string& c) { return pj(pj(a, b), c); }
inline std::string pj(const std::string& a, const std::string& b, const std::string& c, const std::string& d) { return pj(pj(pj(a, b), c), d); }
inline std::string host_basename(const std::string& p) {
#if defined(_WIN32)
    return basename(p);
#else
    const std::size_t k = p.rfind('/');
    return k == std::string::npos ? p : p.substr(k + 1);
#endif
}

// ======================================================================= doctor

struct RunOut { bool ran = false; long long rc = 0; std::string out; };   // ran=false: (None, "")

// subprocess.run(cmd, stdout=PIPE, stderr=STDOUT, timeout=15): (None, "") when it cannot start or times out
inline RunOut real_run(const std::vector<std::string>& cmd, int timeout_s = 15) {
    RunOut r;
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::seconds(timeout_s);
    auto left_ms = [&] { return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()); };
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return r;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring line;
    for (std::size_t k = 0; k < cmd.size(); ++k) {
        if (k) line += L" ";
        line += packtools::console_run::quote_arg(packtools::console_run::wide(cmd[k]));
    }
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return r; }
    std::string out;
    std::thread reader([&] { char b[65536]; DWORD g = 0; while (ReadFile(rd, b, sizeof b, &g, nullptr) && g) out.append(b, g); });
    const long long ms = left_ms();
    const DWORD w = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(ms > 0 ? ms : 0));
    bool timed = false;
    if (w != WAIT_OBJECT_0) { timed = true; TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, INFINITE); }
    reader.join();
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    if (timed) return r;
    r.ran = true; r.rc = static_cast<long long>(code); r.out = decode_utf8_replace(out);
    return r;
#else
    int po[2], pe[2];
    if (pipe(po) != 0) return r;
    if (pipe(pe) != 0) { close(po[0]); close(po[1]); return r; }
    for (int f : {po[0], pe[0], pe[1]}) fcntl(f, F_SETFD, FD_CLOEXEC);
    std::vector<char*> av;
    for (const auto& s : cmd) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = fork();
    if (pid < 0) { close(po[0]); close(po[1]); close(pe[0]); close(pe[1]); return r; }
    if (pid == 0) {
        dup2(po[1], 1); dup2(po[1], 2);
        if (po[1] > 2) close(po[1]);
        execvp(av[0], av.data());
        const int e = errno;
        const ssize_t w = write(pe[1], &e, sizeof e);
        (void)w;
        _exit(127);
    }
    close(po[1]); close(pe[1]);
    int e = 0; ssize_t n;
    do n = read(pe[0], &e, sizeof e); while (n < 0 && errno == EINTR);
    close(pe[0]);
    if (n > 0) { close(po[0]); int st = 0; waitpid(pid, &st, 0); return r; }   // could not start: OSError
    std::string out; bool timed = false; char buf[65536];
    for (;;) {
        const long long ms = left_ms();
        if (ms <= 0) { timed = true; break; }
        pollfd f{po[0], POLLIN, 0};
        const int k = poll(&f, 1, static_cast<int>(std::min<long long>(ms, 1000)));
        if (k < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (k == 0) continue;
        const ssize_t g = read(po[0], buf, sizeof buf);
        if (g < 0 && errno == EINTR) continue;
        if (g <= 0) break;
        out.append(buf, static_cast<std::size_t>(g));
    }
    close(po[0]);
    int st = 0; bool done = false;
    while (!timed && !done) {
        const pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid) done = true;
        else if (w < 0 && errno != EINTR) break;
        else if (left_ms() <= 0) timed = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (timed) { kill(pid, SIGKILL); waitpid(pid, &st, 0); return r; }
    if (!done) return r;
    r.ran = true; r.rc = WIFEXITED(st) ? WEXITSTATUS(st) : -WTERMSIG(st); r.out = decode_utf8_replace(out);
    return r;
#endif
}

// What doctor may look at on this machine. PACKTOOLS_TARGETS_PROBE swaps in a fake one.
struct Probe {
    std::string system, machine;
    std::map<std::string, std::string> env;
    bool fake = false;
    std::map<std::string, std::string> tools, files;
    std::vector<std::pair<std::string, RunOut>> runs;
    std::set<std::string> paths;

    std::string env_get(const std::string& k, const std::string& dflt = "") const { const auto it = env.find(k); return it == env.end() ? dflt : it->second; }
    std::string which(const std::string& name) const {   // "" = None
        if (fake) { const auto it = tools.find(name); return it == tools.end() ? "" : it->second; }
        const auto w = packtools::console_state::which(name);
        return w ? *w : "";
    }
    RunOut run(const std::vector<std::string>& cmd) const {
        if (!fake) return real_run(cmd);
        std::string key = cmd.empty() ? "" : cmd[0];
        const std::size_t k = key.rfind('/');
        if (k != std::string::npos) key = key.substr(k + 1);
        std::vector<std::string> rest(cmd.begin() + (cmd.empty() ? 0 : 1), cmd.end());
        key += " " + join(rest, " ");
        for (const auto& [prefix, res] : runs)
            if (key.compare(0, prefix.size(), prefix) == 0) return res;
        return RunOut{};
    }
    bool exists(const std::string& p) const { return fake ? paths.count(p) > 0 : pyrt::exists(p); }
    std::string read(const std::string& p) const {
        if (fake) { const auto it = files.find(p); return it == files.end() ? "" : it->second; }
        std::string raw;
        try { raw = read_bytes(p); } catch (const std::exception&) { return ""; }
        std::string t = decode_utf8_replace(raw), o;
        for (std::size_t k = 0; k < t.size(); ++k) {
            if (t[k] == '\r') { o += '\n'; if (k + 1 < t.size() && t[k + 1] == '\n') ++k; }
            else o += t[k];
        }
        return o;
    }
};

inline Probe load_probe() {
    Probe p;
    const char* fp = std::getenv("PACKTOOLS_TARGETS_PROBE");
    if (fp && *fp) {
        const Val j = loads(read_text(fp));
        p.fake = true;
        const Val sys = get(j, "system"), mach = get(j, "machine");
        p.system = sys.t == Val::STR ? sys.s : "Darwin";
        p.machine = mach.t == Val::STR ? mach.s : "arm64";
        const Val tl = get(j, "tools");
        if (tl.t == Val::LIST)
            for (const auto& t : *tl.l) p.tools[t.s] = "/usr/bin/" + t.s;
        const Val rs = get(j, "runs");
        if (rs.t == Val::DICT)
            for (const auto& [k, v] : rs.d->items) {
                RunOut r;
                if (v.t == Val::LIST && v.l->size() == 2) {
                    r.ran = !(*v.l)[0].is_none(); r.rc = (*v.l)[0].i; r.out = (*v.l)[1].s;
                }
                p.runs.emplace_back(k, r);
            }
        const Val ps = get(j, "paths");
        if (ps.t == Val::LIST)
            for (const auto& x : *ps.l) p.paths.insert(x.s);
        const Val ev = get(j, "env");
        if (ev.t == Val::DICT && !ev.d->items.empty()) {
            for (const auto& [k, v] : ev.d->items) p.env[k] = v.s;
        } else {
            p.env["HOME"] = "/home/u";
        }
        const Val fs = get(j, "files");
        if (fs.t == Val::DICT)
            for (const auto& [k, v] : fs.d->items) p.files[k] = v.s;
        return p;
    }
#if defined(_WIN32)
    p.system = "Windows";
    if (wchar_t* blk = GetEnvironmentStringsW()) {
        for (wchar_t* e = blk; *e; e += std::wcslen(e) + 1) {
            const std::wstring w(e);
            const std::size_t k = w.find(L'=', 1);
            if (k == std::wstring::npos) continue;
            p.env.emplace(upper(u8(std::filesystem::path(w.substr(0, k)))), u8(std::filesystem::path(w.substr(k + 1))));   // os.environ keys are upper-case on Windows
        }
        FreeEnvironmentStringsW(blk);
    }
    std::string m = p.env_get("PROCESSOR_ARCHITEW6432");
    if (m.empty()) m = p.env_get("PROCESSOR_ARCHITECTURE");
    p.machine = lower(m);
#else
    struct utsname u {};
    if (uname(&u) == 0) { p.system = u.sysname; p.machine = lower(u.machine); }
    for (char** e = environ; e && *e; ++e) {
        const std::string s(*e);
        const std::size_t k = s.find('=');
        if (k == std::string::npos) continue;
        p.env.emplace(s.substr(0, k), s.substr(k + 1));
    }
#endif
    return p;
}

struct Ver { bool ok = false; std::vector<long long> v; };
inline Ver version_of(const std::string& text, const std::string& pattern) {
    Ver r;
    const re::Match m = re::search(pattern, text);
    if (!m) return r;
    r.ok = true;
    const auto ds = re::findall("\\d+", m.group(1));
    for (std::size_t k = 0; k < ds.size() && k < 3; ++k) r.v.push_back(dec_int(ds[k]));
    return r;
}
inline std::string ver_str(const Ver& v) { std::vector<std::string> s; for (long long x : v.v) s.push_back(std::to_string(x)); return join(s, "."); }
inline bool ver_ge(const Ver& v, const std::vector<long long>& minimum) {   // ver[:2] >= minimum
    const std::vector<long long> a(v.v.begin(), v.v.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(2, v.v.size())));
    return a >= minimum;
}

using Checks = std::vector<Val>;
inline Val check(const std::string& name, bool ok, bool required = true, const std::string& found = "", const std::string& fix = "") {
    return dict_of({{"name", Val::str(name)}, {"ok", Val::boolean(ok)}, {"required", Val::boolean(required)},
                    {"found", Val::str(found)}, {"fix", Val::str(fix)}});
}
inline std::string host_target(const Probe& p) {
    if (p.system == "Darwin") return "macos";
    if (p.system == "Windows") return "windows";
    return "linux";
}
inline std::string pkg_hint(const Probe& p, const std::string& apt, const std::string& brew, const std::string& dnf = "") {
    if (p.system == "Darwin") return brew;
    if (p.system == "Linux") return apt + (dnf.empty() ? "" : "  (Fedora: " + dnf + ")");
    return apt;
}
inline Val check_cmake(const Probe& p, long long major = 3, long long minor = 21) {
    const std::string name = cfmt("cmake %lld.%lld+", major, minor);
    const std::string path = p.which("cmake");
    if (path.empty())
        return check(name, false, true, "", p.system != "Windows" ? pkg_hint(p, "sudo apt install cmake", "brew install cmake", "sudo dnf install cmake")
                                                                  : "winget install Kitware.CMake");
    const RunOut r = p.run({path, "--version"});
    const Ver v = version_of(r.out, "cmake version (\\d+\\.\\d+(?:\\.\\d+)?)");
    const bool ok = v.ok && ver_ge(v, {major, minor});
    return check(name, ok, true, v.ok ? ver_str(v) : "unknown version", ok ? "" : "install a newer CMake (https://cmake.org/download/)");
}
inline Val check_tool(const Probe& p, const std::string& name, const std::string& label = "", bool required = true, const std::string& fix = "",
                      const std::vector<std::string>& alternatives = {}) {
    std::vector<std::string> cands = {name};
    cands.insert(cands.end(), alternatives.begin(), alternatives.end());
    const std::string shown = label.empty() ? name : label;
    for (const auto& c : cands) {
        const std::string path = p.which(c);
        if (!path.empty()) return check(shown, true, required, path);
    }
    return check(shown, false, required, "", fix);
}
inline Val check_ffmpeg(const Probe& p) {
    if (p.which("pkg-config").empty())
        return check("pkg-config + FFmpeg libraries", false, true, "",
                     pkg_hint(p, "sudo apt install pkg-config libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev",
                              "brew install pkg-config ffmpeg") +
                         "   (or configure with -DPS2X_ENABLE_FFMPEG=OFF: cutscenes then show stub frames)");
    const RunOut r = p.run({"pkg-config", "--exists", "libavcodec", "libavformat", "libavutil", "libswresample", "libswscale"});
    const bool ok = r.ran && r.rc == 0;
    return check("FFmpeg libraries (pkg-config)", ok, true, ok ? "found" : "",
                 ok ? "" : pkg_hint(p, "sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev",
                                    "brew install ffmpeg", "sudo dnf install ffmpeg-free-devel") + "   (or -DPS2X_ENABLE_FFMPEG=OFF)");
}

inline Checks doctor_desktop(const Probe& p, const std::string& target) {
    Checks c;
    if (target == "windows") {
        if (p.system != "Windows")
            return {check("a Windows PC", false, true, "", "build the Windows runner on Windows (cross-compiling is not set up here), or use the steamdeck target / Proton")};
        c.push_back(check_cmake(p));
        c.push_back(check_tool(p, "git", "", true, "winget install Git.Git"));
        const std::string cl = p.which("cl");
        const std::string vswhere = pj(p.env_get("ProgramFiles(x86)", "C:\\Program Files (x86)"), "Microsoft Visual Studio", "Installer", "vswhere.exe");
        std::string vs_path;
        if (cl.empty() && p.exists(vswhere)) {
            const RunOut r = p.run({vswhere, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                    "-property", "installationPath"});
            vs_path = r.ran && r.rc == 0 ? strip(r.out) : "";
        }
        c.push_back(check("MSVC (Visual Studio 2022, Desktop development with C++)", !cl.empty() || !vs_path.empty(), true, !cl.empty() ? cl : vs_path,
                          "install Visual Studio 2022 or its Build Tools with the C++ workload, then use the 'x64 Native Tools Command Prompt'"));
        c.push_back(check_tool(p, "clang-cl", "", false, "optional: the clang tools from the Visual Studio installer"));
        c.push_back(check_tool(p, "ninja", "", false, "optional: winget install Ninja-build.Ninja"));
        return c;
    }
    if (target == "macos" && p.system != "Darwin")
        return {check("a Mac", false, true, "", "build the macOS runner on a Mac (Apple silicon or Intel)")};
    if ((target == "linux" || target == "steamdeck") && p.system != "Linux") {
        std::string hint = target == "steamdeck" ? "build on Linux x86_64: a PC, a VM, WSL2 on Windows, or the Steam Deck itself in Desktop Mode inside distrobox"
                                                 : "build on a Linux machine (or WSL2 on Windows)";
        if (target == "steamdeck") hint += "; or run the Windows build under Proton";
        return {check("a Linux machine", false, true, "", hint)};
    }
    if (target == "steamdeck" && p.machine != "x86_64" && p.machine != "amd64")
        c.push_back(check("x86_64 CPU", false, true, p.machine, "the Deck is x86_64: build on an x86_64 Linux machine"));
    if (p.system == "Darwin") {
        const RunOut r = p.run({"xcode-select", "-p"});
        const bool ok = r.ran && r.rc == 0;
        c.push_back(check("Xcode Command Line Tools", ok, true, ok ? strip(r.out) : "", "xcode-select --install"));
        c.push_back(check_tool(p, "clang++", "clang++ (C++20)", true, "xcode-select --install"));
        if (p.machine == "arm64" || p.machine == "aarch64")
            c.push_back(check("Apple silicon: sse2neon", true, false, "CMake fetches sse2neon automatically on arm64"));
    } else {
        c.push_back(check_tool(p, "c++", "C++20 compiler (g++ 11+ or clang++ 14+)", true,
                               "sudo apt install build-essential   (Fedora: sudo dnf install gcc-c++)", {"g++", "clang++"}));
    }
    c.push_back(check_cmake(p));
    c.push_back(check_tool(p, "git", "", true, pkg_hint(p, "sudo apt install git", "brew install git", "sudo dnf install git")));
    c.push_back(check_tool(p, "ninja", "", false, pkg_hint(p, "sudo apt install ninja-build", "brew install ninja", "sudo dnf install ninja-build")));
    c.push_back(check_ffmpeg(p));
    if (p.system == "Linux") {
        RunOut r;
        if (!p.which("pkg-config").empty()) r = p.run({"pkg-config", "--exists", "x11", "xrandr", "xinerama", "xcursor", "xi", "gl"});
        c.push_back(check("raylib's X11 and OpenGL headers", r.ran && r.rc == 0, true, "",
                          "sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev"));
        const std::string os_release = p.read("/etc/os-release");
        if (target == "steamdeck" && os_release.find("ID=steamos") != std::string::npos) {
            const std::string d = p.which("distrobox");
            c.push_back(check("SteamOS: writable build environment", !d.empty(), false, d,
                              "SteamOS's system folders are read-only: build inside distrobox (distrobox create, then install the packages there)"));
        }
    }
    return c;
}

inline std::string android_sdk(const Probe& p) {
    for (const char* k : {"ANDROID_HOME", "ANDROID_SDK_ROOT"})
        if (!p.env_get(k).empty()) return p.env_get(k);
    std::string home = p.env_get("HOME");
    if (home.empty()) home = p.env_get("USERPROFILE");
    const std::vector<std::string> cands = {pj(home, "Library", "Android", "sdk"), pj(home, "Android", "Sdk"), pj(p.env_get("LOCALAPPDATA", ""), "Android", "Sdk")};
    for (const std::string& c : cands)
        if (!c.empty() && p.exists(c)) return c;
    return "";
}

inline Checks doctor_android(const Probe& p) {
    Checks c;
    const std::string java = p.which("java");
    if (!java.empty()) {
        const RunOut r = p.run({java, "-version"});
        const Ver v = version_of(r.out, "version \"(\\d+(?:\\.\\d+)*)");
        long long major = 0;
        if (v.ok) major = (v.v[0] == 1 && v.v.size() > 1) ? v.v[1] : v.v[0];
        const bool is17 = v.ok && major == 17;
        c.push_back(check("JDK 17", is17, true, v.ok && major != 0 ? cfmt("Java %lld", major) : "unknown",
                          is17 ? "" : "install JDK 17 (Android Studio bundles one; brew install openjdk@17 / sudo apt install openjdk-17-jdk)"));
    } else {
        c.push_back(check("JDK 17", false, true, "", "install Android Studio (bundles a JDK) or JDK 17"));
    }
    const std::string gradle = p.which("gradle");
    bool studio = false;
    const std::vector<std::string> studios = {"/Applications/Android Studio.app", "/opt/android-studio",
                                              pj(p.env_get("ProgramFiles", "C:\\Program Files"), "Android", "Android Studio")};
    for (const auto& s : studios)
        if (p.exists(s)) { studio = true; break; }
    if (!gradle.empty()) {
        const RunOut r = p.run({gradle, "--version"});
        const Ver v = version_of(r.out, "Gradle (\\d+\\.\\d+(?:\\.\\d+)?)");
        const bool ok = v.ok && ver_ge(v, {8, 7});
        c.push_back(check("Gradle 8.7+ (or Android Studio)", ok || studio, true, v.ok ? "Gradle " + ver_str(v) : gradle,
                          ok ? "" : "install Gradle 8.7+ or use Android Studio"));
    } else {
        c.push_back(check("Gradle 8.7+ (or Android Studio)", studio, true, studio ? "Android Studio" : "",
                          "install Android Studio (recommended) or Gradle 8.7+"));
    }
    const std::string sdk = android_sdk(p);
    c.push_back(check("Android SDK", !sdk.empty(), true, sdk, "install it with Android Studio, then export ANDROID_HOME=<sdk folder>"));
    if (!sdk.empty()) {
        c.push_back(check("SDK platform 34", p.exists(pj(sdk, pj("platforms", "android-34"))), true, "", "sdkmanager \"platforms;android-34\""));
        c.push_back(check("NDK " + ANDROID_NDK + " (pinned in android/app/build.gradle)", p.exists(pj(sdk, pj("ndk", ANDROID_NDK))), true, "",
                          "sdkmanager \"ndk;" + ANDROID_NDK + "\"   (Android Studio installs it on first sync)"));
        c.push_back(check("CMake 3.22.1 from the SDK", p.exists(pj(sdk, pj("cmake", "3.22.1"))), true, "",
                          "sdkmanager \"cmake;3.22.1\"   (Android Studio installs it on demand)"));
    }
    std::string adb = p.which("adb");
    if (adb.empty() && !sdk.empty() && p.exists(pj(sdk, "platform-tools", "adb"))) adb = pj(sdk, "platform-tools", "adb");
    c.push_back(check("adb (to install and push files)", !adb.empty(), true, adb, "sdkmanager platform-tools, then add <sdk>/platform-tools to PATH"));
    return c;
}

inline Checks doctor_vita(const Probe& p) {
    Checks c;
    if (p.system == "Windows")
        c.push_back(check("Linux or WSL", false, true, "", "vita/setup.sh and vita/build.sh are bash scripts: run them in WSL2 (Ubuntu) or on Linux"));
    const std::string vitasdk = p.env_get("VITASDK");
    c.push_back(check("VITASDK set", !vitasdk.empty(), true, vitasdk, "install VitaSDK (https://vitasdk.org) and export VITASDK=/usr/local/vitasdk"));
    if (!vitasdk.empty()) {
        c.push_back(check("VitaSDK toolchain file", p.exists(pj(vitasdk, "share", "vita.toolchain.cmake")), true, "",
                          "reinstall VitaSDK: $VITASDK/share/vita.toolchain.cmake is missing"));
        const std::string gcc = p.which("arm-vita-eabi-gcc");
        c.push_back(check("arm-vita-eabi-gcc in PATH", !gcc.empty(), true, gcc, "export PATH=$VITASDK/bin:$PATH"));
        const std::string prefix = pj(vitasdk, "arm-vita-eabi", "include");
        const bool ready = p.exists(pj(prefix, "SDL2", "SDL.h")) && p.exists(pj(prefix, "gpu_es4", "psp2_pvr_hint.h"));
        c.push_back(check("GL stack from vita/setup.sh (SDL2 + PVR headers)", ready, true, "", "run ./vita/setup.sh once in the PS2Recomp checkout"));
    }
    for (const std::string t : {"cmake", "curl", "unzip", "bash"})
        c.push_back(check_tool(p, t, "", true, pkg_hint(p, "sudo apt install " + t, "brew install " + t)));
    c.push_back(check_tool(p, "nproc", "nproc (used by setup.sh and build.sh)", true,
                           p.system == "Darwin" ? "brew install coreutils and put its gnubin folder first in PATH" : "sudo apt install coreutils"));
    if (p.system == "Darwin")
        c.push_back(check("GNU install (setup.sh uses install -D -t)", false, false, "",
                          "macOS's install lacks -D/-t: run setup.sh in Linux or WSL, or put GNU coreutils' gnubin first in PATH"));
    c.push_back(check_tool(p, "ninja", "", false, "optional: build.sh uses Ninja when present"));
    return c;
}

inline Checks doctor_web(const Probe& p) {
    Checks c;
    c.push_back(check_tool(p, "git", "", true, pkg_hint(p, "sudo apt install git", "brew install git")));
    c.push_back(check_cmake(p, 3, 20));
    c.push_back(check_tool(p, "ninja", "", true, pkg_hint(p, "sudo apt install ninja-build", "brew install ninja")));
    c.push_back(check_tool(p, "clang++", "C++20 compiler (clang preferred)", true, pkg_hint(p, "sudo apt install clang", "xcode-select --install"), {"c++", "g++"}));
    c.push_back(check_tool(p, "python3", "", true, "install Python 3"));
    if (p.system == "Darwin") {
        const std::string bash = p.which("bash");
        RunOut r;
        if (!bash.empty()) r = p.run({bash, "--version"});
        const Ver v = version_of(r.out, "version (\\d+\\.\\d+)");
        c.push_back(check("bash 4+ (macOS ships 3.2)", v.ok && v.v[0] >= 4, true, v.ok ? ver_str(v) : "", "brew install bash"));
        c.push_back(check_tool(p, "7zz", "7-Zip (7zz)", true, "brew install sevenzip", {"7z"}));
    }
    c.push_back(check("Emscripten", true, false, "installed by scripts/port.sh on first run into tools/emsdk"));
    return c;
}

inline Checks doctor(const Probe& p, const std::string& target) {
    if (target == "android") return doctor_android(p);
    if (target == "vita") return doctor_vita(p);
    if (target == "web") return doctor_web(p);
    return doctor_desktop(p, target);
}

inline int cmd_doctor(const Val& a) {
    const Probe p = load_probe();
    std::vector<std::string> targets;
    const Val tv = get(a, "target");
    if (tv.t == Val::LIST)
        for (const auto& x : *tv.l) targets.push_back(x.s);
    if (targets.empty()) targets.push_back(host_target(p));
    if (has_s(targets, "all")) targets = TARGETS;
    Val report = Val::dict();
    long long missing = 0;
    for (const auto& t : targets) {
        const Checks cs = doctor(p, t);
        report.d->set(t, Val::list(List(cs.begin(), cs.end())));
        for (const auto& c : cs)
            if (get(c, "required").b && !get(c, "ok").b) ++missing;
    }
    if (flag(a, "json")) {
        out(dumps2(dict_of({{"host", dict_of({{"system", Val::str(p.system)}, {"machine", Val::str(p.machine)}})},
                            {"targets", report}, {"missing", Val::integer(missing)}})));
    } else {
        out("doctor: this machine is " + p.system + " " + p.machine);
        for (const auto& [t, cs] : report.d->items) {
            out("");
            out(t + ": " + target_title(t));
            for (const auto& c : *cs.l) {
                const bool ok = get(c, "ok").b, req = get(c, "required").b;
                std::string line = std::string("  ") + (ok ? "ok     " : req ? "MISSING" : "note   ") + " " + get(c, "name").s;
                if (!get(c, "found").s.empty()) line += "  (" + get(c, "found").s + ")";
                out(line);
                if (!ok && !get(c, "fix").s.empty()) out("          -> " + get(c, "fix").s);
            }
        }
        out("");
        out(missing == 0 ? "all required tools found" : cfmt("%lld required item(s) missing", missing));
    }
    return missing == 0 ? 0 : 1;
}

// ======================================================================= plan

struct Step { std::string title; std::vector<std::string> commands, notes; };

inline std::vector<Step> plan_steps(const std::string& target, const std::string& repo, const std::string& build, const std::string& elf) {
    const std::string clone = "git clone --recurse-submodules https://github.com/ran-j/PS2Recomp.git";
    const std::string runner_ready = "Your recompiled game code is already in " + repo +
                                     "/ps2xRuntime/src/runner/ (from /console-reverse). The runner is built from it, so build in the checkout you "
                                     "recompiled into, not a fresh clone.";
    if (target == "windows") {
        return {
            {"Install the tools", {"winget install Kitware.CMake Git.Git"},
             {"Visual Studio 2022 (or its Build Tools) with 'Desktop development with C++'. The PS2Recomp README says it is tested mainly with MSVC.",
              "FFmpeg needs nothing: CMake downloads prebuilt LGPL DLLs and copies them next to the runner."}},
            {"Get PS2Recomp (skip if you already have it)", {clone}, {runner_ready}},
            {"Build the runner, Release (x64 Native Tools Command Prompt)",
             {"cd " + repo, "cmake -S . -B " + build, "cmake --build " + build + " --config Release --target ps2EntryRunner"},
             {"Optional at configure time: -DPS2X_SHOW_WINDOWS_CONSOLE=OFF hides the console window in Release builds; -DPS2X_ENABLE_DEBUG_UI=OFF "
              "drops the F1 debug panel.",
              "Never clean the build folder: an incremental build is minutes, a full one is an hour."}},
            {"Try it", {replace_all(build, "/", "\\") + "\\ps2xRuntime\\Release\\ps2EntryRunner.exe C:\\games\\mygame\\" + elf},
             {"The runner takes the ELF path as its first argument. The disc's files sit next to the ELF (cdRoot is the ELF's folder) and saves "
              "go to mc0\\ beside it."}},
            {"Package it",
             {"python3 targets.py package --runner " + build + "/ps2xRuntime/Release/ps2EntryRunner.exe --target windows --elf-name " + elf +
              " --out console/dist/mygame-windows"},
             {"The FFmpeg DLLs next to the runner are copied too."}},
        };
    }
    if (target == "macos" || target == "linux") {
        std::vector<std::string> tool_cmds, tool_notes;
        if (target == "macos") {
            tool_cmds = {"xcode-select --install", "brew install cmake ninja pkg-config ffmpeg"};
            tool_notes = {"Apple silicon works: CMake sees arm64 and fetches sse2neon itself (USE_SSE2NEON).",
                          "No FFmpeg wanted? Configure with -DPS2X_ENABLE_FFMPEG=OFF: cutscenes show stub frames."};
        } else {
            tool_cmds = {"sudo apt install build-essential cmake ninja-build git pkg-config libavcodec-dev libavformat-dev libavutil-dev "
                         "libswresample-dev libswscale-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev"};
            tool_notes = {"Debian and Ubuntu names; Fedora has the same packages under dnf names.",
                          "The X11 and GL headers are for raylib, which CMake fetches (raylib 5.5).",
                          "No FFmpeg wanted? Configure with -DPS2X_ENABLE_FFMPEG=OFF."};
        }
        const std::string release = "out/release";
        std::vector<Step> steps = {
            {"Install the tools", tool_cmds, tool_notes},
            {"Get PS2Recomp (skip if you already have it)", {clone}, {runner_ready}},
            {"Build the runner, Release, in its own folder",
             {"cd " + repo, "cmake -S . -B " + release + " -G Ninja -DCMAKE_BUILD_TYPE=Release", "cmake --build " + release + " --target ps2EntryRunner"},
             {"A separate folder keeps your day-to-day build (" + build +
                  ", the one in console/STATE.md) untouched. A folder never changes generator once configured.",
              "The README build (cmake -S . -B out/build; cmake --build out/build --config Debug) works too; the Release runner is faster to play.",
              "The README says PS2Recomp is tested mainly with MSVC: Mac and Linux builds work through the same CMake but get less testing."}},
            {"Try it", {"./" + release + "/ps2xRuntime/ps2EntryRunner ~/games/mygame/" + elf},
             {"Or run it through /console-tester: run.py boot --runner ... --elf ..."}},
            {"Package it",
             {"python3 targets.py package --runner " + release + "/ps2xRuntime/ps2EntryRunner --target " + target + " --elf-name " + elf +
              " --out console/dist/mygame-" + target},
             {"The runner links FFmpeg dynamically: the other machine needs FFmpeg installed too (PLAYERS.md says so), unless you built with "
              "-DPS2X_ENABLE_FFMPEG=OFF."}},
        };
        if (target == "macos")
            steps[3].notes.push_back("A runner copied from another Mac may be quarantined by Gatekeeper: on your own Mac, xattr -dr "
                                     "com.apple.quarantine <the folder>.");
        return steps;
    }
    if (target == "steamdeck") {
        return {
            {"Pick a route", {}, {"A: a native Linux build (best). B: the Windows build under Proton."}},
            {"A1. Build for Linux x86_64", {"python3 targets.py plan --target linux"},
             {"On a Linux PC or VM, or on the Deck itself in Desktop Mode inside distrobox (SteamOS's system folders are read-only, distrobox "
              "gives you a normal distro).",
              "Build on a distro no newer than SteamOS, or inside distrobox on the Deck, so the C++ and FFmpeg libraries match. "
              "-DPS2X_ENABLE_FFMPEG=OFF removes the FFmpeg dependency entirely."}},
            {"A2. Package and copy it over",
             {"python3 targets.py package --runner out/release/ps2xRuntime/ps2EntryRunner --target steamdeck --elf-name " + elf +
              " --out console/dist/mygame-deck"},
             {"Copy the folder to the Deck (USB stick, scp, or Warpinator), e.g. to ~/Games/mygame. Put your extracted disc in game/."}},
            {"A3. Add it to Steam", {},
             {"Desktop Mode, Steam, Add a Non-Steam Game, browse to play.sh (show all file types).",
              "Steam Input presents the Deck's controls as an Xbox-style pad: use the xbox layout in /console-controller."}},
            {"B. Proton instead", {"python3 targets.py plan --target windows"},
             {"Add ps2EntryRunner.exe as a non-Steam game, put game\\" + elf +
              " in its Launch Options, and force a Proton version under Compatibility. Not tested by this skill."}},
        };
    }
    if (target == "android") {
        const std::string device_elf = ANDROID_FILES + "/" + elf;
        return {
            {"Install the tools", {},
             {"From android/README.md: Android Studio (recommended) or Gradle 8.7+ and JDK 17; Android SDK 34 and the NDK "
              "(android/app/build.gradle pins ndkVersion " + ANDROID_NDK + "); CMake 3.22.1 from the SDK. Android Studio installs the NDK and "
              "CMake on first sync."}},
            {"Decide where the ELF will live on the device", {},
             {"Android has no command line, so the ELF path is baked in at build time through the ps2xBootElf Gradle property (it becomes "
              "PS2X_DEFAULT_BOOT_ELF).",
              "Use " + device_elf + " (keep the disc's real ELF name: the game database and game overrides key on it)."}},
            {"Build the APK", {"cd " + repo + "/android", "gradle wrapper --gradle-version 8.9", "./gradlew assembleRelease -Pps2xBootElf=" + device_elf},
             {"Or in Android Studio: set ps2xBootElf=" + device_elf + " in android/gradle.properties and run 'app'.",
              "Output: android/app/build/outputs/apk/release/app-release.apk (arm64-v8a and x86_64, minSdk 28). build.gradle signs release "
              "with the debug key: fine for your own devices."}},
            {"Install and copy your game",
             {"adb install app/build/outputs/apk/release/app-release.apk", "adb shell mkdir -p " + ANDROID_FILES,
              "adb push \"path/to/your/extracted/disc/.\" " + ANDROID_FILES + "/"},
             {"An Android TV box works the same way (adb connect <its IP>, then the same commands) with a Bluetooth controller. The manifest "
              "has no Android TV (LEANBACK_LAUNCHER) entry, so open it from Settings, Apps, or a sideload launcher."}},
            {"Logs and controls", {"adb logcat -s raylib ps2x"},
             {"main.cpp in this checkout forwards the runtime's stdout and stderr to logcat under the tag ps2x (android/README.md predates "
              "that and says they are not redirected).",
              "No on-screen controls: ps2_android_runtime.cpp is only a TODO for a touch overlay. Pair a controller; /console-controller can "
              "add invisible touch zones."}},
            {"Package it",
             {"python3 targets.py package --runner " + repo + "/android/app/build/outputs/apk/release/app-release.apk --target android --elf-name " +
              elf + " --out console/dist/mygame-android"},
             {"The package adds install scripts that run the adb steps above."}},
        };
    }
    if (target == "vita") {
        const std::string vita_elf = VITA_DEFAULT_DIR + "/" + elf;
        return {
            {"What you need", {},
             {"A PS Vita that already runs homebrew, with VitaShell.",
              "A Linux machine or WSL2: vita/setup.sh uses GNU install -D and nproc (on macOS put GNU coreutils first in PATH, or use a Linux "
              "VM)."}},
            {"Install VitaSDK", {"export VITASDK=/usr/local/vitasdk", "export PATH=$VITASDK/bin:$PATH"},
             {"https://vitasdk.org has the installer (vdpm)."}},
            {"Set up the GL stack, once", {"cd " + repo, "./vita/setup.sh"},
             {"Downloads and installs into $VITASDK: PVR_PSP2 3.9 (GPU driver modules), gl4es4vita 1.1.4, SDL2 2.32.2 rebuilt with PVR, and "
              "Quenom's raylib 5.5 Vita port. It stages the .suprx modules into ps2xRuntime/vita/module/ so the vpk bundles them. Safe to re-run."}},
            {"Build the vpk", {"./vita/build.sh " + vita_elf},
             {"The argument is the ELF's path ON THE VITA, baked in (default " + VITA_DEFAULT_DIR +
                  "/game.elf; the PS2X_BOOT_ELF environment variable works too). Keep the disc's real ELF name.",
              "Build folder: outvita (BUILD_DIR changes it). Do not set CLEAN=1: it wipes the folder and forces a full rebuild.",
              "build.sh prints the vpk's path when it is done (ps2EntryRunner.vpk, made in outvita/ps2xRuntime/): title id RANJ00001, bubble "
              "name 'PS2 Retro X' (the PS2X_VITA_TITLEID and PS2X_VITA_APP_NAME CMake cache variables)."}},
            {"Install and copy your game", {},
             {"Install the vpk with VitaShell, then copy your extracted disc (with the ELF) to " + VITA_DEFAULT_DIR +
                  "/ over VitaShell's FTP or USB mode.",
              "Expect it to be slow: the Vita is far weaker than a PC and the README already calls VU and GS performance very bad. Logs go to "
              "the Vita's TTY (ps2_vita_runtime.cpp)."}},
            {"Package it",
             {"python3 targets.py package --runner outvita/ps2xRuntime/ps2EntryRunner.vpk --target vita --elf-name " + elf +
              " --out console/dist/mygame-vita"},
             {}},
        };
    }
    if (target == "web") {
        return {
            {"What this is", {},
             {"The browser path is a different toolchain: snuri00/ps2-web-recomp (MIT) is its own PS2 static recompiler and runtime (eerecomp, "
              "not PS2Recomp) that compiles to WebAssembly and draws with WebGPU. So it is a second port of the same disc, not a build target "
              "of your PS2Recomp port: your overrides and TOML do not carry over.",
              "Two games have been brought up there so far (Gran Turismo 4, God of War II); others will likely stop at an unhandled syscall or "
              "IOP function. This skill has not been tested against it."}},
            {"Install the tools", {"brew install bash cmake ninja sevenzip   (macOS, Apple silicon)"},
             {"git, CMake, Ninja, a C++20 compiler (clang preferred) and Python 3. Linux x86-64 is the tested host. Emscripten installs itself "
              "into tools/emsdk on the first run. About 300 MB per game."}},
            {"Port your own disc image",
             {"git clone https://github.com/snuri00/ps2-web-recomp.git", "cd ps2-web-recomp", "scripts/port.sh mygame \"My Game.iso\""},
             {"--native-only skips the browser build and only makes the native runner (build/native-mygame/ps2run, SDL2 + wgpu-native; Steam "
              "Deck controls work).",
              "It applies PCSX2 pnach files itself: --pnach FILE --pnach-section \"Widescreen 16:9\" (patches from /console-mod or "
              "/console-graphics)."}},
            {"Play it in the browser", {"python3 scripts/serve.py build/web-mygame games/mygame/disc.iso"},
             {"Serves http://localhost:8620/ on your own machine. The browser needs WebGPU: Chrome or Edge on Windows and macOS; on Linux "
              "enable chrome://flags enable-unsafe-webgpu and enable-vulkan.",
              "Keep it local. Serving it to others would hand them the game."}},
        };
    }
    throw UsageError("unknown target " + str_repr(target));
}

inline std::vector<std::string> plan_sources(const std::string& t) {
    if (t == "windows") return {"README.md (Build, Requirements)", "ps2xRuntime/CMakeLists.txt (runner target, FFmpeg DLLs)"};
    if (t == "macos") return {"README.md", "CMakeLists.txt (sse2neon on ARM)", "ps2xRuntime/CMakeLists.txt (FFmpeg via pkg-config)"};
    if (t == "linux") return {"README.md", "ps2xRuntime/CMakeLists.txt (FFmpeg via pkg-config, raylib 5.5 FetchContent)"};
    if (t == "steamdeck") return {"README.md", "ps2xRuntime/CMakeLists.txt"};
    if (t == "android")
        return {"android/README.md", "android/app/build.gradle", "android/gradle.properties", "ps2xRuntime/src/main.cpp (logcat redirect)",
                "ps2xRuntime/src/lib/ps2_android_runtime.cpp"};
    if (t == "vita")
        return {"vita/setup.sh", "vita/build.sh", "ps2xRuntime/Readme.md (Vita notes)", "ps2xRuntime/CMakeLists.txt (vpk, title id)",
                "ps2xRuntime/src/lib/ps2_vita_runtime.cpp"};
    return {"snuri00/ps2-web-recomp README (third-party, not part of PS2Recomp)"};
}

inline int cmd_plan(const Val& a) {
    const std::string target = S(a, "target");
    const std::string given = S(a, "elf_name");
    const std::string elf = given.empty() ? "SLUS_203.12" : given;
    const std::vector<Step> steps = plan_steps(target, S(a, "repo"), S(a, "build_dir"), elf);
    const std::vector<std::string> sources = plan_sources(target);
    if (flag(a, "json")) {
        List st;
        for (const auto& s : steps)
            st.push_back(dict_of({{"title", Val::str(s.title)}, {"commands", str_list(s.commands)}, {"notes", str_list(s.notes)}}));
        out(dumps2(dict_of({{"target", Val::str(target)}, {"title", Val::str(target_title(target))}, {"steps", Val::list(std::move(st))},
                            {"sources", str_list(sources)}})));
        return 0;
    }
    out("plan: " + target_title(target));
    if (given.empty() && target != "web") out("(SLUS_203.12 below stands for your disc's ELF name: pass --elf-name)");
    for (std::size_t i = 0; i < steps.size(); ++i) {
        out("");
        out(std::to_string(i + 1) + ". " + steps[i].title);
        for (const auto& c : steps[i].commands) out("     $ " + c);
        for (const auto& n : steps[i].notes) out("     - " + n);
    }
    out("");
    out("from: " + join(sources, "; "));
    out("Players always supply their own disc. Nothing from the game goes in a package.");
    return 0;
}

// ======================================================================= guard

inline const std::set<std::string> DISC_EXTS = {".iso", ".bin", ".img", ".mdf", ".mds", ".nrg", ".chd", ".cso", ".zso", ".ecm", ".cue",
                                                ".ccd", ".sub", ".isz", ".dax"};
inline const std::set<std::string> SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh"};
inline const std::string SERIAL_NAME = "^[A-Za-z]{4}[_-]\\d{3}\\.?\\d{2}(;\\d+)?$";
inline const std::set<std::string> RECOMP_NAMES = {"register_functions.cpp", "ps2_recompiled_functions.h", "ps2_recompiled_stubs.h"};
using Rules = std::vector<std::pair<std::string, std::string>>;

// fnmatch.fnmatchcase: * ? [seq] [!seq] over code points; * also matches "/"
inline bool class_match(const std::vector<uint32_t>& p, std::size_t j, uint32_t ch, std::size_t& next, bool& matched) {
    std::size_t i = j + 1;
    if (i < p.size() && p[i] == '!') ++i;
    if (i < p.size() && p[i] == ']') ++i;
    while (i < p.size() && p[i] != ']') ++i;
    if (i >= p.size()) return false;   // no closing bracket: a literal '['
    std::size_t q = j + 1;
    bool neg = false;
    if (p[q] == '!') { neg = true; ++q; }
    bool hit = false;
    while (q < i) {
        if (q + 2 < i && p[q + 1] == '-') {
            if (p[q] <= ch && ch <= p[q + 2]) hit = true;
            q += 3;
        } else {
            if (p[q] == ch) hit = true;
            ++q;
        }
    }
    matched = neg ? !hit : hit;
    next = i + 1;
    return true;
}
inline bool fnmatchcase(const std::string& name, const std::string& pat) {
    std::vector<uint32_t> s, p;
    for (const auto& c : cps(name)) s.push_back(cp_of(c));
    for (const auto& c : cps(pat)) p.push_back(cp_of(c));
    std::size_t i = 0, j = 0, star = std::string::npos, star_i = 0;
    while (i < s.size()) {
        if (j < p.size()) {
            if (p[j] == '*') { star = ++j; star_i = i; continue; }
            bool ok = false; std::size_t nj = j + 1;
            if (p[j] == '?') ok = true;
            else if (p[j] == '[') {
                bool m = false;
                if (class_match(p, j, s[i], nj, m)) ok = m;
                else { ok = s[i] == '['; nj = j + 1; }
            } else ok = p[j] == s[i];
            if (ok) { ++i; j = nj; continue; }
        }
        if (star != std::string::npos) { j = star; i = ++star_i; continue; }
        return false;
    }
    while (j < p.size() && p[j] == '*') ++j;
    return j == p.size();
}

inline std::string read_head(const std::string& path, std::size_t n) {
    if (is_dir(path)) return "";
    std::FILE* f = open_file(path, "rb");
    if (!f) return "";
    std::string b(n, '\0');
    const std::size_t g = std::fread(b.data(), 1, n, f);
    std::fclose(f);
    b.resize(g);
    return b;
}
// os.path.splitext(name)[1]: leading dots do not start an extension
inline std::string ext_of(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos) return "";
    for (std::size_t k = 0; k < dot; ++k)
        if (name[k] != '.') return name.substr(dot);
    return "";
}

inline Rules load_allow(const Val& a) {
    Rules rules;
    const std::string path = S(a, "allow");
    if (path.empty()) return rules;
    std::string raw;
    try {
        raw = read_bytes(path);
    } catch (const OSError& e) {
        if (e.err == ENOENT) throw UsageError("no such allow file: " + path);
        throw UsageError("cannot read " + path + ": " + e.what());
    }
    if (const auto u = packtools::console_run::utf8_error(raw)) throw UsageError("cannot read " + path + ": " + *u);
    std::string text;
    for (std::size_t k = 0; k < raw.size(); ++k) {
        if (raw[k] == '\r') { text += '\n'; if (k + 1 < raw.size() && raw[k + 1] == '\n') ++k; }
        else text += raw[k];
    }
    long long number = 0;
    for (const auto& r : splitlines(text)) {
        ++number;
        const std::string line = strip(r);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t h = line.find('#');
        if (h == std::string::npos)
            throw UsageError(path + " line " + std::to_string(number) + ": every allowed path needs a reason: '<path or pattern>  # why'");
        const std::string pattern = strip(line.substr(0, h)), reason = strip(line.substr(h + 1));
        if (pattern.empty() || reason.empty()) throw UsageError(path + " line " + std::to_string(number) + ": expected '<path or pattern>  # why'");
        rules.emplace_back(replace_all(pattern, "\\", "/"), reason);
    }
    return rules;
}

// the reason this file looks like game data, or "" (None)
inline std::string classify_file(const std::string& path, const std::string& rel, long long size) {
    const std::string name = host_basename(rel);
    const std::string low = lower(name);
    const std::string ext = ext_of(low);
    const std::vector<std::string> parts = split(replace_all(rel, "\\", "/"), "/");
    if (parts[0] == "game" && parts.size() > 1 && name != PLACEHOLDER) return "inside game/: ship that folder empty, the player adds their own disc";
    if (size > MAX_FILE) return "over 512 MB: only disc images and dumps are that big";
    if (DISC_EXTS.count(ext)) return "a disc image file (" + ext + ")";
    if (ext == ".irx") return "an IOP module (.IRX) from the disc";
    if (low == "system.cnf") return "the disc's SYSTEM.CNF";
    if (re::match(SERIAL_NAME, name)) return "named like a disc's main ELF (" + name + ")";
    if (RECOMP_NAMES.count(name)) return "recompiled game code (" + name + ")";
    if (SOURCE_EXTS.count(ext)) {
        for (std::size_t k = 0; k + 1 < parts.size(); ++k)
            if (lower(parts[k]) == "runner") return "a source file from a runner/ folder (recompiled game code)";
    }
    const std::string head = read_head(path, 0x9320);
    if (head.size() >= 20 && at(head, 0, std::string("\x7f" "ELF"))) {
        const unsigned b0 = static_cast<unsigned char>(head[18]), b1 = static_cast<unsigned char>(head[19]);
        const unsigned machine = head[5] == 1 ? (b0 | b1 << 8) : (b0 << 8 | b1);
        if (head[4] == 1 && machine == 8) return "a PS2 ELF or IRX (32-bit MIPS)";
    }
    if (at(head, 0, "MComprHD")) return "a CHD disc image";
    if (at(head, 0, "CISO") || at(head, 0, "ZISO")) return "a compressed ISO (CSO/ZSO)";
    if (at(head, 0x8001, "CD001") || at(head, 0x9319, "CD001")) return "an ISO 9660 disc image";
    if (SOURCE_EXTS.count(ext)) {
        const std::string blob = read_head(path, 256 * 1024);
        if (blob.find("ps2_recompiled_functions.h") != std::string::npos || blob.find("g_ps2RecompiledFunctionTable") != std::string::npos)
            return "recompiled game code (references the generated function table)";
    }
    return "";
}

// os.walk(root) top-down with sorted folders and files; symlinked folders are listed but not entered
inline void scan_dir(const std::string& folder, const std::string& prefix, const Rules& rules, List& findings, List& allowed) {
    std::vector<std::string> files;
    std::vector<std::pair<std::string, bool>> dirs;
    std::error_code ec;
    std::filesystem::directory_iterator it(fspath(folder), ec), end;
    if (ec) return;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e2, e3;
        const std::string name = u8(it->path().filename());
        if (it->is_directory(e2)) dirs.emplace_back(name, it->is_symlink(e3));
        else files.push_back(name);
    }
    std::sort(files.begin(), files.end());
    std::sort(dirs.begin(), dirs.end());
    for (const auto& name : files) {
        const std::string path = path_join(folder, name), rel = prefix + name;
        if (rel == DIST_MARKER) continue;
        std::error_code e4;
        const auto sz = std::filesystem::file_size(fspath(path), e4);
        const long long size = e4 ? 0 : static_cast<long long>(sz);
        const std::string reason = classify_file(path, rel, size);
        if (reason.empty()) continue;
        const std::pair<std::string, std::string>* rule = nullptr;
        for (const auto& r : rules)
            if (fnmatchcase(rel, r.first) || rel == r.first) { rule = &r; break; }
        if (rule)
            allowed.push_back(dict_of({{"path", Val::str(rel)}, {"reason", Val::str(reason)}, {"allowed_because", Val::str(rule->second)}}));
        else
            findings.push_back(dict_of({{"path", Val::str(rel)}, {"reason", Val::str(reason)}}));
    }
    for (const auto& [name, link] : dirs)
        if (!link) scan_dir(path_join(folder, name), prefix + name + "/", rules, findings, allowed);
}
inline void scan_folder(const std::string& root, const Rules& rules, List& findings, List& allowed) { scan_dir(root, "", rules, findings, allowed); }

inline void print_findings(const List& findings, const List& allowed) {
    for (const auto& f : findings) out("  REFUSED  " + get(f, "path").s + ": " + get(f, "reason").s);
    for (const auto& x : allowed)
        out("  allowed  " + get(x, "path").s + ": " + get(x, "reason").s + " (allow file: " + get(x, "allowed_because").s + ")");
    if (!findings.empty())
        out("Nothing from the game may be packaged or published: the disc, its ELF and IRX modules, its assets and the recompiled runner/ "
            "code stay on your machine.");
}

inline int cmd_guard(const Val& a) {
    const std::string folder = S(a, "folder");
    if (!is_dir(folder)) throw UsageError("not a folder: " + folder);
    const Rules rules = load_allow(a);
    List findings, allowed;
    scan_folder(folder, rules, findings, allowed);
    if (flag(a, "json")) {
        out(dumps2(dict_of({{"folder", Val::str(folder)}, {"refused", Val::list(findings)}, {"allowed", Val::list(allowed)},
                            {"ok", Val::boolean(findings.empty())}})));
    } else {
        out("guard: " + folder);
        print_findings(findings, allowed);
        if (findings.empty()) out("  ok: nothing that looks like game data");
    }
    return findings.empty() ? 0 : 1;
}

// ======================================================================= recomp-json

inline std::string normalize_serial(const std::string& text) {
    const std::string t = replace_all(upper(strip(text)), ";1", "");
    const re::Match m = re::fullmatch("([A-Z]{4})[_-]?(\\d{3})\\.?(\\d{2})", t);
    if (!m) throw UsageError(str_repr(text) + " is not a disc serial like SLUS-20312 or SLUS_203.12");
    return m.group(1) + "-" + m.group(2) + m.group(3);
}

inline const std::string LINKED_FILE = "(?i)(magnet:|(https?|ftp)://\\S+\\.(iso|bin|img|chd|cso|zso|elf|irx|7z|zip|rar|nrg|mdf)(\\?\\S*)?$)";
inline void game_file_links(const Val& o, const std::string& path, std::vector<std::string>& hits) {
    if (o.t == Val::DICT) {
        for (const auto& [k, v] : o.d->items) game_file_links(v, path + "." + k, hits);
    } else if (o.t == Val::LIST) {
        for (std::size_t i = 0; i < o.l->size(); ++i) game_file_links((*o.l)[i], path + "[" + std::to_string(i) + "]", hits);
    } else if (o.t == Val::STR && re::search(LINKED_FILE, strip(o.s))) {
        hits.push_back(path);
    }
}

inline Val build_recomp_json(const std::string& game, const std::string& serial0, const std::string& status, const std::string& region0, const Val& existing) {
    if (strip(game).empty()) throw UsageError("--game needs the title as it shipped");
    if (!has_s(STATUSES, status)) throw UsageError("--status must be one of: " + join(STATUSES, ", "));
    const std::string serial = normalize_serial(serial0);
    const std::string region = region0.empty() ? region_of(serial.substr(0, 4)) : region0;
    if (region.empty()) throw UsageError("cannot tell the region from " + serial + ": pass --region");
    Val data = dict_of({{"$schema", Val::str("https://recomp.fyi/schema/v1.json")}, {"game", Val::str(strip(game))}, {"system", Val::str("PS2")},
                        {"type", Val::str("recomp")}, {"toolchain", Val::str("PS2Recomp")}, {"status", Val::str(status)},
                        {"original", dict_of({{"region", Val::str(region)}, {"serial", Val::str(serial)}})}});
    if (existing.t == Val::DICT && !existing.d->items.empty()) {
        Val merged = Val::dict();
        merged.d->items = existing.d->items;
        Val original = Val::dict();
        const Val eo = get(existing, "original");
        if (truthy(eo)) {
            if (eo.t != Val::DICT) throw PyErr("ValueError", "dictionary update sequence element #0 has the wrong length");
            original.d->items = eo.d->items;
        }
        for (const auto& [k, v] : get(data, "original").d->items) original.d->set(k, v);
        for (const auto& [k, v] : data.d->items)
            if (k != "original") merged.d->set(k, v);
        merged.d->set("original", original);
        data = merged;
    }
    std::vector<std::string> bad;
    game_file_links(data, "$", bad);
    if (!bad.empty())
        throw UsageError("refusing: " + std::to_string(bad.size()) + " link(s) to a game file or archive (" + join(bad, ", ") +
                         "). Never put a game file, or a link to one, in .recomp.json");
    return data;
}

inline void make_dirs(const std::string& d) {
    std::error_code ec;
    std::filesystem::create_directories(fspath(d), ec);
    if (ec) throw OSError(ec.value(), d);
}

inline int cmd_recomp_json(const Val& a) {
    const std::string outp = S(a, "out");
    Val existing;
    if (pyrt::exists(outp)) {
        std::string raw, msg;
        try { raw = read_bytes(outp); } catch (const OSError& e) { msg = e.what(); }
        if (msg.empty()) {
            if (const auto u = packtools::console_run::utf8_error(raw)) msg = *u;
        }
        if (msg.empty()) {
            std::string text;
            for (std::size_t k = 0; k < raw.size(); ++k) {
                if (raw[k] == '\r') { text += '\n'; if (k + 1 < raw.size() && raw[k + 1] == '\n') ++k; }
                else text += raw[k];
            }
            try { existing = loads(text); } catch (const PyErr& e) { msg = e.what(); }
        }
        if (!msg.empty()) throw UsageError(outp + " exists but is not valid JSON (" + msg + "); fix or remove it");
        if (existing.t != Val::DICT) throw UsageError(outp + " exists but is not a JSON object");
    }
    const Val data = build_recomp_json(S(a, "game"), S(a, "serial"), S(a, "status"), S(a, "region"), existing);
    make_dirs(dirname(abspath(outp)));
    pyrt::write_text(outp, dumps2(data) + "\n");
    const bool updated = existing.t == Val::DICT && !existing.d->items.empty();
    const Val orig = get(data, "original");
    out((updated ? "updated " : "wrote ") + outp + ": " + get(data, "game").s + " (" + get(orig, "serial").s + ", " + get(orig, "region").s +
        "), status " + get(data, "status").s);
    out("Optional file: PS2Recomp does not read it; recomp lists such as recomp.fyi do. Update the status when the project moves on.");
    return 0;
}

// ======================================================================= package

inline std::string detect_binary(const std::string& path) {
    const std::string h = read_head(path, 64);
    if (at(h, 0, "MZ")) return "windows";
    for (const char* m : {"\xcf\xfa\xed\xfe", "\xce\xfa\xed\xfe", "\xfe\xed\xfa\xcf", "\xfe\xed\xfa\xce", "\xca\xfe\xba\xbe"})
        if (at(h, 0, m)) return "macos";
    if (h.size() >= 20 && at(h, 0, std::string("\x7f" "ELF"))) {
        const unsigned b0 = static_cast<unsigned char>(h[18]), b1 = static_cast<unsigned char>(h[19]);
        const unsigned machine = h[5] == 1 ? (b0 | b1 << 8) : (b0 << 8 | b1);
        if (machine == 8) return "mips";
        if (machine == 40) return "arm32";
        if (machine == 62) return "linux";
        if (machine == 183) return "arm64-linux";
        return "elf";
    }
    if (at(h, 0, std::string("PK\x03\x04", 4))) return "zip";
    return "";
}

inline bool needs_ffmpeg(const std::string& path) {
    if (is_dir(path)) return false;
    std::FILE* f = open_file(path, "rb");
    if (!f) return false;
    std::vector<char> buf(1 << 20);
    std::string tail;
    bool hit = false;
    for (;;) {
        const std::size_t g = std::fread(buf.data(), 1, buf.size(), f);
        if (!g) break;
        const std::string chunk(buf.data(), g);
        if ((tail + chunk).find("libavcodec") != std::string::npos) { hit = true; break; }
        tail = chunk.substr(g > 16 ? g - 16 : 0);
    }
    std::fclose(f);
    return hit;
}

using Values = std::vector<std::pair<std::string, std::string>>;
inline std::string render(std::string text, const Values& values) {
    for (const auto& [k, v] : values) text = replace_all(text, "{{" + k + "}}", v);
    return text;
}

inline std::string launch_posix(const std::string& runner, const std::string& elf) {
    return std::string("#!/bin/sh\n"
                       "# Starts the game with YOUR disc: put your extracted disc in game/ first (see PLAYERS.md).\n"
                       "cd \"$(dirname \"$0\")\" || exit 1\n"
                       "ELF=\"") + elf + "\"\n"
           "if [ -z \"$ELF\" ]; then\n"
           "  if [ ! -f game/SYSTEM.CNF ]; then\n"
           "    echo \"Put your extracted disc in game/ first (see PLAYERS.md).\" >&2\n"
           "    exit 1\n"
           "  fi\n"
           "  line=$(grep -i '^BOOT2' game/SYSTEM.CNF | head -n 1 | tr -d '\\r')\n"
           "  # BOOT2 = cdrom0:\\SLUS_203.12;1  ->  SLUS_203.12 (a subfolder becomes DIR/NAME)\n"
           "  ELF=$(printf '%s' \"$line\" | sed -e 's/^[^=]*=[[:space:]]*//' -e 's/^cdrom0://' -e 's/;.*$//' \\\n"
           "        | tr '\\\\' '/' | sed -e 's#^/*##' -e 's/[[:space:]]*$//')\n"
           "  if [ -z \"$ELF\" ]; then\n"
           "    echo \"No BOOT2 line in game/SYSTEM.CNF: is this the whole extracted disc?\" >&2\n"
           "    exit 1\n"
           "  fi\n"
           "fi\n"
           "if [ ! -f \"game/$ELF\" ]; then\n"
           "  echo \"game/$ELF not found. Put your extracted disc in game/ (see PLAYERS.md).\" >&2\n"
           "  exit 1\n"
           "fi\n"
           "exec \"./" + runner + "\" \"game/$ELF\" \"$@\"\n";
}

inline std::string launch_bat(const std::string& runner, const std::string& elf) {
    return join(std::vector<std::string>{
                    "@echo off",
                    "rem Starts the game with YOUR disc: put your extracted disc in game\\ first (see PLAYERS.md).",
                    "setlocal",
                    "cd /d \"%~dp0\"",
                    "set \"ELF=" + elf + "\"",
                    "if not \"%ELF%\"==\"\" goto run",
                    "if not exist \"game\\SYSTEM.CNF\" (",
                    "  echo Put your extracted disc in game\\ first ^(see PLAYERS.md^).",
                    "  exit /b 1",
                    ")",
                    "rem BOOT2 = cdrom0:\\SLUS_203.12;1 : keep what follows the first backslash, then drop ;1",
                    "for /f \"tokens=1,* delims=\\\" %%A in ('findstr /b /i \"BOOT2\" \"game\\SYSTEM.CNF\"') do set \"ELF=%%B\"",
                    "if \"%ELF%\"==\"\" (",
                    "  echo No BOOT2 line in game\\SYSTEM.CNF: is this the whole extracted disc?",
                    "  exit /b 1",
                    ")",
                    "set \"ELF=%ELF:;1=%\"",
                    ":run",
                    "if not exist \"game\\%ELF%\" (",
                    "  echo game\\%ELF% not found. Put your extracted disc in game\\ ^(see PLAYERS.md^).",
                    "  exit /b 1",
                    ")",
                    "\"%~dp0" + runner + "\" \"game\\%ELF%\" %*",
                    "",
                },
                "\r\n");
}

inline std::string install_android_sh(const std::string& apk) {
    return "#!/bin/sh\n"
           "# Installs the port on a phone, tablet or TV box with USB or wireless debugging on,\n"
           "# then copies YOUR extracted disc from game/ to the folder the app reads.\n"
           "cd \"$(dirname \"$0\")\" || exit 1\n"
           "command -v adb >/dev/null || { echo \"adb not found: install Android platform-tools\" >&2; exit 1; }\n"
           "if [ -z \"$(ls -A game 2>/dev/null | grep -v '" + PLACEHOLDER + "')\" ]; then\n"
           "  echo \"Put your extracted disc in game/ first (see PLAYERS.md).\" >&2\n"
           "  exit 1\n"
           "fi\n"
           "adb install -r \"" + apk + "\" || exit 1\n"
           "adb shell mkdir -p " + ANDROID_FILES + "\n"
           "adb push game/. " + ANDROID_FILES + "/\n";
}

inline std::string install_android_bat(const std::string& apk) {
    return join(std::vector<std::string>{
                    "@echo off",
                    "rem Installs the port with adb, then copies YOUR extracted disc from game\\ to the device.",
                    "setlocal",
                    "cd /d \"%~dp0\"",
                    "where adb >nul 2>nul || (echo adb not found: install Android platform-tools & exit /b 1)",
                    "if not exist \"game\\SYSTEM.CNF\" (echo Put your extracted disc in game\\ first ^(see PLAYERS.md^). & exit /b 1)",
                    "adb install -r \"" + apk + "\" || exit /b 1",
                    "adb shell mkdir -p " + ANDROID_FILES,
                    "adb push game\\. " + ANDROID_FILES + "/",
                    "",
                },
                "\r\n");
}

inline std::pair<std::string, std::string> target_howto(const std::string& t) {
    if (t == "windows")
        return {"Double-click play.bat (or run it from a terminal). It starts {{RUNNER}} with game\\{{ELF_SHOWN}}.", "Windows 10 or 11, x64."};
    if (t == "macos")
        return {"Double-click play.command (or run ./play.command in Terminal). It starts {{RUNNER}} with game/{{ELF_SHOWN}}. If macOS blocks it "
                "the first time, right-click play.command and choose Open.",
                "A Mac of the same kind it was built on (Apple silicon or Intel)."};
    if (t == "linux") return {"Run ./play.sh. It starts {{RUNNER}} with game/{{ELF_SHOWN}}.", "Linux x86_64 with OpenGL."};
    if (t == "steamdeck")
        return {"Desktop Mode: Steam, Add a Non-Steam Game, pick play.sh (show all file types), then start it from Game Mode. Steam Input makes "
                "the Deck's controls an Xbox-style pad.",
                "Steam Deck (SteamOS) or any Linux x86_64 PC."};
    if (t == "android")
        return {"With USB debugging on (or wireless debugging plus adb connect), run install-android.sh (or install-android.bat on Windows). It "
                "installs {{RUNNER}} and copies game/ to {{ANDROID_FILES}}. The app looks for {{ANDROID_ELF}} there.",
                "Android 9 or newer, arm64 (x86_64 for emulators). Pair a controller: there are no on-screen buttons."};
    return {"Install {{RUNNER}} with VitaShell, then copy everything inside game/ to {{VITA_DIR}} on the Vita (VitaShell's FTP or USB mode). The "
            "vpk looks for {{VITA_ELF}}.",
            "A PS Vita that already runs homebrew, with VitaShell."};
}

inline void add_exec(const std::string& path, bool all) {
    std::error_code ec;
    using P = std::filesystem::perms;
    std::filesystem::permissions(fspath(path), all ? (P::owner_exec | P::group_exec | P::others_exec) : P::owner_exec,
                                 std::filesystem::perm_options::add, ec);
}
inline void write_out(const std::string& path, const std::string& text, bool executable = false) {
    pyrt::write_text(path, text);
    if (executable) add_exec(path, true);
}
inline void copy2(const std::string& src, const std::string& dst) {   // shutil.copy2: bytes, permission bits, modification time
    std::error_code ec;
    std::filesystem::copy_file(fspath(src), fspath(dst), std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) throw OSError(ec.value(), src);
    std::error_code e2;
    const auto perms = std::filesystem::status(fspath(src), e2).permissions();
    if (!e2) std::filesystem::permissions(fspath(dst), perms, e2);
    const auto t = std::filesystem::last_write_time(fspath(src), e2);
    if (!e2) std::filesystem::last_write_time(fspath(dst), t, e2);
}
inline void rmtree(const std::string& p, bool ignore_errors) {
    std::error_code ec;
    std::filesystem::remove_all(fspath(p), ec);
    if (ec && !ignore_errors) throw OSError(ec.value(), p);
}
inline std::string utc_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

inline int cmd_package(const Val& a) {
    const std::string runner = S(a, "runner"), target = S(a, "target");
    if (!is_file(runner)) throw UsageError("runner not found: " + runner + " (build it first: targets.py plan --target " + target + ")");
    const std::string kind = detect_binary(runner);
    const std::string low = lower(runner);
    List warnings;
    if (target == "android" && !ends_with(low, ".apk"))
        throw UsageError("the Android runner is the APK (android/app/build/outputs/apk/release/app-release.apk)");
    if (target == "vita" && !ends_with(low, ".vpk")) throw UsageError("the Vita runner is the .vpk that vita/build.sh makes");
    if (target == "windows" && !ends_with(low, ".exe")) throw UsageError("the Windows runner is ps2EntryRunner.exe");
    if (kind == "mips") throw UsageError(runner + " is a PS2 (MIPS) ELF: that is the game, not the runner");
    std::vector<std::string> expect;
    if (target == "windows") expect = {"windows"};
    else if (target == "macos") expect = {"macos"};
    else if (target == "linux") expect = {"linux", "arm64-linux"};
    else if (target == "steamdeck") expect = {"linux"};
    if (!expect.empty()) {
        if (kind.empty())
            warnings.push_back(Val::str(runner + " is not a recognised executable; check it is the runner built for " + target));
        else if (!has_s(expect, kind))
            throw UsageError(runner + " looks like a " + kind + " binary, not a " + target + " one");
    }
    const std::string elf = S(a, "elf_name");
    if (!elf.empty() && (elf.find('/') != std::string::npos || elf.find('\\') != std::string::npos || elf[0] == '.'))
        throw UsageError("--elf-name is just the ELF's file name, like SLUS_203.12");
    std::vector<std::string> libs;
    const Val lv = get(a, "lib");
    if (lv.t == Val::LIST)
        for (const auto& x : *lv.l) libs.push_back(x.s);
    for (const auto& lib : libs)
        if (!is_file(lib)) throw UsageError("--lib file not found: " + lib);
    const Rules allow = load_allow(a);
    Val meta;
    const std::string game = S(a, "game"), serial = S(a, "serial");
    if (!game.empty() || !serial.empty()) {
        if (game.empty() || serial.empty()) throw UsageError("--game and --serial go together (they fill .recomp.json)");
        meta = build_recomp_json(game, serial, S(a, "status"), S(a, "region"), Val());
    }

    const std::string outd = abspath(S(a, "out"));
    if (pyrt::exists(outd)) {
        if (!is_dir(outd)) throw UsageError(outd + " exists and is not a folder");
        if (!listdir(outd).empty()) {
            if (!pyrt::exists(path_join(outd, DIST_MARKER))) throw UsageError(outd + " is not empty and was not made by this tool; pick another --out");
            if (!flag(a, "replace")) throw UsageError(outd + " already holds a package; pass --replace to rebuild it");
            const std::string old_game = path_join(outd, "game");
            if (is_dir(old_game))
                for (const auto& n : listdir(old_game))
                    if (n != PLACEHOLDER)
                        throw UsageError(outd + "/game/ holds files (your disc, or saves in mc0/): move them somewhere safe first, or package to a new "
                                                "--out. Nothing in game/ is ever deleted");
        }
    }
#if defined(_WIN32)
    const long long pid = static_cast<long long>(GetCurrentProcessId());
#else
    const long long pid = static_cast<long long>(getpid());
#endif
    const std::string staging = outd + ".staging-" + std::to_string(pid);
    if (pyrt::exists(staging)) rmtree(staging, false);
    make_dirs(path_join(staging, "game"));
    Val manifest;
    List allowed;
    try {
        const std::string runner_name = host_basename(runner);
        copy2(runner, path_join(staging, runner_name));
        std::vector<std::string> copied = {runner_name};
        std::string lib_ext;
        if (target == "windows") lib_ext = ".dll";
        else if (target == "macos") lib_ext = ".dylib";
        else if (target == "linux" || target == "steamdeck") lib_ext = ".so";
        const std::string runner_dir = dirname(abspath(runner));
        if (!lib_ext.empty()) {
            std::vector<std::string> names = listdir(runner_dir);
            std::sort(names.begin(), names.end());
            for (const auto& name : names) {
                const std::string full = path_join(runner_dir, name), nlow = lower(name);
                if (!is_file(full) || name == runner_name) continue;
                if (ends_with(nlow, lib_ext) || ((target == "linux" || target == "steamdeck") && nlow.find(".so.") != std::string::npos)) {
                    copy2(full, path_join(staging, name));
                    copied.push_back(name);
                }
            }
        }
        for (const auto& lib : libs) {
            copy2(lib, path_join(staging, host_basename(lib)));
            copied.push_back(host_basename(lib));
        }

        const std::string elf_shown = elf.empty() ? "<the ELF named in your disc's SYSTEM.CNF>" : elf;
        write_out(path_join(path_join(staging, "game"), PLACEHOLDER),
                  "Put the files from YOUR OWN disc here (the whole extracted disc: SYSTEM.CNF, the main ELF" + (elf.empty() ? "" : " " + elf) +
                      ", the folders).\nSee PLAYERS.md. Nothing from the game ships with this folder.\n");
        std::vector<std::string> scripts;
        if (target == "windows") {
            write_out(path_join(staging, "play.bat"), launch_bat(runner_name, elf));
            scripts.push_back("play.bat");
        } else if (target == "macos") {
            write_out(path_join(staging, "play.command"), launch_posix(runner_name, elf), true);
            scripts.push_back("play.command");
        } else if (target == "linux" || target == "steamdeck") {
            write_out(path_join(staging, "play.sh"), launch_posix(runner_name, elf), true);
            scripts.push_back("play.sh");
        } else if (target == "android") {
            write_out(path_join(staging, "install-android.sh"), install_android_sh(runner_name), true);
            write_out(path_join(staging, "install-android.bat"), install_android_bat(runner_name));
            scripts.push_back("install-android.sh");
            scripts.push_back("install-android.bat");
        }
#if !defined(_WIN32)
        add_exec(path_join(staging, runner_name), false);
#endif
        std::vector<std::string> deps;
        if ((target == "macos" || target == "linux" || target == "steamdeck") && needs_ffmpeg(runner))
            deps.push_back(std::string("FFmpeg's libraries: ") +
                           (target == "macos" ? "brew install ffmpeg" : "sudo apt install ffmpeg (or your distro's ffmpeg package)"));
        auto [howto, needs] = target_howto(target);
        std::string game_name = meta.t == Val::DICT ? get(meta, "game").s : "";
        if (game_name.empty()) game_name = S(a, "name");
        if (game_name.empty()) game_name = "this game";
        const std::string saves = target == "android" ? "`" + ANDROID_FILES + "/mc0/` on the device"
                                  : target == "vita"  ? "`" + VITA_DEFAULT_DIR + "/mc0/` on the Vita"
                                  : target == "windows" ? std::string("`game\\mc0\\`")
                                                        : std::string("`game/mc0/`");
        Values values = {
            {"GAME", game_name},
            {"TARGET", target_title(target)},
            {"RUNNER", runner_name},
            {"ELF_SHOWN", elf_shown},
            {"ANDROID_FILES", ANDROID_FILES},
            {"ANDROID_ELF", ANDROID_FILES + "/" + (elf.empty() ? "<the ELF name it was built with>" : elf)},
            {"VITA_DIR", VITA_DEFAULT_DIR + "/"},
            {"VITA_ELF", VITA_DEFAULT_DIR + "/" + (elf.empty() ? "<the ELF name it was built with>" : elf)},
            {"SERIAL", meta.t == Val::DICT ? get(get(meta, "original"), "serial").s : "the serial on your disc"},
            {"SAVES", saves},
        };
        howto = render(howto, values);
        values.emplace_back("HOW_TO_RUN", howto);
        values.emplace_back("NEEDS", needs + (deps.empty() ? "" : " Also: " + join(deps, "; ") + "."));
        std::vector<std::string> listed;
        for (const auto& f : copied) listed.push_back("- `" + f + "`");
        for (const auto& f : scripts) listed.push_back("- `" + f + "`");
        values.emplace_back("FILES", join(listed, "\n"));
        const std::string tpl_path = skill_data("templates/PLAYERS.md");
        const std::string tpl_raw = read_bytes(tpl_path);
        if (const auto u = packtools::console_run::utf8_error(tpl_raw)) throw PyErr("UnicodeDecodeError", *u);
        const std::string players = render(read_text(tpl_path), values);
        write_out(path_join(staging, "PLAYERS.md"), players);
        if (meta.t == Val::DICT) write_out(path_join(staging, ".recomp.json"), dumps2(meta) + "\n");

        List findings;
        scan_folder(staging, allow, findings, allowed);
        if (!findings.empty()) {
            out("package REFUSED: " + std::to_string(findings.size()) + " file(s) look like game data");
            print_findings(findings, allowed);
            rmtree(staging, true);
            return 1;
        }
        std::vector<std::string> files = copied;
        files.insert(files.end(), scripts.begin(), scripts.end());
        files.push_back("PLAYERS.md");
        if (meta.t == Val::DICT) files.push_back(".recomp.json");
        std::sort(files.begin(), files.end());
        manifest = dict_of({{"tool", Val::str("console-anywhere targets.py")}, {"target", Val::str(target)}, {"runner", Val::str(runner_name)},
                            {"elf", elf.empty() ? Val() : Val::str(elf)}, {"files", str_list(files)}, {"allowed", Val::list(allowed)},
                            {"created", Val::str(utc_now())}});
        write_out(path_join(staging, DIST_MARKER), dumps2(manifest) + "\n");
        if (pyrt::exists(outd)) rmtree(outd, false);
        make_dirs(dirname(outd));
        std::error_code ec;
        std::filesystem::rename(fspath(staging), fspath(outd), ec);
        if (ec) throw OSError(ec.value(), staging);
    } catch (...) {
        rmtree(staging, true);
        throw;
    }

    if (flag(a, "json")) {
        out(dumps2(dict_of({{"ok", Val::boolean(true)}, {"out", Val::str(outd)}, {"manifest", manifest}, {"warnings", Val::list(warnings)}})));
    } else {
        for (const auto& w : warnings) out("warning: " + w.s);
        for (const auto& x : allowed)
            out("allowed  " + get(x, "path").s + ": " + get(x, "reason").s + " (allow file: " + get(x, "allowed_because").s + ")");
        out("packaged " + outd + " for " + target_title(target));
        for (const auto& f : *get(manifest, "files").l) out("  " + f.s);
        out("  game/  (empty: the player puts their own extracted disc here)");
        out("The runner is built from your game's recompiled code: this folder is for your own devices. Do not upload or share it.");
    }
    return 0;
}

// ======================================================================= CLI

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Build and package a PS2Recomp port for Windows, Mac, Linux, Steam Deck, Android and PS Vita, with no game files inside.");
    p.epilog = "Typical: targets.py doctor --target android; targets.py plan --target android --elf-name SLUS_203.12; targets.py package "
               "--runner app-release.apk --target android --elf-name SLUS_203.12 --out console/dist/mygame-android";
    p.subcommands("cmd", false); p.sub_metavar = "command";
    { Parser& s = p.sub("doctor"); s.help_line = "check this machine's tools for a target";
      std::vector<std::string> choices = TARGETS; choices.push_back("all");
      s.opt({"--target"}).append().choices(choices).help("repeatable; default: this machine's own OS");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("plan"); s.help_line = "print the build steps for a target";
      s.opt({"--target"}).required().choices(TARGETS);
      s.opt({"--repo"}).def("PS2Recomp").help("your PS2Recomp checkout (default PS2Recomp)");
      s.opt({"--build-dir"}).def("out/build").help("your build folder (default out/build)");
      s.opt({"--elf-name"}).help("the disc's main ELF name, like SLUS_203.12");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("package"); s.help_line = "build a player folder with no game files inside";
      s.opt({"--runner"}).required().help("ps2EntryRunner(.exe), the APK, or the .vpk");
      s.opt({"--target"}).required().choices(PACKAGE_TARGETS);
      s.opt({"--out"}).required().help("output folder, like console/dist/mygame-windows");
      s.opt({"--elf-name"}).help("the ELF name the launch script passes (else read from SYSTEM.CNF)");
      s.opt({"--name"}).help("what PLAYERS.md calls the game");
      s.opt({"--game"}).help("title as it shipped (for .recomp.json)");
      s.opt({"--serial"}).help("disc serial, like SLUS-20312 (for .recomp.json)");
      s.opt({"--status"}).def("in-progress").choices(STATUSES);
      s.opt({"--region"}).help("only if the serial prefix does not say");
      s.opt({"--lib"}).append().def(Val::list({})).help("an extra library to ship next to the runner");
      s.opt({"--allow"}).help("allow file: '<path or pattern>  # reason' per line");
      s.opt({"--replace"}).store_true().help("rebuild a package this tool made before");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("guard"); s.help_line = "check any folder for game data before you zip or push it";
      s.pos("folder");
      s.opt({"--allow"}).help("allow file: '<path or pattern>  # reason' per line");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("recomp-json"); s.help_line = "write the optional .recomp.json";
      s.opt({"--game"}).required().help("title as it shipped");
      s.opt({"--serial"}).required().help("like SLUS-20312 or SLUS_203.12");
      s.opt({"--status"}).def("in-progress").choices(STATUSES);
      s.opt({"--region"}).help("only if the serial prefix does not say");
      s.opt({"--out"}).def(".recomp.json"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { std::fflush(stdout); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "doctor") return cmd_doctor(a);
        if (cmd == "plan") return cmd_plan(a);
        if (cmd == "package") return cmd_package(a);
        if (cmd == "guard") return cmd_guard(a);
        return cmd_recomp_json(a);
    } catch (const UsageError& e) {
        std::fflush(stdout); std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}

PACKTOOL("console-anywhere/targets", main);
}  // namespace packtools::targets
