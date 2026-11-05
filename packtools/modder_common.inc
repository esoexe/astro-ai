// modder_common.inc - shared helpers for `um`, the Universal Modder tools: a C++ port of universal-modder's
// um/common.py (Rehan and universal-modder contributors, MIT) plus the pieces Python gave it for free: a YAML reader
// for note front matter (PyYAML's safe_load rules for the shapes notes use: block and flow collections, quoted and
// plain scalars, YAML 1.1 booleans, ints, floats, dates), a YAML writer (safe_dump, block style), fnmatch, an
// incremental SHA-1 for big game files, and a ZIP writer (deflate) for backups.
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <map>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <sys/utsname.h>
#include <unistd.h>
#include <fcntl.h>
#else
#include <io.h>
#include <fcntl.h>
#endif

namespace packtools::modder {
using namespace pyrt;
namespace fs = std::filesystem;
namespace mk = packtools::master_mk;

inline const std::string UM_VERSION = "0.2.0";

// ------------------------------------------------------------------ errors and output
inline void die(const std::string& msg, int code = 1) { eprint("um: " + msg); throw Exit{code}; }
inline std::string env(const char* n) { const char* v = std::getenv(n); return v ? std::string(v) : std::string(); }
inline std::string jdump(const Val& v, int indent = 2) { DumpOpts o; o.indent = indent; return dumps(v, o); }
inline void emit_json(const Val& v, int indent = 2) { print(jdump(v, indent)); }
inline std::string u8p(const fs::path& p) { const auto x = p.generic_u8string(); return std::string(reinterpret_cast<const char*>(x.data()), x.size()); }
inline std::string u8native(const fs::path& p) { const auto x = p.u8string(); return std::string(reinterpret_cast<const char*>(x.data()), x.size()); }
inline fs::path P(const std::string& s) { return fspath(s); }
inline std::string name_of_path(const std::string& p) { return u8native(P(p).filename()); }

// ------------------------------------------------------------------ platform
inline bool is_windows() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}
inline bool is_mac() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}
inline bool is_wsl() {
#ifndef _WIN32
    // UM_FAKE_WSL=1: a test switch (tests/modder_win.sh) that makes any POSIX system take the WSL paths
    if (!env("UM_FAKE_WSL").empty()) return env("UM_FAKE_WSL") == "1";
#endif
#if defined(__linux__)
    struct utsname u{};
    if (uname(&u) == 0 && lower(u.release).find("microsoft") != std::string::npos) return true;
    std::error_code ec; return fs::exists("/proc/sys/fs/binfmt_misc/WSLInterop", ec);
#else
    return false;
#endif
}
inline std::string home_dir() {
#ifdef _WIN32
    std::string h = env("USERPROFILE"); if (!h.empty()) return h;
    return env("HOMEDRIVE") + env("HOMEPATH");
#else
    std::string h = env("HOME"); return h.empty() ? std::string("/") : h;
#endif
}
inline std::string expanduser(const std::string& p) {
    if (p == "~") return home_dir();
    if (p.size() > 1 && p[0] == '~' && (p[1] == '/' || p[1] == '\\')) return home_dir() + p.substr(1);
    return p;
}
inline std::string which_tool(const std::string& name) { return mk::which(name); }   // shutil.which
// Windows PowerShell; falls back to its full path (an agent's PATH often lacks System32\WindowsPowerShell\v1.0)
inline std::string ps_exe() {
    const std::string name = is_wsl() ? "powershell.exe" : "powershell";
    const std::string found = mk::which(name);
    if (!found.empty()) return found;
    std::string full;
    if (is_wsl()) full = "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe";
    else { std::string root = env("SystemRoot"); if (root.empty()) root = "C:\\Windows"; full = root + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"; }
    std::error_code ec; return fs::exists(P(full), ec) ? full : name;
}
inline bool is_alpha_c(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
inline char up_c(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
inline char low_c(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
// /mnt/c/Games/x -> C:\Games\x (WSL); paths that are already Windows paths pass through
inline std::string to_win(const std::string& p) {
    if (p.size() > 1 && p[1] == ':') return p;
    if (starts(p, "/mnt/") && p.size() > 6 && p[6] == '/' && is_alpha_c(p[5])) return std::string(1, up_c(p[5])) + ":\\" + replace_all(p.substr(7), "/", "\\");
    if (starts(p, "/mnt/") && p.size() == 6) return std::string(1, up_c(p[5])) + ":\\";
    if (is_wsl() && !mk::which("wslpath").empty()) { const auto r = mk::run_proc({"wslpath", "-w", p}); return strip(decode_utf8_replace(r.out)); }
    return p;
}
// C:\Games\x -> /mnt/c/Games/x under WSL; unchanged elsewhere
inline std::string to_posix(const std::string& p) {
    if (is_wsl() && p.size() > 1 && p[1] == ':') return "/mnt/" + std::string(1, low_c(p[0])) + replace_all(p.substr(2), "\\", "/");
    return p;
}
// per-user state: backups, synced notes. Override with UM_HOME.
inline std::string data_dir() {
    std::string d = env("UM_HOME"); if (d.empty()) d = path_join(home_dir(), ".universal-modder");
    std::error_code ec; fs::create_directories(P(d), ec); return d;
}
// '64x26' -> (64, 26); '128' -> (128, 128)
inline std::pair<long long, long long> parse_size(const std::string& s) {
    const std::string l = lower(s); const auto k = l.find('x');
    auto num = [&](const std::string& t) { auto v = py_int(t); if (!v) throw PyErr("ValueError", "invalid literal for int() with base 10: " + repr(Val::str(t))); return *v; };
    if (k != std::string::npos) return {num(l.substr(0, k)), num(l.substr(k + 1))};
    const long long v = num(s); return {v, v};
}
inline const std::string PS_UTF8 = "try{[Console]::OutputEncoding=[Text.Encoding]::UTF8}catch{};";

// subprocess.run(..., check=True, capture_output=True, text=True) with um's messages
struct Run { int code = 0; std::string out, err; };
inline Run run(const std::vector<std::string>& cmd, bool check = true, int timeout_s = 0, const std::string& cwd = "") {
    const mk::Proc p = mk::run_proc(cmd, cwd, {}, timeout_s);
    if (p.not_found || (p.os_errno && p.code < 0 && !p.timed_out)) die("not found: " + cmd[0]);
    auto head3 = [&]() { std::vector<std::string> v(cmd.begin(), cmd.begin() + static_cast<long>(std::min<std::size_t>(3, cmd.size()))); return join(v, " "); };
    if (p.timed_out) die(head3() + "... timed out after " + std::to_string(timeout_s) + "s");
    Run r{p.code, decode_utf8_replace(p.out), decode_utf8_replace(p.err)};
    if (check && r.code != 0) {
        std::string t = strip(!r.err.empty() ? r.err : r.out); if (t.size() > 2000) t = t.substr(t.size() - 2000);
        die(head3() + "... failed (" + std::to_string(r.code) + "):\n" + t);
    }
    return r;
}

// ------------------------------------------------------------------ files
inline bool isdir(const std::string& p) { std::error_code ec; return fs::is_directory(P(p), ec); }
inline bool isfile(const std::string& p) { std::error_code ec; return fs::is_regular_file(P(p), ec); }
inline bool pexists(const std::string& p) { std::error_code ec; return fs::exists(P(p), ec); }
inline long long fsize(const std::string& p) { std::error_code ec; const auto n = fs::file_size(P(p), ec); return ec ? -1 : static_cast<long long>(n); }
inline std::string slurp(const std::string& p) {   // bytes, "" when unreadable
    std::ifstream f(P(p), std::ios::binary); if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
inline std::string head_bytes(const std::string& p, std::size_t n) {
    std::ifstream f(P(p), std::ios::binary); if (!f) return {};
    std::string b(n, '\0'); f.read(b.data(), static_cast<std::streamsize>(n)); b.resize(static_cast<std::size_t>(f.gcount())); return b;
}
inline void spit(const std::string& p, const std::string& data) {
    std::error_code ec; const auto parent = P(p).parent_path(); if (!parent.empty()) fs::create_directories(parent, ec);
    std::ofstream f(P(p), std::ios::binary | std::ios::trunc); if (!f) throw OSError(errno ? errno : 13, p);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}
// every regular file under root (sorted, generic relative paths), skipping dirs whose name is in skip
inline std::vector<std::string> rglob_files(const std::string& root, const std::set<std::string>& skip = {}) {
    std::vector<std::string> out; std::error_code ec;
    fs::recursive_directory_iterator it(P(root), fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto& e = *it; std::error_code e2;
        if (e.is_directory(e2)) { if (skip.count(u8native(e.path().filename()))) it.disable_recursion_pending(); continue; }
        if (e.is_regular_file(e2)) out.push_back(u8p(fs::relative(e.path(), P(root), e2)));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ------------------------------------------------------------------ fnmatch (case-sensitive, like fnmatch on POSIX; callers lower-case)
inline std::size_t u8len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4; }
inline bool fnmatch_at(const std::string& s, std::size_t i, const std::string& p, std::size_t j) {
    while (j < p.size()) {
        const char c = p[j];
        if (c == '*') {
            while (j < p.size() && p[j] == '*') ++j;
            if (j == p.size()) return true;
            for (std::size_t k = i; k <= s.size(); ++k) if (fnmatch_at(s, k, p, j)) return true;
            return false;
        }
        if (i >= s.size()) return false;
        if (c == '?') { i += u8len(static_cast<unsigned char>(s[i])); ++j; continue; }
        if (c == '[') {
            std::size_t k = j + 1; bool neg = false;
            if (k < p.size() && p[k] == '!') { neg = true; ++k; }
            if (k < p.size() && p[k] == ']') ++k;
            while (k < p.size() && p[k] != ']') ++k;
            if (k >= p.size()) { if (s[i] != '[') return false; ++i; ++j; continue; }   // no closing ]: a literal [
            const std::string set = p.substr(j + 1 + (neg ? 1 : 0), k - j - 1 - (neg ? 1 : 0));
            bool hit = false; const char ch = s[i];
            for (std::size_t q = 0; q < set.size(); ++q) {
                if (q + 2 < set.size() && set[q + 1] == '-') { if (ch >= set[q] && ch <= set[q + 2]) hit = true; q += 2; }
                else if (set[q] == ch) hit = true;
            }
            if (hit == neg) return false;
            ++i; j = k + 1; continue;
        }
        if (s[i] != c) return false;
        ++i; ++j;
    }
    return i == s.size();
}
inline bool fnmatch(const std::string& s, const std::string& p) { return fnmatch_at(s, 0, p, 0); }

// ------------------------------------------------------------------ SHA-1 (incremental, for big files)
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    unsigned char buf[64]{}; std::size_t blen = 0; uint64_t total = 0;
    static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    void block(const unsigned char* b) {
        uint32_t w[80];
        for (int t = 0; t < 16; ++t) w[t] = (uint32_t(b[t * 4]) << 24) | (uint32_t(b[t * 4 + 1]) << 16) | (uint32_t(b[t * 4 + 2]) << 8) | uint32_t(b[t * 4 + 3]);
        for (int t = 16; t < 80; ++t) w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int t = 0; t < 80; ++t) {
            uint32_t f, k;
            if (t < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999; } else if (t < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1; }
            else if (t < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; } else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
            const uint32_t tmp = rol(a, 5) + f + e + k + w[t]; e = d; d = c; c = rol(bb, 30); bb = a; a = tmp;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    }
    void update(const char* data, std::size_t n) {
        const auto* p = reinterpret_cast<const unsigned char*>(data); total += n;
        while (n) {
            const std::size_t take = std::min<std::size_t>(64 - blen, n);
            std::memcpy(buf + blen, p, take); blen += take; p += take; n -= take;
            if (blen == 64) { block(buf); blen = 0; }
        }
    }
    std::string hex() {
        const uint64_t bits = total * 8; const unsigned char one = 0x80, zero = 0;
        update(reinterpret_cast<const char*>(&one), 1);
        while (blen != 56) update(reinterpret_cast<const char*>(&zero), 1);
        unsigned char l[8]; for (int k = 0; k < 8; ++k) l[k] = static_cast<unsigned char>(bits >> (56 - 8 * k));
        update(reinterpret_cast<const char*>(l), 8);
        char o[41]; for (int k = 0; k < 5; ++k) std::snprintf(o + k * 8, 9, "%08x", h[k]); return std::string(o, 40);
    }
};
inline std::string sha1_file(const std::string& p) {
    std::ifstream f(P(p), std::ios::binary); if (!f) throw OSError(errno ? errno : 2, p);
    Sha1 s; std::vector<char> b(1 << 20);
    while (f) { f.read(b.data(), static_cast<std::streamsize>(b.size())); const auto n = f.gcount(); if (n > 0) s.update(b.data(), static_cast<std::size_t>(n)); }
    return s.hex();
}

// ------------------------------------------------------------------ ZIP writer (deflate, ZIP64 when needed)
struct ZipWriter {
    std::ofstream f; std::string path;
    struct Ent { std::string name; uint32_t crc; uint64_t csize, usize, off; uint16_t method, time, date; };
    std::vector<Ent> ents; uint64_t off = 0;
    explicit ZipWriter(const std::string& p) : f(P(p), std::ios::binary | std::ios::trunc), path(p) { if (!f) throw OSError(errno ? errno : 13, p); }
    static void le(std::string& o, uint64_t v, int n) { for (int k = 0; k < n; ++k) o += static_cast<char>((v >> (8 * k)) & 0xFF); }
    static std::pair<uint16_t, uint16_t> dos(std::time_t t) {
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        if (tm.tm_year < 80) { tm.tm_year = 80; tm.tm_mon = 0; tm.tm_mday = 1; tm.tm_hour = tm.tm_min = tm.tm_sec = 0; }   // strict_timestamps=False
        return {static_cast<uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2)), static_cast<uint16_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday)};
    }
    void add(const std::string& name, const std::string& data, std::time_t mtime) {
        Ent e{name, pyrt::zip::crc32(data), 0, data.size(), off, 8, 0, 0};
        const auto [t, d] = dos(mtime); e.time = t; e.date = d;
        std::string comp;
        if (data.size() <= (256u << 20)) { const std::string z = pyrt::zip::zlib_compress(data); comp = z.substr(2, z.size() - 6); }
        if (comp.empty() || comp.size() >= data.size()) { comp = data; e.method = 0; }
        e.csize = comp.size();
        const bool z64 = e.usize >= 0xFFFFFFFFull || e.csize >= 0xFFFFFFFFull;
        std::string h; le(h, 0x04034b50, 4); le(h, z64 ? 45 : 20, 2); le(h, 0x0800, 2); le(h, e.method, 2); le(h, e.time, 2); le(h, e.date, 2);
        le(h, e.crc, 4); le(h, z64 ? 0xFFFFFFFFu : e.csize, 4); le(h, z64 ? 0xFFFFFFFFu : e.usize, 4); le(h, name.size(), 2); le(h, z64 ? 20 : 0, 2); h += name;
        if (z64) { le(h, 1, 2); le(h, 16, 2); le(h, e.usize, 8); le(h, e.csize, 8); }
        f.write(h.data(), static_cast<std::streamsize>(h.size())); f.write(comp.data(), static_cast<std::streamsize>(comp.size()));
        off += h.size() + comp.size(); ents.push_back(e);
        if (!f) throw OSError(28, path);
    }
    void close() {
        std::string c; const uint64_t cd = off;
        for (const auto& e : ents) {
            const bool z64 = e.usize >= 0xFFFFFFFFull || e.csize >= 0xFFFFFFFFull || e.off >= 0xFFFFFFFFull;
            std::string extra; if (z64) { le(extra, 1, 2); le(extra, 24, 2); le(extra, e.usize, 8); le(extra, e.csize, 8); le(extra, e.off, 8); }
            le(c, 0x02014b50, 4); le(c, z64 ? 45 : 20, 2); le(c, z64 ? 45 : 20, 2); le(c, 0x0800, 2); le(c, e.method, 2); le(c, e.time, 2); le(c, e.date, 2);
            le(c, e.crc, 4); le(c, z64 ? 0xFFFFFFFFu : e.csize, 4); le(c, z64 ? 0xFFFFFFFFu : e.usize, 4); le(c, e.name.size(), 2); le(c, extra.size(), 2);
            le(c, 0, 2); le(c, 0, 2); le(c, 0, 2); le(c, 0x81A40000u, 4); le(c, z64 ? 0xFFFFFFFFu : e.off, 4); c += e.name; c += extra;
        }
        const bool z64 = ents.size() >= 0xFFFF || cd >= 0xFFFFFFFFull;
        if (z64) {
            le(c, 0x06064b50, 4); le(c, 44, 8); le(c, 45, 2); le(c, 45, 2); le(c, 0, 4); le(c, 0, 4); le(c, ents.size(), 8); le(c, ents.size(), 8);
            le(c, c.size() - 0, 8);  // placeholder fixed below
            le(c, cd, 8);
        }
        std::string tail;
        if (z64) {
            // central directory size = bytes before the zip64 record
            const std::size_t cdsize = c.size() - 56; for (int k = 0; k < 8; ++k) c[cdsize + 40 + k] = static_cast<char>((uint64_t(cdsize) >> (8 * k)) & 0xFF);
            le(tail, 0x07064b50, 4); le(tail, 0, 4); le(tail, cd + cdsize, 8); le(tail, 1, 4);
            le(tail, 0x06054b50, 4); le(tail, 0, 2); le(tail, 0, 2); le(tail, 0xFFFF, 2); le(tail, 0xFFFF, 2); le(tail, 0xFFFFFFFFu, 4); le(tail, 0xFFFFFFFFu, 4); le(tail, 0, 2);
        } else {
            le(tail, 0x06054b50, 4); le(tail, 0, 2); le(tail, 0, 2); le(tail, ents.size(), 2); le(tail, ents.size(), 2); le(tail, c.size(), 4); le(tail, cd, 4); le(tail, 0, 2);
        }
        f.write(c.data(), static_cast<std::streamsize>(c.size())); f.write(tail.data(), static_cast<std::streamsize>(tail.size())); f.close();
        if (f.fail()) throw OSError(28, path);
    }
};

// ------------------------------------------------------------------ YAML (the subset PyYAML's safe_load gives note front matter)
struct YamlError : std::runtime_error { using std::runtime_error::runtime_error; };
// A scalar Val tagged as a date: a one-key dict {"\x01date": "YYYY-MM-DD"} would leak, so dates are STR values whose
// text is the ISO date and whose `i` field is set to 1 (strings never use `i`).
inline Val ydate(const std::string& iso) { Val v = Val::str(iso); v.i = 0x7DA7E; return v; }
inline bool is_date(const Val& v) { return v.t == Val::STR && v.i == 0x7DA7E; }
inline std::string ytype(const Val& v) {
    if (is_date(v)) return "date";
    switch (v.t) { case Val::NONE: return "NoneType"; case Val::BOOL: return "bool"; case Val::INT: return "int"; case Val::FLOAT: return "float";
                   case Val::STR: return "str"; case Val::LIST: return "list"; default: return "dict"; }
}
// Python's repr() of a YAML value (dates print as datetime.date(2026, 9, 30))
inline std::string yrepr(const Val& v) {
    if (is_date(v)) { const auto y = py_int(v.s.substr(0, 4)), m = py_int(v.s.substr(5, 2)), d = py_int(v.s.substr(8, 2)); return "datetime.date(" + std::to_string(*y) + ", " + std::to_string(*m) + ", " + std::to_string(*d) + ")"; }
    if (v.t == Val::STR) { Val c = Val::str(v.s); return repr(c); }
    return repr(v);
}
inline std::string ystr(const Val& v) { if (is_date(v)) return v.s; if (v.t == Val::STR) return v.s; return pystr(v); }

namespace yaml_detail {
inline bool all_of(const std::string& s, const char* set) { for (char c : s) if (!std::strchr(set, c)) return false; return true; }
inline Val resolve_plain(const std::string& s) {
    static const std::set<std::string> nulls = {"", "~", "null", "Null", "NULL"};
    static const std::set<std::string> yes = {"yes", "Yes", "YES", "true", "True", "TRUE", "on", "On", "ON"};
    static const std::set<std::string> no = {"no", "No", "NO", "false", "False", "FALSE", "off", "Off", "OFF"};
    if (nulls.count(s)) return Val();
    // PyYAML's bool regex is yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF (no single letters)
    if (yes.count(s)) return Val::boolean(true);
    if (no.count(s)) return Val::boolean(false);
    // ints: [-+]?0b[0-1_]+ | [-+]?0[0-7_]+ | [-+]?(?:0|[1-9][0-9_]*) | [-+]?0x[0-9a-fA-F_]+ | [-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+
    std::string b = s; int sign = 1; if (!b.empty() && (b[0] == '+' || b[0] == '-')) { if (b[0] == '-') sign = -1; b = b.substr(1); }
    auto strip_us = [](std::string t) { t.erase(std::remove(t.begin(), t.end(), '_'), t.end()); return t; };
    if (!b.empty()) {
        if (starts(b, "0b") && b.size() > 2 && all_of(b.substr(2), "01_")) { const std::string d = strip_us(b.substr(2)); if (!d.empty()) return Val::integer(sign * std::stoll(d, nullptr, 2)); }
        if (starts(b, "0x") && b.size() > 2 && all_of(b.substr(2), "0123456789abcdefABCDEF_")) { const std::string d = strip_us(b.substr(2)); if (!d.empty()) return Val::integer(sign * std::stoll(d, nullptr, 16)); }
        if (b[0] == '0' && b.size() > 1 && all_of(b.substr(1), "01234567_")) { const std::string d = strip_us(b.substr(1)); return Val::integer(d.empty() ? 0 : sign * std::stoll(d, nullptr, 8)); }
        if (b == "0") return Val::integer(0);
        if (b[0] >= '1' && b[0] <= '9' && all_of(b, "0123456789_")) {
            const std::string d = strip_us(b);
            if (d.size() < 19) return Val::integer(sign * std::stoll(d));
            Val v = Val::integer(0); v.s = (sign < 0 ? "-" : "") + d; return v;
        }
        // sexagesimal ints (190:20:30) - rare in notes; read as text like a date-less time would be wrong, so resolve
        if (b[0] >= '1' && b[0] <= '9' && b.find(':') != std::string::npos && all_of(b, "0123456789_:")) {
            long long v = 0; bool ok = true; for (const auto& part : split(b, ":")) { if (part.empty()) { ok = false; break; } v = v * 60 + std::stoll(strip_us(part)); }
            if (ok) return Val::integer(sign * v);
        }
    }
    // floats: [-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+][0-9]+)? | \.[0-9_]+(?:[eE][-+][0-9]+)? | [-+]?\.(?:inf|Inf|INF) | \.(?:nan|NaN|NAN)
    if (b == ".inf" || b == ".Inf" || b == ".INF") return Val::flt(sign * HUGE_VAL);
    if (s == ".nan" || s == ".NaN" || s == ".NAN") return Val::flt(std::nan(""));
    {
        std::size_t k = 0; const std::string& t = b; bool digits_first = false;
        while (k < t.size() && (std::isdigit(static_cast<unsigned char>(t[k])) || (k > 0 && t[k] == '_'))) { ++k; digits_first = true; }
        if (k < t.size() && t[k] == '.' && (digits_first || (k + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[k + 1]))))) {
            ++k; while (k < t.size() && (std::isdigit(static_cast<unsigned char>(t[k])) || t[k] == '_')) ++k;
            bool ok = true;
            if (k < t.size() && (t[k] == 'e' || t[k] == 'E')) {
                ++k; if (k < t.size() && (t[k] == '+' || t[k] == '-')) ++k; else ok = false;
                const std::size_t d0 = k; while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k; if (k == d0) ok = false;
            }
            if (ok && k == t.size() && (digits_first || t[0] == '.')) return Val::flt(sign * std::strtod(strip_us(t).c_str(), nullptr));
        }
    }
    // timestamps: a bare date is a datetime.date (an impossible one is an error, as datetime.date() raises)
    if (s.size() == 10 && s[4] == '-' && s[7] == '-' && all_of(s.substr(0, 4) + s.substr(5, 2) + s.substr(8, 2), "0123456789")) {
        const long long y = std::stoll(s.substr(0, 4)), m = std::stoll(s.substr(5, 2)), d = std::stoll(s.substr(8, 2));
        if (m < 1 || m > 12) throw YamlError("month must be in 1..12");
        if (!valid_ymd(y, m, d)) throw YamlError("day is out of range for month");
        return ydate(s);
    }
    return Val::str(s);
}
struct Line { int indent; std::string text; int no; };
inline bool looks_like_key(const std::string& t) {   // "key: ..." or "key:" at the start of a line
    if (t.empty() || std::strchr(" '\"[]{}#", t[0])) return false;
    for (std::size_t k = 0; k < t.size(); ++k) { if (t[k] == ':') return k + 1 == t.size() || t[k + 1] == ' ' || t[k + 1] == '\t'; }
    return false;
}
inline std::string strip_comment(const std::string& s) {   // a plain-scalar line: " #" starts a comment
    char q = 0;
    for (std::size_t k = 0; k < s.size(); ++k) {
        const char c = s[k];
        if (q) { if (c == q) { if (q == '\'' && k + 1 < s.size() && s[k + 1] == '\'') { ++k; continue; } q = 0; } else if (q == '"' && c == '\\') ++k; continue; }
        if ((c == '"' || c == '\'') && (k == 0 || s[k - 1] == ' ' || s[k - 1] == '[' || s[k - 1] == '{' || s[k - 1] == ',' || s[k - 1] == ':')) { q = c; continue; }
        if (c == '#' && (k == 0 || s[k - 1] == ' ' || s[k - 1] == '\t')) return rstrip(s.substr(0, k));
    }
    return rstrip(s);
}
inline std::string dq_unescape(const std::string& s, int no) {
    std::string o;
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] != '\\') { o += s[k]; continue; }
        if (++k >= s.size()) throw YamlError("found unknown escape character at line " + std::to_string(no));
        const char e = s[k];
        switch (e) {
            case '0': o += '\0'; break; case 'a': o += '\a'; break; case 'b': o += '\b'; break; case 't': case '\t': o += '\t'; break;
            case 'n': o += '\n'; break; case 'v': o += '\v'; break; case 'f': o += '\f'; break; case 'r': o += '\r'; break; case 'e': o += '\x1b'; break;
            case ' ': o += ' '; break; case '"': o += '"'; break; case '/': o += '/'; break; case '\\': o += '\\'; break;
            case 'N': o += "\xC2\x85"; break; case '_': o += "\xC2\xA0"; break;
            case 'x': case 'u': case 'U': {
                const int n = e == 'x' ? 2 : e == 'u' ? 4 : 8; if (k + n >= s.size() + 0 && k + n > s.size() - 0) {}
                const std::string h = s.substr(k + 1, static_cast<std::size_t>(n));
                if (static_cast<int>(h.size()) != n || !all_of(h, "0123456789abcdefABCDEF")) throw YamlError("expected escape sequence of " + std::to_string(n) + " hexadecimal numbers at line " + std::to_string(no));
                o += utf8(static_cast<uint32_t>(std::stoul(h, nullptr, 16))); k += static_cast<std::size_t>(n); break;
            }
            default: throw YamlError(std::string("found unknown escape character '") + e + "' at line " + std::to_string(no));
        }
    }
    return o;
}
// a quoted scalar at s[k] (k on the quote); returns the text and moves k past the closing quote
inline std::string quoted(const std::string& s, std::size_t& k, int no) {
    const char q = s[k]; std::string raw; ++k;
    for (; k < s.size(); ++k) {
        if (q == '\'' && s[k] == '\'') { if (k + 1 < s.size() && s[k + 1] == '\'') { raw += '\''; ++k; continue; } ++k; return raw; }
        if (q == '"' && s[k] == '\\') { raw += s[k]; if (k + 1 < s.size()) raw += s[++k]; continue; }
        if (q == '"' && s[k] == '"') { ++k; return dq_unescape(raw, no); }
        raw += s[k];
    }
    throw YamlError("while scanning a quoted scalar, found unexpected end of stream at line " + std::to_string(no));
}
// flow collections: [a, "b", {c: d}] and {a: b}
inline Val flow(const std::string& s, std::size_t& k, int no);
inline void skip_ws(const std::string& s, std::size_t& k) { while (k < s.size() && (s[k] == ' ' || s[k] == '\t' || s[k] == '\n')) ++k; }
inline Val flow_node(const std::string& s, std::size_t& k, int no, bool& was_plain, std::string& plain_text) {
    skip_ws(s, k); was_plain = false;
    if (k < s.size() && (s[k] == '[' || s[k] == '{')) return flow(s, k, no);
    if (k < s.size() && (s[k] == '"' || s[k] == '\'')) return Val::str(quoted(s, k, no));
    std::string t;
    while (k < s.size()) {
        const char c = s[k];
        if (c == ',' || c == ']' || c == '}' || c == '[' || c == '{') break;
        if (c == ':' && (k + 1 >= s.size() || s[k + 1] == ' ' || s[k + 1] == ',' || s[k + 1] == ']' || s[k + 1] == '}' || s[k + 1] == '\n')) break;
        if (c == '#' && (t.empty() || t.back() == ' ')) break;
        t += (c == '\n' ? ' ' : c); ++k;
    }
    // collapse the line breaks of a multi-line plain scalar
    std::string norm; bool sp = false; for (char c : strip(t)) { if (c == ' ') { if (!sp) norm += ' '; sp = true; } else { norm += c; sp = false; } }
    was_plain = true; plain_text = norm;
    return resolve_plain(norm);
}
inline std::string key_text(const Val& v) { return ystr(v); }
inline Val flow(const std::string& s, std::size_t& k, int no) {
    const char open = s[k], close = open == '[' ? ']' : '}'; ++k;
    if (open == '[') {
        Val out = Val::list();
        for (;;) {
            skip_ws(s, k);
            if (k >= s.size()) throw YamlError("while parsing a flow sequence, expected ',' or ']', but got end of stream (line " + std::to_string(no) + ")");
            if (s[k] == ']') { ++k; return out; }
            bool plain = false; std::string pt;
            const std::size_t at = k;
            Val item = flow_node(s, k, no, plain, pt);
            skip_ws(s, k);
            if (k < s.size() && s[k] == ':') {   // a single-pair mapping inside a flow sequence: [Cataclysm: Dark Days Ahead]
                ++k; bool p2 = false; std::string pt2; skip_ws(s, k);
                Val v = (k < s.size() && (s[k] == ',' || s[k] == ']')) ? Val() : flow_node(s, k, no, p2, pt2);
                Val m = Val::dict(); m.d->set(key_text(item), v); item = m; skip_ws(s, k);
            }
            if (plain && pt.empty() && k == at) throw YamlError("while parsing a flow sequence, expected ',' or ']' (line " + std::to_string(no) + ")");
            out.l->push_back(item);
            if (k < s.size() && s[k] == ',') { ++k; continue; }
            if (k < s.size() && s[k] == ']') { ++k; return out; }
            throw YamlError("while parsing a flow sequence, expected ',' or ']', but got " + std::string(k < s.size() ? std::string("'") + s[k] + "'" : "end of stream") + " (line " + std::to_string(no) + ")");
        }
    }
    Val out = Val::dict();
    for (;;) {
        skip_ws(s, k);
        if (k >= s.size()) throw YamlError("while parsing a flow mapping, expected ',' or '}', but got end of stream (line " + std::to_string(no) + ")");
        if (s[k] == close) { ++k; return out; }
        bool plain = false; std::string pt;
        Val key = flow_node(s, k, no, plain, pt); skip_ws(s, k);
        Val v;
        if (k < s.size() && s[k] == ':') { ++k; skip_ws(s, k); if (k < s.size() && (s[k] == ',' || s[k] == '}')) v = Val(); else { bool p2; std::string t2; v = flow_node(s, k, no, p2, t2); } }
        out.d->set(key_text(key), v); skip_ws(s, k);
        if (k < s.size() && s[k] == ',') { ++k; continue; }
        if (k < s.size() && s[k] == '}') { ++k; return out; }
        throw YamlError("while parsing a flow mapping, expected ',' or '}' (line " + std::to_string(no) + ")");
    }
}
struct Doc {
    std::vector<Line> L; std::size_t i = 0;
    static int indent_of(const std::string& s) { int n = 0; while (n < static_cast<int>(s.size()) && s[static_cast<std::size_t>(n)] == ' ') ++n; return n; }
    explicit Doc(const std::string& text) {
        int no = 0;
        for (auto ln : splitlines(text)) {
            ++no; if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (ln.find('\t') != std::string::npos && strip(ln).empty()) continue;
            const std::string t = strip(ln);
            if (t.empty() || t[0] == '#') { L.push_back({-1, "", no}); continue; }   // blank/comment: kept for block scalars
            L.push_back({indent_of(ln), ln.substr(static_cast<std::size_t>(indent_of(ln))), no});
        }
    }
    void skip_blank() { while (i < L.size() && L[i].indent < 0) ++i; }
    // the rest of a value that may continue on more-indented lines (flow collections, quoted and plain multi-line scalars)
    std::string gather(std::string first, int min_indent, bool flow_mode) {
        if (flow_mode) {
            auto balanced = [](const std::string& s) {
                int d = 0; char q = 0;
                // A quote opens a quoted scalar only where a node starts (after [ { , : ), as in strip_comment: the apostrophe
                // in [the core's library] is part of a plain scalar. Inside single quotes '' is an escaped quote.
                char prev = 0;   // the last character outside quotes that is not white space
                for (std::size_t k = 0; k < s.size(); ++k) {
                    const char c = s[k];
                    if (q) { if (c == q) { if (q == '\'' && k + 1 < s.size() && s[k + 1] == '\'') { ++k; continue; } q = 0; prev = c; } else if (q == '"' && c == '\\') ++k; continue; }
                    if ((c == '"' || c == '\'') && (prev == 0 || prev == '[' || prev == '{' || prev == ',' || prev == ':')) { q = c; continue; }
                    if (c == '[' || c == '{') ++d; else if (c == ']' || c == '}') --d;
                    if (c != ' ' && c != '\t' && c != '\n') prev = c;
                }
                return d <= 0;
            };
            std::string acc = first;
            while (!balanced(acc) && i < L.size()) { if (L[i].indent >= 0) acc += "\n" + strip_comment(L[i].text); ++i; }
            return acc;
        }
        std::string acc = first;
        while (true) {
            std::size_t j = i; while (j < L.size() && L[j].indent < 0) ++j;
            if (j >= L.size() || L[j].indent < min_indent) break;
            const std::string& t = L[j].text;
            if (starts(t, "- ") || t == "-") break;
            if (looks_like_key(t)) break;
            acc += " " + strip(strip_comment(t)); i = j + 1;
        }
        return acc;
    }
    Val scalar_value(const std::string& v0, int min_indent, int no) {
        std::string v = strip(v0);
        if (v.empty()) return Val();
        if (v[0] == '[' || v[0] == '{') {
            std::string all = gather(strip_comment(v), min_indent, true); std::size_t k = 0; Val r = flow(all, k, no);
            skip_ws(all, k); if (k < all.size() && all[k] != '#') throw YamlError("expected <block end>, but found '" + all.substr(k, 1) + "' (line " + std::to_string(no) + ")");
            return r;
        }
        if (v[0] == '"' || v[0] == '\'') {
            std::string all = v;
            // multi-line quoted scalar: keep reading until the quote closes
            for (;;) {
                std::size_t k = 0; try { std::string r = quoted(all, k, no); std::string rest = strip(all.substr(k)); if (!rest.empty() && rest[0] != '#') throw YamlError("expected <block end>, but found '<scalar>' (line " + std::to_string(no) + ")"); return Val::str(r); }
                catch (const YamlError& e) { if (std::string(e.what()).find("end of stream") == std::string::npos || i >= L.size()) throw; }
                std::size_t j = i; while (j < L.size() && L[j].indent < 0) ++j; if (j >= L.size()) throw YamlError("while scanning a quoted scalar, found unexpected end of stream (line " + std::to_string(no) + ")");
                all += " " + strip(L[j].text); i = j + 1;
            }
        }
        if (v[0] == '|' || v[0] == '>') {
            const bool fold = v[0] == '>'; std::string keep = v.substr(1);
            std::vector<std::string> lines; int ind = -1;
            while (i < L.size()) {
                if (L[i].indent < 0) { lines.emplace_back(""); ++i; continue; }
                if (L[i].indent < min_indent) break;
                if (ind < 0) ind = L[i].indent;
                if (L[i].indent < ind) break;
                lines.push_back(std::string(static_cast<std::size_t>(L[i].indent - ind), ' ') + L[i].text); ++i;
            }
            while (!lines.empty() && lines.back().empty()) lines.pop_back();
            std::string o;
            for (std::size_t k = 0; k < lines.size(); ++k) { if (k) o += (fold && !lines[k].empty() && !lines[k - 1].empty()) ? " " : "\n"; o += lines[k]; }
            if (keep.find('-') == std::string::npos && !lines.empty()) o += "\n";
            return Val::str(o);
        }
        if (v[0] == '&' || v[0] == '*' || v[0] == '!') {   // anchors, aliases, tags: not used by notes
            if (v[0] == '*') throw YamlError("found undefined alias '" + v.substr(1) + "' (line " + std::to_string(no) + ")");
            const auto sp = v.find(' '); return scalar_value(sp == std::string::npos ? "" : v.substr(sp + 1), min_indent, no);
        }
        if (v[0] == '@' || v[0] == '`') throw YamlError("found character '" + v.substr(0, 1) + "' that cannot start any token (line " + std::to_string(no) + ")");
        if (v[0] == '%') throw YamlError("found character '%' that cannot start any token (line " + std::to_string(no) + ")");
        std::string t = gather(strip_comment(v), min_indent, false);
        if (t.find(": ") != std::string::npos && !(t[0] == '"' || t[0] == '\''))
            throw YamlError("mapping values are not allowed here (line " + std::to_string(no) + ")");
        return resolve_plain(strip(t));
    }
    static bool split_key(const std::string& t, std::string& key, std::string& rest, int no) {
        std::size_t k = 0;
        if (!t.empty() && (t[0] == '"' || t[0] == '\'')) {
            key = quoted(t, k, no); skip_ws(t, k);
            if (k < t.size() && t[k] == ':' && (k + 1 == t.size() || t[k + 1] == ' ')) { rest = t.substr(k + 1); return true; }
            return false;
        }
        for (k = 0; k < t.size(); ++k) {
            if (t[k] == '#' && k > 0 && t[k - 1] == ' ') return false;
            if (t[k] == ':' && (k + 1 == t.size() || t[k + 1] == ' ')) { key = rstrip(t.substr(0, k)); rest = t.substr(k + 1); return !key.empty(); }
        }
        return false;
    }
    Val node(int indent) {
        skip_blank();
        if (i >= L.size()) return Val();
        const Line& ln = L[i];
        if (ln.indent < indent) return Val();
        const int ind = ln.indent;
        if (starts(ln.text, "- ") || ln.text == "-") {
            Val out = Val::list();
            while (true) {
                skip_blank(); if (i >= L.size() || L[i].indent != ind || !(starts(L[i].text, "- ") || L[i].text == "-")) break;
                const int no = L[i].no; std::string rest = L[i].text.size() > 1 ? L[i].text.substr(2) : ""; ++i;
                std::string k2, r2;
                const std::string rs = strip(rest);
                if (rs.empty()) out.l->push_back(node(ind + 1));
                else if (rs[0] != '[' && rs[0] != '{' && split_key(strip_comment(rs), k2, r2, no)) {
                    // "- key: value" starts a mapping indented at the item's text
                    const int sub = ind + 2 + static_cast<int>(rest.size() - ltrim(rest).size());
                    L.insert(L.begin() + static_cast<long>(i), Line{sub, rs, no});
                    out.l->push_back(node(sub));
                } else out.l->push_back(scalar_value(rest, ind + 1, no));
            }
            return out;
        }
        Val out = Val::dict();
        while (true) {
            skip_blank(); if (i >= L.size()) break;
            if (L[i].indent < ind) break;
            if (L[i].indent > ind) throw YamlError("mapping values are not allowed here (line " + std::to_string(L[i].no) + ")");
            const int no = L[i].no; std::string key, rest;
            if (starts(L[i].text, "- ")) throw YamlError("expected <block end>, but found '-' (line " + std::to_string(no) + ")");
            if (!split_key(L[i].text, key, rest, no)) throw YamlError("could not find expected ':' (line " + std::to_string(no) + ")");
            ++i;
            if (strip(strip_comment(rest)).empty()) {
                skip_blank();
                if (i < L.size() && (L[i].indent > ind || (L[i].indent == ind && (starts(L[i].text, "- ") || L[i].text == "-")))) out.d->set(key, node(L[i].indent));
                else out.d->set(key, Val());
            } else out.d->set(key, scalar_value(rest, ind + 1, no));
        }
        return out;
    }
    static std::string ltrim(const std::string& s) { std::size_t k = 0; while (k < s.size() && s[k] == ' ') ++k; return s.substr(k); }
};
}  // namespace yaml_detail
inline Val yaml_load(const std::string& text) {
    yaml_detail::Doc d(text);
    d.skip_blank(); if (d.i >= d.L.size()) return Val();
    if (d.L[d.i].indent > 0 && !(starts(d.L[d.i].text, "- "))) {}
    const std::string& first = d.L[d.i].text;
    std::string k, r;
    if (!(starts(first, "- ") || first == "-") && !yaml_detail::Doc::split_key(first, k, r, d.L[d.i].no)) {   // a lone scalar document
        const int no = d.L[d.i].no; std::string all; for (; d.i < d.L.size(); ++d.i) if (d.L[d.i].indent >= 0) all += (all.empty() ? "" : " ") + strip(d.L[d.i].text);
        return d.scalar_value(all, 0, no);
    }
    Val v = d.node(d.L[d.i].indent);
    d.skip_blank();
    if (d.i < d.L.size()) throw YamlError("expected <block end>, but found something else (line " + std::to_string(d.L[d.i].no) + ")");
    return v;
}

// yaml.safe_dump(meta, sort_keys=False, allow_unicode=True, width=120) for the values notes hold
namespace yaml_detail {
inline bool plain_ok(const std::string& s) {
    if (s.empty()) return false;
    if (strip(s) != s) return false;
    const Val r = [&]() { try { return resolve_plain(s); } catch (const YamlError&) { return Val::str("\x01"); } }();
    if (!(r.t == Val::STR && !is_date(r) && r.s == s)) return false;
    static const std::string first_bad = "-?:,[]{}#&*!|>'\"%@`";
    if (first_bad.find(s[0]) != std::string::npos) {
        if (!((s[0] == '-' || s[0] == '?' || s[0] == ':') && s.size() > 1 && s[1] != ' ')) return false;
    }
    if (s.find(": ") != std::string::npos || s.find(" #") != std::string::npos || s.back() == ':') return false;
    for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return false;
    return true;
}
inline std::string dump_str(const std::string& s) {
    if (plain_ok(s)) return s;
    bool printable = true; for (unsigned char c : s) if ((c < 0x20 && c != '\n') || c == 0x7F) printable = false;
    if (printable && s.find('\n') == std::string::npos) return "'" + replace_all(s, "'", "''") + "'";
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t";
        else if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\x%02X", c); o += b; } else o += static_cast<char>(c);
    }
    return o + "\"";
}
inline std::string dump_scalar(const Val& v) {
    if (is_date(v)) return v.s;
    switch (v.t) {
        case Val::NONE: return "null"; case Val::BOOL: return v.b ? "true" : "false"; case Val::INT: return v.big() ? v.s : std::to_string(v.i);
        case Val::FLOAT: { if (std::isnan(v.f)) return ".nan"; if (std::isinf(v.f)) return v.f < 0 ? "-.inf" : ".inf"; std::string r = float_repr(v.f); if (r.find('.') == std::string::npos && r.find('e') == std::string::npos) r += ".0"; return r; }
        case Val::STR: return dump_str(v.s); default: return "";
    }
}
inline void dump_node(std::string& o, const Val& v, int indent);
inline void dump_map(std::string& o, const Val& m, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    for (const auto& [k, v] : m.d->items) {
        o += pad + dump_str(k) + ":";
        if (v.t == Val::LIST && !v.l->empty()) { o += "\n"; for (const auto& x : *v.l) { o += pad + "-"; if (x.t == Val::DICT && !x.d->items.empty()) { std::string sub; dump_map(sub, x, indent + 2); o += " " + sub.substr(static_cast<std::size_t>(indent + 2)); } else { o += " " + (x.t == Val::LIST ? std::string("[]") : dump_scalar(x)) + "\n"; } } }
        else if (v.t == Val::DICT && !v.d->items.empty()) { o += "\n"; dump_map(o, v, indent + 2); }
        else if (v.t == Val::LIST) o += " []\n";
        else if (v.t == Val::DICT) o += " {}\n";
        else o += " " + dump_scalar(v) + "\n";
    }
}
}  // namespace yaml_detail
inline std::string yaml_dump(const Val& m) { std::string o; yaml_detail::dump_map(o, m, 0); return o; }

// ------------------------------------------------------------------ HTTP (through the engine's client)
inline HttpResp http_get_um(const std::string& url, const std::vector<std::string>& headers, long timeout_s = 60) {
    HttpFn f = http_hook();
    if (!f) { HttpResp r; r.error = "this build of A.S.T.R.O AI has no network client for pack tools"; return r; }
    return f(url, headers, timeout_s, nullptr);
}
}  // namespace packtools::modder
