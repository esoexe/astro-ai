// common.h - small helpers shared by the built-in pack tools (on top of pyrt.h).
#pragma once
#include "pyrt.h"
#include <ctime>
#include <set>
#include <algorithm>
#include <optional>
#include <map>
#include <chrono>
#include <thread>
#include <optional>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <csignal>
#include <cerrno>
#endif
namespace pyrt {
// datetime.datetime.now().replace(microsecond=0).isoformat()  ->  2026-10-31T21:05:09 (local time)
inline std::string now_iso() {
    const std::time_t t = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32]; std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &tm); return b;
}
// date.today().isoformat()
inline std::string today_iso() { return now_iso().substr(0, 10); }
// os.replace(src, dst): rename over an existing file (Windows needs the old one removed first)
inline void os_replace(const std::string& src, const std::string& dst) {
    std::error_code ec; std::filesystem::rename(fspath(src), fspath(dst), ec);
    if (ec) { std::filesystem::remove(fspath(dst), ec); std::filesystem::rename(fspath(src), fspath(dst), ec); if (ec) throw OSError(ec.value(), dst); }
}
// '%+.<nd>f' % x
inline std::string fmt_fs(double x, int nd) { std::string s = fmt_f(x, nd); return (s.empty() || s[0] == '-') ? s : "+" + s; }
// "{:,.<nd>f}".format(x)
inline std::string fmt_fc(double x, int nd) { return commas(fmt_f(x, nd)); }
// an insertion-ordered str -> double map (a Python dict of floats)
struct ODict {
    std::vector<std::pair<std::string, double>> items;
    double* find(const std::string& k) { for (auto& [a, v] : items) if (a == k) return &v; return nullptr; }
    const double* find(const std::string& k) const { for (const auto& [a, v] : items) if (a == k) return &v; return nullptr; }
    bool has(const std::string& k) const { return find(k) != nullptr; }
    double get(const std::string& k, double d = 0.0) const { const double* p = find(k); return p ? *p : d; }
    double& operator[](const std::string& k) { if (double* p = find(k)) return *p; items.emplace_back(k, 0.0); return items.back().second; }
    void erase(const std::string& k) { for (auto it = items.begin(); it != items.end(); ++it) if (it->first == k) { items.erase(it); return; } }
    std::vector<double> values() const { std::vector<double> v; for (const auto& [a, x] : items) v.push_back(x); return v; }
    bool empty() const { return items.empty(); }
};
// d.get(k) for a dict Val, raising AttributeError like Python when it is not a dict
inline Val dget(const Val& o, const std::string& k, Val dflt = Val()) {
    if (o.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(o) + "' object has no attribute 'get'");
    return o.d->at(k, std::move(dflt));
}
// a or b (Python truthiness)
inline Val orv(const Val& a, const Val& b) { return truthy(a) ? a : b; }
// iterate like Python's for x in v (list items, dict keys, string code points)
inline List iter(const Val& v) {
    if (v.t == Val::LIST) return *v.l;
    List o;
    if (v.t == Val::DICT) for (const auto& [k, x] : v.d->items) o.push_back(Val::str(k));
    else if (v.t == Val::STR) for (const auto& c : cps(v.s)) o.push_back(Val::str(c));
    else if (!v.is_none()) throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
    return o;
}
}  // namespace pyrt
namespace pyrt {
// hashlib.sha256(data).hexdigest() (FIPS 180-4), self-contained so the pack tools need no crypto library.
inline std::string sha256_hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string m = data; const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80); while (m.size() % 64 != 56) m += '\0';
    for (int k = 7; k >= 0; --k) m += static_cast<char>((bits >> (k * 8)) & 0xFF);
    const auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int k = 0; k < 16; ++k) w[k] = (static_cast<uint32_t>(static_cast<unsigned char>(m[off + 4 * k])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + 4 * k + 1])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + 4 * k + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(m[off + 4 * k + 3]));
        for (int k = 16; k < 64; ++k) { const uint32_t s0 = rotr(w[k - 15], 7) ^ rotr(w[k - 15], 18) ^ (w[k - 15] >> 3), s1 = rotr(w[k - 2], 17) ^ rotr(w[k - 2], 19) ^ (w[k - 2] >> 10); w[k] = w[k - 16] + s0 + w[k - 7] + s1; }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int k = 0; k < 64; ++k) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[k] + w[k];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65]; for (int k = 0; k < 8; ++k) std::snprintf(out + 8 * k, 9, "%08x", h[k]);
    return std::string(out, 64);
}
// float(x) that returns nothing instead of raising (the _f() helper several tools share)
inline std::optional<double> f_(const Val& v) { try { return to_float(v); } catch (const PyErr&) { return std::nullopt; } }
// int(x) like Python for the JSON types (bool, int, float truncates, numeric string)
inline long long to_int(const Val& v) {
    if (v.t == Val::BOOL) return v.b;
    if (v.t == Val::INT) return v.big() ? static_cast<long long>(v.num()) : v.i;
    if (v.t == Val::FLOAT) { if (!std::isfinite(v.f)) throw PyErr(std::isnan(v.f) ? "ValueError" : "OverflowError", std::isnan(v.f) ? "cannot convert float NaN to integer" : "cannot convert float infinity to integer"); return static_cast<long long>(std::trunc(v.f)); }
    if (v.t == Val::STR) { if (auto r = parse_int(v.s)) return *r; throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(v.s)); }
    throw PyErr("TypeError", std::string("int() argument must be a string, a bytes-like object or a real number, not '") + type_name(v) + "'");
}
}  // namespace pyrt
namespace pyrt {
// hashlib.sha1(data).hexdigest()
inline std::string sha1_hex(const std::string& data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = data; const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    m += static_cast<char>(0x80); while (m.size() % 64 != 56) m += '\0';
    for (int k = 7; k >= 0; --k) m += static_cast<char>((bits >> (k * 8)) & 0xFF);
    const auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (std::size_t o = 0; o < m.size(); o += 64) {
        uint32_t w[80];
        for (int t = 0; t < 16; ++t) w[t] = (static_cast<uint32_t>(static_cast<unsigned char>(m[o + t * 4])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(m[o + t * 4 + 1])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(m[o + t * 4 + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(m[o + t * 4 + 3]));
        for (int t = 16; t < 80; ++t) w[t] = rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int t = 0; t < 80; ++t) {
            const uint32_t f = t < 20 ? ((b & c) | (~b & d)) : t < 40 ? (b ^ c ^ d) : t < 60 ? ((b & c) | (b & d) | (c & d)) : (b ^ c ^ d);
            const uint32_t k = t < 20 ? 0x5A827999 : t < 40 ? 0x6ED9EBA1 : t < 60 ? 0x8F1BBCDC : 0xCA62C1D6;
            const uint32_t tmp = rol(a, 5) + f + e + k + w[t]; e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    char out[41]; for (int k = 0; k < 5; ++k) std::snprintf(out + k * 8, 9, "%08x", h[k]); return std::string(out, 40);
}
// Python's \w for one code point (str.isalnum() or '_'), close enough for text matching: ASCII exactly; other code
// points are word characters unless they are spaces, punctuation or symbols in the common blocks.
inline bool py_word(uint32_t c) {
    if (c < 0x80) return std::isalnum(static_cast<int>(c)) || c == '_';
    if (py_space(c)) return false;
    if ((c >= 0x80 && c <= 0xBF && c != 0xAA && c != 0xB2 && c != 0xB3 && c != 0xB5 && c != 0xB9 && c != 0xBA && c != 0xBC && c != 0xBD && c != 0xBE) || c == 0xD7 || c == 0xF7) return false;
    if ((c >= 0x2000 && c <= 0x206F) || (c >= 0x20A0 && c <= 0x20CF) || (c >= 0x2100 && c <= 0x214F && c != 0x2102 && c != 0x2107 && !(c >= 0x210A && c <= 0x2113) && c != 0x2115 && !(c >= 0x2119 && c <= 0x211D) && c != 0x2124 && c != 0x2126 && c != 0x2128 && !(c >= 0x212A && c <= 0x212D) && !(c >= 0x212F && c <= 0x2139))) return false;
    if ((c >= 0x2190 && c <= 0x245F) || (c >= 0x2500 && c <= 0x2775) || (c >= 0x2794 && c <= 0x2BFF) || (c >= 0x2E00 && c <= 0x2E7F) || (c >= 0x3000 && c <= 0x3004) || (c >= 0x3008 && c <= 0x3020) || c == 0x3030) return false;
    if ((c >= 0xE000 && c <= 0xF8FF) || (c >= 0xFE10 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40 && c != 0xFF3F) || (c >= 0xFF5B && c <= 0xFF65)) return false;
    if ((c >= 0x1F000 && c <= 0x1FAFF) || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x200D || c == 0xFEFF) return false;
    if (c == 0x060C || c == 0x061B || c == 0x061F || c == 0x066A || c == 0x066B || c == 0x066C || c == 0x066D || c == 0x06D4 || c == 0x0964 || c == 0x0965) return false;   // Arabic / Devanagari punctuation
    return true;
}
// ---- UTC datetimes as microseconds since the epoch (datetime.datetime with tzinfo=UTC)
inline long long civil_days(long long y, unsigned m, unsigned d) { y -= m <= 2; const long long era = (y >= 0 ? y : y - 399) / 400; const long long yoe = y - era * 400; const long long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy; }
inline void civil_from(long long z, long long& y, unsigned& m, unsigned& d) { z += 719468; const long long era = (z >= 0 ? z : z - 146096) / 146097; const long long doe = z - era * 146097; const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100); const long long mp = (5 * doy + 2) / 153; d = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1); m = static_cast<unsigned>(mp < 10 ? mp + 3 : mp - 9); y = yoe + era * 400 + (m <= 2); }
inline bool valid_ymd(long long y, long long m, long long d) { static const int n[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1) return false; const bool lp = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; return d <= (m == 2 && lp ? 29 : n[m - 1]); }
inline long long make_us(long long y, long long mo, long long d, long long h, long long mi, long long s, long long us, long long off_s) { return ((civil_days(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) - 719468) * 86400 + h * 3600 + mi * 60 + s - off_s) * 1000000 + us; }
// datetime.date(y, m, d) as days since 1970-01-01, with Python's ValueErrors; date_iso is date.isoformat()
inline long long py_date(long long y, long long m, long long d) {
    if (y < 1 || y > 9999) throw PyErr("ValueError", "year " + std::to_string(y) + " is out of range");
    if (m < 1 || m > 12) throw PyErr("ValueError", "month must be in 1..12");
    static const int ML[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (d < 1 || d > (m == 2 && leap ? 29 : ML[m - 1])) throw PyErr("ValueError", "day is out of range for month");
    return civil_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d)) - 719468;
}
inline std::string date_iso(long long days) { long long y; unsigned m, d; civil_from(days, y, m, d); char b[48]; std::snprintf(b, sizeof b, "%04lld-%02u-%02u", y, m, d); return b; }
inline long long now_us() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
inline void split_us(long long t, long long& y, unsigned& mo, unsigned& d, int& h, int& mi, int& s, long long& us) {
    long long secs = t >= 0 ? t / 1000000 : -((-t + 999999) / 1000000); us = t - secs * 1000000;
    long long days = secs >= 0 ? secs / 86400 : -((-secs + 86399) / 86400); long long rem = secs - days * 86400;
    civil_from(days, y, mo, d); h = static_cast<int>(rem / 3600); mi = static_cast<int>(rem % 3600 / 60); s = static_cast<int>(rem % 60);
}
// dt.isoformat() for a UTC datetime: 2026-10-13T19:46:52+00:00 (".ffffff" only when there are microseconds)
inline std::string iso_utc(long long t) {
    long long y, us; unsigned mo, d; int h, mi, s; split_us(t, y, mo, d, h, mi, s, us); char b[64];
    if (us) std::snprintf(b, sizeof b, "%04lld-%02u-%02uT%02d:%02d:%02d.%06lld+00:00", y, mo, d, h, mi, s, us);
    else std::snprintf(b, sizeof b, "%04lld-%02u-%02uT%02d:%02d:%02d+00:00", y, mo, d, h, mi, s);
    return b;
}
inline std::string utc_date(long long t) { return iso_utc(t).substr(0, 10); }
inline std::string utc_hm(long long t) { return iso_utc(t).substr(0, 10) + " " + iso_utc(t).substr(11, 5) + " UTC"; }   // strftime("%Y-%m-%d %H:%M UTC")
inline std::optional<long long> py_int(const std::string& s0) {   // int(str): optional sign, digits, surrounding whitespace, underscores between digits
    const std::string s = strip(s0); std::size_t k = 0; bool neg = false;
    if (k < s.size() && (s[k] == '+' || s[k] == '-')) { neg = s[k] == '-'; k++; }
    if (k >= s.size()) return std::nullopt;
    long long v = 0; bool prev_digit = false;
    for (; k < s.size(); ++k) { if (std::isdigit(static_cast<unsigned char>(s[k]))) { v = v * 10 + (s[k] - '0'); prev_digit = true; if (v > 100000000000000LL) return std::nullopt; } else if (s[k] == '_' && prev_digit && k + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[k + 1]))) prev_digit = false; else return std::nullopt; }
    return neg ? -v : v;
}
// email.utils.parsedate_to_datetime (via _parsedate_tz) -> UTC microseconds; nullopt where Python raises
inline std::optional<long long> parsedate_us(const std::string& value) {
    static const char* MONTHS[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec", "january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december"};
    static const char* DAYS[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    static const std::map<std::string, int> TZ = {{"UT", 0}, {"UTC", 0}, {"GMT", 0}, {"Z", 0}, {"AST", -400}, {"ADT", -300}, {"EST", -500}, {"EDT", -400}, {"CST", -600}, {"CDT", -500}, {"MST", -700}, {"MDT", -600}, {"PST", -800}, {"PDT", -700}};
    std::vector<std::string> data = split_ws(value);
    if (data.empty()) return std::nullopt;
    bool dayname = false; for (const char* d : DAYS) if (lower(data[0]) == d) dayname = true;
    if (ends(data[0], ",") || dayname) data.erase(data.begin());
    else { const auto k = data[0].rfind(','); if (k != std::string::npos) data[0] = data[0].substr(k + 1); }
    if (data.size() == 3) { const auto stuff = split(data[0], "-"); if (stuff.size() == 3) { std::vector<std::string> nd = stuff; nd.insert(nd.end(), data.begin() + 1, data.end()); data = nd; } }
    if (data.size() == 4) { const std::string s = data[3]; auto k = s.find('+'); if (k == std::string::npos) k = s.find('-'); if (k != std::string::npos && k > 0) { data[3] = s.substr(0, k); data.push_back(s.substr(k)); } else data.push_back(""); }
    if (data.size() < 5) return std::nullopt;
    std::string dd = data[0], mm = data[1], yy = data[2], tm = data[3], tz = data[4];
    if (dd.empty() || mm.empty() || yy.empty()) return std::nullopt;
    const auto month = [&](const std::string& x) { for (int k = 0; k < 24; ++k) if (x == MONTHS[k]) return k; return -1; };
    mm = lower(mm); int mi = month(mm);
    if (mi < 0) { std::swap(dd, mm); mm = lower(mm); mi = month(mm); if (mi < 0) return std::nullopt; }
    int mon = mi + 1; if (mon > 12) mon -= 12;
    if (!dd.empty() && dd.back() == ',') dd.pop_back();
    if (const auto k = yy.find(':'); k != std::string::npos && k > 0) std::swap(yy, tm);
    if (!yy.empty() && yy.back() == ',') yy.pop_back();
    if (yy.empty()) return std::nullopt;
    if (!std::isdigit(static_cast<unsigned char>(yy[0]))) std::swap(yy, tz);
    if (!tm.empty() && tm.back() == ',') tm.pop_back();
    std::vector<std::string> t = split(tm, ":"); std::string thh, tmm, tss = "0";
    if (t.size() == 2) { thh = t[0]; tmm = t[1]; }
    else if (t.size() == 3) { thh = t[0]; tmm = t[1]; tss = t[2]; }
    else if (t.size() == 1 && t[0].find('.') != std::string::npos) { t = split(t[0], "."); if (t.size() == 2) { thh = t[0]; tmm = t[1]; } else if (t.size() == 3) { thh = t[0]; tmm = t[1]; tss = t[2]; } else return std::nullopt; }
    else return std::nullopt;
    const auto Y = py_int(yy), D = py_int(dd), H = py_int(thh), M = py_int(tmm), S = py_int(tss);
    if (!Y || !D || !H || !M || !S) return std::nullopt;
    long long y = *Y; if (y < 100) y += y > 68 ? 1900 : 2000;
    std::optional<long long> off; const std::string TZU = upper(tz);
    if (const auto it = TZ.find(TZU); it != TZ.end()) off = it->second;
    else { off = py_int(TZU); if (off && *off == 0 && starts(TZU, "-")) off.reset(); }
    long long off_s = 0;
    if (off && *off) { const long long sign = *off < 0 ? -1 : 1; const long long a = *off < 0 ? -*off : *off; off_s = sign * ((a / 100) * 3600 + (a % 100) * 60); }
    if (!valid_ymd(y, mon, *D) || *H < 0 || *H > 23 || *M < 0 || *M > 59 || *S < 0 || *S > 59) return std::nullopt;   // datetime() raises ValueError
    if (off_s <= -86400 || off_s >= 86400) return std::nullopt;
    return make_us(y, mon, *D, *H, *M, *S, 0, off_s);
}
// datetime.fromisoformat (3.11+) for the common forms -> UTC microseconds (naive = UTC)
inline std::optional<long long> fromiso_us(const std::string& v) {
    const auto dig = [&](std::size_t a, std::size_t n) -> long long { if (a + n > v.size()) return -1; long long x = 0; for (std::size_t k = a; k < a + n; ++k) { if (!std::isdigit(static_cast<unsigned char>(v[k]))) return -1; x = x * 10 + (v[k] - '0'); } return x; };
    std::size_t i; long long y, mo, d;
    if (v.size() >= 10 && v[4] == '-' && v[7] == '-') { y = dig(0, 4); mo = dig(5, 2); d = dig(8, 2); i = 10; }
    else { y = dig(0, 4); mo = dig(4, 2); d = dig(6, 2); i = 8; }
    if (y < 0 || mo < 0 || d < 0 || !valid_ymd(y, mo, d)) return std::nullopt;
    long long h = 0, mi = 0, s = 0, us = 0, off = 0;
    if (i < v.size()) {
        i++;   // any single separator
        h = dig(i, 2); if (h < 0) return std::nullopt; i += 2;
        if (i < v.size() && v[i] == ':') { mi = dig(i + 1, 2); if (mi < 0) return std::nullopt; i += 3; if (i < v.size() && v[i] == ':') { s = dig(i + 1, 2); if (s < 0) return std::nullopt; i += 3; } }
        else if (dig(i, 2) >= 0) { mi = dig(i, 2); i += 2; if (dig(i, 2) >= 0) { s = dig(i, 2); i += 2; } }
        if (i < v.size() && (v[i] == '.' || v[i] == ',')) { std::size_t k = ++i; std::string f; while (k < v.size() && std::isdigit(static_cast<unsigned char>(v[k]))) f += v[k++]; if (f.empty()) return std::nullopt; i = k; f = (f + "000000").substr(0, 6); us = std::stoll(f); }
        if (i < v.size()) {
            if (v[i] == 'Z') { i++; }
            else if (v[i] == '+' || v[i] == '-') {
                const long long sign = v[i] == '-' ? -1 : 1; i++; const long long oh = dig(i, 2); if (oh < 0) return std::nullopt; i += 2; long long om = 0, os = 0;
                if (i < v.size() && v[i] == ':') { om = dig(i + 1, 2); if (om < 0) return std::nullopt; i += 3; if (i < v.size() && v[i] == ':') { os = dig(i + 1, 2); if (os < 0) return std::nullopt; i += 3; } }
                else if (dig(i, 2) >= 0) { om = dig(i, 2); i += 2; }
                if (oh > 23 || om > 59 || os > 59) return std::nullopt;
                off = sign * (oh * 3600 + om * 60 + os);
            } else return std::nullopt;
            if (i != v.size()) return std::nullopt;
        }
        if (h > 24 || mi > 59 || s > 59) return std::nullopt;
        if (h == 24) { if (mi || s || us) return std::nullopt; }
    }
    return make_us(y, mo, d, h, mi, s, us, off);
}
}  // namespace pyrt
namespace pyrt {
inline std::string read_bytes(const std::string& path) {   // open(path, "rb").read()
    return read_text(path, false, true);
}
}  // namespace pyrt
namespace pyrt {
// str.splitlines(): \n \r \r\n \v \f \x1c \x1d \x1e \x85 \u2028 \u2029 end a line
inline std::vector<std::string> splitlines(const std::string& s) {
    std::vector<std::string> out; std::string cur; const auto cs = cps(s);
    for (std::size_t k = 0; k < cs.size(); ++k) {
        const uint32_t c = cp_of(cs[k]);
        if (c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || c == 0x1C || c == 0x1D || c == 0x1E || c == 0x85 || c == 0x2028 || c == 0x2029) {
            out.push_back(cur); cur.clear();
            if (c == '\r' && k + 1 < cs.size() && cs[k + 1] == "\n") ++k;
        } else cur += cs[k];
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
inline std::string rstrip(const std::string& s) {   // str.rstrip()
    const auto cs = cps(s); std::size_t n = cs.size(); while (n && py_space(cp_of(cs[n - 1]))) --n;
    std::string o; for (std::size_t k = 0; k < n; ++k) o += cs[k]; return o;
}
}  // namespace pyrt
namespace pyrt {
// bytes.decode("utf-8", "replace"): each maximal invalid subpart becomes U+FFFD
inline std::string decode_utf8_replace(const std::string& b) {
    std::string o; o.reserve(b.size()); const std::size_t n = b.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(b[i]);
        if (c < 0x80) { o += static_cast<char>(c); ++i; continue; }
        int need = 0; unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else { o += "\xEF\xBF\xBD"; ++i; continue; }
        std::size_t k = i + 1; int got = 0;
        while (got < need && k < n) {
            const unsigned char d = static_cast<unsigned char>(b[k]);
            if (got == 0 ? (d < lo || d > hi) : (d < 0x80 || d > 0xBF)) break;
            ++k; ++got;
        }
        if (got == need) o.append(b, i, k - i); else o += "\xEF\xBF\xBD";
        i = k;
    }
    return o;
}
// UnicodeDecodeError text for bytes that are not UTF-8 (strict decoding), or nothing when they are
inline std::optional<std::string> utf8_decode_error(const std::string& b) {
    const std::size_t n = b.size(); char m[160];
    auto byte = [&](std::size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(b[i])); };
    for (std::size_t i = 0; i < n;) {
        const unsigned c = byte(i);
        if (c < 0x80) { ++i; continue; }
        int need; unsigned lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else { std::snprintf(m, sizeof m, "'utf-8' codec can't decode byte 0x%02x in position %zu: invalid start byte", c, i); return std::string(m); }
        std::size_t k = 1;
        for (; k <= static_cast<std::size_t>(need); ++k) {
            const char* why = nullptr;
            if (i + k >= n) why = "unexpected end of data";
            else {
                const unsigned d = byte(i + k);
                if (!(k == 1 ? (d >= lo && d <= hi) : (d >= 0x80 && d <= 0xBF))) why = "invalid continuation byte";
            }
            if (why) {
                if (k == 1) std::snprintf(m, sizeof m, "'utf-8' codec can't decode byte 0x%02x in position %zu: %s", c, i, why);
                else std::snprintf(m, sizeof m, "'utf-8' codec can't decode bytes in position %zu-%zu: %s", i, i + k - 1, why);
                return std::string(m);
            }
        }
        i += k;
    }
    return std::nullopt;
}
// open(path, encoding="utf-8[-sig]", errors="strict"|"replace").read(): decode, then universal newlines unless raw
inline std::string read_text_utf8(const std::string& path, bool replace = false, bool sig = false, bool raw = false) {
    std::string t = read_text(path, false, true);
    if (sig && t.compare(0, 3, "\xEF\xBB\xBF") == 0) t.erase(0, 3);
    if (replace) t = decode_utf8_replace(t);
    else if (const auto bad = utf8_decode_error(t)) throw PyErr("UnicodeDecodeError", *bad);
    if (!raw) {
        std::string o; o.reserve(t.size());
        for (std::size_t k = 0; k < t.size(); ++k) { if (t[k] == '\r') { o += '\n'; if (k + 1 < t.size() && t[k + 1] == '\n') ++k; } else o += t[k]; }
        t.swap(o);
    }
    return t;
}
}  // namespace pyrt
