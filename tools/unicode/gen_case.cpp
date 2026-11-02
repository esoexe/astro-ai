// gen_case.cpp: builds packtools/unicode_case.h from the Unicode Character Database, so pyrt's str.lower(), str.upper()
// and str.title() map case the way Python's do: full mappings (SpecialCasing.txt's unconditional entries, e.g.
// "ß".upper() == "SS"), the simple mappings of UnicodeData.txt otherwise (a missing title mapping means the upper one,
// as in CPython's makeunicodedata.py), and the Cased / Case_Ignorable properties that str.title() and the final-sigma
// rule of str.lower() read; and the code points with a digit value, which is what str.isdigit() tests.
// It also writes the character classes pyre.h (Python's re for str patterns) and str.is*() need: DECIMAL (field 6:
// re's \d, str.isdecimal), NUMERIC (field 8), ALPHA (categories Lu Ll Lt Lm Lo), TITLE (Lt), UPPER / LOWER (the
// Uppercase / Lowercase properties), and EXTRA_CASES: CPython's re/_casefix.py, rebuilt the way
// Tools/build/generate_re_casefix.py builds it (lowercase letters that share an uppercase, e.g. s and long s).
// NUMERIC leaves out the Unihan-only numeric values CPython adds (CJK ideographs, which are ALPHA anyway).
//
//   cd tools/unicode
//   curl -sfLO https://www.unicode.org/Public/15.1.0/ucd/SpecialCasing.txt          (UnicodeData.txt as for gen_unicode.cpp)
//   curl -sfL https://www.unicode.org/Public/15.1.0/ucd/DerivedCoreProperties.txt -o /tmp/DerivedCoreProperties.txt
//   g++ -std=c++20 -O1 gen_case.cpp -o /tmp/gen_case && /tmp/gen_case . /tmp/DerivedCoreProperties.txt ../../packtools/unicode_case.h
//
// The data files are (c) Unicode, Inc., under the Unicode License v3 (see THIRD-PARTY-NOTICES.md).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> o; std::string cur;
    for (char ch : s) { if (ch == sep) { o.push_back(cur); cur.clear(); } else cur += ch; }
    o.push_back(cur);
    return o;
}
static std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}
static std::vector<uint32_t> cps(const std::string& s) {
    std::vector<uint32_t> o; std::istringstream in(s); std::string h;
    while (in >> h) o.push_back(static_cast<uint32_t>(std::stoul(h, nullptr, 16)));
    return o;
}
static void ranges(std::ostream& out, const char* name, const std::vector<bool>& on) {
    out << "inline constexpr Range " << name << "[] = {\n";
    int n = 0;
    for (uint32_t c = 0; c < 0x110000;) {
        if (!on[c]) { c++; continue; }
        uint32_t e = c; while (e + 1 < 0x110000 && on[e + 1]) e++;
        char b[40]; std::snprintf(b, sizeof b, "{0x%x,0x%x},", c, e); out << b; if (++n % 8 == 0) out << "\n";
        c = e + 1;
    }
    out << "\n};\n";
}

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: gen_case UCD_DIR DerivedCoreProperties.txt OUT.h\n"; return 2; }
    const std::string dir = argv[1];
    std::ifstream ud(dir + "/UnicodeData.txt"), sc(dir + "/SpecialCasing.txt"), dp(argv[2]);
    if (!ud || !sc || !dp) { std::cerr << "missing UnicodeData.txt / SpecialCasing.txt / DerivedCoreProperties.txt\n"; return 1; }
    struct M { uint32_t lo = 0, up = 0, ti = 0; };
    std::map<uint32_t, M> simple;   // only code points with some mapping
    std::vector<bool> digit(0x110000, false);   // a digit value (field 7): CPython's str.isdigit()
    std::vector<bool> decimal(0x110000, false), numeric(0x110000, false), alpha(0x110000, false), titlecat(0x110000, false);
    uint32_t range_first = 0;
    std::string line;
    while (std::getline(ud, line)) {
        const auto f = split(line, ';');
        if (f.size() < 15) continue;
        const uint32_t c = static_cast<uint32_t>(std::stoul(f[0], nullptr, 16));
        if (f[1].find(", First>") != std::string::npos) range_first = c;
        const uint32_t lo_cp = f[1].find(", Last>") != std::string::npos ? range_first : c;
        const auto mark = [&](std::vector<bool>& v) { for (uint32_t k = lo_cp; k <= c; ++k) v[k] = true; };
        if (!f[7].empty()) mark(digit);
        if (!f[6].empty()) mark(decimal);
        if (!f[8].empty()) mark(numeric);
        if (f[2] == "Lu" || f[2] == "Ll" || f[2] == "Lt" || f[2] == "Lm" || f[2] == "Lo") mark(alpha);
        if (f[2] == "Lt") mark(titlecat);
        M m;
        m.up = f[12].empty() ? c : static_cast<uint32_t>(std::stoul(f[12], nullptr, 16));
        m.lo = f[13].empty() ? c : static_cast<uint32_t>(std::stoul(f[13], nullptr, 16));
        m.ti = f[14].empty() ? m.up : static_cast<uint32_t>(std::stoul(f[14], nullptr, 16));
        if (m.up != c || m.lo != c || m.ti != c) simple[c] = m;
    }
    struct S { std::vector<uint32_t> lo, ti, up; };
    std::map<uint32_t, S> special;
    while (std::getline(sc, line)) {
        const auto hash = line.find('#');
        const std::string body = trim(hash == std::string::npos ? line : line.substr(0, hash));
        if (body.empty()) continue;
        const auto f = split(body, ';');
        if (f.size() < 5) continue;
        if (!trim(f[4]).empty()) continue;   // conditional (language or context): CPython leaves these out
        const uint32_t c = static_cast<uint32_t>(std::stoul(trim(f[0]), nullptr, 16));
        special[c] = S{cps(f[1]), cps(f[2]), cps(f[3])};
    }
    std::vector<bool> cased(0x110000, false), ignorable(0x110000, false), upperp(0x110000, false), lowerp(0x110000, false);
    while (std::getline(dp, line)) {
        const auto hash = line.find('#');
        const std::string body = trim(hash == std::string::npos ? line : line.substr(0, hash));
        if (body.empty()) continue;
        const auto f = split(body, ';');
        if (f.size() < 2) continue;
        const std::string prop = trim(f[1]), r = trim(f[0]);
        std::vector<bool>* v = prop == "Cased" ? &cased : prop == "Case_Ignorable" ? &ignorable : prop == "Uppercase" ? &upperp : prop == "Lowercase" ? &lowerp : nullptr;
        if (!v) continue;
        const auto dots = r.find("..");
        const uint32_t a = static_cast<uint32_t>(std::stoul(r.substr(0, dots), nullptr, 16));
        const uint32_t b = dots == std::string::npos ? a : static_cast<uint32_t>(std::stoul(r.substr(dots + 2), nullptr, 16));
        for (uint32_t c = a; c <= b; ++c) (*v)[c] = true;
    }
    std::ofstream out(argv[3]);
    out << "// Generated by tools/unicode/gen_case.cpp from the Unicode Character Database 15.1.0 (UnicodeData.txt,\n"
           "// SpecialCasing.txt, DerivedCoreProperties.txt; (c) Unicode, Inc., Unicode License v3). Do not edit: re-run the generator.\n"
           "#pragma once\n#include <cstdint>\nnamespace pyrt::ucase {\n"
           "struct Simple { uint32_t cp, lo, up, ti; };   // simple mappings (identity where a field has none)\n"
           "inline constexpr Simple SIMPLE[] = {\n";
    int n = 0;
    for (const auto& [c, m] : simple) {
        char b[64]; std::snprintf(b, sizeof b, "{0x%x,0x%x,0x%x,0x%x},", c, m.lo, m.up, m.ti); out << b; if (++n % 6 == 0) out << "\n";
    }
    out << "\n};\n// unconditional full mappings: up to 3 code points each, 0 = unused\n"
           "struct Special { uint32_t cp; uint32_t lo[3], ti[3], up[3]; };\ninline constexpr Special SPECIAL[] = {\n";
    const auto three = [](const std::vector<uint32_t>& v) {
        std::string s = "{"; for (int k = 0; k < 3; ++k) { char b[16]; std::snprintf(b, sizeof b, "%s0x%x", k ? "," : "", k < static_cast<int>(v.size()) ? v[k] : 0u); s += b; }
        return s + "}";
    };
    for (const auto& [c, s] : special) {
        if (s.lo.size() > 3 || s.ti.size() > 3 || s.up.size() > 3) { std::cerr << "mapping longer than 3\n"; return 1; }
        char b[16]; std::snprintf(b, sizeof b, "{0x%x,", c);
        out << b << three(s.lo) << "," << three(s.ti) << "," << three(s.up) << "},\n";
    }
    out << "};\nstruct Range { uint32_t lo, hi; };\n";
    ranges(out, "CASED", cased);
    ranges(out, "CASE_IGNORABLE", ignorable);
    ranges(out, "DIGIT", digit);
    ranges(out, "DECIMAL", decimal);
    ranges(out, "NUMERIC", numeric);
    ranges(out, "ALPHA", alpha);
    ranges(out, "TITLE", titlecat);
    ranges(out, "UPPER", upperp);
    ranges(out, "LOWER", lowerp);
    // EXTRA_CASES: group code points by their full uppercase; in groups of 2+, the distinct full lowercases (all
    // single code points) that differ are case-insensitive equals for re.
    const auto full = [&](uint32_t c, bool up) {
        if (auto it = special.find(c); it != special.end()) return up ? it->second.up : it->second.lo;
        auto it = simple.find(c); return std::vector<uint32_t>{it == simple.end() ? c : (up ? it->second.up : it->second.lo)};
    };
    std::map<std::vector<uint32_t>, std::vector<uint32_t>> groups;
    for (uint32_t c = 0; c < 0x110000; ++c) groups[full(c, true)].push_back(c);
    std::map<uint32_t, std::vector<uint32_t>> extra;
    for (const auto& [u, members] : groups) {
        if (members.size() < 2) continue;
        std::vector<uint32_t> los;
        for (uint32_t c : members) { const auto l = full(c, false); if (l.size() != 1) { std::cerr << "multi-char lower in a case group\n"; return 1; } if (std::find(los.begin(), los.end(), l[0]) == los.end()) los.push_back(l[0]); }
        if (los.size() < 2) continue;
        std::sort(los.begin(), los.end());
        for (uint32_t i : los) for (uint32_t j : los) if (i != j) extra[i].push_back(j);
    }
    out << "struct Extra { uint32_t cp, a, b; };   // re's _EXTRA_CASES: up to two other lowercase equals, 0 = none\ninline constexpr Extra EXTRA_CASES[] = {\n";
    n = 0;
    for (const auto& [c, v] : extra) {
        if (v.size() > 2) { std::cerr << "more than 2 extra cases\n"; return 1; }
        char b[48]; std::snprintf(b, sizeof b, "{0x%x,0x%x,0x%x},", c, v[0], v.size() > 1 ? v[1] : 0u); out << b; if (++n % 6 == 0) out << "\n";
    }
    out << "\n};\n}  // namespace pyrt::ucase\n";
    std::cerr << extra.size() << " extra cases\n";
    std::cerr << simple.size() << " simple, " << special.size() << " special\n";
    return 0;
}
