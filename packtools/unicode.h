// unicode.h: Python's unicodedata.category / combining / normalize for the pack tools, on UTF-8 std::strings.
// The tables come from tools/unicode/gen_unicode.cpp (Unicode 15.1.0); the algorithms are the ones in UAX #15.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "unicode_data.h"

namespace pyrt::uni {
inline std::vector<uint32_t> to_u32(const std::string& s) {   // UTF-8 -> code points (bad bytes pass through one by one)
    std::vector<uint32_t> o; o.reserve(s.size());
    const auto* u = reinterpret_cast<const unsigned char*>(s.data()); const std::size_t n = s.size();
    for (std::size_t k = 0; k < n;) {
        const unsigned char c = u[k];
        const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        if (len == 1 || k + len > n) { o.push_back(c); ++k; continue; }
        uint32_t cp = len == 2 ? (c & 0x1Fu) : len == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (std::size_t j = 1; j < len; ++j) cp = (cp << 6) | (u[k + j] & 0x3Fu);
        o.push_back(cp); k += len;
    }
    return o;
}
inline std::string from_u32(const std::vector<uint32_t>& v) {
    std::string o; o.reserve(v.size());
    for (uint32_t c : v) {
        if (c < 0x80) o += static_cast<char>(c);
        else if (c < 0x800) { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += static_cast<char>(0xE0 | (c >> 12)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
        else { o += static_cast<char>(0xF0 | (c >> 18)); o += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return o;
}
// unicodedata.category(chr(c)): "Lu", "Cf", ... ("Cn" when unassigned)
inline const char* category(uint32_t c) {
    if (c > 0x10FFFF) return "Cn";
    const auto* b = std::begin(data::CAT); const auto* e = std::end(data::CAT);
    const auto* it = std::upper_bound(b, e, c, [](uint32_t x, const data::CatRun& r) { return x < r.lo; });
    return data::CAT_NAMES[(it - 1)->cat];
}
inline int combining(uint32_t c) {   // unicodedata.combining(chr(c))
    const auto* b = std::begin(data::CCC); const auto* e = std::end(data::CCC);
    const auto* it = std::upper_bound(b, e, c, [](uint32_t x, const data::CccRun& r) { return x < r.lo; });
    if (it == b) return 0;
    --it;
    return c <= it->hi ? it->ccc : 0;
}
inline const data::Dec* dec_of(uint32_t c) {
    const auto* b = std::begin(data::DEC); const auto* e = std::end(data::DEC);
    const auto* it = std::lower_bound(b, e, c, [](const data::Dec& d, uint32_t x) { return d.cp < x; });
    return it != e && it->cp == c ? it : nullptr;
}
inline constexpr uint32_t SBASE = 0xAC00, LBASE = 0x1100, VBASE = 0x1161, TBASE = 0x11A7, LCOUNT = 19, VCOUNT = 21, TCOUNT = 28, NCOUNT = VCOUNT * TCOUNT, SCOUNT = LCOUNT * NCOUNT;
inline void decompose(uint32_t c, bool compat, std::vector<uint32_t>& out) {
    if (c >= SBASE && c < SBASE + SCOUNT) {
        const uint32_t s = c - SBASE;
        out.push_back(LBASE + s / NCOUNT); out.push_back(VBASE + (s % NCOUNT) / TCOUNT);
        if (s % TCOUNT) out.push_back(TBASE + s % TCOUNT);
        return;
    }
    const data::Dec* d = dec_of(c);
    if (!d || (!compat && (d->info >> 25 & 1))) { out.push_back(c); return; }
    const uint32_t off = d->info & 0xFFFFF, len = d->info >> 20 & 0x1F;
    for (uint32_t k = 0; k < len; ++k) decompose(data::POOL[off + k], compat, out);
}
inline void canonical_order(std::vector<uint32_t>& v) {   // stable sort of each run of non-starters by combining class
    for (std::size_t k = 0; k < v.size();) {
        if (!combining(v[k])) { ++k; continue; }
        std::size_t j = k; while (j < v.size() && combining(v[j])) ++j;
        std::stable_sort(v.begin() + static_cast<long>(k), v.begin() + static_cast<long>(j), [](uint32_t a, uint32_t b) { return combining(a) < combining(b); });
        k = j;
    }
}
inline const std::map<std::pair<uint32_t, uint32_t>, uint32_t>& compositions() {
    static const auto m = [] {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> o;
        for (const auto& d : data::DEC) {
            const uint32_t len = d.info >> 20 & 0x1F;
            if ((d.info >> 25 & 1) || len != 2) continue;   // compatibility mappings and singletons never compose
            if (std::find(std::begin(data::EXCL), std::end(data::EXCL), d.cp) != std::end(data::EXCL)) continue;
            const uint32_t a = data::POOL[d.info & 0xFFFFF], b = data::POOL[(d.info & 0xFFFFF) + 1];
            if (combining(d.cp) || combining(a)) continue;   // non-starter decompositions
            o[{a, b}] = d.cp;
        }
        return o;
    }();
    return m;
}
inline uint32_t compose_pair(uint32_t a, uint32_t b) {   // 0 = no primary composite
    if (a >= LBASE && a < LBASE + LCOUNT && b >= VBASE && b < VBASE + VCOUNT) return SBASE + ((a - LBASE) * VCOUNT + (b - VBASE)) * TCOUNT;
    if (a >= SBASE && a < SBASE + SCOUNT && (a - SBASE) % TCOUNT == 0 && b > TBASE && b < TBASE + TCOUNT) return a + (b - TBASE);
    const auto& comp = compositions();
    const auto it = comp.find({a, b});
    return it == comp.end() ? 0 : it->second;
}
inline void compose(std::vector<uint32_t>& v) {   // canonical composition (UAX #15): v is already decomposed and ordered
    std::vector<uint32_t> o; o.reserve(v.size());
    std::size_t starter = SIZE_MAX;
    int last_cc = -1;   // class of the last character kept since the starter (-1: none, the starter is adjacent)
    for (uint32_t c : v) {
        const int cc = combining(c);
        if (starter != SIZE_MAX && !(last_cc != -1 && last_cc >= cc)) {
            if (const uint32_t x = compose_pair(o[starter], c)) { o[starter] = x; continue; }
        }
        if (cc == 0) { starter = o.size(); last_cc = -1; }
        else last_cc = cc;
        o.push_back(c);
    }
    v.swap(o);
}
// unicodedata.normalize(form, s) for form in NFC, NFD, NFKC, NFKD
inline std::string normalize(const std::string& form, const std::string& s) {
    if (form != "NFC" && form != "NFD" && form != "NFKC" && form != "NFKD") throw std::invalid_argument("invalid normalization form");
    const bool compat = form[2] == 'K';
    std::vector<uint32_t> v;
    for (uint32_t c : to_u32(s)) decompose(c, compat, v);
    canonical_order(v);
    if (form.back() == 'C') compose(v);
    return from_u32(v);
}
}  // namespace pyrt::uni
