// A.S.T.R.O AI desktop launcher.
//
// A small native window (system webview: WebView2 on Windows, WebKitGTK on Linux, WKWebView on macOS)
// that starts the bundled A.S.T.R.O AI C++ server on 127.0.0.1, supervises it, and shows the app in the
// OpenCode-style desktop shell. No Electron, no Python, no Node at runtime.
//
// Resources (next to the executable, or Contents/Resources on macOS):
//   web/  providers.json  astro-server[.exe]  edition.json
//   gateway.env      (personal edition only: LLM_* gateway settings, exported to the server)
//   seed-keys.env    (personal edition only: provider keys, copied to the data folder on first run)
// Each edition has its own data folder, local port and webview profile, so the clean edition never
// sees the personal edition's keys or chats.

#include "webview/webview.h"

#include <atomic>
#include <cerrno>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <glib.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr const char* kVersion = "1.8.1";

// ------------------------------------------------------------------------------------------------
// Small utilities
// ------------------------------------------------------------------------------------------------
#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
fs::path upath(const std::string& s) { return fs::path(widen(s)); }
std::string pstr(const fs::path& p) { return narrow(p.wstring()); }
void set_env(const std::string& k, const std::string& v) { SetEnvironmentVariableW(widen(k).c_str(), widen(v).c_str()); }
std::string get_env(const char* k) {
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(widen(k).c_str(), buf, 32768);
    return n && n < 32768 ? narrow(std::wstring(buf, n)) : std::string();
}
#else
fs::path upath(const std::string& s) { return fs::path(s); }
std::string pstr(const fs::path& p) { return p.string(); }
void set_env(const std::string& k, const std::string& v) { setenv(k.c_str(), v.c_str(), 1); }
std::string get_env(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }
#endif

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}
bool write_bytes(const fs::path& p, const std::string& data, bool private_file = false) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), (std::streamsize)data.size());
    out.close();
#ifndef _WIN32
    if (private_file) chmod(p.c_str(), 0600);
#else
    (void)private_file;
#endif
    return static_cast<bool>(out);
}
std::string json_field(const std::string& json, const std::string& key) {
    std::smatch m;
    std::regex re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    return std::regex_search(json, m, re) ? m[1].str() : std::string();
}
int json_int_field(const std::string& json, const std::string& key, int fallback) {
    std::smatch m;
    std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
    return std::regex_search(json, m, re) ? std::atoi(m[1].str().c_str()) : fallback;
}
std::string html_escape(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) {
        switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; default: o += c; }
    }
    return o;
}
// KEY=VALUE lines (comments, blank lines and optional quotes allowed).
std::vector<std::pair<std::string, std::string>> read_env_file(const fs::path& p) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(read_text(p));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#') continue;
        line = line.substr(a);
        if (line.rfind("export ", 0) == 0) line = line.substr(7);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) v = v.substr(1, v.size() - 2);
        if (!k.empty()) out.emplace_back(k, v);
    }
    return out;
}
std::string b64decode(const std::string& in) {
    static int T[256]; static bool init = false;
    if (!init) {
        for (int& t : T) t = -1;
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(unsigned char)a[i]] = i;
        T[(unsigned char)'-'] = 62; T[(unsigned char)'_'] = 63;
        init = true;
    }
    std::string out; out.reserve(in.size() * 3 / 4);
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] < 0) continue;
        val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}
// File names coming from the page: keep the base name and drop characters no OS likes.
std::string safe_name(std::string n) {
    size_t s = n.find_last_of("/\\");
    if (s != std::string::npos) n = n.substr(s + 1);
    std::string o;
    for (unsigned char c : n) o += (c < 32 || std::strchr("<>:\"|?*", c)) ? '_' : char(c);
    while (!o.empty() && (o.back() == '.' || o.back() == ' ')) o.pop_back();
    while (!o.empty() && (o.front() == '.' || o.front() == ' ')) o.erase(o.begin());
    if (o.empty()) o = "download";
    if (o.size() > 150) o = o.substr(o.size() - 150);
    return o;
}
fs::path unique_path(const fs::path& dir, const std::string& name) {
    fs::path p = dir / upath(name);
    if (!fs::exists(p)) return p;
    fs::path stem = p.stem(), ext = p.extension();
    for (int i = 1; i < 1000; ++i) {
        fs::path q = dir / (stem.native() + fs::path(" (" + std::to_string(i) + ")").native() + ext.native());
        if (!fs::exists(q)) return q;
    }
    return p;
}

// ------------------------------------------------------------------------------------------------
// Paths
// ------------------------------------------------------------------------------------------------
fs::path exe_path() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    buf.resize(n);
    return fs::path(buf);
#elif defined(__APPLE__)
    uint32_t size = 0; _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0'); _NSGetExecutablePath(buf.data(), &size);
    std::error_code ec; auto p = fs::canonical(buf.c_str(), ec);
    return ec ? fs::path(buf.c_str()) : p;
#else
    std::error_code ec; auto p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p;
#endif
}
fs::path resources_dir() {
    fs::path d = exe_path().parent_path();
    for (const fs::path& c : {d, d / ".." / "Resources", d / "resources", d / ".." / "lib" / "astro-ai"}) {
        std::error_code ec;
        if (fs::exists(c / "web" / "index.html", ec)) return fs::weakly_canonical(c, ec);
    }
    return d;
}
fs::path home_dir() {
#ifdef _WIN32
    return upath(get_env("USERPROFILE"));
#else
    std::string h = get_env("HOME");
    return h.empty() ? fs::path("/tmp") : fs::path(h);
#endif
}
fs::path data_root(const std::string& win_mac_name, const std::string& linux_name) {
#ifdef _WIN32
    PWSTR p = nullptr; fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) base = fs::path(p);
    if (p) CoTaskMemFree(p);
    if (base.empty()) base = upath(get_env("APPDATA"));
    return base / upath(win_mac_name);
#elif defined(__APPLE__)
    (void)linux_name;
    return home_dir() / "Library" / "Application Support" / win_mac_name;
#else
    (void)win_mac_name;
    std::string x = get_env("XDG_DATA_HOME");
    return (x.empty() ? home_dir() / ".local" / "share" : fs::path(x)) / linux_name;
#endif
}
fs::path downloads_dir() {
#ifdef _WIN32
    PWSTR p = nullptr; fs::path d;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) d = fs::path(p);
    if (p) CoTaskMemFree(p);
    if (d.empty()) d = home_dir() / "Downloads";
    return d;
#elif defined(__APPLE__)
    return home_dir() / "Downloads";
#else
    std::string cfg = get_env("XDG_CONFIG_HOME");
    fs::path f = (cfg.empty() ? home_dir() / ".config" : fs::path(cfg)) / "user-dirs.dirs";
    for (auto& [k, v] : read_env_file(f)) {
        if (k != "XDG_DOWNLOAD_DIR") continue;
        std::string s = v;
        if (s.rfind("$HOME", 0) == 0) s = pstr(home_dir()) + s.substr(5);
        if (!s.empty()) return fs::path(s);
    }
    return home_dir() / "Downloads";
#endif
}

// ------------------------------------------------------------------------------------------------
// Opening things with the OS
// ------------------------------------------------------------------------------------------------
#ifndef _WIN32
void spawn_detached(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid == 0) {
        if (fork() == 0) {
            setsid();
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
            std::vector<char*> a;
            for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
            a.push_back(nullptr);
            execvp(a[0], a.data());
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0) waitpid(pid, nullptr, 0);
}
#endif
void os_open(const std::string& target) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", widen(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    spawn_detached({"open", target});
#else
    spawn_detached({"xdg-open", target});
#endif
}
void os_reveal(const fs::path& p) {
#ifdef _WIN32
    std::wstring args = L"/select,\"" + p.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    spawn_detached({"open", "-R", p.string()});
#else
    spawn_detached({"xdg-open", (fs::is_directory(p) ? p : p.parent_path()).string()});
#endif
}

// ------------------------------------------------------------------------------------------------
// Local networking (port checks and a tiny HTTP GET for /api/status)
// ------------------------------------------------------------------------------------------------
#ifdef _WIN32
using sock_t = SOCKET;
void close_sock(sock_t s) { closesocket(s); }
#else
using sock_t = int;
void close_sock(sock_t s) { close(s); }
#endif
sock_t connect_local(int port, int timeout_ms) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    DWORD tv = (DWORD)timeout_ms;
#else
    if (s < 0) return -1;
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
#endif
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) {
        close_sock(s);
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }
    return s;
}
bool sock_ok(sock_t s) {
#ifdef _WIN32
    return s != INVALID_SOCKET;
#else
    return s >= 0;
#endif
}
bool port_open(int port) {
    sock_t s = connect_local(port, 400);
    if (!sock_ok(s)) return false;
    close_sock(s);
    return true;
}
std::string http_get_local(int port, const std::string& path) {
    sock_t s = connect_local(port, 1500);
    if (!sock_ok(s)) return {};
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nx-astro-client: 1\r\nConnection: close\r\n\r\n";
    send(s, req.data(), (int)req.size(), 0);
    std::string out; char buf[8192];
    for (;;) {
        int n = (int)recv(s, buf, sizeof(buf), 0);
        if (n <= 0 || out.size() > (1u << 20)) break;
        out.append(buf, n);
    }
    close_sock(s);
    return out;
}

// ------------------------------------------------------------------------------------------------
// The server process
// ------------------------------------------------------------------------------------------------
struct Config {
    std::string edition, name, instance;
    fs::path res, data, server, log;
    int port = 0;
};

class Server {
public:
    explicit Server(const Config& c) : cfg_(c) {
#ifdef _WIN32
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof(info));
#endif
    }
    // Starts the process; returns false if it could not even be launched.
    bool start() {
        std::lock_guard<std::mutex> lock(mu_);
        std::error_code ec;
        fs::create_directories(cfg_.log.parent_path(), ec);
        // Keep the log small: rotate at 2 MB.
        if (fs::exists(cfg_.log, ec) && fs::file_size(cfg_.log, ec) > 2u * 1024 * 1024) {
            fs::path old = cfg_.log; old += ".1";
            fs::remove(old, ec); fs::rename(cfg_.log, old, ec);
        }
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE log = CreateFileW(cfg_.log.wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        si.hStdInput = nul; si.hStdOutput = log; si.hStdError = log;
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + cfg_.server.wstring() + L"\"";
        BOOL ok = CreateProcessW(cfg_.server.wstring().c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                 cfg_.res.wstring().c_str(), &si, &pi);
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!ok) return false;
        AssignProcessToJobObject(job_, pi.hProcess);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        proc_ = pi.hProcess;
        return true;
#else
        int logfd = open(cfg_.log.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        pid_t parent = getpid();
        pid_t pid = fork();
        if (pid < 0) { if (logfd >= 0) close(logfd); return false; }
        if (pid == 0) {
#ifdef __linux__
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (getppid() != parent) _exit(1);
#endif
            (void)parent;
            if (chdir(cfg_.res.c_str()) != 0) _exit(126);
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull >= 0) dup2(devnull, 0);
            if (logfd >= 0) { dup2(logfd, 1); dup2(logfd, 2); }
            signal(SIGPIPE, SIG_DFL);
            execl(cfg_.server.c_str(), cfg_.server.c_str(), (char*)nullptr);
            _exit(127);
        }
        if (logfd >= 0) close(logfd);
        pid_ = pid;
        return true;
#endif
    }
    // Blocks until the process exits; returns its exit code (or -1).
    int wait() {
#ifdef _WIN32
        HANDLE h; { std::lock_guard<std::mutex> lock(mu_); h = proc_; }
        if (!h) return -1;
        WaitForSingleObject(h, INFINITE);
        DWORD code = 1; GetExitCodeProcess(h, &code);
        std::lock_guard<std::mutex> lock(mu_);
        CloseHandle(proc_); proc_ = nullptr;
        return (int)code;
#else
        pid_t p; { std::lock_guard<std::mutex> lock(mu_); p = pid_; }
        if (p <= 0) return -1;
        int status = 0;
        while (waitpid(p, &status, 0) < 0 && errno == EINTR) {}
        std::lock_guard<std::mutex> lock(mu_);
        pid_ = -1;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
    }
    void stop() {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(mu_);
        if (job_) TerminateJobObject(job_, 0);
#else
        pid_t p; { std::lock_guard<std::mutex> lock(mu_); p = pid_; }
        if (p <= 0) return;
        kill(p, SIGTERM);
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(50ms);
            std::lock_guard<std::mutex> lock(mu_);
            if (pid_ <= 0) return;
        }
        kill(p, SIGKILL);
#endif
    }
private:
    Config cfg_;
    std::mutex mu_;
#ifdef _WIN32
    HANDLE job_ = nullptr;
    HANDLE proc_ = nullptr;
#else
    pid_t pid_ = -1;
#endif
};

std::string log_tail(const fs::path& p, size_t max = 4000) {
    std::string s = read_text(p);
    return s.size() > max ? s.substr(s.size() - max) : s;
}

// ------------------------------------------------------------------------------------------------
// Pages shown before the app loads
// ------------------------------------------------------------------------------------------------
const char* kStyle = R"CSS(
html,body{margin:0;height:100%;background:#0a0a0a;color:#eee;font:13px/1.5 "JetBrains Mono",ui-monospace,Menlo,Consolas,monospace}
@media (prefers-color-scheme: light){html,body{background:#fff;color:#161616}.muted{color:#767676!important}pre{background:#f4f4f4!important;border-color:#e6e6e6!important}}
.wrap{height:100%;display:grid;place-items:center;text-align:center}
.mark{width:72px;height:72px}
.core{transform-origin:11.7px 12.2px;animation:pulse 1.6s ease-in-out infinite}
.moon{animation:blink 1.6s ease-in-out infinite}
@keyframes pulse{50%{transform:scale(.9)}}
@keyframes blink{50%{opacity:.35}}
h1{font-size:20px;letter-spacing:-.03em;margin:18px 0 4px}
.muted{color:#7d7d7d}
pre{text-align:left;max-width:760px;max-height:300px;overflow:auto;background:#141414;border:1px solid #1e1e1e;border-radius:8px;padding:12px;white-space:pre-wrap;font-size:11.5px}
button{font:inherit;padding:7px 14px;border-radius:7px;border:1px solid #2c2c2c;background:#1a1a1a;color:inherit;cursor:pointer;margin:0 4px}
button.p{background:#fab283;border-color:#fab283;color:#1f1006}
)CSS";

std::string splash_html(const std::string& name, const std::string& edition) {
    return std::string("<!doctype html><html><head><meta charset=utf-8><style>") + kStyle +
           "</style></head><body><div class=wrap><div>"
           "<svg class=mark viewBox='0 0 24 24'><defs><linearGradient id=g x1=0 y1=0 x2=1 y2=1><stop offset=0 stop-color='#ffd2b0'/><stop offset=1 stop-color='#f08a4b'/></linearGradient></defs>"
           "<ellipse cx=12 cy=12.4 rx=10.2 ry=6.6 fill=none stroke='#f3a679' stroke-width=0.8 /><circle class=core cx=11.7 cy=12.2 r=4.3 fill='url(#g)' />"
           "<circle class=moon cx=16.4 cy=6.45 r=1.55 fill='#fab283' /></svg>"
           "<h1>" + html_escape(name) + "</h1><div class=muted>" +
           (edition == "clean" ? "Clean edition &middot; " : "") + "starting the local brain&hellip;</div></div></div></body></html>";
}
std::string error_html(const std::string& name, const std::string& why, const std::string& tail) {
    return std::string("<!doctype html><html><head><meta charset=utf-8><style>") + kStyle +
           "</style></head><body><div class=wrap><div style='padding:24px'>"
           "<h1>" + html_escape(name) + " could not start</h1><p class=muted>" + html_escape(why) + "</p>"
           "<pre>" + html_escape(tail.empty() ? "(no log output)" : tail) + "</pre>"
           "<p><button class=p onclick='astroRetry()'>Try again</button><button onclick='astroOpenDataFolder()'>Open data folder</button></p>"
           "</div></div></body></html>";
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// main
// ------------------------------------------------------------------------------------------------
static int run_app();

// The window cannot open (no WebView2 runtime on Windows, no display / WebKitGTK on Linux): say so
// in plain words instead of crashing.
static void fatal(const std::string& what) {
#ifdef _WIN32
    const std::string msg = "A.S.T.R.O AI could not open its window.\n\n" + what +
        "\n\nA.S.T.R.O AI uses the Microsoft Edge WebView2 Runtime (built into Windows 10 and 11). "
        "Click OK to open the download page, install the Evergreen runtime, then start A.S.T.R.O AI again.";
    if (MessageBoxW(nullptr, widen(msg).c_str(), L"A.S.T.R.O AI", MB_OKCANCEL | MB_ICONERROR) == IDOK)
        ShellExecuteW(nullptr, L"open", L"https://developer.microsoft.com/microsoft-edge/webview2/", nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::fprintf(stderr, "A.S.T.R.O AI could not open its window: %s\n"
                         "Linux needs a graphical session and WebKitGTK 4.1 (libwebkit2gtk-4.1-0).\n", what.c_str());
#endif
}

// astroai://library/install?id=<id> (the website's "Open in A.S.T.R.O AI" button). Only a catalog id is taken from the
// link; the app opens its Library on that item, and the user still sees what it does and presses Install.
static std::string g_library_id;
static void take_deep_link(const std::string& a) {
    static const std::string pre[] = {"astroai://library/install?id=", "astroai://library/"};
    for (const auto& p : pre) {
        if (a.rfind(p, 0) != 0) continue;
        std::string id = a.substr(p.size());
        id = id.substr(0, id.find_first_of("&#/?"));
        if (id.empty() || id.size() > 48) return;
        for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return;
        g_library_id = id;
        return;
    }
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    {
        int n = 0;
        if (LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &n)) {
            for (int i = 1; i < n; ++i) {
                const int len = WideCharToMultiByte(CP_UTF8, 0, av[i], -1, nullptr, 0, nullptr, nullptr);
                std::string s8(len > 0 ? len - 1 : 0, '\0');
                if (len > 1) WideCharToMultiByte(CP_UTF8, 0, av[i], -1, s8.data(), len, nullptr, nullptr);
                take_deep_link(s8);
            }
            LocalFree(av);
        }
    }
    int rc = 1;
    try { rc = run_app(); } catch (const std::exception& e) { fatal(e.what()); }
    WSACleanup();
    return rc;
}
#else
int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);
    for (int i = 1; i < argc; ++i) take_deep_link(argv[i]);
    try { return run_app(); } catch (const std::exception& e) { fatal(e.what()); return 1; }
}
#endif

static int run_app() {
    Config cfg;
    cfg.res = resources_dir();
    const std::string edition_json = read_text(cfg.res / "edition.json");
    cfg.edition = json_field(edition_json, "edition");
    if (cfg.edition.empty()) cfg.edition = "clean";
    cfg.name = json_field(edition_json, "name");
    if (cfg.name.empty()) cfg.name = cfg.edition == "clean" ? "Astro AI Clean" : "Astro AI";
    // what people see (A.S.T.R.O AI); cfg.name stays the folder/data name so existing data keeps working
    std::string display = json_field(edition_json, "display");
    if (display.empty()) display = cfg.edition == "clean" ? "A.S.T.R.O AI Clean" : "A.S.T.R.O AI";
    std::string linux_name = json_field(edition_json, "data_name");
    if (linux_name.empty()) linux_name = cfg.edition == "clean" ? "astro-ai-clean" : "astro-ai";
    cfg.instance = linux_name;
    const int base_port = json_int_field(edition_json, "port", cfg.edition == "clean" ? 47612 : 47611);
    cfg.data = data_root(cfg.name, linux_name);
    std::string override_dir = get_env("ASTRO_DESKTOP_DATA");
    if (!override_dir.empty()) cfg.data = upath(override_dir);
    std::error_code ec;
    fs::create_directories(cfg.data / "logs", ec);
    cfg.log = cfg.data / "logs" / "server.log";
#ifdef _WIN32
    cfg.server = cfg.res / "astro-server.exe";
#else
    cfg.server = cfg.res / "astro-server";
#endif

    // Personal edition: seed the keys file once (later edits made in Settings > Keys are kept),
    // and export the gateway settings on every launch.
    if (fs::exists(cfg.res / "seed-keys.env") && !fs::exists(cfg.data / ".astro_keys.env"))
        write_bytes(cfg.data / ".astro_keys.env", read_text(cfg.res / "seed-keys.env"), true);
    for (auto& [k, v] : read_env_file(cfg.res / "gateway.env"))
        if (k.rfind("LLM_", 0) == 0) set_env(k, v);

    // Pick the port: our fixed port (so the webview keeps its storage), reuse our own server if it is
    // already running there (a second window), otherwise the first free fallback port.
    bool reuse = false;
    cfg.port = base_port;
    for (int p = base_port, tries = 0; tries < 20; ++tries, p = 47613 + tries + (cfg.edition == "clean" ? 20 : 0)) {
        if (!port_open(p)) { cfg.port = p; break; }
        std::string st = http_get_local(p, "/api/status");
        if (st.find("\"instance\":\"" + cfg.instance + "\"") != std::string::npos) { cfg.port = p; reuse = true; break; }
    }

    set_env("PORT", std::to_string(cfg.port));
    set_env("ASTRO_BIND", "127.0.0.1");
    set_env("ASTRO_DATA_DIR", pstr(cfg.data));
    set_env("ASTRO_AUTH", "off");
    set_env("ASTRO_SYNC_MIRROR", "off");
    set_env("ASTRO_SUPERVISED", "1");
    set_env("ASTRO_DESKTOP", "1");
    set_env("ASTRO_INSTANCE", cfg.instance);
#ifdef __linux__
    g_set_prgname(linux_name.c_str());
    g_set_application_name(display.c_str());
#endif

    // Hidden check for the build scripts: start + supervise + stop the server without a window.
    if (const std::string out = get_env("ASTRO_DESKTOP_HEADLESS_TEST"); !out.empty()) {
        Server s(cfg);
        std::string r = "{\"spawn\":" + std::string(s.start() ? "true" : "false");
        bool up = false;
        for (int i = 0; i < 200 && !up; ++i) { up = port_open(cfg.port); if (!up) std::this_thread::sleep_for(150ms); }
        const std::string st = http_get_local(cfg.port, "/api/status");
        r += ",\"port\":" + std::to_string(cfg.port) + ",\"up\":" + (up ? "true" : "false");
        r += ",\"instance_ok\":" + std::string(st.find("\"instance\":\"" + cfg.instance + "\"") != std::string::npos ? "true" : "false");
        std::thread waiter([&] { r += ",\"exit\":" + std::to_string(s.wait()); });
        s.stop();
        waiter.join();
        std::this_thread::sleep_for(300ms);
        r += ",\"port_closed\":" + std::string(port_open(cfg.port) ? "false" : "true");
        r += ",\"keys_seeded\":" + std::string(fs::exists(cfg.data / ".astro_keys.env") ? "true" : "false");
        r += ",\"log_bytes\":" + std::to_string(fs::exists(cfg.log) ? fs::file_size(cfg.log) : 0) + "}";
        write_bytes(upath(out), r);
        return 0;
    }

    const std::string platform =
#ifdef _WIN32
        "windows";
#elif defined(__APPLE__)
        "macos";
#else
        "linux";
#endif
    const std::string arch =
#if defined(__aarch64__) || defined(_M_ARM64)
        "arm64";   /* the updater picks the matching ARM64 installer */
#else
        "x64";
#endif
    const std::string info = std::string("{\"edition\":") + webview::detail::json_escape(cfg.edition) +
                             ",\"name\":" + webview::detail::json_escape(display) +
                             ",\"version\":" + webview::detail::json_escape(kVersion) +
                             ",\"platform\":" + webview::detail::json_escape(platform) +
                             ",\"arch\":" + webview::detail::json_escape(arch) +
                             ",\"port\":" + std::to_string(cfg.port) +
                             ",\"data_dir\":" + webview::detail::json_escape(pstr(cfg.data)) + "}";

    webview::webview w(false, nullptr);
    w.set_title(display);
    w.set_size(1280, 820, WEBVIEW_HINT_NONE);
    w.set_size(960, 640, WEBVIEW_HINT_MIN);
    w.init("window.__ASTRO_DESKTOP__ = " + info + ";");

    const fs::path temp_dir = fs::temp_directory_path(ec) / ("astro-desktop-" + linux_name);
    auto arg = [](const std::string& req, int i) { return webview::detail::json_parse(req, "", i); };
    w.bind("astroOpenExternal", [&](const std::string& req) -> std::string {
        const std::string url = arg(req, 0);
        if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || url.rfind("mailto:", 0) == 0) { os_open(url); return "true"; }
        return "false";
    });
    w.bind("astroSaveFile", [&](const std::string& req) -> std::string {
        const fs::path dir = downloads_dir();
        std::error_code e2; fs::create_directories(dir, e2);
        const fs::path p = unique_path(dir, safe_name(arg(req, 0)));
        if (!write_bytes(p, b64decode(arg(req, 1))))
            return "{\"ok\":false,\"error\":" + webview::detail::json_escape("cannot write " + pstr(p)) + "}";
        return "{\"ok\":true,\"path\":" + webview::detail::json_escape(pstr(p)) + ",\"name\":" + webview::detail::json_escape(pstr(p.filename())) +
               ",\"dir_name\":" + webview::detail::json_escape(pstr(dir.filename())) + "}";
    });
    w.bind("astroOpenData", [&](const std::string& req) -> std::string {
        const fs::path p = temp_dir / upath(safe_name(arg(req, 0)));
        if (!write_bytes(p, b64decode(arg(req, 1)))) return "false";
        os_open(pstr(p));
        return "true";
    });
    w.bind("astroReveal", [&](const std::string& req) -> std::string {
        const fs::path p = upath(arg(req, 0));
        const fs::path dl = downloads_dir();
        // Only reveal files we saved (Downloads) or our own folders.
        const std::string ps = pstr(p);
        if (ps.rfind(pstr(dl), 0) == 0 || ps.rfind(pstr(cfg.data), 0) == 0) os_reveal(p);
        return "true";
    });
    w.bind("astroOpenDataFolder", [&](const std::string&) -> std::string { os_open(pstr(cfg.data)); return "true"; });
    w.bind("astroGetInfo", [&](const std::string&) -> std::string { return info; });
    // 1.7 updates: the engine downloads the installer into <data>/updates and checks its SHA-256 against the
    // published latest.json; here we only start that file (it has to outlive the engine, which we own).
    w.bind("astroRunUpdate", [&](const std::string& req) -> std::string {
        const std::string name = safe_name(arg(req, 0));
        const fs::path p = cfg.data / "updates" / upath(name);
        std::error_code e3;
        if (name.empty() || !fs::is_regular_file(p, e3)) return "{\"ok\":false,\"error\":\"The update file is missing. Download it again.\"}";
#ifdef _WIN32
        if (p.extension() != ".exe") return "{\"ok\":false,\"error\":\"Not an installer.\"}";
        std::wstring cmd = L"\"" + p.wstring() + L"\" /S --launch";
        STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
        if (!ok) ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        if (!ok) return "{\"ok\":false,\"error\":\"Could not start the installer.\"}";
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        // Close now so the installer can replace our files; it starts the new version when it is done.
        w.dispatch([&w] { w.terminate(); });
        return "{\"ok\":true,\"restarting\":true}";
#else
        os_open(pstr(p));    // macOS mounts the DMG, Linux opens the package installer
        return "{\"ok\":true,\"opened\":true}";
#endif
    });
    // Hidden self-test used by the build checks: ASTRO_DESKTOP_SELFTEST=<result file>.
    const std::string selftest = get_env("ASTRO_DESKTOP_SELFTEST");
    if (!selftest.empty()) {
        w.bind("astroSelftestDone", [&w, selftest](const std::string& req) -> std::string {
            write_bytes(upath(selftest), webview::detail::json_parse(req, "", 0));
            w.dispatch([&w] { w.terminate(); });
            return "true";
        });
        w.init(R"JS(
          addEventListener('load', () => { if (!window.AstroApp) return; setTimeout(async () => {
            const out = {};
            try {
              out.info = await astroGetInfo();
              out.shell = document.documentElement.dataset.shell || '';
              out.tabs = document.querySelectorAll('.dt-tab').length;
              out.save = await astroSaveFile('astro-selftest.txt', btoa('hello from the desktop'));
              AstroApp.download('astro-selftest-hook.md', '# saved through the download hook');
              await new Promise((r) => setTimeout(r, 1500));
              out.models = (await (await fetch('/api/status')).json()).models.length;
            } catch (e) { out.error = String(e); }
            astroSelftestDone(JSON.stringify(out));
          }, 2500); });
        )JS");
    }

    Server server(cfg);
    std::atomic<bool> quitting{false};
    std::atomic<bool> supervising{false};
    std::atomic<bool> navigated{false};
    const std::string app_url = "http://127.0.0.1:" + std::to_string(cfg.port) + "/?desktop=1" + (g_library_id.empty() ? std::string() : "#library=" + g_library_id);

    auto show_error = [&](const std::string& why) {
        const std::string html = error_html(display, why, log_tail(cfg.log));
        w.dispatch([&w, html, &navigated] { navigated = false; w.set_html(html); });
    };
    // Waits for the port, then shows the app (only the first time, or after an error page).
    auto wait_and_open = [&](int timeout_s) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
        while (!quitting && std::chrono::steady_clock::now() < until) {
            if (port_open(cfg.port)) {
                if (!navigated.exchange(true)) w.dispatch([&w, app_url] { w.navigate(app_url); });
                return true;
            }
            std::this_thread::sleep_for(150ms);
        }
        return false;
    };
    std::function<void()> supervise = [&]() {
        if (supervising.exchange(true)) return;
        std::thread([&] {
            int crashes = 0;
            while (!quitting) {
                if (!server.start()) { show_error("The bundled server (" + pstr(cfg.server.filename()) + ") could not be launched."); break; }
                std::thread([&] { if (!wait_and_open(25) && !quitting && !navigated) show_error("The local server did not open its port in time."); }).detach();
                const auto started = std::chrono::steady_clock::now();
                const int code = server.wait();
                if (quitting) break;
                if (code == 75) continue;  // asked to restart (e.g. after saving keys)
                if (std::chrono::steady_clock::now() - started > 60s) crashes = 0;
                if (++crashes > 3) { show_error("The local server stopped (exit code " + std::to_string(code) + ") several times."); break; }
                std::this_thread::sleep_for(800ms);
            }
            supervising = false;
        }).detach();
    };
    w.bind("astroRetry", [&](const std::string&) -> std::string {
        w.set_html(splash_html(display, cfg.edition));
        if (reuse) std::thread([&] { if (!wait_and_open(10)) show_error("The running A.S.T.R.O AI window's server is gone. Close every window and start again."); }).detach();
        else supervise();
        return "true";
    });

    w.set_html(splash_html(display, cfg.edition));
    if (reuse) std::thread([&] { wait_and_open(10); }).detach();
    else supervise();

    w.run();
    quitting = true;
    server.stop();
    return 0;
}
