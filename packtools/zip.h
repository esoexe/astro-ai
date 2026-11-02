// zip.h - read members of a ZIP archive in memory (zipfile.ZipFile(...).namelist() / .read(name)) for the built-in
// pack tools. Stored and deflate members, CRC-32 checked, ZIP64 sizes. Self-contained: own inflate (RFC 1951).
#pragma once
#include "pyrt.h"
#include <array>
#include <cstdint>
namespace pyrt::zip {
struct BadZipFile : PyErr { explicit BadZipFile(const std::string& m) : PyErr("zipfile.BadZipFile", m) {} };
inline uint32_t crc32(const std::string& s) {
    static const auto table = [] { std::array<uint32_t, 256> t{}; for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } return t; }();
    uint32_t c = 0xFFFFFFFFu; for (unsigned char ch : s) c = table[(c ^ ch) & 0xFF] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}
// ---- inflate (raw deflate stream)
struct Inflate {
    const unsigned char* p; std::size_t n, pos = 0; uint32_t bitbuf = 0; int bitcnt = 0; std::string out;
    Inflate(const std::string& s, std::size_t a, std::size_t len) : p(reinterpret_cast<const unsigned char*>(s.data()) + a), n(len) {}
    [[noreturn]] static void bad(const char* what) { throw PyErr("zlib.error", std::string("Error -3 while decompressing data: ") + what); }
    int bits(int need) {
        while (bitcnt < need) { if (pos >= n) bad("incomplete or truncated stream"); bitbuf |= static_cast<uint32_t>(p[pos++]) << bitcnt; bitcnt += 8; }
        const int v = static_cast<int>(bitbuf & ((1u << need) - 1)); bitbuf >>= need; bitcnt -= need; return v;
    }
    struct Huff { std::array<short, 16> count{}; std::vector<short> sym; };
    static Huff build(const short* lengths, int num) {
        Huff h; h.sym.assign(static_cast<std::size_t>(num), 0);
        for (int s = 0; s < num; ++s) h.count[static_cast<std::size_t>(lengths[s])]++;
        h.count[0] = 0;
        std::array<short, 16> offs{}; for (int len = 1; len < 16; ++len) offs[static_cast<std::size_t>(len)] = static_cast<short>(offs[static_cast<std::size_t>(len - 1)] + h.count[static_cast<std::size_t>(len - 1)]);
        for (int s = 0; s < num; ++s) if (lengths[s]) h.sym[static_cast<std::size_t>(offs[static_cast<std::size_t>(lengths[s])]++)] = static_cast<short>(s);
        return h;
    }
    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1); const int count = h.count[static_cast<std::size_t>(len)];
            if (code - count < first) return h.sym[static_cast<std::size_t>(index + (code - first))];
            index += count; first += count; first <<= 1; code <<= 1;
        }
        bad("invalid code");
    }
    void codes(const Huff& lit, const Huff& dist) {
        static const short LBASE[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short LEXT[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short DBASE[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short DEXT[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int s = decode(lit);
            if (s < 256) { out += static_cast<char>(s); continue; }
            if (s == 256) return;
            s -= 257; if (s >= 29) bad("invalid literal/length code");
            const int len = LBASE[s] + bits(LEXT[s]);
            const int ds = decode(dist); if (ds >= 30) bad("invalid distance code");
            const std::size_t d = static_cast<std::size_t>(DBASE[ds] + bits(DEXT[ds]));
            if (d > out.size()) bad("invalid distance too far back");
            const std::size_t from = out.size() - d; for (int k = 0; k < len; ++k) out += out[from + static_cast<std::size_t>(k)];
        }
    }
    void fixed() {
        static const auto tabs = [] { short l[288]; for (int s = 0; s < 288; ++s) l[s] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8; short d[30]; for (short& x : d) x = 5; return std::make_pair(build(l, 288), build(d, 30)); }();
        codes(tabs.first, tabs.second);
    }
    void dynamic() {
        static const short ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) bad("too many length or distance symbols");
        short lengths[320] = {};
        for (int k = 0; k < ncode; ++k) lengths[ORDER[k]] = static_cast<short>(bits(3));
        const Huff lencode = build(lengths, 19);
        for (int k = 0; k < 320; ++k) lengths[k] = 0;
        for (int idx = 0; idx < nlen + ndist;) {
            int sym = decode(lencode);
            if (sym < 16) { lengths[idx++] = static_cast<short>(sym); continue; }
            short len = 0; int rep;
            if (sym == 16) { if (idx == 0) bad("invalid bit length repeat"); len = lengths[idx - 1]; rep = 3 + bits(2); }
            else if (sym == 17) rep = 3 + bits(3); else rep = 11 + bits(7);
            if (idx + rep > nlen + ndist) bad("invalid bit length repeat");
            while (rep--) lengths[idx++] = len;
        }
        if (lengths[256] == 0) bad("invalid code -- missing end-of-block");
        codes(build(lengths, nlen), build(lengths + nlen, ndist));
    }
    std::string run() {
        for (int last = 0; !last;) {
            last = bits(1); const int type = bits(2);
            if (type == 0) {
                bitbuf = 0; bitcnt = 0;
                if (pos + 4 > n) bad("incomplete or truncated stream");
                const unsigned len = p[pos] | (p[pos + 1] << 8), nlen = p[pos + 2] | (p[pos + 3] << 8); pos += 4;
                if (len != (~nlen & 0xFFFFu)) bad("invalid stored block lengths");
                if (pos + len > n) bad("incomplete or truncated stream");
                out.append(reinterpret_cast<const char*>(p + pos), len); pos += len;
            } else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else bad("invalid block type");
        }
        return out;
    }
};
inline std::string inflate_raw(const std::string& s, std::size_t at, std::size_t len) { return Inflate(s, at, len).run(); }
// ---- archive
struct Member { std::string name; uint16_t method = 0, flags = 0; uint32_t crc = 0; uint64_t csize = 0, usize = 0, local = 0; };
inline uint64_t le(const std::string& s, std::size_t at, int nbytes) {
    if (at + static_cast<std::size_t>(nbytes) > s.size()) throw BadZipFile("Truncated file header");
    uint64_t v = 0; for (int k = nbytes - 1; k >= 0; --k) v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<std::size_t>(k)]); return v;
}
inline std::vector<Member> members(const std::string& z) {
    if (z.size() < 22) throw BadZipFile("File is not a zip file");
    std::size_t e = std::string::npos;
    for (std::size_t k = z.size() - 22 + 1; k-- > 0 && z.size() - k <= 22 + 65535;) if (z.compare(k, 4, "PK\x05\x06") == 0) { e = k; break; }
    if (e == std::string::npos) throw BadZipFile("File is not a zip file");
    uint64_t count = le(z, e + 10, 2), cd = le(z, e + 16, 4);
    if ((count == 0xFFFF || cd == 0xFFFFFFFFu) && e >= 20 && z.compare(e - 20, 4, "PK\x06\x07") == 0) {   // ZIP64 end of central directory
        const uint64_t e64 = le(z, e - 20 + 8, 8);
        if (e64 + 56 <= z.size() && z.compare(static_cast<std::size_t>(e64), 4, "PK\x06\x06") == 0) { count = le(z, static_cast<std::size_t>(e64) + 32, 8); cd = le(z, static_cast<std::size_t>(e64) + 48, 8); }
    }
    std::vector<Member> out; std::size_t at = static_cast<std::size_t>(cd);
    for (uint64_t i = 0; i < count; ++i) {
        if (at + 46 > z.size() || z.compare(at, 4, "PK\x01\x02") != 0) throw BadZipFile("Bad magic number for central directory");
        Member m; m.flags = static_cast<uint16_t>(le(z, at + 8, 2)); m.method = static_cast<uint16_t>(le(z, at + 10, 2)); m.crc = static_cast<uint32_t>(le(z, at + 16, 4));
        m.csize = le(z, at + 20, 4); m.usize = le(z, at + 24, 4);
        const std::size_t nl = static_cast<std::size_t>(le(z, at + 28, 2)), xl = static_cast<std::size_t>(le(z, at + 30, 2)), cl = static_cast<std::size_t>(le(z, at + 32, 2));
        m.local = le(z, at + 42, 4);
        if (at + 46 + nl + xl > z.size()) throw BadZipFile("Truncated central directory");
        const std::string raw = z.substr(at + 46, nl);
        if (m.flags & 0x800) m.name = raw; else { for (unsigned char c : raw) { if (c < 0x80) m.name += static_cast<char>(c); else { static const uint16_t CP437[128] = {0xC7,0xFC,0xE9,0xE2,0xE4,0xE0,0xE5,0xE7,0xEA,0xEB,0xE8,0xEF,0xEE,0xEC,0xC4,0xC5,0xC9,0xE6,0xC6,0xF4,0xF6,0xF2,0xFB,0xF9,0xFF,0xD6,0xDC,0xA2,0xA3,0xA5,0x20A7,0x192,0xE1,0xED,0xF3,0xFA,0xF1,0xD1,0xAA,0xBA,0xBF,0x2310,0xAC,0xBD,0xBC,0xA1,0xAB,0xBB,0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,0x3B1,0xDF,0x393,0x3C0,0x3A3,0x3C3,0xB5,0x3C4,0x3A6,0x398,0x3A9,0x3B4,0x221E,0x3C6,0x3B5,0x2229,0x2261,0xB1,0x2265,0x2264,0x2320,0x2321,0xF7,0x2248,0xB0,0x2219,0xB7,0x221A,0x207F,0xB2,0x25A0,0xA0}; m.name += utf8(CP437[c - 0x80]); } } }
        for (std::size_t x = at + 46 + nl; x + 4 <= at + 46 + nl + xl;) {   // ZIP64 extra field
            const uint64_t id = le(z, x, 2), sz = le(z, x + 2, 2); std::size_t q = x + 4;
            if (id == 1) { if (m.usize == 0xFFFFFFFFu) { m.usize = le(z, q, 8); q += 8; } if (m.csize == 0xFFFFFFFFu) { m.csize = le(z, q, 8); q += 8; } if (m.local == 0xFFFFFFFFu) m.local = le(z, q, 8); }
            x += 4 + static_cast<std::size_t>(sz);
        }
        out.push_back(m); at += 46 + nl + xl + cl;
    }
    return out;
}
inline std::vector<std::string> namelist(const std::string& z) { std::vector<std::string> v; for (const auto& m : members(z)) v.push_back(m.name); return v; }
inline std::string read(const std::string& z, const std::string& name) {
    for (const auto& m : members(z)) {
        if (m.name != name) continue;
        const std::size_t h = static_cast<std::size_t>(m.local);
        if (h + 30 > z.size() || z.compare(h, 4, "PK\x03\x04") != 0) throw BadZipFile("Bad magic number for file header");
        if (m.flags & 1) throw PyErr("RuntimeError", "File " + str_repr(name) + " is encrypted, password required for extraction");
        const std::size_t data = h + 30 + static_cast<std::size_t>(le(z, h + 26, 2)) + static_cast<std::size_t>(le(z, h + 28, 2));
        if (data + m.csize > z.size()) throw BadZipFile("Truncated file data");
        std::string out;
        if (m.method == 0) out = z.substr(data, static_cast<std::size_t>(m.csize));
        else if (m.method == 8) out = inflate_raw(z, data, static_cast<std::size_t>(m.csize));
        else throw PyErr("NotImplementedError", "That compression method is not supported");
        if (crc32(out) != m.crc) throw BadZipFile("Bad CRC-32 for file " + str_repr(name));
        return out;
    }
    throw PyErr("KeyError", "\"There is no item named " + str_repr(name) + " in the archive\"");
}
// gzip.decompress(): RFC 1952 members (FEXTRA, FNAME, FCOMMENT, FHCRC), CRC-32 and size checked
inline std::string gunzip(const std::string& s) {
    std::string out; std::size_t at = 0;
    while (at < s.size()) {
        if (s.size() - at < 10 || static_cast<unsigned char>(s[at]) != 0x1f || static_cast<unsigned char>(s[at + 1]) != 0x8b) throw BadZipFile("Not a gzipped file");
        if (s[at + 2] != 8) throw BadZipFile("Unknown compression method");
        const unsigned flg = static_cast<unsigned char>(s[at + 3]); std::size_t p = at + 10;
        const auto need = [&](std::size_t k) { if (p + k > s.size()) throw BadZipFile("Compressed file ended before the end-of-stream marker was reached"); };
        if (flg & 4) { need(2); const std::size_t xl = static_cast<unsigned char>(s[p]) | (static_cast<unsigned char>(s[p + 1]) << 8); p += 2; need(xl); p += xl; }
        if (flg & 8) { while (true) { need(1); if (s[p++] == 0) break; } }
        if (flg & 16) { while (true) { need(1); if (s[p++] == 0) break; } }
        if (flg & 2) { need(2); p += 2; }
        Inflate inf(s, p, s.size() - p); const std::string part = inf.run(); p += inf.pos;
        need(8);
        const auto u32 = [&](std::size_t k) { return static_cast<uint32_t>(static_cast<unsigned char>(s[k])) | static_cast<uint32_t>(static_cast<unsigned char>(s[k + 1])) << 8 | static_cast<uint32_t>(static_cast<unsigned char>(s[k + 2])) << 16 | static_cast<uint32_t>(static_cast<unsigned char>(s[k + 3])) << 24; };
        if (u32(p) != crc32(part)) throw BadZipFile("CRC check failed");
        if (u32(p + 4) != static_cast<uint32_t>(part.size() & 0xffffffffu)) throw BadZipFile("Incorrect length of data produced");
        out += part; at = p + 8;
        while (at < s.size() && s[at] == 0) ++at;   // trailing zero padding is allowed
    }
    return out;
}
// ---- zlib (RFC 1950): zlib.decompress() and a level-9-style zlib.compress() (LZ77 hash chains, fixed Huffman codes:
// a valid stream that any inflater reads; the bytes differ from zlib's own encoder)
inline uint32_t adler32(const std::string& s) { uint32_t a = 1, b = 0; for (unsigned char c : s) { a = (a + c) % 65521; b = (b + a) % 65521; } return (b << 16) | a; }
inline std::string zlib_decompress(const std::string& s) {
    if (s.size() < 2) Inflate::bad("incomplete or truncated stream");
    const unsigned cmf = static_cast<unsigned char>(s[0]), flg = static_cast<unsigned char>(s[1]);
    if ((cmf * 256 + flg) % 31) Inflate::bad("incorrect header check");
    if ((cmf & 15) != 8 || (cmf >> 4) > 7) Inflate::bad("invalid window size");
    if (flg & 0x20) throw PyErr("zlib.error", "Error 2 while decompressing data");
    Inflate inf(s, 2, s.size() - 2); const std::string o = inf.run(); const std::size_t p = 2 + inf.pos;
    if (p + 4 > s.size()) throw PyErr("zlib.error", "Error -5 while decompressing data: incomplete or truncated stream");
    uint32_t want = 0; for (int k = 0; k < 4; ++k) want = (want << 8) | static_cast<unsigned char>(s[p + static_cast<std::size_t>(k)]);
    if (want != adler32(o)) Inflate::bad("incorrect data check");
    return o;
}
inline std::string zlib_compress(const std::string& in) {
    struct Bits { std::string o; uint32_t buf = 0; int cnt = 0;
        void put(uint32_t v, int n) { buf |= v << cnt; cnt += n; while (cnt >= 8) { o += static_cast<char>(buf & 0xFF); buf >>= 8; cnt -= 8; } }
        void huff(uint32_t code, int n) { uint32_t r = 0; for (int k = 0; k < n; ++k) r |= ((code >> k) & 1u) << (n - 1 - k); put(r, n); } } w;
    static const int LBASE[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int LEXT[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int DBASE[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int DEXT[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    w.o = std::string("\x78\xDA", 2);
    w.put(1, 1); w.put(1, 2);   // one final block, fixed Huffman codes
    const auto lit = [&](int c) {
        if (c < 144) w.huff(static_cast<uint32_t>(0x30 + c), 8);
        else if (c < 256) w.huff(static_cast<uint32_t>(0x190 + c - 144), 9);
        else if (c < 280) w.huff(static_cast<uint32_t>(c - 256), 7);
        else w.huff(static_cast<uint32_t>(0xC0 + c - 280), 8);
    };
    const std::size_t n = in.size(); const auto* p = reinterpret_cast<const unsigned char*>(in.data());
    std::vector<long long> head(1u << 15, -1), prev(n, -1);
    const auto hash = [&](std::size_t i) { return static_cast<std::size_t>(((p[i] << 10) ^ (p[i + 1] << 5) ^ p[i + 2]) & 0x7FFF); };
    const auto insert = [&](std::size_t i) { if (i + 2 < n) { const std::size_t h = hash(i); prev[i] = head[h]; head[h] = static_cast<long long>(i); } };
    for (std::size_t i = 0; i < n;) {
        std::size_t best = 0, dist = 0;
        if (i + 2 < n) {
            const std::size_t maxm = std::min<std::size_t>(258, n - i);
            int chain = 128;
            for (long long c = head[hash(i)]; c >= 0 && i - static_cast<std::size_t>(c) <= 32768 && chain-- > 0; c = prev[static_cast<std::size_t>(c)]) {
                const std::size_t from = static_cast<std::size_t>(c); std::size_t m = 0;
                while (m < maxm && p[from + m] == p[i + m]) ++m;
                if (m > best) { best = m; dist = i - from; if (m == maxm) break; }
            }
        }
        if (best >= 3) {
            int li = 28; while (LBASE[li] > static_cast<int>(best)) --li;
            lit(257 + li); w.put(static_cast<uint32_t>(static_cast<int>(best) - LBASE[li]), LEXT[li]);
            int di = 29; while (DBASE[di] > static_cast<int>(dist)) --di;
            w.huff(static_cast<uint32_t>(di), 5); w.put(static_cast<uint32_t>(static_cast<int>(dist) - DBASE[di]), DEXT[di]);
            for (std::size_t k = i; k < i + best; ++k) insert(k);
            i += best;
        } else { lit(p[i]); insert(i); ++i; }
    }
    lit(256);
    if (w.cnt > 0) { w.o += static_cast<char>(w.buf & 0xFF); }
    const uint32_t ad = adler32(in);
    for (int k = 3; k >= 0; --k) w.o += static_cast<char>((ad >> (8 * k)) & 0xFF);
    return w.o;
}
}  // namespace pyrt::zip
