// console_gspacket.inc - console-graphics/gspacket: a C++ port of console-skill/console-graphics/gspacket.py (Jake
// Schincariol, MIT; it grew out of vif_gif_surgeon.py in hkmodd/ps2-recomp-Agent-SKILL, MIT). Reads the packets a PS2
// game sends to its graphics chip from a RAM dump: dma (walk a DMA source chain, then decode its data as VIF1 or GIF),
// vif (VIF codes, UNPACK sizes, MPG uploads, DIRECT payloads) and gif (tags, PACKED / REGLIST / IMAGE, GS writes),
// each ending with the issues that usually explain a black screen or garbage textures in a port.
namespace packtools::console_gspacket {
using namespace pyrt;
using namespace packtools::console_common;

const char* const PROG = "gspacket";
using opt_ll = std::optional<long long>;

const std::map<int, const char*> DMA_IDS{{0, "refe"}, {1, "cnt"}, {2, "next"}, {3, "ref"}, {4, "refs"}, {5, "call"}, {6, "ret"}, {7, "end"}};
// VIF codes (bits 24-30; bit 31 asks for an interrupt), as the runtime's VIFCmd enum. MSCALF is 0x15 and MSCNT is 0x17.
const std::map<int, const char*> VIF_NAMES{
    {0x00, "NOP"}, {0x01, "STCYCL"}, {0x02, "OFFSET"}, {0x03, "BASE"}, {0x04, "ITOP"}, {0x05, "STMOD"},
    {0x06, "MSKPATH3"}, {0x07, "MARK"}, {0x10, "FLUSHE"}, {0x11, "FLUSH"}, {0x13, "FLUSHA"}, {0x14, "MSCAL"},
    {0x15, "MSCALF"}, {0x17, "MSCNT"}, {0x20, "STMASK"}, {0x30, "STROW"}, {0x31, "STCOL"}, {0x4A, "MPG"},
    {0x50, "DIRECT"}, {0x51, "DIRECTHL"}};
inline const char* unpack_format(int vn, int vl) {
    static const char* const F[4][4] = {{"S-32", "S-16", "S-8", nullptr}, {"V2-32", "V2-16", "V2-8", nullptr},
                                        {"V3-32", "V3-16", "V3-8", nullptr}, {"V4-32", "V4-16", "V4-8", "V4-5"}};
    return F[vn][vl];
}
const char* const GIF_DESC[16] = {"PRIM", "RGBAQ", "ST", "UV", "XYZF2", "XYZ2", "TEX0_1", "TEX0_2", "CLAMP_1", "CLAMP_2",
                                  "FOG", "(reserved)", "XYZF3", "XYZ3", "A+D", "NOP"};
const char* const GIF_FLG[4] = {"PACKED", "REGLIST", "IMAGE", "DISABLED"};
// GS registers by address (A+D writes), as GSRegId in runtime/gs/gs_types.h
const std::map<int, const char*> GS_REGS{
    {0x00, "PRIM"}, {0x01, "RGBAQ"}, {0x02, "ST"}, {0x03, "UV"}, {0x04, "XYZF2"}, {0x05, "XYZ2"},
    {0x06, "TEX0_1"}, {0x07, "TEX0_2"}, {0x08, "CLAMP_1"}, {0x09, "CLAMP_2"}, {0x0A, "FOG"}, {0x0C, "XYZF3"},
    {0x0D, "XYZ3"}, {0x14, "TEX1_1"}, {0x15, "TEX1_2"}, {0x16, "TEX2_1"}, {0x17, "TEX2_2"},
    {0x18, "XYOFFSET_1"}, {0x19, "XYOFFSET_2"}, {0x1A, "PRMODECONT"}, {0x1B, "PRMODE"}, {0x1C, "TEXCLUT"},
    {0x22, "SCANMSK"}, {0x34, "MIPTBP1_1"}, {0x35, "MIPTBP1_2"}, {0x36, "MIPTBP2_1"}, {0x37, "MIPTBP2_2"},
    {0x3B, "TEXA"}, {0x3D, "FOGCOL"}, {0x3F, "TEXFLUSH"}, {0x40, "SCISSOR_1"}, {0x41, "SCISSOR_2"},
    {0x42, "ALPHA_1"}, {0x43, "ALPHA_2"}, {0x44, "DIMX"}, {0x45, "DTHE"}, {0x46, "COLCLAMP"}, {0x47, "TEST_1"},
    {0x48, "TEST_2"}, {0x49, "PABE"}, {0x4A, "FBA_1"}, {0x4B, "FBA_2"}, {0x4C, "FRAME_1"}, {0x4D, "FRAME_2"},
    {0x4E, "ZBUF_1"}, {0x4F, "ZBUF_2"}, {0x50, "BITBLTBUF"}, {0x51, "TRXPOS"}, {0x52, "TRXREG"},
    {0x53, "TRXDIR"}, {0x54, "HWREG"}, {0x60, "SIGNAL"}, {0x61, "FINISH"}, {0x62, "LABEL"}};
const char* const PRIM_TYPES[8] = {"point", "line", "line strip", "triangle", "triangle strip", "triangle fan", "sprite", "PROHIBITED"};
const std::map<int, std::pair<const char*, int>> PSM{
    {0x00, {"PSMCT32", 32}}, {0x01, {"PSMCT24", 24}}, {0x02, {"PSMCT16", 16}}, {0x0A, {"PSMCT16S", 16}},
    {0x13, {"PSMT8", 8}}, {0x14, {"PSMT4", 4}}, {0x1B, {"PSMT8H", 8}}, {0x24, {"PSMT4HL", 4}},
    {0x2C, {"PSMT4HH", 4}}, {0x30, {"PSMZ32", 32}}, {0x31, {"PSMZ24", 24}}, {0x32, {"PSMZ16", 16}}, {0x3A, {"PSMZ16S", 16}}};
const char* const SEV_ERROR = "error";
const char* const SEV_WARN = "warning";
const char* const SEV_NOTE = "note";

// "0x%0*X" % (width, v): Python pads after the sign
inline std::string hx(long long v, int width = 8) {
    const unsigned long long m = v < 0 ? 0ULL - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
    std::string h = cfmt("%llX", m);
    const std::size_t w = static_cast<std::size_t>(std::max(0, width - (v < 0 ? 1 : 0)));
    while (h.size() < w) h = "0" + h;
    return std::string("0x") + (v < 0 ? "-" : "") + h;
}
inline std::string hxu(uint64_t v, int width) { std::string h = cfmt("%llX", static_cast<unsigned long long>(v)); while (h.size() < static_cast<std::size_t>(width)) h = "0" + h; return "0x" + h; }
inline uint64_t bits(uint64_t v, int lo, int n) { return n >= 64 ? v >> lo : (v >> lo) & ((1ULL << n) - 1); }
inline std::string psm_name(uint64_t v) { const auto it = PSM.find(static_cast<int>(v)); return it == PSM.end() ? cfmt("PSM 0x%02X?", static_cast<unsigned>(v)) : it->second.first; }
inline Val uval(uint64_t v) {   // a JSON int of any 64-bit value
    if (v <= static_cast<uint64_t>(LLONG_MAX)) return Val::integer(static_cast<long long>(v));
    Val x = Val::integer(0); x.s = cfmt("%llu", static_cast<unsigned long long>(v)); return x;
}
inline Val optv(const opt_ll& v) { return v ? Val::integer(*v) : Val::none(); }
inline std::string addr_txt(const opt_ll& a) { return a ? hx(*a) : "?"; }
inline uint32_t le32(const std::string& b, std::size_t at) { return static_cast<uint32_t>(u32le(b, at)); }
inline uint64_t le64(const std::string& b, std::size_t at) { return static_cast<uint64_t>(le32(b, at)) | (static_cast<uint64_t>(le32(b, at + 4)) << 32); }
// Python's s[a:b] on a byte string
inline std::string pyslice(const std::string& s, long long a, long long b) {
    const long long n = static_cast<long long>(s.size());
    auto norm = [n](long long x) { if (x < 0) x += n; return std::clamp(x, 0LL, n); };
    a = norm(a); b = norm(b);
    return b <= a ? std::string() : s.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a));
}
inline long long slice_len(long long n, long long stop) { if (stop < 0) stop += n; return std::clamp(stop, 0LL, n); }   // len(xs[:stop])

struct Issue { std::string sev, msg; opt_ll addr; };
using Where = std::function<std::pair<opt_ll, opt_ll>(long long)>;

// ---------------------------------------------------------------- the dump
struct Dump {
    std::string data; long long base = 0;
    static long long physical(long long addr) {
        if ((0x20000000LL <= addr && addr < 0x22000000LL) || (0x30000000LL <= addr && addr < 0x32000000LL)) return addr & 0x01FFFFFF;
        if (addr >= 0x80000000LL) return addr & 0x1FFFFFFF;
        return addr;
    }
    static Dump load(const std::string& path, long long base) {
        if (!is_file(path)) throw InputError(path + " does not exist");
        Dump d; d.base = base;
        std::FILE* f = open_file(path, "rb");
        if (!f) throw OSError(errno, path);
        char buf[65536]; std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.data.append(buf, n);
        std::fclose(f);
        if (d.data.empty()) throw InputError(path + " is empty");
        return d;
    }
    opt_ll offset(long long addr) const {
        const long long off = physical(addr) - physical(base);
        if (off < 0 || off >= static_cast<long long>(data.size())) return std::nullopt;
        return off;
    }
    long long addr_of(long long off) const { return physical(base) + off; }
    std::optional<std::string> read(long long addr, long long size) const {
        const auto off = offset(addr);
        if (!off || *off + size > static_cast<long long>(data.size())) return std::nullopt;
        return data.substr(static_cast<std::size_t>(*off), static_cast<std::size_t>(size));
    }
};

// ---------------------------------------------------------------- DMA source chain
struct Packet { long long index, tag; std::string id; long long qwc, addr, spr, irq, pce, data; uint32_t vif[2]; };
struct Segment { long long start, length, gaddr, pkt; };
struct Chain { std::vector<Packet> packets; std::string stream; std::vector<Segment> segments; std::vector<Issue> issues; };

inline Chain walk_dma(const Dump& dump, long long tag_addr, bool tte, bool tie, long long max_tags) {
    Chain C;
    std::vector<long long> stack;
    std::map<std::pair<long long, std::vector<long long>>, long long> seen;
    long long addr = tag_addr;
    bool broke = false;
    for (long long n = 0; n < max_tags; ++n) {
        if (addr % 16) { C.issues.push_back({SEV_ERROR, "tag at " + hx(addr) + " is not 16-byte aligned", addr}); broke = true; break; }
        const auto raw = dump.read(addr, 16);
        if (!raw) { C.issues.push_back({SEV_ERROR, "tag at " + hx(addr) + " is outside the dump (the chain points off the end, or --base is wrong)", addr}); broke = true; break; }
        const uint32_t lo = le32(*raw, 0), hi = le32(*raw, 4), w2 = le32(*raw, 8), w3 = le32(*raw, 12);
        const long long qwc = lo & 0xFFFF, pce = bits(lo, 26, 2), tid = static_cast<long long>(bits(lo, 28, 3)), irq = bits(lo, 31, 1);
        const long long daddr = hi & 0x7FFFFFFF, spr = hi >> 31;
        const std::string name = DMA_IDS.at(static_cast<int>(tid));
        const auto key = std::make_pair(addr, stack);
        if (const auto it = seen.find(key); it != seen.end()) {
            C.issues.push_back({SEV_ERROR, "the chain loops: tag " + hx(addr) + cfmt(" was already visited as packet #%lld", it->second), addr}); broke = true; break;
        }
        seen[key] = n;
        const long long data_addr = (tid == 0 || tid == 3 || tid == 4) ? daddr : addr + 16;   // refe/ref/refs: data at ADDR
        C.packets.push_back({n, addr, name, qwc, daddr, spr, irq, pce, data_addr, {w2, w3}});
        if (spr && (tid == 0 || tid == 3 || tid == 4)) C.issues.push_back({SEV_WARN, cfmt("packet #%lld reads from scratchpad ", n) + hx(daddr) + " (not in a RAM dump)", addr});
        if (tte) { C.segments.push_back({static_cast<long long>(C.stream.size()), 8, addr + 8, n}); C.stream += raw->substr(8, 8); }
        if (qwc) {
            const auto payload = dump.read(data_addr, qwc * 16);
            if (!payload) { C.issues.push_back({SEV_ERROR, cfmt("packet #%lld (%s) moves %lld qwords from ", n, name.c_str(), qwc) + hx(data_addr) + ", outside the dump", addr}); broke = true; break; }
            C.segments.push_back({static_cast<long long>(C.stream.size()), qwc * 16, data_addr, n});
            C.stream += *payload;
        } else if (tid >= 1 && tid <= 4) {
            C.issues.push_back({SEV_NOTE, cfmt("packet #%lld (%s) has QWC=0: it moves no data", n, name.c_str()), addr});
        }
        if (irq && tie) { C.issues.push_back({SEV_NOTE, cfmt("packet #%lld has IRQ set and TIE is on: the transfer stops here", n), addr}); broke = true; break; }
        if (tid == 1) addr = data_addr + qwc * 16;
        else if (tid == 2) addr = daddr;
        else if (tid == 3 || tid == 4) addr = addr + 16;
        else if (tid == 5) {
            if (stack.size() >= 2) { C.issues.push_back({SEV_ERROR, cfmt("packet #%lld is a third nested call: the DMAC only keeps two return addresses", n), addr}); broke = true; break; }
            stack.push_back(data_addr + qwc * 16);
            addr = daddr;
        } else if (tid == 6) {
            if (stack.empty()) { broke = true; break; }   // ret with an empty stack ends the transfer
            addr = stack.back(); stack.pop_back();
        } else { broke = true; break; }                    // refe, end
    }
    if (!broke) C.issues.push_back({SEV_WARN, cfmt("stopped after %lld tags (--max-tags): the chain may not end", max_tags), addr});
    return C;
}
inline std::pair<opt_ll, opt_ll> guest_of(const std::vector<Segment>& segs, long long off) {
    for (const auto& s : segs) if (s.start <= off && off < s.start + s.length) return {s.gaddr + (off - s.start), s.pkt};
    return {std::nullopt, std::nullopt};
}

// ---------------------------------------------------------------- GIF packets
struct Write { std::string reg; uint64_t value; std::string detail; };
struct GifTag { long long off; opt_ll addr, packet; long long nloop, eop; std::string flg; long long nreg; std::vector<std::string> regs; std::vector<Write> writes; bool truncated = false; opt_ll bytes; };
struct Upload { long long dbp; std::string psm; long long width, height, bytes; };
struct GifState {
    std::optional<uint64_t> bitblt, trxreg;
    opt_ll expect, expect_from;   // bytes still expected by the current host -> GS upload
    std::vector<Upload> uploads;
    std::vector<std::pair<std::string, long long>> prims;
    void count_prim(const std::string& p) { for (auto& [k, v] : prims) if (k == p) { ++v; return; } prims.emplace_back(p, 1); }
};

// fields of the GS registers that explain most black screens and broken textures
inline std::string reg_detail(int reg, uint64_t v) {
    using ull = unsigned long long;
    if (reg == 0x06 || reg == 0x07)
        return "TBP0=" + hxu(bits(v, 0, 14), 4) + cfmt(" TBW=%llu ", static_cast<ull>(bits(v, 14, 6))) + psm_name(bits(v, 20, 6)) + cfmt(" %llux%llu CBP=", 1ULL << bits(v, 26, 4), 1ULL << bits(v, 30, 4))
               + hxu(bits(v, 37, 14), 4) + " CPSM=" + psm_name(bits(v, 51, 4)) + cfmt(" CSM=%llu", static_cast<ull>(bits(v, 55, 1)));
    if (reg == 0x4C || reg == 0x4D) return "FBP=" + hxu(bits(v, 0, 9), 3) + cfmt(" FBW=%llu ", static_cast<ull>(bits(v, 16, 6))) + psm_name(bits(v, 24, 6)) + " FBMSK=" + hxu(bits(v, 32, 32), 8);
    if (reg == 0x4E || reg == 0x4F) return "ZBP=" + hxu(bits(v, 0, 9), 3) + " " + psm_name(0x30 | bits(v, 24, 4)) + cfmt(" ZMSK=%llu", static_cast<ull>(bits(v, 32, 1)));
    if (reg == 0x50)
        return "SBP=" + hxu(bits(v, 0, 14), 4) + cfmt(" SBW=%llu ", static_cast<ull>(bits(v, 16, 6))) + psm_name(bits(v, 24, 6)) + " -> DBP=" + hxu(bits(v, 32, 14), 4)
               + cfmt(" DBW=%llu ", static_cast<ull>(bits(v, 48, 6))) + psm_name(bits(v, 56, 6));
    if (reg == 0x51) return cfmt("src %llu,%llu dst %llu,%llu", static_cast<ull>(bits(v, 0, 11)), static_cast<ull>(bits(v, 16, 11)), static_cast<ull>(bits(v, 32, 11)), static_cast<ull>(bits(v, 48, 11)));
    if (reg == 0x52) return cfmt("%llux%llu", static_cast<ull>(bits(v, 0, 12)), static_cast<ull>(bits(v, 32, 12)));
    if (reg == 0x53) { static const char* const D[4] = {"host -> GS (upload)", "GS -> host (download)", "GS -> GS (copy)", "off"}; return D[bits(v, 0, 2)]; }
    if (reg == 0x00) return std::string(PRIM_TYPES[v & 7]) + (v & 0x10 ? " textured" : "") + (v & 0x40 ? " blended" : "") + (v & 0x200 ? " context 2" : "");
    if (reg == 0x40 || reg == 0x41) return cfmt("x %llu..%llu y %llu..%llu", static_cast<ull>(bits(v, 0, 11)), static_cast<ull>(bits(v, 16, 11)), static_cast<ull>(bits(v, 32, 11)), static_cast<ull>(bits(v, 48, 11)));
    return "";
}
inline void close_upload(GifState& st, std::vector<Issue>& issues) {
    if (st.expect && *st.expect)
        issues.push_back({SEV_WARN, "image upload started at " + (st.expect_from ? hx(*st.expect_from) : std::string("?")) + cfmt(" is %lld bytes short of its TRXREG size: the texture will be incomplete", *st.expect), st.expect_from});
    st.expect.reset();
}
inline void gs_write(GifState& st, int reg, uint64_t value, const opt_ll& gaddr, std::vector<Write>& out_, std::vector<Issue>& issues) {
    const auto it = GS_REGS.find(reg);
    out_.push_back({it == GS_REGS.end() ? cfmt("reg 0x%02X?", static_cast<unsigned>(reg)) : it->second, value, reg_detail(reg, value)});
    if (it == GS_REGS.end()) issues.push_back({SEV_WARN, cfmt("write to an unknown GS register 0x%02X", static_cast<unsigned>(reg)), gaddr});
    if (reg == 0x00) {
        const int p = static_cast<int>(value & 7);
        st.count_prim(PRIM_TYPES[p]);
        if (p == 7) issues.push_back({SEV_ERROR, "PRIM type 7 is prohibited on the GS", gaddr});
    } else if (reg == 0x50) st.bitblt = value;
    else if (reg == 0x52) st.trxreg = value;
    else if (reg == 0x53) {
        close_upload(st, issues);
        if (bits(value, 0, 2) == 0 && st.bitblt && st.trxreg) {
            const long long w = static_cast<long long>(bits(*st.trxreg, 0, 12)), h = static_cast<long long>(bits(*st.trxreg, 32, 12));
            const uint64_t dpsm = bits(*st.bitblt, 56, 6);
            const auto p = PSM.find(static_cast<int>(dpsm));
            const long long bpp = p == PSM.end() ? 0 : p->second.second;
            if (bpp) {
                st.expect = (w * h * bpp + 7) / 8;
                st.expect_from = gaddr;
                st.uploads.push_back({static_cast<long long>(bits(*st.bitblt, 32, 14)), psm_name(dpsm), w, h, *st.expect});
            }
        }
    }
}
inline std::vector<GifTag> decode_gif(const std::string& buf, const Where& where, bool inside_direct, GifState& st, std::vector<Issue>& issues) {
    std::vector<GifTag> tags;
    long long pos = 0;
    const long long size = static_cast<long long>(buf.size());
    while (pos + 16 <= size) {
        const uint64_t lo = le64(buf, static_cast<std::size_t>(pos)), hi = le64(buf, static_cast<std::size_t>(pos + 8));
        const auto [gaddr, pkt] = where(pos);
        const long long nloop = static_cast<long long>(lo & 0x7FFF), eop = static_cast<long long>(bits(lo, 15, 1)), pre = static_cast<long long>(bits(lo, 46, 1));
        const uint64_t prim = bits(lo, 47, 11);
        const int flg = static_cast<int>(bits(lo, 58, 2));
        const long long nreg = bits(lo, 60, 4) ? static_cast<long long>(bits(lo, 60, 4)) : 16;
        std::vector<int> descs; for (long long i = 0; i < nreg; ++i) descs.push_back(static_cast<int>((hi >> (4 * i)) & 0xF));
        GifTag tag{pos, gaddr, pkt, nloop, eop, GIF_FLG[flg], nreg, {}, {}, false, std::nullopt};
        for (const int d : descs) tag.regs.push_back(GIF_DESC[d]);
        pos += 16;
        if (pre && flg != 2) gs_write(st, 0x00, prim, gaddr, tag.writes, issues);
        long long need;
        if (flg == 0) need = nloop * nreg * 16;
        else if (flg == 1) need = ((nloop * nreg * 8) + 15) & ~15LL;
        else {
            need = nloop * 16;
            if (flg == 3) issues.push_back({SEV_WARN, "GIF tag with FLG=3 (disabled): treated as IMAGE", gaddr});
        }
        if (pos + need > size) {
            tag.truncated = true;
            tags.push_back(tag);
            issues.push_back({inside_direct ? SEV_NOTE : SEV_ERROR, std::string(GIF_FLG[flg]) + cfmt(" GIF packet needs %lld bytes but ", need) + (inside_direct ? "its DIRECT block" : "the buffer")
                              + cfmt(" has %lld left", size - pos) + (inside_direct ? " (it may continue in the next DIRECT)" : ""), gaddr});
            break;
        }
        if (flg == 0) {
            for (long long i = 0; i < nloop * nreg; ++i) {
                const int d = descs[static_cast<std::size_t>(i % nreg)];
                const uint64_t qlo = le64(buf, static_cast<std::size_t>(pos + i * 16)), qhi = le64(buf, static_cast<std::size_t>(pos + i * 16 + 8));
                if (d == 0xE) gs_write(st, static_cast<int>(qhi & 0xFF), qlo, gaddr, tag.writes, issues);
                else if (d == 0x0) gs_write(st, 0x00, qlo & 0x7FF, gaddr, tag.writes, issues);
                else if (d >= 0x6 && d <= 0x9) gs_write(st, d, qlo, gaddr, tag.writes, issues);
            }
        } else if (flg == 1) {
            for (long long i = 0; i < nloop * nreg; ++i) {
                const int d = descs[static_cast<std::size_t>(i % nreg)];
                const uint64_t word = le64(buf, static_cast<std::size_t>(pos + i * 8));
                if (d == 0x0) gs_write(st, 0x00, word & 0x7FF, gaddr, tag.writes, issues);
                else if (d >= 0x6 && d <= 0x9) gs_write(st, d, word, gaddr, tag.writes, issues);
            }
        } else {
            if (!st.expect) issues.push_back({SEV_WARN, "IMAGE data with no upload set up before it (BITBLTBUF, TRXREG, then TRXDIR=0)", gaddr});
            else {
                *st.expect -= need;
                if (*st.expect < 0) {
                    issues.push_back({SEV_WARN, "IMAGE data overruns the upload set up at " + (st.expect_from ? hx(*st.expect_from) : std::string("?")) + cfmt(" by %lld bytes: the wrong TRXREG or pixel format", -*st.expect), gaddr});
                    st.expect.reset();
                } else if (*st.expect == 0) st.expect.reset();
            }
        }
        tag.bytes = need;
        pos += need;
        tags.push_back(tag);
    }
    if (!inside_direct) {
        close_upload(st, issues);
        if (0 < size - pos) issues.push_back({SEV_WARN, cfmt("%lld bytes after the last whole GIF packet", size - pos), std::nullopt});
        if (!tags.empty() && !tags.back().eop) issues.push_back({SEV_NOTE, "the last GIF tag has EOP=0: the packet continues past this buffer", tags.back().addr});
    }
    return tags;
}

// ---------------------------------------------------------------- VIF1 stream
// bytes an UNPACK reads, the way the runtime's VIF1 interpreter counts them
inline long long unpack_bytes(int vn, int vl, long long count, long long cl, long long wl) {
    const long long comp_bits = vl == 0 ? 32 : vl == 1 ? 16 : vl == 2 ? 8 : (vn == 3 ? 4 : 16);
    const long long vec_bits = (vl == 3 && vn == 3) ? 16 : (vn + 1) * comp_bits;
    const long long per_vec = (vec_bits + 7) / 8;
    if (!cl) cl = 1;
    if (!wl) wl = 1;
    long long src = count;
    if (cl < wl) { const long long full = count / wl, rem = count % wl; src = full * cl + std::min(rem, cl); }
    return (src * per_vec + 3) & ~3LL;
}
struct VifCmd { long long off; opt_ll addr, packet; uint32_t code; long long irq; std::string cmd, detail; bool truncated = false; };
struct VifResult { std::vector<VifCmd> cmds; std::vector<std::pair<std::string, long long>> stats; std::vector<GifTag> gif_tags; std::vector<Issue> issues; };

inline VifResult decode_vif(const std::string& stream, bool gif, const Where& where) {
    VifResult R;
    long long cycle = 0, pos = 0;
    const long long size = static_cast<long long>(stream.size());
    auto bump = [&](const std::string& n) { for (auto& [k, v] : R.stats) if (k == n) { ++v; return; } R.stats.emplace_back(n, 1); };
    while (pos + 4 <= size) {
        const long long start = pos;
        const uint32_t code = le32(stream, static_cast<std::size_t>(pos));
        pos += 4;
        const int op = static_cast<int>((code >> 24) & 0x7F);
        const long long num = (code >> 16) & 0xFF, imm = code & 0xFFFF, irq = code >> 31;
        const auto [gaddr, pkt] = where(start);
        VifCmd e{start, gaddr, pkt, code, irq, "", "", false};
        long long need = 0;
        std::string name;
        if (const auto it = VIF_NAMES.find(op); it != VIF_NAMES.end()) {
            name = it->second; e.cmd = name;
            if (op == 0x01) { cycle = imm; e.detail = cfmt("CL=%lld WL=%lld", imm & 0xFF, imm >> 8); }
            else if (op == 0x02 || op == 0x03 || op == 0x04) e.detail = name + cfmt("=%lld", imm & 0x3FF);
            else if (op == 0x14 || op == 0x15) e.detail = "start VU1 microprogram at " + hx(imm * 8, 4);
            else if (op == 0x20) need = 4;
            else if (op == 0x30 || op == 0x31) need = 16;
            else if (op == 0x4A) {
                const long long count = num ? num : 256;
                need = count * 8;
                e.detail = cfmt("upload %lld VU1 instructions to ", count) + hx(imm * 8, 4);
                if (pos % 8) R.issues.push_back({SEV_WARN, cfmt("MPG data at stream offset %lld is not 64-bit aligned", pos), gaddr});
            } else if (op == 0x50 || op == 0x51) {
                const long long qw = imm ? imm : 65536;
                need = qw * 16;
                e.detail = cfmt("%lld qwords to the GIF (PATH2)", qw);
                if (pos % 16) R.issues.push_back({SEV_WARN, name + cfmt(" data at stream offset %lld is not 128-bit aligned: real hardware expects NOP padding before it, so the stream may be decoded from the wrong place", pos), gaddr});
            }
        } else if ((op & 0x60) == 0x60) {
            const int vn = (op >> 2) & 3, vl = op & 3;
            const char* f = unpack_format(vn, vl);
            if (!f) {
                R.issues.push_back({SEV_ERROR, cfmt("UNPACK with an invalid format (vn=%d vl=%d) at stream offset %lld", vn, vl, start), gaddr});
                e.cmd = "UNPACK ?";
                R.cmds.push_back(e);
                break;
            }
            const long long count = num ? num : 256;
            need = unpack_bytes(vn, vl, count, cycle & 0xFF, cycle >> 8);
            name = "UNPACK"; e.cmd = name;
            e.detail = std::string(f) + cfmt(" x%lld to VU1 ", count) + hx(imm & 0x3FF, 3) + (imm & 0x8000 ? " +TOPS" : "") + (imm & 0x4000 ? " unsigned" : "") + (op & 0x10 ? " masked" : "");
        } else {
            name = cfmt("UNKNOWN 0x%02X", static_cast<unsigned>(op)); e.cmd = name;
            R.cmds.push_back(e);
            R.issues.push_back({SEV_ERROR, "VIF code " + hx(code) + cfmt(" at stream offset %lld is not a command the VIF knows: the stream is corrupt, or decoding started in the wrong place", start), gaddr});
            break;
        }
        bump(name);
        if (pos + need > size) {
            R.issues.push_back({SEV_ERROR, name + cfmt(" at stream offset %lld needs %lld bytes but only %lld are left: the stream ends in the middle of a command", start, need, size - pos), gaddr});
            e.truncated = true;
            R.cmds.push_back(e);
            break;
        }
        if ((name == "DIRECT" || name == "DIRECTHL") && gif) {
            const long long b = pos;
            GifState st;
            const auto tags = decode_gif(stream.substr(static_cast<std::size_t>(pos), static_cast<std::size_t>(need)), [&](long long o) { return where(b + o); }, true, st, R.issues);
            R.gif_tags.insert(R.gif_tags.end(), tags.begin(), tags.end());
        }
        pos += need;
        R.cmds.push_back(e);
    }
    if (0 < size - pos && size - pos < 4) R.issues.push_back({SEV_WARN, cfmt("%lld stray bytes at the end of the stream", size - pos), std::nullopt});
    return R;
}

// ---------------------------------------------------------------- output
inline Val issues_json(const std::vector<Issue>& is) { List l; for (const auto& i : is) l.push_back(dict_of({{"severity", Val::str(i.sev)}, {"message", Val::str(i.msg)}, {"addr", optv(i.addr)}})); return Val::list(std::move(l)); }
inline Val counts_json(const std::vector<std::pair<std::string, long long>>& c) { Val d = Val::dict(); for (const auto& [k, v] : c) d.d->set(k, Val::integer(v)); return d; }
inline Val gif_json(const std::vector<GifTag>& tags) {
    List l;
    for (const auto& t : tags) {
        List ws; for (const auto& w : t.writes) ws.push_back(dict_of({{"reg", Val::str(w.reg)}, {"value", uval(w.value)}, {"detail", Val::str(w.detail)}}));
        Val d = dict_of({{"off", Val::integer(t.off)}, {"addr", optv(t.addr)}, {"packet", optv(t.packet)}, {"nloop", Val::integer(t.nloop)}, {"eop", Val::integer(t.eop)},
                         {"flg", Val::str(t.flg)}, {"nreg", Val::integer(t.nreg)}, {"regs", str_list(t.regs)}, {"writes", Val::list(std::move(ws))}});
        if (t.truncated) d.d->set("truncated", Val::boolean(true));
        if (t.bytes) d.d->set("bytes", Val::integer(*t.bytes));
        l.push_back(d);
    }
    return Val::list(std::move(l));
}
inline Val vif_json(const std::vector<VifCmd>& cmds) {
    List l;
    for (const auto& c : cmds) {
        Val d = dict_of({{"off", Val::integer(c.off)}, {"addr", optv(c.addr)}, {"packet", optv(c.packet)}, {"code", Val::integer(c.code)}, {"irq", Val::integer(c.irq)}, {"cmd", Val::str(c.cmd)}});
        if (!c.detail.empty()) d.d->set("detail", Val::str(c.detail));
        if (c.truncated) d.d->set("truncated", Val::boolean(true));
        l.push_back(d);
    }
    return Val::list(std::move(l));
}
inline std::string dumps1(const Val& v) { DumpOpts o; o.indent = 1; return dumps(v, o); }

inline void print_issues(const std::vector<Issue>& issues) {
    if (issues.empty()) { out("\nissues: none"); return; }
    out(cfmt("\nissues (%zu):", issues.size()));
    for (const char* sev : {SEV_ERROR, SEV_WARN, SEV_NOTE})
        for (const auto& i : issues)
            if (i.sev == sev) out("  " + ljust(i.sev, 7) + " " + i.msg + (i.addr ? "  [" + addr_txt(i.addr) + "]" : ""));
}
inline std::string prims_txt(const GifState& st) {
    auto p = st.prims; std::sort(p.begin(), p.end());
    std::vector<std::string> o; for (const auto& [k, v] : p) o.push_back(k + cfmt(" x%lld", v));
    return join(o, ", ");
}
inline void print_gif(const std::vector<GifTag>& tags, long long limit, const GifState& st) {
    long long shown = 0;
    for (const auto& t : tags) {
        if (shown >= limit) { out(cfmt("  ... %lld more GIF tags (raise --max-lines)", static_cast<long long>(tags.size()) - shown)); break; }
        out("  GIF " + t.flg + cfmt(" nloop=%lld nreg=%lld eop=%lld regs=", t.nloop, t.nreg, t.eop) + join(t.regs, ",") + "  @" + addr_txt(t.addr));
        ++shown;
        for (const auto& w : t.writes) {
            if (shown >= limit) break;
            out("      " + ljust(w.reg, 10) + " " + hxu(w.value, 16) + "  " + w.detail);
            ++shown;
        }
    }
    if (!st.prims.empty()) out("\nprimitives set: " + prims_txt(st));
    if (!st.uploads.empty()) {
        std::vector<std::string> u; for (const auto& x : st.uploads) u.push_back(cfmt("%lldx%lld ", x.width, x.height) + x.psm + " to DBP " + hx(x.dbp, 4) + cfmt(" (%lld bytes)", x.bytes));
        out("image uploads: " + join(u, "; "));
    }
}
inline int exit_code(const std::vector<Issue>& is) { for (const auto& i : is) if (i.sev == SEV_ERROR) return 1; return 0; }
inline std::string vif_name(uint32_t code) {
    const int op = static_cast<int>((code >> 24) & 0x7F);
    if (const auto it = VIF_NAMES.find(op); it != VIF_NAMES.end()) return it->second;
    if ((op & 0x60) == 0x60) return "UNPACK";
    return cfmt("UNKNOWN 0x%02X", static_cast<unsigned>(op));
}
inline void print_vif(const VifResult& R, long long max_lines, bool verbose) {
    auto st = R.stats; std::stable_sort(st.begin(), st.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::string> sc; for (const auto& [k, v] : st) sc.push_back(k + cfmt(" x%lld", v));
    out(cfmt("\nVIF1 stream: %zu commands (", R.cmds.size()) + join(sc, ", ") + ")");
    const long long n = static_cast<long long>(R.cmds.size()), shown = slice_len(n, max_lines);
    for (long long k = 0; k < shown; ++k) {
        const auto& c = R.cmds[static_cast<std::size_t>(k)];
        out("  " + addr_txt(c.addr) + " " + ljust(c.cmd, 9) + (c.detail.empty() ? "" : "  " + c.detail) + (c.irq ? "  (interrupt)" : ""));
    }
    if (n > max_lines) out(cfmt("  ... %lld more VIF commands", n - max_lines));
    if (!R.gif_tags.empty()) {
        GifState gs;
        for (const auto& t : R.gif_tags) for (const auto& w : t.writes) if (w.reg == "PRIM") gs.count_prim(PRIM_TYPES[w.value & 7]);
        out(cfmt("\nGIF tags inside DIRECT blocks: %zu", R.gif_tags.size()));
        if (verbose) print_gif(R.gif_tags, max_lines, gs);
        else if (!gs.prims.empty()) out("primitives set: " + prims_txt(gs));
    }
}

// the bytes a vif/gif command reads: --offset (file) or --addr (guest), then --length
inline std::pair<long long, std::string> region(const Dump& dump, const Val& a) {
    long long off;
    const long long len = static_cast<long long>(dump.data.size());
    if (!get(a, "addr").is_none()) {
        const auto o = dump.offset(get(a, "addr").i);
        if (!o) throw InputError(hx(get(a, "addr").i) + " is not inside the dump (base " + hx(dump.base) + cfmt(", %lld bytes)", len));
        off = *o;
    } else {
        off = get(a, "offset").is_none() ? 0 : get(a, "offset").i;
        if (off >= len) throw InputError(cfmt("offset %lld is past the end of the dump (%lld bytes)", off, len));
    }
    const long long end = get(a, "length").is_none() ? len : std::min(len, off + get(a, "length").i);
    return {off, pyslice(dump.data, off, end)};
}

inline int cmd_dma(const Val& a) {
    const Dump dump = Dump::load(S(a, "dump"), get(a, "base").i);
    const std::string channel = S(a, "channel");
    const bool tte = channel == "vif1" && !B(a, "no_tte");
    const long long tag = get(a, "tag").i, max_lines = get(a, "max_lines").i;
    Chain C = walk_dma(dump, tag, tte, B(a, "tie"), get(a, "max_tags").i);
    const Where where = [&](long long o) { return guest_of(C.segments, o); };
    GifState state;
    VifResult R;
    if (channel == "vif1") R = decode_vif(C.stream, !B(a, "no_gif"), where);
    else R.gif_tags = decode_gif(C.stream, where, false, state, R.issues);
    std::vector<Issue> issues = C.issues; issues.insert(issues.end(), R.issues.begin(), R.issues.end());
    if (B(a, "json")) {
        List pk;
        for (const auto& p : C.packets)
            pk.push_back(dict_of({{"index", Val::integer(p.index)}, {"tag", Val::integer(p.tag)}, {"id", Val::str(p.id)}, {"qwc", Val::integer(p.qwc)}, {"addr", Val::integer(p.addr)},
                                  {"spr", Val::integer(p.spr)}, {"irq", Val::integer(p.irq)}, {"pce", Val::integer(p.pce)}, {"data", Val::integer(p.data)},
                                  {"vif", Val::list({Val::integer(p.vif[0]), Val::integer(p.vif[1])})}}));
        out(dumps1(dict_of({{"packets", Val::list(std::move(pk))}, {"stream_bytes", Val::integer(static_cast<long long>(C.stream.size()))}, {"vif", vif_json(R.cmds)},
                            {"vif_counts", counts_json(R.stats)}, {"gif_tags", gif_json(R.gif_tags)}, {"issues", issues_json(issues)}})));
        return exit_code(issues);
    }
    out("DMA chain from " + hx(tag) + " (" + upper(channel) + " channel, TTE " + (tte ? "on" : "off") + cfmt("): %zu packets, %zu bytes of data", C.packets.size(), C.stream.size()));
    const long long np = static_cast<long long>(C.packets.size());
    for (long long k = 0; k < slice_len(np, max_lines); ++k) {
        const auto& p = C.packets[static_cast<std::size_t>(k)];
        const std::string extra = tte ? "  tag VIF: " + vif_name(p.vif[0]) + ", " + vif_name(p.vif[1]) : "";
        out("  #" + ljust(cfmt("%lld", p.index), 4) + " " + hx(p.tag) + " " + ljust(p.id, 4) + " qwc=" + ljust(cfmt("%lld", p.qwc), 5) + " data@" + hx(p.data) + (p.irq ? "  irq" : "") + extra);
    }
    if (np > max_lines) out(cfmt("  ... %lld more packets", np - max_lines));
    if (channel == "vif1") print_vif(R, max_lines, B(a, "verbose"));
    else { out(cfmt("\nGIF packets: %zu", R.gif_tags.size())); print_gif(R.gif_tags, max_lines, state); }
    print_issues(issues);
    return exit_code(issues);
}
inline int cmd_vif(const Val& a) {
    const Dump dump = Dump::load(S(a, "dump"), get(a, "base").i);
    const auto [off, data] = region(dump, a);
    const long long o0 = off;
    const VifResult R = decode_vif(data, !B(a, "no_gif"), [&](long long o) { return std::make_pair(opt_ll(dump.addr_of(o0 + o)), opt_ll()); });
    if (B(a, "json")) {
        out(dumps1(dict_of({{"vif", vif_json(R.cmds)}, {"vif_counts", counts_json(R.stats)}, {"gif_tags", gif_json(R.gif_tags)}, {"issues", issues_json(R.issues)}})));
        return exit_code(R.issues);
    }
    print_vif(R, get(a, "max_lines").i, B(a, "verbose"));
    print_issues(R.issues);
    return exit_code(R.issues);
}
inline int cmd_gif(const Val& a) {
    const Dump dump = Dump::load(S(a, "dump"), get(a, "base").i);
    const auto [off, data] = region(dump, a);
    const long long o0 = off;
    GifState state; std::vector<Issue> issues;
    const auto tags = decode_gif(data, [&](long long o) { return std::make_pair(opt_ll(dump.addr_of(o0 + o)), opt_ll()); }, false, state, issues);
    if (B(a, "json")) {
        List up; for (const auto& u : state.uploads) up.push_back(dict_of({{"dbp", Val::integer(u.dbp)}, {"psm", Val::str(u.psm)}, {"width", Val::integer(u.width)}, {"height", Val::integer(u.height)}, {"bytes", Val::integer(u.bytes)}}));
        out(dumps1(dict_of({{"gif_tags", gif_json(tags)}, {"prims", counts_json(state.prims)}, {"uploads", Val::list(std::move(up))}, {"issues", issues_json(issues)}})));
        return exit_code(issues);
    }
    out("GIF packets from " + hx(dump.addr_of(off)) + cfmt(": %zu tags", tags.size()));
    print_gif(tags, get(a, "max_lines").i, state);
    print_issues(issues);
    return exit_code(issues);
}

inline Val parse_int_arg(const std::string& text) {
    const auto v = console_abdiff::int0(text);
    if (!v) throw PyErr("ArgumentTypeError", "not a number: " + text + " (use 0x... for hex)");
    return Val::integer(*v);
}
inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "Walk DMA chains, VIF1 streams and GIF packets in a PS2 RAM dump, and flag what breaks rendering.");
    p.raw_description = true;
    p.epilog = "examples:\n"
               "  gspacket.py dma ram.bin --tag 0x01F2A000                  # VIF1 chain, TTE on\n"
               "  gspacket.py dma ram.bin --tag 0x00402000 --channel gif\n"
               "  gspacket.py vif ram.bin --addr 0x00403C10 --length 4096\n"
               "  gspacket.py gif packet.bin --json\n"
               "exit codes: 0 no errors, 1 errors found, 2 bad input";
    p.subcommands("cmd", false);
    const auto common = [](Parser& sp, bool region_args) {
        sp.pos("dump").help("raw guest memory dump (a full 32 MB RDRAM dump needs no --base)");
        sp.opt({"--base"}).type(parse_int_arg, "parse_int").def(Val::integer(0)).help("guest address of the dump's first byte (default 0)");
        if (region_args) {
            sp.opt({"--offset"}).type(parse_int_arg, "parse_int").help("start at this file offset");
            sp.opt({"--addr"}).type(parse_int_arg, "parse_int").help("start at this guest address");
            sp.opt({"--length"}).type(parse_int_arg, "parse_int").help("bytes to read (default: to the end)");
        }
        sp.opt({"--max-lines"}).integer().def(Val::integer(400)).help("cap on listed items (default 400)");
        sp.opt({"--json"}).store_true().help("machine-readable output");
    };
    { Parser& d = p.sub("dma"); d.help_line = "walk a DMA source chain from its first tag"; common(d, false);
      d.opt({"--tag"}).type(parse_int_arg, "parse_int").required().help("guest address of the first DMA tag");
      d.opt({"--channel"}).choices({"vif1", "gif"}).def(Val::str("vif1")).help("vif1 (D1, the usual 3D path) or gif (D2, PATH3)");
      d.opt({"--no-tte"}).store_true().help("VIF1: the tag's upper 64 bits are NOT sent (Dn_CHCR.TTE=0)");
      d.opt({"--tie"}).store_true().help("stop at a tag with IRQ set (Dn_CHCR.TIE=1)");
      d.opt({"--max-tags"}).integer().def(Val::integer(10000)).help("give up after this many tags");
      d.opt({"--no-gif"}).store_true().help("do not decode GIF packets inside DIRECT");
      d.opt({"--verbose"}).store_true().help("list every GIF tag and register write"); }
    { Parser& v = p.sub("vif"); v.help_line = "decode a VIF1 stream"; common(v, true);
      v.opt({"--no-gif"}).store_true().help("do not decode GIF packets inside DIRECT");
      v.opt({"--verbose"}).store_true().help("list every GIF tag and register write"); }
    { Parser& g = p.sub("gif"); g.help_line = "decode GIF packets (PATH2/PATH3 data)"; common(g, true); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    return guard(PROG, [&]() -> int {
        if (cmd == "dma") return cmd_dma(a);
        if (cmd == "vif") return cmd_vif(a);
        return cmd_gif(a);
    });
}
PACKTOOL("console-graphics/gspacket", main);
}  // namespace packtools::console_gspacket
