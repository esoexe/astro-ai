// pyrt.h - A.S.T.R.O AI pack tools runtime (C++20, standard library only).
// The pack helpers (ported from Jake Schincariol's MIT-licensed skills) must print exactly what the originals
// printed, so this header reproduces the few runtime behaviours they depend on: ordered JSON with int/float kept
// apart, json.loads error messages and positions, json.dumps formatting, repr()/str(), '%.nf' / '%e' / '%g' and
// round() on the exact binary value (round-half-even), compensated float sums, csv parsing and argparse-style
// command lines (same errors, exit code 2). Original A.S.T.R.O AI code, MIT.
#pragma once
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>

#include "unicode_case.h"
namespace pyrt {

// ------------------------------------------------------------------ errors
struct PyErr : std::runtime_error { std::string type; PyErr(std::string t, const std::string& m) : std::runtime_error(m), type(std::move(t)) {} };
struct Exit { int code; };   // like SystemExit

// ------------------------------------------------------------------ values (Python's json types; dicts keep insertion order)
struct Val; using List = std::vector<Val>;
struct Dict;
struct Val {
    enum T { NONE, BOOL, INT, FLOAT, STR, LIST, DICT } t = NONE;
    bool b = false; long long i = 0; double f = 0; std::string s;   // s also holds the digits of an int too big for long long
    std::shared_ptr<List> l; std::shared_ptr<Dict> d;
    Val() = default;
    static Val none() { return Val(); }
    static Val boolean(bool v) { Val x; x.t = BOOL; x.b = v; return x; }
    static Val integer(long long v) { Val x; x.t = INT; x.i = v; return x; }
    static Val flt(double v) { Val x; x.t = FLOAT; x.f = v; return x; }
    static Val str(std::string v) { Val x; x.t = STR; x.s = std::move(v); return x; }
    static Val list(List v = {}) { Val x; x.t = LIST; x.l = std::make_shared<List>(std::move(v)); return x; }
    static Val dict();
    bool is_none() const { return t == NONE; }
    bool is_num() const { return t == INT || t == FLOAT; }            // int or float, not bool
    bool is_real() const { return t == INT || t == FLOAT || t == BOOL; }  // isinstance(v, (int, float)) in Python (bool is an int)
    bool big() const { return t == INT && !s.empty(); }
    double num() const { return t == FLOAT ? f : t == BOOL ? (b ? 1.0 : 0.0) : big() ? std::strtod(s.c_str(), nullptr) : static_cast<double>(i); }
    Val& operator[](std::size_t k) { return (*l)[k]; }
    const Val& operator[](std::size_t k) const { return (*l)[k]; }
    std::size_t size() const;
};
struct Dict {
    std::vector<std::pair<std::string, Val>> items;
    const Val* get(const std::string& k) const { for (const auto& [a, v] : items) if (a == k) return &v; return nullptr; }
    Val* get(const std::string& k) { for (auto& [a, v] : items) if (a == k) return &v; return nullptr; }
    bool has(const std::string& k) const { return get(k) != nullptr; }
    void set(const std::string& k, Val v) { if (Val* p = get(k)) *p = std::move(v); else items.emplace_back(k, std::move(v)); }
    Val at(const std::string& k, Val dflt = Val()) const { const Val* p = get(k); return p ? *p : dflt; }   // d.get(k, dflt)
};
inline Val Val::dict() { Val x; x.t = DICT; x.d = std::make_shared<Dict>(); return x; }
inline std::size_t Val::size() const { return t == LIST ? l->size() : t == DICT ? d->items.size() : 0; }
inline Val get(const Val& o, const std::string& k, Val dflt = Val()) { return o.t == Val::DICT ? o.d->at(k, std::move(dflt)) : dflt; }
inline bool has(const Val& o, const std::string& k) { return o.t == Val::DICT && o.d->has(k); }

// ------------------------------------------------------------------ UTF-8 / code points
inline std::size_t cplen(const std::string& s) { std::size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; }
inline std::vector<std::string> cps(const std::string& s) {   // split into code points
    std::vector<std::string> o; for (std::size_t k = 0; k < s.size();) { std::size_t j = k + 1; while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) j++; o.push_back(s.substr(k, j - k)); k = j; } return o;
}
inline std::string cpslice(const std::string& s, long a, long b = 1L << 30) {   // s[a:b] in code points (negative indices like Python)
    auto v = cps(s); const long n = static_cast<long>(v.size());
    if (a < 0) a = std::max(0L, n + a); if (b < 0) b = std::max(0L, n + b); a = std::min(a, n); b = std::min(b, n);
    std::string o; for (long k = a; k < b; ++k) o += v[static_cast<std::size_t>(k)]; return o;
}
inline uint32_t cp_of(const std::string& c) {
    const auto* u = reinterpret_cast<const unsigned char*>(c.data()); const std::size_t n = c.size();
    if (n == 1) return u[0]; if (n == 2) return ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
    if (n == 3) return ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
    return ((u[0] & 0x07u) << 18) | ((u[1] & 0x3Fu) << 12) | ((u[2] & 0x3Fu) << 6) | (u[3] & 0x3Fu);
}
inline std::string utf8(uint32_t c) {   // also encodes lone surrogates (WTF-8), as Python strs can hold them
    std::string o;
    if (c < 0x80) o += static_cast<char>(c);
    else if (c < 0x800) { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { o += static_cast<char>(0xE0 | (c >> 12)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    else { o += static_cast<char>(0xF0 | (c >> 18)); o += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    return o;
}
inline bool py_space(uint32_t c) { return (c >= 9 && c <= 13) || c == 32 || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000; }
inline std::string strip(const std::string& s) {   // str.strip()
    auto v = cps(s); std::size_t a = 0, b = v.size();
    while (a < b && py_space(cp_of(v[a]))) a++; while (b > a && py_space(cp_of(v[b - 1]))) b--;
    std::string o; for (std::size_t k = a; k < b; ++k) o += v[k]; return o;
}
inline std::vector<std::string> split_ws(const std::string& s) {   // str.split()
    std::vector<std::string> o; std::string cur; bool in = false;
    for (const auto& c : cps(s)) { if (py_space(cp_of(c))) { if (in) { o.push_back(cur); cur.clear(); in = false; } } else { cur += c; in = true; } }
    if (in) o.push_back(cur); return o;
}
inline std::vector<std::string> split(const std::string& s, const std::string& sep, int maxsplit = -1) {   // str.split(sep)
    std::vector<std::string> o; std::size_t a = 0;
    for (;;) { if (maxsplit >= 0 && static_cast<int>(o.size()) == maxsplit) break; const std::size_t k = s.find(sep, a); if (k == std::string::npos) break; o.push_back(s.substr(a, k - a)); a = k + sep.size(); }
    o.push_back(s.substr(a)); return o;
}
inline std::string join(const std::vector<std::string>& v, const std::string& sep) { std::string o; for (std::size_t k = 0; k < v.size(); ++k) { if (k) o += sep; o += v[k]; } return o; }
// str.lower() / str.upper() / str.title(): full Unicode case mapping, as CPython does it (unicode_case.h, UCD 15.1.0):
// SpecialCasing's unconditional entries, else the simple mappings; capital sigma becomes final sigma at the end of a word.
namespace ucase_detail {
inline const ucase::Simple* simple(uint32_t c) {
    const auto* b = std::begin(ucase::SIMPLE); const auto* e = std::end(ucase::SIMPLE);
    const auto* it = std::lower_bound(b, e, c, [](const ucase::Simple& m, uint32_t x) { return m.cp < x; });
    return it != e && it->cp == c ? it : nullptr;
}
inline const ucase::Special* special(uint32_t c) {
    const auto* b = std::begin(ucase::SPECIAL); const auto* e = std::end(ucase::SPECIAL);
    const auto* it = std::lower_bound(b, e, c, [](const ucase::Special& m, uint32_t x) { return m.cp < x; });
    return it != e && it->cp == c ? it : nullptr;
}
template <std::size_t N> inline bool in_ranges(const ucase::Range (&r)[N], uint32_t c) {
    const auto* it = std::upper_bound(std::begin(r), std::end(r), c, [](uint32_t x, const ucase::Range& g) { return x < g.lo; });
    return it != std::begin(r) && c <= (it - 1)->hi;
}
inline void put(std::string& o, const uint32_t (&v)[3]) { for (uint32_t x : v) if (x) o += utf8(x); }
enum Which { LO, UP, TI };
inline void map_full(std::string& o, uint32_t c, Which w) {
    if (const auto* s = special(c)) { put(o, w == LO ? s->lo : w == UP ? s->up : s->ti); return; }
    const auto* m = simple(c);
    o += utf8(m ? (w == LO ? m->lo : w == UP ? m->up : m->ti) : c);
}
inline bool lower_at(std::string& o, const std::vector<uint32_t>& v, std::size_t i) {   // lower_ucs4, with handle_capital_sigma
    if (v[i] != 0x3A3) { map_full(o, v[i], LO); return true; }
    long j = static_cast<long>(i) - 1;
    while (j >= 0 && in_ranges(ucase::CASE_IGNORABLE, v[static_cast<std::size_t>(j)])) j--;
    bool fin = j >= 0 && in_ranges(ucase::CASED, v[static_cast<std::size_t>(j)]);
    if (fin) {
        std::size_t k = i + 1;
        while (k < v.size() && in_ranges(ucase::CASE_IGNORABLE, v[k])) k++;
        fin = k == v.size() || !in_ranges(ucase::CASED, v[k]);
    }
    o += utf8(fin ? 0x3C2 : 0x3C3);
    return true;
}
inline std::vector<uint32_t> u32(const std::string& s) { std::vector<uint32_t> v; for (const auto& ch : cps(s)) v.push_back(cp_of(ch)); return v; }
}  // namespace ucase_detail
inline bool is_cased(uint32_t c) { return ucase_detail::in_ranges(ucase::CASED, c); }
inline bool isdigit_str(const std::string& s) {   // str.isdigit(): non-empty, every code point has a digit value (², ¹, ٣, ...)
    if (s.empty()) return false;
    for (uint32_t c : ucase_detail::u32(s)) if (!ucase_detail::in_ranges(ucase::DIGIT, c)) return false;
    return true;
}
inline std::string upper(const std::string& s) {
    std::string o; for (uint32_t c : ucase_detail::u32(s)) ucase_detail::map_full(o, c, ucase_detail::UP); return o;
}
inline std::string lower(const std::string& s) {
    std::string o; const auto v = ucase_detail::u32(s);
    for (std::size_t i = 0; i < v.size(); ++i) ucase_detail::lower_at(o, v, i);
    return o;
}
inline std::string title(const std::string& s) {   // str.title(): words are runs of cased characters
    std::string o; const auto v = ucase_detail::u32(s); bool prev = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (prev) ucase_detail::lower_at(o, v, i);
        else ucase_detail::map_full(o, v[i], ucase_detail::TI);
        prev = is_cased(v[i]);
    }
    return o;
}
inline std::string ljust(const std::string& s, std::size_t w) { const std::size_t n = cplen(s); return n >= w ? s : s + std::string(w - n, ' '); }
inline std::string rjust(const std::string& s, std::size_t w) { const std::size_t n = cplen(s); return n >= w ? s : std::string(w - n, ' ') + s; }
inline int cpcmp(const std::string& a, const std::string& b) { return a < b ? -1 : a > b ? 1 : 0; }   // UTF-8 byte order == code point order
inline bool starts(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }
inline bool ends(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
inline std::string replace_all(std::string s, const std::string& a, const std::string& b) { if (a.empty()) return s; std::size_t k = 0; while ((k = s.find(a, k)) != std::string::npos) { s.replace(k, a.size(), b); k += b.size(); } return s; }

// ------------------------------------------------------------------ exact decimal expansion of a double
// |x| = D / 10^k exactly (D a decimal digit string). Every float is a finite decimal, so '%f', '%e', '%g' and
// round() can all be done by rounding this string half-to-even, which is what CPython does.
namespace detail {
struct Big { std::vector<uint32_t> d; };   // base 1e9, little endian
inline void mul(Big& b, uint64_t m) { uint64_t carry = 0; for (auto& x : b.d) { const uint64_t v = static_cast<uint64_t>(x) * m + carry; x = static_cast<uint32_t>(v % 1000000000ULL); carry = v / 1000000000ULL; } while (carry) { b.d.push_back(static_cast<uint32_t>(carry % 1000000000ULL)); carry /= 1000000000ULL; } }
inline std::string dec(const Big& b) { if (b.d.empty()) return "0"; std::string o = std::to_string(b.d.back()); char buf[16]; for (std::size_t k = b.d.size() - 1; k-- > 0;) { std::snprintf(buf, sizeof buf, "%09u", b.d[k]); o += buf; } return o; }
}
inline std::pair<std::string, int> exact_decimal(double x) {
    x = std::fabs(x); if (x == 0) return {"0", 0};
    int e2 = 0; const double fr = std::frexp(x, &e2);              // x = fr * 2^e2, fr in [0.5, 1)
    uint64_t m = static_cast<uint64_t>(std::ldexp(fr, 53)); int e = e2 - 53;
    while (m && !(m & 1)) { m >>= 1; e++; }
    detail::Big b; b.d.push_back(static_cast<uint32_t>(m % 1000000000ULL)); if (m >= 1000000000ULL) { b.d.push_back(static_cast<uint32_t>((m / 1000000000ULL) % 1000000000ULL)); if (m >= 1000000000000000000ULL) b.d.push_back(static_cast<uint32_t>(m / 1000000000000000000ULL)); }
    while (!b.d.empty() && b.d.back() == 0) b.d.pop_back();
    int k = 0;
    if (e >= 0) { int r = e; while (r >= 30) { detail::mul(b, 1ULL << 30); r -= 30; } if (r) detail::mul(b, 1ULL << r); }
    else { int r = -e; k = r; while (r >= 13) { detail::mul(b, 1220703125ULL); r -= 13; } uint64_t p = 1; while (r--) p *= 5; if (p > 1) detail::mul(b, p); }
    return {detail::dec(b), k};
}
// round digit string `digs` (an integer) by dropping its last `drop` digits, half-to-even
inline std::string round_digits(const std::string& digs, std::size_t drop) {
    if (drop == 0) return digs;
    if (drop > digs.size()) return "0";
    std::string keep = digs.substr(0, digs.size() - drop); const std::string rest = digs.substr(digs.size() - drop);
    bool up = false;
    if (rest[0] > '5') up = true;
    else if (rest[0] == '5') { const bool exact_half = rest.find_first_not_of('0', 1) == std::string::npos; up = !exact_half || (!keep.empty() && ((keep.back() - '0') & 1)); }
    if (keep.empty()) keep = "0";
    if (up) { int k = static_cast<int>(keep.size()) - 1; while (k >= 0 && keep[static_cast<std::size_t>(k)] == '9') { keep[static_cast<std::size_t>(k)] = '0'; --k; } if (k < 0) keep.insert(keep.begin(), '1'); else keep[static_cast<std::size_t>(k)]++; }
    return keep;
}
inline std::string fmt_f(double x, int nd = 6) {   // '%.<nd>f' % x
    if (std::isnan(x)) return "nan"; if (std::isinf(x)) return x < 0 ? "-inf" : "inf";
    auto [D, k] = exact_decimal(x);
    std::string q = static_cast<int>(nd) >= k ? D + std::string(static_cast<std::size_t>(nd - k), '0') : round_digits(D, static_cast<std::size_t>(k - nd));
    if (q.size() < static_cast<std::size_t>(nd) + 1) q.insert(0, static_cast<std::size_t>(nd) + 1 - q.size(), '0');
    if (nd) q.insert(q.size() - static_cast<std::size_t>(nd), ".");
    return (std::signbit(x) ? "-" : "") + q;
}
inline std::pair<std::string, int> sig_digits(double x, int n) {   // n significant digits (half-even) and the decimal exponent
    auto [D, k] = exact_decimal(x);
    const std::size_t nz = D.find_first_not_of('0'); const std::string Dn = D.substr(nz);
    int e = static_cast<int>(Dn.size()) - 1 - k;
    std::string q = static_cast<int>(Dn.size()) <= n ? Dn + std::string(static_cast<std::size_t>(n) - Dn.size(), '0') : round_digits(Dn, Dn.size() - static_cast<std::size_t>(n));
    if (static_cast<int>(q.size()) > n) { q.pop_back(); e++; }
    return {q, e};
}
inline std::string fmt_e(double x, int prec = 6) {   // '%.<prec>e' % x
    if (std::isnan(x)) return "nan"; if (std::isinf(x)) return x < 0 ? "-inf" : "inf";
    std::string ds; int e = 0;
    if (x == 0) ds = std::string(static_cast<std::size_t>(prec) + 1, '0'); else std::tie(ds, e) = sig_digits(x, prec + 1);
    char eb[16]; std::snprintf(eb, sizeof eb, "e%c%02d", e < 0 ? '-' : '+', std::abs(e));
    return (std::signbit(x) ? "-" : "") + ds.substr(0, 1) + (prec ? "." + ds.substr(1) : "") + eb;
}
inline std::string fmt_g(double x, int p = 6, bool alt = false) {   // '%.<p>g' % x
    if (std::isnan(x)) return "nan"; if (std::isinf(x)) return x < 0 ? "-inf" : "inf";
    if (p == 0) p = 1;
    const int e = x == 0 ? 0 : sig_digits(x, p).second;
    std::string s = (e < -4 || e >= p) ? fmt_e(x, p - 1) : fmt_f(x, p - 1 - e);
    if (!alt) {
        const std::size_t ep = s.find('e'); std::string m = s.substr(0, ep), tail = ep == std::string::npos ? "" : s.substr(ep);
        if (m.find('.') != std::string::npos) { while (m.back() == '0') m.pop_back(); if (m.back() == '.') m.pop_back(); }
        s = m + tail;
    }
    return s;
}
inline std::string float_repr(double x) {   // repr(float): shortest digits that read back as x
    if (std::isnan(x)) return "nan"; if (std::isinf(x)) return x < 0 ? "-inf" : "inf";
    if (x == 0) return std::signbit(x) ? "-0.0" : "0.0";
    std::string ds; int e = 0;
    for (int p = 1; p <= 17; ++p) { std::tie(ds, e) = sig_digits(x, p); const std::string t = ds.substr(0, 1) + "." + ds.substr(1) + "e" + std::to_string(e); if (std::strtod(t.c_str(), nullptr) == std::fabs(x)) break; }
    while (ds.size() > 1 && ds.back() == '0') ds.pop_back();
    const std::string sign = x < 0 ? "-" : "";
    if (e < -4 || e >= 16) { char eb[16]; std::snprintf(eb, sizeof eb, "e%c%02d", e < 0 ? '-' : '+', std::abs(e)); return sign + ds.substr(0, 1) + (ds.size() > 1 ? "." + ds.substr(1) : "") + eb; }
    if (e < 0) return sign + "0." + std::string(static_cast<std::size_t>(-e - 1), '0') + ds;
    if (static_cast<int>(ds.size()) <= e + 1) return sign + ds + std::string(static_cast<std::size_t>(e + 1) - ds.size(), '0') + ".0";
    return sign + ds.substr(0, static_cast<std::size_t>(e) + 1) + "." + ds.substr(static_cast<std::size_t>(e) + 1);
}
inline double round_n(double x, int nd) { if (!std::isfinite(x)) return x; return std::strtod(fmt_f(x, nd).c_str(), nullptr); }   // round(x, nd)
inline long long round_int(double x) { const double fl = std::floor(x), d = x - fl; return static_cast<long long>(d > 0.5 || (d == 0.5 && std::fmod(fl, 2.0) != 0) ? fl + 1 : fl); }   // round(x)
inline std::string commas(const std::string& s) {   // insert thousands separators into a formatted number
    const bool neg = !s.empty() && s[0] == '-'; std::string body = neg ? s.substr(1) : s;
    const std::size_t dot = body.find('.'); std::string ip = body.substr(0, dot), fp = dot == std::string::npos ? "" : body.substr(dot);
    std::string o; for (std::size_t k = 0; k < ip.size(); ++k) { if (k && (ip.size() - k) % 3 == 0) o += ','; o += ip[k]; }
    return (neg ? "-" : "") + o + fp;
}
inline double fsum(const std::vector<double>& xs) {   // sum() of floats (CPython 3.12+ Neumaier compensation)
    double s = 0, c = 0;
    for (double x : xs) { const double t = s + x; if (std::fabs(s) >= std::fabs(x)) c += (s - t) + x; else c += (x - t) + s; s = t; }
    return (c != 0 && std::isfinite(c)) ? s + c : s;
}
}  // namespace pyrt

namespace pyrt {
// ------------------------------------------------------------------ json.loads (CPython's C scanner, strict mode)
struct JSONDecodeError : PyErr { std::string msg; std::size_t pos; JSONDecodeError(const std::string& m, std::size_t p, const std::string& full) : PyErr("json.decoder.JSONDecodeError", full), msg(m), pos(p) {} };
inline Val loads(const std::string& text) {
    const std::size_t n = text.size(); std::size_t i = 0;
    auto fail = [&](const std::string& msg, std::size_t pos) -> void {
        const std::string before = text.substr(0, pos); const std::size_t nl = before.rfind('\n');
        const std::size_t cpos = cplen(before); const std::size_t line = static_cast<std::size_t>(std::count(before.begin(), before.end(), '\n')) + 1;
        const std::size_t col = nl == std::string::npos ? cpos + 1 : cpos - cplen(text.substr(0, nl));
        throw JSONDecodeError(msg, cpos, msg + ": line " + std::to_string(line) + " column " + std::to_string(col) + " (char " + std::to_string(cpos) + ")");
    };
    if (n >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) fail("Unexpected UTF-8 BOM (decode using utf-8-sig)", 0);
    auto ws = [&](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    auto skip = [&] { while (i < n && ws(text[i])) i++; };
    auto hex4 = [&](std::size_t p) -> long { if (p + 4 > n) return -1; long v = 0; for (std::size_t k = p; k < p + 4; ++k) { const char c = text[k]; v <<= 4; if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return -1; } return v; };
    std::function<std::string(std::size_t)> str = [&](std::size_t q) -> std::string {
        std::string out; std::size_t k = q + 1;
        for (;;) {
            std::size_t j = k;
            while (j < n && text[j] != '"' && text[j] != '\\') { if (static_cast<unsigned char>(text[j]) <= 0x1f) fail("Invalid control character at", j); j++; }
            if (j >= n) fail("Unterminated string starting at", q);
            out.append(text, k, j - k);
            if (text[j] == '"') { i = j + 1; return out; }
            if (j + 1 == n) fail("Unterminated string starting at", q);
            const char e = text[j + 1];
            if (e != 'u') {
                char m = 0; switch (e) { case '"': m = '"'; break; case '\\': m = '\\'; break; case '/': m = '/'; break; case 'b': m = '\b'; break; case 'f': m = '\f'; break; case 'n': m = '\n'; break; case 'r': m = '\r'; break; case 't': m = '\t'; break; default: fail("Invalid \\escape", j); }
                out += m; k = j + 2; continue;
            }
            if (j + 6 >= n) fail("Invalid \\uXXXX escape", j + 1);
            long c = hex4(j + 2); if (c < 0) fail("Invalid \\uXXXX escape", j + 1);
            k = j + 6;
            if (c >= 0xD800 && c <= 0xDBFF && k + 6 < n && text[k] == '\\' && text[k + 1] == 'u') {
                const long c2 = hex4(k + 2); if (c2 < 0) fail("Invalid \\uXXXX escape", k + 1);
                if (c2 >= 0xDC00 && c2 <= 0xDFFF) { out += utf8(static_cast<uint32_t>(0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00))); k += 6; continue; }
            }
            out += utf8(static_cast<uint32_t>(c));
        }
    };
    std::function<Val()> value = [&]() -> Val {
        if (i >= n) fail("Expecting value", i);
        const char c = text[i];
        if (c == '"') return Val::str(str(i));
        if (c == '{') {
            Val o = Val::dict(); i++; skip();
            if (i < n && text[i] == '}') { i++; return o; }
            for (;;) {
                if (i >= n || text[i] != '"') fail("Expecting property name enclosed in double quotes", i);
                std::string key = str(i); skip();
                if (i >= n || text[i] != ':') fail("Expecting ':' delimiter", i);
                i++; skip(); o.d->set(key, value()); skip();
                if (i < n && text[i] == '}') { i++; return o; }
                if (i >= n || text[i] != ',') fail("Expecting ',' delimiter", i);
                const std::size_t comma = i; i++; skip();
                if (i < n && text[i] == '}') fail("Illegal trailing comma before end of object", comma);
            }
        }
        if (c == '[') {
            Val a = Val::list(); i++; skip();
            if (i < n && text[i] == ']') { i++; return a; }
            for (;;) {
                a.l->push_back(value()); skip();
                if (i < n && text[i] == ']') { i++; return a; }
                if (i >= n || text[i] != ',') fail("Expecting ',' delimiter", i);
                const std::size_t comma = i; i++; skip();
                if (i < n && text[i] == ']') fail("Illegal trailing comma before end of array", comma);
            }
        }
        auto lit = [&](const char* w) { const std::size_t L = std::strlen(w); if (text.compare(i, L, w) == 0) { i += L; return true; } return false; };
        if (lit("null")) return Val(); if (lit("true")) return Val::boolean(true); if (lit("false")) return Val::boolean(false);
        if (lit("NaN")) return Val::flt(NAN); if (lit("Infinity")) return Val::flt(INFINITY); if (lit("-Infinity")) return Val::flt(-INFINITY);
        std::size_t j = i; if (j < n && text[j] == '-') j++;
        if (j >= n) fail("Expecting value", i);
        if (text[j] == '0') j++; else if (text[j] >= '1' && text[j] <= '9') { while (j < n && std::isdigit(static_cast<unsigned char>(text[j]))) j++; } else fail("Expecting value", i);
        bool isf = false;
        if (j + 1 < n && text[j] == '.' && std::isdigit(static_cast<unsigned char>(text[j + 1]))) { isf = true; j += 2; while (j < n && std::isdigit(static_cast<unsigned char>(text[j]))) j++; }
        if (j + 1 < n && (text[j] == 'e' || text[j] == 'E')) { const std::size_t es = j; j++; if (j + 1 < n && (text[j] == '-' || text[j] == '+')) j++; const std::size_t d0 = j; while (j < n && std::isdigit(static_cast<unsigned char>(text[j]))) j++; if (j > d0) isf = true; else j = es; }
        const std::string num = text.substr(i, j - i); i = j;
        if (isf) return Val::flt(std::strtod(num.c_str(), nullptr));
        errno = 0; char* end = nullptr; const long long v = std::strtoll(num.c_str(), &end, 10);
        if (errno == ERANGE) { Val b; b.t = Val::INT; b.s = num; return b; }
        return Val::integer(v);
    };
    skip(); Val v = value(); skip();
    if (i != n) fail("Extra data", i);
    return v;
}

// ------------------------------------------------------------------ repr() / str() / bool()
inline bool printable(uint32_t c) {
    if (c < 0x20 || (c >= 0x7F && c <= 0xA0) || c == 0xAD) return false;
    if (c == 0x1680 || c == 0x180E || (c >= 0x2000 && c <= 0x200F) || (c >= 0x2028 && c <= 0x202F) || (c >= 0x205F && c <= 0x206F) || c == 0x3000 || c == 0xFEFF) return false;
    if ((c >= 0xD800 && c <= 0xF8FF) || (c >= 0xFFF9 && c <= 0xFFFB) || c == 0xFFFE || c == 0xFFFF || c >= 0xF0000 || (c >= 0xE0000 && c <= 0xE0FFF)) return false;
    return true;
}
inline std::string str_repr(const std::string& s) {
    const char q = (s.find('\'') != std::string::npos && s.find('"') == std::string::npos) ? '"' : '\'';
    std::string o(1, q);
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c == static_cast<uint32_t>(q) || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\n') o += "\\n"; else if (c == '\r') o += "\\r"; else if (c == '\t') o += "\\t";
        else if (c != ' ' && !printable(c)) { char b[16]; if (c < 0x100) std::snprintf(b, sizeof b, "\\x%02x", c); else if (c < 0x10000) std::snprintf(b, sizeof b, "\\u%04x", c); else std::snprintf(b, sizeof b, "\\U%08x", c); o += b; }
        else o += ch;
    }
    return o + q;
}
std::string repr(const Val& v);
inline std::string pystr(const Val& v) {   // str(v)
    switch (v.t) {
        case Val::NONE: return "None"; case Val::BOOL: return v.b ? "True" : "False";
        case Val::INT: return v.big() ? v.s : std::to_string(v.i); case Val::FLOAT: return float_repr(v.f);
        case Val::STR: return v.s; default: return repr(v);
    }
}
inline std::string repr(const Val& v) {
    if (v.t == Val::STR) return str_repr(v.s);
    if (v.t == Val::LIST) { std::string o = "["; for (std::size_t k = 0; k < v.l->size(); ++k) { if (k) o += ", "; o += repr((*v.l)[k]); } return o + "]"; }
    if (v.t == Val::DICT) { std::string o = "{"; bool first = true; for (const auto& [k, x] : v.d->items) { if (!first) o += ", "; first = false; o += str_repr(k) + ": " + repr(x); } return o + "}"; }
    return pystr(v);
}
inline bool truthy(const Val& v) {
    switch (v.t) { case Val::NONE: return false; case Val::BOOL: return v.b; case Val::INT: return v.big() || v.i != 0; case Val::FLOAT: return v.f != 0; case Val::STR: return !v.s.empty(); default: return v.size() > 0; }
}
inline const char* type_name(const Val& v) { switch (v.t) { case Val::NONE: return "NoneType"; case Val::BOOL: return "bool"; case Val::INT: return "int"; case Val::FLOAT: return "float"; case Val::STR: return "str"; case Val::LIST: return "list"; default: return "dict"; } }

// float() and int() of a value, Python's rules (ASCII digits; whitespace, sign, underscores, inf/nan)
inline std::optional<double> parse_float(const std::string& s0) {
    std::string s = lower(strip(s0)); if (s.empty()) return std::nullopt;
    std::size_t k = 0; bool neg = false; if (s[0] == '+' || s[0] == '-') { neg = s[0] == '-'; k = 1; }
    const std::string body = s.substr(k);
    if (body == "inf" || body == "infinity") return neg ? -INFINITY : INFINITY;
    if (body == "nan") return NAN;
    std::string clean; bool digit_before = false, seen_e = false, seen_dot = false, any_digit = false;
    for (std::size_t j = 0; j < body.size(); ++j) {
        const char c = body[j];
        if (std::isdigit(static_cast<unsigned char>(c))) { clean += c; digit_before = true; any_digit = true; continue; }
        if (c == '_') { if (!digit_before || j + 1 >= body.size() || !std::isdigit(static_cast<unsigned char>(body[j + 1]))) return std::nullopt; continue; }
        digit_before = false;
        if (c == '.' && !seen_dot && !seen_e) { seen_dot = true; clean += c; continue; }
        if (c == 'e' && !seen_e && any_digit) { seen_e = true; clean += c; if (j + 1 < body.size() && (body[j + 1] == '+' || body[j + 1] == '-')) { clean += body[++j]; } if (j + 1 >= body.size()) return std::nullopt; continue; }
        return std::nullopt;
    }
    if (!any_digit || clean == "." || clean.back() == 'e' || clean.back() == '+' || clean.back() == '-') return std::nullopt;
    { const std::size_t ep = clean.find('e'); const std::string mant = clean.substr(0, ep); if (mant.find_first_of("0123456789") == std::string::npos) return std::nullopt; }
    const double v = std::strtod(clean.c_str(), nullptr); return neg ? -v : v;
}
inline double to_float(const Val& v) {   // float(v); raises like Python
    if (v.t == Val::FLOAT || v.t == Val::INT || v.t == Val::BOOL) return v.num();
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("float() argument must be a string or a real number, not '") + type_name(v) + "'");
    const auto r = parse_float(v.s); if (!r) throw PyErr("ValueError", "could not convert string to float: " + str_repr(v.s));
    return *r;
}
inline std::optional<long long> parse_int(const std::string& s0) {
    const std::string s = strip(s0); if (s.empty()) return std::nullopt;
    std::size_t k = 0; bool neg = false; if (s[0] == '+' || s[0] == '-') { neg = s[0] == '-'; k = 1; }
    std::string d; bool prev_digit = false;
    for (std::size_t j = k; j < s.size(); ++j) { const char c = s[j]; if (std::isdigit(static_cast<unsigned char>(c))) { d += c; prev_digit = true; } else if (c == '_' && prev_digit && j + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[j + 1]))) prev_digit = false; else return std::nullopt; }
    if (d.empty()) return std::nullopt;
    errno = 0; const long long v = std::strtoll(d.c_str(), nullptr, 10); if (errno == ERANGE) return std::nullopt;
    return neg ? -v : v;
}

// ------------------------------------------------------------------ json.dumps
struct DumpOpts { int indent = -1; bool sort_keys = false; bool ensure_ascii = true; bool compact = false; };   // compact = separators=(",", ":")
inline std::string jstr(const std::string& s, bool ascii) {
    std::string o = "\"";
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch); char b[16];
        switch (c) {
            case '"': o += "\\\""; continue; case '\\': o += "\\\\"; continue; case '\n': o += "\\n"; continue; case '\r': o += "\\r"; continue;
            case '\t': o += "\\t"; continue; case '\b': o += "\\b"; continue; case '\f': o += "\\f"; continue; default: break;
        }
        if (c < 0x20 || (ascii && c >= 0x7F)) {
            if (c >= 0x10000) { const uint32_t v = c - 0x10000; std::snprintf(b, sizeof b, "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF)); }
            else std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += ch;
    }
    return o + "\"";
}
inline std::string jnum(const Val& v) {
    if (v.t == Val::FLOAT) { if (std::isnan(v.f)) return "NaN"; if (std::isinf(v.f)) return v.f < 0 ? "-Infinity" : "Infinity"; return float_repr(v.f); }
    return v.big() ? v.s : std::to_string(v.i);
}
inline std::string dumps(const Val& v, const DumpOpts& o = {}, int level = 0) {
    switch (v.t) {
        case Val::NONE: return "null"; case Val::BOOL: return v.b ? "true" : "false";
        case Val::INT: case Val::FLOAT: return jnum(v); case Val::STR: return jstr(v.s, o.ensure_ascii); default: break;
    }
    const bool ind = o.indent >= 0;
    const std::string pad = ind ? "\n" + std::string(static_cast<std::size_t>(o.indent * (level + 1)), ' ') : "", end = ind ? "\n" + std::string(static_cast<std::size_t>(o.indent * level), ' ') : "";
    const std::string sep = (ind || o.compact) ? "," : ", ", ksep = o.compact ? ":" : ": ";
    if (v.t == Val::LIST) {
        if (v.l->empty()) return "[]";
        std::string s = "["; for (std::size_t k = 0; k < v.l->size(); ++k) { if (k) s += sep; s += pad + dumps((*v.l)[k], o, level + 1); } return s + end + "]";
    }
    if (v.d->items.empty()) return "{}";
    std::vector<const std::pair<std::string, Val>*> its; for (const auto& it : v.d->items) its.push_back(&it);
    if (o.sort_keys) std::stable_sort(its.begin(), its.end(), [](auto* a, auto* b) { return a->first < b->first; });
    std::string s = "{"; bool first = true;
    for (const auto* it : its) { if (!first) s += sep; first = false; s += pad + jstr(it->first, o.ensure_ascii) + ksep + dumps(it->second, o, level + 1); }
    return s + end + "}";
}
}  // namespace pyrt

#include <deque>
#include <list>
#include <regex>
#include <fstream>
#include <filesystem>
namespace pyrt {
// ------------------------------------------------------------------ files, with Python's OSError wording
inline std::string errno_text(int e) {
    switch (e) { case ENOENT: return "[Errno 2] No such file or directory"; case EACCES: return "[Errno 13] Permission denied"; case EISDIR: return "[Errno 21] Is a directory";
        case ENOTDIR: return "[Errno 20] Not a directory"; case EEXIST: return "[Errno 17] File exists"; case ENOSPC: return "[Errno 28] No space left on device";
        case EROFS: return "[Errno 30] Read-only file system"; case ENAMETOOLONG: return "[Errno 36] File name too long"; case EINVAL: return "[Errno 22] Invalid argument";
        default: return "[Errno " + std::to_string(e) + "] " + std::strerror(e); }
}
struct OSError : PyErr { int err; OSError(int e, const std::string& file) : PyErr("OSError", errno_text(e) + ": " + str_repr(file)), err(e) {} };
inline std::filesystem::path fspath(const std::string& p) { return std::filesystem::path(std::u8string(p.begin(), p.end())); }   // UTF-8 paths on every OS
inline bool is_file(const std::string& p) { std::error_code ec; return std::filesystem::is_regular_file(fspath(p), ec); }
inline bool is_dir(const std::string& p) { std::error_code ec; return std::filesystem::is_directory(fspath(p), ec); }
inline bool exists(const std::string& p) { std::error_code ec; return std::filesystem::exists(fspath(p), ec); }
inline std::FILE* open_file(const std::string& p, const char* mode) {
#ifdef _WIN32
    std::wstring m(mode, mode + std::strlen(mode)); return _wfopen(fspath(p).c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}
inline void makedirs(const std::string& p) { std::error_code ec; std::filesystem::create_directories(fspath(p), ec); if (ec && !is_dir(p)) throw OSError(ec.value(), p); }
// open(path, encoding='utf-8[-sig]').read(): universal newlines unless raw (newline='')
inline std::string read_text(const std::string& path, bool sig = false, bool raw = false) {
    if (is_dir(path)) throw OSError(EISDIR, path);
    errno = 0; std::FILE* f = open_file(path, "rb"); if (!f) throw OSError(errno ? errno : ENOENT, path);
    std::string t; char buf[65536]; std::size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) t.append(buf, n); std::fclose(f);
    if (sig && t.compare(0, 3, "\xEF\xBB\xBF") == 0) t.erase(0, 3);
    if (!raw) { std::string o; o.reserve(t.size()); for (std::size_t k = 0; k < t.size(); ++k) { if (t[k] == '\r') { o += '\n'; if (k + 1 < t.size() && t[k + 1] == '\n') k++; } else o += t[k]; } t.swap(o); }
    return t;
}
inline void write_text(const std::string& path, const std::string& text) {
    if (is_dir(path)) throw OSError(EISDIR, path);
    errno = 0; std::FILE* f = open_file(path, "wb"); if (!f) throw OSError(errno ? errno : EACCES, path);
    std::fwrite(text.data(), 1, text.size(), f); std::fclose(f);
}
inline std::string dirname(const std::string& p) { const std::size_t k = p.find_last_of("/\\"); return k == std::string::npos ? "" : p.substr(0, k == 0 ? 1 : k); }
inline std::string basename(const std::string& p) { const std::size_t k = p.find_last_of("/\\"); return k == std::string::npos ? p : p.substr(k + 1); }
inline std::string path_join(const std::string& a, const std::string& b) { if (a.empty() || (!b.empty() && (b[0] == '/' || b[0] == '\\'))) return b; return (a.back() == '/' || a.back() == '\\') ? a + b : a + "/" + b; }

// ------------------------------------------------------------------ csv.reader / csv.DictReader (excel dialect; text read with newline='')
inline std::vector<std::vector<std::string>> csv_rows(const std::string& text, char delim = ',') {   // csv.reader(..., delimiter=delim)
    std::vector<std::vector<std::string>> rows; std::vector<std::string> row; std::string f; bool inq = false, quoted = false, any = false;
    for (std::size_t k = 0; k < text.size();) {
        const char c = text[k];
        if (inq) { if (c == '"') { if (k + 1 < text.size() && text[k + 1] == '"') { f += '"'; k += 2; continue; } inq = false; k++; continue; } f += c; k++; continue; }
        if (c == '"' && f.empty() && !quoted) { inq = true; quoted = true; any = true; k++; continue; }
        if (c == delim) { row.push_back(f); f.clear(); quoted = false; any = true; k++; continue; }
        if (c == '\r' || c == '\n') {
            if (any || !f.empty() || !row.empty()) { row.push_back(f); rows.push_back(row); } else rows.emplace_back();
            row.clear(); f.clear(); quoted = false; any = false; k += (c == '\r' && k + 1 < text.size() && text[k + 1] == '\n') ? 2 : 1; continue;
        }
        f += c; any = true; k++;
    }
    if (inq) throw PyErr("_csv.Error", "unexpected end of data");
    if (any || !f.empty() || !row.empty()) { row.push_back(f); rows.push_back(row); }
    return rows;
}
struct CsvDict { std::vector<std::string> fields; std::vector<std::vector<std::pair<std::optional<std::string>, Val>>> rows; };   // key nullopt = Python's None restkey
inline CsvDict csv_dicts(const std::string& text) {
    CsvDict o; auto rows = csv_rows(text); if (rows.empty()) return o;
    o.fields = rows[0];
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r]; if (row.empty()) continue;
        std::vector<std::pair<std::optional<std::string>, Val>> d;
        auto put = [&](const std::optional<std::string>& k, Val v) { for (auto& [a, b] : d) if (a == k) { b = std::move(v); return; } d.emplace_back(k, std::move(v)); };
        for (std::size_t j = 0; j < o.fields.size(); ++j) put(o.fields[j], j < row.size() ? Val::str(row[j]) : Val());
        if (row.size() > o.fields.size()) { Val extra = Val::list(); for (std::size_t j = o.fields.size(); j < row.size(); ++j) extra.l->push_back(Val::str(row[j])); put(std::nullopt, extra); }
        o.rows.push_back(std::move(d));
    }
    return o;
}

// ------------------------------------------------------------------ output
inline std::string& outbuf() { static std::string b; return b; }
inline void out(const std::string& s) { outbuf() += s; if (outbuf().size() > (1u << 16)) { std::fwrite(outbuf().data(), 1, outbuf().size(), stdout); outbuf().clear(); } }
inline void print(const std::string& s = "") { out(s); out("\n"); }
inline void flush() { if (!outbuf().empty()) { std::fwrite(outbuf().data(), 1, outbuf().size(), stdout); outbuf().clear(); } std::fflush(stdout); }
inline void eprint(const std::string& s) { flush(); std::fwrite(s.data(), 1, s.size(), stderr); std::fputc('\n', stderr); std::fflush(stderr); }

// ------------------------------------------------------------------ argparse
struct Arg {
    std::vector<std::string> flags; std::string dest_, action_ = "store", type_, nargs_, metavar_, help_, type_name_; bool has_metavar = false;
    Val dflt; Val konst; bool has_default = false; std::vector<std::string> choices_; bool required_ = false, req_set = false;
    bool nodef_ = false;
    bool hidden_ = false;
    bool suppress_ = false;   // default=argparse.SUPPRESS, help=argparse.SUPPRESS: unset unless given, and not listed
    std::function<Val(const std::string&)> conv;   // custom type; throw PyErr("ArgumentTypeError", msg) for a custom message
    bool opt() const { return !flags.empty(); }
    int zero() const { return action_ == "store_true" || action_ == "store_false" || action_ == "count" || action_ == "store_const" || action_ == "help" || action_ == "append_const" || action_ == "version"; }
    Arg& integer() { type_ = "int"; return *this; }
    Arg& number() { type_ = "float"; return *this; }
    Arg& type(std::function<Val(const std::string&)> f, std::string name) { conv = std::move(f); type_ = "custom"; type_name_ = std::move(name); return *this; }
    Arg& store_true() { action_ = "store_true"; if (!has_default) { dflt = Val::boolean(false); } return *this; }
    Arg& store_false() { action_ = "store_false"; if (!has_default) { dflt = Val::boolean(true); } return *this; }
    Arg& count() { action_ = "count"; return *this; }
    std::string version_;
    Arg& version(std::string v) { action_ = "version"; version_ = std::move(v); return *this; }   // action="version": print it and exit 0
    Arg& append() { action_ = "append"; return *this; }
    Arg& store_const(Val c) { action_ = "store_const"; konst = std::move(c); return *this; }
    Arg& def(Val v) { dflt = std::move(v); has_default = true; return *this; }
    Arg& def(const char* v) { return def(Val::str(v)); }
    Arg& def(long long v) { return def(Val::integer(v)); }
    Arg& def(int v) { return def(Val::integer(v)); }
    Arg& def_float(double v) { return def(Val::flt(v)); }
    Arg& cnst(Val v) { konst = std::move(v); return *this; }
    Arg& choices(std::vector<std::string> c) { choices_ = std::move(c); return *this; }
    Arg& required(bool r = true) { required_ = r; req_set = true; return *this; }
    Arg& nargs(std::string n) { nargs_ = std::move(n); return *this; }
    Arg& metavar(std::string m) { metavar_ = std::move(m); has_metavar = true; return *this; }
    Arg& help(std::string h) { help_ = std::move(h); return *this; }
    Arg& dest(std::string d) { dest_ = std::move(d); return *this; }
    Arg& suppress() { suppress_ = true; return *this; }
    Arg& hidden() { hidden_ = true; return *this; }   // help=argparse.SUPPRESS alone: not listed, the default still set
    Arg& no_default() { nodef_ = true; return *this; }   // default=argparse.SUPPRESS alone: unset unless given, still listed in help
};
struct Parser {
    std::string prog, description, epilog;
    bool raw_description = false;   // formatter_class=argparse.RawDescriptionHelpFormatter
    std::deque<Arg> args;
    std::vector<std::pair<bool, std::vector<std::string>>> groups;   // mutually exclusive: (required, dests)
    bool has_sub = false, sub_required = true; std::string sub_dest, sub_metavar, sub_help;
    std::list<std::pair<std::string, Parser>> subs;   /* list, not deque: Parser is incomplete here and only list/vector/forward_list allow that (libc++ enforces it) */
    std::string help_line;   // add_parser(name, help=...): listed under the parent's {a,b,c} line
    Parser(std::string p = "", std::string d = "") : prog(std::move(p)), description(std::move(d)) {}
    Arg& opt(std::vector<std::string> flags) {
        Arg a; a.flags = std::move(flags);
        std::string pick = a.flags[0]; for (const auto& f : a.flags) if (starts(f, "--")) { pick = f; break; }
        std::size_t k = 0; while (k < pick.size() && pick[k] == '-') k++;
        a.dest_ = replace_all(pick.substr(k), "-", "_"); args.push_back(std::move(a)); return args.back();
    }
    Arg& pos(std::string name) { Arg a; a.dest_ = std::move(name); args.push_back(std::move(a)); return args.back(); }
    void exclusive(std::vector<std::string> dests, bool required = false) { groups.emplace_back(required, std::move(dests)); }
    void subcommands(std::string dest, bool required = true) { has_sub = true; sub_dest = std::move(dest); sub_required = required; }
    Parser& sub(const std::string& name, std::string description = "") { subs.emplace_back(name, Parser("", std::move(description))); return subs.back().second; }
};
namespace ap {
struct Act { Arg* a = nullptr; bool help = false, parser = false; const Parser* sp = nullptr; std::vector<std::string> flags; std::string dest; };
inline int width() {
    if (const char* c = std::getenv("COLUMNS")) { const int v = std::atoi(c); if (v > 0) return std::max(11, v - 2); }
    return 78;
}
struct P { std::string prog; const Parser* p; std::vector<Act> acts; };
inline P norm(const Parser& p, const std::string& prog) {
    P o{prog, &p, {}};
    Act h; h.help = true; h.flags = {"-h", "--help"}; h.dest = "help"; o.acts.push_back(h);
    for (auto& a : const_cast<Parser&>(p).args) {
        Act x; x.a = &a; x.flags = a.flags; x.dest = a.dest_;
        if (!a.opt() && !a.req_set) a.required_ = !(a.nargs_ == "?" || a.nargs_ == "*");
        o.acts.push_back(x);
    }
    if (p.has_sub) { Act s; s.parser = true; s.sp = &p; s.dest = p.sub_dest; o.acts.push_back(s); }
    return o;
}
inline bool is_opt(const Act& x) { return !x.flags.empty(); }
inline std::vector<std::string> sub_names(const Parser& p) { std::vector<std::string> v; for (const auto& s : p.subs) v.push_back(s.first); return v; }
inline std::string metavar(const Act& x) {
    if (x.parser) return x.sp->sub_metavar.empty() ? "{" + join(sub_names(*x.sp), ",") + "}" : x.sp->sub_metavar;
    if (x.a->has_metavar) return x.a->metavar_;
    if (!x.a->choices_.empty()) return "{" + join(x.a->choices_, ",") + "}";
    return is_opt(x) ? upper(x.dest) : x.dest;
}
inline std::string fmt_args(const Act& x) {
    const std::string m = metavar(x);
    if (x.parser) return m + " ...";
    const std::string& n = x.a->nargs_;
    if (n.empty()) return m; if (n == "?") return "[" + m + "]"; if (n == "*") return "[" + m + " ...]"; if (n == "+") return m + " [" + m + " ...]";
    std::vector<std::string> v(static_cast<std::size_t>(std::atoi(n.c_str())), m); return join(v, " ");
}
inline bool zero(const Act& x) { return x.help || (x.a && x.a->zero()); }
inline bool required(const Act& x) { return x.parser ? x.sp->sub_required : x.help ? false : x.a->required_; }
inline std::string name_of(const Act& x) {
    if (is_opt(x)) return join(x.flags, "/");
    if (x.a && x.a->has_metavar) return x.a->metavar_;
    if (x.parser && x.sp && !x.sp->sub_metavar.empty()) return x.sp->sub_metavar;   /* add_subparsers(metavar=...) wins over dest */
    if (!x.dest.empty()) return x.dest;
    return metavar(x);
}
inline std::vector<std::string> usage_parts(const P& P_, bool which) {
    std::vector<std::string> parts; std::vector<const Act*> done;
    auto one = [](const Act& x) { return zero(x) ? x.flags[0] : x.flags[0] + " " + fmt_args(x); };
    for (const auto& x : P_.acts) {
        if (is_opt(x) != which || std::find(done.begin(), done.end(), &x) != done.end() || (x.a && (x.a->suppress_ || x.a->hidden_))) continue;
        if (which) {
            const std::pair<bool, std::vector<std::string>>* g = nullptr;
            for (const auto& gr : P_.p->groups) if (std::find(gr.second.begin(), gr.second.end(), x.dest) != gr.second.end()) g = &gr;
            if (g) {
                std::vector<std::string> inner;
                for (const auto& y : P_.acts) if (is_opt(y) && std::find(g->second.begin(), g->second.end(), y.dest) != g->second.end()) { inner.push_back(one(y)); done.push_back(&y); }
                parts.push_back(g->first ? "(" + join(inner, " | ") + ")" : "[" + join(inner, " | ") + "]"); continue;
            }
            parts.push_back(required(x) ? one(x) : "[" + one(x) + "]");
        } else parts.push_back(fmt_args(x));
    }
    return parts;
}
inline std::string usage(const P& P_) {
    const std::string prefix = "usage: "; const int tw = width();
    const auto opt = usage_parts(P_, true), pos = usage_parts(P_, false);
    std::vector<std::string> all{P_.prog}; all.insert(all.end(), opt.begin(), opt.end()); all.insert(all.end(), pos.begin(), pos.end());
    std::string u = join(all, " ");
    if (static_cast<int>(prefix.size() + u.size()) > tw) {
        auto lines_of = [&](const std::vector<std::string>& parts, const std::string& indent, const std::string* pre) {
            std::vector<std::string> lines, line; int len = pre ? static_cast<int>(pre->size()) - 1 : static_cast<int>(indent.size()) - 1;
            for (const auto& p : parts) { if (len + 1 + static_cast<int>(p.size()) > tw && !line.empty()) { lines.push_back(indent + join(line, " ")); line.clear(); len = static_cast<int>(indent.size()) - 1; } line.push_back(p); len += static_cast<int>(p.size()) + 1; }
            if (!line.empty()) lines.push_back(indent + join(line, " "));
            if (pre && !lines.empty()) lines[0] = lines[0].substr(indent.size());
            return lines;
        };
        std::vector<std::string> lines;
        if (prefix.size() + P_.prog.size() <= 0.75 * tw) {
            const std::string indent(prefix.size() + P_.prog.size() + 1, ' ');
            std::vector<std::string> first{P_.prog};
            if (!opt.empty()) { first.insert(first.end(), opt.begin(), opt.end()); lines = lines_of(first, indent, &prefix); auto l2 = lines_of(pos, indent, nullptr); lines.insert(lines.end(), l2.begin(), l2.end()); }
            else if (!pos.empty()) { first.insert(first.end(), pos.begin(), pos.end()); lines = lines_of(first, indent, &prefix); }
            else lines = {P_.prog};
        } else {
            const std::string indent(prefix.size(), ' '); std::vector<std::string> parts = opt; parts.insert(parts.end(), pos.begin(), pos.end());
            lines = lines_of(parts, indent, nullptr);
            if (lines.size() > 1) { lines = lines_of(opt, indent, nullptr); auto l2 = lines_of(pos, indent, nullptr); lines.insert(lines.end(), l2.begin(), l2.end()); }
            lines.insert(lines.begin(), P_.prog);
        }
        u = join(lines, "\n");
    }
    return prefix + u + "\n";
}
// textwrap.wrap(text, width) with TextWrapper's defaults, on text whose whitespace is already single spaces
namespace tw {
inline bool letter(const std::string& c) { const unsigned char b = static_cast<unsigned char>(c[0]); return c.size() > 1 || std::isalpha(b) || b == '_'; }   // [^\d\W]
inline bool word(const std::string& c) { const unsigned char b = static_cast<unsigned char>(c[0]); return c.size() > 1 || std::isalnum(b) || b == '_'; }    // \w
inline bool punct(const std::string& c) { return word(c) || (c.size() == 1 && std::string("!\"'&.,?").find(c[0]) != std::string::npos); }   // [\w!"'&.,?]
inline std::string collapse(const std::string& t) {   // re.sub(r'\s+', ' ', t, flags=re.ASCII).strip()
    std::string o; bool sp = false;
    for (char c : t) { if (c == ' ' || (c >= 9 && c <= 13)) sp = true; else { if (sp && !o.empty()) o += ' '; sp = false; o += c; } }
    return o;
}
inline std::vector<std::string> chunks(const std::string& text) {   // TextWrapper.wordsep_re
    const auto c = cps(text); const std::size_t n = c.size(); std::vector<std::string> out; std::size_t i = 0;
    auto at = [&](std::size_t k) -> const std::string& { static const std::string none = " "; return k < n ? c[k] : none; };
    auto is = [&](std::size_t k, const char* x) { return k < n && c[k] == x; };
    auto dash_run_then_word = [&](std::size_t k) { std::size_t j = k; while (is(j, "-")) ++j; return j - k >= 2 && j < n && word(c[j]); };
    while (i < n) {
        if (c[i] == " ") { std::string s; while (i < n && c[i] == " ") s += c[i++]; out.push_back(s); continue; }
        if (c[i] == "-" && i > 0 && punct(c[i - 1]) && dash_run_then_word(i)) { std::string s; while (is(i, "-")) s += c[i++]; out.push_back(s); continue; }
        std::size_t j = i + 1; std::string s = c[i];
        for (;;) {
            const bool behind = (j >= 2 && letter(at(j - 2)) && letter(at(j - 1))) || (j >= 3 && letter(at(j - 3)) && at(j - 2) == "-" && letter(at(j - 1)));
            if (is(j, "-") && behind && j + 1 < n && letter(c[j + 1]) && ((j + 2 < n && letter(c[j + 2])) || (is(j + 2, "-") && j + 3 < n && letter(c[j + 3])))) { s += "-"; ++j; break; }
            if (j >= n || c[j] == " ") break;
            if (punct(c[j - 1]) && dash_run_then_word(j)) break;
            s += c[j]; ++j;
        }
        out.push_back(s); i = j;
    }
    return out;
}
inline std::vector<std::string> wrap(const std::string& text, int width) {
    auto ch = chunks(text); std::reverse(ch.begin(), ch.end()); std::vector<std::string> lines;
    while (!ch.empty()) {
        std::vector<std::string> cur; long len = 0; const long w = width;
        if (strip(ch.back()).empty() && !lines.empty()) ch.pop_back();
        while (!ch.empty()) { const long l = static_cast<long>(cplen(ch.back())); if (len + l <= w) { cur.push_back(ch.back()); ch.pop_back(); len += l; } else break; }
        if (!ch.empty() && static_cast<long>(cplen(ch.back())) > w) {   // _handle_long_word (break_long_words, break_on_hyphens)
            const long space = w < 1 ? 1 : w - len; const std::string chunk = ch.back(); long end = space;
            if (static_cast<long>(cplen(chunk)) > space) {
                const auto cs = cps(chunk); long hy = -1; for (long k = std::min<long>(space, static_cast<long>(cs.size())) - 1; k >= 0; --k) if (cs[static_cast<std::size_t>(k)] == "-") { hy = k; break; }
                bool other = false; for (long k = 0; k < hy; ++k) if (cs[static_cast<std::size_t>(k)] != "-") other = true;
                if (hy > 0 && other) end = hy + 1;
            }
            cur.push_back(cpslice(chunk, 0, end)); ch.back() = cpslice(chunk, end);
            len = 0; for (const auto& x : cur) len += static_cast<long>(cplen(x));
        }
        if (!cur.empty() && strip(cur.back()).empty()) { len -= static_cast<long>(cplen(cur.back())); cur.pop_back(); }
        if (!cur.empty()) lines.push_back(join(cur, ""));
    }
    return lines;
}
}  // namespace tw
inline std::string help_text(const P& P_) {
    std::string o = usage(P_) + "\n";
    const int W = width();
    auto text = [&](std::string t) {   // HelpFormatter._format_text
        if (t.find("%(prog)") != std::string::npos) t = replace_all(replace_all(t, "%(prog)s", P_.prog), "%%", "%");
        if (P_.p->raw_description) return t + "\n\n";
        std::string r; for (const auto& l : tw::wrap(tw::collapse(t), std::max(W, 11))) r += l + "\n";
        return r + "\n";
    };
    if (!P_.p->description.empty()) o += text(P_.p->description);
    auto hs = [&](const Act& x) { if (x.help) return std::string("show this help message and exit"); if (x.parser) return x.sp->sub_help; if (x.a->action_ == "version" && x.a->help_.empty()) return std::string("show program's version number and exit"); return replace_all(replace_all(replace_all(x.a->help_, "%(default)s", pystr(x.a->dflt)), "%(prog)s", P_.prog), "%%", "%"); };
    auto inv_of = [&](const Act& x) { if (!is_opt(x)) return metavar(x); std::vector<std::string> inv; for (const auto& f : x.flags) inv.push_back(zero(x) ? f : f + " " + fmt_args(x)); return join(inv, ", "); };
    auto shown = [&](const Act& x) { return !(x.a && (x.a->suppress_ || x.a->hidden_)); };
    std::size_t maxlen = 0;   // _action_max_length
    for (const auto& x : P_.acts) {
        if (!shown(x)) continue;
        maxlen = std::max(maxlen, cplen(inv_of(x)) + 2);
        if (x.parser) for (const auto& sp : x.sp->subs) if (!sp.second.help_line.empty()) maxlen = std::max(maxlen, cplen(sp.first) + 4);
    }
    const int mhp = std::min(24, std::max(W - 20, 4));
    const int hp = std::min(static_cast<int>(maxlen) + 2, mhp), hw = std::max(W - hp, 11);
    auto item = [&](const std::string& inv, const std::string& h, int indent) {   // HelpFormatter._format_action
        const std::string pad(static_cast<std::size_t>(indent), ' '); const int aw = hp - indent - 2;
        if (h.empty()) return pad + inv + "\n";
        std::string r; int first = 0;
        if (static_cast<int>(cplen(inv)) <= aw) r = pad + inv + std::string(static_cast<std::size_t>(aw - static_cast<int>(cplen(inv))), ' ') + "  ";
        else { r = pad + inv + "\n"; first = hp; }
        const auto lines = tw::wrap(tw::collapse(h), hw);
        if (lines.empty()) return r + (r.back() == '\n' ? "" : "\n");
        r += std::string(static_cast<std::size_t>(first), ' ') + lines[0] + "\n";
        for (std::size_t k = 1; k < lines.size(); ++k) r += std::string(static_cast<std::size_t>(hp), ' ') + lines[k] + "\n";
        return r;
    };
    bool any_pos = false; for (const auto& x : P_.acts) if (!is_opt(x)) any_pos = true;
    if (any_pos) {
        o += "positional arguments:\n";
        for (const auto& x : P_.acts) if (!is_opt(x)) {
            o += item(inv_of(x), hs(x), 2);
            if (x.parser) for (const auto& sp : x.sp->subs) if (!sp.second.help_line.empty()) o += item(sp.first, sp.second.help_line, 4);
        }
        o += "\n";
    }
    o += "options:\n";
    for (const auto& x : P_.acts) if (is_opt(x) && shown(x)) o += item(inv_of(x), hs(x), 2);
    if (!P_.p->epilog.empty()) { std::string e = text(P_.p->epilog); while (!e.empty() && e.back() == '\n') e.pop_back(); o += "\n" + e + "\n"; }
    return o;
}
[[noreturn]] inline void fail(const P& P_, const std::string& msg) { flush(); const std::string t = usage(P_) + P_.prog + ": error: " + msg + "\n"; std::fwrite(t.data(), 1, t.size(), stderr); throw Exit{2}; }
inline Val convert(const P& P_, const Act& x, const std::string& s) {
    Val v = Val::str(s); const Arg& a = *x.a;
    if (a.type_ == "int") { const auto r = parse_int(s); if (!r) fail(P_, "argument " + name_of(x) + ": invalid int value: " + str_repr(s)); v = Val::integer(*r); }
    else if (a.type_ == "float") { const auto r = parse_float(s); if (!r) fail(P_, "argument " + name_of(x) + ": invalid float value: " + str_repr(s)); v = Val::flt(*r); }
    else if (a.type_ == "custom") {
        try { v = a.conv(s); }
        catch (const PyErr& e) { if (e.type == "ArgumentTypeError") fail(P_, "argument " + name_of(x) + ": " + e.what()); fail(P_, "argument " + name_of(x) + ": invalid " + a.type_name_ + " value: " + str_repr(s)); }
    }
    if (!a.choices_.empty()) {
        const std::string key = v.t == Val::STR ? v.s : pystr(v);
        if (std::find(a.choices_.begin(), a.choices_.end(), key) == a.choices_.end()) {
            std::vector<std::string> rc; for (const auto& c : a.choices_) rc.push_back(a.type_.empty() ? str_repr(c) : c);
            fail(P_, "argument " + name_of(x) + ": invalid choice: " + (v.t == Val::STR ? str_repr(v.s) : pystr(v)) + " (choose from " + join(rc, ", ") + ")");
        }
    }
    return v;
}
inline bool neg_num(const std::string& s) { static const std::regex r("^-\\d+$|^-\\d*\\.\\d+$"); return std::regex_match(s, r); }
struct Tup { const Act* x = nullptr; std::string os; std::optional<std::string> explicit_; bool known = false; };
inline std::optional<Tup> classify(const P& P_, const std::string& s, bool has_neg) {
    if (s.empty() || s[0] != '-' || s == "-") return std::nullopt;
    auto find = [&](const std::string& f) -> const Act* { for (const auto& x : P_.acts) for (const auto& g : x.flags) if (g == f) return &x; return nullptr; };
    if (const Act* x = find(s)) return Tup{x, s, std::nullopt, true};
    if (s.size() == 1) return std::nullopt;
    const std::size_t eq = s.find('=');
    if (eq != std::string::npos) { const std::string o = s.substr(0, eq); if (const Act* x = find(o)) return Tup{x, o, s.substr(eq + 1), true}; }
    if (starts(s, "--")) {
        const std::string o = eq == std::string::npos ? s : s.substr(0, eq);
        std::vector<std::pair<const Act*, std::string>> hits;
        for (const auto& x : P_.acts) for (const auto& g : x.flags) if (starts(g, "--") && starts(g, o)) hits.emplace_back(&x, g);
        if (hits.size() > 1) { std::vector<std::string> names; for (auto& h : hits) names.push_back(h.second); fail(P_, "ambiguous option: " + o + " could match " + join(names, ", ")); }
        if (hits.size() == 1) return Tup{hits[0].first, hits[0].second, eq == std::string::npos ? std::nullopt : std::optional<std::string>(s.substr(eq + 1)), true};
    } else if (s[1] != '-') {
        const std::string sh = s.substr(0, 2); if (const Act* x = find(sh)) return Tup{x, sh, s.substr(2), true};
    }
    if (neg_num(s) && !has_neg) return std::nullopt;
    if (s.find(' ') != std::string::npos) return std::nullopt;
    return Tup{nullptr, s, std::nullopt, false};
}
inline std::string nargs_pat(const Act& x) {
    std::string p;
    if (zero(x)) p = "(-*-*)";
    else if (x.parser) p = "(-*A[-AO]*)";
    else { const std::string& n = x.a->nargs_;
        if (n.empty()) p = "(-*A-*)"; else if (n == "?") p = "(-*A?-*)"; else if (n == "*") p = "(-*[A-]*)"; else if (n == "+") p = "(-*A[A-]*)";
        else { p = "(-*"; for (int k = 0; k < std::atoi(n.c_str()); ++k) p += k ? "-*A" : "A"; p += "-*)"; } }
    if (is_opt(x)) { p = replace_all(p, "-*", ""); p = replace_all(p, "-", ""); }
    return p;
}
std::vector<std::string> parse_into(const P& P_, const std::vector<std::string>& argv, Dict& ns);
inline void set_defaults(const P& P_, Dict& ns, bool override_existing) {
    for (const auto& x : P_.acts) { if (x.help || x.dest.empty() || (x.a && (x.a->suppress_ || x.a->nodef_))) continue; if (override_existing || !ns.has(x.dest)) ns.set(x.dest, x.parser ? Val() : x.a->dflt); }
}
inline std::vector<std::string> parse_into(const P& P_, const std::vector<std::string>& argv, Dict& ns) {
    bool has_neg = false; for (const auto& x : P_.acts) for (const auto& f : x.flags) if (neg_num(f)) has_neg = true;
    std::string pat; std::vector<std::optional<Tup>> info; bool dd = false;
    for (const auto& s : argv) {
        if (dd) { pat += 'A'; info.emplace_back(); continue; }
        if (s == "--") { dd = true; pat += '-'; info.emplace_back(); continue; }
        auto t = classify(P_, s, has_neg); if (t) { pat += 'O'; info.push_back(t); } else { pat += 'A'; info.emplace_back(); }
    }
    std::vector<std::string> extras; std::vector<const Act*> seen, seen_nondefault;
    auto take = [&](const Act& x, std::vector<std::string> args) {
        seen.push_back(&x);
        if (x.help) { flush(); const std::string h = help_text(P_); std::fwrite(h.data(), 1, h.size(), stdout); throw Exit{0}; }
        if (x.a && x.a->action_ == "version") { flush(); const std::string v = x.a->version_ + "\n"; std::fwrite(v.data(), 1, v.size(), stdout); throw Exit{0}; }
        if (x.parser) {
            const std::string name = args[0]; const Parser* sp = nullptr; for (const auto& s : x.sp->subs) if (s.first == name) sp = &s.second;
            if (!sp) { std::vector<std::string> rc; for (const auto& n : sub_names(*x.sp)) rc.push_back(str_repr(n)); fail(P_, "argument " + name_of(x) + ": invalid choice: " + str_repr(name) + " (choose from " + join(rc, ", ") + ")"); }
            if (!x.dest.empty()) ns.set(x.dest, Val::str(name));
            P tmp = P_; tmp.acts.clear(); for (const auto& y : P_.acts) if (!is_opt(y) && !y.parser) tmp.acts.push_back(y);
            std::vector<std::string> pp{P_.prog}; for (const auto& u : usage_parts(tmp, false)) pp.push_back(u);
            const P SP = norm(*sp, join(pp, " ") + " " + name);
            Dict sub; set_defaults(SP, sub, true);
            auto ex = parse_into(SP, std::vector<std::string>(args.begin() + 1, args.end()), sub);
            for (auto& [k, v] : sub.items) ns.set(k, v);
            extras.insert(extras.end(), ex.begin(), ex.end());
            return;
        }
        const Arg& a = *x.a; Val v;
        if (!is_opt(x)) { auto it = std::find(args.begin(), args.end(), "--"); if (it != args.end()) args.erase(it); }
        if (a.zero()) { if (a.action_ == "store_true") v = Val::boolean(true); else if (a.action_ == "store_false") v = Val::boolean(false); else if (a.action_ == "count") { const Val* c = ns.get(x.dest); v = Val::integer((c && c->t == Val::INT ? c->i : 0) + 1); } else v = a.konst; }
        else if (a.nargs_.empty()) v = convert(P_, x, args[0]);
        else if (a.nargs_ == "?") v = !args.empty() ? convert(P_, x, args[0]) : is_opt(x) ? a.konst : (a.dflt.t == Val::STR && !a.type_.empty() ? convert(P_, x, a.dflt.s) : a.dflt);
        else { v = Val::list(); for (const auto& s : args) v.l->push_back(convert(P_, x, s)); if (!is_opt(x) && a.nargs_ == "*" && args.empty() && !a.dflt.is_none()) v = a.dflt; }
        const bool nondefault = !((v.t == Val::BOOL && a.dflt.t == Val::BOOL && v.b == a.dflt.b) || (v.is_none() && a.dflt.is_none()));
        for (const auto& g : P_.p->groups) if (std::find(g.second.begin(), g.second.end(), x.dest) != g.second.end())
            for (const Act* o : seen_nondefault) if (o != &x && std::find(g.second.begin(), g.second.end(), o->dest) != g.second.end()) fail(P_, "argument " + name_of(x) + ": not allowed with argument " + name_of(*o));
        if (nondefault) seen_nondefault.push_back(&x);
        if (a.action_ == "append") { Val* cur = ns.get(x.dest); Val l = Val::list(); if (cur && cur->t == Val::LIST && !(a.has_default && cur->l == a.dflt.l)) *l.l = *cur->l; l.l->push_back(v); ns.set(x.dest, l); }
        else ns.set(x.dest, v);
    };
    std::vector<const Act*> positionals; for (const auto& x : P_.acts) if (!is_opt(x)) positionals.push_back(&x);
    bool ended = false;
    auto consume_positionals = [&](std::size_t start) -> std::size_t {
        const std::string sel = pat.substr(start); std::vector<std::size_t> counts;
        for (std::size_t k = positionals.size(); k > 0; --k) {
            std::string re = "^"; for (std::size_t j = 0; j < k; ++j) re += nargs_pat(*positionals[j]);
            std::smatch m; if (std::regex_search(sel, m, std::regex(re))) { for (std::size_t g = 1; g < m.size(); ++g) counts.push_back(static_cast<std::size_t>(m[g].length())); break; }
        }
        for (std::size_t k = 0; k < counts.size(); ++k) {
            const Act* x = positionals[k]; std::vector<std::string> args(argv.begin() + static_cast<long>(start), argv.begin() + static_cast<long>(start + counts[k])); start += counts[k];
            take(*x, args);
            if (x->parser) { positionals.clear(); ended = true; return argv.size(); }
        }
        positionals.erase(positionals.begin(), positionals.begin() + static_cast<long>(counts.size()));
        return start;
    };
    auto consume_optional = [&](std::size_t start) -> std::size_t {
        Tup t = *info[start];
        for (;;) {
            if (!t.x) { extras.push_back(argv[start]); return start + 1; }
            if (t.explicit_) {
                if (zero(*t.x)) {
                    if (t.os.size() == 2 && t.os[0] == '-' && t.os[1] != '-' && !t.explicit_->empty()) {
                        take(*t.x, {});
                        const std::string next = std::string("-") + (*t.explicit_)[0]; const std::string rest = t.explicit_->substr(1);
                        const Act* nx = nullptr; for (const auto& y : P_.acts) for (const auto& f : y.flags) if (f == next) nx = &y;
                        if (nx) { t = Tup{nx, next, rest.empty() ? std::nullopt : std::optional<std::string>(rest), true}; continue; }
                    }
                    fail(P_, "argument " + name_of(*t.x) + ": ignored explicit argument " + str_repr(*t.explicit_));
                }
                take(*t.x, {*t.explicit_}); return start + 1;
            }
            const std::string sel = pat.substr(start + 1); std::smatch m;
            if (!std::regex_search(sel, m, std::regex("^" + nargs_pat(*t.x)))) {
                const std::string& n = t.x->a->nargs_;
                fail(P_, "argument " + name_of(*t.x) + ": " + (n.empty() ? "expected one argument" : n == "?" ? "expected at most one argument" : n == "+" ? "expected at least one argument" : "expected " + n + " argument" + (n == "1" ? "" : "s")));
            }
            const std::size_t cnt = static_cast<std::size_t>(m[1].length());
            take(*t.x, std::vector<std::string>(argv.begin() + static_cast<long>(start + 1), argv.begin() + static_cast<long>(start + 1 + cnt)));
            return start + 1 + cnt;
        }
    };
    std::vector<std::size_t> opt_idx; for (std::size_t k = 0; k < pat.size(); ++k) if (pat[k] == 'O') opt_idx.push_back(k);
    const long max_opt = opt_idx.empty() ? -1 : static_cast<long>(opt_idx.back());
    std::size_t start = 0;
    while (!ended && static_cast<long>(start) <= max_opt) {
        const std::size_t next = *std::find_if(opt_idx.begin(), opt_idx.end(), [&](std::size_t k) { return k >= start; });
        if (start != next) { const std::size_t e = consume_positionals(start); if (ended) break; if (e > start) { start = e; continue; } start = e; }
        if (std::find(opt_idx.begin(), opt_idx.end(), start) == opt_idx.end()) { extras.insert(extras.end(), argv.begin() + static_cast<long>(start), argv.begin() + static_cast<long>(next)); start = next; }
        start = consume_optional(start);
    }
    if (!ended) { const std::size_t stop = consume_positionals(start); extras.insert(extras.end(), argv.begin() + static_cast<long>(std::min(stop, argv.size())), argv.end()); }
    std::vector<std::string> missing;
    for (const auto& x : P_.acts) if (required(x) && std::find(seen.begin(), seen.end(), &x) == seen.end()) missing.push_back(name_of(x));
    if (!missing.empty()) fail(P_, "the following arguments are required: " + join(missing, ", "));
    for (const auto& g : P_.p->groups) if (g.first) {
        bool any = false; std::vector<std::string> names;
        for (const auto& x : P_.acts) if (is_opt(x) && std::find(g.second.begin(), g.second.end(), x.dest) != g.second.end()) { names.push_back(join(x.flags, "/")); if (std::find(seen.begin(), seen.end(), &x) != seen.end()) any = true; }
        if (!any) fail(P_, "one of the arguments " + join(names, " ") + " is required");
    }
    // string defaults go through type= like argparse does
    for (const auto& x : P_.acts) if (x.a && std::find(seen.begin(), seen.end(), &x) == seen.end() && x.a->dflt.t == Val::STR && !x.a->type_.empty() && ns.has(x.dest)) ns.set(x.dest, convert(P_, x, x.a->dflt.s));
    return extras;
}
}  // namespace ap
// parse_args(): the namespace as an ordered dict (dest -> value); prints usage + error and throws Exit{2} like argparse
inline Val parse_args(const Parser& p, const std::vector<std::string>& argv) {
    const ap::P P_ = ap::norm(p, p.prog);
    Val ns = Val::dict(); ap::set_defaults(P_, *ns.d, true);
    auto extras = ap::parse_into(P_, argv, *ns.d);
    if (!extras.empty()) ap::fail(P_, "unrecognized arguments: " + join(extras, " "));
    return ns;
}
inline std::string S(const Val& ns, const std::string& k) { const Val v = get(ns, k); return v.t == Val::STR ? v.s : v.is_none() ? "" : pystr(v); }   // string arg ("" when None)
inline bool B(const Val& ns, const std::string& k) { return truthy(get(ns, k)); }
}  // namespace pyrt
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
