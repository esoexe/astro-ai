// console_memcard.inc - console-saves/memcard: a C++ port of console-skill/console-saves/memcard.py (Jake Schincariol, MIT).
// PS2 memory card saves: read card images (with or without ECC), PCSX2 folder cards and the port's mc0 folder; copy
// saves into mc0; .psu single-save files; timestamped zip backups; two-way cloud-folder sync that never silently
// overwrites. Card images are only read. Exit codes: 0 ok, 1 needs attention ("refused: ..."), 2 bad input ("error: ...").
// icon.sys titles are Shift-JIS: decoded like CPython's cp932 codec (errors="replace") and NFKC-normalized.
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "cp932_table.h"

namespace packtools::console_memcard {
using namespace pyrt;
using namespace packtools::console_common;

const char* const PROG = "memcard";
const std::string MAGIC("Sony PS2 Memory Card Format ");
constexpr std::size_t DIRENT_SIZE = 512, PSU_CLUSTER = 1024;
constexpr long long JST = 9 * 3600;   // the PS2 clock and card timestamps are UTC+9
constexpr unsigned DF_FILE = 0x0010, DF_DIR = 0x0020, DF_EXISTS = 0x8000;
constexpr unsigned MODE_DIR = 0x8427, MODE_FILE = 0x8497;
constexpr uint32_t FAT_ALLOCATED = 0x80000000u, FAT_NEXT_MASK = 0x7FFFFFFFu;   // low 31 bits all set = end of chain

struct Attention : std::runtime_error { using std::runtime_error::runtime_error; };   // a check failed: exit 1
[[noreturn]] inline void fail(const std::string& m) { throw InputError(m); }

inline unsigned bt(const std::string& d, std::size_t i) { return static_cast<unsigned char>(d[i]); }
inline uint32_t le32(const std::string& d, std::size_t o) { return bt(d, o) | bt(d, o + 1) << 8 | bt(d, o + 2) << 16 | static_cast<uint32_t>(bt(d, o + 3)) << 24; }
inline unsigned le16(const std::string& d, std::size_t o) { return bt(d, o) | bt(d, o + 1) << 8; }
inline void put16(std::string& o, unsigned v) { o += static_cast<char>(v & 0xFF); o += static_cast<char>((v >> 8) & 0xFF); }
inline void put32(std::string& o, uint32_t v) { for (int k = 0; k < 4; ++k) o += static_cast<char>((v >> (8 * k)) & 0xFF); }
inline std::string plural(long long n) { return n == 1 ? "" : "s"; }
inline long long floor_div(long long a, long long b) { long long q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }

// repr() of a bytes object
inline std::string brepr(const std::string& b) {
    const bool sq = b.find('\'') != std::string::npos, dq = b.find('"') != std::string::npos;
    const char q = (sq && !dq) ? '"' : '\'';
    std::string o = "b"; o += q;
    for (unsigned char c : b) {
        if (c == static_cast<unsigned char>(q) || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c < 0x20 || c >= 0x7F) o += cfmt("\\x%02x", c);
        else o += static_cast<char>(c);
    }
    o += q; return o;
}
// "%-Ns" % s: Python pads by code points
inline std::string ljust(const std::string& s, std::size_t w) { const std::size_t n = cplen(s); return n >= w ? s : s + std::string(w - n, ' '); }

// ---------------------------------------------------------------- cp932 (CPython's _codecs_jp decoder, errors="replace")
inline std::string cp932_decode(const std::string& b) {
    std::string o; const std::size_t n = b.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned c = bt(b, i);
        if (c <= 0x80) { o += utf8(c); ++i; continue; }
        if (c >= 0xA0 && c <= 0xDF) { o += utf8(c == 0xA0 ? 0xF8F0u : 0xFEC0u + c); ++i; continue; }   // half-width katakana
        if (c >= 0xFD) { o += utf8(0xF8F1u - 0xFD + c); ++i; continue; }                                   // Windows compatibility
        if (i + 1 >= n) { o += utf8(0xFFFD); ++i; continue; }                                              // incomplete multibyte sequence
        const unsigned c2 = bt(b, i + 1);
        const int li = cp932::lead_index(c);
        const bool trail_ok = (c2 >= 0x40 && c2 <= 0x7E) || (c2 >= 0x80 && c2 <= 0xFC);
        if (li >= 0 && c2 >= 0x40 && c2 <= 0xFC && cp932::DBCS[li][c2 - 0x40]) { o += utf8(cp932::DBCS[li][c2 - 0x40]); i += 2; continue; }
        if (c >= 0xF0 && c <= 0xF9 && trail_ok) { o += utf8(0xE000u + 188 * (c - 0xF0) + (c2 < 0x80 ? c2 - 0x40 : c2 - 0x41)); i += 2; continue; }   // user-defined area
        o += utf8(0xFFFD); ++i;   // an invalid sequence replaces the lead byte only
    }
    return o;
}

// ---------------------------------------------------------------- time
struct Tod { long long year = 0, month = 0, day = 0, hour = 0, minute = 0, sec = 0; };
inline Tod tod_unpack(const std::string& r, std::size_t o) {   // "<xBBBBBH"
    Tod t; t.sec = bt(r, o + 1); t.minute = bt(r, o + 2); t.hour = bt(r, o + 3); t.day = bt(r, o + 4); t.month = bt(r, o + 5); t.year = le16(r, o + 6);
    return t;
}
inline std::string tod_pack(const Tod& t) {
    for (long long v : {t.sec, t.minute, t.hour, t.day, t.month}) if (v < 0 || v > 255) throw PyErr("struct.error", "ubyte format requires 0 <= number <= 255");
    if (t.year < 0 || t.year > 65535) throw PyErr("struct.error", "'H' format requires 0 <= number <= 65535");
    std::string o(1, '\0');
    for (long long v : {t.sec, t.minute, t.hour, t.day, t.month}) o += static_cast<char>(v);
    put16(o, static_cast<unsigned>(t.year)); return o;
}
inline long long days_from_civil(long long y, long long m, long long d) {
    y -= m <= 2; const long long era = floor_div(y, 400); const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
inline void civil_from_days(long long z, long long& y, long long& m, long long& d) {
    z += 719468; const long long era = floor_div(z, 146097); const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; y = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100); const long long mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1; m = mp < 10 ? mp + 3 : mp - 9; y += m <= 2;
}
struct Epoch { bool ok = false; long long v = 0; };
inline Epoch tod_to_epoch(const Tod& t) {   // calendar.timegm(...) - JST; None when datetime.date() would refuse
    const long long mo = t.month ? t.month : 1, d = t.day ? t.day : 1;
    if (t.year < 1 || t.year > 9999 || mo < 1 || mo > 12) return {};
    const long long days = days_from_civil(t.year, mo, 1) + d - 1;
    return {true, ((days * 24 + t.hour) * 60 + t.minute) * 60 + t.sec - JST};
}
inline Tod gm_tod(long long w) {
    Tod t; const long long days = floor_div(w, 86400), rem = w - days * 86400;
    civil_from_days(days, t.year, t.month, t.day); t.hour = rem / 3600; t.minute = rem / 60 % 60; t.sec = rem % 60; return t;
}
inline Tod epoch_to_tod(double when) { long long w = static_cast<long long>(when); if (w < 0) w = 0; return gm_tod(w + JST); }
inline std::string tod_text(const Tod& t) {
    if (!t.year) return "-";
    return cfmt("%04lld-%02lld-%02lld %02lld:%02lld", t.year, t.month, t.day, t.hour, t.minute);
}
inline std::string utc_stamp() {   // time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    const Tod t = gm_tod(static_cast<long long>(std::time(nullptr)));
    return cfmt("%04lld%02lld%02lldT%02lld%02lld%02lldZ", t.year, t.month, t.day, t.hour, t.minute, t.sec);
}

// ---------------------------------------------------------------- host files: stat, mtime, walk
struct FStat { bool ok = false, dir = false; uint64_t size = 0; double mtime = 0; uint32_t mode = 0; };
inline FStat stat_of(const std::string& p) {   // os.stat (follows symlinks)
    FStat r; std::error_code ec; const auto st = std::filesystem::status(fspath(p), ec);
    if (ec || !std::filesystem::exists(st)) return r;
    r.ok = true; r.dir = std::filesystem::is_directory(st);
    if (std::filesystem::is_regular_file(st)) { const auto s = std::filesystem::file_size(fspath(p), ec); if (!ec) r.size = s; }
    const auto ft = std::filesystem::last_write_time(fspath(p), ec);
    if (!ec) {
        const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::file_clock::to_sys(ft).time_since_epoch()).count();
        const long long sec = floor_div(ns, 1000000000LL), nsec = ns - sec * 1000000000LL;
        r.mtime = static_cast<double>(sec) + static_cast<double>(nsec) * 1e-9;
    }
#if !defined(_WIN32)
    struct stat s{}; if (::stat(p.c_str(), &s) == 0) r.mode = static_cast<uint32_t>(s.st_mode);
#else
    r.mode = (r.dir ? 0040000u : 0100000u) | ((st.permissions() & std::filesystem::perms::owner_write) != std::filesystem::perms::none ? 0666u : 0444u) | (r.dir ? 0111u : 0u);
#endif
    return r;
}
inline void set_mtime(const std::string& p, long long t) {   // os.utime(p, (t, t)), errors ignored
    std::error_code ec;
    const auto ft = std::chrono::file_clock::from_sys(std::chrono::sys_time<std::chrono::nanoseconds>(std::chrono::nanoseconds(t * 1000000000LL)));
    std::filesystem::last_write_time(fspath(p), std::chrono::time_point_cast<std::filesystem::file_time_type::duration>(ft), ec);
}
inline bool islink(const std::string& p) { std::error_code ec; return std::filesystem::is_symlink(std::filesystem::symlink_status(fspath(p), ec)); }
inline void rmtree(const std::string& p) { std::error_code ec; std::filesystem::remove_all(fspath(p), ec); }
inline void remove_file(const std::string& p) { std::error_code ec; if (!std::filesystem::remove(fspath(p), ec) || ec) throw OSError(ec ? ec.value() : ENOENT, p); }
inline void write_bytes(const std::string& p, const std::string& data) { write_text(p, data); }
inline std::vector<std::string> sorted_lower(std::vector<std::string> v) {   // sorted(names, key=str.lower)
    std::vector<std::pair<std::string, std::string>> k; for (auto& s : v) k.emplace_back(lower(s), s);
    std::stable_sort(k.begin(), k.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> o; for (auto& x : k) o.push_back(x.second); return o;
}
inline std::vector<std::string> listdir_quiet(const std::string& d) { try { return listdir(d); } catch (const PyErr&) { return {}; } }
// os.walk(root) with sorted, non-symlink folders and files: (full path, path relative to root with "/")
inline void host_walk(const std::string& base, const std::string& rel, std::vector<std::pair<std::string, std::string>>& out) {
    std::vector<std::string> dirs, files;
    for (const auto& n : listdir_quiet(base)) { if (is_dir(path_join(base, n))) dirs.push_back(n); else files.push_back(n); }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) { const std::string p = path_join(base, f); if (!islink(p)) out.emplace_back(p, rel.empty() ? f : rel + "/" + f); }
    std::vector<std::string> keep; for (const auto& d : dirs) if (!islink(path_join(base, d))) keep.push_back(d);
    std::sort(keep.begin(), keep.end());
    for (const auto& d : keep) host_walk(path_join(base, d), rel.empty() ? d : rel + "/" + d, out);
}
inline std::vector<std::pair<std::string, std::string>> host_files(const std::string& root) { std::vector<std::pair<std::string, std::string>> o; if (is_dir(root)) host_walk(root, "", o); return o; }

// ---------------------------------------------------------------- names
inline std::string host_name(const std::string& raw) {   // a card name as a safe host file name
    if (raw.empty() || raw == "." || raw == "..") fail("empty or dot name");
    for (unsigned char c : raw) if (c < 0x20 || std::strchr("/\\:*?\"<>|", c)) fail("name " + brepr(raw) + " has characters a host folder cannot hold");
    if (raw.size() > 31) fail("name " + brepr(raw) + " is longer than 31 bytes");
    return raw;
}
struct Serial { bool ok = false; std::string serial, region; };
inline Serial serial_of(const std::string& name) {
    static const std::string PAT = "^B([AEI])(S[CL][A-Z]{2})[-_]?(\\d{3})\\.?(\\d{2})";
    const auto m = re::match(PAT, name);
    if (!m) return {};
    const std::string r = m.group(1);
    return {true, m.group(2) + "-" + m.group(3) + m.group(4), r == "A" ? "USA" : r == "E" ? "Europe" : r == "I" ? "Japan/Asia" : "?"};
}
inline Val icon_title(const std::string& blob) {   // the two-line save title of an icon.sys, or None
    if (blob.size() < 0x104 || blob.compare(0, 4, "PS2D") != 0) return Val();
    std::size_t brk = le16(blob, 6);
    std::string raw = blob.substr(0xC0, 0x104 - 0xC0); raw = raw.substr(0, raw.find('\0'));
    brk = std::min(brk, raw.size());
    const auto conv = [](const std::string& b) { return strip(uni::normalize("NFKC", cp932_decode(b))); };
    std::vector<std::string> parts; for (const auto& p : {conv(raw.substr(0, brk)), conv(raw.substr(brk))}) if (!p.empty()) parts.push_back(p);
    if (parts.empty()) return Val();
    return Val::str(join(parts, " / "));
}

// ---------------------------------------------------------------- entries
struct Entry {
    std::string name, raw; unsigned mode = 0; uint64_t length = 0; Tod created, modified; uint32_t cluster = 0, attr = 0;
    std::string host;   // folder sources: the host path read_file opens
    std::vector<Entry> children;
    bool is_dir() const { return (mode & DF_DIR) != 0; }
};
struct Dirent { unsigned mode = 0; uint32_t length = 0, cluster = 0, parent = 0, attr = 0; Tod created, modified; std::string raw; };
inline Dirent unpack_dirent(const std::string& b, std::size_t o) {   // "<HHL8sLL8sL28x448s"
    Dirent d; d.mode = le16(b, o); d.length = le32(b, o + 4); d.created = tod_unpack(b, o + 8); d.cluster = le32(b, o + 16);
    d.parent = le32(b, o + 20); d.modified = tod_unpack(b, o + 24); d.attr = le32(b, o + 32);
    const std::string nm = b.substr(o + 64, 32); d.raw = nm.substr(0, nm.find('\0'));
    return d;
}
inline std::string pack_dirent(unsigned mode, uint64_t length, const std::string& created, const std::string& modified, const std::string& raw) {
    if (length > 0xFFFFFFFFull) throw PyErr("struct.error", "'L' format requires 0 <= number <= 4294967295");
    std::string o; put16(o, mode); put16(o, 0); put32(o, static_cast<uint32_t>(length)); o += created; put32(o, 0); put32(o, 0); o += modified; put32(o, 0);
    o += std::string(28, '\0'); std::string nm = raw.substr(0, 32); nm.resize(448, '\0'); o += nm;
    return o;
}

struct Source {
    virtual ~Source() = default;
    virtual std::vector<Entry> tree() = 0;
    virtual std::string read_file(const Entry& e) = 0;
};

// ---------------------------------------------------------------- card image (read only, with or without ECC)
struct Card : Source {
    std::string path; uint64_t size = 0; std::ifstream fh; std::string version;
    unsigned page_len = 0, ppc = 0, ppb = 0, card_type = 0, card_flags = 0;
    uint32_t clusters = 0, alloc_offset = 0, alloc_end = 0, root_cluster = 0, backup1 = 0, backup2 = 0, ifc[32] = {};
    uint64_t cluster_size = 0, spare = 0, raw_page = 0; bool ecc = false;
    std::map<uint32_t, std::string> cache;

    explicit Card(const std::string& p) : path(p) {
        std::error_code ec; const auto st = std::filesystem::status(fspath(p), ec);
        if (ec || !std::filesystem::exists(st)) fail("cannot read " + p + ": " + path_error(p));
        if (std::filesystem::is_directory(st)) fail("cannot read " + p + ": " + strerror_of(EISDIR));
        size = std::filesystem::file_size(fspath(p), ec); if (ec) size = 0;
        errno = 0; fh.open(fspath(p), std::ios::binary);
        if (!fh) fail("cannot read " + p + ": " + strerror_of(errno ? errno : EACCES));
        std::string sb(0x154, '\0'); fh.read(sb.data(), 0x154); sb.resize(static_cast<std::size_t>(fh.gcount())); fh.clear();
        if (sb.size() < 0x154 || sb.compare(0, 28, MAGIC) != 0)
            fail(p + " is not a PS2 memory card image (no 'Sony PS2 Memory Card Format' superblock)");
        std::string v = sb.substr(0x1C, 12); v = v.substr(0, v.find('\0'));
        for (unsigned char c : v) version += c < 0x80 ? std::string(1, static_cast<char>(c)) : utf8(0xFFFD);
        page_len = le16(sb, 0x28); ppc = le16(sb, 0x2A); ppb = le16(sb, 0x2C);
        clusters = le32(sb, 0x30); alloc_offset = le32(sb, 0x34); alloc_end = le32(sb, 0x38); root_cluster = le32(sb, 0x3C);
        backup1 = le32(sb, 0x40); backup2 = le32(sb, 0x44);
        for (int k = 0; k < 32; ++k) ifc[k] = le32(sb, 0x50 + 4 * static_cast<std::size_t>(k));
        card_type = bt(sb, 0x150); card_flags = bt(sb, 0x151);
        if (page_len < 128 || page_len % 128 || page_len > 8192 || ppc < 1 || ppc > 16)
            fail(cfmt("%s: implausible page size %u / pages per cluster %u", p.c_str(), page_len, ppc));
        if (clusters < 16 || clusters > (1u << 22)) fail(cfmt("%s: implausible cluster count %u", p.c_str(), clusters));
        if (static_cast<uint64_t>(alloc_offset) + alloc_end > clusters || root_cluster >= std::max<uint32_t>(alloc_end, 1))
            fail(cfmt("%s: allocation area (%u + %u clusters) does not fit the card", p.c_str(), alloc_offset, alloc_end));
        cluster_size = static_cast<uint64_t>(page_len) * ppc;
        const uint64_t pages = static_cast<uint64_t>(clusters) * ppc;
        spare = (page_len + 127) / 128 * 4;
        const uint64_t plain = pages * page_len, withecc = pages * (page_len + spare);
        if (size == withecc) ecc = true;
        else if (size == plain) ecc = false;
        else fail(cfmt("%s is %llu bytes; a card with this superblock is %llu bytes (no ECC) or %llu (with ECC): truncated or not a raw card dump",
                       p.c_str(), static_cast<unsigned long long>(size), static_cast<unsigned long long>(plain), static_cast<unsigned long long>(withecc)));
        raw_page = page_len + (ecc ? spare : 0);
    }
    const std::string& read_cluster(uint32_t n) {
        if (n >= clusters) fail(cfmt("cluster %u is past the end of the card: the card is damaged", n));
        const auto it = cache.find(n);
        if (it != cache.end()) return it->second;
        std::string c;
        const auto rd = [&](uint64_t at, uint64_t len) {
            std::string part(static_cast<std::size_t>(len), '\0'); fh.clear(); fh.seekg(static_cast<std::streamoff>(at));
            fh.read(part.data(), static_cast<std::streamsize>(len)); part.resize(static_cast<std::size_t>(std::max<std::streamsize>(0, fh.gcount()))); fh.clear(); c += part;
        };
        if (!ecc) rd(n * cluster_size, cluster_size);
        else for (uint64_t pg = static_cast<uint64_t>(n) * ppc; pg < static_cast<uint64_t>(n) * ppc + ppc; ++pg) rd(pg * raw_page, page_len);
        if (c.size() != cluster_size) fail(cfmt("short read at cluster %u", n));
        static std::string scratch;
        if (cache.size() < 4096) return cache.emplace(n, std::move(c)).first->second;
        scratch = std::move(c); return scratch;
    }
    uint32_t fat(uint32_t n) {
        const uint64_t epc = cluster_size / 4, fat_idx = n / epc, ent = n % epc, ind_idx = fat_idx / epc, ind_ent = fat_idx % epc;
        if (ind_idx >= 32 || !ifc[ind_idx]) fail(cfmt("FAT entry %u is outside the indirect FAT list: the card is damaged", n));
        const uint32_t fat_cluster = le32(read_cluster(ifc[ind_idx]), static_cast<std::size_t>(ind_ent * 4));
        if (!fat_cluster || fat_cluster >= clusters) fail(cfmt("indirect FAT points at cluster %u: the card is damaged", fat_cluster));
        return le32(read_cluster(fat_cluster), static_cast<std::size_t>(ent * 4));
    }
    std::vector<uint32_t> chain(uint32_t start, uint64_t need) {   // the first `need` clusters of a chain
        std::vector<uint32_t> out; std::set<uint32_t> seen; uint32_t n = start;
        while (out.size() < need) {
            if (n >= alloc_end) fail(cfmt("a cluster chain points past the allocation area (cluster %u): the card is damaged", n));
            if (seen.count(n)) fail(cfmt("a cluster chain loops back on itself at cluster %u: the card is damaged", n));
            seen.insert(n); out.push_back(n);
            if (out.size() == need) break;
            const uint32_t v = fat(n);
            if (!(v & FAT_ALLOCATED)) fail(cfmt("a cluster chain runs into a free cluster after cluster %u: the card is damaged", n));
            const uint32_t nxt = v & FAT_NEXT_MASK;
            if (nxt == FAT_NEXT_MASK) fail(cfmt("a chain ends after %zu clusters but its entry needs %llu: the card is damaged", out.size(), static_cast<unsigned long long>(need)));
            n = nxt;
        }
        return out;
    }
    std::string read_chain(uint32_t start, uint64_t length) {
        if (length == 0) return "";
        const uint64_t need = (length + cluster_size - 1) / cluster_size;
        std::string data; for (uint32_t c : chain(start, need)) data += read_cluster(alloc_offset + c);
        data.resize(static_cast<std::size_t>(length)); return data;
    }
    std::vector<Dirent> dirents(uint32_t cluster, long long count) {
        if (count < 1 || count > 16384) fail(cfmt("a directory claims %lld entries: the card is damaged", count));
        const std::string blob = read_chain(cluster, static_cast<uint64_t>(count) * DIRENT_SIZE);
        std::vector<Dirent> v; for (long long i = 0; i < count; ++i) v.push_back(unpack_dirent(blob, static_cast<std::size_t>(i) * DIRENT_SIZE));
        return v;
    }
    std::vector<Dirent> root() {
        const Dirent first = dirents(root_cluster, 1)[0];
        if (!(first.mode & DF_DIR)) fail("the root directory's first entry is not a directory: not formatted, or damaged");
        if (first.length < 2) fail(cfmt("the root directory claims %u entries (at least . and .. are needed): the card is damaged", first.length));
        return dirents(root_cluster, first.length);
    }
    std::vector<Entry> children(const std::vector<Dirent>& ents, int depth) {
        std::vector<Entry> out;
        for (std::size_t i = 0; i < ents.size(); ++i) {
            const Dirent& d = ents[i];
            if (i < 2 || !(d.mode & DF_EXISTS) || d.raw == "." || d.raw == "..") continue;
            Entry e; e.name = d.raw; e.raw = d.raw; e.mode = d.mode; e.length = d.length; e.created = d.created; e.modified = d.modified; e.cluster = d.cluster; e.attr = d.attr;
            if (e.is_dir() && depth < 8) e.children = children(dirents(d.cluster, d.length), depth + 1);
            out.push_back(std::move(e));
        }
        return out;
    }
    std::vector<Entry> tree() override { return children(root(), 0); }
    std::string read_file(const Entry& e) override { return read_chain(e.cluster, e.length); }
    uint64_t free_clusters() { uint64_t f = 0; for (uint32_t n = 0; n < alloc_end; ++n) if (!(fat(n) & FAT_ALLOCATED)) ++f; return f; }
};

// ---------------------------------------------------------------- folder sources (mc0, PCSX2 folder cards)
inline bool skip_host(const std::string& n) {
    return starts(n, "_pcsx2_") || starts(n, ".console-") || n == ".DS_Store" || n == "Thumbs.db" || n == "desktop.ini" || starts(n, "._");
}
struct FolderCard : Source {
    std::string path;
    explicit FolderCard(std::string p) : path(std::move(p)) {}
    std::vector<Entry> tree() override {
        std::vector<Entry> out;
        for (const auto& n : sorted_lower(listdir(path))) {
            const std::string full = path_join(path, n);
            if (skip_host(n) || islink(full)) continue;
            out.push_back(entry(full, n, 0));
        }
        return out;
    }
    static Entry entry(const std::string& full, const std::string& name, int depth) {
        const FStat st = stat_of(full);
        if (!st.ok) throw OSError(ENOENT, full);
        const Tod t = epoch_to_tod(st.mtime);
        Entry e; e.name = name; e.raw = name; e.created = t; e.modified = t; e.host = full;
        if (st.dir) {
            e.mode = MODE_DIR;
            if (depth < 8) for (const auto& n : sorted_lower(listdir(full))) {
                const std::string p = path_join(full, n);
                if (!skip_host(n) && !islink(p)) e.children.push_back(entry(p, n, depth + 1));
            }
            e.length = e.children.size() + 2;
        } else { e.mode = MODE_FILE; e.length = st.size; }
        return e;
    }
    std::string read_file(const Entry& e) override { return read_bytes(e.host); }
};

inline std::unique_ptr<Source> open_source(const std::string& path) {
    if (is_dir(path)) return std::make_unique<FolderCard>(path);
    if (!exists(path)) fail(path + " does not exist");
    return std::make_unique<Card>(path);
}

inline Val save_summary(Source& src, const Entry& e) {
    long long nfiles = 0, total = 0, subs = 0; Val title;
    for (const auto& c : e.children) {
        if (c.is_dir()) { ++subs; continue; }
        ++nfiles; total += static_cast<long long>(c.length);
        if (lower(c.name) == "icon.sys") {
            try { title = icon_title(src.read_file(c)); } catch (const InputError&) { title = Val(); }
        }
    }
    const Serial ser = serial_of(e.name);
    List fl;
    for (const auto& c : e.children) fl.push_back(dict_of({{"name", Val::str(c.name)}, {"bytes", Val::integer(static_cast<long long>(c.length))}, {"mode", Val::str(cfmt("0x%04x", c.mode))}}));
    return dict_of({{"name", Val::str(e.name)}, {"dir", Val::boolean(e.is_dir())}, {"files", Val::integer(nfiles)},
                    {"bytes", Val::integer(e.is_dir() ? total : static_cast<long long>(e.length))}, {"modified", Val::str(tod_text(e.modified))},
                    {"title", title}, {"serial", ser.ok ? Val::str(ser.serial) : Val()}, {"region", ser.ok ? Val::str(ser.region) : Val()},
                    {"subfolders", Val::integer(subs)}, {"file_list", Val::list(std::move(fl))}});
}

// ---------------------------------------------------------------- zip (ZIP_DEFLATED writer; reader with entry times)
inline bool non_ascii(const std::string& s) { for (unsigned char c : s) if (c >= 0x80) return true; return false; }
// zipfile.ZipFile(path, "w", ZIP_DEFLATED, strict_timestamps=False) + write(host, arcname) for each file
inline void zip_write(const std::string& path, const std::vector<std::pair<std::string, std::string>>& files) {
    std::string body, cd; std::size_t count = 0;
    for (const auto& [host, arc] : files) {
        const std::string data = read_bytes(host);
        const FStat st = stat_of(host);
        std::time_t tt = static_cast<std::time_t>(std::floor(st.mtime)); std::tm lt{};
#if defined(_WIN32)
        localtime_s(&lt, &tt);
#else
        localtime_r(&tt, &lt);
#endif
        int y = lt.tm_year + 1900, mo = lt.tm_mon + 1, d = lt.tm_mday, h = lt.tm_hour, mi = lt.tm_min, s = lt.tm_sec;
        if (y < 1980) { y = 1980; mo = 1; d = 1; h = 0; mi = 0; s = 0; }
        else if (y > 2107) { y = 2107; mo = 12; d = 31; h = 23; mi = 59; s = 59; }
        const unsigned ddate = static_cast<unsigned>((y - 1980) << 9 | mo << 5 | d), dtime = static_cast<unsigned>(h << 11 | mi << 5 | s / 2);
        const std::string z = zip::zlib_compress(data), raw = z.substr(2, z.size() - 6);   // raw deflate
        const uint32_t crc = zip::crc32(data); const unsigned flags = non_ascii(arc) ? 0x800 : 0;
        if (raw.size() > 0xFFFFFFFFull || data.size() > 0xFFFFFFFFull || body.size() > 0xFFFFFFFFull) throw PyErr("zipfile.LargeZipFile", "Filesize would require ZIP64 extensions");
        const uint32_t local = static_cast<uint32_t>(body.size());
        std::string head = "PK\x03\x04"; put16(head, 20); put16(head, flags); put16(head, 8); put16(head, dtime); put16(head, ddate);
        put32(head, crc); put32(head, static_cast<uint32_t>(raw.size())); put32(head, static_cast<uint32_t>(data.size())); put16(head, static_cast<unsigned>(arc.size())); put16(head, 0);
        body += head; body += arc; body += raw;
#if defined(_WIN32)
        const unsigned made = 20;
#else
        const unsigned made = 3 << 8 | 20;
#endif
        cd += "PK\x01\x02"; put16(cd, made); put16(cd, 20); put16(cd, flags); put16(cd, 8); put16(cd, dtime); put16(cd, ddate);
        put32(cd, crc); put32(cd, static_cast<uint32_t>(raw.size())); put32(cd, static_cast<uint32_t>(data.size())); put16(cd, static_cast<unsigned>(arc.size()));
        put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, (st.mode & 0xFFFF) << 16); put32(cd, local); cd += arc;
        ++count;
    }
    std::string end = "PK\x05\x06"; put16(end, 0); put16(end, 0); put16(end, static_cast<unsigned>(count)); put16(end, static_cast<unsigned>(count));
    put32(end, static_cast<uint32_t>(cd.size())); put32(end, static_cast<uint32_t>(body.size())); put16(end, 0);
    write_bytes(path, body + cd + end);
}
struct ZEntry { zip::Member m; unsigned dtime = 0, ddate = 0; };
inline std::vector<ZEntry> zip_entries(const std::string& z) {   // ZipFile(...).infolist(), with each entry's DOS date and time
    const auto ms = zip::members(z);
    std::size_t e = 0;
    for (std::size_t k = z.size() - 22 + 1; k-- > 0 && z.size() - k <= 22 + 65535;) if (z.compare(k, 4, "PK\x05\x06") == 0) { e = k; break; }
    uint64_t cd = zip::le(z, e + 16, 4);
    if ((zip::le(z, e + 10, 2) == 0xFFFF || cd == 0xFFFFFFFFu) && e >= 20 && z.compare(e - 20, 4, "PK\x06\x07") == 0) {
        const uint64_t e64 = zip::le(z, e - 20 + 8, 8);
        if (e64 + 56 <= z.size() && z.compare(static_cast<std::size_t>(e64), 4, "PK\x06\x06") == 0) cd = zip::le(z, static_cast<std::size_t>(e64) + 48, 8);
    }
    std::vector<ZEntry> out; std::size_t at = static_cast<std::size_t>(cd);
    for (const auto& m : ms) {
        ZEntry x; x.m = m; x.dtime = static_cast<unsigned>(zip::le(z, at + 12, 2)); x.ddate = static_cast<unsigned>(zip::le(z, at + 14, 2));
        const std::size_t nul = x.m.name.find('\0'); if (nul != std::string::npos) x.m.name.resize(nul);   // ZipInfo cuts names at NUL
#if defined(_WIN32)
        x.m.name = replace_all(x.m.name, "\\", "/");
#endif
        at += 46 + static_cast<std::size_t>(zip::le(z, at + 28, 2) + zip::le(z, at + 30, 2) + zip::le(z, at + 32, 2));
        out.push_back(std::move(x));
    }
    return out;
}
inline std::string zip_read(const std::string& z, const zip::Member& m) {   // ZipFile.open(info).read()
    const std::size_t h = static_cast<std::size_t>(m.local);
    if (h + 30 > z.size() || z.compare(h, 4, "PK\x03\x04") != 0) throw zip::BadZipFile("Bad magic number for file header");
    if (m.flags & 1) throw PyErr("RuntimeError", "File " + str_repr(m.name) + " is encrypted, password required for extraction");
    const std::size_t data = h + 30 + static_cast<std::size_t>(zip::le(z, h + 26, 2)) + static_cast<std::size_t>(zip::le(z, h + 28, 2));
    if (data + m.csize > z.size()) throw zip::BadZipFile("Truncated file data");
    std::string out;
    if (m.method == 0) out = z.substr(data, static_cast<std::size_t>(m.csize));
    else if (m.method == 8) out = zip::inflate_raw(z, data, static_cast<std::size_t>(m.csize));
    else throw PyErr("NotImplementedError", "That compression method is not supported");
    if (zip::crc32(out) != m.crc) throw zip::BadZipFile("Bad CRC-32 for file " + str_repr(m.name));
    return out;
}

// ---------------------------------------------------------------- backups
inline std::string label_of(const std::string& mc0) {
    const std::string full = abspath(mc0);
    std::string parent = basename(dirname(full)); if (parent.empty()) parent = "card";
    static const std::string PAT = "[^A-Za-z0-9_.-]";
    return re::sub(PAT, "_", parent + "-" + basename(full));
}
inline std::vector<std::string> prune_backups(const std::string& folder, const std::string& label, long long keep, const std::string& protect) {
    const std::string pat = "^" + re::escape(label) + "-(\\d{8}T\\d{6}Z)(?:-(\\d+))?\\.zip$";
    struct Mine { std::string stamp; long long k; std::string f; };
    std::vector<Mine> mine;
    for (const auto& f : listdir(folder)) if (const auto m = re::match(pat, f)) mine.push_back({m.group(1), m.has(2) && !m.group(2).empty() ? dec_int(m.group(2)) : 1, f});
    std::sort(mine.begin(), mine.end(), [](const Mine& a, const Mine& b) { return std::tie(a.stamp, a.k, a.f) < std::tie(b.stamp, b.k, b.f); });
    std::vector<std::string> removed;
    if (static_cast<long long>(mine.size()) > keep)
        for (std::size_t i = 0; i < mine.size() - static_cast<std::size_t>(keep); ++i) {
            if (mine[i].f == protect) continue;
            remove_file(path_join(folder, mine[i].f)); removed.push_back(mine[i].f);
        }
    return removed;
}
inline std::string make_backup(const std::string& mc0, std::string to, long long keep, std::string label = "") {
    if (!is_dir(mc0)) fail(mc0 + " is not a folder");
    if (to.empty()) to = path_join(path_join("console", "saves"), "backups");
    const std::string full_to = abspath(to), full_mc = abspath(mc0);
    if (full_to == full_mc || starts(full_to, full_mc + ossep())) fail("put backups outside the card folder (the game would see them in " + mc0 + ")");
    makedirs(to);
    if (label.empty()) label = label_of(mc0);
    const std::string stamp = utc_stamp();
    const std::string pat = "^" + re::escape(label) + "-" + stamp + "(?:-(\\d+))?\\.zip$";
    long long k = 1; bool any = false, first = true; long long best = 0;
    for (const auto& f : listdir(to)) if (const auto m = re::match(pat, f)) {
        const long long v = m.has(1) && !m.group(1).empty() ? dec_int(m.group(1)) : 1;
        if (first || v > best) best = v;
        first = false; any = true;
    }
    if (any) k = best + 1;
    const std::string path = path_join(to, label + "-" + stamp + (k == 1 ? std::string() : cfmt("-%lld", k)) + ".zip");
    const std::string tmp = path + ".tmp";
    zip_write(tmp, host_files(mc0));
    replace_file(tmp, path);
    if (keep > 0) prune_backups(to, label, keep, basename(path));
    return path;
}

// ---------------------------------------------------------------- commands: info, ls
inline int cmd_info(const Val& a) {
    const std::string path = S(a, "card");
    Card card(path);
    const uint64_t fr = card.free_clusters();
    const Val res = dict_of({{"card", Val::str(path)}, {"bytes", Val::integer(static_cast<long long>(card.size))}, {"ecc", Val::boolean(card.ecc)},
        {"version", Val::str(card.version)}, {"page_len", Val::integer(card.page_len)}, {"pages_per_cluster", Val::integer(card.ppc)},
        {"pages_per_block", Val::integer(card.ppb)}, {"clusters_per_card", Val::integer(card.clusters)}, {"alloc_offset", Val::integer(card.alloc_offset)},
        {"alloc_end", Val::integer(card.alloc_end)}, {"rootdir_cluster", Val::integer(card.root_cluster)}, {"backup_block1", Val::integer(card.backup1)},
        {"backup_block2", Val::integer(card.backup2)}, {"card_type", Val::integer(card.card_type)}, {"card_flags", Val::str(cfmt("0x%02x", card.card_flags))},
        {"free_kb", Val::integer(static_cast<long long>(fr * card.cluster_size / 1024))},
        {"usable_kb", Val::integer(static_cast<long long>(card.alloc_end * card.cluster_size / 1024))}});
    if (B(a, "json")) { out(dumps2(res)); return 0; }
    out(cfmt("%s: PS2 memory card, format %s, %llu MB, %s", path.c_str(), card.version.c_str(),
             static_cast<unsigned long long>(card.clusters * card.cluster_size / (1u << 20)), card.ecc ? "with ECC (528-byte pages)" : "no ECC"));
    out(cfmt("  free %lld KB of %lld KB; %u-byte pages, %u per cluster, allocation from cluster %u", get(res, "free_kb").i, get(res, "usable_kb").i,
             card.page_len, card.ppc, card.alloc_offset));
    return 0;
}
inline int cmd_ls(const Val& a) {
    const std::string path = S(a, "card");
    List rows;
    { auto src = open_source(path); for (const auto& e : src->tree()) rows.push_back(save_summary(*src, e)); }
    if (B(a, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; out(dumps(Val::list(rows), o)); return 0; }
    if (rows.empty()) { out("no saves on " + path); return 0; }
    out(cfmt("%-22s %5s %9s  %-16s %-11s %s", "save folder", "files", "size", "modified", "serial", "title"));
    for (const auto& r : rows) {
        const long long by = get(r, "bytes").i;
        out(ljust(get(r, "name").s, 22) + cfmt(" %5lld %8lldK  ", get(r, "files").i, -floor_div(-by, 1024)) + ljust(get(r, "modified").s, 16) + " " +
            ljust(get(r, "serial").is_none() ? "-" : get(r, "serial").s, 11) + " " + (get(r, "title").is_none() ? "" : get(r, "title").s));
        if (B(a, "files"))
            for (const auto& f : *get(r, "file_list").l)
                out("    " + ljust(get(f, "name").s, 30) + cfmt(" %8lld bytes  mode ", get(f, "bytes").i) + get(f, "mode").s);
    }
    out(cfmt("%zu save(s). Card times are the PS2 clock (Japan time, UTC+9).", rows.size()));
    return 0;
}

// ---------------------------------------------------------------- export
inline void stamp_path(const std::string& p, const Tod& t) { const Epoch e = tod_to_epoch(t); if (e.ok && e.v > 0) set_mtime(p, e.v); }
inline long long write_tree(Source& src, const Entry& e, const std::string& dest, std::vector<std::string>& warnings) {
    makedirs(dest);
    long long count = 0;
    for (const auto& c : e.children) {
        std::string nm;
        try { nm = host_name(c.raw); } catch (const InputError& err) { warnings.push_back("skipped " + e.name + "/" + brepr(c.raw) + ": " + err.what()); continue; }
        const std::string target = path_join(dest, nm);
        if (c.is_dir()) count += write_tree(src, c, target, warnings);
        else { const std::string data = src.read_file(c); write_bytes(target, data); stamp_path(target, c.modified); ++count; }
    }
    stamp_path(dest, e.modified);
    return count;
}
inline std::vector<std::string> list_of(const Val& v) { std::vector<std::string> o; if (v.t == Val::LIST) for (const auto& x : *v.l) o.push_back(x.s); return o; }
inline int cmd_export(const Val& a) {
    const std::string card = S(a, "card"), to = S(a, "to");
    if (abspath(card) == abspath(to)) fail("the source and --to are the same folder");
    auto src = open_source(card);
    std::vector<std::string> warnings; std::vector<std::pair<std::string, long long>> done;
    std::vector<Entry> saves; for (auto& e : src->tree()) if (e.is_dir()) saves.push_back(std::move(e));
    const auto only = list_of(get(a, "only"));
    if (!only.empty()) {
        std::set<std::string> want(only.begin(), only.end()), have; for (const auto& e : saves) have.insert(e.name);
        std::vector<std::string> missing; for (const auto& w : want) if (!have.count(w)) missing.push_back(w);
        if (!missing.empty()) fail("not on the card: " + join(missing, ", "));
        std::vector<Entry> keep; for (auto& e : saves) if (want.count(e.name)) keep.push_back(std::move(e));
        saves.swap(keep);
    }
    makedirs(to);
    std::vector<std::string> clash; for (const auto& e : saves) if (exists(path_join(to, e.name))) clash.push_back(e.name);
    if (!clash.empty() && !B(a, "force"))
        throw Attention("already in " + to + ": " + join(clash, ", ") + ". Nothing written. Pass --force to replace them (mc0 is backed up first).");
    if (!clash.empty()) {
        const std::string z = make_backup(to, S(a, "backups"), get(a, "keep").i);
        out("backed up " + to + " to " + z + cfmt(" before replacing %zu save(s)", clash.size()));
    }
    for (const auto& e : saves) {
        std::string nm;
        try { nm = host_name(e.raw); } catch (const InputError& err) { warnings.push_back("skipped " + brepr(e.raw) + ": " + err.what()); continue; }
        const std::string dest = path_join(to, nm), tmp = path_join(to, ".console-tmp-" + nm);
        if (exists(tmp)) rmtree(tmp);
        long long n = 0;
        try { n = write_tree(*src, e, tmp, warnings); }
        catch (const InputError& err) { rmtree(tmp); warnings.push_back("skipped " + e.name + ", nothing written for it: " + err.what()); continue; }
        catch (...) { rmtree(tmp); throw; }
        if (is_dir(dest) && !islink(dest)) rmtree(dest);
        else if (lexists(dest)) remove_file(dest);
        replace_file(tmp, dest);
        done.emplace_back(e.name, n);
    }
    for (const auto& [name, n] : done) out("exported " + name + cfmt(" (%lld file%s) -> ", n, plural(n).c_str()) + path_join(to, name));
    for (const auto& w : warnings) out("WARNING: " + w);
    out(cfmt("%zu save(s) in ", done.size()) + to + ". The port reads them as mc0:/<folder>/... (slot 2 is the sibling mc1 folder).");
    return warnings.empty() ? 0 : 1;
}

// ---------------------------------------------------------------- psu
inline std::string psu_build(Source& src, const Entry& e) {   // a .psu for one save folder (files only)
    for (const auto& c : e.children) if (c.is_dir()) fail(e.name + " has a subfolder; the .psu format holds one folder of files");
    const std::string created = tod_pack(e.created), modified = tod_pack(e.modified);
    std::string o = pack_dirent(MODE_DIR, e.children.size() + 2, created, modified, e.raw);
    o += pack_dirent(MODE_DIR, 0, created, created, ".");
    o += pack_dirent(MODE_DIR, 0, created, created, "..");
    for (const auto& c : e.children) {
        if (c.raw.size() > 31) fail("file name " + brepr(c.raw) + " is longer than 31 bytes");
        const std::string data = src.read_file(c);
        const unsigned mode = (c.mode & DF_FILE) ? c.mode : MODE_FILE;
        o += pack_dirent(mode, data.size(), tod_pack(c.created), tod_pack(c.modified), c.raw);
        o += data; o += std::string((PSU_CLUSTER - data.size() % PSU_CLUSTER) % PSU_CLUSTER, '\0');
    }
    return o;
}
struct PsuFile { Dirent h; std::string data; };
inline Dirent psu_parse(const std::string& blob, const std::string& label, std::vector<PsuFile>& files) {
    if (blob.size() < 3 * DIRENT_SIZE) fail(label + " is too short to be a .psu");
    Dirent heads[3]; for (int i = 0; i < 3; ++i) heads[i] = unpack_dirent(blob, static_cast<std::size_t>(i) * DIRENT_SIZE);
    if (!(heads[0].mode & DF_DIR) || !(heads[1].mode & DF_DIR) || !(heads[2].mode & DF_DIR) || heads[0].length < 2)
        fail(label + " is not a .psu save (the first three headers must be folders)");
    const Dirent folder = heads[0];
    const long long count = static_cast<long long>(folder.length) - 2;
    if (count > 4096) fail(label + cfmt(" claims %lld files", count));
    std::size_t pos = 3 * DIRENT_SIZE;
    for (long long i = 0; i < count; ++i) {
        if (pos + DIRENT_SIZE > blob.size()) fail(label + cfmt(" is truncated: header %lld of %lld is missing", i + 1, count));
        PsuFile f; f.h = unpack_dirent(blob, pos); pos += DIRENT_SIZE;
        if ((f.h.mode & DF_DIR) || !(f.h.mode & DF_FILE)) fail(label + " holds a subfolder (" + brepr(f.h.raw) + "); not supported");
        const std::size_t n = f.h.length;
        if (pos + n > blob.size()) fail(label + " is truncated inside " + brepr(f.h.raw) + cfmt(" (%zu bytes expected, %zu present)", n, blob.size() - pos));
        f.data = blob.substr(pos, n); pos += n + (PSU_CLUSTER - n % PSU_CLUSTER) % PSU_CLUSTER;
        files.push_back(std::move(f));
    }
    return folder;
}
inline std::string read_or_fail(const std::string& p) {
    try { return read_bytes(p); } catch (const OSError& e) { fail("cannot read " + p + ": " + strerror_of(e.err)); }
}
inline std::string normpath_base(const std::string& p) {   // os.path.basename(os.path.normpath(p))
    std::string s = u8(fspath(p.empty() ? std::string(".") : p).lexically_normal());
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    if (s.empty()) s = ".";
    return basename(s);
}
inline int cmd_psu_export(const Val& a) {
    const std::string source = S(a, "source"), save = S(a, "save");
    std::unique_ptr<Source> src; Entry entry;
    if (is_dir(source)) {
        std::string parent = dirname(abspath(source)); if (parent.empty()) parent = ".";
        src = std::make_unique<FolderCard>(parent);
        entry = FolderCard::entry(abspath(source), normpath_base(source), 0);
    } else {
        src = std::make_unique<Card>(source);
        if (save.empty()) fail("exporting from a card image needs --save <folder name> (see memcard.py ls)");
        bool found = false;
        for (auto& e : src->tree()) if (e.is_dir() && e.name == save) { entry = std::move(e); found = true; break; }
        if (!found) fail(save + " is not on " + source);
    }
    host_name(entry.raw);
    const std::string blob = psu_build(*src, entry);
    src.reset();
    std::string o = S(a, "out"); if (o.empty()) o = path_join(path_join("console", "saves"), entry.name + ".psu");
    if (exists(o) && !B(a, "force")) fail(o + " exists (pass --force to overwrite)");
    makedirs(dirname(abspath(o)));
    write_bytes(o, blob);
    out("wrote " + o + ": " + entry.name + cfmt(", %zu file(s), %zu bytes", entry.children.size(), blob.size()));
    return 0;
}
inline int cmd_psu_import(const Val& a) {
    const std::string psu = S(a, "psu"), to = S(a, "to");
    const std::string blob = read_or_fail(psu);
    std::vector<PsuFile> files; const Dirent folder = psu_parse(blob, psu, files);
    const std::string name = host_name(folder.raw);
    std::vector<std::string> names; std::set<std::string> lowered;
    for (const auto& f : files) { names.push_back(host_name(f.h.raw)); }
    for (const auto& n : names) lowered.insert(lower(n));
    if (lowered.size() != names.size()) fail(psu + " has two files whose names differ only in case");
    const std::string dest = path_join(to, name);
    if (exists(dest)) {
        if (!B(a, "force")) throw Attention(dest + " already exists. Nothing written. Pass --force to replace it (mc0 is backed up first).");
        const std::string z = make_backup(to, S(a, "backups"), get(a, "keep").i);
        out("backed up " + to + " to " + z);
        rmtree(dest);
    }
    makedirs(dest);
    for (std::size_t i = 0; i < files.size(); ++i) { const std::string p = path_join(dest, names[i]); write_bytes(p, files[i].data); stamp_path(p, files[i].h.modified); }
    stamp_path(dest, folder.modified);
    out("imported " + name + cfmt(" (%zu file(s)) -> ", files.size()) + dest);
    return 0;
}
inline std::string ascii_lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return s; }
inline int cmd_psu_ls(const Val& a) {
    const std::string psu = S(a, "psu");
    const std::string blob = read_or_fail(psu);
    std::vector<PsuFile> files; const Dirent folder = psu_parse(blob, psu, files);
    Val title; List fl;
    for (const auto& f : files) if (ascii_lower(f.h.raw) == "icon.sys") title = icon_title(f.data);
    for (const auto& f : files) fl.push_back(dict_of({{"name", Val::str(f.h.raw)}, {"bytes", Val::integer(f.h.length)}, {"mode", Val::str(cfmt("0x%04x", f.h.mode))}}));
    const Val res = dict_of({{"folder", Val::str(folder.raw)}, {"modified", Val::str(tod_text(folder.modified))}, {"title", title}, {"files", Val::list(fl)}});
    if (B(a, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; out(dumps(res, o)); return 0; }
    out(folder.raw + "  (" + tod_text(folder.modified) + ")" + (title.is_none() ? std::string() : "  " + title.s));
    for (const auto& f : fl) out("    " + ljust(get(f, "name").s, 30) + cfmt(" %8lld bytes", get(f, "bytes").i));
    return 0;
}

// ---------------------------------------------------------------- backup / restore
inline int cmd_backup(const Val& a) {
    const std::string mc0 = S(a, "mc0");
    if (!is_dir(mc0)) fail(mc0 + " is not a folder (point at the port's mc0 folder)");
    const long long keep = get(a, "keep").i;
    const std::string path = make_backup(mc0, S(a, "to"), keep, S(a, "label"));
    const std::size_t n = zip::members(read_bytes(path)).size();
    out("backed up " + mc0 + cfmt(" (%zu file%s) to ", n, plural(static_cast<long long>(n)).c_str()) + path + cfmt("; keeping the newest %lld", keep));
    return 0;
}
inline std::string safe_member(const std::string& name) {
    const std::string n = replace_all(name, "\\", "/");
    static const std::string DRIVE = "^[A-Za-z]:";
    bool dots = false; for (const auto& part : split(n, "/")) if (part == "..") dots = true;
    if (starts(n, "/") || re::match(DRIVE, n) || dots || n.find('\0') != std::string::npos)
        fail("backup contains an unsafe path " + str_repr(name) + "; refusing to restore");
    return n;
}
inline int cmd_restore(const Val& a) {
    const std::string zp = S(a, "zip"), to = S(a, "to");
    std::string z; std::vector<ZEntry> infos;
    try { z = read_bytes(zp); infos = zip_entries(z); }
    catch (const OSError& e) { fail(zp + " is not a readable zip: " + e.what()); }
    catch (const zip::BadZipFile& e) { fail(zp + " is not a readable zip: " + e.what()); }
    std::vector<std::string> names; for (const auto& x : infos) names.push_back(safe_member(x.m.name));
    if (is_dir(to) && !listdir(to).empty()) {
        if (!B(a, "force")) throw Attention(to + " is not empty. Nothing written. Pass --force to replace its contents (it is backed up first).");
        const std::string b = make_backup(to, S(a, "backups"), 0);
        out("backed up the current " + to + " to " + b);
        for (const auto& entry : listdir(to)) {
            const std::string p = path_join(to, entry);
            if (is_dir(p) && !islink(p)) rmtree(p); else remove_file(p);
        }
    }
    makedirs(to);
    std::size_t nfiles = 0;
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const std::string& n = names[i];
        std::string target = to; for (const auto& part : split(n, "/")) target = path_join(target, part);
        if (ends(n, "/")) { makedirs(target); continue; }
        ++nfiles;
        makedirs(dirname(target));
        write_bytes(target, zip_read(z, infos[i].m));
        std::tm tm{}; tm.tm_year = static_cast<int>(infos[i].ddate >> 9) + 80; tm.tm_mon = static_cast<int>((infos[i].ddate >> 5) & 15) - 1; tm.tm_mday = static_cast<int>(infos[i].ddate & 31);
        tm.tm_hour = static_cast<int>(infos[i].dtime >> 11); tm.tm_min = static_cast<int>((infos[i].dtime >> 5) & 63); tm.tm_sec = static_cast<int>((infos[i].dtime & 31) * 2); tm.tm_isdst = -1;
        const std::time_t t = std::mktime(&tm);
        if (t != static_cast<std::time_t>(-1)) set_mtime(target, static_cast<long long>(t));
    }
    out(cfmt("restored %zu file(s) from ", nfiles) + zp + " into " + to);
    return 0;
}

// ---------------------------------------------------------------- sync
struct SyncFile { std::string path; uint64_t size = 0; double mtime = 0; std::string sha; bool has_sha = false; };
using SyncMap = std::map<std::string, SyncFile>;
inline SyncMap sync_files(const std::string& root, std::vector<std::string>& placeholders) {
    SyncMap out;
    for (const auto& [p, rel] : host_files(root)) {
        const auto parts = split(rel, "/"); const std::string& base = parts.back();
        if (parts[0] == ".console-conflicts" || starts(base, ".console-")) continue;
        if (ends(base, ".icloud") && starts(base, ".")) { placeholders.push_back(rel); continue; }
        if (base == ".DS_Store" || base == "Thumbs.db" || base == "desktop.ini" || starts(base, "._") || starts(base, "_pcsx2_")) continue;
        const FStat st = stat_of(p);
        if (!st.ok) throw OSError(ENOENT, p);
        SyncFile f; f.path = p; f.size = st.size; f.mtime = st.mtime; out[rel] = f;
    }
    return out;
}
inline const std::string& sha_of(SyncFile& f) { if (!f.has_sha) { f.sha = sha256_hex(read_bytes(f.path)); f.has_sha = true; } return f.sha; }
inline void copy2(const std::string& src, const std::string& dst) {   // _copy: shutil.copy2 to a temp name, then os.replace
    makedirs(dirname(dst));
    const std::string tmp = path_join(dirname(dst), ".console-tmp-" + basename(dst));
    std::error_code ec;
    std::filesystem::copy_file(fspath(src), fspath(tmp), std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) throw OSError(ec.value(), ec.value() == ENOENT && !exists(src) ? src : tmp);
    std::filesystem::permissions(fspath(tmp), std::filesystem::status(fspath(src), ec).permissions(), ec);
    const auto ft = std::filesystem::last_write_time(fspath(src), ec);
    if (!ec) std::filesystem::last_write_time(fspath(tmp), ft, ec);
    replace_file(tmp, dst);
}
inline std::string device_name() {
    std::string h;
#if defined(_WIN32)
    if (const char* c = std::getenv("COMPUTERNAME")) h = c;
#else
    char buf[256] = {}; if (::gethostname(buf, sizeof buf - 1) == 0) h = buf;
#endif
    h = h.substr(0, h.find('.'));
    static const std::string PAT = "[^A-Za-z0-9_-]";
    h = re::sub(PAT, "_", h);
    return h.empty() ? "device" : h;
}
inline std::string mc_type_name(const Val& v) {
    switch (v.t) { case Val::NONE: return "NoneType"; case Val::BOOL: return "bool"; case Val::INT: return "int"; case Val::FLOAT: return "float";
                   case Val::STR: return "str"; case Val::LIST: return "list"; default: return "dict"; }
}
inline Val mc_get(const Val& o, const std::string& k, Val dflt) {   // o.get(k, dflt) on whatever json.load gave
    if (o.t != Val::DICT) throw PyErr("AttributeError", "'" + mc_type_name(o) + "' object has no attribute 'get'");
    return o.d->at(k, std::move(dflt));
}
inline Val bsha(const Val& b) {   // B["sha"]
    if (b.t == Val::DICT) { if (const Val* p = b.d->get("sha")) return *p; throw PyErr("KeyError", "'sha'"); }
    if (b.t == Val::LIST) throw PyErr("TypeError", "list indices must be integers or slices, not str");
    if (b.t == Val::STR) throw PyErr("TypeError", "string indices must be integers, not 'str'");
    throw PyErr("TypeError", "'" + mc_type_name(b) + "' object is not subscriptable");
}
struct Step { std::string rel, act, why; };
inline std::vector<Step> plan_sync(SyncMap& local, SyncMap& cloud, const Val& base, const std::string& direction, const std::string& prefer) {
    std::set<std::string> rels; for (const auto& kv : local) rels.insert(kv.first); for (const auto& kv : cloud) rels.insert(kv.first);
    std::vector<Step> plan;
    for (const auto& rel : rels) {
        SyncFile* L = local.count(rel) ? &local[rel] : nullptr; SyncFile* C = cloud.count(rel) ? &cloud[rel] : nullptr;
        const Val B = mc_get(base, rel, Val()); const bool hasB = truthy(B);
        if (L && C && L->size == C->size && sha_of(*L) == sha_of(*C)) { plan.push_back({rel, "same", "identical"}); continue; }
        std::string act, why;
        if (L && !C) { act = "push"; why = !hasB ? "new here" : "missing in the cloud (sync never deletes)"; }
        else if (C && !L) { act = "pull"; why = !hasB ? "new in the cloud" : "missing here (sync never deletes)"; }
        else if (hasB && [&] { const Val s = bsha(B); return s.t == Val::STR && s.s == sha_of(*L); }()) { act = "pull"; why = "changed in the cloud since the last sync"; }
        else if (hasB && [&] { const Val s = bsha(B); return s.t == Val::STR && s.s == sha_of(*C); }()) { act = "push"; why = "changed here since the last sync"; }
        else {
            why = hasB ? "changed on both sides since the last sync" : "different on both sides and never synced";
            if (prefer == "local") act = "push-keep";
            else if (prefer == "cloud") act = "pull-keep";
            else if (prefer == "newest" && L->mtime != C->mtime) act = L->mtime > C->mtime ? "push-keep" : "pull-keep";
            else act = "conflict";
            if (act != "conflict") why += "; --prefer " + prefer;
        }
        if (direction == "push" && starts(act, "pull")) { act = "skip"; why += " (run without --push to bring it here)"; }
        else if (direction == "pull" && starts(act, "push")) { act = "skip"; why += " (run without --pull to send it up)"; }
        plan.push_back({rel, act, why});
    }
    return plan;
}
inline std::string rel_join(const std::string& root, const std::string& rel) { std::string p = root; for (const auto& part : split(rel, "/")) p = path_join(p, part); return p; }
inline Val plan_json(const std::vector<Step>& plan) {
    List l; for (const auto& s : plan) l.push_back(dict_of({{"path", Val::str(s.rel)}, {"action", Val::str(s.act)}, {"reason", Val::str(s.why)}}));
    return Val::list(std::move(l));
}
inline int cmd_sync(const Val& a) {
    const std::string mc0 = S(a, "mc0"), cloud = S(a, "cloud");
    const bool dry = B(a, "dry_run"), as_json = B(a, "json");
    if (!is_dir(mc0)) fail(mc0 + " is not a folder (point at the port's mc0 folder)");
    const std::string am = abspath(mc0), ac = abspath(cloud);
    if (starts(ac, am + ossep()) || starts(am, ac + ossep()) || am == ac) fail("the cloud folder and mc0 must not contain each other");
    if (exists(cloud) && !is_dir(cloud)) fail(cloud + " is a file, not a folder");
    if (!is_dir(cloud)) {
        if (!is_dir(dirname(ac))) fail(cloud + " does not exist (and neither does its parent): check the cloud path");
        if (!dry) makedirs(cloud);
    }
    const std::string direction = B(a, "push") ? "push" : B(a, "pull") ? "pull" : "both";
    std::string mpath = S(a, "manifest"); if (mpath.empty()) mpath = path_join(dirname(am), ".console-sync.json");
    Val manifest = Val::dict();
    if (exists(mpath)) {
        try { manifest = loads(read_text(mpath)); }
        catch (const JSONDecodeError&) { fail(mpath + " is not valid JSON; move it aside to start fresh"); }
    }
    const std::string key = am + " -> " + ac;
    const Val base = mc_get(mc_get(manifest, key, Val::dict()), "files", Val::dict());
    std::vector<std::string> lph, rph;
    SyncMap local = sync_files(mc0, lph);
    SyncMap remote; if (is_dir(cloud)) remote = sync_files(cloud, rph);
    if (!lph.empty() || !rph.empty()) {
        std::vector<std::string> all = lph; all.insert(all.end(), rph.begin(), rph.end());
        std::vector<std::string> first3(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(3, all.size())));
        std::fflush(stdout);
        std::fprintf(stderr, "refused: %zu file(s) are cloud placeholders not downloaded yet (%s). Download the folder first (Finder: Download Now, or keep it downloaded), then sync.\n",
                     all.size(), join(first3, ", ").c_str());
        return 1;
    }
    const std::vector<Step> plan = plan_sync(local, remote, base, direction, S(a, "prefer"));
    std::map<std::string, long long> counts; for (const auto& s : plan) counts[s.act]++;
    const auto cnt = [&](const char* k) { const auto it = counts.find(k); return it == counts.end() ? 0LL : it->second; };
    if (as_json && dry) { out(dumps2(plan_json(plan))); return cnt("conflict") ? 1 : 0; }
    const std::string stamp = utc_stamp();
    const std::string cdir = path_join(path_join(cloud, ".console-conflicts"), stamp + "-" + device_name());
    bool writes_here = false; for (const auto& s : plan) if (s.act == "pull" || s.act == "pull-keep") writes_here = true;
    Val backup;
    if (writes_here && !dry && !host_files(mc0).empty()) backup = Val::str(make_backup(mc0, S(a, "backups"), get(a, "keep").i));
    Val newbase = Val::dict(); if (base.t == Val::DICT) newbase.d->items = base.d->items;
    for (const auto& s : plan) {
        SyncFile* L = local.count(s.rel) ? &local[s.rel] : nullptr; SyncFile* C = remote.count(s.rel) ? &remote[s.rel] : nullptr;
        const std::string lp = rel_join(mc0, s.rel), cp = rel_join(cloud, s.rel);
        const auto rec = [&](SyncFile& f) { newbase.d->set(s.rel, dict_of({{"sha", Val::str(sha_of(f))}, {"size", Val::integer(static_cast<long long>(f.size))}})); };
        if (!dry) {
            if (s.act == "conflict" || s.act == "push-keep" || s.act == "pull-keep") {
                copy2(L->path, rel_join(path_join(cdir, "here"), s.rel));
                copy2(C->path, rel_join(path_join(cdir, "cloud"), s.rel));
            }
            if (s.act == "push" || s.act == "push-keep") { copy2(L->path, cp); rec(*L); }
            else if (s.act == "pull" || s.act == "pull-keep") { copy2(C->path, lp); rec(*C); }
            else if (s.act == "same") rec(*L);
        }
        if (!as_json && (s.act != "same" || B(a, "verbose"))) out(cfmt("%-10s ", s.act.c_str()) + s.rel + "  (" + s.why + ")");
    }
    { std::vector<std::pair<std::string, Val>> kept;
      for (auto& kv : newbase.d->items) if (local.count(kv.first) || remote.count(kv.first)) kept.push_back(kv);
      newbase.d->items.swap(kept); }
    if (!dry) {
        manifest.d->set(key, dict_of({{"files", newbase}, {"last_sync", Val::str(stamp)}, {"device", Val::str(device_name())}}));
        const std::string tmp = mpath + ".tmp";
        DumpOpts o; o.indent = 1; o.sort_keys = true;
        write_text(tmp, dumps(manifest, o));
        replace_file(tmp, mpath);
    }
    const bool copies = (cnt("conflict") || cnt("push-keep") || cnt("pull-keep")) && !dry;
    Val summary = dict_of({{"pushed", Val::integer(cnt("push") + cnt("push-keep"))}, {"pulled", Val::integer(cnt("pull") + cnt("pull-keep"))},
                           {"same", Val::integer(cnt("same"))}, {"conflicts", Val::integer(cnt("conflict"))}, {"skipped", Val::integer(cnt("skip"))},
                           {"backup", backup}, {"dry_run", Val::boolean(dry)}, {"conflict_copies", copies ? Val::str(cdir) : Val()}});
    if (as_json) { summary.d->set("plan", plan_json(plan)); out(dumps2(summary)); }
    else {
        out(cfmt("%s%lld pushed, %lld pulled, %lld already the same, %lld conflict(s), %lld skipped", dry ? "dry run: " : "",
                 get(summary, "pushed").i, get(summary, "pulled").i, get(summary, "same").i, get(summary, "conflicts").i, get(summary, "skipped").i));
        if (!backup.is_none()) out("mc0 was backed up first: " + backup.s);
        if (copies) out("both versions of every conflict are in " + cdir + " (here/ and cloud/). Nothing was overwritten for a conflict: pick one and rerun with --prefer local|cloud|newest.");
    }
    return cnt("conflict") ? 1 : 0;
}

// ---------------------------------------------------------------- CLI
inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "PS2 memory card saves: read card images, move saves into the port's mc0 folder, .psu files, backups and cloud sync.");
    p.epilog = "Card images are read only. Close the game before export, restore or sync.";
    p.subcommands("cmd", true); p.sub_metavar = "command";
    { Parser& s = p.sub("info"); s.help_line = "superblock of a card image (.ps2/.mcd/.bin): ECC, version, free space";
      s.pos("card"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("ls"); s.help_line = "saves on a card image, a PCSX2 folder card or an mc0 folder";
      s.pos("card"); s.opt({"--files"}).store_true().help("list the files in each save"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("export"); s.help_line = "copy saves from a card into the port's folder memory card (mc0)";
      s.pos("card").help("card image, PCSX2 folder card, or another mc0 folder");
      s.opt({"--to"}).required().help("the mc0 folder next to the game's ELF");
      s.opt({"--only"}).nargs("+").metavar("SAVE").help("only these save folders");
      s.opt({"--force"}).store_true().help("replace saves already in mc0 (backs mc0 up first)");
      s.opt({"--backups"}).help("backup folder (default console/saves/backups)");
      s.opt({"--keep"}).integer().def(20); }
    { Parser& s = p.sub("psu"); s.help_line = ".psu single-save files: export, import, ls";
      s.subcommands("psu_cmd", true); s.sub_metavar = "action";
      { Parser& q = s.sub("export"); q.help_line = "one save folder (mc0/SAVE, or a card image with --save) to a .psu";
        q.pos("source").help("mc0/<SAVE> folder, or a card image");
        q.opt({"--save"}).help("the save folder name when the source is a card image");
        q.opt({"--out"}).help("the .psu path (default console/saves/<SAVE>.psu)");
        q.opt({"--force"}).store_true(); }
      { Parser& q = s.sub("import"); q.help_line = "a .psu into mc0/<SAVE>/";
        q.pos("psu"); q.opt({"--to"}).required().help("the mc0 folder");
        q.opt({"--force"}).store_true().help("replace an existing save (backs mc0 up first)");
        q.opt({"--backups"}); q.opt({"--keep"}).integer().def(20); }
      { Parser& q = s.sub("ls"); q.help_line = "what a .psu holds";
        q.pos("psu"); q.opt({"--json"}).store_true(); } }
    { Parser& s = p.sub("backup"); s.help_line = "timestamped zip of mc0, keeping the newest N";
      s.pos("mc0");
      s.opt({"--to"}).def(Val::str(path_join(path_join("console", "saves"), "backups")));
      s.opt({"--keep"}).integer().def(20).help("backups to keep for this card (default 20, 0 = all)");
      s.opt({"--label"}).help("name prefix (default <parent folder>-<mc0 folder>)"); }
    { Parser& s = p.sub("restore"); s.help_line = "put a backup zip back into mc0 (backs up the current contents first)";
      s.pos("zip"); s.opt({"--to"}).required().help("the mc0 folder");
      s.opt({"--force"}).store_true().help("replace a non-empty mc0"); s.opt({"--backups"}); }
    { Parser& s = p.sub("sync"); s.help_line = "sync mc0 with a cloud folder: changed side wins, conflicts keep both";
      s.pos("mc0");
      s.opt({"--cloud"}).required().help("a folder your cloud app syncs (iCloud Drive, Dropbox, ...)");
      s.opt({"--push"}).store_true().help("only send changes up");
      s.opt({"--pull"}).store_true().help("only bring changes down");
      s.exclusive({"push", "pull"});
      s.opt({"--prefer"}).choices({"local", "cloud", "newest"}).help("settle conflicts this way (the losing version is still kept in .console-conflicts)");
      s.opt({"--dry-run"}).store_true().help("show what would happen, change nothing");
      s.opt({"--manifest"}).help("sync state file (default .console-sync.json next to mc0)");
      s.opt({"--backups"}).help("backup folder (default console/saves/backups)");
      s.opt({"--keep"}).integer().def(20);
      s.opt({"--verbose"}).store_true().help("also list files that are already the same");
      s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    try {
        return guard("error", [&]() -> int {
            if (cmd == "info") return cmd_info(a);
            if (cmd == "ls") return cmd_ls(a);
            if (cmd == "export") return cmd_export(a);
            if (cmd == "psu") { const std::string q = S(a, "psu_cmd"); return q == "export" ? cmd_psu_export(a) : q == "import" ? cmd_psu_import(a) : cmd_psu_ls(a); }
            if (cmd == "backup") return cmd_backup(a);
            if (cmd == "restore") return cmd_restore(a);
            return cmd_sync(a);
        });
    } catch (const Attention& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "refused: %s\n", e.what());
        return 1;
    }
}
PACKTOOL("console-saves/memcard", main);
}  // namespace packtools::console_memcard
