// console_datascan.inc - console-levels/datascan: a C++ port of console-skill/console-levels/datascan.py (Jake Schincariol, MIT).
//
// Map a PS2 disc's data files: inventory (magic, entropy, table-of-contents guess), AFS / generic table
// archive unpack + repack, byte-range diffs, strings (ASCII, UTF-16LE, Shift-JIS) and the disc files a
// PS2Recomp runtime log looked up. Exit codes: 0 ok, 1 needs attention (or, for diff, the files differ),
// 2 bad input.
//
// Notes on fidelity:
//  * the zlib sniff mirrors zlib.decompressobj().decompress(head[:4096]): truncation is fine, bad
//    Huffman tables (incomplete / over-subscribed), bad distances and a wrong Adler-32 are not;
//  * scores are computed one operation per statement so no target fuses them into FMAs;
//  * a negative --min is refused (Python builds a literal "{-1,}" pattern there); hex numbers in a
//    runtime log past 63 bits saturate; the test-only env var PACKTOOLS_DATASCAN_CHUNK shrinks the
//    read chunk (upstream tests patch datascan.CHUNK).
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace packtools::datascan {
using namespace pyrt;
using namespace packtools::console_common;

static constexpr const char* PROG = "datascan";
static constexpr const char* INDEX_NAME = "index.json";
static constexpr long long HEAD_BYTES = 1LL << 20;
static constexpr long long MAX_ENTRIES = 65536;
static constexpr double TOC_THRESHOLD = 0.6;

inline long long chunk() {
    static const long long c = [] {
        const char* e = std::getenv("PACKTOOLS_DATASCAN_CHUNK");
        if (e && *e) { char* end = nullptr; const long long v = std::strtoll(e, &end, 10); if (end && !*end && v > 0) return v; }
        return 4LL << 20;
    }();
    return c;
}

struct ToolError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Refused : std::runtime_error { using std::runtime_error::runtime_error; };

using Ent = std::pair<long long, long long>;   // (offset, size) in bytes

// ---------------------------------------------------------------- small helpers
inline unsigned bt(const std::string& d, std::size_t o) { return static_cast<unsigned char>(d[o]); }
inline long long u32(const std::string& buf, long long off) {   // struct.unpack_from("<I", buf, off)[0]
    if (off < 0 || off + 4 > static_cast<long long>(buf.size()))
        throw PyErr("struct.error", cfmt("unpack_from requires a buffer of at least %lld bytes for unpacking 4 bytes at offset %lld (actual buffer size is %zu)", off + 4, off, buf.size()));
    const auto o = static_cast<std::size_t>(off);
    return static_cast<long long>(bt(buf, o) | (bt(buf, o + 1) << 8) | (bt(buf, o + 2) << 16) | (static_cast<uint32_t>(bt(buf, o + 3)) << 24));
}
inline unsigned u16at(const std::string& buf, std::size_t o) { return bt(buf, o) | (bt(buf, o + 1) << 8); }
inline long long align_up(long long v, long long a) { return a <= 1 ? v : (v + a - 1) / a * a; }
inline long long floordiv(long long a, long long b) {
    if (b == 0) throw PyErr("ZeroDivisionError", "integer division or modulo by zero");
    long long q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q;
}
inline long long pymod(long long a, long long b) {
    if (b == 0) throw PyErr("ZeroDivisionError", "integer modulo by zero");
    long long r = a % b; if (r != 0 && ((r < 0) != (b < 0))) r += b; return r;
}
inline std::string ds_basename(const std::string& p) {
#if defined(_WIN32)
    const std::size_t k = p.find_last_of("/\\");
#else
    const std::size_t k = p.rfind('/');
#endif
    return k == std::string::npos ? p : p.substr(k + 1);
}
inline bool ds_isabs(const std::string& p) {
#if defined(_WIN32)
    return fspath(p).is_absolute();
#else
    return !p.empty() && p[0] == '/';
#endif
}
inline bool ds_islink(const std::string& p) { std::error_code ec; return std::filesystem::is_symlink(std::filesystem::symlink_status(fspath(p), ec)); }
inline long long getsize(const std::string& p) {
    std::error_code ec; const auto n = std::filesystem::file_size(fspath(p), ec);
    if (ec) throw OSError(ec.value(), p);
    return static_cast<long long>(n);
}

// open(path, "rb") with read / seek
struct BinFile {
    std::ifstream f;
    explicit BinFile(const std::string& p) {
        if (is_dir(p)) throw OSError(EISDIR, p);
        errno = 0; f.open(fspath(p), std::ios::binary);
        if (!f) throw OSError(errno ? errno : ENOENT, p);
    }
    std::string read(long long n) {
        if (n <= 0) return {};
        std::string b; std::size_t got = 0;
        while (static_cast<long long>(got) < n) {
            const std::size_t want = static_cast<std::size_t>(std::min<long long>(n - static_cast<long long>(got), 1LL << 24));
            b.resize(got + want);
            f.read(b.data() + got, static_cast<std::streamsize>(want));
            const auto g = static_cast<std::size_t>(f.gcount()); got += g;
            if (g < want) break;
        }
        b.resize(got); if (f.eof()) f.clear();
        return b;
    }
    void seek(long long o) { f.clear(); f.seekg(static_cast<std::streamoff>(o)); }
};
inline void write_bytes(const std::string& p, const std::string& data) {
    errno = 0; std::ofstream o(fspath(p), std::ios::binary | std::ios::trunc);
    if (!o) throw OSError(errno ? errno : EACCES, p);
    o.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!o) throw OSError(EIO, p);
}

// hashlib.sha256, streamed
struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buf[64]{}; std::size_t blen = 0; uint64_t total = 0;
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const unsigned char* m) {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
            0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
            0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int k = 0; k < 16; ++k) w[k] = (static_cast<uint32_t>(m[4 * k]) << 24) | (static_cast<uint32_t>(m[4 * k + 1]) << 16) | (static_cast<uint32_t>(m[4 * k + 2]) << 8) | m[4 * k + 3];
        for (int k = 16; k < 64; ++k) {
            const uint32_t s0 = rotr(w[k - 15], 7) ^ rotr(w[k - 15], 18) ^ (w[k - 15] >> 3);
            const uint32_t s1 = rotr(w[k - 2], 17) ^ rotr(w[k - 2], 19) ^ (w[k - 2] >> 10);
            w[k] = w[k - 16] + s0 + w[k - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int k = 0; k < 64; ++k) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[k] + w[k];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const std::string& s) {
        const auto* p = reinterpret_cast<const unsigned char*>(s.data()); std::size_t n = s.size(); total += n;
        while (n) {
            const std::size_t take = std::min(n, 64 - blen); std::memcpy(buf + blen, p, take); blen += take; p += take; n -= take;
            if (blen == 64) { block(buf); blen = 0; }
        }
    }
    std::string hex() {
        const uint64_t bits = total * 8; std::string pad(1, static_cast<char>(0x80));
        while ((total + pad.size()) % 64 != 56) pad += '\0';
        for (int k = 7; k >= 0; --k) pad += static_cast<char>((bits >> (k * 8)) & 0xFF);
        const uint64_t keep = total; update(pad); total = keep;
        char out[65]; for (int k = 0; k < 8; ++k) std::snprintf(out + 8 * k, 9, "%08x", h[k]);
        return std::string(out, 64);
    }
};
inline std::string sha256_file(const std::string& path) {
    BinFile fh(path); Sha256 h;
    while (true) { const std::string b = fh.read(chunk()); if (b.empty()) break; h.update(b); }
    return h.hex();
}
inline std::string sha256_bytes(const std::string& data) { Sha256 h; h.update(data); return h.hex(); }

inline std::string read_head(const std::string& path, long long n = HEAD_BYTES) {
    if (is_dir(path)) throw ToolError(path + " is a folder, not a file");
    try { BinFile fh(path); return fh.read(n); }
    catch (const OSError& e) { throw ToolError("cannot read " + path + ": " + strerror_of(e.err)); }
}
inline void need_file(const std::string& path) {
    if (!exists(path)) throw ToolError(path + " does not exist");
    if (!is_file(path)) throw ToolError(path + " is not a file");
}
inline std::string human(long long n0) {
    if (n0 < 1024) return cfmt("%lld B", n0);
    double n = static_cast<double>(n0) / 1024.0;
    for (const char* unit : {"KB", "MB"}) {
        if (n < 1024) return cfmt("%.1f %s", n, unit);
        n /= 1024.0;
    }
    return cfmt("%.1f GB", n);
}

inline double entropy(const std::string& data) {
    if (data.empty()) return 0.0;
    long long counts[256] = {};
    for (const char ch : data) counts[static_cast<unsigned char>(ch)]++;
    const double n = static_cast<double>(data.size());
    const double ln2 = std::log(2.0);
    double e = 0.0;
    for (const long long c : counts) {
        if (!c) continue;
        const double p = static_cast<double>(c) / n;
        const double lp = std::log(p) / ln2;   // math.log(p, 2)
        const double t = p * lp;
        e -= t;
    }
    return e;
}
inline double sample_entropy(const std::string& path, long long size, long long window = 1LL << 16) {
    if (size <= 0) return 0.0;
    std::vector<long long> spots;
    if (size <= 3 * window) spots = {0};
    else spots = {0, size / 2 - window / 2, size - window};
    std::string data; BinFile fh(path);
    for (const long long s : spots) { fh.seek(std::max(0LL, s)); data += fh.read(window); }
    return entropy(data);
}
inline std::string entropy_label(double e) {
    if (e < 1.0) return "mostly empty";
    if (e < 6.0) return "structured";
    if (e < 7.5) return "mixed";
    return "compressed/encrypted/ADPCM";
}

// str.strip() of a latin-1 decoded byte string (Unicode whitespace below U+0100), as UTF-8
inline std::string latin1_strip(const std::string& raw) {
    const auto ws = [](unsigned c) { return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0; };
    std::size_t a = 0, b = raw.size();
    while (a < b && ws(bt(raw, a))) ++a;
    while (b > a && ws(bt(raw, b - 1))) --b;
    return latin1(raw.substr(a, b - a));
}

// ---------------------------------------------------------------- zlib.decompressobj().decompress(data) succeeds?
struct ZCheck {
    std::string src; const unsigned char* p; std::size_t n, pos = 0; uint32_t bitbuf = 0; int bitcnt = 0; std::string out;
    struct Trunc {}; struct Bad {};
    explicit ZCheck(std::string s) : src(std::move(s)), p(reinterpret_cast<const unsigned char*>(src.data())), n(src.size()) {}
    ZCheck(const ZCheck&) = delete; ZCheck& operator=(const ZCheck&) = delete;
    int bits(int need) {
        long long v = bitbuf;
        while (bitcnt < need) { if (pos >= n) throw Trunc{}; v |= static_cast<long long>(p[pos++]) << bitcnt; bitcnt += 8; }
        bitbuf = static_cast<uint32_t>(v >> need); bitcnt -= need;
        return static_cast<int>(v & ((1LL << need) - 1));
    }
    struct Huff { short count[16]{}; std::vector<short> sym; };
    static int construct(Huff& h, const short* len, int num) {   // puff: 0 complete, >0 incomplete, <0 over-subscribed
        h.sym.assign(static_cast<std::size_t>(num), 0);
        for (short& c : h.count) c = 0;
        for (int s = 0; s < num; ++s) h.count[len[s]]++;
        if (h.count[0] == num) return 0;
        int left = 1;
        for (int l = 1; l < 16; ++l) { left <<= 1; left -= h.count[l]; if (left < 0) return left; }
        short offs[16]; offs[1] = 0;
        for (int l = 1; l < 15; ++l) offs[l + 1] = static_cast<short>(offs[l] + h.count[l]);
        for (int s = 0; s < num; ++s) if (len[s]) h.sym[static_cast<std::size_t>(offs[len[s]]++)] = static_cast<short>(s);
        return left;
    }
    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; ++l) {
            code |= bits(1); const int count = h.count[l];
            if (code - count < first) return h.sym[static_cast<std::size_t>(index + (code - first))];
            index += count; first += count; first <<= 1; code <<= 1;
        }
        throw Bad{};
    }
    void codes(const Huff& lc, const Huff& dc) {
        static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        while (true) {
            int sym = decode(lc);
            if (sym < 256) { out += static_cast<char>(sym); continue; }
            if (sym == 256) return;
            sym -= 257;
            if (sym >= 29) throw Bad{};
            const int len = lbase[sym] + bits(lext[sym]);
            const int ds = decode(dc);
            if (ds >= 30) throw Bad{};
            const std::size_t dist = static_cast<std::size_t>(dbase[ds] + bits(dext[ds]));
            if (dist > out.size()) throw Bad{};
            for (int k = 0; k < len; ++k) out += out[out.size() - dist];
        }
    }
    void stored() {
        bitbuf = 0; bitcnt = 0;
        if (pos + 4 > n) throw Trunc{};
        const unsigned len = p[pos] | (p[pos + 1] << 8), nlen = p[pos + 2] | (p[pos + 3] << 8); pos += 4;
        if (len != (~nlen & 0xFFFFu)) throw Bad{};
        if (pos + len > n) throw Trunc{};
        out.append(reinterpret_cast<const char*>(p + pos), len); pos += len;
    }
    void fixed() {
        static Huff lc, dc; static bool built = false;
        if (!built) {
            short l[320]; int s = 0;
            for (; s < 144; ++s) l[s] = 8;
            for (; s < 256; ++s) l[s] = 9;
            for (; s < 280; ++s) l[s] = 7;
            for (; s < 288; ++s) l[s] = 8;
            construct(lc, l, 288);
            for (s = 0; s < 30; ++s) l[s] = 5;
            construct(dc, l, 30);
            built = true;
        }
        codes(lc, dc);
    }
    void dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) throw Bad{};
        short lengths[320] = {};
        for (int i = 0; i < ncode; ++i) lengths[order[i]] = static_cast<short>(bits(3));
        Huff lc, dc;
        if (construct(lc, lengths, 19) != 0) throw Bad{};
        int index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lc);
            if (sym < 16) { lengths[index++] = static_cast<short>(sym); continue; }
            short len = 0;
            if (sym == 16) { if (index == 0) throw Bad{}; len = lengths[index - 1]; sym = 3 + bits(2); }
            else if (sym == 17) sym = 3 + bits(3);
            else sym = 11 + bits(7);
            if (index + sym > nlen + ndist) throw Bad{};
            while (sym--) lengths[index++] = len;
        }
        if (lengths[256] == 0) throw Bad{};
        int err = construct(lc, lengths, nlen);
        if (err && (err < 0 || nlen != lc.count[0] + lc.count[1])) throw Bad{};
        err = construct(dc, lengths + nlen, ndist);
        if (err && (err < 0 || ndist != dc.count[0] + dc.count[1])) throw Bad{};
        codes(lc, dc);
    }
    bool ok() {   // the caller already checked the two header bytes (78 01 / 5E / 9C / DA)
        pos = 2;
        try {
            int last = 0;
            do {
                last = bits(1); const int type = bits(2);
                if (type == 0) stored();
                else if (type == 1) fixed();
                else if (type == 2) dynamic();
                else throw Bad{};
            } while (!last);
            if (pos + 4 > n) return true;   // the Adler-32 has not arrived yet
            uint32_t a = 1, b = 0;
            for (const char ch : out) { a = (a + static_cast<unsigned char>(ch)) % 65521; b = (b + a) % 65521; }
            const uint32_t want = (static_cast<uint32_t>(p[pos]) << 24) | (static_cast<uint32_t>(p[pos + 1]) << 16) | (static_cast<uint32_t>(p[pos + 2]) << 8) | p[pos + 3];
            return want == ((b << 16) | a);
        } catch (const Trunc&) {
            return true;
        } catch (const Bad&) {
            return false;
        }
    }
};

// ---------------------------------------------------------------- magic sniffing
inline bool elf_has_iopmod(const std::string& path, const std::string& head) {   // ".iopmod" in _elf_section_names(path, head)
    try {
        const long long shoff = u32(head, 0x20);
        const long long shentsize = u16at(head, 0x2E), shnum = u16at(head, 0x30), shstrndx = u16at(head, 0x32);
        const long long size = getsize(path);
        if (!shoff || shentsize < 40 || !shnum || shnum > 4096 || shstrndx >= shnum) return false;
        if (shoff + shnum * shentsize > size) return false;
        BinFile fh(path);
        fh.seek(shoff);
        const std::string table = fh.read(shnum * shentsize);
        const long long so = shstrndx * shentsize;
        const long long str_off = u32(table, so + 16), str_size = u32(table, so + 20);
        if (str_off + str_size > size || str_size > (1LL << 20)) return false;
        fh.seek(str_off);
        const std::string strtab = fh.read(str_size);
        bool found = false;
        for (long long i = 0; i < shnum; ++i) {
            const long long nm = u32(table, i * shentsize);
            if (nm < static_cast<long long>(strtab.size())) {
                const std::size_t z = strtab.find('\0', static_cast<std::size_t>(nm));
                if (strtab.substr(static_cast<std::size_t>(nm), z == std::string::npos ? std::string::npos : z - static_cast<std::size_t>(nm)) == ".iopmod") found = true;
            }
        }
        return found;
    } catch (const PyErr&) {   // struct.error, OSError
        return false;
    }
}
inline std::pair<std::string, std::string> elf_kind(const std::string& path, const std::string& head) {
    if (head.size() < 52) return {"ELF", "truncated header"};
    if (bt(head, 4) != 1 || bt(head, 5) != 1) return {"ELF", "not 32-bit little-endian"};
    const unsigned e_type = u16at(head, 16), e_machine = u16at(head, 18);
    if (e_machine != 8) return {"ELF", cfmt("machine %u, not MIPS", e_machine)};
    if (e_type == 0xFF80) return {"IRX", "IOP module (ELF type 0xFF80)"};
    if (elf_has_iopmod(path, head)) return {"IRX", "IOP module (.iopmod section)"};
    if (e_type == 2) return {"ELF", "MIPS executable (EE)"};
    if (e_type == 1) return {"ELF", "MIPS relocatable (an overlay or library?)"};
    return {"ELF", cfmt("MIPS, type 0x%x", e_type)};
}
inline bool looks_text(const std::string& head) {
    if (head.empty()) return false;
    const std::size_t n = std::min<std::size_t>(head.size(), 4096);
    for (std::size_t i = 0; i < n; ++i) { const unsigned b = bt(head, i); if (b < 9 || (b > 13 && b < 32) || b == 127) return false; }
    return true;
}
inline std::pair<std::string, std::string> sniff(const std::string& path, const std::string& head, long long size) {
    const std::string h4 = head.substr(0, 4);
    if (size == 0) return {"empty", ""};
    if (h4 == "TIM2") return {"TIM2", "texture (/console-graphics)"};
    if (h4 == "VAGp" || h4 == "VAGi") return {"VAG", "ADPCM audio (/console-sound)"};
    if (head.substr(0, 8) == "IECSsreV") return {"IECS", "Sony sound bank header (.hd/.sq); the samples live in the matching .bd"};
    if (h4 == "IECS") return {"IECS", "Sony sound data (IECS chunks)"};
    if (h4 == std::string("\x00\x00\x01\xba", 4)) return {"MPEG-PS", "PSS/MPEG program stream (a movie)"};
    if (h4 == std::string("\x00\x00\x01\xb3", 4)) return {"MPEG", "MPEG video elementary stream"};
    if (h4 == "ipum") return {"IPU", "IPU video (MPEG-2 intra frames)"};
    if (h4 == "\x7f" "ELF") return elf_kind(path, head);
    if (head.substr(0, 3) == "\x1f\x8b\x08") return {"gzip", "compressed"};
    if (h4 == "PK\x03\x04") return {"ZIP", "archive"};
    if (h4 == "RIFF") { const std::string t = latin1_strip(head.size() > 8 ? head.substr(8, 4) : ""); return {"RIFF", t.empty() ? "RIFF" : t}; }
    if (h4 == std::string("AFS\0", 4)) return {"AFS", "CRI archive (unpack works)"};
    if (h4 == "CPK ") return {"CPK", "CRI CPK archive (not unpacked by this tool)"};
    if (h4 == "PS2D") return {"icon.sys", "memory card save icon info"};
    if (head.substr(0, 28) == "Sony PS2 Memory Card Format ") return {"memcard", "PS2 memory card image (/console-saves)"};
    if (head.size() >= 0x8006 && head.substr(0x8001, 5) == "CD001") return {"ISO9660", "disc image (/console-reverse)"};
    if (head.substr(0, 8) == "\x89PNG\r\n\x1a\n") return {"PNG", "image"};
    if (h4 == "OggS") return {"Ogg", "audio"};
    if (h4 == "MThd") return {"MIDI", "music sequence"};
    if (head.size() >= 2 && bt(head, 0) == 0x78 && (bt(head, 1) == 0x01 || bt(head, 1) == 0x5E || bt(head, 1) == 0x9C || bt(head, 1) == 0xDA)) {
        ZCheck z(head.substr(0, 4096));
        if (z.ok()) return {"zlib", "compressed stream"};
    }
    if (looks_text(head)) {
        const std::string first40 = head.substr(0, 40);
        return {"text", latin1_strip(first40.substr(0, first40.find('\n')))};
    }
    return {"data", ""};
}

// ---------------------------------------------------------------- table-of-contents detection
struct Cand {
    std::string kind; long long header = 0, count_at = -1, table_at = 0, n = 0, off_scale = 1, size_scale = 1;
    bool sentinel = false; long long table_end = 0; double score = 0; long long alignment = 1; double coverage = 0;
    std::vector<Ent> entries;
};
inline long long alignment_of(const std::vector<long long>& offsets) {
    for (long long a = 2048; a > 1; a /= 2) {
        bool all = true;
        for (const long long o : offsets) if (pymod(o, a) != 0) { all = false; break; }
        if (all) return a;
    }
    return 1;
}
struct Score { double score; long long align; double coverage; };
inline std::optional<Score> score_of(const std::vector<Ent>& entries, long long table_end, long long size, double prior) {
    std::vector<Ent> real;
    for (const auto& e : entries) if (!(e.first == 0 && e.second == 0)) real.push_back(e);
    if (real.size() < 2 || size <= table_end) return std::nullopt;
    long long prev_off = -1, prev_end = table_end;
    for (const auto& [o, s] : real) {
        if (o < table_end || o + s > size || o < prev_off || o < prev_end) return std::nullopt;
        prev_off = o; prev_end = o + s;
    }
    std::size_t zero = 0;
    for (const auto& e : real) if (e.second == 0) ++zero;
    if (zero * 2 > real.size()) return std::nullopt;
    std::vector<long long> offs; for (const auto& e : real) offs.push_back(e.first);
    const long long a = alignment_of(offs);
    const long long span = size - table_end;
    long long total = 0; for (const auto& e : real) total += e.second;
    const double coverage = std::min(1.0, static_cast<double>(total) / static_cast<double>(span));
    const long long pad = std::max(a, 16LL);
    std::size_t small_gaps = 0;
    for (std::size_t i = 0; i + 1 < real.size(); ++i) if (real[i + 1].first - (real[i].first + real[i].second) < pad) ++small_gaps;
    const double gap_reg = static_cast<double>(small_gaps) / static_cast<double>(real.size() - 1);
    const double align = a >= 16 ? 1.0 : (a >= 4 ? 0.6 : 0.3);
    const long long front = real[0].first - table_end;
    const double front_fit = front < std::max(pad, 2048LL) ? 1.0 : (front < 65536 ? 0.5 : 0.0);
    const long long tail = size - (real.back().first + real.back().second);
    const double tail_fit = tail < std::max(pad, 2048LL) ? 1.0 : (tail < 65536 ? 0.5 : 0.0);
    const std::size_t n = real.size();
    const double count_factor = n == 2 ? 0.75 : (n == 3 ? 0.9 : 1.0);
    // one operation per statement: the same rounding as CPython on every target
    double score = 0.3 * coverage;
    const double t2 = 0.2 * gap_reg; score = score + t2;
    const double t3 = 0.2 * align; score = score + t3;
    const double t4 = 0.15 * front_fit; score = score + t4;
    const double t5 = 0.15 * tail_fit; score = score + t5;
    double r = score * count_factor;
    r = r * prior;
    return Score{round_n(r, 3), a, coverage};
}
inline std::optional<std::vector<Ent>> read_pairs(const std::string& head, long long t0, long long n, long long osc, long long ssc, long long table_end, long long size) {
    std::vector<Ent> ents; long long prev = table_end;
    for (long long i = 0; i < n; ++i) {
        const long long o = u32(head, t0 + i * 8) * osc, s = u32(head, t0 + i * 8 + 4) * ssc;
        if (o || s) {
            if (o < prev || o + s > size) return std::nullopt;
            prev = o + s;
        }
        ents.emplace_back(o, s);
    }
    return ents;
}
inline std::optional<std::vector<long long>> read_offsets(const std::string& head, long long t0, long long n, long long osc, long long table_end, long long size) {
    std::vector<long long> offs; long long prev = table_end;
    for (long long i = 0; i < n; ++i) {
        const long long o = u32(head, t0 + i * 4) * osc;
        if (o < prev || o > size) return std::nullopt;
        prev = o; offs.push_back(o);
    }
    return offs;
}
inline std::vector<Cand> detect_toc(const std::string& head, long long size, std::size_t limit = 5) {
    std::vector<Cand> cands;
    const long long hl = static_cast<long long>(head.size());
    const auto add = [&](const std::string& kind, long long h, long long count_at, long long t0, long long n, long long osc, long long ssc, bool sentinel, long long tend, const Score& r, std::vector<Ent> ents) {
        Cand c; c.kind = kind; c.header = h; c.count_at = count_at; c.table_at = t0; c.n = n; c.off_scale = osc; c.size_scale = ssc;
        c.sentinel = sentinel; c.table_end = tend; c.score = r.score; c.alignment = r.align; c.coverage = round_n(r.coverage, 3); c.entries = std::move(ents);
        cands.push_back(std::move(c));
    };
    for (const long long h : {0LL, 4LL, 8LL, 12LL, 16LL}) {
        if (h + 4 > hl) break;
        const long long n = u32(head, h);
        for (const std::string kind : {"count+pairs", "count+offsets"}) {
            if (!(1 <= n && n <= MAX_ENTRIES)) continue;
            const long long stride = kind == "count+pairs" ? 8 : 4;
            const long long t0 = h + 4, t1 = t0 + n * stride;
            if (t1 > hl) continue;
            for (const long long osc : {1LL, 2048LL}) {
                if (kind == "count+pairs") {
                    for (const long long ssc : {1LL, 2048LL}) {
                        const auto ents = read_pairs(head, t0, n, osc, ssc, t1, size);
                        if (!ents || ents->empty()) continue;
                        const auto r = score_of(*ents, t1, size, 1.0);
                        if (r) add(kind, h, h, t0, n, osc, ssc, false, t1, *r, *ents);
                    }
                } else {
                    const auto offs = read_offsets(head, t0, n, osc, t1, size);
                    if (!offs) continue;
                    for (const bool sentinel : {false, true}) {
                        long long end, tend;
                        if (sentinel) {
                            if (t1 + 4 > hl) continue;
                            end = u32(head, t1) * osc;
                            if (end > size || offs->empty() || end < offs->back()) continue;
                            tend = t1 + 4;
                        } else { end = size; tend = t1; }
                        std::vector<Ent> ents; bool neg = false;
                        for (long long i = 0; i < n; ++i) {
                            const long long o = (*offs)[static_cast<std::size_t>(i)];
                            const long long s = (i + 1 < n ? (*offs)[static_cast<std::size_t>(i + 1)] : end) - o;
                            if (s < 0) neg = true;
                            ents.emplace_back(o, s);
                        }
                        if (neg) continue;
                        const auto r = score_of(ents, tend, size, 0.85);
                        if (r) add(kind, h, h, t0, n, osc, 1, sentinel, tend, *r, ents);
                    }
                }
            }
        }
        // tables without a count: the first offset says where the table ends
        if (h + 8 > hl) continue;
        for (const long long osc : {1LL, 2048LL}) {
            const long long first = u32(head, h) * osc;
            if (first <= h || first > size) continue;
            for (const auto& [kind, stride] : {std::pair<std::string, long long>{"pairs", 8}, {"offsets", 4}}) {
                const long long n = (first - h) / stride;
                if (!(2 <= n && n <= MAX_ENTRIES) || h + n * stride > hl) continue;
                if (kind == "pairs") {
                    long long m = n;
                    while (m && u32(head, h + (m - 1) * 8) == 0 && u32(head, h + (m - 1) * 8 + 4) == 0) --m;
                    const auto ents = read_pairs(head, h, m, osc, 1, h + n * stride, size);
                    if (!ents || ents->size() < 2) continue;
                    const auto r = score_of(*ents, h + n * stride, size, 0.9);
                    if (r) add(kind, h, -1, h, static_cast<long long>(ents->size()), osc, 1, false, h + n * stride, *r, *ents);
                } else {
                    long long m = n;
                    while (m && u32(head, h + (m - 1) * 4) == 0) --m;
                    const auto offs = read_offsets(head, h, m, osc, h + n * stride, size);
                    if (!offs || offs->size() < 2) continue;
                    std::vector<Ent> ents; bool neg = false;
                    for (std::size_t i = 0; i < offs->size(); ++i) {
                        const long long o = (*offs)[i], s = (i + 1 < offs->size() ? (*offs)[i + 1] : size) - o;
                        if (s < 0) neg = true;
                        ents.emplace_back(o, s);
                    }
                    if (neg) continue;
                    const auto r = score_of(ents, h + n * stride, size, 0.8);
                    if (r) add(kind, h, -1, h, static_cast<long long>(ents.size()), osc, 1, false, h + n * stride, *r, ents);
                }
            }
        }
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.header != b.header) return a.header < b.header;
        return a.kind < b.kind;
    });
    if (cands.size() > limit) cands.resize(limit);
    return cands;
}
inline Val cand_val(const Cand& c, bool with_extra) {   // the candidate dict without "entries"
    Val d = Val::dict();
    d.d->set("kind", Val::str(c.kind)); d.d->set("header", Val::integer(c.header));
    d.d->set("count_at", c.count_at < 0 ? Val() : Val::integer(c.count_at));
    d.d->set("table_at", Val::integer(c.table_at)); d.d->set("n", Val::integer(c.n));
    d.d->set("off_scale", Val::integer(c.off_scale)); d.d->set("size_scale", Val::integer(c.size_scale));
    d.d->set("sentinel", Val::boolean(c.sentinel)); d.d->set("table_end", Val::integer(c.table_end));
    d.d->set("score", Val::flt(c.score));
    if (with_extra) { d.d->set("alignment", Val::integer(c.alignment)); d.d->set("coverage", Val::flt(c.coverage)); }
    return d;
}

// ---------------------------------------------------------------- AFS
struct Afs {
    long long n = 0; std::vector<Ent> entries; long long first = 0;
    bool has_meta = false; long long meta_off = 0, meta_len = 0, meta_at = 0; std::string meta_where;
    std::vector<std::string> names; std::vector<std::vector<long long>> dates;   // names: raw latin-1 bytes
    std::string rule; bool has_raw = false; std::vector<long long> raw_tail; long long size = 0;
};
inline Afs parse_afs(const std::string& path) {
    Afs A; A.size = getsize(path);
    BinFile fh(path);
    const std::string head = fh.read(8);
    if (head.size() < 8 || head.substr(0, 4) != std::string("AFS\0", 4)) throw ToolError(path + " is not an AFS archive (no 'AFS\\0' magic)");
    const long long n = u32(head, 4);
    if (!(1 <= n && n <= MAX_ENTRIES)) throw ToolError(cfmt("AFS entry count %lld is implausible", n));
    A.n = n;
    const std::string toc = fh.read(8 * n + 8);
    if (static_cast<long long>(toc.size()) < 8 * n) throw ToolError("AFS table of contents is truncated");
    for (long long i = 0; i < n; ++i) A.entries.emplace_back(u32(toc, 8 * i), u32(toc, 8 * i + 4));
    for (long long i = 0; i < n; ++i) {
        const auto [o, s] = A.entries[static_cast<std::size_t>(i)];
        if ((o || s) && (o < 8 + 8 * n || o + s > A.size))
            throw ToolError(cfmt("AFS entry %lld (offset 0x%llx, size %lld) points outside the file (%lld bytes): truncated?", i, o, s, A.size));
    }
    std::vector<long long> real;
    for (const auto& [o, s] : A.entries) if (o || s) real.push_back(o);
    std::sort(real.begin(), real.end());
    A.first = real.empty() ? align_up(8 + 8 * n, 2048) : real[0];
    const long long first = A.first;
    struct MC { std::string where; long long at, mo, ml; };
    std::vector<MC> cands;
    if (8 + 8 * n + 8 <= first && static_cast<long long>(toc.size()) >= 8 * n + 8) cands.push_back({"after_toc", 8 + 8 * n, u32(toc, 8 * n), u32(toc, 8 * n + 4)});
    if (first - 8 >= 8 + 8 * n && first - 8 != 8 + 8 * n) {
        fh.seek(first - 8);
        const std::string pair = fh.read(8);
        if (pair.size() == 8) cands.push_back({"before_data", first - 8, u32(pair, 0), u32(pair, 4)});
    }
    for (const auto& c : cands) {
        if (c.mo >= first && c.ml >= 48 * n && c.mo + 48 * n <= A.size) {
            A.has_meta = true; A.meta_off = c.mo; A.meta_len = c.ml; A.meta_at = c.at; A.meta_where = c.where;
            break;
        }
    }
    if (A.has_meta) {
        fh.seek(A.meta_off);
        const std::string mt = fh.read(48 * n);
        fh.seek(0);
        const std::string hdr_words = fh.read(4 + 4 * n + 4);
        std::vector<long long> tails;
        for (long long i = 0; i < n; ++i) {
            const std::string rec = mt.substr(static_cast<std::size_t>(48 * i), 48);
            const std::string nm = rec.substr(0, 32);
            A.names.push_back(nm.substr(0, nm.find('\0')));
            std::vector<long long> d;
            for (int k = 0; k < 6; ++k) {
                if (rec.size() < static_cast<std::size_t>(34 + 2 * k)) throw PyErr("struct.error", "unpack_from requires a buffer of at least 44 bytes for unpacking 12 bytes at offset 32");
                d.push_back(u16at(rec, static_cast<std::size_t>(32 + 2 * k)));
            }
            A.dates.push_back(d);
            tails.push_back(u32(rec, 44));
        }
        const auto all = [&](const std::function<long long(long long)>& f) { for (long long i = 0; i < n; ++i) if (tails[static_cast<std::size_t>(i)] != f(i)) return false; return true; };
        if (all([&](long long i) { return A.entries[static_cast<std::size_t>(i)].second; })) A.rule = "size";
        else if (all([&](long long i) { return A.entries[static_cast<std::size_t>(i)].first; })) A.rule = "offset";
        else if (static_cast<long long>(hdr_words.size()) >= 4 + 4 * n + 4 && all([&](long long i) { return u32(hdr_words, 4 + 4 * i); })) A.rule = "header_seq";
        else { A.rule = "raw"; A.has_raw = true; A.raw_tail = tails; }
    }
    return A;
}

// ---------------------------------------------------------------- scan
struct Sig { std::string magic, name; };
inline const std::vector<Sig>& embed_sigs() {
    static const std::vector<Sig> v = {{"TIM2", "TIM2"}, {"VAGp", "VAG"}, {"IECSsreV", "IECS"}, {std::string("AFS\0", 4), "AFS"},
                                       {"\x7f" "ELF", "ELF"}, {std::string("\x00\x00\x01\xba", 4), "MPEG-PS"}, {"PS2D", "icon.sys"}};
    return v;
}
inline Val embedded_signatures(const std::string& path) {
    Val found = Val::dict();
    BinFile fh(path);
    long long base = 0; std::string carry;
    while (true) {
        const std::string block = fh.read(chunk());
        if (block.empty()) break;
        const std::string data = carry + block;
        std::size_t i = 0;
        while (i < data.size()) {
            const Sig* hit = nullptr;
            for (const auto& s : embed_sigs()) if (data.compare(i, s.magic.size(), s.magic) == 0) { hit = &s; break; }
            if (!hit) { ++i; continue; }
            const std::size_t start = i; i += hit->magic.size();
            if (i <= carry.size()) continue;   // already counted in the previous chunk
            Val* rec = found.d->get(hit->name);
            if (!rec) { found.d->set(hit->name, dict_of({{"count", Val::integer(0)}, {"first_offsets", Val::list()}})); rec = found.d->get(hit->name); }
            rec->d->get("count")->i += 1;
            Val* fo = rec->d->get("first_offsets");
            if (fo->l->size() < 5) fo->l->push_back(Val::integer(base - static_cast<long long>(carry.size()) + static_cast<long long>(start)));
        }
        carry = data.size() > 7 ? data.substr(data.size() - 7) : data;
        base += static_cast<long long>(block.size());
    }
    return found;
}
inline Val scan_file(const std::string& path, bool deep) {
    const long long size = getsize(path);
    const std::string head = read_head(path);
    const auto [kind, detail] = sniff(path, head, size);
    const double ent = round_n(sample_entropy(path, size), 2);
    Val row = dict_of({{"path", Val::str(path)}, {"size", Val::integer(size)}, {"kind", Val::str(kind)}, {"detail", Val::str(detail)},
                       {"entropy", Val::flt(ent)}});
    row.d->set("entropy_label", Val::str(entropy_label(ent)));
    if (kind == "AFS") {
        try {
            const Afs a = parse_afs(path);
            row.d->set("toc", dict_of({{"kind", Val::str("AFS")}, {"n", Val::integer(a.n)}, {"score", Val::flt(1.0)}, {"names", Val::boolean(a.has_meta)}}));
        } catch (const ToolError& e) {
            row.d->set("toc", dict_of({{"kind", Val::str("AFS")}, {"error", Val::str(e.what())}}));
        }
    } else if ((kind == "data" || kind == "IECS") && size >= 64) {
        const auto c = detect_toc(head, size, 1);
        if (!c.empty() && c[0].score >= TOC_THRESHOLD) row.d->set("toc", cand_val(c[0], true));
    }
    if (deep) row.d->set("embedded", embedded_signatures(path));
    return row;
}

// os.walk(target), dirs sorted, regular non-link files only; rel = the path below target (os.sep joined)
struct Walked { std::string path, rel; };
inline void walk_dir(const std::string& base, const std::string& rel, const std::string& sep, std::vector<Walked>& out) {
    std::vector<std::string> names; std::error_code ec;
    for (std::filesystem::directory_iterator it(fspath(base), ec), end; !ec && it != end; it.increment(ec)) names.push_back(u8(it->path().filename()));
    if (ec && names.empty()) return;   // os.walk ignores scandir errors
    std::sort(names.begin(), names.end());
    std::vector<std::string> dirs;
    for (const auto& nm : names) {
        const std::string p = path_join(base, nm);
        if (is_dir(p)) { dirs.push_back(nm); continue; }
        if (is_file(p) && !ds_islink(p)) out.push_back({p, rel.empty() ? nm : rel + sep + nm});
    }
    for (const auto& d : dirs) {
        const std::string p = path_join(base, d);
        if (ds_islink(p)) continue;
        walk_dir(p, rel.empty() ? d : rel + sep + d, sep, out);
    }
}
inline std::vector<Walked> walk(const std::string& target, const std::string& sep) {
    std::vector<Walked> out;
    if (is_file(target)) { out.push_back({target, target}); return out; }
    walk_dir(target, "", sep, out);
    return out;
}

inline int cmd_scan(const Val& a) {
    const std::string target = S(a, "target");
    if (!exists(target)) throw ToolError(target + " does not exist");
    const long long min_size = get(a, "min_size").i;
    const bool embedded = truthy(get(a, "embedded"));
    const bool isdir = is_dir(target);
    std::vector<Val> rows;
    for (const auto& w : walk(target, ossep())) {
        if (getsize(w.path) < min_size) continue;
        Val r = scan_file(w.path, embedded);
        r.d->set("path", Val::str(isdir ? w.rel : w.path));
        rows.push_back(r);
    }
    const auto I = [](const Val& r, const char* k) { return r.d->get(k)->i; };
    const auto Sv = [](const Val& r, const char* k) { return r.d->get(k)->s; };
    if (S(a, "sort") == "size") std::stable_sort(rows.begin(), rows.end(), [&](const Val& x, const Val& y) { return I(x, "size") > I(y, "size"); });
    if (truthy(get(a, "json"))) { out(dumps2(Val::list(rows))); return 0; }
    if (rows.empty()) { out("no files"); return 0; }
    out(cfmt("%-10s %-9s %-5s %-24s %s", "size", "kind", "ent", "table of contents", "path"));
    for (const Val& r : rows) {
        const Val* t = r.d->get("toc");
        std::string ts;
        if (!t) ts = "";
        else if (t->d->get("kind")->s == "AFS") {
            const Val* nn = t->d->get("n");
            ts = nn ? cfmt("AFS, %lld entries%s", nn->i, truthy(t->d->at("names")) ? ", names" : "") : "AFS (broken)";
        } else ts = cfmt("%s n=%lld (%.2f)", t->d->get("kind")->s.c_str(), t->d->get("n")->i, t->d->get("score")->f);
        const std::string kind = Sv(r, "kind"), detail = Sv(r, "detail");
        out(cfmt("%-10s %-9s %-5.2f %-24s ", human(I(r, "size")).c_str(), kind.c_str(), r.d->get("entropy")->f, ts.c_str()) + Sv(r, "path") +
            ((!detail.empty() && kind != "data") ? "  [" + detail + "]" : ""));
        const Val* emb = r.d->get("embedded");
        if (embedded && emb && !emb->d->items.empty()) {
            auto items = emb->d->items;
            std::sort(items.begin(), items.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
            std::vector<std::string> parts;
            for (const auto& [k, v] : items) parts.push_back(cfmt("%s x%lld (first 0x%llx)", k.c_str(), v.d->get("count")->i, v.d->get("first_offsets")->l->at(0).i));
            out("           inside: " + join(parts, ", "));
        }
    }
    std::vector<std::pair<std::string, std::pair<long long, long long>>> kinds;
    long long total = 0;
    for (const Val& r : rows) {
        total += I(r, "size");
        auto it = std::find_if(kinds.begin(), kinds.end(), [&](const auto& kv) { return kv.first == Sv(r, "kind"); });
        if (it == kinds.end()) { kinds.push_back({Sv(r, "kind"), {0, 0}}); it = kinds.end() - 1; }
        it->second.first += 1; it->second.second += I(r, "size");
    }
    std::stable_sort(kinds.begin(), kinds.end(), [](const auto& x, const auto& y) { return x.second.second > y.second.second; });
    std::vector<std::string> kparts;
    for (const auto& [k, v] : kinds) kparts.push_back(cfmt("%s %lld (%s)", k.c_str(), v.first, human(v.second).c_str()));
    out(cfmt("\n%zu files, %s. By kind: ", rows.size(), human(total).c_str()) + join(kparts, ", "));
    std::vector<Val> big = rows;
    std::stable_sort(big.begin(), big.end(), [&](const Val& x, const Val& y) { return I(x, "size") > I(y, "size"); });
    if (big.size() > 5) big.resize(5);
    std::vector<std::string> bparts;
    for (const Val& r : big) bparts.push_back(Sv(r, "path") + " (" + human(I(r, "size")) + ")");
    out("Biggest: " + join(bparts, ", "));
    return 0;
}
// ---------------------------------------------------------------- unpack
inline std::string safe_name(const std::string& raw, const std::string& fallback) {   // raw latin-1 bytes in, ASCII out
    const std::size_t k = raw.find_last_of("/\\");
    const std::string last = k == std::string::npos ? raw : raw.substr(k + 1);
    std::string name;
    for (const char ch : last) {
        const unsigned c = static_cast<unsigned char>(ch);
        name += (c < 128 && (std::isalnum(static_cast<int>(c)) || c == '-' || c == '_' || c == '.')) ? static_cast<char>(c) : '_';
    }
    std::size_t a = 0, b = name.size();
    while (a < b && name[a] == '.') ++a;
    while (b > a && name[b - 1] == '.') --b;
    name = name.substr(a, std::min<std::size_t>(b - a, 64));
    return name.empty() ? fallback : name;
}
inline std::string guess_ext(const std::string& blob) {
    const std::string k = blob.empty() ? "empty" : sniff("", blob.substr(0, 64), static_cast<long long>(blob.size())).first;
    static const std::map<std::string, std::string> m = {{"TIM2", ".tm2"}, {"VAG", ".vag"}, {"AFS", ".afs"}, {"ELF", ".elf"}, {"IRX", ".irx"},
        {"MPEG-PS", ".pss"}, {"IECS", ".hd"}, {"gzip", ".gz"}, {"ZIP", ".zip"}, {"RIFF", ".riff"}, {"text", ".txt"}, {"PNG", ".png"}, {"icon.sys", ".sys"}};
    const auto it = m.find(k);
    return it == m.end() ? ".bin" : it->second;
}
inline int pad_byte(BinFile& fh, const std::vector<Ent>& ranges) {
    std::vector<std::pair<int, long long>> counts;   // insertion order, like the dict
    long long idx[256]; std::fill(std::begin(idx), std::end(idx), -1LL);
    for (const auto& [a, b] : ranges) {
        if (b - a <= 0) continue;
        fh.seek(a);
        for (const char ch : fh.read(std::min(b - a, 4096LL))) {
            const int x = static_cast<unsigned char>(ch);
            if (idx[x] < 0) { idx[x] = static_cast<long long>(counts.size()); counts.push_back({x, 0}); }
            counts[static_cast<std::size_t>(idx[x])].second++;
        }
    }
    if (counts.empty()) return 0;
    std::size_t best = 0;   // max(..., key=(count, x == 0)): the first maximal item wins
    for (std::size_t i = 1; i < counts.size(); ++i) {
        const auto& c = counts[i]; const auto& m = counts[best];
        if (c.second > m.second || (c.second == m.second && c.first == 0 && m.first != 0)) best = i;
    }
    return counts[best].first;
}
inline bool is_pad(BinFile& fh, long long a, long long b, int pad) {
    fh.seek(a);
    long long left = b - a;
    while (left > 0) {
        const std::string c = fh.read(std::min(left, 65536LL));
        if (c.empty()) return true;
        for (const char ch : c) if (static_cast<unsigned char>(ch) != pad) return false;
        left -= static_cast<long long>(c.size());
    }
    return true;
}

inline int cmd_unpack(const Val& a) {
    const std::string file = S(a, "file"), outdir = S(a, "out");
    need_file(file);
    const long long size = getsize(file);
    const bool force = truthy(get(a, "force"));
    if (exists(outdir) && !listdir(outdir).empty() && !force) throw ToolError(outdir + " is not empty (pass --force to write into it)");
    const std::string head = read_head(file);
    std::string fmt = S(a, "format");
    if (fmt == "auto") fmt = head.substr(0, 4) == std::string("AFS\0", 4) ? "afs" : "toc";
    Val index = dict_of({{"tool", Val::str("datascan.py")}, {"version", Val::integer(1)}, {"format", Val::str(fmt)}, {"source", Val::str(ds_basename(file))},
                         {"source_size", Val::integer(size)}, {"source_sha256", Val::str(sha256_file(file))}});
    std::vector<Ent> regen, ents; std::vector<std::string> names; bool have_names = false;
    Val layout = Val::dict(); long long first = 0; Afs afs; long long off_scale = 1;
    if (fmt == "afs") {
        afs = parse_afs(file);
        ents = afs.entries;
        layout = dict_of({{"kind", Val::str("afs")}, {"n", Val::integer(afs.n)}, {"table_at", Val::integer(8)}, {"count_at", Val::integer(4)},
                          {"off_scale", Val::integer(1)}, {"size_scale", Val::integer(1)}, {"sentinel", Val::boolean(false)}});
        first = afs.first;
        if (afs.has_meta) {
            have_names = true; names = afs.names;
            List rt; for (const long long t : afs.raw_tail) rt.push_back(Val::integer(t));
            index.d->set("afs", dict_of({{"meta_where", Val::str(afs.meta_where)}, {"meta_ptr_at", Val::integer(afs.meta_at)}, {"meta_offset", Val::integer(afs.meta_off)},
                                         {"meta_length", Val::integer(afs.meta_len)}, {"name_rule", Val::str(afs.rule)}, {"raw_tail", afs.has_raw ? Val::list(rt) : Val()}}));
            regen.emplace_back(afs.meta_off, afs.meta_off + 48 * afs.n);
        } else {
            index.d->set("afs", dict_of({{"meta_where", Val()}}));
        }
    } else {
        auto cands = detect_toc(head, size);
        const Val hdr = get(a, "header"); const std::string lay = S(a, "layout");
        if (!hdr.is_none() || !lay.empty()) {
            std::vector<Cand> keep;
            for (const auto& c : cands) if ((hdr.is_none() || c.header == hdr.i) && (lay.empty() || c.kind == lay)) keep.push_back(c);
            cands = keep;
        }
        if (cands.empty() || cands[0].score < (truthy(get(a, "force_toc")) ? 0.0 : TOC_THRESHOLD)) {
            const double best = cands.empty() ? 0 : cands[0].score;
            throw Refused(cfmt("no table of contents found in %s (best guess scored %.2f, need %.2f). This format needs"
                               " per-game work: find the loader in Ghidra, or try --layout/--header with --force-toc.", file.c_str(), best, TOC_THRESHOLD));
        }
        const Cand& c = cands[0];
        ents = c.entries;
        layout = dict_of({{"kind", Val::str(c.kind)}, {"n", Val::integer(c.n)}, {"header", Val::integer(c.header)}, {"count_at", c.count_at < 0 ? Val() : Val::integer(c.count_at)},
                          {"table_at", Val::integer(c.table_at)}, {"off_scale", Val::integer(c.off_scale)}, {"size_scale", Val::integer(c.size_scale)},
                          {"sentinel", Val::boolean(c.sentinel)}, {"table_end", Val::integer(c.table_end)}, {"score", Val::flt(c.score)}});
        off_scale = c.off_scale;
        long long mn = -1;
        for (const auto& [o, s] : ents) if (o || s) mn = mn < 0 ? o : std::min(mn, o);
        first = mn < 0 ? c.table_end : mn;
    }
    std::vector<Ent> real_sorted;
    for (const auto& e : ents) if (e.first || e.second) real_sorted.push_back(e);
    std::sort(real_sorted.begin(), real_sorted.end());
    std::vector<long long> ro; for (const auto& e : real_sorted) ro.push_back(e.first);
    const long long align = std::max(real_sorted.empty() ? 1 : alignment_of(ro), off_scale);
    index.d->set("layout", layout);
    index.d->set("alignment", Val::integer(align));
    const std::string files_dir = path_join(outdir, "files");
    makedirs(files_dir);
    List entries; std::set<std::string> used;
    std::vector<Val> extras;
    int pad = 0;
    {
        BinFile fh(file);
        fh.seek(0);
        write_bytes(path_join(outdir, "_header.bin"), fh.read(first));
        index.d->set("header_file", Val::str("_header.bin"));
        std::set<long long> bset;
        for (const auto& e : real_sorted) bset.insert(e.first);
        for (const auto& r : regen) bset.insert(r.first);
        bset.insert(size);
        for (std::size_t i = 0; i < ents.size(); ++i) {
            const auto [o, s] = ents[i];
            Val e = dict_of({{"index", Val::integer(static_cast<long long>(i))}, {"offset", Val::integer(o)}, {"size", Val::integer(s)}});
            if (fmt == "afs") {
                e.d->set("name", have_names ? Val::str(latin1(names[i])) : Val());
                if (have_names) { List d; for (const long long x : afs.dates[i]) d.push_back(Val::integer(x)); e.d->set("date", Val::list(d)); }
                else e.d->set("date", Val());
            }
            if (!(o || s)) {
                e.d->set("file", Val()); e.d->set("slot", Val::integer(0)); e.d->set("empty", Val::boolean(true));
                entries.push_back(e);
                continue;
            }
            const auto nx = bset.upper_bound(o);
            e.d->set("slot", Val::integer(s ? (nx == bset.end() ? size : *nx) - o : 0));
            fh.seek(o);
            const std::string blob = fh.read(s);
            const std::string base = (have_names && !names[i].empty()) ? safe_name(names[i], "") : "";
            std::string fname = !base.empty() ? cfmt("%04zu_", i) + base : cfmt("%04zu", i) + guess_ext(blob);
            while (used.count(fname)) fname = "_" + fname;
            used.insert(fname);
            write_bytes(path_join(files_dir, fname), blob);
            e.d->set("file", Val::str("files/" + fname));
            e.d->set("sha256", Val::str(sha256_bytes(blob)));
            entries.push_back(e);
        }
        std::vector<Ent> covered;
        for (const auto& [o, s] : real_sorted) covered.emplace_back(o, o + s);
        for (const auto& r : regen) covered.push_back(r);
        std::sort(covered.begin(), covered.end());
        std::vector<Ent> gaps; long long pos = first;
        for (const auto& [x, y] : covered) { if (x > pos) gaps.emplace_back(pos, x); pos = std::max(pos, y); }
        if (pos < size) gaps.emplace_back(pos, size);
        pad = pad_byte(fh, gaps);
        long long last_end = first;
        for (const auto& c : covered) last_end = std::max(last_end, c.second);
        for (const auto& [x, y] : gaps) {
            if (!is_pad(fh, x, y, pad)) {
                fh.seek(x);
                const std::string name = cfmt("_extra_%08llx.bin", x);
                write_bytes(path_join(outdir, name), fh.read(y - x));
                extras.push_back(dict_of({{"offset", Val::integer(x)}, {"length", Val::integer(y - x)}, {"file", Val::str(name)}, {"after_last", Val::boolean(x >= last_end)}}));
            }
        }
    }
    index.d->set("pad_byte", Val::integer(pad));
    index.d->set("extras", Val::list(extras));
    index.d->set("entries", Val::list(entries));
    { DumpOpts o; o.indent = 1; write_bytes(path_join(outdir, INDEX_NAME), dumps(index, o)); }
    long long n_files = 0;
    for (const Val& e : entries) if (truthy(e.d->at("file"))) ++n_files;
    if (truthy(get(a, "json"))) {
        out(dumps2(dict_of({{"out", Val::str(outdir)}, {"format", Val::str(fmt)}, {"entries", Val::integer(n_files)}, {"alignment", Val::integer(align)},
                            {"extras", Val::integer(static_cast<long long>(extras.size()))}})));
    } else {
        const std::string desc = fmt == "afs" ? "AFS" : cfmt("%s table at 0x%llx (confidence %.2f)", layout.d->get("kind")->s.c_str(), layout.d->get("table_at")->i, layout.d->get("score")->f);
        out(cfmt("unpacked %lld entries from ", n_files) + file + " (" + desc + cfmt(", %lld-byte alignment) into ", align) + files_dir);
        if (!extras.empty()) out(cfmt("kept %zu byte range(s) outside the entries as _extra_*.bin (repack puts them back)", extras.size()));
        out("edit the files (same names), then: datascan.py repack " + outdir + " --out <new archive>");
    }
    return 0;
}

// ---------------------------------------------------------------- repack
inline Val req(const Val& d, const std::string& k) {   // d[k]
    if (d.t != Val::DICT) throw PyErr("TypeError", "index.json value is not an object");
    const Val* p = d.d->get(k);
    if (!p) throw PyErr("KeyError", str_repr(k));
    return *p;
}
inline Val opt(const Val& d, const std::string& k, Val dflt = Val()) {   // d.get(k, dflt)
    if (d.t != Val::DICT) throw PyErr("AttributeError", "index.json value is not an object (no attribute 'get')");
    const Val* p = d.d->get(k);
    return p ? *p : dflt;
}
inline long long ii(const Val& v) {   // a JSON value used where Python needs an int
    if (v.t == Val::BOOL) return v.b ? 1 : 0;
    if (v.t == Val::INT && !v.big()) return v.i;
    throw PyErr("TypeError", "index.json: expected an integer, got " + dumps(v));
}
inline Val load_index(const std::string& folder) {
    const std::string p = path_join(folder, INDEX_NAME);
    if (!is_file(p)) throw ToolError(folder + " has no " + INDEX_NAME + " (run unpack first)");
    Val idx;
    try {
        const std::string raw = read_bytes(p);
        const std::string txt = decode_utf8_replace(raw);
        if (txt != raw) throw PyErr("ValueError", "utf-8");   // UnicodeDecodeError is a ValueError
        idx = loads(text_u(raw));
    } catch (const PyErr& e) {
        if (std::string(e.what()).find("OSError") != std::string::npos) throw;
        throw ToolError(p + " is not valid JSON");
    }
    if (idx.t != Val::DICT) throw PyErr("AttributeError", "'" + std::string(idx.t == Val::LIST ? "list" : "object") + "' object has no attribute 'get'");
    const Val* tool = idx.d->get("tool");
    if (!tool || tool->t != Val::STR || tool->s != "datascan.py" || !idx.d->has("entries") || !idx.d->has("layout"))
        throw ToolError(p + " was not written by datascan.py unpack");
    return idx;
}
inline std::optional<std::string> inside(const std::string& folder, const Val& rel) {
    if (rel.is_none()) return std::nullopt;
    if (rel.t != Val::STR) throw PyErr("TypeError", "expected str, bytes or os.PathLike object");
    std::string r = rel.s;
    std::replace(r.begin(), r.end(), '\\', '/');
    bool dotdot = false; std::size_t a = 0;
    while (true) { const std::size_t k = r.find('/', a); if (r.substr(a, k == std::string::npos ? std::string::npos : k - a) == "..") dotdot = true; if (k == std::string::npos) break; a = k + 1; }
    if (ds_isabs(rel.s) || dotdot) throw ToolError("index.json names a path outside the folder: " + str_repr(rel.s));
    const std::string p = path_join(folder, rel.s);
    if (!is_file(p)) throw ToolError("missing " + p + " (named in index.json)");
    return p;
}
// out[a:b] = d (Python slice assignment, step 1)
inline void slice_assign(std::string& o, long long a, long long b, const std::string& d) {
    const long long L = static_cast<long long>(o.size());
    const auto norm = [&](long long x) { if (x < 0) { x += L; if (x < 0) x = 0; } else if (x > L) x = L; return x; };
    a = norm(a); b = norm(b); if (b < a) b = a;
    o.replace(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a), d);
}
// struct.pack_into("<I", buf, at, v)
inline void pack_u32(std::string& buf, long long at, long long v) {
    const long long L = static_cast<long long>(buf.size());
    if (at < 0) {
        if (at + 4 > 0) throw PyErr("struct.error", cfmt("no space to pack 4 bytes at offset %lld", at));
        if (at + L < 0) throw PyErr("struct.error", cfmt("offset %lld out of range for %lld-byte buffer", at, L));
        at += L;
    }
    if (L - at < 4) throw PyErr("struct.error", cfmt("pack_into requires a buffer of at least %lld bytes for packing 4 bytes at offset %lld (actual buffer size is %lld)", at + 4, at, L));
    if (v < 0 || v > 0xFFFFFFFFLL) throw PyErr("struct.error", "'I' format requires 0 <= number <= 4294967295");
    for (int k = 0; k < 4; ++k) buf[static_cast<std::size_t>(at + k)] = static_cast<char>((v >> (8 * k)) & 0xFF);
}

struct Built { std::string data; Val report; };
inline Built build_archive(const std::string& folder, const std::string& mode) {
    Val idx = load_index(folder);
    const Val lay = req(idx, "layout");
    long long align = to_int(opt(idx, "alignment", Val::integer(1)));
    if (!align) align = 1;
    const long long pad = to_int(opt(idx, "pad_byte", Val::integer(0)));
    std::string header = read_bytes(*inside(folder, req(idx, "header_file")));
    const long long first = static_cast<long long>(header.size());
    const long long size0 = to_int(req(idx, "source_size"));
    const Val entsv = req(idx, "entries");
    if (entsv.t != Val::LIST) throw PyErr("TypeError", "index.json entries is not a list");
    const List& ents = *entsv.l;
    std::vector<std::optional<std::string>> data;
    for (const Val& e : ents) { const auto p = inside(folder, opt(e, "file")); data.push_back(p ? std::optional<std::string>(read_bytes(*p)) : std::nullopt); }
    const auto key = [](const Val& e) { return dumps(req(e, "index")); };
    List changed;
    for (std::size_t i = 0; i < ents.size(); ++i) {
        if (!data[i]) continue;
        const Val sh = opt(ents[i], "sha256");
        if (!(sh.t == Val::STR && sh.s == sha256_bytes(*data[i]))) changed.push_back(req(ents[i], "index"));
    }
    bool fits = true;
    for (std::size_t i = 0; i < ents.size(); ++i) if (data[i] && static_cast<long long>(data[i]->size()) > ii(req(ents[i], "slot"))) fits = false;
    if (mode == "keep" && !fits) {
        for (std::size_t i = 0; i < ents.size(); ++i) if (data[i] && static_cast<long long>(data[i]->size()) > ii(req(ents[i], "slot")))
            throw Refused("entry " + pystr(req(ents[i], "index")) + cfmt(" is now %zu bytes but its slot holds %lld; --layout keep cannot move anything"
                          " (shrink it, or use --layout pack if the game reads offsets from this table)", data[i]->size(), ii(req(ents[i], "slot"))));
    }
    const bool use_keep = mode == "keep" || (mode == "auto" && fits);
    Val afs = opt(idx, "afs"); if (!truthy(afs)) afs = Val::dict();
    const long long meta_len = truthy(opt(afs, "meta_where")) ? 48 * static_cast<long long>(ents.size()) : 0;
    const Val ml = opt(afs, "meta_length");
    const long long meta_room = meta_len ? std::max(meta_len, truthy(ml) ? to_int(ml) : 0LL) : 0;
    std::map<std::string, long long> placed;
    long long end = 0; Val meta_at;
    Val extrasv = opt(idx, "extras", Val::list());
    if (extrasv.t != Val::LIST) throw PyErr("TypeError", "index.json extras is not a list");
    std::map<std::size_t, long long> new_at;   // x["_new"] for tail extras
    if (use_keep) {
        for (std::size_t i = 0; i < ents.size(); ++i) if (data[i]) placed[key(ents[i])] = ii(req(ents[i], "offset"));
        end = size0;
        meta_at = opt(afs, "meta_offset");
    } else {
        long long pos = first;
        for (std::size_t i = 0; i < ents.size(); ++i) {
            if (!data[i]) continue;
            pos = align_up(pos, align);
            placed[key(ents[i])] = pos;
            pos += static_cast<long long>(data[i]->size());
        }
        if (meta_len) { pos = align_up(pos, align); meta_at = Val::integer(pos); pos += meta_room; }
        for (std::size_t k = 0; k < extrasv.l->size(); ++k) {
            const Val& x = (*extrasv.l)[k];
            if (!truthy(opt(x, "after_last"))) continue;
            pos = align_up(pos, align);
            new_at[k] = pos;
            pos += to_int(req(x, "length"));
        }
        end = align_up(pos, align);
    }
    if (pad < 0 || pad > 255) throw PyErr("ValueError", "byte must be in range(0, 256)");
    std::string outb(static_cast<std::size_t>(std::max(0LL, end)), static_cast<char>(pad));
    slice_assign(outb, 0, first, header);
    std::map<std::string, long long> sizes;
    const long long ssc_l = ii(req(lay, "size_scale"));
    for (std::size_t i = 0; i < ents.size(); ++i) {
        if (!data[i]) continue;
        const long long o = placed.at(key(ents[i]));
        std::string d = *data[i];
        if (ssc_l > 1) d += std::string(static_cast<std::size_t>(align_up(static_cast<long long>(d.size()), ssc_l) - static_cast<long long>(d.size())), static_cast<char>(pad));
        slice_assign(outb, o, o + static_cast<long long>(d.size()), d);
        sizes[key(ents[i])] = static_cast<long long>(d.size());
    }
    long long dropped = 0;
    for (std::size_t k = 0; k < extrasv.l->size(); ++k) {
        const Val& x = (*extrasv.l)[k];
        const std::string blob = read_bytes(*inside(folder, req(x, "file")));
        const auto L = static_cast<long long>(blob.size());
        if (use_keep) { const long long o = ii(req(x, "offset")); slice_assign(outb, o, o + L, blob); }
        else if (new_at.count(k)) slice_assign(outb, new_at[k], new_at[k] + L, blob);
        else dropped += L;
    }
    const auto n = static_cast<long long>(ents.size());
    const long long osc = ii(req(lay, "off_scale")), ssc = ssc_l;
    const Val kindv = req(lay, "kind");
    const std::string kind = kindv.t == Val::STR ? kindv.s : "";
    const auto put = [&](long long at, long long v) {
        if (at + 4 > first) throw ToolError(cfmt("table field at 0x%llx is outside the saved header", at));
        pack_u32(outb, at, v);
    };
    std::vector<Ent> offs_bytes;
    for (const Val& e : ents) {
        const auto it = placed.find(key(e));
        offs_bytes.push_back(it != placed.end() ? Ent{it->second, sizes.at(key(e))} : Ent{0, 0});
    }
    const bool pairs = kind == "afs" || kind == "count+pairs" || kind == "pairs";
    for (long long i = 0; i < n; ++i) {
        auto [o, s] = offs_bytes[static_cast<std::size_t>(i)];
        if (pymod(o, osc)) throw ToolError(cfmt("entry %lld offset 0x%llx is not a multiple of the table's unit %lld", i, o, osc));
        if (pairs) {
            put(ii(req(lay, "table_at")) + 8 * i, floordiv(o, osc));
            put(ii(req(lay, "table_at")) + 8 * i + 4, -floordiv(-s, ssc));
        } else {
            if (o == 0 && s == 0) o = i == n - 1 ? end : 0;
            put(ii(req(lay, "table_at")) + 4 * i, floordiv(o, osc));
        }
    }
    if (kind == "count+offsets" && truthy(opt(lay, "sentinel"))) {
        long long last_end = first;
        if (!offs_bytes.empty()) { last_end = offs_bytes[0].first + offs_bytes[0].second; for (const auto& [o, s] : offs_bytes) last_end = std::max(last_end, o + s); }
        put(ii(req(lay, "table_at")) + 4 * n, floordiv(align_up(last_end, osc), osc));
    }
    if (meta_len) {
        const long long ptr_at = ii(req(afs, "meta_ptr_at"));
        if (ptr_at + 8 > static_cast<long long>(outb.size())) throw ToolError("AFS name-table pointer position is outside the archive");
        if (meta_at.is_none()) throw PyErr("struct.error", "required argument is not an integer");
        const long long mat = ii(meta_at);
        pack_u32(outb, ptr_at, mat);
        pack_u32(outb, ptr_at + 4, truthy(ml) ? to_int(ml) : meta_len);
        const Val rulev = opt(afs, "name_rule");
        const std::string rule = rulev.t == Val::STR ? rulev.s : "";
        const Val rtv = opt(afs, "raw_tail");
        const List raw_tail = truthy(rtv) && rtv.t == Val::LIST ? *rtv.l : List{};
        for (long long i = 0; i < n; ++i) {
            const Val& e = ents[static_cast<std::size_t>(i)];
            std::string rec(48, '\0');
            const Val nmv = opt(e, "name");
            std::string nm;
            if (truthy(nmv)) {
                if (nmv.t != Val::STR) throw PyErr("AttributeError", "name has no attribute 'encode'");
                for (const auto& ch : cps(nmv.s)) { const uint32_t c = cp_of(ch); nm += c <= 0xFF ? static_cast<char>(c) : '?'; }
            }
            if (nm.size() > 32) nm.resize(32);
            rec.replace(0, nm.size(), nm);
            const Val dv = opt(e, "date");
            List dl; if (truthy(dv)) { if (dv.t != Val::LIST) throw PyErr("TypeError", "date is not a list"); dl = *dv.l; } else dl = List(6, Val::integer(0));
            if (dl.size() != 6) throw PyErr("struct.error", cfmt("pack_into expected 6 items for packing (got %zu)", dl.size()));
            for (int k = 0; k < 6; ++k) { const long long v = to_int(dl[static_cast<std::size_t>(k)]) & 0xFFFF; rec[static_cast<std::size_t>(32 + 2 * k)] = static_cast<char>(v & 0xFF); rec[static_cast<std::size_t>(33 + 2 * k)] = static_cast<char>(v >> 8); }
            const auto [o, s] = offs_bytes[static_cast<std::size_t>(i)];
            long long t;
            if (rule == "size") t = s;
            else if (rule == "offset") t = o;
            else if (rule == "header_seq") t = u32(outb, 4 + 4 * i);
            else t = i < static_cast<long long>(raw_tail.size()) ? ii(raw_tail[static_cast<std::size_t>(i)]) : 0;
            pack_u32(rec, 44, t);
            slice_assign(outb, mat + 48 * i, mat + 48 * i + 48, rec);
        }
    }
    long long moved = 0;
    for (const Val& e : ents) { const auto it = placed.find(key(e)); if (it != placed.end() && it->second != ii(req(e, "offset"))) ++moved; }
    Val report = dict_of({{"layout", Val::str(use_keep ? "keep" : "pack")}, {"changed", Val::list(changed)}, {"size", Val::integer(static_cast<long long>(outb.size()))},
                          {"original_size", Val::integer(size0)}, {"dropped_extra_bytes", Val::integer(dropped)}, {"moved", Val::integer(moved)}});
    return {std::move(outb), report};
}
inline bool verify(const std::string& path, const Val& idx, const std::string& folder) {
    const long long size = getsize(path);
    const Val lay = req(idx, "layout");
    const List& ents = *req(idx, "entries").l;
    BinFile fh(path);
    const long long table_at = ii(opt(lay, "table_at", Val::integer(8)));
    const std::string head = fh.read(std::max(table_at + 8 * static_cast<long long>(ents.size()) + 8, 64LL));
    const Val kv = req(lay, "kind"); const std::string kind = kv.t == Val::STR ? kv.s : "";
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const auto p = inside(folder, opt(ents[i], "file"));
        if (!p) continue;
        const std::string want = read_bytes(*p);
        const long long ta = ii(req(lay, "table_at")), osc = ii(req(lay, "off_scale"));
        const long long o = (kind == "afs" || kind == "count+pairs" || kind == "pairs") ? u32(head, ta + 8 * static_cast<long long>(i)) * osc : u32(head, ta + 4 * static_cast<long long>(i)) * osc;
        if (o + static_cast<long long>(want.size()) > size) return false;
        fh.seek(o);
        if (fh.read(static_cast<long long>(want.size())) != want) return false;
    }
    return true;
}
inline int cmd_repack(const Val& a) {
    const std::string folder = S(a, "folder"), outp = S(a, "out");
    if (!is_dir(folder)) throw ToolError(folder + " is not a folder");
    if (exists(outp) && !truthy(get(a, "force"))) throw ToolError(outp + " exists (pass --force to overwrite; never overwrite your only copy of the original)");
    Built b = build_archive(folder, S(a, "layout"));
    const std::string tmp = outp + ".tmp";
    makedirs(dirname(abspath(outp)));
    write_bytes(tmp, b.data);
    replace_file(tmp, outp);
    const Val idx = load_index(folder);
    const bool ok = verify(outp, idx, folder);
    Val& rep = b.report;
    const long long nchanged = static_cast<long long>(rep.d->get("changed")->l->size()), moved = rep.d->get("moved")->i, dropped = rep.d->get("dropped_extra_bytes")->i;
    if (truthy(get(a, "json"))) {
        rep.d->set("verified", Val::boolean(ok));
        rep.d->set("out", Val::str(outp));
        out(dumps2(rep));
    } else {
        out("wrote " + outp + ": " + human(rep.d->get("size")->i) + " (" + human(rep.d->get("original_size")->i) + " before), layout " + rep.d->get("layout")->s +
            cfmt(", %lld entr%s changed", nchanged, nchanged == 1 ? "y" : "ies") + (moved ? cfmt(", %lld moved to new offsets", moved) : ""));
        if (rep.d->get("layout")->s == "pack" && moved)
            out("note: offsets changed. Fine when the game reads this table; if it hard-codes offsets (in the ELF), shrink your edit and use --layout keep.");
        if (dropped) out(cfmt("WARNING: %lld bytes found between entries were dropped by the pack layout", dropped));
        out(ok ? "verified: every entry reads back" : "WARNING: read-back check failed");
    }
    return ok && !dropped ? 0 : 1;
}

// ---------------------------------------------------------------- diff
struct DiffRes { std::vector<Ent> ranges; long long count = 0, sa = 0, sb = 0, extra = 0; };
inline DiffRes diff_ranges(const std::string& pa, const std::string& pb, long long gap = 8, long long keep = 200) {
    DiffRes r; r.sa = getsize(pa); r.sb = getsize(pb);
    bool have = false; long long c0 = 0, c1 = 0;
    const auto flush = [&]() { if (static_cast<long long>(r.ranges.size()) < keep) r.ranges.emplace_back(c0, c1); else r.extra += 1; };
    BinFile fa(pa), fb(pb);
    long long off = 0;
    while (true) {
        const std::string a = fa.read(chunk()), b = fb.read(chunk());
        const std::size_t n = std::min(a.size(), b.size());
        if (n == 0) break;
        if (std::memcmp(a.data(), b.data(), n) != 0) {
            for (std::size_t k = 0; k < n; ++k) {
                if (a[k] == b[k]) continue;
                const long long p = off + static_cast<long long>(k);
                r.count += 1;
                if (have && p - c1 <= gap) c1 = p + 1;
                else { if (have) flush(); have = true; c0 = p; c1 = p + 1; }
            }
        }
        off += static_cast<long long>(n);
        if (a.size() != b.size()) break;
    }
    if (have) flush();
    return r;
}
inline std::pair<long long, std::vector<std::string>> values_of(const std::string& path, long long a, long long b, const std::string& as) {
    const long long w = as == "u8" ? 1 : as == "u16" ? 2 : 4;
    const long long start = a - pymod(a, w);
    BinFile fh(path);
    fh.seek(start);
    const std::string blob = fh.read(std::min(align_up(b, w) - start, 4 * w));
    std::vector<std::string> vals;
    for (long long i = 0; i + w <= static_cast<long long>(blob.size()); i += w) {
        const auto o = static_cast<std::size_t>(i);
        if (as == "u8") vals.push_back(std::to_string(bt(blob, o)));
        else if (as == "u16") vals.push_back(std::to_string(u16at(blob, o)));
        else {
            const auto u = static_cast<uint32_t>(u32(blob, i));
            if (as == "u32") vals.push_back(std::to_string(u));
            else { float f; std::memcpy(&f, &u, 4); const double d = f; vals.push_back(std::isnan(d) ? "nan" : cfmt("%g", d)); }
        }
    }
    return {start, vals};
}
inline std::string preview(const std::string& path, long long a, long long n = 16) {
    BinFile fh(path); fh.seek(a);
    std::string h; for (const char ch : fh.read(n)) h += cfmt("%02x", static_cast<unsigned char>(ch));
    return h;
}
inline int diff_dirs(const Val& args) {
    const std::string A = S(args, "a"), B = S(args, "b");
    std::map<std::string, std::string> fa, fb;
    for (const auto& w : walk(A, "/")) fa[w.rel] = w.path;
    for (const auto& w : walk(B, "/")) fb[w.rel] = w.path;
    std::vector<std::string> added, removed; List changed;
    for (const auto& [k, v] : fb) if (!fa.count(k)) added.push_back(k);
    for (const auto& [k, v] : fa) if (!fb.count(k)) removed.push_back(k);
    const long long gap = get(args, "gap").i;
    for (const auto& [rel, pa] : fa) {
        const auto it = fb.find(rel);
        if (it == fb.end()) continue;
        const std::string& pb = it->second;
        if (getsize(pa) == getsize(pb) && sha256_file(pa) == sha256_file(pb)) continue;
        const DiffRes r = diff_ranges(pa, pb, gap, 1000);
        changed.push_back(dict_of({{"path", Val::str(rel)}, {"size_a", Val::integer(r.sa)}, {"size_b", Val::integer(r.sb)}, {"differing_bytes", Val::integer(r.count)},
                                   {"ranges", Val::integer(static_cast<long long>(r.ranges.size()) + r.extra)}}));
    }
    const bool same = added.empty() && removed.empty() && changed.empty();
    if (truthy(get(args, "json"))) {
        out(dumps2(dict_of({{"added", str_list(added)}, {"removed", str_list(removed)}, {"changed", Val::list(changed)}})));
    } else {
        if (same) out(cfmt("identical folders (%zu files)", fa.size()));
        for (const auto& r : added) out("  added    " + r);
        for (const auto& r : removed) out("  removed  " + r);
        for (const Val& c : changed) {
            const long long sa = c.d->get("size_a")->i, sb = c.d->get("size_b")->i;
            out("  changed  " + c.d->get("path")->s + cfmt(": %lld bytes in %lld range(s)", c.d->get("differing_bytes")->i, c.d->get("ranges")->i) +
                (sa == sb ? "" : cfmt(", size %lld -> %lld", sa, sb)));
        }
    }
    return same ? 0 : 1;
}
inline int cmd_diff(const Val& args) {
    const std::string a = S(args, "a"), b = S(args, "b");
    for (const auto& p : {a, b}) if (!exists(p)) throw ToolError(p + " does not exist");
    if (is_dir(a) != is_dir(b)) throw ToolError("compare two files or two folders, not one of each");
    if (is_dir(a)) return diff_dirs(args);
    const long long gap = get(args, "gap").i;
    const DiffRes r = diff_ranges(a, b, gap, get(args, "max").i);
    const std::string as = S(args, "as_type");
    List rows;
    for (const auto& [r0, r1] : r.ranges) {
        Val row = dict_of({{"start", Val::integer(r0)}, {"end", Val::integer(r1)}, {"length", Val::integer(r1 - r0)}, {"a", Val::str(preview(a, r0))}, {"b", Val::str(preview(b, r0))}});
        if (!as.empty()) {
            const auto va = values_of(a, r0, r1, as), vb = values_of(b, r0, r1, as);
            row.d->set("values_at", Val::integer(va.first));
            row.d->set("a_values", str_list(va.second));
            row.d->set("b_values", str_list(vb.second));
        }
        rows.push_back(row);
    }
    const Val res = dict_of({{"a", Val::str(a)}, {"b", Val::str(b)}, {"size_a", Val::integer(r.sa)}, {"size_b", Val::integer(r.sb)}, {"differing_bytes", Val::integer(r.count)},
                             {"ranges", Val::list(rows)}, {"more_ranges", Val::integer(r.extra)}});
    const bool same = r.count == 0 && r.sa == r.sb;
    if (truthy(get(args, "json"))) { out(dumps2(res)); return same ? 0 : 1; }
    if (same) { out(cfmt("identical (%lld bytes)", r.sa)); return 0; }
    if (r.sa != r.sb)
        out("sizes differ: " + a + cfmt(" is %lld bytes, ", r.sa) + b + cfmt(" is %lld bytes (the last %lld bytes exist only in the %s file)", r.sb, std::llabs(r.sa - r.sb), r.sb > r.sa ? "second" : "first"));
    out(cfmt("%lld differing bytes in %lld range(s)", r.count, static_cast<long long>(r.ranges.size()) + r.extra) + (r.extra ? cfmt(", first %zu shown", r.ranges.size()) : "") +
        cfmt(" (ranges closer than %lld bytes merged)", gap));
    for (const Val& row : rows) {
        const long long st = row.d->get("start")->i, en = row.d->get("end")->i;
        out("  0x" + hex08(st) + "-0x" + hex08(en - 1) + cfmt("  %6lld bytes  a=", row.d->get("length")->i) + row.d->get("a")->s + "  b=" + row.d->get("b")->s);
        if (!as.empty()) {
            std::vector<std::string> va, vb;
            for (const Val& v : *row.d->get("a_values")->l) va.push_back(v.s);
            for (const Val& v : *row.d->get("b_values")->l) vb.push_back(v.s);
            out("      as " + as + cfmt(" from 0x%llx: a=[", row.d->get("values_at")->i) + join(va, ", ") + "] b=[" + join(vb, ", ") + "]");
        }
    }
    return 1;
}

// ---------------------------------------------------------------- strings
inline bool printable(unsigned c) { return c >= 0x20 && c <= 0x7E; }
// pat.finditer(data) for the three patterns: (start, length)
inline std::vector<std::pair<std::size_t, std::size_t>> find_runs(const std::string& data, const std::string& enc, long long n) {
    std::vector<std::pair<std::size_t, std::size_t>> m;
    const std::size_t L = data.size();
    std::size_t pos = 0;
    if (enc == "sjis") {
        while (pos < L) {
            std::size_t k = pos;
            while (k < L) {
                const unsigned c = bt(data, k);
                if (printable(c) || (c >= 0xA1 && c <= 0xDF)) { ++k; continue; }
                if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xEF)) && k + 1 < L) {
                    const unsigned t = bt(data, k + 1);
                    if ((t >= 0x40 && t <= 0x7E) || (t >= 0x80 && t <= 0xFC)) { k += 2; continue; }
                }
                break;
            }
            if (k > pos) { m.emplace_back(pos, k - pos); pos = k; } else ++pos;
        }
        return m;
    }
    const std::size_t unit = enc == "ascii" ? 1 : 2;
    while (pos <= L) {
        std::size_t k = 0;   // whole units from pos
        if (unit == 1) { while (pos + k < L && printable(bt(data, pos + k))) ++k; }
        else { while (pos + 2 * k + 1 < L && printable(bt(data, pos + 2 * k)) && data[pos + 2 * k + 1] == '\0') ++k; }
        if (static_cast<long long>(k) >= n) {
            if (k > 0) { m.emplace_back(pos, k * unit); pos += k * unit; }
            else { m.emplace_back(pos, 0); ++pos; }   // {0,}: an empty match
        } else pos += std::max<std::size_t>(k * unit, 1);
    }
    return m;
}
inline std::size_t trailing_run(const std::string& data, const std::string& enc, std::size_t cap = 1u << 20) {
    std::size_t i = data.size();
    const std::size_t lo = data.size() > cap ? data.size() - cap : 0;
    const auto allowed = [&](unsigned c) {
        if (printable(c)) return true;
        if (enc == "utf16le") return c == 0;
        if (enc == "sjis") return c >= 0x81 && c <= 0xFC;
        return false;
    };
    while (i > lo && allowed(bt(data, i - 1))) --i;
    return i;
}
inline std::string u8enc(uint32_t c) {
    std::string o;
    if (c < 0x80) o += static_cast<char>(c);
    else if (c < 0x800) { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    else { o += static_cast<char>(0xE0 | (c >> 12)); o += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    return o;
}
inline std::optional<std::string> decode_run(const std::string& kind, const std::string& raw, long long n) {
    if (kind == "ascii") return raw;
    if (kind == "utf16le") { std::string o; for (std::size_t i = 0; i < raw.size(); i += 2) o += raw[i]; return o; }
    // Shift-JIS (heuristic): a clean strict cp932 decode, mostly double-byte, only kana / kanji / full-width,
    // at least half kana or full-width (random byte pairs decode mostly to kanji)
    std::vector<uint32_t> text;
    for (std::size_t i = 0; i < raw.size();) {
        const unsigned c = bt(raw, i);
        if (c < 0x80) { text.push_back(c); ++i; continue; }
        if (c >= 0xA1 && c <= 0xDF) { text.push_back(0xFEC0u + c); ++i; continue; }
        if (i + 1 >= raw.size()) return std::nullopt;
        const unsigned c2 = bt(raw, i + 1);
        const int li = cp932::lead_index(c);
        if (li < 0 || c2 < 0x40 || c2 > 0xFC || !cp932::DBCS[li][c2 - 0x40]) return std::nullopt;
        text.push_back(cp932::DBCS[li][c2 - 0x40]); i += 2;
    }
    std::vector<uint32_t> wide;
    for (const uint32_t c : text) if (c > 0x7F) wide.push_back(c);
    if (wide.size() < 2 || static_cast<long long>(text.size()) < n || wide.size() * 2 < text.size()) return std::nullopt;
    std::size_t kana = 0;
    for (const uint32_t o : wide) {
        if (!((o >= 0x3000 && o <= 0x30FF) || (o >= 0x4E00 && o <= 0x9FFF) || (o >= 0xFF01 && o <= 0xFF5E))) return std::nullopt;
        if ((o >= 0x3000 && o <= 0x30FF) || (o >= 0xFF01 && o <= 0xFF5E)) ++kana;
    }
    if (kana * 2 < wide.size()) return std::nullopt;
    std::string o; for (const uint32_t c : text) o += u8enc(c);
    return o;
}
struct StrRow { long long offset; std::string enc, text; };
inline std::vector<StrRow> iter_strings(const std::string& path, long long n, const std::string& enc) {
    std::vector<std::string> pats;
    if (enc == "ascii" || enc == "all") pats.push_back("ascii");
    if (enc == "utf16le" || enc == "all") pats.push_back("utf16le");
    if (enc == "sjis" || enc == "all") pats.push_back("sjis");
    std::vector<StrRow> rows;
    BinFile fh(path);
    for (const auto& name : pats) {
        fh.seek(0);
        long long base = 0; std::string carry;
        while (true) {
            const std::string block = fh.read(chunk());
            const std::string data = carry + block;
            if (data.empty()) break;
            const std::size_t cut = block.empty() ? data.size() : trailing_run(data, name);
            for (const auto& [st, len] : find_runs(data, name, n)) {
                if (!block.empty() && st >= cut) break;
                const auto text = decode_run(name, data.substr(st, len), n);
                if (text) rows.push_back({base + static_cast<long long>(st), name, *text});
            }
            if (block.empty()) break;
            carry = data.substr(cut);
            base += static_cast<long long>(cut);
        }
    }
    return rows;
}
inline int cmd_strings(const Val& a) {
    const std::string file = S(a, "file");
    need_file(file);
    const long long n = get(a, "min").i;
    if (n < 0) throw ToolError("--min must be 0 or more");
    const std::string enc = S(a, "encoding"), grep0 = S(a, "grep");
    const std::string grep = grep0.empty() ? "" : lower(grep0);
    std::vector<StrRow> rows;
    for (auto& r : iter_strings(file, n, enc)) {
        if (!grep.empty() && lower(r.text).find(grep) == std::string::npos) continue;
        rows.push_back(std::move(r));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const StrRow& x, const StrRow& y) { return x.offset < y.offset; });
    const long long mx = get(a, "max").i;
    std::size_t shown = rows.size();
    if (mx > 0) shown = std::min<std::size_t>(rows.size(), static_cast<std::size_t>(mx));
    else if (mx < 0) shown = static_cast<long long>(rows.size()) + mx > 0 ? static_cast<std::size_t>(static_cast<long long>(rows.size()) + mx) : 0;
    if (truthy(get(a, "json"))) {
        List l;
        for (std::size_t i = 0; i < shown; ++i) l.push_back(dict_of({{"offset", Val::integer(rows[i].offset)}, {"encoding", Val::str(rows[i].enc)}, {"text", Val::str(rows[i].text)}}));
        DumpOpts o; o.indent = 2; o.ensure_ascii = false;
        out(dumps(Val::list(l), o));
    } else {
        for (std::size_t i = 0; i < shown; ++i) {
            const std::string tag = enc == "ascii" ? "" : cfmt("%-7s ", rows[i].enc.c_str());
            out("0x" + hex08(rows[i].offset) + "  " + tag + rows[i].text);
        }
        if (rows.size() > shown) out(cfmt("... %zu more (raise --max)", rows.size() - shown));
    }
    return 0;
}

// ---------------------------------------------------------------- files (runtime log)
inline int cmd_files(const Val& a) {
    const std::string log = S(a, "log");
    need_file(log);
    static const std::string ok_pat = "\\[sceCdSearchFile:ok\\] path=\"(.*?)\" lsn=0x([0-9a-fA-F]+) size=0x([0-9a-fA-F]+)";
    static const std::string fail_pat = "sceCdSearchFile failed: (.*?) \\((?:normalized path is empty, )?root: ";
    const auto& LOG_OK = re::compile(ok_pat);
    const auto& LOG_FAIL = re::compile(fail_pat);
    std::vector<std::string> order; std::map<std::string, Val> seen;
    const auto lines = lines_keep(text_u(read_bytes(log)));
    for (std::size_t ln = 0; ln < lines.size(); ++ln) {
        const std::string& line = lines[ln];
        std::string path, st; Val lsn, size;
        re::Match m = LOG_OK.search(line);
        if (m) {
            path = m.group(1);
            lsn = Val::integer(*int16(m.group(2))); size = Val::integer(*int16(m.group(3)));
            st = "found";
        } else {
            m = LOG_FAIL.search(line);
            if (!m) continue;
            path = m.group(1); st = "missing";
        }
        const std::string key = upper(path);
        if (!seen.count(key)) {
            seen[key] = dict_of({{"path", Val::str(path)}, {"first_line", Val::integer(static_cast<long long>(ln + 1))}, {"lookups", Val::integer(0)},
                                 {"status", Val::str(st)}, {"size", size}, {"lsn", lsn}});
            order.push_back(key);
        }
        Val& r = seen[key];
        r.d->get("lookups")->i += 1;
        if (st == "found") { r.d->set("status", Val::str("found")); r.d->set("size", size); r.d->set("lsn", lsn); }
    }
    List rows; std::size_t missing = 0;
    for (const auto& k : order) { rows.push_back(seen[k]); if (seen[k].d->get("status")->s == "missing") ++missing; }
    if (truthy(get(a, "json"))) out(dumps2(Val::list(rows)));
    else {
        if (rows.empty()) out("no sceCdSearchFile lines in " + log + " (runtime logs need PS2X_ENABLE_RUNTIME_LOGS, on by default)");
        for (const Val& r : rows) {
            const Val* sz = r.d->get("size");
            out(cfmt("line %-7lld %-8s %-10s x%-4lld ", r.d->get("first_line")->i, r.d->get("status")->s.c_str(), sz->is_none() ? "-" : human(sz->i).c_str(),
                     r.d->get("lookups")->i) + r.d->get("path")->s);
        }
        if (!rows.empty()) out(cfmt("%zu file(s) looked up, %zu missing. The runtime traces the first 128 lookups, then every 512th.", rows.size(), missing));
    }
    return missing ? 1 : 0;
}

// ---------------------------------------------------------------- CLI
inline Val int_arg(const std::string& text) {   // int(text, 0)
    const auto v = console_abdiff::int0(text);
    if (!v) throw PyErr("ArgumentTypeError", "not a number: " + str_repr(text));
    return Val::integer(*v);
}
inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Map a PS2 game's data files: inventory, archive unpack/repack, byte diffs, strings.");
    p.epilog = "Only for discs you own. Unpacked game files stay on your machine; never publish them.";
    p.subcommands("cmd", true); p.sub_metavar = "command";
    { Parser& s = p.sub("scan"); s.help_line = "inventory a folder or file: size, type, entropy, table-of-contents guess";
      s.pos("target");
      s.opt({"--embedded"}).store_true().help("also count known signatures inside each file");
      s.opt({"--sort"}).choices({"path", "size"}).def("path");
      s.opt({"--min-size"}).type(int_arg, "_int").def(Val::integer(0));
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("unpack"); s.help_line = "split an AFS or detected table-of-contents archive into a folder + index.json";
      s.pos("file");
      s.opt({"--out"}).required().help("folder to write (must be empty)");
      s.opt({"--format"}).choices({"auto", "afs", "toc"}).def("auto");
      s.opt({"--layout"}).choices({"count+pairs", "count+offsets", "pairs", "offsets"}).help("only consider this table layout");
      s.opt({"--header"}).type(int_arg, "_int").def(Val()).help("table starts after this many header bytes (0, 4, 8, 12, 16)");
      s.opt({"--force-toc"}).store_true().help("use the best guess even below the confidence threshold");
      s.opt({"--force"}).store_true().help("write into a non-empty folder");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("repack"); s.help_line = "rebuild an archive from an unpacked folder (same order, original alignment)";
      s.pos("folder");
      s.opt({"--out"}).required().help("new archive path (never your only copy of the original)");
      s.opt({"--layout"}).choices({"auto", "keep", "pack"}).def("auto")
          .help("keep: every entry stays at its offset (refuse if one outgrew its slot); pack: recompute offsets; auto (default): keep when everything fits, else pack");
      s.opt({"--force"}).store_true().help("overwrite --out if it exists");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("diff"); s.help_line = "changed byte ranges between two files (or two folders)";
      s.pos("a"); s.pos("b");
      s.opt({"--gap"}).type(int_arg, "_int").def(Val::integer(8)).help("merge ranges closer than this many bytes (default 8)");
      s.opt({"--max"}).type(int_arg, "_int").def(Val::integer(200)).help("ranges to list (default 200)");
      s.opt({"--as"}).dest("as_type").choices({"u8", "u16", "u32", "f32"}).help("also show the values");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("strings"); s.help_line = "printable strings with offsets";
      s.pos("file");
      s.opt({"-n", "--min"}).type(int_arg, "_int").def(Val::integer(4)).help("minimum length (default 4)");
      s.opt({"--encoding"}).choices({"ascii", "sjis", "utf16le", "all"}).def("ascii");
      s.opt({"--grep"}).help("only strings containing this text (case-insensitive)");
      s.opt({"--max"}).type(int_arg, "_int").def(Val::integer(0)).help("stop after this many");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("files"); s.help_line = "disc files the port looked up, from a PS2Recomp runtime log";
      s.pos("log"); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    try {
        if (cmd == "scan") return cmd_scan(a);
        if (cmd == "unpack") return cmd_unpack(a);
        if (cmd == "repack") return cmd_repack(a);
        if (cmd == "diff") return cmd_diff(a);
        if (cmd == "strings") return cmd_strings(a);
        return cmd_files(a);
    } catch (const ToolError& e) {
        std::fflush(stdout); std::fprintf(stderr, "error: %s\n", e.what()); return 2;
    } catch (const Refused& e) {
        std::fflush(stdout); std::fprintf(stderr, "refused: %s\n", e.what()); return 1;
    }
}

PACKTOOL("console-levels/datascan", main);
}  // namespace packtools::datascan
