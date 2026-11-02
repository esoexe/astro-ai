// console_common.inc - helpers shared by the console pack's tools (Jake Schincariol's console-skill, MIT): printf-style
// formatting, os.path.abspath, little-endian readers, latin-1 text, and the "bad input -> exit 2" error the tools raise.
#include <array>
#include <cstdarg>
#include <filesystem>

namespace packtools::console_common {
using namespace pyrt;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline std::string cfmt(const char* fmt, ...) {   // "%s" % (...) for the C-compatible conversions the tools use
    va_list ap; va_start(ap, fmt);
    va_list ap2; va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string o(n > 0 ? static_cast<std::size_t>(n) : 0, '\0');
    if (n > 0) std::vsnprintf(o.data(), o.size() + 1, fmt, ap2);
    va_end(ap2);
    return o;
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

// struct.unpack_from("<H"/"<I", data, off): the callers check the bounds first, as the originals do
inline uint32_t u16le(const std::string& d, std::size_t o) {
    return static_cast<uint32_t>(static_cast<unsigned char>(d[o])) | static_cast<uint32_t>(static_cast<unsigned char>(d[o + 1])) << 8;
}
inline uint32_t u32le(const std::string& d, std::size_t o) {
    return u16le(d, o) | u16le(d, o + 2) << 16;
}

inline std::string latin1(const std::string& bytes) {   // bytes.decode("latin-1"), as UTF-8
    std::string o; o.reserve(bytes.size());
    for (const char ch : bytes) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80) { o += static_cast<char>(c); }
        else { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return o;
}

// The originals' InputError: a one-line message on stderr as "<prog>: <message>" and exit 2.
struct InputError : std::runtime_error { using std::runtime_error::runtime_error; };

// e.strerror for a path that cannot be read (POSIX strerror text; Windows's own message there, as Python shows it)
inline std::string path_error(const std::string& path) {
    std::error_code ec; (void)std::filesystem::status(fspath(path), ec);
    if (!ec) return "No such file or directory";
    std::string m = ec.message();
    while (!m.empty() && (m.back() == '\n' || m.back() == '\r' || m.back() == '.')) m.pop_back();
    return m;
}

// OSError.strerror for an errno value: errno_text without its "[Errno N] " prefix
inline std::string strerror_of(int e) { const std::string t = errno_text(e); const auto k = t.find("] "); return k == std::string::npos ? t : t.substr(k + 2); }

template <class F> inline int guard(const std::string& prog, F&& body) {
    try {
        return body();
    } catch (const InputError& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "%s: %s\n", prog.c_str(), e.what());
        return 2;
    }
}

inline std::string u8(const std::filesystem::path& p) { const auto x = p.u8string(); return std::string(reinterpret_cast<const char*>(x.data()), x.size()); }
// os.replace, os.path.realpath (non-strict), os.path.lexists, os.listdir (directory order, like readdir)
inline void replace_file(const std::string& from, const std::string& to) {
    std::error_code ec; std::filesystem::rename(fspath(from), fspath(to), ec);
    if (ec) throw OSError(ec.value(), to);
}
inline std::string realpath(const std::string& p) {
    std::error_code ec; const auto r = std::filesystem::weakly_canonical(std::filesystem::absolute(fspath(p), ec), ec);
    if (ec) return abspath(p);
    std::string s = u8(r.lexically_normal());
    while (s.size() > 1 && (s.back() == 0x2F || s.back() == 0x5C) && !(s.size() == 3 && s[1] == 0x3A)) s.pop_back();
    return s;
}
inline bool lexists(const std::string& p) { std::error_code ec; return std::filesystem::exists(std::filesystem::symlink_status(fspath(p), ec)); }
inline std::vector<std::string> listdir(const std::string& d) {
    std::vector<std::string> v; std::error_code ec;
    for (std::filesystem::directory_iterator it(fspath(d), ec), end; !ec && it != end; it.increment(ec)) {
        v.push_back(u8(it->path().filename()));
    }
    if (ec && v.empty()) throw OSError(ec.value(), d);
    return v;
}
inline std::string ossep() {
#if defined(_WIN32)
    return "\\";
#else
    return "/";
#endif
}
// int() of re's \d+ text: any Unicode decimal digits (each Nd run is ten code points, 0 to 9)
inline long long dec_int(const std::string& digits) {
    long long v = 0;
    for (const auto& ch : cps(digits)) { uint32_t c = cp_of(ch), k = 0; while (k < c && uprop::decimal(c - k - 1)) ++k; v = v * 10 + static_cast<long long>(k % 10); }
    return v;
}
// int(text, 16): optional sign, optional 0x, single underscores between digits; past 63 bits it saturates
inline std::optional<long long> int16(const std::string& s0) {
    std::string s = strip(s0); bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { neg = s[0] == '-'; s = s.substr(1); }
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s = s.substr(2); if (!s.empty() && s[0] == '_') s = s.substr(1); }
    if (s.empty()) return std::nullopt;
    unsigned long long v = 0; bool prev_digit = false, big = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '_') { if (!prev_digit || i + 1 >= s.size()) return std::nullopt; prev_digit = false; continue; }
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
        if (v > (static_cast<unsigned long long>(LLONG_MAX) >> 4)) big = true;
        v = v * 16 + static_cast<unsigned long long>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
        prev_digit = true;
    }
    if (big || v > static_cast<unsigned long long>(LLONG_MAX)) return neg ? LLONG_MIN : LLONG_MAX;
    const long long r = static_cast<long long>(v);
    return neg ? -r : r;
}
// "%08x" % v for any int (Python pads after the sign)
inline std::string hex08(long long v) { const unsigned long long m = v < 0 ? 0ULL - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v); std::string h = cfmt("%llx", m); while (h.size() + (v < 0 ? 1 : 0) < 8) h = "0" + h; return (v < 0 ? "-" : "") + h; }
// a text file as Python reads it with encoding="utf-8", errors="replace" (newline="" keeps \r; otherwise universal newlines)
inline std::string text_u(const std::string& raw, bool keep_cr = false) {
    const std::string s = decode_utf8_replace(raw); if (keep_cr) return s;
    std::string o; o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) { if (s[i] == '\r') { o += '\n'; if (i + 1 < s.size() && s[i + 1] == '\n') ++i; } else o += s[i]; }
    return o;
}
// the lines of a text, each with its "\n" (for line in file)
inline std::vector<std::string> lines_keep(const std::string& t) {
    std::vector<std::string> v; std::size_t a = 0;
    while (a < t.size()) { const std::size_t nl = t.find('\n', a); if (nl == std::string::npos) { v.push_back(t.substr(a)); break; } v.push_back(t.substr(a, nl - a + 1)); a = nl + 1; }
    return v;
}
// a file the skill ships (templates/...): the Astro library keeps code files as <name>.txt examples, so try both
inline std::string skill_data(const std::string& rel) {
    const std::string p = skill_file(rel);
    if (is_file(p)) return p;
    if (is_file(p + ".txt")) return p + ".txt";
    return p;
}
inline void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); std::fputc('\n', stdout); }
inline std::string dumps2(const Val& v) { DumpOpts o; o.indent = 2; return dumps(v, o); }
inline Val dict_of(std::initializer_list<std::pair<std::string, Val>> kv) { Val d = Val::dict(); for (const auto& [k, v] : kv) d.d->set(k, v); return d; }
inline Val str_list(const std::vector<std::string>& xs) { List l; for (const auto& x : xs) l.push_back(Val::str(x)); return Val::list(std::move(l)); }
}  // namespace packtools::console_common
