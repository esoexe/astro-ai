// console_vag.inc - console-sound/vag: a C++ port of console-skill/console-sound/vag.py (Jake Schincariol, MIT).
// PS2 SPU2 ADPCM (VAG) audio: decode VAG files (and raw ADPCM from sound banks) to WAV, encode a 16-bit WAV back to
// VAG (closed-loop filter/shift search), find VAGs inside bigger files (streamed) and split headerless .bd banks.
// The decoder follows PS2Recomp's ps2_audio_vag.cpp exactly: C integer division, shift 13..15 played as 9, filter
// 5..7 as 0. Exit codes: 0 ok, 1 needs attention, 2 bad input ("error: <message>").
#include <fstream>

namespace packtools::console_vag {
using namespace pyrt;
using namespace packtools::console_common;

const char* const PROG = "vag";
constexpr int FILTERS[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
constexpr unsigned FLAG_END = 0x01, FLAG_REPEAT = 0x02, FLAG_LOOP_START = 0x04, FLAG_MARKER = 0x07;
constexpr long long HEADER_SIZE = 0x30, FRAME_BYTES = 16, SPF = 28, HEADER_SLACK = 0x30;   // SPF: samples per frame
constexpr long long DEFAULT_VERSION = 0x20;
constexpr long long SCAN_CHUNK = 4LL << 20;

[[noreturn]] inline void fail(const std::string& m) { throw InputError(m); }
inline unsigned b8(const std::string& d, long long i) { return static_cast<unsigned char>(d[static_cast<std::size_t>(i)]); }
inline int nib(unsigned n) { return (n & 8) ? static_cast<int>(n) - 16 : static_cast<int>(n); }
inline int cdiv64(long long t) { return static_cast<int>(t >= 0 ? t >> 6 : -((-t) >> 6)); }   // C division by 64
inline int clamp16(long long v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : static_cast<int>(v); }
inline std::string px(long long v) { return v < 0 ? cfmt("0x-%llx", 0ULL - static_cast<unsigned long long>(v)) : cfmt("0x%llx", static_cast<unsigned long long>(v)); }   // "0x%x" % v
inline uint32_t be32(const std::string& d, long long o) { return b8(d, o) << 24 | b8(d, o + 1) << 16 | b8(d, o + 2) << 8 | b8(d, o + 3); }
inline std::string be32s(uint32_t v) { std::string s(4, '\0'); for (int k = 0; k < 4; ++k) s[static_cast<std::size_t>(k)] = static_cast<char>((v >> (24 - 8 * k)) & 0xFF); return s; }
inline std::string le32s(uint32_t v) { std::string s(4, '\0'); for (int k = 0; k < 4; ++k) s[static_cast<std::size_t>(k)] = static_cast<char>((v >> (8 * k)) & 0xFF); return s; }
inline std::string le16s(uint32_t v) { std::string s(2, '\0'); s[0] = static_cast<char>(v & 0xFF); s[1] = static_cast<char>((v >> 8) & 0xFF); return s; }
// struct.pack("I") range check, as Python raises it (an uncaught error: exit 1)
inline uint32_t pack_u32(long long v) { if (v < 0 || v > 0xFFFFFFFFLL) throw PyErr("struct.error", "'I' format requires 0 <= number <= 4294967295"); return static_cast<uint32_t>(v); }
inline double secs(long long samples, long long rate) { return rate ? static_cast<double>(samples) / static_cast<double>(rate) : 0.0; }
inline bool any_nonzero(const std::string& d, long long a, long long b) { for (long long i = a; i < b; ++i) if (d[static_cast<std::size_t>(i)]) return true; return false; }
inline Val opt_int(long long v) { return v < 0 ? Val::none() : Val::integer(v); }   // -1 stands for None
inline std::string sep() { return ossep(); }

// ---------------------------------------------------------------- decoding
struct DecInfo { long long frames = 0, loop_start = -1, loop_end = -1, bad = 0, bytes_used = 0; std::string end = "data-end"; };

inline std::vector<int> decode_adpcm(const std::string& data, long long start, long long end, bool stop_at_end, DecInfo& info) {
    const long long n = static_cast<long long>(data.size());
    if (end < 0 || end > n) end = n;
    std::vector<int> out;
    if (end > start) out.reserve(static_cast<std::size_t>((end - start) / FRAME_BYTES * SPF));
    int s1 = 0, s2 = 0;
    info = DecInfo();
    long long pos = start;
    while (pos + FRAME_BYTES <= end) {
        const unsigned b0 = b8(data, pos), flags = b8(data, pos + 1);
        if (stop_at_end && (flags & FLAG_MARKER) == FLAG_MARKER) { info.end = "marker"; break; }
        unsigned shift = b0 & 0x0F, filt = (b0 >> 4) & 0x07;
        bool odd = false;
        if (shift > 12) { shift = 9; odd = true; }
        if (filt > 4) { filt = 0; odd = true; }
        if (b0 & 0x80) odd = true;
        if (odd) ++info.bad;
        if ((flags & FLAG_LOOP_START) && info.loop_start < 0) info.loop_start = static_cast<long long>(out.size());
        const int mul = 1 << (12 - shift);
        if (filt == 0) {
            for (long long i = pos + 2; i < pos + FRAME_BYTES; ++i) { const unsigned b = b8(data, i); out.push_back(nib(b & 0x0F) * mul); out.push_back(nib(b >> 4) * mul); }
            s1 = out[out.size() - 1]; s2 = out[out.size() - 2];
        } else {
            const int k1 = FILTERS[filt][0], k2 = FILTERS[filt][1];
            for (long long i = pos + 2; i < pos + FRAME_BYTES; ++i) {
                const unsigned b = b8(data, i);
                int v = clamp16(static_cast<long long>(nib(b & 0x0F)) * mul + cdiv64(static_cast<long long>(k1) * s1 + static_cast<long long>(k2) * s2 + 32));
                s2 = s1; s1 = v;
                v = clamp16(static_cast<long long>(nib(b >> 4)) * mul + cdiv64(static_cast<long long>(k1) * s1 + static_cast<long long>(k2) * s2 + 32));
                s2 = s1; s1 = v;
                out.push_back(s2); out.push_back(s1);
            }
        }
        ++info.frames;
        pos += FRAME_BYTES;
        if (flags & FLAG_END) {
            if (flags & FLAG_REPEAT) info.loop_end = static_cast<long long>(out.size());
            if (stop_at_end) { info.end = "end-flag"; break; }
        }
    }
    info.bytes_used = pos - start;
    return out;
}

struct FrameStats { long long frames = 0, bad = 0, loop_start = -1, loop_end = -1, end_flag = -1, marker = -1; bool lead_zero = false; };
inline FrameStats frame_stats(const std::string& data, long long start, long long end) {
    const long long n = static_cast<long long>(data.size());
    if (end < 0 || end > n) end = n;
    FrameStats st;
    st.lead_zero = end - start >= FRAME_BYTES && !any_nonzero(data, start, start + FRAME_BYTES);
    long long pos = start;
    while (pos + FRAME_BYTES <= end) {
        const unsigned b0 = b8(data, pos), flags = b8(data, pos + 1);
        if ((flags & FLAG_MARKER) == FLAG_MARKER) { st.marker = st.frames; break; }
        if ((b0 & 0x0F) > 12 || ((b0 >> 4) & 0x07) > 4 || (b0 & 0x80)) ++st.bad;
        if ((flags & FLAG_LOOP_START) && st.loop_start < 0) st.loop_start = st.frames;
        if (flags & FLAG_END) {
            st.end_flag = st.frames;
            if (flags & FLAG_REPEAT) st.loop_end = st.frames;
            ++st.frames;
            break;
        }
        ++st.frames;
        pos += FRAME_BYTES;
    }
    return st;
}

// ---------------------------------------------------------------- encoding
// closed-loop encode of one frame with one filter and shift; false as soon as the squared error reaches limit
inline bool try_block(const int* x, int s1, int s2, int k1, int k2, int shift, long long limit, long long& err, int* nibs, int& o1, int& o2) {
    const int sh = 12 - shift, step = 1 << sh, half = step >> 1;
    err = 0;
    for (int k = 0; k < SPF; ++k) {
        const long long v = x[k];
        const long long pred = cdiv64(static_cast<long long>(k1) * s1 + static_cast<long long>(k2) * s2 + 32);
        const long long r = v - pred;
        long long q;
        if (r >= 0) { q = (r + half) >> sh; if (q > 7) q = 7; }
        else { q = -((half - r) >> sh); if (q < -8) q = -8; }
        const int y = clamp16(q * step + pred);
        const long long d = v - y;
        err += d * d;
        if (err >= limit) return false;
        nibs[k] = static_cast<int>(q & 0x0F);
        s2 = s1; s1 = y;
    }
    o1 = s1; o2 = s2;
    return true;
}

struct Block { unsigned hdr = 0; int nibs[28] = {}; int s1 = 0, s2 = 0; long long err = 0; };
inline Block encode_block(const int* x, int s1, int s2, bool exhaustive) {
    Block best;
    bool nonzero = false;
    for (int k = 0; k < SPF; ++k) if (x[k]) { nonzero = true; break; }
    if (!nonzero) return best;   // 0x00, silent nibbles, s1 = s2 = 0
    struct Est { long long mx; int f, sh; };
    std::vector<Est> ests;
    for (int f = 0; f < 5; ++f) {
        const int k1 = FILTERS[f][0], k2 = FILTERS[f][1];
        long long p1 = s1, p2 = s2, mx = 0;
        for (int k = 0; k < SPF; ++k) {
            long long r = x[k] - cdiv64(k1 * p1 + k2 * p2 + 32);
            if (r < 0) r = -r;
            if (r > mx) mx = r;
            p2 = p1; p1 = x[k];
        }
        int sh = 12;
        while (sh > 0 && ((15LL << (12 - sh)) >> 1) < mx) --sh;   // 7.5 steps
        ests.push_back({mx, f, sh});
    }
    std::sort(ests.begin(), ests.end(), [](const Est& a, const Est& b) { return a.mx != b.mx ? a.mx < b.mx : a.f != b.f ? a.f < b.f : a.sh < b.sh; });
    long long best_err = 1LL << 62;
    bool found = false;
    for (const Est& e : ests) {
        std::vector<int> shifts;
        if (exhaustive) {
            for (int s = 0; s <= 12; ++s) shifts.push_back(s);
            std::stable_sort(shifts.begin(), shifts.end(), [&](int a, int b) { return std::abs(a - e.sh) < std::abs(b - e.sh); });
        } else {
            for (const int s : {e.sh, e.sh + 1, e.sh - 1}) if (s >= 0 && s <= 12) shifts.push_back(s);
        }
        for (const int s : shifts) {
            long long err = 0; int nibs[28]; int o1 = 0, o2 = 0;
            if (try_block(x, s1, s2, FILTERS[e.f][0], FILTERS[e.f][1], s, best_err, err, nibs, o1, o2) && err < best_err) {
                best_err = err; found = true;
                best.hdr = static_cast<unsigned>(e.f << 4 | s); std::copy(nibs, nibs + 28, best.nibs); best.s1 = o1; best.s2 = o2; best.err = err;
            }
        }
    }
    (void)found;
    return best;
}

inline std::string encode_adpcm(const std::vector<int>& samples, long long loop_start, bool lead_frame, bool end_marker, bool exhaustive) {
    const long long n = static_cast<long long>(samples.size());
    const long long nframes = std::max(1LL, (n + SPF - 1) / SPF);
    long long lsf = -1;
    if (loop_start >= 0) {
        if (loop_start % SPF) throw PyErr("ValueError", "loop start must be a multiple of 28 samples");
        lsf = std::min(loop_start / SPF, nframes - 1);
    }
    std::string out;
    out.reserve(static_cast<std::size_t>((nframes + 2) * FRAME_BYTES));
    if (lead_frame) out.append(static_cast<std::size_t>(FRAME_BYTES), '\0');
    int s1 = 0, s2 = 0;
    int x[28];
    for (long long fi = 0; fi < nframes; ++fi) {
        for (long long k = 0; k < SPF; ++k) { const long long i = fi * SPF + k; x[k] = i < n ? samples[static_cast<std::size_t>(i)] : 0; }
        const Block b = encode_block(x, s1, s2, exhaustive);
        s1 = b.s1; s2 = b.s2;
        const bool last = fi == nframes - 1;
        unsigned flags;
        if (lsf < 0) flags = last ? FLAG_END : 0;
        else if (fi < lsf) flags = 0;
        else { flags = FLAG_REPEAT; if (fi == lsf) flags |= FLAG_LOOP_START; if (last) flags |= FLAG_END; }
        out += static_cast<char>(b.hdr);
        out += static_cast<char>(flags);
        for (int i = 0; i < SPF; i += 2) out += static_cast<char>(b.nibs[i] | (b.nibs[i + 1] << 4));
    }
    if (lsf < 0 && end_marker) { out += '\0'; out += static_cast<char>(FLAG_MARKER); out.append(14, '\0'); }   // silent end-of-data marker
    return out;
}

// pad the front with silence so the loop start lands on a frame: (samples, loop_start, pad)
inline long long align_loop(std::vector<int>& samples, long long loop_start) {
    const long long pad = ((-loop_start) % SPF + SPF) % SPF;
    if (pad) samples.insert(samples.begin(), static_cast<std::size_t>(pad), 0);
    return pad;
}

// ---------------------------------------------------------------- headers
struct Header { bool ok = false; std::string magic, name; long long version = 0, reserved = 0, data_size = 0, rate = 0; };
inline Header parse_header(const std::string& buf, long long off) {
    Header h;
    if (off + HEADER_SIZE > static_cast<long long>(buf.size())) return h;
    const std::string m = buf.substr(static_cast<std::size_t>(off), 4);
    if (m != "VAGp" && m != "VAGi") return h;
    h.ok = true; h.magic = m;
    h.version = be32(buf, off + 4); h.reserved = be32(buf, off + 8); h.data_size = be32(buf, off + 12); h.rate = be32(buf, off + 16);
    std::string nm = buf.substr(static_cast<std::size_t>(off + 0x20), 16);
    const auto z = nm.find('\0');
    if (z != std::string::npos) nm.resize(z);
    h.name = latin1(nm);
    return h;
}
inline Val header_val(const Header& h) {
    return dict_of({{"magic", Val::str(h.magic)}, {"version", Val::integer(h.version)}, {"reserved", Val::integer(h.reserved)},
                    {"data_size", Val::integer(h.data_size)}, {"rate", Val::integer(h.rate)}, {"name", Val::str(h.name)}});
}
inline std::string make_header(long long data_size, long long rate, const std::string& name, long long version, const std::string& magic) {
    std::string h(static_cast<std::size_t>(HEADER_SIZE), '\0');
    h.replace(0, 4, magic.substr(0, 4));
    h.replace(4, 4, be32s(pack_u32(version)));
    h.replace(0x0C, 4, be32s(pack_u32(data_size)));
    h.replace(0x10, 4, be32s(pack_u32(rate)));
    std::string nm;   // name.encode("ascii", "replace")[:16]
    for (const auto& ch : cps(name)) { const uint32_t c = cp_of(ch); nm += c < 0x80 ? static_cast<char>(c) : '?'; if (nm.size() >= 16) break; }
    h.replace(0x20, nm.size(), nm);
    return h;
}
inline std::string header_name(const std::string& path) {
    const std::string stem = console_tim2::splitext0(basename(path));
    std::string o; std::size_t k = 0;
    for (const auto& ch : cps(stem)) { if (k++ >= 16) break; const uint32_t c = cp_of(ch); o += (c > 32 && c < 127) ? static_cast<char>(c) : '_'; }
    return o;
}
struct Range { long long start, end; Val note; };
inline Range data_range(const std::string& buf, long long off, const Header& h) {
    const long long start = off + HEADER_SIZE, avail = static_cast<long long>(buf.size()) - start, size = h.data_size;
    if (size == 0) return {start, static_cast<long long>(buf.size()), Val::none()};
    if (size > avail) {
        if (size - avail <= HEADER_SLACK) return {start, static_cast<long long>(buf.size()), Val::str(cfmt("the header's data size counts the header too; using the %lld bytes present", avail))};
        fail(cfmt("truncated VAG: the header says %lld bytes of data but only %lld follow", size, std::max(avail, 0LL)));
    }
    return {start, start + size, Val::none()};
}

// ---------------------------------------------------------------- WAV
struct Wav { long long rate = 0; std::vector<std::vector<int>> chans; bool has_loop = false; long long loop_a = 0, loop_b = 0; };
inline Wav read_wav(const std::string& path) {
    std::string data;
    try { data = read_bytes(path); } catch (const OSError& e) { fail("cannot read " + path + ": " + strerror_of(e.err)); }
    const long long n = static_cast<long long>(data.size());
    if (n < 12 || data.compare(0, 4, "RIFF") != 0 || data.compare(8, 4, "WAVE") != 0) fail(path + " is not a RIFF/WAVE file");
    long long pos = 12;
    bool have_fmt = false, have_pcm = false;
    unsigned tag = 0, ch = 0, bits = 0; long long rate = 0;
    std::string pcm; Wav w;
    while (pos + 8 <= n) {
        const std::string cid = data.substr(static_cast<std::size_t>(pos), 4);
        const long long size = u32le(data, static_cast<std::size_t>(pos + 4));
        const std::string body = data.substr(static_cast<std::size_t>(pos + 8), static_cast<std::size_t>(std::min(size, n - (pos + 8))));
        const long long bl = static_cast<long long>(body.size());
        if (bl < size && cid != "data") fail(path + " is truncated inside its " + str_repr(latin1(cid)) + " chunk");
        if (cid == "fmt ") {
            if (bl < 16) fail(path + " has a short fmt chunk");
            tag = u16le(body, 0); ch = u16le(body, 2); rate = u32le(body, 4); bits = u16le(body, 14);
            if (tag == 0xFFFE && bl >= 26) tag = u16le(body, 24);
            have_fmt = true;
        } else if (cid == "data") {
            pcm = body; have_pcm = true;
        } else if (cid == "smpl" && bl >= 36) {
            const long long nloops = u32le(body, 28);
            if (nloops && bl >= 36 + 24) { w.has_loop = true; w.loop_a = u32le(body, 44); w.loop_b = static_cast<long long>(u32le(body, 48)) + 1; }
        }
        pos += 8 + size + (size & 1);
    }
    if (!have_fmt || !have_pcm) fail(path + " has no fmt or data chunk");
    if (tag != 1 || bits != 16) fail(cfmt("%s is not 16-bit PCM (format %u, %u-bit): export it as signed 16-bit PCM WAV first", path.c_str(), tag, bits));
    if (ch != 1 && ch != 2) fail(cfmt("%s has %u channels: only mono or stereo", path.c_str(), ch));
    if (rate <= 0) fail(path + " has sample rate 0");
    const std::size_t frame = 2 * ch, usable = pcm.size() - pcm.size() % frame;
    w.rate = rate;
    w.chans.assign(ch, {});
    for (auto& c : w.chans) c.reserve(usable / frame);
    for (std::size_t i = 0; i < usable; i += 2) {
        const int v = static_cast<int16_t>(static_cast<uint16_t>(u16le(pcm, i)));
        w.chans[(i / 2) % ch].push_back(v);
    }
    return w;
}

inline void ensure_parent(const std::string& path) { makedirs(dirname(abspath(path))); }

inline void write_wav(const std::string& path, const std::vector<std::vector<int>>& chans, long long rate, bool has_loop, long long la, long long lb) {
    const long long ch = static_cast<long long>(chans.size());
    std::size_t n = 0;
    for (const auto& c : chans) n = std::max(n, c.size());
    std::string pcm(static_cast<std::size_t>(2 * ch) * n, '\0');
    for (long long i = 0; i < ch; ++i) {
        const auto& c = chans[static_cast<std::size_t>(i)];
        for (std::size_t k = 0; k < c.size(); ++k) {
            const auto v = static_cast<uint16_t>(static_cast<int16_t>(c[k]));
            const std::size_t at = (k * static_cast<std::size_t>(ch) + static_cast<std::size_t>(i)) * 2;
            pcm[at] = static_cast<char>(v & 0xFF); pcm[at + 1] = static_cast<char>(v >> 8);
        }
    }
    std::string fmt = "fmt " + le32s(16) + le16s(1) + le16s(static_cast<uint32_t>(ch)) + le32s(pack_u32(rate)) + le32s(pack_u32(rate * ch * 2)) + le16s(static_cast<uint32_t>(ch * 2)) + le16s(16);
    std::string chunks = fmt;
    if (has_loop && lb > la) {
        if (rate == 0) throw PyErr("ZeroDivisionError", "float division by zero");
        const long long period = static_cast<long long>(std::nearbyint(1e9 / static_cast<double>(rate)));
        std::string smpl = le32s(0) + le32s(0) + le32s(pack_u32(period)) + le32s(60) + le32s(0) + le32s(0) + le32s(0) + le32s(1) + le32s(0);
        smpl += le32s(0) + le32s(0) + le32s(pack_u32(la)) + le32s(pack_u32(lb - 1)) + le32s(0) + le32s(0);
        chunks += "smpl" + le32s(static_cast<uint32_t>(smpl.size())) + smpl;
    }
    chunks += "data" + le32s(pack_u32(static_cast<long long>(pcm.size()))) + pcm + ((pcm.size() & 1) ? std::string(1, '\0') : std::string());
    const std::string body = "WAVE" + chunks;
    ensure_parent(path);
    write_text(path, "RIFF" + le32s(pack_u32(static_cast<long long>(body.size()))) + body);
}

// linear-interpolation resample (simple; resample in an editor for best quality)
inline std::vector<int> resample(const std::vector<int>& x, long long src, long long dst) {
    if (src == dst || x.empty()) return x;
    const long long n_out = std::max(1LL, static_cast<long long>(std::nearbyint(static_cast<double>(static_cast<long long>(x.size()) * dst) / static_cast<double>(src))));
    const double ratio = static_cast<double>(src) / static_cast<double>(dst);
    const long long last = static_cast<long long>(x.size()) - 1;
    std::vector<int> out; out.reserve(static_cast<std::size_t>(n_out));
    for (long long i = 0; i < n_out; ++i) {
        const double p = static_cast<double>(i) * ratio;
        const long long j = static_cast<long long>(p);
        if (j >= last) { out.push_back(x[static_cast<std::size_t>(last)]); continue; }
        const double fr = p - static_cast<double>(j);
        const double a = x[static_cast<std::size_t>(j)], b = x[static_cast<std::size_t>(j + 1)];
        out.push_back(static_cast<int>(std::nearbyint(a + (b - a) * fr)));
    }
    return out;
}

inline double snr_db(const std::vector<int>& ref, const std::vector<int>& test, std::size_t t0 = 0, std::size_t tlen = SIZE_MAX) {
    const std::size_t avail = t0 < test.size() ? test.size() - t0 : 0;
    const std::size_t n = std::min({ref.size(), avail, tlen});
    long long sig = 0, noise = 0;
    for (std::size_t i = 0; i < n; ++i) { const long long r = ref[i], d = r - test[t0 + i]; sig += r * r; noise += d * d; }
    if (noise == 0) return HUGE_VAL;
    if (sig == 0) return 0.0;
    return 10.0 * std::log10(static_cast<double>(sig) / static_cast<double>(noise));
}

// ---------------------------------------------------------------- helpers
inline std::string read_input(const std::string& path) {   // _read
    try { return read_bytes(path); }
    catch (const OSError& e) {
        if (e.err == EISDIR) fail(path + " is a folder, not a file");
        fail("cannot read " + path + ": " + strerror_of(e.err));
    }
}
inline Val parse_int_arg(const std::string& text) {
    const auto v = console_abdiff::int0(text);
    if (!v) throw PyErr("ArgumentTypeError", "not a number: " + str_repr(text));
    return Val::integer(*v);
}
inline std::vector<std::string> interleave_split(const std::string& data, long long start, long long end, int channels, long long il) {
    std::vector<std::string> ch(static_cast<std::size_t>(channels));
    long long pos = start, i = 0;
    while (pos < end) {
        const long long b = std::min(pos + il, end);
        if (b > pos) ch[static_cast<std::size_t>(i % channels)].append(data, static_cast<std::size_t>(pos), static_cast<std::size_t>(b - pos));
        pos += il; ++i;
    }
    return ch;
}
inline std::string interleave_join(const std::vector<std::string>& ch, long long il) {
    long long longest = 0;
    for (const auto& c : ch) longest = std::max(longest, static_cast<long long>(c.size()));
    const long long q = -longest / il, r = -longest % il;          // -(-longest // il), floor division
    const long long blocks = -((r != 0 && ((r < 0) != (il < 0))) ? q - 1 : q);
    std::string out;
    for (long long b = 0; b < blocks; ++b) {
        for (const auto& c : ch) {
            const long long a = std::min(b * il, static_cast<long long>(c.size())), e = std::min((b + 1) * il, static_cast<long long>(c.size()));
            const std::string piece = e > a ? c.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(e - a)) : std::string();
            out += piece; out.append(static_cast<std::size_t>(il - static_cast<long long>(piece.size())), '\0');
        }
    }
    return out;
}

struct Source { Header hdr; long long start, end, rate; Val note; };
inline Source source_of(const std::string& buf, long long offset, long long rate) {
    if (offset < 0 || offset >= static_cast<long long>(buf.size())) fail("offset " + px(offset) + cfmt(" is outside the file (%lld bytes)", static_cast<long long>(buf.size())));
    const Header h = parse_header(buf, offset);
    if (h.ok) {
        Range r = data_range(buf, offset, h);
        long long rr = rate ? rate : h.rate;
        if (!rr) {
            rr = 44100;
            r.note = Val::str((r.note.is_none() ? std::string() : r.note.s + "; ") + "header rate is 0, assuming 44100 Hz (the runtime does the same)");
        }
        return {h, r.start, r.end, rr, r.note};
    }
    if (buf.compare(static_cast<std::size_t>(offset), 4, "pGAV") == 0) fail("byte-swapped VAG header ('pGAV'): not a PS2 layout this tool reads");
    if (!rate) fail("no VAGp header at offset " + px(offset) + ": for raw ADPCM pass --rate N (the sample rate)");
    return {h, offset, static_cast<long long>(buf.size()), rate, Val::none()};
}
inline long long I(const Val& a, const char* k) { const Val v = get(a, k); return v.is_none() ? 0 : v.i; }
inline std::string default_out(const std::string& file, const std::string& ext) {
    return std::string("console") + sep() + "audio" + sep() + console_tim2::splitext0(basename(file)) + ext;
}

// ---------------------------------------------------------------- commands
inline int cmd_info(const Val& a) {
    const std::string file = S(a, "file");
    const std::string buf = read_input(file);
    const long long offset = I(a, "offset");
    const Source src = source_of(buf, offset, I(a, "rate"));
    const FrameStats st = frame_stats(buf, src.start, src.end);
    const long long samples = st.frames * SPF;
    const double seconds = round_n(secs(samples, src.rate), 3);
    const Val res = dict_of({{"file", Val::str(file)}, {"offset", Val::integer(offset)}, {"header", src.hdr.ok ? header_val(src.hdr) : Val::none()},
                             {"rate", Val::integer(src.rate)}, {"data_bytes", Val::integer(src.end - src.start)}, {"frames", Val::integer(st.frames)},
                             {"samples", Val::integer(samples)}, {"seconds", Val::flt(seconds)}, {"lead_zero_frame", Val::boolean(st.lead_zero)},
                             {"loop_start_sample", st.loop_start < 0 ? Val::none() : Val::integer(st.loop_start * SPF)},
                             {"loops", Val::boolean(st.loop_end >= 0)}, {"end_flag_frame", opt_int(st.end_flag)}, {"end_marker_frame", opt_int(st.marker)},
                             {"bad_frames", Val::integer(st.bad)}, {"note", src.note}});
    if (B(a, "json")) { out(dumps2(res)); }
    else {
        if (src.hdr.ok) {
            out(file + ": " + src.hdr.magic + " header, version " + px(src.hdr.version) + ", name " + str_repr(src.hdr.name));
            out(cfmt("  data size   %lld bytes (header field %lld)", src.end - src.start, src.hdr.data_size));
        } else {
            out(file + ": raw ADPCM from offset " + px(offset) + " (no header)");
        }
        out(cfmt("  sample rate %lld Hz", src.rate));
        out(cfmt("  frames      %lld (%lld samples, %.3f s, up to the end flag)", st.frames, samples, seconds));
        if (st.loop_end >= 0) out("  loop        from sample " + (st.loop_start < 0 ? std::string("None") : std::to_string(st.loop_start * SPF)) + " to the end");
        else out("  loop        none (one-shot)");
        out(std::string("  lead frame  ") + (st.lead_zero ? "silent first frame (Sony convention)" : "no"));
        if (st.bad) out(cfmt("  WARNING: %lld frames have an out-of-range filter or shift: this may not be ADPCM at this offset", st.bad));
        if (!src.note.is_none()) out("  note: " + src.note.s);
    }
    return st.bad ? 1 : 0;
}

inline int cmd_towav(const Val& a) {
    const std::string file = S(a, "file");
    const std::string buf = read_input(file);
    const Source src = source_of(buf, I(a, "offset"), I(a, "rate"));
    const long long channels = I(a, "channels"), il = I(a, "interleave");
    const bool all = B(a, "all");
    if (channels != 1 && channels != 2) fail("--channels must be 1 or 2");
    std::vector<std::vector<int>> chans;
    DecInfo info;
    if (channels == 2) {
        if (!il) fail("stereo needs --interleave N (bytes per channel block, a multiple of 16)");
        if (il % 16 || il < 0) fail("--interleave must be a multiple of 16");   // a negative block would never end upstream
        const auto parts = interleave_split(buf, src.start, src.end, 2, il);
        std::vector<DecInfo> infos;
        for (const auto& p : parts) { DecInfo inf; chans.push_back(decode_adpcm(p, 0, -1, !all, inf)); infos.push_back(inf); }
        info = infos[0];
    } else {
        chans.push_back(decode_adpcm(buf, src.start, src.end, !all, info));
    }
    const std::string outp = S(a, "out").empty() ? default_out(file, ".wav") : S(a, "out");
    const bool loop = info.loop_start >= 0 && info.loop_end >= 0 && !B(a, "no_loop_chunk");
    write_wav(outp, chans, src.rate, loop, info.loop_start, info.loop_end);
    std::size_t n = 0;
    for (const auto& c : chans) n = std::max(n, c.size());
    out(cfmt("wrote %s: %lld Hz, %s, %zu samples (%.3f s)", outp.c_str(), src.rate, chans.size() == 2 ? "stereo" : "mono", n, secs(static_cast<long long>(n), src.rate)) +
        (loop ? cfmt(", loop %lld..%lld in a smpl chunk", info.loop_start, info.loop_end) : std::string()));
    if (!src.note.is_none()) out("note: " + src.note.s);
    if (info.bad) { out(cfmt("WARNING: %lld frames had an out-of-range filter or shift; check the offset and format", info.bad)); return 1; }
    return 0;
}

struct Like { Header hdr; bool loops = false; long long bytes = 0; };
inline Like like_of(const std::string& path) {
    const std::string buf = read_input(path);
    Like l; l.hdr = parse_header(buf, 0);
    if (!l.hdr.ok) fail("--like " + path + " has no VAGp header");
    const Range r = data_range(buf, 0, l.hdr);
    const FrameStats st = frame_stats(buf, r.start, r.end);
    l.loops = st.loop_end >= 0; l.bytes = r.end - r.start;
    return l;
}

inline int cmd_fromwav(const Val& a) {
    const std::string wavp = S(a, "wav");
    Wav w = read_wav(wavp);
    auto& chans = w.chans;
    if (chans[0].empty()) fail(wavp + " has no audio samples");
    const bool has_like = !S(a, "like").empty();   // args.like is truthy
    Like like;
    if (has_like) like = like_of(S(a, "like"));
    const long long rate = w.rate;
    const long long target_rate = I(a, "rate") ? I(a, "rate") : (has_like && like.hdr.rate ? like.hdr.rate : rate);
    std::vector<std::string> notes;
    if (B(a, "mono") && chans.size() == 2) {
        std::vector<int> m; const std::size_t n = std::min(chans[0].size(), chans[1].size()); m.reserve(n);
        for (std::size_t i = 0; i < n; ++i) m.push_back((chans[0][i] + chans[1][i]) >> 1);   // floor division
        chans = {m};
        notes.push_back("mixed stereo down to mono");
    }
    bool wav_loop = w.has_loop; long long wl0 = w.loop_a, wl1 = w.loop_b;
    if (target_rate != rate) {
        for (auto& c : chans) c = resample(c, rate, target_rate);
        if (wav_loop) {
            wl0 = static_cast<long long>(std::nearbyint(static_cast<double>(wl0 * target_rate) / static_cast<double>(rate)));
            wl1 = static_cast<long long>(std::nearbyint(static_cast<double>(wl1 * target_rate) / static_cast<double>(rate)));
        }
        notes.push_back(cfmt("resampled %lld Hz to %lld Hz (linear; for best quality resample in an audio editor)", rate, target_rate));
    }
    long long loop_start = -1; bool looping = false;
    if (!B(a, "no_loop")) {
        if (!get(a, "loop_start").is_none()) { loop_start = get(a, "loop_start").i; looping = true; }
        else if (wav_loop) {
            loop_start = wl0; looping = true;
            const long long n = static_cast<long long>(chans[0].size());
            if (wl1 < n) {
                for (auto& c : chans) c.resize(static_cast<std::size_t>(std::max(0LL, wl1)));
                notes.push_back(cfmt("trimmed after the WAV's loop end (sample %lld): a VAG loops to its last frame", wl1));
            }
            notes.push_back("loop taken from the WAV's smpl chunk");
        } else if (B(a, "loop") || (has_like && like.loops)) {
            loop_start = 0; looping = true;
            if (!B(a, "loop")) notes.push_back("loops because the original (--like) loops");
        }
    }
    if (looping) {
        const long long n0 = static_cast<long long>(chans[0].size());
        if (loop_start < 0 || loop_start >= std::max(1LL, n0)) fail(cfmt("loop start %lld is outside the sound (%lld samples)", loop_start, n0));
        long long pad = 0;
        for (auto& c : chans) pad = align_loop(c, loop_start);
        if (pad) notes.push_back(cfmt("added %lld samples of silence at the start so the loop begins on a 28-sample frame", pad));
        loop_start += pad;
        const long long loop_len = static_cast<long long>(chans[0].size()) - loop_start;
        if (loop_len % SPF)
            notes.push_back(cfmt("loop length %lld is not a multiple of 28: %lld samples of silence will play at the loop point (trim the loop to a multiple of 28 to avoid it)", loop_len, SPF - loop_len % SPF));
    }
    const bool lead = !B(a, "no_lead_frame");
    std::vector<std::string> encoded;
    for (const auto& c : chans) encoded.push_back(encode_adpcm(c, looping ? loop_start : -1, lead, true, B(a, "exhaustive")));
    std::string magic = "VAGp"; long long version = DEFAULT_VERSION;
    if (has_like) { magic = like.hdr.magic; version = like.hdr.version; }
    const std::string outp = S(a, "out").empty() ? default_out(wavp, ".vag") : S(a, "out");
    const std::string name = !get(a, "name").is_none() ? S(a, "name") : (has_like && !like.hdr.name.empty() ? like.hdr.name : header_name(outp));
    std::string stereo_mode = get(a, "stereo").is_none() ? std::string() : S(a, "stereo");
    const long long il = I(a, "interleave");
    if (encoded.size() == 2 && il && stereo_mode.empty()) stereo_mode = "interleave";
    std::vector<std::pair<std::string, std::string>> outputs;
    if (encoded.size() == 1) outputs.emplace_back(outp, encoded[0]);
    else if (stereo_mode == "interleave") {
        if (!il || il % 16) fail("interleaved stereo needs --interleave N, a multiple of 16");
        outputs.emplace_back(outp, interleave_join(encoded, il));
        notes.push_back(cfmt("stereo interleaved every %lld bytes (left block, then right)", il));
    } else {
        const std::string stem = console_tim2::splitext0(outp), ext = outp.substr(stem.size());
        outputs.emplace_back(stem + "_L" + (ext.empty() ? ".vag" : ext), encoded[0]);
        outputs.emplace_back(stem + "_R" + (ext.empty() ? ".vag" : ext), encoded[1]);
        notes.push_back("stereo written as two mono files (_L and _R)");
    }
    if (has_like && !B(a, "allow_bigger")) {
        for (const auto& [path, data] : outputs) {
            if (static_cast<long long>(data.size()) > like.bytes) {
                std::fflush(stdout);
                std::fprintf(stderr, "refused: %s would be %lld bytes of ADPCM, the original is %lld. Games often load sounds into fixed slots in the 2 MB of SPU2 RAM; shorten the sound, lower the rate, or pass --allow-bigger if you know it fits.\n",
                             path.c_str(), static_cast<long long>(data.size()), like.bytes);
                return 1;
            }
        }
    }
    for (const auto& [path, data] : outputs) {
        const std::string hdr = make_header(static_cast<long long>(data.size()), target_rate, name, version, magic);
        ensure_parent(path);
        write_text(path, hdr + data);
    }
    double q = HUGE_VAL;   // quality: decode what was written and compare
    for (std::size_t i = 0; i < chans.size() && i < encoded.size(); ++i) {
        DecInfo inf;
        const auto dec = decode_adpcm(encoded[i], 0, -1, true, inf);
        q = std::min(q, snr_db(chans[i], dec, lead ? SPF : 0, chans[i].size()));
    }
    for (const auto& [path, data] : outputs) {
        out(cfmt("wrote %s: %lld Hz, %zu bytes of ADPCM (%zu frames, %.3f s), ", path.c_str(), target_rate, data.size(), data.size() / static_cast<std::size_t>(FRAME_BYTES), secs(static_cast<long long>(chans[0].size()), target_rate)) +
            (looping ? cfmt("loops from sample %lld", loop_start + (lead ? SPF : 0)) : std::string("one-shot")));
    }
    out("quality: " + (std::isinf(q) ? std::string("inf") : cfmt("%.1f", q)) + " dB signal to noise after decoding");
    for (const auto& n : notes) out("note: " + n);
    if (has_like)
        out(cfmt("matched the original: rate %lld, magic %s, version ", target_rate, magic.c_str()) + px(version) + ", name " + str_repr(name) + cfmt(" (original %lld bytes)", like.bytes));
    return 0;
}

// VAGp/VAGi headers in a file, read in pieces (a disc image never sits in memory whole)
struct Hit { Header hdr; long long offset = 0, bytes = 0; bool valid = false; Val seconds; std::string extracted; };
inline std::vector<Hit> scan_hits(const std::string& path, long long chunk = SCAN_CHUNK) {
    std::ifstream fh(fspath(path), std::ios::binary);
    if (!fh) throw OSError(errno ? errno : ENOENT, path);
    std::vector<long long> offs;
    long long base = 0;
    std::string carry, block(static_cast<std::size_t>(chunk), '\0');
    while (true) {
        fh.read(block.data(), static_cast<std::streamsize>(chunk));
        const long long got = static_cast<long long>(fh.gcount());
        if (got <= 0) break;
        const std::string data = carry + block.substr(0, static_cast<std::size_t>(got));
        std::size_t st = 0;
        while (true) {
            const std::size_t i = data.find("VAG", st);
            if (i == std::string::npos || i + 4 > data.size()) break;
            if (data[i + 3] == 'p' || data[i + 3] == 'i') offs.push_back(base - static_cast<long long>(carry.size()) + static_cast<long long>(i));
            st = i + 1;
        }
        carry = data.size() > 3 ? data.substr(data.size() - 3) : data;
        base += got;
    }
    const long long size = base;
    std::sort(offs.begin(), offs.end());
    offs.erase(std::unique(offs.begin(), offs.end()), offs.end());
    std::vector<Hit> hits;
    for (const long long off : offs) {
        fh.clear(); fh.seekg(off);
        std::string head(static_cast<std::size_t>(HEADER_SIZE), '\0');
        fh.read(head.data(), HEADER_SIZE);
        head.resize(static_cast<std::size_t>(fh.gcount()));
        Hit h; h.hdr = parse_header(head, 0);
        if (!h.hdr.ok) continue;
        const long long avail = size - off - HEADER_SIZE;
        h.valid = h.hdr.rate >= 1000 && h.hdr.rate <= 192000 && h.hdr.data_size > 0 && h.hdr.data_size <= avail + HEADER_SLACK;
        h.offset = off;
        h.bytes = std::min(h.hdr.data_size, std::max(avail, 0LL));
        h.seconds = h.hdr.rate ? Val::flt(round_n(static_cast<double>(h.bytes / FRAME_BYTES * SPF) / static_cast<double>(h.hdr.rate), 3)) : Val::integer(0);
        hits.push_back(h);
    }
    return hits;
}
inline Val hit_val(const Hit& h) {
    Val d = header_val(h.hdr);
    d.d->set("offset", Val::integer(h.offset)); d.d->set("valid", Val::boolean(h.valid)); d.d->set("bytes", Val::integer(h.bytes)); d.d->set("seconds", h.seconds);
    if (!h.extracted.empty()) d.d->set("extracted", Val::str(h.extracted));
    return d;
}

inline int cmd_scan(const Val& a) {
    const std::string file = S(a, "file");
    if (!is_file(file)) fail(file + " is not a file");
    const bool all = B(a, "all");
    std::vector<Hit> found;
    for (auto& h : scan_hits(file)) if (h.valid || all) found.push_back(h);
    const std::string extract = S(a, "extract");
    const bool do_extract = !get(a, "extract").is_none() && !extract.empty();
    if (do_extract) {
        makedirs(extract);
        std::ifstream fh(fspath(file), std::ios::binary);
        for (std::size_t i = 0; i < found.size(); ++i) {
            Hit& h = found[i];
            if (!h.valid) continue;
            fh.clear(); fh.seekg(h.offset);
            std::string blob(static_cast<std::size_t>(HEADER_SIZE + h.bytes), '\0');
            fh.read(blob.data(), static_cast<std::streamsize>(blob.size()));
            blob.resize(static_cast<std::size_t>(fh.gcount()));
            std::string safe; std::size_t k = 0;   // c.isalnum() or c in "-_" over the latin-1 name, 24 characters
            for (const auto& ch : cps(h.hdr.name)) {
                if (k++ >= 24) break;
                const uint32_t c = cp_of(ch);
                safe += (uprop::alnum(c) || c == '-' || c == '_') ? ch : std::string("_");
            }
            if (safe.empty()) safe = "vag";
            h.extracted = path_join(extract, cfmt("%03zu_%08llx_", i, static_cast<unsigned long long>(h.offset)) + safe + ".vag");
            write_text(h.extracted, blob);
        }
    }
    if (B(a, "json")) {
        List l; for (const auto& h : found) l.push_back(hit_val(h));
        out(dumps2(Val::list(std::move(l))));
    } else if (found.empty()) {
        out("no VAGp headers found in " + file);
    } else {
        out(cfmt("%-12s %-5s %-8s %-10s %-9s %s", "offset", "magic", "rate", "bytes", "seconds", "name"));
        for (const auto& h : found) {
            const double s = h.seconds.t == Val::FLOAT ? h.seconds.f : 0.0;
            out(cfmt("0x%-10llx %-5s %-8lld %-10lld %-9.3f ", static_cast<unsigned long long>(h.offset), h.hdr.magic.c_str(), h.hdr.rate, h.bytes, s) + h.hdr.name +
                (h.valid ? "" : "  (implausible header, skipped)"));
        }
        out(cfmt("%zu VAG header(s)", found.size()) + (do_extract ? ", extracted to " + extract : std::string()));
    }
    return 0;
}

// split raw ADPCM (a .bd style bank) into sounds at end flags
inline std::vector<std::pair<long long, long long>> split_segments(const std::string& buf, long long start) {
    const long long end = static_cast<long long>(buf.size());
    std::vector<std::pair<long long, long long>> segs, keep;
    long long seg_start = start, pos = start;
    while (pos + FRAME_BYTES <= end) {
        const unsigned flags = b8(buf, pos + 1);
        if ((flags & FLAG_MARKER) == FLAG_MARKER) { if (pos > seg_start) segs.emplace_back(seg_start, pos); seg_start = pos + FRAME_BYTES; }
        else if (flags & FLAG_END) { segs.emplace_back(seg_start, pos + FRAME_BYTES); seg_start = pos + FRAME_BYTES; }
        pos += FRAME_BYTES;
    }
    if (seg_start < pos) segs.emplace_back(seg_start, pos);
    for (const auto& s : segs) if (any_nonzero(buf, s.first, s.second)) keep.push_back(s);
    return keep;
}

inline int cmd_split(const Val& a) {
    const std::string file = S(a, "file");
    const std::string buf = read_input(file);
    const long long offset = I(a, "offset"), rate = I(a, "rate");
    if (offset < 0 || offset >= static_cast<long long>(buf.size())) fail("offset " + px(offset) + " is outside the file");
    if (parse_header(buf, offset).ok) fail("this file has a VAGp header: use towav (or scan --extract for several)");
    const auto segs = split_segments(buf, offset);
    const std::string outd = S(a, "out");
    makedirs(outd);
    const std::string stem = console_tim2::splitext0(basename(file));
    std::string stem11; { std::size_t k = 0; for (const auto& ch : cps(stem)) { if (k++ >= 11) break; stem11 += ch; } }
    List index; long long bad_total = 0;
    std::vector<std::string> text_rows;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const auto [s0, s1] = segs[i];
        DecInfo info;
        const auto samples = decode_adpcm(buf, s0, s1, true, info);
        bad_total += info.bad;
        const bool loop = info.loop_end >= 0 && info.loop_start >= 0;
        const std::string wav = path_join(outd, stem + cfmt("_%03zu.wav", i));
        write_wav(wav, {samples}, rate, loop, info.loop_start, info.loop_end);
        const double seconds = round_n(secs(static_cast<long long>(samples.size()), rate), 3);
        Val row = dict_of({{"index", Val::integer(static_cast<long long>(i))}, {"offset", Val::integer(s0)}, {"bytes", Val::integer(s1 - s0)},
                           {"samples", Val::integer(static_cast<long long>(samples.size()))}, {"seconds", Val::flt(seconds)}, {"loops", Val::boolean(loop)},
                           {"wav", Val::str(wav)}, {"bad_frames", Val::integer(info.bad)}});
        if (B(a, "vag")) {
            const std::string vag = path_join(outd, stem + cfmt("_%03zu.vag", i));
            const std::string hdr = make_header(s1 - s0, rate, stem11 + cfmt("_%03zu", i), DEFAULT_VERSION, "VAGp");
            write_text(vag, hdr + buf.substr(static_cast<std::size_t>(s0), static_cast<std::size_t>(s1 - s0)));
            row.d->set("vag", Val::str(vag));
        }
        text_rows.push_back(cfmt("%3zu  0x%08llx  %8lld bytes  %7.3f s", i, static_cast<unsigned long long>(s0), s1 - s0, seconds) + (loop ? "  loops" : ""));
        index.push_back(row);
    }
    const Val idx = Val::list(index);
    write_text(path_join(outd, "split.json"), dumps2(dict_of({{"source", Val::str(basename(file))}, {"rate", Val::integer(rate)}, {"sounds", idx}})));
    if (B(a, "json")) out(dumps2(idx));
    else {
        for (const auto& r : text_rows) out(r);
        out(cfmt("%zu sound(s) written to ", index.size()) + outd + " (heuristic split at end flags; the bank's .hd decides the real list)");
    }
    if (bad_total) { out(cfmt("WARNING: %lld frames had an out-of-range filter or shift: this may not be raw ADPCM", bad_total)); return 1; }
    return 0;
}

// ---------------------------------------------------------------- CLI
inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "PS2 SPU2 ADPCM (VAG) audio: decode to WAV, encode your own WAV to VAG, find VAGs in bigger files.");
    p.epilog = "Rip only from discs you own; never publish ripped game audio. Outputs go under console/audio/ by default.";
    p.subcommands("cmd", true); p.sub_metavar = "command";
    { Parser& s = p.sub("info"); s.help_line = "header, rate, length, loop points of a VAG (or raw ADPCM with --rate)";
      s.pos("file");
      s.opt({"--offset"}).type(parse_int_arg, "_int").def(Val::integer(0)).help("byte offset of the VAG or raw data (default 0)");
      s.opt({"--rate"}).type(parse_int_arg, "_int").def(Val::integer(0)).help("sample rate (required for raw ADPCM)");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("towav"); s.help_line = "decode a VAG (or raw ADPCM) to a 16-bit WAV";
      s.pos("file");
      s.opt({"--out"}).help("WAV path (default console/audio/<name>.wav)");
      s.opt({"--rate"}).type(parse_int_arg, "_int").def(Val::integer(0)).help("sample rate: overrides the header, required for raw ADPCM");
      s.opt({"--offset"}).type(parse_int_arg, "_int").def(Val::integer(0)).help("byte offset of the VAG or raw data");
      s.opt({"--channels"}).type(parse_int_arg, "_int").def(Val::integer(1)).help("2 for interleaved stereo");
      s.opt({"--interleave"}).type(parse_int_arg, "_int").def(Val::integer(0)).help("bytes per channel block for stereo (multiple of 16)");
      s.opt({"--all"}).store_true().help("decode every frame, ignoring end flags");
      s.opt({"--no-loop-chunk"}).store_true().help("do not write loop points into a smpl chunk"); }
    { Parser& s = p.sub("fromwav"); s.help_line = "encode a 16-bit PCM WAV to a VAG";
      s.pos("wav");
      s.opt({"--out"}).help("VAG path (default console/audio/<name>.vag)");
      s.opt({"--rate"}).type(parse_int_arg, "_int").def(Val::integer(0)).help("resample to this rate first");
      s.opt({"--like"}).help("the original VAG you are replacing: copy its rate, header and looping, refuse if bigger");
      s.opt({"--allow-bigger"}).store_true().help("with --like, write even if bigger than the original");
      s.opt({"--loop"}).store_true().help("loop the whole sound");
      s.opt({"--loop-start"}).type(parse_int_arg, "_int").help("loop back to this sample (implies a loop)");
      s.opt({"--no-loop"}).store_true().help("never loop, even if the WAV has a smpl loop");
      s.opt({"--mono"}).store_true().help("mix stereo down to one channel");
      s.opt({"--stereo"}).choices({"split", "interleave"}).help("stereo input: two mono files (default) or one interleaved file");
      s.opt({"--interleave"}).type(parse_int_arg, "_int").def(Val::integer(0)).help("bytes per channel block for --stereo interleave");
      s.opt({"--name"}).help("name stored in the header (16 characters)");
      s.opt({"--no-lead-frame"}).store_true().help("omit the silent first frame Sony tools write");
      s.opt({"--exhaustive"}).store_true().help("try all 13 shifts for every filter (slower)"); }
    { Parser& s = p.sub("scan"); s.help_line = "find VAGp headers inside a bigger file (archive, bank, disc file)";
      s.pos("file");
      s.opt({"--extract"}).metavar("DIR").help("write each VAG found to DIR");
      s.opt({"--all"}).store_true().help("also list implausible headers");
      s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("split"); s.help_line = "split headerless ADPCM (a .bd sound bank) into WAVs at end flags (heuristic)";
      s.pos("file");
      s.opt({"--rate"}).type(parse_int_arg, "_int").required().help("sample rate to write (banks do not store it in the .bd)");
      s.opt({"--out"}).required().help("folder for the WAVs and split.json");
      s.opt({"--offset"}).type(parse_int_arg, "_int").def(Val::integer(0));
      s.opt({"--vag"}).store_true().help("also write each sound as a VAG with a header");
      s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    return guard("error", [&]() -> int {
        if (cmd == "info") return cmd_info(a);
        if (cmd == "towav") return cmd_towav(a);
        if (cmd == "fromwav") return cmd_fromwav(a);
        if (cmd == "scan") return cmd_scan(a);
        return cmd_split(a);
    });
}
PACKTOOL("console-sound/vag", main);
}  // namespace packtools::console_vag
