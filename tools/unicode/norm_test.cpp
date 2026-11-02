// norm_test.cpp: runs Unicode's NormalizationTest.txt (the UAX #15 conformance file) against packtools/unicode.h.
//   g++ -std=c++20 -O1 -I../.. norm_test.cpp -o /tmp/norm_test && /tmp/norm_test NormalizationTest.txt
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "packtools/unicode.h"
using namespace pyrt::uni;
static std::vector<uint32_t> hexes(const std::string& s) { std::vector<uint32_t> o; std::istringstream ss(s); std::string t; while (ss >> t) o.push_back(static_cast<uint32_t>(std::stoul(t, nullptr, 16))); return o; }
int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "NormalizationTest.txt");
    if (!f) { std::cerr << "no NormalizationTest.txt\n"; return 2; }
    std::string line; long cases = 0, bad = 0;
    std::vector<bool> listed(0x110000, false); bool part1 = false;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '@') { part1 = line.rfind("@Part1", 0) == 0; continue; }
        std::vector<std::vector<uint32_t>> c;
        std::size_t k = 0;
        for (int i = 0; i < 5; ++i) { const std::size_t j = line.find(';', k); c.push_back(hexes(line.substr(k, j - k))); k = j + 1; }
        if (part1) listed[c[0][0]] = true;
        const std::string src = from_u32(c[0]);
        // c2 == NFC(c1..c3), c4 == NFC(c4,c5); c3 == NFD(c1..c3), c5 == NFD(c4,c5); c4 == NFKC(all); c5 == NFKD(all)
        const auto chk = [&](const char* form, int col, int want) {
            if (to_u32(normalize(form, from_u32(c[static_cast<std::size_t>(col)]))) != c[static_cast<std::size_t>(want)]) {
                if (++bad <= 10) std::cerr << "FAIL " << form << " col " << col + 1 << ": " << line.substr(0, 60) << "\n";
            }
        };
        for (int col : {0, 1, 2}) { chk("NFC", col, 1); chk("NFD", col, 2); }
        for (int col : {3, 4}) { chk("NFC", col, 3); chk("NFD", col, 4); }
        for (int col = 0; col < 5; ++col) { chk("NFKC", col, 3); chk("NFKD", col, 4); }
        ++cases; (void)src;
    }
    // every code point not in Part 1 is unchanged by all four forms
    long inv = 0;
    for (uint32_t cp = 0; cp < 0x110000; ++cp) {
        if (listed[cp] || (cp >= 0xD800 && cp <= 0xDFFF)) continue;
        const std::string s = from_u32({cp});
        for (const char* form : {"NFC", "NFD", "NFKC", "NFKD"}) if (normalize(form, s) != s) { if (++bad <= 20) std::fprintf(stderr, "FAIL invariant %04X %s\n", cp, form); }
        ++inv;
    }
    std::printf("NormalizationTest: %ld lines, %ld unlisted code points, %ld failures\n", cases, inv, bad);
    return bad ? 1 : 0;
}
