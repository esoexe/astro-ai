// modder_upk.inc - `um upk`: a C++ port of universal-modder's skills/reverse-engineering/references/ue3-packages/
// unpack_upk.py (Rehan and universal-modder contributors, MIT): decompress a fully-compressed, LZO-compressed Unreal
// Engine 3 .upk so UELib can read it. Upstream needed the minilzo C extension through Python; the LZO1X decompressor
// is written here (the safe variant: every read and write is bounds-checked). The follow-up step the reference did
// with a Python snippet (zero ONLY the CompressionFlags dword) is `um upk clear-flags`, or --clear-flags.
//   um upk unpack <in.upk> <out.upk> [--clear-flags]      um upk clear-flags <file> <offset>
#include <tuple>
namespace packtools::modder::upk {
using namespace pyrt;
using namespace packtools::modder;

constexpr uint32_t SRC_TAG = 0x9E2A83C1u;
inline uint32_t u32(const std::string& d, std::size_t at) { return r3d::Gltf::le32(d, at); }
inline int32_t i32(const std::string& d, std::size_t at) { return static_cast<int32_t>(u32(d, at)); }

// LZO1X decompression (lzo1x_decompress_safe semantics). Returns false on corrupt input or output overrun.
inline bool lzo1x_decompress(const std::string& in, std::string& out, std::size_t out_len, std::string& err) {
    out.assign(out_len, '\0');
    const auto* ip = reinterpret_cast<const unsigned char*>(in.data()); const unsigned char* const ip_end = ip + in.size();
    auto* const ob = reinterpret_cast<unsigned char*>(out.data()); unsigned char* op = ob; unsigned char* const op_end = ob + out_len;
    std::size_t t = 0; const unsigned char* m_pos = nullptr;
    auto need_in = [&](std::size_t n) { return static_cast<std::size_t>(ip_end - ip) >= n; };
    auto need_out = [&](std::size_t n) { return static_cast<std::size_t>(op_end - op) >= n; };
    auto copy_lit = [&](std::size_t n) -> bool { if (!need_in(n) || !need_out(n)) return false; std::memcpy(op, ip, n); op += n; ip += n; return true; };
    auto copy_match = [&](std::size_t n) -> bool { if (m_pos < ob || m_pos >= op) return false; if (!need_out(n)) return false; while (n--) *op++ = *m_pos++; return true; };
    auto ext = [&](std::size_t base) -> bool { while (true) { if (!need_in(1)) return false; if (*ip != 0) break; t += 255; ++ip; } t += base + *ip++; return true; };
    enum { OUTER, FIRST_LIT, MATCH, MATCH_DONE, MATCH_NEXT } st = OUTER;
    if (!need_in(1)) { err = "empty input"; return false; }
    if (*ip > 17) {
        t = static_cast<std::size_t>(*ip++ - 17);
        if (t < 4) st = MATCH_NEXT;
        else { if (!copy_lit(t)) { err = "input overrun"; return false; } st = FIRST_LIT; }
    }
    for (;;) {
        switch (st) {
            case OUTER:
                if (!need_in(1)) { err = "input overrun"; return false; }
                t = *ip++;
                if (t >= 16) { st = MATCH; break; }
                if (t == 0 && !ext(15)) { err = "input overrun"; return false; }
                if (!copy_lit(t + 3)) { err = "input/output overrun"; return false; }
                st = FIRST_LIT; break;
            case FIRST_LIT:
                if (!need_in(1)) { err = "input overrun"; return false; }
                t = *ip++;
                if (t >= 16) { st = MATCH; break; }
                if (!need_in(1)) { err = "input overrun"; return false; }
                m_pos = op - (1 + 0x0800); m_pos -= t >> 2; m_pos -= static_cast<std::size_t>(*ip++) << 2;
                if (!copy_match(3)) { err = "lookbehind overrun"; return false; }
                st = MATCH_DONE; break;
            case MATCH:
                if (t >= 64) {
                    if (!need_in(1)) { err = "input overrun"; return false; }
                    m_pos = op - 1; m_pos -= (t >> 2) & 7; m_pos -= static_cast<std::size_t>(*ip++) << 3; t = (t >> 5) - 1;
                } else if (t >= 32) {
                    t &= 31; if (t == 0 && !ext(31)) { err = "input overrun"; return false; }
                    if (!need_in(2)) { err = "input overrun"; return false; }
                    m_pos = op - 1; m_pos -= (ip[0] >> 2) + (static_cast<std::size_t>(ip[1]) << 6); ip += 2;
                } else if (t >= 16) {
                    m_pos = op; m_pos -= (t & 8) << 11; t &= 7;
                    if (t == 0 && !ext(7)) { err = "input overrun"; return false; }
                    if (!need_in(2)) { err = "input overrun"; return false; }
                    m_pos -= (ip[0] >> 2) + (static_cast<std::size_t>(ip[1]) << 6); ip += 2;
                    if (m_pos == op) {   // end of stream
                        if (static_cast<std::size_t>(op - ob) != out_len) { err = "decompressed " + std::to_string(op - ob) + " bytes, expected " + std::to_string(out_len); out.resize(static_cast<std::size_t>(op - ob)); return false; }
                        if (ip != ip_end) { err = ip < ip_end ? "input not consumed" : "input overrun"; return ip < ip_end; }
                        return true;
                    }
                    m_pos -= 0x4000;
                } else {
                    if (!need_in(1)) { err = "input overrun"; return false; }
                    m_pos = op - 1; m_pos -= t >> 2; m_pos -= static_cast<std::size_t>(*ip++) << 2;
                    if (!copy_match(2)) { err = "lookbehind overrun"; return false; }
                    st = MATCH_DONE; break;
                }
                if (!copy_match(t + 2)) { err = "lookbehind or output overrun"; return false; }
                st = MATCH_DONE; break;
            case MATCH_DONE:
                t = ip[-2] & 3u;
                if (t == 0) { st = OUTER; break; }
                st = MATCH_NEXT; break;
            case MATCH_NEXT:
                if (!copy_lit(t)) { err = "input/output overrun"; return false; }
                if (!need_in(1)) { err = "input overrun"; return false; }
                t = *ip++; st = MATCH; break;
        }
    }
}

struct Rec { uint32_t uo, us, co, cs; };
inline std::tuple<std::size_t, uint32_t, std::vector<Rec>> find_chunk_table(const std::string& d) {   // first offset that validates
    for (std::size_t off = 0x40; off < 0x400; ++off) {
        if (off + 8 > d.size()) break;
        const uint32_t flags = u32(d, off); if (flags != 1 && flags != 2 && flags != 4) continue;   // ZLIB / LZO / LZX
        const uint32_t count = u32(d, off + 4); if (!(count > 0 && count < 8192)) continue;
        const std::size_t table = off + 8; if (table + static_cast<std::size_t>(count) * 16 > d.size()) continue;
        std::vector<Rec> recs; for (uint32_t i = 0; i < count; ++i) { const std::size_t at = table + i * 16u; recs.push_back({u32(d, at), u32(d, at + 4), u32(d, at + 8), u32(d, at + 12)}); }
        bool ok = true;
        for (std::size_t i = 0; i + 1 < recs.size(); ++i)   // uncompressed and compressed regions must each be contiguous
            if (static_cast<uint64_t>(recs[i].uo) + recs[i].us != recs[i + 1].uo || static_cast<uint64_t>(recs[i].co) + recs[i].cs != recs[i + 1].co) { ok = false; break; }
        uint64_t sum = recs[0].co; for (const auto& r : recs) sum += r.cs;
        if (ok && sum <= d.size() + 8) return {off, count, recs};
    }
    die("could not locate the compressed chunk table"); return {};
}
inline std::string hex(uint64_t v) { char b[32]; std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v)); return b; }
inline std::string hex8(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%08X", v); return b; }

inline void clear_flags(const std::string& path, std::size_t off) {
    std::string d = slurp(path); if (d.empty() && !isfile(path)) die("no such file: " + path);
    if (off + 4 > d.size()) die("offset 0x" + hex(off) + " is past the end of " + path);
    for (int k = 0; k < 4; ++k) d[off + static_cast<std::size_t>(k)] = '\0';
    spit(path, d); print("cleared CompressionFlags@0x" + hex(off) + " in " + path);
}
inline void unpack(const std::string& src, const std::string& dst, bool clear) {
    if (!isfile(src)) die("no such file: " + src);
    const std::string d = slurp(src);
    if (d.size() < 8) die("not a UE3 package (too short)");
    const uint32_t tag = u32(d, 0); const unsigned ver = static_cast<unsigned char>(d[4]) | (static_cast<unsigned char>(d[5]) << 8), lic = static_cast<unsigned char>(d[6]) | (static_cast<unsigned char>(d[7]) << 8);
    if (tag != SRC_TAG) die("not a UE3 package (tag " + hex8(tag) + ")");
    const auto [off, count, recs] = find_chunk_table(d);
    const uint32_t flags = u32(d, off);
    if (flags != 2) die("CompressionFlags=" + std::to_string(flags) + ": only LZO (2) is handled here; use Gildor's decompress for zlib (1) or LZX (4) packages");
    const uint64_t total = static_cast<uint64_t>(recs.back().uo) + recs.back().us;
    if (total > (uint64_t{4} << 30)) die("uncompressed size " + std::to_string(total) + " is implausible");
    print("version=" + std::to_string(ver) + "/" + std::to_string(lic) + " compressionFlags=" + std::to_string(flags) + "@0x" + hex(off) + " chunks=" + std::to_string(count) +
          " table@0x" + hex(off + 8) + " uncompressed=" + std::to_string(total));
    std::string out(static_cast<std::size_t>(total), '\0');
    const std::size_t first = recs[0].uo; if (first > d.size() || first > out.size()) die("chunk table points past the end of the file");
    std::memcpy(out.data(), d.data(), first);   // uncompressed summary/header region
    for (std::size_t idx = 0; idx < recs.size(); ++idx) {
        const Rec& r = recs[idx]; const std::size_t co = r.co;
        if (co + 16 > d.size()) die("chunk " + std::to_string(idx) + ": header past the end of the file");
        const uint32_t ctag = u32(d, co); int64_t chunk_size = i32(d, co + 4);
        if (ctag != SRC_TAG) die("chunk " + std::to_string(idx) + ": bad tag " + hex8(ctag) + " at 0x" + hex(co));
        if (chunk_size == 0) chunk_size = 0x20000;
        if (chunk_size < 0) die("chunk " + std::to_string(idx) + ": bad block size");
        const int64_t suncomp = i32(d, co + 12);
        const int64_t nblocks = suncomp <= 0 ? 0 : (suncomp + chunk_size - 1) / chunk_size;
        std::size_t pos = co + 16 + static_cast<std::size_t>(nblocks) * 8;
        std::string data;
        for (int64_t b = 0; b < nblocks; ++b) {
            const std::size_t hb = co + 16 + static_cast<std::size_t>(b) * 8; if (hb + 8 > d.size()) die("chunk " + std::to_string(idx) + ": block table past the end of the file");
            const int64_t bc = i32(d, hb), bu = i32(d, hb + 4);
            if (bc < 0 || bu < 0 || pos + static_cast<std::size_t>(bc) > d.size()) die("chunk " + std::to_string(idx) + " block " + std::to_string(b) + ": past the end of the file");
            std::string dec, err;
            const bool ok = lzo1x_decompress(d.substr(pos, static_cast<std::size_t>(bc)), dec, static_cast<std::size_t>(bu), err);
            if (dec.size() != static_cast<std::size_t>(bu)) die("chunk " + std::to_string(idx) + " block " + std::to_string(b) + ": got " + std::to_string(dec.size()) + " expected " + std::to_string(bu));
            if (!ok) die("chunk " + std::to_string(idx) + " block " + std::to_string(b) + ": LZO error: " + err);
            data += dec; pos += static_cast<std::size_t>(bc);
        }
        if (data.size() != r.us) die("chunk " + std::to_string(idx) + ": decompressed " + std::to_string(data.size()) + " expected " + std::to_string(r.us));
        if (static_cast<uint64_t>(r.uo) + r.us > out.size()) die("chunk " + std::to_string(idx) + ": past the end of the output");
        std::memcpy(out.data() + r.uo, data.data(), data.size());
    }
    spit(dst, out);
    print("wrote " + dst + " " + std::to_string(out.size()) + " (set CompressionFlags@0x" + hex(off) + " to 0 for UELib" + (clear ? ": done, --clear-flags)" : ": `um upk clear-flags " + dst + " 0x" + hex(off) + "`)"));
    if (clear) clear_flags(dst, off);
}

inline const char* DOC =
    "Decompress a fully-compressed, LZO-compressed Unreal Engine 3 .upk (reverse-engineering/references/ue3-packages).\n\n"
    "    um upk unpack DishonoredGame.upk DishonoredGame.fixed.upk            # prints the CompressionFlags offset\n"
    "    um upk clear-flags DishonoredGame.fixed.upk 0x79                     # then UELib's Deserialize reads the exports\n"
    "    um upk unpack in.upk out.upk --clear-flags                           # both at once\n\n"
    "Only LZO (CompressionFlags 2) is handled; for zlib (1) or LZX (4) use Gildor's Unreal Package Decompressor, which\n"
    "also writes a package other tools load (this output keeps the summary and chunk table: input for UELib).\n"
    "Work on copies outside the game folder.\n";
inline void reg(Parser& sub) {
    Parser& p = sub.sub("upk", DOC); p.help_line = "Unreal Engine 3: decompress LZO .upk packages for UELib"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("unpack"); q.help_line = "decompress an LZO-compressed .upk"; q.pos("src"); q.pos("dst"); q.opt({"--clear-flags"}).store_true().help("also zero the CompressionFlags dword in the output"); }
    { Parser& q = p.sub("clear-flags"); q.help_line = "zero ONLY the CompressionFlags dword at an offset (keeps the chunk table)"; q.pos("file"); q.pos("offset"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "unpack") unpack(S(a, "src"), S(a, "dst"), truthy(get(a, "clear_flags")));
    else if (c == "clear-flags") {
        const std::string o = lower(S(a, "offset")); std::size_t used = 0; unsigned long long v = 0;
        try { v = std::stoull(o, &used, 0); } catch (...) { used = 0; }
        if (!used || used != o.size()) die("bad offset: " + S(a, "offset") + " (decimal or 0x hex)");
        clear_flags(S(a, "file"), static_cast<std::size_t>(v));
    }
    return 0;
}
}  // namespace packtools::modder::upk
