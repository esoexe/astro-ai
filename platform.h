// platform.h - every operating-system difference of the A.S.T.R.O AI C++ backend lives here.
//
//   Linux   : POSIX sockets, fork/exec, forkpty, OpenSSL libcrypto      (the original server)
//   macOS   : POSIX sockets, fork/exec, forkpty (<util.h>), CommonCrypto (no OpenSSL needed)
//   Windows : Winsock 2, CreateProcess + Job Objects, ConPTY terminals, LibreSSL libcrypto
//
// The rest of the code calls the small helpers below (sock_close, peek_nonblock, astro_socketpair,
// pipe_cloexec, astro_rand_bytes, astro_sha256, astro_pbkdf2_sha256, astro_memeq) and keeps its POSIX
// code paths for Linux/macOS; Windows-only branches are marked with #ifdef _WIN32 where they are used.
#pragma once

// The app version reported by the engine (/api/status, update checks). Bumped with every release.
#ifndef ASTRO_APP_VERSION
#define ASTRO_APP_VERSION "1.8.1"
#endif

#if defined(_WIN32)
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <iphlpapi.h>
#  include <io.h>
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <ctime>
#  include <cerrno>
#  include <string>
#  include <vector>
#  include <map>
#  include <mutex>
#  include <algorithm>
#  include <filesystem>
#  include <thread>
#  include <cwctype>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <sys/stat.h>
#  include <sys/ioctl.h>
#  include <sys/resource.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <poll.h>
extern char** environ;   // the process environment (used to start MCP servers with the user's environment)
#  include <cerrno>
#  if defined(__APPLE__)
#    include <util.h>
#  else
#    include <pty.h>
#  endif
#endif

#if defined(__APPLE__)
#  include <CommonCrypto/CommonDigest.h>
#  include <CommonCrypto/CommonKeyDerivation.h>
#  include <cstdlib>
#else
#  include <openssl/crypto.h>
#  include <openssl/evp.h>
#  include <openssl/rand.h>
#  include <openssl/sha.h>
#endif

#include <cstring>
#include <string>

#if defined(__ANDROID__)
// Android has no /bin/bash or /bin/sh: the system shell is /system/bin/sh (mksh). Every place that starts a
// shell keeps its normal path; these wrappers swap it at the last moment, so no call site needs an #ifdef.
inline const char* astro_android_shell(const char* path) {
    if (path && (std::strcmp(path, "/bin/bash") == 0 || std::strcmp(path, "/bin/sh") == 0 || std::strcmp(path, "bash") == 0))
        return "/system/bin/sh";
    return path;
}
inline int astro_android_execve(const char* p, char* const* a, char* const* e) { return (execve)(astro_android_shell(p), a, e); }
inline int astro_android_execvp(const char* p, char* const* a) { return (execvp)(astro_android_shell(p), a); }
inline int astro_android_execvpe(const char* p, char* const* a, char* const* e) { return (execvpe)(astro_android_shell(p), a, e); }
#  define execve astro_android_execve
#  define execvp astro_android_execvp
#  define execvpe astro_android_execvpe
#endif

#ifndef O_BINARY
#  define O_BINARY 0
#endif
#ifndef O_CLOEXEC
#  define O_CLOEXEC 0
#endif
#ifndef SOCK_CLOEXEC
#  define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#endif

// ------------------------------------------------------------------------------------------------
// Secrets of the A.S.T.R.O AI server itself (model keys, service tokens). Programs it starts - MCP servers,
// project code, terminals - never inherit them; a server that needs a key gets it from its own "env" settings.
// ------------------------------------------------------------------------------------------------
inline bool astro_secret_env(const std::string& name_in) {
    std::string n;
    for (char ch : name_in) n.push_back(static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch));
    auto starts = [&](const char* s) { return n.rfind(s, 0) == 0; };
    auto ends = [&](const char* s) { const std::size_t l = std::strlen(s); return n.size() >= l && n.compare(n.size() - l, l, s) == 0; };
    return starts("LLM_") || starts("FALLBACK_") || starts("ASTRO_KEY") || n == "OPENCODE_SERVER_PASSWORD" || n == "SUPABASE_SERVICE_ROLE_KEY" ||
           n == "ASTRO_KMS_KEY" || n == "ASTRO_APP_TOKEN" || n == "GH_TOKEN" || n == "GITHUB_TOKEN" || ends("_API_KEY") || ends("_API_TOKEN") || ends("_SECRET_KEY") ||
           ends("_ACCESS_TOKEN");
}

// ------------------------------------------------------------------------------------------------
// crypto (random bytes, SHA-256, PBKDF2-HMAC-SHA256, constant-time compare)
// ------------------------------------------------------------------------------------------------
inline bool astro_rand_bytes(unsigned char* out, std::size_t n) {
#if defined(__APPLE__)
    arc4random_buf(out, n);
    return true;
#else
    return RAND_bytes(out, static_cast<int>(n)) == 1;
#endif
}
inline void astro_sha256(const void* data, std::size_t len, unsigned char out[32]) {
#if defined(__APPLE__)
    CC_SHA256(data, static_cast<CC_LONG>(len), out);
#else
    SHA256(static_cast<const unsigned char*>(data), len, out);
#endif
}
inline void astro_pbkdf2_sha256(const std::string& password, const std::string& salt, int iterations, unsigned char out[32]) {
#if defined(__APPLE__)
    CCKeyDerivationPBKDF(kCCPBKDF2, password.data(), password.size(), reinterpret_cast<const uint8_t*>(salt.data()), salt.size(),
                         kCCPRFHmacAlgSHA256, static_cast<unsigned>(iterations), out, 32);
#else
    PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), reinterpret_cast<const unsigned char*>(salt.data()),
                      static_cast<int>(salt.size()), iterations, EVP_sha256(), 32, out);
#endif
}
inline bool astro_memeq(const void* a, const void* b, std::size_t n) {
#if defined(__APPLE__)
    const unsigned char* x = static_cast<const unsigned char*>(a);
    const unsigned char* y = static_cast<const unsigned char*>(b);
    unsigned char d = 0;
    for (std::size_t i = 0; i < n; ++i) d |= static_cast<unsigned char>(x[i] ^ y[i]);
    return d == 0;
#else
    return CRYPTO_memcmp(a, b, n) == 0;
#endif
}

// ------------------------------------------------------------------------------------------------
// sockets
// ------------------------------------------------------------------------------------------------
#if defined(_WIN32)
#  ifndef SHUT_RDWR
#    define SHUT_RDWR SD_BOTH
#  endif
inline int sock_close(int s) { return closesocket(static_cast<SOCKET>(s)); }
inline int poll(pollfd* fds, std::size_t n, int timeout_ms) { return WSAPoll(fds, static_cast<ULONG>(n), timeout_ms); }
inline int fsync(int fd) { return _commit(fd); }
inline std::tm* localtime_r(const std::time_t* t, std::tm* out) { return localtime_s(out, t) == 0 ? out : nullptr; }
inline std::tm* gmtime_r(const std::time_t* t, std::tm* out) { return gmtime_s(out, t) == 0 ? out : nullptr; }

// One-byte non-blocking peek: 1 = data waiting, 0 = peer closed, -1 = nothing yet, -2 = error.
inline int peek_nonblock(int fd) {
    u_long avail = 0;
    if (ioctlsocket(static_cast<SOCKET>(fd), FIONREAD, &avail) != 0) return -2;
    if (avail > 0) return 1;
    WSAPOLLFD p{}; p.fd = static_cast<SOCKET>(fd); p.events = POLLRDNORM;
    const int r = WSAPoll(&p, 1, 0);
    if (r < 0) return -2;
    if (r == 0) return -1;
    if (p.revents & (POLLERR | POLLNVAL)) return -2;
    if (p.revents & POLLHUP) return 0;
    char c; const int got = recv(static_cast<SOCKET>(fd), &c, 1, MSG_PEEK);
    if (got == 0) return 0;
    if (got < 0) return WSAGetLastError() == WSAEWOULDBLOCK ? -1 : -2;
    return 1;
}

// socketpair() for threads: two connected loopback TCP sockets.
inline int astro_socketpair(int sv[2]) {
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l == INVALID_SOCKET) return -1;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    int alen = sizeof(a);
    SOCKET c = INVALID_SOCKET, s = INVALID_SOCKET;
    if (bind(l, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 && getsockname(l, reinterpret_cast<sockaddr*>(&a), &alen) == 0 &&
        listen(l, 1) == 0) {
        c = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (c != INVALID_SOCKET && connect(c, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) s = accept(l, nullptr, nullptr);
    }
    closesocket(l);
    if (s == INVALID_SOCKET) { if (c != INVALID_SOCKET) closesocket(c); return -1; }
    BOOL nodelay = TRUE;
    setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
    sv[0] = static_cast<int>(s); sv[1] = static_cast<int>(c);
    return 0;
}
#else
inline int sock_close(int s) { return ::close(s); }
inline int peek_nonblock(int fd) {
    char c;
    const ssize_t got = recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    if (got > 0) return 1;
    if (got == 0) return 0;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? -1 : -2;
}
inline int astro_socketpair(int sv[2]) { return socketpair(AF_UNIX, SOCK_STREAM, 0, sv); }
inline int pipe_cloexec(int fds[2]) {
#  if defined(__APPLE__)
    if (pipe(fds) != 0) return -1;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC); fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return 0;
#  else
    return pipe2(fds, O_CLOEXEC);
#  endif
}
#endif

// ------------------------------------------------------------------------------------------------
// Windows process toolkit: UTF-8 <-> UTF-16, argument quoting, environment blocks, Job Objects
// (kill the whole process tree), inherited-handle lists (no handle leaks between threads), ConPTY.
// ------------------------------------------------------------------------------------------------
#if defined(_WIN32)
namespace winp {

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// Quotes one argument so CommandLineToArgvW / the MSVC CRT / MSYS bash read it back unchanged.
// UTF-8 <-> std::filesystem::path (C++20 u8string() returns char8_t strings).
inline std::filesystem::path u8p(const std::string& s) { return std::filesystem::path(widen(s)); }
inline std::string p8(const std::filesystem::path& p) { return narrow(p.wstring()); }

inline std::string quote_arg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
    std::string out = "\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t bs = 0;
        while (i < a.size() && a[i] == '\\') { ++i; ++bs; }
        if (i == a.size()) { out.append(bs * 2, '\\'); break; }
        if (a[i] == '"') { out.append(bs * 2 + 1, '\\'); out += '"'; }
        else { out.append(bs, '\\'); out += a[i]; }
    }
    out += '"';
    return out;
}
inline std::string join_args(const std::vector<std::string>& args) {
    std::string s;
    for (const auto& a : args) { if (!s.empty()) s += ' '; s += quote_arg(a); }
    return s;
}

inline std::string getenv_s8(const char* name) {
    const std::wstring w = widen(name);
    const DWORD n = GetEnvironmentVariableW(w.c_str(), nullptr, 0);
    if (!n) return {};
    std::wstring v(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(w.c_str(), v.data(), n);
    v.resize(got);
    return narrow(v);
}

// Searches PATH (+PATHEXT) for a program. Skips the Microsoft Store "app execution alias" stubs.
inline std::string which(const std::string& name) {
    namespace fs = std::filesystem;
    std::vector<std::string> exts = {""};
    if (fs::path(name).extension().empty()) exts = {".exe", ".cmd", ".bat", ".com"};
    std::string path = getenv_s8("PATH");
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find(';', start);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(start, end - start);
        start = end + 1;
        if (dir.empty()) continue;
        std::string low = dir; for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (low.find("\\windowsapps") != std::string::npos) continue;
        for (const auto& e : exts) {
            std::error_code ec;
            const fs::path p = u8p(dir) / u8p(name + e);
            if (fs::is_regular_file(p, ec)) return p8(p);
        }
    }
    return {};
}

// Git for Windows' bash (runs code, the agent's commands and app servers exactly like on Linux).
inline const std::string& bash_path() {
    static const std::string found = [] {
        namespace fs = std::filesystem;
        std::vector<std::string> cands;
        if (auto e = getenv_s8("ASTRO_BASH"); !e.empty()) cands.push_back(e);
        for (const char* var : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"})
            if (auto b = getenv_s8(var); !b.empty()) cands.push_back(b + "\\Git\\bin\\bash.exe");
        if (auto l = getenv_s8("LOCALAPPDATA"); !l.empty()) cands.push_back(l + "\\Programs\\Git\\bin\\bash.exe");
        if (auto g = which("git"); !g.empty()) {   // ...\Git\cmd\git.exe -> ...\Git\bin\bash.exe
            const fs::path root = u8p(g).parent_path().parent_path();
            cands.push_back(p8(root / "bin" / "bash.exe"));
        }
        for (const auto& c : cands) { std::error_code ec; if (!c.empty() && fs::is_regular_file(u8p(c), ec)) return c; }
        return std::string();
    }();
    return found;
}

// Current environment merged with NAME=value overrides (case-insensitive names), as a sorted UTF-16 block.
inline std::wstring env_block(const std::vector<std::string>& overrides) {
    std::map<std::wstring, std::pair<std::wstring, std::wstring>> vars;   // upper-name -> (name, value)
    auto up = [](std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towupper(c)); return s; };
    if (wchar_t* env = GetEnvironmentStringsW()) {
        for (wchar_t* p = env; *p; p += wcslen(p) + 1) {
            std::wstring kv(p);
            const auto eq = kv.find(L'=', 1);
            if (eq == std::wstring::npos) continue;
            if (astro_secret_env(narrow(kv.substr(0, eq)))) continue;
            vars[up(kv.substr(0, eq))] = {kv.substr(0, eq), kv.substr(eq + 1)};
        }
        FreeEnvironmentStringsW(env);
    }
    for (const auto& o : overrides) {
        const std::wstring kv = widen(o);
        const auto eq = kv.find(L'=');
        if (eq == std::wstring::npos || eq == 0) continue;
        vars[up(kv.substr(0, eq))] = {kv.substr(0, eq), kv.substr(eq + 1)};
    }
    std::wstring block;
    for (const auto& [k, nv] : vars) { block += nv.first; block += L'='; block += nv.second; block.push_back(L'\0'); }
    block.push_back(L'\0');
    return block;
}

struct Child {
    HANDLE proc = nullptr;
    HANDLE job = nullptr;
    DWORD pid = 0;
};

inline void close_child(Child& c) {
    if (c.proc) { CloseHandle(c.proc); c.proc = nullptr; }
    if (c.job) { CloseHandle(c.job); c.job = nullptr; }
}
inline void kill_tree(Child& c, UINT code = 1) { if (c.job) TerminateJobObject(c.job, code); else if (c.proc) TerminateProcess(c.proc, code); }
inline int exit_code(const Child& c) { DWORD code = 0; return c.proc && GetExitCodeProcess(c.proc, &code) ? static_cast<int>(code) : -1; }
inline bool running(const Child& c) { return c.proc && WaitForSingleObject(c.proc, 0) == WAIT_TIMEOUT; }

// Anonymous pipe; the end the child uses is inheritable, the parent's end is not.
inline bool make_pipe(HANDLE& rd, HANDLE& wr, bool child_reads) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(child_reads ? wr : rd, HANDLE_FLAG_INHERIT, 0);
    return true;
}

// ConPTY entry points (Windows 10 1809+), loaded at run time so the program still starts on older systems.
using CreatePC_t = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, void**);
using ResizePC_t = HRESULT(WINAPI*)(void*, COORD);
using ClosePC_t = void(WINAPI*)(void*);
struct ConPtyApi { CreatePC_t create = nullptr; ResizePC_t resize = nullptr; ClosePC_t close = nullptr; };
inline const ConPtyApi& conpty() {
    static const ConPtyApi api = [] {
        ConPtyApi a;
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll")) {
            a.create = reinterpret_cast<CreatePC_t>(reinterpret_cast<void*>(GetProcAddress(k, "CreatePseudoConsole")));
            a.resize = reinterpret_cast<ResizePC_t>(reinterpret_cast<void*>(GetProcAddress(k, "ResizePseudoConsole")));
            a.close = reinterpret_cast<ClosePC_t>(reinterpret_cast<void*>(GetProcAddress(k, "ClosePseudoConsole")));
        }
        return a;
    }();
    return api;
}
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#  define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

// Starts `cmdline` in `cwd` with the merged environment, inside a kill-on-close Job Object (so the whole
// tree dies together). Only the given std handles are inherited. With `pseudo_console`, the child is
// attached to that ConPTY instead of std handles.
inline bool spawn(const std::string& cmdline, const std::string& cwd, const std::vector<std::string>& env,
                  HANDLE in, HANDLE out, HANDLE err, Child& c, std::string& error, void* pseudo_console = nullptr,
                  bool in_job = true) {
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    HANDLE inherit[3]; DWORD n_inherit = 0;
    if (!pseudo_console) {
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = in; si.StartupInfo.hStdOutput = out; si.StartupInfo.hStdError = err;
        for (HANDLE h : {in, out, err}) {
            if (!h || h == INVALID_HANDLE_VALUE) continue;
            if (std::find(inherit, inherit + n_inherit, h) == inherit + n_inherit) inherit[n_inherit++] = h;
        }
    }
    const DWORD attr_count = pseudo_console ? 1 : (n_inherit ? 1 : 0);
    SIZE_T attr_size = 0;
    std::vector<unsigned char> attr_buf;
    if (attr_count) {
        InitializeProcThreadAttributeList(nullptr, attr_count, 0, &attr_size);
        attr_buf.resize(attr_size);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
        if (!InitializeProcThreadAttributeList(si.lpAttributeList, attr_count, 0, &attr_size)) { error = "Could not prepare the process."; return false; }
        const BOOL ok = pseudo_console
            ? UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudo_console, sizeof(void*), nullptr, nullptr)
            : UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, n_inherit * sizeof(HANDLE), nullptr, nullptr);
        if (!ok) { DeleteProcThreadAttributeList(si.lpAttributeList); error = "Could not prepare the process."; return false; }
    }
    std::wstring cmd = widen(cmdline);
    std::wstring wcwd = widen(cwd);
    std::wstring block = env_block(env);
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT |
                        (pseudo_console ? 0 : CREATE_NO_WINDOW);
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, (!pseudo_console && n_inherit) ? TRUE : FALSE, flags,
                                   block.data(), wcwd.empty() ? nullptr : wcwd.c_str(), &si.StartupInfo, &pi);
    if (attr_count) DeleteProcThreadAttributeList(si.lpAttributeList);
    if (!ok) {
        const DWORD e = GetLastError();
        error = e == ERROR_FILE_NOT_FOUND ? "Program not found." : "Could not start the process (Windows error " + std::to_string(e) + ").";
        return false;
    }
    c.job = in_job ? CreateJobObjectW(nullptr, nullptr) : nullptr;
    if (c.job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(c.job, JobObjectExtendedLimitInformation, &li, sizeof(li));
        if (!AssignProcessToJobObject(c.job, pi.hProcess)) { CloseHandle(c.job); c.job = nullptr; }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    c.proc = pi.hProcess; c.pid = pi.dwProcessId;
    return true;
}

// Builds the command line that runs a bash command string (Git Bash), or cmd.exe as a last resort.
inline std::string shell_cmdline(const std::string& command, bool& have_bash) {
    const std::string& bash = bash_path();
    have_bash = !bash.empty();
    if (have_bash) return quote_arg(bash) + " -c " + quote_arg(command);
    return "cmd.exe /d /s /c \"" + command + "\"";
}

// Process ids inside a job (for "which port did this app open?").
inline std::vector<DWORD> job_pids(HANDLE job) {
    std::vector<DWORD> out;
    if (!job) return out;
    std::vector<unsigned char> buf(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 1024 * sizeof(ULONG_PTR));
    auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, static_cast<DWORD>(buf.size()), nullptr)) return out;
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) out.push_back(static_cast<DWORD>(list->ProcessIdList[i]));
    return out;
}
// Lowest TCP port in LISTEN state owned by any of `pids` (IPv4 + IPv6).
inline int listen_port_of(const std::vector<DWORD>& pids) {
    if (pids.empty()) return 0;
    int best = 0;
    for (ULONG family : {static_cast<ULONG>(AF_INET), static_cast<ULONG>(AF_INET6)}) {
        DWORD size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (!size) continue;
        std::vector<unsigned char> buf(size + 4096);
        size = static_cast<DWORD>(buf.size());
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) continue;
        if (family == AF_INET) {
            auto* t = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < t->dwNumEntries; ++i)
                if (std::find(pids.begin(), pids.end(), t->table[i].dwOwningPid) != pids.end()) {
                    const int port = ntohs(static_cast<u_short>(t->table[i].dwLocalPort));
                    if (best == 0 || port < best) best = port;
                }
        } else {
            auto* t = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < t->dwNumEntries; ++i)
                if (std::find(pids.begin(), pids.end(), t->table[i].dwOwningPid) != pids.end()) {
                    const int port = ntohs(static_cast<u_short>(t->table[i].dwLocalPort));
                    if (best == 0 || port < best) best = port;
                }
        }
    }
    return best;
}

// Reads a pipe to the end on a background thread, handing each chunk to `sink`.
template <typename Sink>
void drain_async(HANDLE rd, Sink sink) {
    std::thread([rd, sink]() mutable {
        char buf[16384];
        DWORD got = 0;
        while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) sink(buf, static_cast<std::size_t>(got));
        CloseHandle(rd);
        sink(nullptr, 0);   // end of stream
    }).detach();
}

} // namespace winp
#endif
