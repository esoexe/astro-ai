// astro — the A.S.T.R.O AI terminal app.
//   astro                         interactive agent in the current folder (streaming, approvals, slash commands)
//   astro -p "task"               print mode for scripts and CI (final report on stdout; --json for JSON lines)
//   astro parallel "A" "B" ...    several agents at once, each in an isolated copy of the project, then merge
//   astro agents | attach | logs | diff | merge | discard | stop | undo | models | server
// It talks to the local A.S.T.R.O AI server (the desktop app's engine, /api/agent/*) and starts it when needed.
// Build: g++ -std=c++20 -O2 cli/astro.cpp -o astro $(pkg-config --cflags --libs libcurl)
#include "json.h"

#include <curl/curl.h>

#include <algorithm>
#include <ctime>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;
using aj::Value;
using aj::Object;
using aj::Array;

static const char* VERSION = "1.7.0";

// ---------- terminal ----------
static bool g_tty = false, g_color = false, g_err_tty = false;
static std::atomic<int> g_interrupts{0};
static std::atomic<long long> g_last_interrupt{0};
static long long now_ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static std::string col(const char* code, const std::string& s) { return g_color ? std::string(code) + s + "\x1b[0m" : s; }
static std::string peach(const std::string& s) { return col("\x1b[38;2;243;166;121m", s); }
static std::string dim(const std::string& s) { return col("\x1b[2m", s); }
static std::string bold(const std::string& s) { return col("\x1b[1m", s); }
static std::string red(const std::string& s) { return col("\x1b[31m", s); }
static std::string green(const std::string& s) { return col("\x1b[32m", s); }
static std::string yellow(const std::string& s) { return col("\x1b[33m", s); }
static std::string cyan(const std::string& s) { return col("\x1b[36m", s); }

static int term_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) return info.srWindow.Right - info.srWindow.Left + 1;
#else
    winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) return w.ws_col;
#endif
    return 100;
}
// Visible length of a UTF-8 string (counts code points; good enough for status lines).
static size_t vis_len(const std::string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n; return n; }
static std::string clip(const std::string& s, size_t n) {
    if (vis_len(s) <= n) return s;
    std::string out; size_t k = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) { if (k == n - 1) break; ++k; }
        out += s[i];
    }
    return out + "\xE2\x80\xA6";
}
static std::string one_line(std::string s) { for (char& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' '; return s; }
static std::string trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
static std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
static std::string fmt_ms(double ms) {
    std::ostringstream os;
    if (ms < 1000) os << static_cast<long long>(ms) << "ms";
    else if (ms < 60000) { os.setf(std::ios::fixed); os.precision(1); os << ms / 1000 << "s"; }
    else os << static_cast<long long>(ms / 60000) << "m " << (static_cast<long long>(ms / 1000) % 60) << "s";
    return os.str();
}
static std::string ago(double t_ms) {
    const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const long long s = std::max(0LL, (now - static_cast<long long>(t_ms)) / 1000);
    if (s < 60) return "just now";
    if (s < 3600) return std::to_string(s / 60) + "m ago";
    if (s < 86400) return std::to_string(s / 3600) + "h ago";
    return std::to_string(s / 86400) + "d ago";
}

static bool g_status_shown = false;
static void clear_status() { if (g_status_shown && g_tty) { std::fputs("\r\x1b[2K", stdout); std::fflush(stdout); } g_status_shown = false; }
static void show_status(const std::string& text) {
    if (!g_tty) return;
    std::fputs(("\r\x1b[2K" + clip(text, static_cast<size_t>(std::max(20, term_width() - 1)))).c_str(), stdout);
    std::fflush(stdout);
    g_status_shown = true;
}
static void out(const std::string& line) { clear_status(); std::fputs((line + "\n").c_str(), stdout); std::fflush(stdout); }

#ifdef _WIN32
static BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) { ++g_interrupts; g_last_interrupt = now_ms(); return TRUE; }
    return FALSE;
}
static std::string to_utf8(const fs::path& p) {
    const std::wstring w = p.wstring();
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
static fs::path from_utf8(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return fs::path(w);
}
#else
static void on_sigint(int) { ++g_interrupts; g_last_interrupt = now_ms(); }
static std::string to_utf8(const fs::path& p) { return p.string(); }
static fs::path from_utf8(const std::string& s) { return fs::path(s); }
#endif

static void init_terminal() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE); DWORD mode = 0;
    g_tty = _isatty(_fileno(stdout)) != 0;
    g_err_tty = _isatty(_fileno(stderr)) != 0;
    if (g_tty && GetConsoleMode(h, &mode)) g_color = SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
    SetConsoleCtrlHandler(on_ctrl, TRUE);
#else
    g_tty = isatty(STDOUT_FILENO) != 0;
    g_err_tty = isatty(STDERR_FILENO) != 0;
    g_color = g_tty;
    struct sigaction sa{};
    sa.sa_handler = on_sigint; sigemptyset(&sa.sa_mask); sa.sa_flags = 0;   // no SA_RESTART: Ctrl+C interrupts a blocking read
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
#endif
    if (const char* nc = std::getenv("NO_COLOR"); nc && *nc) g_color = false;
    if (const char* fc = std::getenv("FORCE_COLOR"); fc && *fc && std::string(fc) != "0") g_color = true;
}

// Reads one line. nullopt on EOF (Ctrl+D) or when Ctrl+C interrupted the read.
static std::optional<std::string> read_line(const std::string& prompt, bool* interrupted = nullptr) {
    clear_status();
    std::fputs(prompt.c_str(), stdout); std::fflush(stdout);
    std::string line;
    const int before = g_interrupts.load();
    while (true) {
        const int c = std::fgetc(stdin);
        if (c == EOF) {
            if (g_interrupts.load() != before) { std::clearerr(stdin); if (interrupted) *interrupted = true; std::fputs("\n", stdout); return std::nullopt; }
            if (line.empty()) return std::nullopt;
            return line;
        }
        if (c == '\n') break;
        if (c != '\r') line += static_cast<char>(c);
    }
    return line;
}

// ---------- HTTP ----------
static std::string g_url, g_token;
struct Http { long status = 0; std::string body, err; };
static size_t on_write(char* p, size_t s, size_t n, void* ud) { static_cast<std::string*>(ud)->append(p, s * n); return s * n; }
// Long polls end early when the user presses Ctrl+C.
static int on_progress(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) { return g_interrupts.load() != *static_cast<int*>(ud) ? 1 : 0; }
static Http http(const std::string& method, const std::string& url, const std::string& body, long timeout_s, bool interruptible = false) {
    Http r;
    int base = g_interrupts.load();
    CURL* c = curl_easy_init();
    if (!c) { r.err = "curl init failed"; return r; }
    curl_slist* h = nullptr;
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "X-Astro-Client: 1");
    h = curl_slist_append(h, "Expect:");
    if (!g_token.empty()) h = curl_slist_append(h, ("X-Astro-Token: " + g_token).c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout_s);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_NOPROXY, "127.0.0.1,localhost");
    if (interruptible) { curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L); curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress); curl_easy_setopt(c, CURLOPT_XFERINFODATA, &base); }
    if (method == "POST") { curl_easy_setopt(c, CURLOPT_POST, 1L); curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str()); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size())); }
    const CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) r.err = curl_easy_strerror(rc);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return r;
}
struct ApiError : std::runtime_error { long status; Value body; ApiError(const std::string& m, long s, Value b) : std::runtime_error(m), status(s), body(std::move(b)) {} };
static Value api(const std::string& path, const Object& body = {}, long timeout_s = 120, bool interruptible = false) {
    const Http r = http("POST", g_url + path, aj::dump(Value(body)), timeout_s, interruptible);
    if (!r.err.empty()) throw ApiError("Cannot reach the A.S.T.R.O AI server at " + g_url + " (" + r.err + ").", 0, Value());
    Value j;
    aj::try_parse(r.body, j);
    if (r.status < 200 || r.status >= 300) {
        std::string msg = j["error"].str();
        if (r.status == 401) msg = "The server needs a sign-in token. Set ASTRO_TOKEN (Settings \xE2\x86\x92 Account in the web app), or use the desktop app's server.";
        if (msg.empty()) msg = "HTTP " + std::to_string(r.status);
        throw ApiError(msg, r.status, j);
    }
    return j;
}
static bool server_alive(const std::string& url) {
    const Http r = http("GET", url + "/api/status", "", 3);
    return r.err.empty() && (r.status == 200 || r.status == 401) && (r.body.find("configured") != std::string::npos || r.status == 401);
}

// ---------- finding / starting the server ----------
static fs::path exe_path() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2]; const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return fs::path(std::wstring(buf, n));
#elif defined(__APPLE__)
    char buf[4096]; uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) { std::error_code ec; return fs::weakly_canonical(fs::path(buf), ec); }
    return fs::path("astro");
#else
    std::error_code ec; return fs::read_symlink("/proc/self/exe", ec);
#endif
}
static std::string read_text(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
static std::string env(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }
struct ServerKit { fs::path res, bin; std::string name = "A.S.T.R.O AI Clean", data_name = "astro-ai-clean"; int port = 47612; };
static std::optional<ServerKit> find_server() {
    const fs::path d = exe_path().parent_path();
#ifdef _WIN32
    const char* names[] = {"astro-server.exe", "workspace_server.exe"};
#else
    const char* names[] = {"astro-server", "workspace_server"};
#endif
    std::vector<fs::path> dirs = {d, d / ".." / "Resources", d / "resources", d / ".." / "lib" / "astro-ai-clean", d / ".." / "lib" / "astro-ai", d / ".."};
    for (const auto& c : dirs) {
        std::error_code ec;
        if (!fs::exists(c / "web" / "index.html", ec)) continue;
        for (const char* n : names) if (fs::exists(c / n, ec)) {
            ServerKit k; k.res = fs::weakly_canonical(c, ec); k.bin = k.res / n;
            Value ed;
            if (aj::try_parse(read_text(k.res / "edition.json"), ed)) {
                if (!ed["name"].str().empty()) k.name = ed["name"].str();
                if (!ed["data_name"].str().empty()) k.data_name = ed["data_name"].str();
                else if (ed["edition"].str() == "personal") k.data_name = "astro-ai";
                k.port = static_cast<int>(ed["port"].num(ed["edition"].str() == "personal" ? 47611 : 47612));
            }
            return k;
        }
    }
    return std::nullopt;
}
static fs::path data_root(const ServerKit& k) {
    if (!env("ASTRO_DESKTOP_DATA").empty()) return from_utf8(env("ASTRO_DESKTOP_DATA"));
#ifdef _WIN32
    PWSTR p = nullptr; fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) base = fs::path(p);
    if (p) CoTaskMemFree(p);
    return base / from_utf8(k.name);
#elif defined(__APPLE__)
    return fs::path(env("HOME")) / "Library" / "Application Support" / k.name;
#else
    const std::string x = env("XDG_DATA_HOME");
    return (x.empty() ? fs::path(env("HOME").empty() ? "/tmp" : env("HOME")) / ".local" / "share" : fs::path(x)) / k.data_name;
#endif
}
static void set_env(const std::string& k, const std::string& v) {
#ifdef _WIN32
    _putenv_s(k.c_str(), v.c_str());
#else
    setenv(k.c_str(), v.c_str(), 1);
#endif
}
static bool spawn_server(const ServerKit& k, int port, std::string& log_path) {
    const fs::path data = data_root(k);
    std::error_code ec;
    fs::create_directories(data / "logs", ec);
    log_path = to_utf8(data / "logs" / "server-cli.log");
    // the personal edition's keys and gateway settings, exactly as the desktop launcher does
    if (fs::exists(k.res / "seed-keys.env", ec) && !fs::exists(data / ".astro_keys.env", ec)) {
        std::ofstream o(data / ".astro_keys.env", std::ios::binary); o << read_text(k.res / "seed-keys.env");
    }
    {
        std::stringstream ss(read_text(k.res / "gateway.env")); std::string line;
        while (std::getline(ss, line)) {
            const auto eq = line.find('=');
            if (eq == std::string::npos || line.rfind("LLM_", 0) != 0) continue;
            std::string v = trim(line.substr(eq + 1)); if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'')) v = v.substr(1, v.size() - 2);
            set_env(trim(line.substr(0, eq)), v);
        }
    }
    set_env("PORT", std::to_string(port)); set_env("ASTRO_BIND", "127.0.0.1"); set_env("ASTRO_DATA_DIR", to_utf8(data));
    set_env("ASTRO_AUTH", "off"); set_env("ASTRO_SYNC_MIRROR", "off"); set_env("ASTRO_DESKTOP", "1"); set_env("ASTRO_INSTANCE", k.data_name);
    set_env("ASTRO_CLI_SERVER", "1");
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE logf = CreateFileW((data / "logs" / "server-cli.log").wstring().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = logf; si.hStdError = logf; si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + k.bin.wstring() + L"\"";
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, k.res.wstring().c_str(), &si, &pi);
    if (logf != INVALID_HANDLE_VALUE) CloseHandle(logf);
    if (!ok) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
#else
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);   // double fork: the server is not our child, it keeps running after astro exits
        if (chdir(k.res.c_str()) != 0) _exit(126);
        const int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        const int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) dup2(nul, 0);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        execl(k.bin.c_str(), k.bin.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int st = 0; waitpid(pid, &st, 0);
    return true;
#endif
}
// After an update, an engine the terminal app started earlier can still be the old version: replace it.
// Only engines started by the terminal app agree to stop (the desktop app owns its own), so this is safe.
static bool stop_if_stale(const std::string& url, int kit_port) {
    if (env("ASTRO_KEEP_ENGINE") == "1" || url != "http://127.0.0.1:" + std::to_string(kit_port)) return false;
    const Http r = http("GET", url + "/api/status", "", 3);
    Value j;
    if (r.status != 200 || !aj::try_parse(r.body, j) || j["version"].str() == VERSION) return false;
    const std::string keep = g_url; g_url = url;
    bool stopped = false;
    try { api("/api/agent/server-stop", {}, 10); stopped = true; } catch (...) {}
    g_url = keep;
    if (!stopped) return false;
    for (int i = 0; i < 60 && server_alive(url); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
}
static bool connect_server(bool allow_start, bool quiet = false) {
    g_token = env("ASTRO_TOKEN");
    std::string u = env("ASTRO_URL");
    if (!u.empty()) { while (!u.empty() && u.back() == '/') u.pop_back(); g_url = u; return server_alive(g_url); }
    const auto kit = find_server();
    std::vector<int> ports;
    if (kit) ports.push_back(kit->port);
    for (int p : {47612, 47611, 4173}) if (std::find(ports.begin(), ports.end(), p) == ports.end()) ports.push_back(p);
    for (int p : ports) {
        const std::string url = "http://127.0.0.1:" + std::to_string(p);
        if (!server_alive(url)) continue;
        if (allow_start && kit && stop_if_stale(url, kit->port)) {
            if (!quiet) std::cerr << dim("The running engine was an older version; restarting it.") << "\n";
            break;
        }
        g_url = url; return true;
    }
    if (!allow_start) return false;
    if (!kit) {
        std::cerr << red("No A.S.T.R.O AI server is running, and none was found next to this program.") << "\n"
                  << "Start the A.S.T.R.O AI app, or point astro at a server: ASTRO_URL=http://127.0.0.1:4173 astro\n";
        return false;
    }
    if (!quiet) std::cerr << dim("Starting the A.S.T.R.O AI engine (" + to_utf8(kit->bin) + ")...") << "\n";
    std::string log;
    if (!spawn_server(*kit, kit->port, log)) { std::cerr << red("Could not start " + to_utf8(kit->bin)) << "\n"; return false; }
    g_url = "http://127.0.0.1:" + std::to_string(kit->port);
    for (int i = 0; i < 100; ++i) { if (server_alive(g_url)) return true; std::this_thread::sleep_for(std::chrono::milliseconds(200)); }
    std::cerr << red("The engine did not start. Log: " + log) << "\n";
    return false;
}
static bool is_local_url() { return g_url.find("://127.0.0.1") != std::string::npos || g_url.find("://localhost") != std::string::npos || g_url.find("://[::1]") != std::string::npos; }

// ---------- astro update (1.7) ----------
static int version_cmp(const std::string& a, const std::string& b) {
    auto parts = [](std::string v) { std::vector<long long> o; if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
        v = v.substr(0, v.find_first_of("-+")); std::stringstream ss(v); std::string x; while (std::getline(ss, x, '.')) o.push_back(std::atoll(x.c_str())); return o; };
    const auto x = parts(a), y = parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) { const long long p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0; if (p != q) return p < q ? -1 : 1; }
    return 0;
}
static int cmd_update(const std::vector<std::string>& pos) {
    const bool check_only = std::find(pos.begin(), pos.end(), "--check") != pos.end() || (!pos.empty() && pos[0] == "check");
    std::string feed = env("ASTRO_UPDATE_URL"); if (feed.empty()) feed = "https://esoexe.github.io/astro-ai/latest.json";
    const std::string saved = g_token; g_token.clear();            // never send the engine token to the website
    const Http r = http("GET", feed, "", 20); g_token = saved;
    Value j;
    if (!r.err.empty() || r.status != 200 || !aj::try_parse(r.body, j) || j["version"].str().empty()) {
        std::cerr << red("Could not read the update feed (" + (r.err.empty() ? "HTTP " + std::to_string(r.status) : r.err) + ").") << "\n"; return 1;
    }
    const std::string latest = j["version"].str();
    if (version_cmp(latest, VERSION) <= 0) { out(green("\xE2\x9C\x93 ") + "A.S.T.R.O AI " + VERSION + " is the latest version."); return 0; }
    out(peach("A.S.T.R.O AI " + latest) + " is available " + dim("(you have " + std::string(VERSION) + ")"));
    if (const auto* h = j["highlights"].arr()) for (const auto& x : *h) out(dim("  \xC2\xB7 ") + x.str());
    if (!j["notes"].str().empty()) out(dim("  What's new: " + j["notes"].str()));
    if (check_only) { out(dim("  Run astro update to install it.")); return 0; }
    const fs::path dir = exe_path().parent_path();
    const std::string ds = to_utf8(dir);
    std::error_code ec;
    const bool app_copy = fs::exists(dir / "Uninstall.exe", ec) || ds.find(".app/") != std::string::npos || ds.rfind("/usr/", 0) == 0 || ds.rfind("/opt/", 0) == 0;
    std::string low = ds; for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); std::replace(low.begin(), low.end(), '\\', '/');
    if (low.find("/scoop/apps/") != std::string::npos) { out("This astro was installed with Scoop. Run: " + bold("scoop update astro-ai")); return 0; }
    if (low.find("/cellar/") != std::string::npos || low.find("/caskroom/") != std::string::npos || low.find("linuxbrew") != std::string::npos) { out("This astro was installed with Homebrew. Run: " + bold("brew upgrade astro-ai")); return 0; }
    if (app_copy) {
        out("This astro came with the A.S.T.R.O AI app. Open the app and click " + bold("Update now") + ", or download it from https://esoexe.github.io/astro-ai/download.html");
        return 0;
    }
    out(dim("Updating " + ds + " ..."));
    set_env("ASTRO_HOME", ds); set_env("ASTRO_VERSION", latest);
#ifdef _WIN32
    set_env("ASTRO_UPDATER_PID", std::to_string(GetCurrentProcessId()));
    const int rc = _wsystem(L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"irm https://esoexe.github.io/astro-ai/install.ps1 | iex\"");
#else
    const int rc = std::system("curl -fsSL https://esoexe.github.io/astro-ai/install.sh | sh");
#endif
    if (rc != 0) { std::cerr << red("The update did not finish. Your current version still works; try again later.") << "\n"; return 1; }
    out(green("\xE2\x9C\x93 ") + "Updated to " + latest + ". The engine restarts with the new version the next time you run astro.");
    return 0;
}

// ---------- astro bug (1.7): a filled-in GitHub issue form; nothing is sent by itself ----------
static std::string url_enc(const std::string& s) {
    static const char* hex = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) { if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c); else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; } }
    return o;
}
static int cmd_bug() {
#ifdef _WIN32
    const std::string os = "Windows";
#elif defined(__APPLE__)
    const std::string os = "macOS";
#else
    const std::string os = "Linux";
#endif
    std::string term = env("WT_SESSION").empty() ? env("TERM_PROGRAM") : "Windows Terminal";
    if (term.empty()) term = env("TERM");
    const std::string body = "**What happened?**\n\n(Describe what you ran and what you expected.)\n\n**Details** (filled in by astro; check before sending)\n- Version: " + std::string(VERSION) +
        " (terminal)\n- System: " + os + "\n- Terminal: " + (term.empty() ? "?" : term) + "\n";
    const std::string url = "https://github.com/esoexe/astro-ai/issues/new?labels=bug&title=" + url_enc("Problem (terminal): ") + "&body=" + url_enc(body);
    out("Opening a filled-in issue form in your browser. Nothing is sent until you press Submit there.");
    out(dim(url));
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", from_utf8(url).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    (void)!std::system(("open '" + url + "' >/dev/null 2>&1 &").c_str());
#else
    (void)!std::system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str());
#endif
    return 0;
}

// ---------- options ----------
struct Opts {
    std::string project, dir, mode = "auto", model = "auto", prompt, cmd, resume;
    std::vector<std::string> args;
    bool print = false, json = false, review = false, isolate = false, merge = false, keep = false, force = false, partial = false, all = false, yes = false, classic = false, cont = false;
    int max_steps = 40;
};
static std::string resolve_project(const Opts& o) {
    if (!o.project.empty()) return o.project;
    std::error_code ec;
    fs::path dir = o.dir.empty() ? fs::current_path(ec) : fs::absolute(from_utf8(o.dir), ec);
    dir = fs::weakly_canonical(dir, ec);
    if (!is_local_url()) throw std::runtime_error("The server at " + g_url + " is not on this computer, so it cannot open this folder. Choose one of its projects with --project <name>.");
    if (!fs::is_directory(dir, ec)) throw std::runtime_error("Not a folder: " + to_utf8(dir));
    const Value r = api("/api/dev/fs/open", Object{{"path", to_utf8(dir)}});
    return r["name"].str();
}

// ---------- rendering events ----------
struct View {
    std::string id, status, waiting, waiting_kind, live;
    bool running = false;
    long long seq = 0;
};
struct Renderer {
    bool human = true;           // false: --json (one JSON object per line on stdout)
    bool to_stderr = false;      // print mode: progress on stderr, the report on stdout
    std::map<std::string, Value> pending;   // approval / question events by id
    std::string final_text;
    bool attention = false;      // the run finished but the engine flagged it (nothing changed, a check still fails...)
    std::string last_tool;
    std::string skip_user;       // the REPL already shows what the user typed
    int spin = 0;
    void line(const std::string& s) {
        if (to_stderr) { std::fputs((s + "\n").c_str(), stderr); std::fflush(stderr); }
        else out(s);
    }
    void indent_block(const std::string& text, const std::string& pre, size_t max_lines = 400) {
        std::stringstream ss(text); std::string l; size_t n = 0;
        while (std::getline(ss, l)) { if (++n > max_lines) { line(pre + dim("\xE2\x80\xA6")); break; } line(pre + l); }
    }
    void event(const Value& e) {
        const std::string t = e["type"].str();
        if (!human) { std::fputs((aj::dump(e) + "\n").c_str(), stdout); std::fflush(stdout); if (t == "done") attention = e["attention"].truthy() || !e["failing"].str().empty(); if (t == "done") final_text = e["text"].str(); return; }
        if (t == "user") { if (!skip_user.empty() && e["text"].str() == skip_user) { skip_user.clear(); return; } line(""); line(peach("\xE2\x80\xBA ") + bold(e["text"].str())); return; }
        if (t == "say") {
            if (e["final"].truthy()) return;   // the done event repeats it
            line(""); 
            std::stringstream ss(e["text"].str()); std::string l; bool first = true;
            while (std::getline(ss, l)) { line((first ? peach("\xE2\x97\x8F ") : std::string("  ")) + l); first = false; }
            return;
        }
        if (t == "tool") { last_tool = e["label"].str(); return; }
        if (t == "tool_done") {
            const bool ok = e["ok"].truthy(), blocked = e["blocked"].truthy();
            const std::string tool = e["tool"].str();
            std::string mark = ok ? green("\xE2\x9C\x93") : blocked ? yellow("\xE2\x8A\x98") : red("\xE2\x9C\x97");
            std::string extra;
            if (!e["added"].is_null() || !e["removed"].is_null()) extra = " " + green("+" + e["added"].str("0")) + " " + red("\xE2\x88\x92" + e["removed"].str("0"));
            else if (tool == "bash" && !e["code"].is_null()) extra = dim(" exit " + e["code"].str());
            const std::string ms = e["ms"].num() > 0 ? dim(" \xC2\xB7 " + fmt_ms(e["ms"].num())) : "";
            line("  " + mark + " " + e["label"].str() + extra + ms);
            if (!ok) {
                std::string why = e["summary"].str();
                if (why.rfind("BLOCKED: ", 0) == 0) why = why.substr(9);
                if (!why.empty() && !(tool == "bash" && why.rfind("exit code", 0) == 0)) line("    " + (blocked ? yellow(clip(one_line(why), 200)) : red(clip(one_line(why), 200))));
                if (tool == "bash" || tool == "start_server") {
                    std::vector<std::string> ls; std::stringstream ss(e["out"].str()); std::string l;
                    while (std::getline(ss, l)) ls.push_back(l);
                    for (size_t i = ls.size() > 8 ? ls.size() - 8 : 0; i < ls.size(); ++i) line(dim("    \xE2\x94\x82 " + clip(ls[i], 160)));
                }
            }
            return;
        }
        if (t == "approval" || t == "question") { pending[e["id"].str()] = e; return; }
        if (t == "answered") { line(dim("    \xE2\x86\xB3 " + e["answer"].str())); return; }
        if (t == "note") { line(dim("  \xC2\xB7 " + e["text"].str())); return; }
        if (t == "todo") {
            line(dim("  To-do"));
            if (auto a = e["items"].arr()) for (const auto& it : *a) {
                const std::string st = it["status"].str();
                line("   " + (st == "done" ? green("\xE2\x98\x91") : st == "doing" ? yellow("\xE2\x97\x90") : dim("\xE2\x98\x90")) + " " + (st == "done" ? dim(it["text"].str()) : it["text"].str()));
            }
            return;
        }
        if (t == "review") {
            const std::string v = e["verdict"].str();
            line(""); line("  " + peach("\xE2\x97\x86 Review") + " " + (v == "PASS" ? green(v) : v == "FIX" ? yellow(v) : dim(v)) + (e["model"].str().empty() ? "" : dim(" \xC2\xB7 " + e["model"].str())));
            indent_block(e["text"].str(), "    ", 20);
            return;
        }
        if (t == "done") {
            final_text = e["text"].str();
            attention = e["attention"].truthy() || !e["failing"].str().empty();
            size_t nfiles = e["files"].arr() ? e["files"].arr()->size() : 0;
            line("");
            line((e["attention"].truthy() ? yellow("! Needs attention") : green("\xE2\x9C\x94 Done")) + dim("  \xC2\xB7 " + std::to_string(nfiles) + (nfiles == 1 ? " file" : " files") + " changed \xC2\xB7 " + e["steps"].str("0") + (e["steps"].num() == 1 ? " step" : " steps") + " \xC2\xB7 " + fmt_ms(e["ms"].num()) + (e["model"].str().empty() ? "" : " \xC2\xB7 " + e["model"].str())));
            if (!to_stderr && !final_text.empty()) indent_block(final_text, "  ");
            if (auto a = e["files"].arr()) {
                size_t n = 0;
                for (const auto& f : *a) {
                    if (++n > 30) { line(dim("  \xE2\x80\xA6 and " + std::to_string(a->size() - 30) + " more")); break; }
                    const std::string st = f["status"].str();
                    line("  " + (st == "added" ? green("+") : st == "deleted" ? red("-") : yellow("~")) + " " + f["path"].str());
                }
            }
            return;
        }
        if (t == "error") { line(red("\xE2\x9C\x97 " + e["text"].str())); return; }
    }
    void status(const View& v) {
        if (!human || to_stderr || !g_tty) return;
        static const char* frames[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9", "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4", "\xE2\xA0\xA6", "\xE2\xA0\xA7", "\xE2\xA0\x87", "\xE2\xA0\x8F"};
        if (!v.running || !v.waiting.empty()) { clear_status(); return; }
        std::string what = v.status == "queued" ? "waiting for a free agent slot" : !v.live.empty() ? one_line(v.live.substr(v.live.size() > 200 ? v.live.size() - 200 : 0)) : !last_tool.empty() ? last_tool : "thinking";
        show_status(peach(frames[spin++ % 10]) + " " + dim(what + "  (Ctrl+C to stop)"));
    }
};

static View view_of(const Value& j) {
    View v; v.id = j["id"].str(); v.status = j["status"].str(); v.waiting = j["waiting"].str(); v.waiting_kind = j["waiting_kind"].str();
    v.live = j["live"].str(); v.running = j["running"].truthy(); v.seq = static_cast<long long>(j["seq"].num());
    return v;
}

// Asks the user an approval or a question in the terminal; returns the answer text.
static std::optional<std::string> ask_terminal(Renderer& R, const Value& e) {
    const std::string t = e["type"].str();
    R.line("");
    if (t == "approval") {
        R.line("  " + yellow("\xE2\x94\x8C " + e["text"].str()));
        std::stringstream ss(e["preview"].str()); std::string l; int n = 0;
        while (std::getline(ss, l)) { if (++n > 18) { R.line(yellow("  \xE2\x94\x82 ") + dim("\xE2\x80\xA6")); break; } R.line(yellow("  \xE2\x94\x82 ") + clip(l, 200)); }
        bool intr = false;
        auto ans = read_line(yellow("  \xE2\x94\x94 ") + "[" + bold("y") + "] allow  [" + bold("a") + "] allow all this run  [" + bold("n") + "] deny  or type what to do instead: ", &intr);
        if (!ans) return std::nullopt;
        const std::string a = lower(trim(*ans));
        if (a.empty() || a == "y" || a == "yes") return std::string("Allow");
        if (a == "a" || a == "all") return std::string("Allow all in this run");
        if (a == "n" || a == "no") return std::string("Deny");
        return trim(*ans);
    }
    R.line("  " + cyan("? ") + bold(e["question"].str()));
    std::vector<std::string> opts;
    if (auto a = e["options"].arr()) for (const auto& o : *a) { opts.push_back(o.str()); R.line("    " + cyan(std::to_string(opts.size()) + ".") + " " + o.str()); }
    auto ans = read_line("  " + cyan("\xE2\x80\xBA "));
    if (!ans) return std::nullopt;
    const std::string a = trim(*ans);
    if (!a.empty() && std::all_of(a.begin(), a.end(), ::isdigit)) { const size_t k = static_cast<size_t>(std::stoul(a)); if (k >= 1 && k <= opts.size()) return opts[k - 1]; }
    return a.empty() && !opts.empty() ? opts[0] : a;
}

// Follows a session until it stops working. Returns the final status.
static std::string follow(const std::string& id, long long& since, Renderer& R, bool interactive) {
    std::string live_seen;
    R.attention = false;
    std::set<std::string> asked;
    int seen_interrupts = g_interrupts.load();
    bool stop_sent = false;
    while (true) {
        if (g_interrupts.load() != seen_interrupts) {
            seen_interrupts = g_interrupts.load();
            if (stop_sent) { R.line(dim("  (stopping\xE2\x80\xA6 press Ctrl+C again to leave)")); }
            if (!stop_sent) { try { api("/api/agent/stop", Object{{"id", id}}, 10); } catch (...) {} stop_sent = true; }
            else return "stopped";
        }
        Value j;
        try { j = api("/api/agent/events", Object{{"id", id}, {"since", static_cast<double>(since)}, {"wait", (g_tty && R.human && !R.to_stderr) ? 1 : 15}, {"live", live_seen}}, 40, true); }
        catch (const ApiError& e) { if (e.status == 0) { if (g_interrupts.load() != seen_interrupts) continue; std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; } throw; }
        const View v = view_of(j);
        live_seen = v.live;
        bool any = false;
        if (auto a = j["events"].arr()) for (const auto& e : *a) { R.event(e); since = std::max(since, static_cast<long long>(e["seq"].num())); any = true; }
        if (!v.waiting.empty() && !asked.count(v.waiting)) {
            auto it = R.pending.find(v.waiting);
            if (it != R.pending.end()) {
                asked.insert(v.waiting);
                if (interactive && R.human) {
                    const auto ans = ask_terminal(R, it->second);
                    if (!ans) { try { api("/api/agent/stop", Object{{"id", id}}, 10); } catch (...) {} stop_sent = true; seen_interrupts = g_interrupts.load(); continue; }
                    try { api("/api/agent/answer", Object{{"id", id}, {"qid", v.waiting}, {"answer", *ans}}, 10); } catch (const std::exception& ex) { R.line(red(ex.what())); }
                } else if (R.human) {
                    R.line(yellow("  The agent is waiting for an answer: ") + it->second["text"].str() + it->second["question"].str() + dim("  (answer with: astro attach " + id + ")"));
                }
            }
        }
        R.status(v);
        if (!v.running && v.waiting.empty() && !any) { clear_status(); return v.status; }
    }
}

// ---------- commands ----------
static Object start_body(const Opts& o, const std::string& project, const std::string& prompt, bool interactive) {
    Object b{{"project", project}, {"prompt", prompt}, {"mode", o.mode}, {"model", o.model}, {"review", o.review}, {"interactive", interactive}, {"max_steps", o.max_steps}};
    if (o.isolate) b["isolate"] = true;
    return b;
}
static int exit_code_for(const std::string& st) { return st == "done" ? 0 : st == "error" ? 1 : st == "paused" ? 3 : st == "needs attention" ? 4 : 2; }

static void print_diff(const std::string& diff) {
    std::stringstream ss(diff); std::string l;
    while (std::getline(ss, l)) {
        if (l.rfind("diff --astro", 0) == 0) out(bold(l.substr(13)));
        else if (l.rfind("+++", 0) == 0 || l.rfind("---", 0) == 0) out(dim(l));
        else if (l.rfind("@@", 0) == 0) out(cyan(l));
        else if (!l.empty() && l[0] == '+') out(green(l));
        else if (!l.empty() && l[0] == '-') out(red(l));
        else out(l);
    }
}
static int cmd_diff(const std::string& id) {
    const Value r = api("/api/agent/diff", Object{{"id", id}});
    if (r["diff"].str().empty()) {
        if (auto a = r["changes"].arr(); a && !a->empty()) { for (const auto& c : *a) out("  " + c["status"].str() + "  " + c["path"].str()); }
        else out(dim("No changes."));
        return 0;
    }
    print_diff(r["diff"].str());
    return 0;
}
static int cmd_merge(const std::string& id, const Opts& o) {
    Object b{{"id", id}, {"discard", !o.keep}};
    if (o.force) b["force"] = true;
    if (o.partial) b["partial"] = true;
    try {
        const Value r = api("/api/agent/merge", b);
        const size_t n = r["merged"].arr() ? r["merged"].arr()->size() : 0;
        out(green("\xE2\x9C\x94 Merged ") + std::to_string(n) + " file(s)" + (r["discarded"].truthy() ? dim(" \xC2\xB7 isolated copy removed") : "") + (r["checkpoint"].str().empty() ? "" : dim(" \xC2\xB7 undo with: astro undo " + id)));
        if (auto a = r["merged"].arr()) for (const auto& f : *a) out("  " + green("\xE2\x86\x92") + " " + f["path"].str() + (f["combined"].truthy() ? dim("  (combined with the project's changes)") : ""));
        if (auto a = r["conflicts"].arr(); a && !a->empty()) { out(yellow("  Not merged (changed in the project meanwhile):")); for (const auto& f : *a) out("  " + yellow("!") + " " + f["path"].str()); }
        return 0;
    } catch (const ApiError& e) {
        out(red("\xE2\x9C\x97 " + std::string(e.what())));
        if (auto a = e.body["conflicts"].arr()) for (const auto& f : *a) out("  " + yellow("!") + " " + f["path"].str());
        if (e.body["conflicts"].arr()) out(dim("  astro merge " + id + " --partial   (merge the rest)\n  astro merge " + id + " --force     (overwrite the project's version)"));
        return 1;
    }
}
static int cmd_agents(const Opts& o) {
    Object b{{"all", o.all}};
    if (!o.project.empty()) b["project"] = o.project;
    const Value r = api("/api/agent/list", b);
    const Array* a = r["sessions"].arr();
    if (!a || a->empty()) { out(dim("No agent sessions yet.")); return 0; }
    out(dim("  running " + r["running"].str() + " / " + r["max"].str() + " at once"));
    for (const auto& s : *a) {
        const std::string st = s["status"].str();
        const std::string stc = st == "done" ? green(st) : st == "running" || st == "waiting" || st == "queued" ? yellow(st) : st == "error" ? red(st) : dim(st);
        std::string where = s["isolated"].truthy() ? s["base"].str() + dim(" (isolated" + std::string(s["merged"].truthy() ? ", merged" : s["discarded"].truthy() ? ", discarded" : "") + ")") : s["project"].str();
        out("  " + bold(s["id"].str()) + "  " + stc + std::string(st.size() < 11 ? 11 - st.size() : 1, ' ') + clip(s["title"].str(), 48) + dim("  \xC2\xB7 " + where + " \xC2\xB7 " + s["files"].str("0") + (s["files"].num() == 1 ? " file" : " files") + " \xC2\xB7 " + ago(s["updated"].num())));
    }
    return 0;
}
static int cmd_models() {
    const Value r = api("/api/agent/models");
    out(bold("Coding models, best first") + dim(" (the agent falls back down this list automatically)"));
    if (auto a = r["ranked"].arr()) for (const auto& m : *a) out("  " + peach("\xE2\x80\xA2") + " " + m["label"].str() + dim("  " + m["id"].str()));
    size_t n = r["all"].arr() ? r["all"].arr()->size() : 0;
    out(dim(std::to_string(n) + " models in total. Pick one with --model <id> or /model <id>."));
    return 0;
}

// Compilers, runtimes and language servers, installed portably by the engine (Extensions > Tools in the app).
// astro ext: VS Code extensions from Open VSX for the extension host (preview). They run in the app's editor.
static int cmd_ext(const std::vector<std::string>& pos) {
    const std::string sub = pos.empty() ? "list" : pos[0];
    if (sub == "list" || sub == "ls") {
        const Value r = api("/api/dev/exthost/list", Object{}, 30);
        out(bold("VS Code extensions") + dim("  (preview) run inside the A.S.T.R.O AI editor"));
        if (!r["ready"].truthy()) out(dim("  ") + red(r["why"].str()));
        bool any = false;
        if (auto o = r["extensions"].obj()) for (const auto& [id, e] : *o) {
            any = true; std::string pad = id; if (pad.size() < 34) pad += std::string(34 - pad.size(), ' ');
            out("  " + peach(pad) + " " + dim("v" + e["version"].str() + "  ") + (e["enabled"].truthy() || e["enabled"].is_null() ? green("enabled") : dim("disabled")) + dim("  " + e["displayName"].str()));
        }
        if (!any) out(dim("  none yet"));
        out(dim("\nInstall: astro ext install <publisher.name>   Remove: astro ext remove <id>   Toggle: astro ext enable|disable <id>"));
        return 0;
    }
    if (pos.size() < 2) { out(red("astro ext " + sub + " <publisher.name>")); return 2; }
    if (sub == "install" || sub == "add") {
        int rc = 0;
        for (size_t i = 1; i < pos.size(); ++i) {
            const auto dot = pos[i].find('.');
            if (dot == std::string::npos) { out(red("Use publisher.name, for example wmaurer.change-case")); rc = 2; continue; }
            out(dim("Downloading ") + pos[i] + dim(" from Open VSX\xE2\x80\xA6"));
            try {
                const Value r = api("/api/dev/exthost/install", Object{{"namespace", pos[i].substr(0, dot)}, {"name", pos[i].substr(dot + 1)}}, 600);
                out(green("\xE2\x9C\x94 ") + r["displayName"].str() + " v" + r["version"].str() + dim(r["verified"].str() == "sha256" ? "  Checksum verified (SHA-256)" : "  (no checksum published)"));
            } catch (const std::exception& e) { out(red("\xE2\x9C\x97 ") + e.what()); rc = 1; }
        }
        return rc;
    }
    if (sub == "remove" || sub == "rm" || sub == "uninstall") { api("/api/dev/exthost/remove", Object{{"id", pos[1]}}, 30); out(green("Removed ") + pos[1]); return 0; }
    if (sub == "enable" || sub == "disable") { api("/api/dev/exthost/toggle", Object{{"id", pos[1]}, {"on", sub == "enable"}}, 30); out(green(sub == "enable" ? "Enabled " : "Disabled ") + pos[1]); return 0; }
    out(red("astro ext list|install|remove|enable|disable")); return 2;
}
static int cmd_tools(const std::vector<std::string>& pos) {
    const std::string sub = pos.empty() ? "list" : pos[0];
    auto find = [](const Value& r, const std::string& id) -> const Value* { if (auto a = r["tools"].arr()) for (const auto& t : *a) if (t["id"].str() == id) return &t; return nullptr; };
    if (sub == "list" || sub == "ls") {
        const Value r = api("/api/dev/tools/list", Object{{"detect", true}}, 120);
        out(bold("Tools") + dim("  portable, checksum-verified installs in " + r["root"].str() + " (" + r["platform"].str() + ")"));
        std::string group;
        if (auto a = r["tools"].arr()) for (const auto& t : *a) {
            if (t["group"].str() != group) { group = t["group"].str(); out("\n" + bold(group)); }
            std::string st;
            if (!t["installed"].is_null()) st = green("installed ") + dim(t["installed"]["version"].str());
            else if (!t["found"].is_null()) st = dim("on this computer: " + t["found"]["version"].str().substr(0, 40));
            else if (t["installable"].truthy()) st = dim("not installed");
            else st = dim("see: astro tools info " + t["id"].str());
            std::string id = t["id"].str(); if (id.size() < 26) id += std::string(26 - id.size(), ' ');
            out("  " + peach(id) + " " + st);
        }
        out(dim("\nInstall with: astro tools install <id> [<id>...]   Remove: astro tools remove <id>"));
        return 0;
    }
    if (sub == "info") {
        if (pos.size() < 2) { out(red("astro tools info <id>")); return 2; }
        const Value r = api("/api/dev/tools/list", Object{{"detect", true}}, 120); const Value* t = find(r, pos[1]);
        if (!t) { out(red("Unknown tool: " + pos[1])); return 2; }
        out(bold((*t)["name"].str()) + dim("  " + (*t)["source"].str())); out("  " + (*t)["desc"].str());
        if (!(*t)["installed"].is_null()) out("  " + green("installed ") + (*t)["installed"]["version"].str() + dim("  (" + (*t)["installed"]["verified"].str() + ") " + (*t)["installed"]["dir"].str()));
        if (!(*t)["found"].is_null()) out(dim("  on this computer: " + (*t)["found"]["path"].str() + "  " + (*t)["found"]["version"].str()));
        if (!(*t)["instructions"].str().empty()) out("  " + (*t)["instructions"].str());
        return 0;
    }
    if (sub == "install" || sub == "add" || sub == "update") {
        if (pos.size() < 2) { out(red("astro tools install <id> [<id>...]   (see: astro tools list)")); return 2; }
        int failed = 0;
        for (size_t i = 1; i < pos.size(); ++i) {
            const std::string id = pos[i];
            const Value s = api("/api/dev/tools/install", Object{{"id", id}});
            if (!s["started"].truthy() && !s["running"].truthy()) { out(red("Could not start " + id)); ++failed; continue; }
            std::string last;
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                const Value r = api("/api/dev/tools/list", Object{}); const Value* t = find(r, id);
                if (!t) break;
                const Value& j = (*t)["job"];
                const std::string state = j["state"].str();
                const int pct = static_cast<int>(j["progress"].num() * 100);
                std::string line = "  " + id + "  " + std::to_string(pct) + "%  " + j["step"].str();
                if (j["total"].num() > 0) line += "  " + std::to_string(static_cast<long long>(j["bytes"].num() / 1048576)) + "/" + std::to_string(static_cast<long long>(j["total"].num() / 1048576)) + " MB";
                if (state == "running" || state == "cancel") { if (line != last) { std::cout << "\r\x1b[2K" << dim(line) << std::flush; last = line; } continue; }
                std::cout << "\r\x1b[2K";
                if (state == "done") out(green("\xE2\x9C\x94 ") + (*t)["name"].str() + " " + (*t)["installed"]["version"].str() + dim("  " + j["message"].str()));
                else { out(red("\xE2\x9C\x98 " + (*t)["name"].str() + ": " + j["message"].str())); ++failed; }
                break;
            }
        }
        return failed ? 1 : 0;
    }
    if (sub == "remove" || sub == "rm" || sub == "uninstall") {
        if (pos.size() < 2) { out(red("astro tools remove <id>")); return 2; }
        for (size_t i = 1; i < pos.size(); ++i) { api("/api/dev/tools/remove", Object{{"id", pos[i]}}); out(dim("Removed " + pos[i] + ".")); }
        return 0;
    }
    out(red("astro tools list | info <id> | install <id>... | remove <id>..."));
    return 2;
}

// Several agents at once, each in its own isolated copy. Live table, then merge.
static int cmd_parallel(const std::vector<std::string>& tasks, Opts o, const std::string& project) {
    if (tasks.empty()) { out(red("Give the tasks: astro parallel \"task one\" \"task two\"")); return 2; }
    if (o.mode == "ask") o.mode = "auto";   // nobody approves inside parallel runs
    struct Agent { std::string id, task, status = "starting", last, report, failing; long long since = 0; int steps = 0, files = 0; double ms = 0; };
    std::vector<Agent> ags;
    for (const auto& t : tasks) {
        Opts oi = o; oi.isolate = true;
        try {
            const Value r = api("/api/agent/start", start_body(oi, project, t, false), 600);
            Agent ag; ag.id = r["id"].str(); ag.task = t; ags.push_back(ag);
        } catch (const std::exception& e) { out(red("\xE2\x9C\x97 Could not start \"" + clip(t, 40) + "\": " + e.what())); }
    }
    if (ags.empty()) return 1;
    out(peach("\xE2\x97\x86 ") + bold(std::to_string(ags.size()) + " agents") + dim(" working in parallel, each in an isolated copy of " + project));
    int drawn = 0, spin = 0, seen = g_interrupts.load();
    bool stopping = false;
    while (true) {
        if (g_interrupts.load() != seen) {
            seen = g_interrupts.load();
            if (stopping) break;
            stopping = true;
            for (auto& a : ags) try { api("/api/agent/stop", Object{{"id", a.id}}, 10); } catch (...) {}
        }
        bool any_running = false;
        for (auto& a : ags) {
            Value j;
            try { j = api("/api/agent/events", Object{{"id", a.id}, {"since", static_cast<double>(a.since)}, {"wait", 0}}, 30); } catch (...) { continue; }
            if (auto ev = j["events"].arr()) for (const auto& e : *ev) {
                a.since = std::max(a.since, static_cast<long long>(e["seq"].num()));
                const std::string t = e["type"].str();
                if (t == "tool_done") a.last = (e["ok"].truthy() ? "\xE2\x9C\x93 " : "\xE2\x9C\x97 ") + e["label"].str();
                else if (t == "say") a.last = one_line(e["text"].str());
                else if (t == "done") { a.failing = e["failing"].str(); if (a.failing.empty() && e["attention"].truthy()) a.failing = "nothing usable was changed"; a.report = e["text"].str(); a.files = e["files"].arr() ? static_cast<int>(e["files"].arr()->size()) : 0; a.ms = e["ms"].num(); }
                else if (t == "error") a.last = "error: " + e["text"].str();
                if (!g_tty) out(dim("[" + a.id + "] ") + t + " " + clip(one_line(e["label"].str() + e["text"].str() + " " + e["summary"].str()), 120));
            }
            a.status = j["status"].str(); a.steps = static_cast<int>(j["steps"].num());
            if (j["running"].truthy()) any_running = true;
        }
        if (g_tty) {
            if (drawn) std::fputs(("\x1b[" + std::to_string(drawn) + "A").c_str(), stdout);
            static const char* frames[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9", "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4", "\xE2\xA0\xA6", "\xE2\xA0\xA7", "\xE2\xA0\x87", "\xE2\xA0\x8F"};
            const int w = std::max(60, term_width() - 1);
            drawn = 0;
            for (auto& a : ags) {
                const bool run = a.status == "running" || a.status == "waiting" || a.status == "queued" || a.status == "starting";
                const std::string icon = run ? peach(frames[(spin + drawn) % 10]) : a.status == "done" ? green("\xE2\x9C\x94") : a.status == "error" ? red("\xE2\x9C\x97") : yellow("\xE2\x96\xA0");
                std::string head = a.id + "  " + clip(a.task, 34);
                std::string tail = a.status + " \xC2\xB7 " + std::to_string(a.steps) + (a.steps == 1 ? " step" : " steps") + (a.status == "done" ? " \xC2\xB7 " + std::to_string(a.files) + (a.files == 1 ? " file" : " files") : "") + (a.last.empty() ? "" : " \xC2\xB7 " + a.last);
                std::fputs(("\x1b[2K  " + icon + " " + bold(head) + "  " + dim(clip(tail, static_cast<size_t>(std::max(10, w - 8 - static_cast<int>(vis_len(head)))))) + "\n").c_str(), stdout);
                ++drawn;
            }
            std::fflush(stdout);
            ++spin;
        }
        if (!any_running) {
            bool all_final = true;
            for (auto& a : ags) if (a.status == "running" || a.status == "waiting" || a.status == "queued" || a.status == "starting") all_final = false;
            if (all_final) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(g_tty ? 400 : 1500));
    }
    out("");
    int failures = 0;
    for (auto& a : ags) {
        out((a.status == "done" ? green("\xE2\x9C\x94 ") : red("\xE2\x9C\x97 ")) + bold(a.id) + "  " + a.task + dim("  \xC2\xB7 " + a.status + " \xC2\xB7 " + std::to_string(a.files) + (a.files == 1 ? " file" : " files") + (a.ms > 0 ? " \xC2\xB7 " + fmt_ms(a.ms) : "")));
        if (!a.report.empty()) { std::stringstream ss(a.report); std::string l; int n = 0; while (std::getline(ss, l) && n++ < 4) out("    " + dim(clip(l, 160))); }
        if (!a.failing.empty()) out("    " + yellow("! the last check still fails: " + a.failing));
        if (a.status != "done") ++failures;
    }
    if (o.merge) {
        out("");
        for (auto& a : ags) {
            if (a.status != "done") { out(yellow("  skipped ") + a.id + dim(" (" + a.status + ")")); continue; }
            if (!a.failing.empty()) { out(yellow("  skipped ") + a.id + dim(" (its last check failed: " + a.failing + ")  review: astro diff " + a.id)); ++failures; continue; }
            if (a.files == 0) { try { api("/api/agent/discard", Object{{"id", a.id}}); } catch (...) {} out(dim("  " + a.id + ": no changes")); continue; }
            out(bold("Merging " + a.id));
            Opts m = o; m.keep = false; m.partial = false;
            if (cmd_merge(a.id, m) != 0) ++failures;
        }
    } else {
        out(""); out(dim("Review and merge:"));
        for (auto& a : ags) if (a.status == "done" && a.files > 0) out(dim("  astro diff " + a.id + "    astro merge " + a.id + "    astro discard " + a.id));
    }
    return failures ? 1 : 0;
}

static void help() {
    std::cout << peach("\xE2\x97\x86 A.S.T.R.O AI") << " terminal " << VERSION << " \xE2\x80\x94 " << dim("one brain, many minds") << "\n\n"
              << bold("Usage") << "\n"
              << "  astro [options] [task]           full-screen terminal app in this folder (Ctrl+B side panel, / commands)\n"
              << "  astro -c | --resume <id>          continue the latest session here, or a given one\n"
              << "  astro -p \"task\" [--json]         print mode: run once, report on stdout (JSON lines with --json)\n"
              << "  astro parallel \"A\" \"B\" [--merge] several agents at once, each in an isolated copy\n"
              << "  astro agents [--all]              list agent sessions (background and parallel)\n"
              << "  astro attach <id>                 follow an agent live (answer its questions)\n"
              << "  astro logs <id>                   print an agent's full log\n"
              << "  astro diff <id>                   changes made by an agent\n"
              << "  astro merge <id> [--partial|--force] [--keep]   bring an isolated agent's changes into the project\n"
              << "  astro discard <id> | stop <id> | undo <id>\n"
              << "  astro models                      coding models, best first\n"
              << "  astro tools list|install|remove   compilers, runtimes and language servers (portable)\n"
              << "  astro ext list|install|remove     VS Code extensions from Open VSX (preview, run in the editor)\n"
              << "  astro server status|start|stop    the local A.S.T.R.O AI engine\n"
              << "  astro update [--check]            install the newest version (terminal installs)\n"
              << "  astro bug                         report a problem (opens a filled-in GitHub issue)\n\n"
              << bold("Options") << "\n"
              << "  -C <dir>            work in this folder (default: the current folder)\n"
              << "  --project <name>    use an existing A.S.T.R.O AI project instead of a folder\n"
              << "  --mode plan|ask|auto  plan = read-only; ask = approve each change; auto = act alone (default; checkpoint for undo)\n"
              << "  --model <id>        preferred model (see astro models); default: the best free coding model, with fallback\n"
              << "  --review            a second model reviews the work (one fix round)\n"
              << "  --isolate           run in an isolated copy (git worktree or copy); merge later\n"
              << "  --max-steps <n>     steps per run (default 40)\n"
              << "  --json              JSON lines output (print mode)\n"
              << "  --classic           the simple line-by-line prompt instead of the full-screen app (or ASTRO_TUI=0)\n\n"
              << bold("Environment") << "  ASTRO_URL (server URL), ASTRO_TOKEN (sign-in token for a server with accounts), NO_COLOR,\n             ASTRO_THEME=light|dark, ASTRO_COLOR=256\n";
}
static void repl_help() {
    out(bold("Commands"));
    for (const auto& [c, d] : std::vector<std::pair<std::string, std::string>>{
             {"/mode plan|ask|auto", "permission mode for the next task"}, {"/model <id>|auto", "preferred model"}, {"/models", "list coding models"},
             {"/review on|off", "second-model review after each task"}, {"/new", "start a new session"}, {"/sessions", "sessions in this project"},
             {"/resume <id>", "continue an earlier session"}, {"/diff", "what this session changed (git diff for repos)"}, {"/undo", "restore the project to before this session"},
             {"/parallel A | B | C", "run tasks in parallel, isolated; add --merge to merge"}, {"/agents", "all agents"}, {"/merge <id>", "merge an isolated agent"},
             {"/discard <id>", "remove an isolated agent's copy"}, {"/quit", "leave (Ctrl+D)"}})
        out("  " + peach(c) + std::string(c.size() < 24 ? 24 - c.size() : 1, ' ') + dim(d));
    out(dim("End a line with \\ to write several lines. Ctrl+C stops the agent."));
}
// Tasks for /parallel: "quoted" "tasks", or task one | task two.
static std::vector<std::string> split_tasks(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    if (s.find('"') != std::string::npos) {
        bool q = false;
        for (char c : s) {
            if (c == '"') { if (q && !trim(cur).empty()) out.push_back(trim(cur)); cur.clear(); q = !q; continue; }
            if (q) cur += c; else if (c == '|') continue; else if (!std::isspace(static_cast<unsigned char>(c))) cur += c;
        }
        if (!trim(cur).empty()) out.push_back(trim(cur));
        return out;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '|' ) { if (!trim(cur).empty()) out.push_back(trim(cur)); cur.clear(); continue; }
        cur += s[i];
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

static int repl(Opts o, const std::string& project) {
    std::string sid;
    long long since = 0;
    Renderer R;
    out(peach("\xE2\x97\x86 A.S.T.R.O AI") + " " + dim("terminal " + std::string(VERSION)));
    out(dim("  project ") + project + dim("  \xC2\xB7  mode ") + o.mode + dim("  \xC2\xB7  model ") + o.model + dim("  \xC2\xB7  ") + dim(g_url));
    out(dim("  Type a task, or /help. Ctrl+C stops the agent, Ctrl+D quits."));
    std::string pending = o.prompt;
    while (true) {
        std::string text;
        if (!pending.empty()) { text = pending; pending.clear(); out(""); out(peach("\xE2\x80\xBA ") + bold(text)); }
        else {
            out("");
            bool intr = false;
            auto l = read_line(peach("\xE2\x80\xBA "), &intr);
            if (!l) {
                if (intr) {
                    if (now_ms() - g_last_interrupt.load() < 50 && g_interrupts.load() >= 2) { /* fallthrough */ }
                    static long long last = 0;
                    if (now_ms() - last < 2000) return 0;
                    last = now_ms();
                    out(dim("  (press Ctrl+C again to quit)"));
                    continue;
                }
                out(""); return 0;
            }
            text = *l;
            while (!text.empty() && text.back() == '\\') { text.pop_back(); auto more = read_line(dim("\xE2\x80\xA6 ")); if (!more) break; text += "\n" + *more; }
            text = trim(text);
            if (text.empty()) continue;
        }
        if (text[0] == '/') {
            std::string cmd = text.substr(1), arg;
            if (const auto sp = cmd.find(' '); sp != std::string::npos) { arg = trim(cmd.substr(sp + 1)); cmd = cmd.substr(0, sp); }
            cmd = lower(cmd);
            try {
                if (cmd == "quit" || cmd == "exit" || cmd == "q") return 0;
                else if (cmd == "help" || cmd == "?") repl_help();
                else if (cmd == "mode") {
                    if (arg == "plan" || arg == "ask" || arg == "auto") { o.mode = arg; if (!sid.empty()) api("/api/agent/mode", Object{{"id", sid}, {"mode", arg}}); out(dim("  mode: ") + arg); }
                    else out(dim("  mode: " + o.mode + "   (use /mode plan | ask | auto)"));
                }
                else if (cmd == "model") {
                    if (!arg.empty()) { o.model = arg; if (!sid.empty()) api("/api/agent/mode", Object{{"id", sid}, {"model", arg}}); out(dim("  model: ") + arg); }
                    else out(dim("  model: " + o.model + "   (see /models)"));
                }
                else if (cmd == "models") cmd_models();
                else if (cmd == "review") { o.review = arg != "off"; out(dim(std::string("  review: ") + (o.review ? "on" : "off"))); }
                else if (cmd == "new" || cmd == "clear") { sid.clear(); since = 0; out(dim("  New session.")); }
                else if (cmd == "sessions") { Opts q = o; q.project = project; cmd_agents(q); }
                else if (cmd == "agents") { Opts q = o; q.project.clear(); cmd_agents(q); }
                else if (cmd == "resume") {
                    if (arg.empty()) { out(dim("  /resume <id>  (see /sessions)")); continue; }
                    const Value r = api("/api/agent/get", Object{{"id", arg}});
                    sid = arg; since = 0;
                    if (auto a = r["events"].arr()) for (const auto& e : *a) { R.event(e); since = std::max(since, static_cast<long long>(e["seq"].num())); }
                    out(dim("  Resumed " + arg + " (" + r["status"].str() + "). Your next message continues it."));
                    if (r["running"].truthy()) { const std::string st = follow(sid, since, R, true); out(dim("  " + st)); }
                }
                else if (cmd == "diff") { if (sid.empty()) out(dim("  Nothing yet.")); else cmd_diff(sid); }
                else if (cmd == "undo") {
                    if (sid.empty()) { out(dim("  Nothing to undo.")); continue; }
                    auto c = read_line(yellow("  Restore the project to before this session? [y/N] "));
                    if (c && lower(trim(*c)) == "y") { api("/api/agent/undo", Object{{"id", sid}}); out(green("  \xE2\x9C\x94 Restored.")); }
                }
                else if (cmd == "parallel") {
                    bool merge = false;
                    if (const auto p = arg.find("--merge"); p != std::string::npos) { merge = true; arg.erase(p, 7); }
                    Opts q = o; q.merge = merge;
                    cmd_parallel(split_tasks(arg), q, project);
                }
                else if (cmd == "merge") { if (arg.empty()) out(dim("  /merge <id>")); else { Opts q = o; q.keep = false; cmd_merge(arg, q); } }
                else if (cmd == "discard") { if (arg.empty()) out(dim("  /discard <id>")); else { api("/api/agent/discard", Object{{"id", arg}}); out(dim("  Discarded.")); } }
                else if (cmd == "stop") { if (!sid.empty()) api("/api/agent/stop", Object{{"id", sid}}); }
                else out(dim("  Unknown command. /help lists them."));
            } catch (const std::exception& e) { out(red("  " + std::string(e.what()))); }
            continue;
        }
        try {
            if (sid.empty()) {
                const Value r = api("/api/agent/start", start_body(o, project, text, true), 600);
                sid = r["id"].str(); since = 0;
                if (r["isolated"].truthy()) out(dim("  isolated copy: " + r["project"].str()));
            } else {
                api("/api/agent/send", Object{{"id", sid}, {"prompt", text}, {"mode", o.mode}, {"model", o.model}});
            }
            R.skip_user = text;   // our own prompt is already on screen: skip the echo
            const std::string st = follow(sid, since, R, true);
            if (st == "stopped") out(dim("  Stopped. Send a message to continue, or /undo."));
            else if (st == "paused") out(dim("  Step limit reached. Send \"continue\" to keep going."));
        } catch (const std::exception& e) { out(red("  " + std::string(e.what()))); }
    }
}

#include "tui.inc"

int main(int argc, char** argv) {
    init_terminal();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Opts o;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> std::string { if (i + 1 >= argc) { std::cerr << "Missing value for " << what << "\n"; std::exit(2); } return argv[++i]; };
        if (a == "-h" || a == "--help") { help(); return 0; }
        else if (a == "-v" || a == "--version" || a == "version") { std::cout << "astro " << VERSION << "\n"; return 0; }
        else if (a == "-p" || a == "--print") o.print = true;
        else if (a == "--json") o.json = true;
        else if (a == "-C" || a == "--dir") o.dir = next("-C");
        else if (a == "--project") o.project = next("--project");
        else if (a == "--mode") o.mode = next("--mode");
        else if (a == "--model" || a == "-m") o.model = next("--model");
        else if (a == "--review") o.review = true;
        else if (a == "--isolate") o.isolate = true;
        else if (a == "--merge") o.merge = true;
        else if (a == "--keep") o.keep = true;
        else if (a == "--force") o.force = true;
        else if (a == "--partial") o.partial = true;
        else if (a == "--all") o.all = true;
        else if (a == "-y" || a == "--yes") o.yes = true;
        else if (a == "--max-steps") o.max_steps = std::max(1, std::atoi(next("--max-steps").c_str()));
        else if (a == "--classic") o.classic = true;
        else if (a == "-c" || a == "--continue") o.cont = true;
        else if (a == "-r" || a == "--resume") o.resume = next("--resume");
        else if (a == "--plan") o.mode = "plan";
        else if (a == "--ask") o.mode = "ask";
        else if (a == "--auto") o.mode = "auto";
        else if (a == "--check") pos.push_back(a);
        else if (a.size() > 1 && a[0] == '-' && a != "-") { std::cerr << "Unknown option " << a << " (astro --help)\n"; return 2; }
        else pos.push_back(a);
    }
    if (o.mode != "plan" && o.mode != "ask" && o.mode != "auto") { std::cerr << "--mode must be plan, ask or auto\n"; return 2; }
    static const std::set<std::string> cmds = {"run", "parallel", "agents", "attach", "logs", "diff", "merge", "discard", "stop", "undo", "models", "server", "chat", "help", "tools", "ext", "update", "bug"};
    if (!pos.empty() && cmds.count(pos[0])) { o.cmd = pos[0]; pos.erase(pos.begin()); }
    if (o.cmd == "help") { help(); return 0; }
    if (o.cmd == "run") o.print = true;
    if (o.cmd.empty() || o.cmd == "run" || o.cmd == "chat") { for (const auto& p : pos) o.prompt += (o.prompt.empty() ? "" : " ") + p; }
    if (o.print && o.prompt.empty()) {
#ifndef _WIN32
        if (!isatty(STDIN_FILENO))
#else
        if (!_isatty(_fileno(stdin)))
#endif
        { std::stringstream ss; ss << std::cin.rdbuf(); o.prompt = trim(ss.str()); }
    }

    try {
        if (o.cmd == "update") return cmd_update(pos);
        if (o.cmd == "bug") return cmd_bug();
        if (o.cmd == "server") {
            const std::string sub = pos.empty() ? "status" : pos[0];
            if (sub == "status") {
                if (connect_server(false)) { const Http r = http("GET", g_url + "/api/status", "", 5); Value j; aj::try_parse(r.body, j);
                    size_t n = j["models"].arr() ? j["models"].arr()->size() : 0;
                    out(green("\xE2\x97\x8F ") + "A.S.T.R.O AI engine at " + g_url + dim("  \xC2\xB7 " + std::to_string(n) + " models")); return 0; }
                out(dim("\xE2\x97\x8B No A.S.T.R.O AI engine is running.")); return 1;
            }
            if (sub == "start") { if (connect_server(true)) { out(green("\xE2\x97\x8F ") + "A.S.T.R.O AI engine at " + g_url); return 0; } return 1; }
            if (sub == "stop") {
                if (!connect_server(false)) { out(dim("Not running.")); return 0; }
                try { api("/api/agent/server-stop", {}, 10); out(dim("Stopped the engine at " + g_url + ".")); return 0; }
                catch (const std::exception& e) { out(red(e.what())); return 1; }
            }
            std::cerr << "astro server status|start|stop\n"; return 2;
        }
        if (!connect_server(true)) return 1;
        auto need_id = [&]() -> std::string { if (pos.empty()) { std::cerr << "Give the agent id (astro agents lists them).\n"; std::exit(2); } return pos[0]; };
        if (o.cmd == "agents") return cmd_agents(o);
        if (o.cmd == "models") return cmd_models();
        if (o.cmd == "tools") return cmd_tools(pos);
        if (o.cmd == "ext") return cmd_ext(pos);
        if (o.cmd == "diff") return cmd_diff(need_id());
        if (o.cmd == "merge") return cmd_merge(need_id(), o);
        if (o.cmd == "discard") { api("/api/agent/discard", Object{{"id", need_id()}}); out(dim("Discarded.")); return 0; }
        if (o.cmd == "stop") { api("/api/agent/stop", Object{{"id", need_id()}}); out(dim("Stopping.")); return 0; }
        if (o.cmd == "undo") { const Value r = api("/api/agent/undo", Object{{"id", need_id()}}); out(green("\xE2\x9C\x94 Restored ") + r["project"].str() + "."); return 0; }
        if (o.cmd == "logs" || o.cmd == "attach") {
            const std::string id = need_id();
            Renderer R; R.human = !o.json;
            const Value r = api("/api/agent/get", Object{{"id", id}});
            long long since = 0;
            if (auto a = r["events"].arr()) for (const auto& e : *a) { R.event(e); since = std::max(since, static_cast<long long>(e["seq"].num())); }
            if (o.cmd == "attach" && (r["running"].truthy() || !r["waiting"].str().empty())) return exit_code_for(follow(id, since, R, true));
            return 0;
        }
        const std::string project = resolve_project(o);
        if (o.cmd == "parallel") return cmd_parallel(pos, o, project);
        if (o.print) {
            if (o.prompt.empty()) { std::cerr << "Give the task: astro -p \"what to do\"\n"; return 2; }
            Renderer R; R.human = !o.json; R.to_stderr = !o.json;
            const Value r = api("/api/agent/start", start_body(o, project, o.prompt, false), 600);
            long long since = 0;
            const std::string st0 = follow(r["id"].str(), since, R, false);
            const std::string st = st0 == "done" && R.attention ? "needs attention" : st0;
            if (!o.json) {
                if (!R.final_text.empty()) std::cout << R.final_text << "\n";
                std::cerr << dim("session " + r["id"].str() + " \xC2\xB7 " + st) << "\n";
            }
            return exit_code_for(st);
        }
        if (tui::wanted(o)) return tui::run(o, project);
        return repl(o, project);
    } catch (const std::exception& e) {
        clear_status();
        std::cerr << red("\xE2\x9C\x97 " + std::string(e.what())) << "\n";
        return 1;
    }
}
