// console_tim2.inc - console-graphics/tim2: a C++ port of console-skill/console-graphics/tim2.py (Jake Schincariol,
// MIT). PS2 TIM2 textures (.tm2) to PNG and back at the same size and format: info, topng, frompng (median cut for
// --quantize), scan (TIM2 blobs inside bigger files, streamed) and insert. PNG and zlib are built in (zip.h).
namespace packtools::console_tim2 {
using namespace pyrt;
using namespace packtools::console_common;

const char* const PROG = "tim2";
const std::string MAGIC = "TIM2";
const long long CHUNK = 8LL * 1024 * 1024;   // scan reads big files in pieces this size

inline const char* type_name(int t) {
    switch (t) { case 1: return "16-bit RGBA5551"; case 2: return "24-bit RGB"; case 3: return "32-bit RGBA"; case 4: return "4-bit indexed"; case 5: return "8-bit indexed"; default: return nullptr; }
}
inline int type_bpp(int t) { return t == 1 ? 16 : t == 2 ? 24 : t == 3 ? 32 : t == 4 ? 4 : 8; }
inline const char* clut_format(int c) { return c == 1 ? "16-bit" : c == 2 ? "24-bit" : c == 3 ? "32-bit" : nullptr; }
inline std::string psm_name(unsigned v) {
    switch (v) {
        case 0x00: return "PSMCT32"; case 0x01: return "PSMCT24"; case 0x02: return "PSMCT16"; case 0x0A: return "PSMCT16S";
        case 0x13: return "PSMT8"; case 0x14: return "PSMT4"; case 0x1B: return "PSMT8H"; case 0x24: return "PSMT4HL"; case 0x2C: return "PSMT4HH";
        default: return cfmt("0x%x", v);   // hex()
    }
}

#ifdef _WIN32
const std::string SEP = "\\"; const char* const SEP_CHARS = "/\\";
#else
const std::string SEP = "/"; const char* const SEP_CHARS = "/";
#endif
inline bool ends_with(const std::string& s, const std::string& t) { return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0; }
[[noreturn]] inline void fail(const std::string& m) { throw InputError(m); }

inline unsigned rd8(const std::string& d, long long at) { return static_cast<unsigned char>(d[static_cast<std::size_t>(at)]); }
inline unsigned rd16(const std::string& d, long long at) { return rd8(d, at) | rd8(d, at + 1) << 8; }
inline uint32_t rd32(const std::string& d, long long at) { return rd16(d, at) | static_cast<uint32_t>(rd16(d, at + 2)) << 16; }
inline uint64_t rd64(const std::string& d, long long at) { return rd32(d, at) | static_cast<uint64_t>(rd32(d, at + 4)) << 32; }
inline uint32_t be32(const std::string& d, long long at) { return rd8(d, at) << 24 | rd8(d, at + 1) << 16 | rd8(d, at + 2) << 8 | rd8(d, at + 3); }
inline std::string be32s(uint32_t v) { std::string s(4, '\0'); for (int k = 0; k < 4; ++k) s[static_cast<std::size_t>(k)] = static_cast<char>((v >> (24 - 8 * k)) & 0xFF); return s; }

// data[a:b] with Python slice rules
inline std::string py_slice(const std::string& d, long long a, long long b) {
    const long long n = static_cast<long long>(d.size());
    if (a < 0) a = std::max(0LL, a + n);
    if (b < 0) b = std::max(0LL, b + n);
    a = std::min(a, n); b = std::min(b, n);
    return b > a ? d.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a)) : std::string();
}

// repr() of a bytes object
inline std::string bytes_repr(const std::string& b) {
    const bool dq = b.find('\'') != std::string::npos && b.find('"') == std::string::npos;
    const char q = dq ? '"' : '\'';
    std::string o = "b"; o += q;
    for (unsigned char c : b) {
        if (c == static_cast<unsigned char>(q) || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c < 32 || c >= 127) o += cfmt("\\x%02x", c);
        else o += static_cast<char>(c);
    }
    return o + q;
}

// int(text, 16): optional sign and 0x prefix, underscores between digits, Unicode decimal digits as digits;
// past 63 bits is refused (Python would take it)
inline std::optional<long long> hex_int(const std::string& s0) {
    std::string s = strip(s0); bool neg = false;
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) { neg = s[0] == '-'; s = s.substr(1); }
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s = s.substr(2); if (!s.empty() && s[0] == '_') s = s.substr(1); }
    if (s.empty() || s[0] == '_' || s.back() == '_' || s.find("__") != std::string::npos) return std::nullopt;
    unsigned long long v = 0;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c == '_') continue;
        unsigned d;
        if (c < 128 && std::isxdigit(static_cast<int>(c))) d = static_cast<unsigned>(std::isdigit(static_cast<int>(c)) ? c - '0' : std::tolower(static_cast<int>(c)) - 'a' + 10);
        else if (uprop::decimal(c)) { uint32_t k = 0; while (k < c && uprop::decimal(c - k - 1)) ++k; d = k % 10; }
        else return std::nullopt;
        if (v > (static_cast<unsigned long long>(LLONG_MAX) - d) / 16) return std::nullopt;
        v = v * 16 + d;
    }
    return neg ? -static_cast<long long>(v) : static_cast<long long>(v);
}

// --------------------------------------------------------------------------
// Colour helpers (a colour is packed r<<24 | g<<16 | b<<8 | a)
// --------------------------------------------------------------------------
using Color = uint32_t;
inline Color rgba(unsigned r, unsigned g, unsigned b, unsigned a) { return (r & 0xFF) << 24 | (g & 0xFF) << 16 | (b & 0xFF) << 8 | (a & 0xFF); }
inline unsigned chan(Color c, int k) { return (c >> (24 - 8 * k)) & 0xFF; }
inline unsigned alpha_of(Color c) { return c & 0xFF; }

inline unsigned alpha_to_png(unsigned a) { return a >= 128 ? 255 : (a * 255 + 64) / 128; }   // PS2 alpha (0x80 = opaque)
inline unsigned alpha_to_ps2(unsigned a) { return (a * 128 + 127) / 255; }
inline unsigned expand5(unsigned x) { return (x << 3) | (x >> 2); }
inline unsigned pack5(unsigned v) { return (v * 31 + 127) / 255; }

// raw texels or CLUT entries -> colours; alpha_unused: -1 = work it out (16-bit)
inline std::vector<Color> decode_colors(const std::string& raw, int fmt, long long count, int alpha_unused = -1) {
    std::vector<Color> o;
    const long long es = fmt == 32 ? 4 : fmt == 24 ? 3 : 2;
    if (static_cast<long long>(raw.size()) < count * es) throw PyErr("ValueError", "not enough values to unpack");
    if (fmt == 32) for (long long i = 0; i < count; ++i) o.push_back(rgba(rd8(raw, 4 * i), rd8(raw, 4 * i + 1), rd8(raw, 4 * i + 2), alpha_to_png(rd8(raw, 4 * i + 3))));
    else if (fmt == 24) for (long long i = 0; i < count; ++i) o.push_back(rgba(rd8(raw, 3 * i), rd8(raw, 3 * i + 1), rd8(raw, 3 * i + 2), 255));
    else if (fmt == 16) {
        if (alpha_unused < 0) { alpha_unused = 1; for (long long i = 0; i < count; ++i) if (rd16(raw, 2 * i) & 0x8000) alpha_unused = 0; }
        for (long long i = 0; i < count; ++i) {
            const unsigned v = rd16(raw, 2 * i);
            o.push_back(rgba(expand5(v & 31), expand5((v >> 5) & 31), expand5((v >> 10) & 31), (alpha_unused || (v & 0x8000)) ? 255 : 0));
        }
    }
    return o;
}
// -> the stored value, little-endian packed (what read_raw gives back)
inline uint32_t encode_color(Color c, int fmt, bool alpha_unused = false) {
    const unsigned r = chan(c, 0), g = chan(c, 1), b = chan(c, 2), a = chan(c, 3);
    if (fmt == 32) return r | g << 8 | b << 16 | alpha_to_ps2(a) << 24;
    if (fmt == 24) return r | g << 8 | b << 16;
    return pack5(r) | (pack5(g) << 5) | (pack5(b) << 10) | (alpha_unused ? 0u : (a >= 128 ? 0x8000u : 0u));
}
inline long long swizzle(long long i) {   // CSM1 palette order: entries 8-15 and 16-23 of every 32 are swapped
    const long long m = i & 0x1F;
    if (8 <= m && m < 16) return i + 8;
    if (16 <= m && m < 24) return i - 8;
    return i;
}

// --------------------------------------------------------------------------
// TIM2 parsing
// --------------------------------------------------------------------------
struct Level { long long off; long long w, h; long long size; };
struct Picture {
    long long index = 0, offset = 0, total = 0, clut_size = 0, image_size = 0;
    int header_size = 0, clut_colors = 0, pict_fmt = 0, mips = 1, clut_type = 0, image_type = 0, width = 0, height = 0;
    uint64_t tex0 = 0, tex1 = 0; uint32_t regs = 0, texclut = 0;
    int bpp = 0; bool indexed = false; long long image_off = 0, clut_off = 0;
    int clut_bpp = 0, pal_size = 0; bool swizzled = false; int palettes = 0;
    std::vector<Level> levels;
};
struct Header { int version = 0, align = 0x10, count = 0; long long end = 0; };

inline std::pair<Header, std::vector<Picture>> parse_tim2(const std::string& data, long long base = 0, const std::string& name = "file") {
    const long long len = static_cast<long long>(data.size());
    if (len - base < 16) fail(name + " is too short to be a TIM2 file");
    if (data.compare(static_cast<std::size_t>(base), 4, MAGIC) != 0) fail(name + " is not a TIM2 file (no 'TIM2' magic)");
    const int version = static_cast<int>(rd8(data, base + 4)), fmt_id = static_cast<int>(rd8(data, base + 5)), count = static_cast<int>(rd16(data, base + 6));
    if (fmt_id != 0 && fmt_id != 1) fail(name + cfmt(" has an unknown TIM2 alignment id %d", fmt_id));
    if (count == 0) fail(name + " holds no pictures");
    long long pos = base + (fmt_id == 1 ? 0x80 : 0x10);
    std::vector<Picture> pics;
    for (int n = 0; n < count; ++n) {
        if (pos + 0x30 > len) fail(name + cfmt(" is truncated: picture %d header runs past the end", n));
        Picture p;
        p.total = rd32(data, pos); p.clut_size = rd32(data, pos + 4); p.image_size = rd32(data, pos + 8);
        p.header_size = static_cast<int>(rd16(data, pos + 12)); p.clut_colors = static_cast<int>(rd16(data, pos + 14));
        p.pict_fmt = static_cast<int>(rd8(data, pos + 16)); const int mips = static_cast<int>(rd8(data, pos + 17));
        p.clut_type = static_cast<int>(rd8(data, pos + 18)); p.image_type = static_cast<int>(rd8(data, pos + 19));
        p.width = static_cast<int>(rd16(data, pos + 20)); p.height = static_cast<int>(rd16(data, pos + 22));
        p.tex0 = rd64(data, pos + 0x18); p.tex1 = rd64(data, pos + 0x20); p.regs = rd32(data, pos + 0x28); p.texclut = rd32(data, pos + 0x2C);
        if (!type_name(p.image_type)) fail(name + cfmt(" picture %d has an unknown image type %d", n, p.image_type));
        if (p.header_size < 0x30 || p.total < p.header_size + p.image_size + p.clut_size) fail(name + cfmt(" picture %d has inconsistent sizes", n));
        if (p.width == 0 || p.height == 0) fail(name + cfmt(" picture %d has a zero size", n));
        if (pos + p.total > len) fail(name + cfmt(" is truncated: picture %d runs past the end of the file", n));
        p.index = n; p.offset = pos; p.mips = std::max(mips, 1);
        p.bpp = type_bpp(p.image_type);
        p.indexed = p.image_type == 4 || p.image_type == 5;
        p.image_off = pos + p.header_size;
        p.clut_off = p.image_off + p.image_size;
        if (p.indexed) {
            const int cf = p.clut_type & 0x3F;
            if (!clut_format(cf) || p.clut_colors == 0 || p.clut_size == 0) fail(name + cfmt(" picture %d is indexed but has no usable palette", n));
            p.clut_bpp = cf == 1 ? 16 : cf == 2 ? 24 : 32;
            if (p.clut_size < static_cast<long long>(p.clut_colors) * p.clut_bpp / 8) fail(name + cfmt(" picture %d palette is shorter than its colour count", n));
            p.pal_size = p.image_type == 5 ? 256 : 16;
            p.swizzled = p.image_type == 5 ? !(p.clut_type & 0x80) : (p.clut_type & 0x40) != 0;
            p.palettes = std::max(1, p.clut_colors / p.pal_size);
        }
        // mip levels: sizes from the MIPMAP header when present
        const bool sized = p.mips > 1 && p.header_size >= 0x30 + 16 + 4 * p.mips;
        long long off = p.image_off;
        for (int k = 0; k < p.mips; ++k) {
            const long long w = std::max(1LL, k >= 31 ? 0LL : static_cast<long long>(p.width) >> k);
            const long long h = std::max(1LL, k >= 31 ? 0LL : static_cast<long long>(p.height) >> k);
            const long long need = (w * h * p.bpp + 7) / 8;
            const long long size = sized ? static_cast<long long>(rd32(data, pos + 0x30 + 16 + 4 * k)) : need;
            if (size < need || off + size > p.image_off + p.image_size) fail(name + cfmt(" picture %d mip level %d does not fit its image data", n, k));
            p.levels.push_back({off, w, h, size});
            off += size;
        }
        pics.push_back(p);
        pos += p.total;
    }
    Header hd; hd.version = version; hd.align = fmt_id == 1 ? 0x80 : 0x10; hd.count = count; hd.end = pos;
    return {hd, pics};
}

// bytes per row (rows padded to whole bytes), or -1 for one continuous bit stream
inline long long level_stride(const Picture& p, long long w, long long h, long long size) {
    const long long row = (w * p.bpp + 7) / 8;
    return size >= row * h ? row : -1;
}
// texels (little-endian packed, bpp >= 8) or palette indices (4-bit)
inline std::vector<uint32_t> read_raw(const std::string& data, const Picture& p, int level = 0) {
    const Level& L = p.levels[static_cast<std::size_t>(level)];
    const long long stride = level_stride(p, L.w, L.h, L.size);
    std::vector<uint32_t> o; o.reserve(static_cast<std::size_t>(L.w * L.h));
    if (p.bpp >= 8) {
        const int step = p.bpp / 8;
        for (long long y = 0; y < L.h; ++y) {
            const long long row = L.off + (stride >= 0 ? y * stride : y * L.w * step);
            for (long long x = 0; x < L.w; ++x) { uint32_t v = 0; for (int k = 0; k < step; ++k) v |= rd8(data, row + x * step + k) << (8 * k); o.push_back(v); }
        }
        return o;
    }
    for (long long y = 0; y < L.h; ++y)
        for (long long x = 0; x < L.w; ++x) {
            if (stride >= 0) { const unsigned b = rd8(data, L.off + y * stride + x / 2); o.push_back(x % 2 == 0 ? b & 0x0F : b >> 4); }
            else { const long long pix = y * L.w + x; const unsigned b = rd8(data, L.off + pix / 2); o.push_back(pix % 2 == 0 ? b & 0x0F : b >> 4); }
        }
    return o;
}
inline std::string vals_bytes(const std::vector<uint32_t>& v, int step) {
    std::string s; s.reserve(v.size() * static_cast<std::size_t>(step));
    for (const uint32_t x : v) for (int k = 0; k < step; ++k) s += static_cast<char>((x >> (8 * k)) & 0xFF);
    return s;
}

// logical palette (swizzle undone), padded to pal_size
inline std::vector<Color> palette(const std::string& data, const Picture& p, long long which = 0) {
    const long long n = std::min<long long>(p.pal_size, p.clut_colors - which * p.pal_size);
    if (n <= 0) fail(cfmt("picture %lld has no palette %lld (it has %d)", p.index, which, p.palettes));
    const long long es = p.clut_bpp / 8, start = p.clut_off + which * p.pal_size * es;
    const auto stored = decode_colors(py_slice(data, start, start + n * es), p.clut_bpp, n);
    std::vector<Color> pal;
    for (long long k = 0; k < p.pal_size; ++k) {
        const long long s = p.swizzled ? swizzle(k) : k;
        pal.push_back(s < static_cast<long long>(stored.size()) ? stored[static_cast<std::size_t>(s)] : 0);
    }
    return pal;
}

struct Image { long long w = 0, h = 0; std::vector<Color> px; bool has_idx = false; std::vector<uint32_t> idx; std::vector<Color> pal; };
inline Image decode_picture(const std::string& data, const Picture& p, int level = 0, long long which = 0) {
    const Level& L = p.levels[static_cast<std::size_t>(level)];
    Image im; im.w = L.w; im.h = L.h;
    auto vals = read_raw(data, p, level);
    if (p.indexed) {
        im.pal = palette(data, p, which); im.has_idx = true;
        for (const uint32_t v : vals) im.px.push_back(im.pal[v]);
        im.idx = std::move(vals);
        return im;
    }
    int alpha_unused = -1;
    if (p.bpp == 16) {
        const auto lvl0 = level ? read_raw(data, p, 0) : vals;
        alpha_unused = 1; for (const uint32_t v : lvl0) if (v & 0x8000) alpha_unused = 0;
    }
    im.px = decode_colors(vals_bytes(vals, p.bpp / 8), p.bpp, L.w * L.h, alpha_unused);
    return im;
}

inline Val tex0_fields(uint64_t t) {
    const auto I = [&](int sh, uint64_t mask) { return Val::integer(static_cast<long long>((t >> sh) & mask)); };
    return dict_of({{"TBP0", I(0, 0x3FFF)}, {"TBW", I(14, 0x3F)}, {"PSM", Val::str(psm_name(static_cast<unsigned>((t >> 20) & 0x3F)))},
                    {"TW", I(26, 0xF)}, {"TH", I(30, 0xF)}, {"TCC", I(34, 1)}, {"TFX", I(35, 3)}, {"CBP", I(37, 0x3FFF)},
                    {"CPSM", Val::str(psm_name(static_cast<unsigned>((t >> 51) & 0xF)))}, {"CSM", I(55, 1)}, {"CSA", I(56, 0x1F)}, {"CLD", I(61, 7)}});
}

// --------------------------------------------------------------------------
// PNG encoder / decoder
// --------------------------------------------------------------------------
const std::string PNG_SIG("\x89PNG\r\n\x1a\n", 8);

inline std::string chunk(const std::string& kind, const std::string& payload) {
    return be32s(static_cast<uint32_t>(payload.size())) + kind + payload + be32s(zip::crc32(kind + payload));
}
inline std::string ihdr(long long w, long long h, int ctype) {
    return be32s(static_cast<uint32_t>(w)) + be32s(static_cast<uint32_t>(h)) + std::string{8, static_cast<char>(ctype), 0, 0, 0};
}
// colours -> RGB(A) PNG; or indices + palette -> 8-bit palette PNG (tRNS for alpha)
inline std::string encode_png(const Image& im, bool indexed) {
    std::string rows, extra, hd;
    if (indexed) {
        hd = ihdr(im.w, im.h, 3);
        for (long long y = 0; y < im.h; ++y) { rows += '\0'; for (long long x = 0; x < im.w; ++x) rows += static_cast<char>(im.idx[static_cast<std::size_t>(y * im.w + x)]); }
        std::string pl; for (const Color c : im.pal) { pl += static_cast<char>(chan(c, 0)); pl += static_cast<char>(chan(c, 1)); pl += static_cast<char>(chan(c, 2)); }
        extra = chunk("PLTE", pl);
        long long last = -1; for (std::size_t i = 0; i < im.pal.size(); ++i) if (alpha_of(im.pal[i]) != 255) last = static_cast<long long>(i);
        if (last >= 0) { std::string tr; for (long long i = 0; i <= last; ++i) tr += static_cast<char>(alpha_of(im.pal[static_cast<std::size_t>(i)])); extra += chunk("tRNS", tr); }
    } else {
        bool has_alpha = false; for (const Color c : im.px) if (alpha_of(c) != 255) has_alpha = true;
        hd = ihdr(im.w, im.h, has_alpha ? 6 : 2);
        const int n = has_alpha ? 4 : 3;
        for (long long y = 0; y < im.h; ++y) {
            rows += '\0';
            for (long long x = 0; x < im.w; ++x) { const Color c = im.px[static_cast<std::size_t>(y * im.w + x)]; for (int k = 0; k < n; ++k) rows += static_cast<char>(chan(c, k)); }
        }
    }
    return PNG_SIG + chunk("IHDR", hd) + extra + chunk("IDAT", zip::zlib_compress(rows)) + chunk("IEND", "");
}

inline int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}
// -> width, height, colours (+ indices and palette for palette PNGs). 8-bit, non-interlaced only.
inline Image decode_png(const std::string& blob, const std::string& name = "PNG") {
    if (blob.compare(0, 8, PNG_SIG) != 0) fail(name + " is not a PNG");
    const long long len = static_cast<long long>(blob.size());
    long long pos = 8;
    bool have_ihdr = false; uint32_t W = 0, H = 0; int depth = 0, ctype = 0, comp = 0, filt = 0, interlace = 0;
    bool have_plte = false; std::vector<Color> plte; bool have_trns = false; std::string trns, idat;
    while (pos + 8 <= len) {
        const long long length = be32(blob, pos); const std::string kind = blob.substr(static_cast<std::size_t>(pos + 4), 4);
        if (pos + 12 + length > len) fail(name + " is truncated (chunk " + bytes_repr(kind) + ")");
        const std::string payload = blob.substr(static_cast<std::size_t>(pos + 8), static_cast<std::size_t>(length));
        if (zip::crc32(kind + payload) != be32(blob, pos + 8 + length)) {
            std::string k; for (unsigned char c : kind) k += c < 128 ? std::string(1, static_cast<char>(c)) : std::string("\xEF\xBF\xBD");   // decode("ascii", "replace")
            fail(name + " is corrupt (bad CRC in chunk " + str_repr(k) + ")");
        }
        if (kind == "IHDR") {
            if (payload.size() != 13) throw PyErr("struct.error", "unpack requires a buffer of 13 bytes");
            have_ihdr = true; W = be32(payload, 0); H = be32(payload, 4);
            depth = static_cast<int>(rd8(payload, 8)); ctype = static_cast<int>(rd8(payload, 9)); comp = static_cast<int>(rd8(payload, 10));
            filt = static_cast<int>(rd8(payload, 11)); interlace = static_cast<int>(rd8(payload, 12));
        } else if (kind == "PLTE") {
            have_plte = true; plte.clear();
            for (long long i = 0; i + 2 < static_cast<long long>(payload.size()); i += 3) plte.push_back(rgba(rd8(payload, i), rd8(payload, i + 1), rd8(payload, i + 2), 0));
        } else if (kind == "tRNS") { have_trns = true; trns = payload; }
        else if (kind == "IDAT") idat += payload;
        else if (kind == "IEND") break;
        pos += 12 + length;
    }
    if (!have_ihdr) fail(name + " has no IHDR");
    if (interlace) fail(name + " is interlaced: save it again without interlacing");
    if (comp || filt) fail(name + " uses an unknown compression or filter method");
    const int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!channels) fail(name + cfmt(" has an unknown colour type %d", ctype));
    if (depth != 8 && !(ctype == 3 && (depth == 1 || depth == 2 || depth == 4)))
        fail(name + cfmt(" is %d-bit per channel: save it as an 8-bit PNG (RGB, RGBA, grey or palette)", depth));
    if (ctype == 3 && (!have_plte || plte.empty())) fail(name + " is a palette PNG without a palette");
    std::string raw;
    try { raw = zip::zlib_decompress(idat); } catch (const PyErr&) { fail(name + " has corrupt image data"); }
    const uint64_t w = W, h = H;
    const uint64_t stride = (w * static_cast<uint64_t>(channels) * static_cast<uint64_t>(depth) + 7) / 8;
    const std::size_t bpp = static_cast<std::size_t>(std::max(1, channels * depth / 8));
    if (h && (stride + 1 > raw.size() || h > raw.size() / (stride + 1))) fail(name + " image data is truncated");
    const std::size_t st = static_cast<std::size_t>(stride);
    std::vector<std::string> rows; std::string prev(st, '\0');
    for (uint64_t y = 0; y < h; ++y) {
        const std::size_t at = static_cast<std::size_t>(y) * (st + 1);
        const unsigned ft = static_cast<unsigned char>(raw[at]);
        std::string cur = raw.substr(at + 1, st);
        auto U = [](const std::string& s, std::size_t i) { return static_cast<int>(static_cast<unsigned char>(s[i])); };
        if (ft == 1) { for (std::size_t i = bpp; i < st; ++i) cur[i] = static_cast<char>((U(cur, i) + U(cur, i - bpp)) & 0xFF); }
        else if (ft == 2) { for (std::size_t i = 0; i < st; ++i) cur[i] = static_cast<char>((U(cur, i) + U(prev, i)) & 0xFF); }
        else if (ft == 3) { for (std::size_t i = 0; i < st; ++i) { const int left = i >= bpp ? U(cur, i - bpp) : 0; cur[i] = static_cast<char>((U(cur, i) + ((left + U(prev, i)) >> 1)) & 0xFF); } }
        else if (ft == 4) {
            for (std::size_t i = 0; i < st; ++i) {
                const int left = i >= bpp ? U(cur, i - bpp) : 0, up_left = i >= bpp ? U(prev, i - bpp) : 0;
                cur[i] = static_cast<char>((U(cur, i) + paeth(left, U(prev, i), up_left)) & 0xFF);
            }
        } else if (ft != 0) fail(name + cfmt(" has an unknown row filter %u", ft));
        rows.push_back(cur); prev = cur;
    }
    Image im; im.w = static_cast<long long>(w); im.h = static_cast<long long>(h);
    if (ctype == 3) {
        for (std::size_t i = 0; i < plte.size(); ++i) im.pal.push_back((plte[i] & 0xFFFFFF00u) | (i < trns.size() ? rd8(trns, static_cast<long long>(i)) : 255u));
        im.has_idx = true;
        const unsigned per = 8u / static_cast<unsigned>(depth), mask = (1u << depth) - 1;
        for (const auto& row : rows)
            for (uint64_t x = 0; x < w; ++x) {
                unsigned i;
                if (depth == 8) i = rd8(row, static_cast<long long>(x));
                else { const unsigned byte = rd8(row, static_cast<long long>(x / per)); i = (byte >> (8 - depth * static_cast<int>(x % per + 1))) & mask; }
                if (i >= im.pal.size()) fail(name + cfmt(" uses palette entry %u but its palette has %zu", i, im.pal.size()));
                im.idx.push_back(i);
            }
        for (const uint32_t i : im.idx) im.px.push_back(im.pal[i]);
    } else {
        int key_g = -1; long long key_rgb = -1;
        if (have_trns && ctype == 0 && trns.size() >= 2) key_g = static_cast<int>(rd8(trns, 1));
        if (have_trns && ctype == 2 && trns.size() >= 6) key_rgb = static_cast<long long>(rd8(trns, 1) << 16 | rd8(trns, 3) << 8 | rd8(trns, 5));
        for (const auto& row : rows)
            for (uint64_t x = 0; x < w; ++x) {
                const long long b = static_cast<long long>(x) * channels;
                const auto c = [&](int k) { return rd8(row, b + k); };
                if (ctype == 0) im.px.push_back(rgba(c(0), c(0), c(0), key_g == static_cast<int>(c(0)) ? 0 : 255));
                else if (ctype == 4) im.px.push_back(rgba(c(0), c(0), c(0), c(1)));
                else if (ctype == 2) im.px.push_back(rgba(c(0), c(1), c(2), key_rgb == static_cast<long long>(c(0) << 16 | c(1) << 8 | c(2)) ? 0 : 255));
                else im.px.push_back(rgba(c(0), c(1), c(2), c(3)));
            }
    }
    return im;
}

// --------------------------------------------------------------------------
// Median cut quantiser (for --quantize)
// --------------------------------------------------------------------------
using Counts = std::vector<std::pair<Color, long long>>;   // first-seen order, like a dict
struct Quant { std::vector<Color> pal; std::map<Color, std::size_t> mapping; };
inline Quant median_cut(const Counts& counts, long long k) {
    std::vector<Counts> boxes{counts};
    while (static_cast<long long>(boxes.size()) < k) {
        long long best = -1; long long best_i = -1; int best_ch = 0;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].size() < 2) continue;
            for (int ch = 0; ch < 4; ++ch) {
                unsigned lo = 255, hi = 0; for (const auto& e : boxes[i]) { lo = std::min(lo, chan(e.first, ch)); hi = std::max(hi, chan(e.first, ch)); }
                const long long rng = static_cast<long long>(hi) - lo;
                if (rng > best) { best = rng; best_i = static_cast<long long>(i); best_ch = ch; }
            }
        }
        if (best_i < 0 || best <= 0) break;
        Counts box = boxes[static_cast<std::size_t>(best_i)];
        std::stable_sort(box.begin(), box.end(), [&](const auto& x, const auto& y) { return chan(x.first, best_ch) < chan(y.first, best_ch); });
        long long tot = 0; for (const auto& e : box) tot += e.second;
        const double half = static_cast<double>(tot) / 2.0;
        long long acc = 0; std::size_t cut = 1;
        for (std::size_t j = 0; j < box.size(); ++j) {
            acc += box[j].second;
            if (static_cast<double>(acc) >= half) { cut = std::min(std::max<std::size_t>(j + 1, 1), box.size() - 1); break; }
        }
        Counts a(box.begin(), box.begin() + static_cast<long>(cut)), b(box.begin() + static_cast<long>(cut), box.end());
        boxes[static_cast<std::size_t>(best_i)] = std::move(a);
        boxes.insert(boxes.begin() + best_i + 1, std::move(b));
    }
    Quant q;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        long long tot = 0; for (const auto& e : boxes[i]) tot += e.second;
        unsigned c[4];
        for (int ch = 0; ch < 4; ++ch) {
            long long s = 0; for (const auto& e : boxes[i]) s += static_cast<long long>(chan(e.first, ch)) * e.second;
            c[ch] = static_cast<unsigned>(std::nearbyint(static_cast<double>(s) / static_cast<double>(tot)));   // round(): half to even
        }
        q.pal.push_back(rgba(c[0], c[1], c[2], c[3]));
        for (const auto& e : boxes[i]) q.mapping[e.first] = i;
    }
    return q;
}

// --------------------------------------------------------------------------
// Rebuilding a picture from a PNG
// --------------------------------------------------------------------------
inline std::vector<uint32_t> nearest_level(const std::vector<uint32_t>& vals, long long w, long long h, long long w2, long long h2) {
    std::vector<uint32_t> o;
    for (long long y = 0; y < h2; ++y) for (long long x = 0; x < w2; ++x) o.push_back(vals[static_cast<std::size_t>((y * h / h2) * w + (x * w / w2))]);
    return o;
}
inline void write_level(std::string& buf, const Picture& p, int level, const std::vector<uint32_t>& vals) {
    const Level& L = p.levels[static_cast<std::size_t>(level)];
    const long long stride = level_stride(p, L.w, L.h, L.size);
    if (p.bpp >= 8) {
        const int step = p.bpp / 8;
        for (long long y = 0; y < L.h; ++y) {
            const long long row = L.off + (stride >= 0 ? y * stride : y * L.w * step);
            for (long long x = 0; x < L.w; ++x) {
                const uint32_t v = vals[static_cast<std::size_t>(y * L.w + x)];
                for (int k = 0; k < step; ++k) buf[static_cast<std::size_t>(row + x * step + k)] = static_cast<char>((v >> (8 * k)) & 0xFF);
            }
        }
        return;
    }
    for (long long y = 0; y < L.h; ++y)
        for (long long x = 0; x < L.w; ++x) {
            const unsigned i = vals[static_cast<std::size_t>(y * L.w + x)] & 0x0F;
            const long long pix = y * L.w + x;
            const std::size_t at = static_cast<std::size_t>(L.off + (stride >= 0 ? y * stride + x / 2 : pix / 2));
            const bool low = stride >= 0 ? x % 2 == 0 : pix % 2 == 0;
            const unsigned b = static_cast<unsigned char>(buf[at]);
            buf[at] = static_cast<char>(low ? ((b & 0xF0) | i) : ((b & 0x0F) | (i << 4)));
        }
}

// -> (new file bytes, notes). Same size and format; header untouched.
inline std::pair<std::string, std::vector<std::string>> rebuild(const std::string& data, const Picture& p, const Image& png, long long which, bool quantize, bool keep_mips) {
    const long long w = png.w, h = png.h;
    if (w != p.width || h != p.height)
        fail(cfmt("the PNG is %lldx%lld but picture %lld is %dx%d: keep the same size (the game's code and GS registers expect it)", w, h, p.index, p.width, p.height));
    std::vector<std::string> notes;
    std::string buf = data;
    const Image orig = decode_picture(data, p, 0, which);
    const auto& px = png.px; const auto& opx = orig.px;
    const std::size_t N = static_cast<std::size_t>(w * h);
    if (p.bpp == 24) { for (const Color c : px) if (alpha_of(c) != 255) { notes.push_back("24-bit textures have no alpha: transparency in the PNG was dropped"); break; } }
    std::vector<uint32_t> vals; long long changed = 0;
    for (std::size_t i = 0; i < N; ++i) if (px[i] != opx[i]) ++changed;
    if (!p.indexed) {
        const auto raw = read_raw(data, p, 0);
        bool alpha_unused = false;
        if (p.bpp == 16) { alpha_unused = true; for (const uint32_t r : raw) if (r & 0x8000) alpha_unused = false; }
        for (std::size_t i = 0; i < N; ++i) vals.push_back(px[i] == opx[i] ? raw[i] : encode_color(px[i], p.bpp, alpha_unused));
    } else {
        const auto& orig_pal = orig.pal; const auto& orig_idx = orig.idx;
        std::map<Color, std::size_t> lookup;
        for (std::size_t i = 0; i < orig_pal.size(); ++i) lookup.emplace(orig_pal[i], i);
        bool all_in = true; for (const Color c : px) if (!lookup.count(c)) { all_in = false; break; }
        std::vector<Color> new_pal;
        if (all_in) {
            for (std::size_t i = 0; i < N; ++i) vals.push_back(orig_pal[orig_idx[i]] == px[i] ? orig_idx[i] : static_cast<uint32_t>(lookup[px[i]]));
            new_pal = orig_pal;
        } else {
            Counts counts; std::map<Color, std::size_t> at;
            for (const Color c : px) { const auto it = at.find(c); if (it == at.end()) { at.emplace(c, counts.size()); counts.emplace_back(c, 1); } else counts[it->second].second++; }
            const long long ncol = static_cast<long long>(counts.size());
            if (ncol > p.pal_size && !quantize)
                fail(cfmt("the edit uses %lld colours but this %d-bit texture holds %d: reduce the colours in your editor (indexed/palette mode), or pass --quantize", ncol, p.bpp, p.pal_size));
            if (ncol <= p.pal_size) {
                new_pal = orig_pal;
                std::map<Color, std::size_t> keep;
                for (std::size_t i = 0; i < N; ++i) if (lookup.count(px[i]) && orig_pal[orig_idx[i]] == px[i]) keep[px[i]] = orig_idx[i];
                for (const auto& e : counts) if (lookup.count(e.first) && !keep.count(e.first)) keep[e.first] = lookup[e.first];
                std::set<std::size_t> used; for (const auto& kv : keep) used.insert(kv.second);
                std::vector<std::size_t> free; for (std::size_t k = 0; k < static_cast<std::size_t>(p.pal_size); ++k) if (!used.count(k)) free.push_back(k);
                std::vector<Color> fresh; for (const auto& e : counts) if (!keep.count(e.first)) fresh.push_back(e.first);
                if (fresh.size() <= free.size()) {
                    for (std::size_t k = 0; k < fresh.size(); ++k) { new_pal[free[k]] = fresh[k]; keep[fresh[k]] = free[k]; }
                } else {
                    new_pal.clear(); for (const auto& e : counts) new_pal.push_back(e.first);
                    new_pal.resize(static_cast<std::size_t>(p.pal_size), 0);
                    keep.clear(); for (std::size_t k = 0; k < counts.size(); ++k) keep[counts[k].first] = k;
                }
                for (const Color c : px) vals.push_back(static_cast<uint32_t>(keep[c]));
            } else {
                const Quant q = median_cut(counts, p.pal_size);
                new_pal = q.pal; new_pal.resize(static_cast<std::size_t>(p.pal_size), 0);
                for (const Color c : px) vals.push_back(static_cast<uint32_t>(q.mapping.at(c)));
                notes.push_back(cfmt("quantised %lld colours down to %zu (median cut)", ncol, q.pal.size()));
            }
        }
        if (new_pal != orig_pal) {
            const long long n = std::min<long long>(p.pal_size, p.clut_colors - which * p.pal_size);
            const long long es = p.clut_bpp / 8, start = p.clut_off + which * p.pal_size * es;
            const std::string stored_raw = py_slice(data, start, start + n * es);
            bool alpha_unused = p.clut_bpp == 16;
            if (alpha_unused) for (long long i = 0; i < n; ++i) if (rd16(stored_raw, 2 * i) & 0x8000) alpha_unused = false;
            for (long long k = 0; k < p.pal_size; ++k) {
                const long long s = p.swizzled ? swizzle(k) : k;
                if (s >= n) continue;
                if (new_pal[static_cast<std::size_t>(k)] == orig_pal[static_cast<std::size_t>(k)]) continue;
                const uint32_t v = encode_color(new_pal[static_cast<std::size_t>(k)], p.clut_bpp, alpha_unused);
                for (long long b = 0; b < es; ++b) buf[static_cast<std::size_t>(start + s * es + b)] = static_cast<char>((v >> (8 * b)) & 0xFF);
            }
            long long diff = 0; for (std::size_t k = 0; k < std::min(new_pal.size(), orig_pal.size()); ++k) if (new_pal[k] != orig_pal[k]) ++diff;
            notes.push_back(cfmt("palette %lld rewritten (%lld entries changed)", which, diff));
        }
    }
    write_level(buf, p, 0, vals);
    if (p.mips > 1 && changed) {
        if (keep_mips) notes.push_back(cfmt("kept the original %d smaller mip levels (they still show the old art far away)", p.mips - 1));
        else {
            for (int k = 1; k < p.mips; ++k) { const Level& L = p.levels[static_cast<std::size_t>(k)]; write_level(buf, p, k, nearest_level(vals, w, h, L.w, L.h)); }
            notes.push_back(cfmt("regenerated %d mip level(s) from your image", p.mips - 1));
        }
    }
    notes.insert(notes.begin(), cfmt("%lld of %lld pixels changed", changed, w * h));
    return {buf, notes};
}

// --------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------
inline std::string read_file(const std::string& path, const std::string& what = "file") {
    if (!is_file(path)) fail(what + " not found: " + path);
    return read_bytes(path);
}
inline void write_file(const std::string& path, const std::string& data) { write_text(path, data); }

inline std::string order_of(const Picture& p) { return p.swizzled ? "CSM1 swizzled" : "linear"; }
inline Val pic_info(const Picture& p) {
    Val d = dict_of({{"index", Val::integer(p.index)}, {"offset", Val::integer(p.offset)}, {"width", Val::integer(p.width)}, {"height", Val::integer(p.height)},
                     {"type", Val::str(type_name(p.image_type))}, {"bpp", Val::integer(p.bpp)}, {"mipmaps", Val::integer(p.mips)},
                     {"image_bytes", Val::integer(p.image_size)}, {"header_bytes", Val::integer(p.header_size)}, {"tex0", tex0_fields(p.tex0)},
                     {"tex0_raw", Val::str(cfmt("0x%016llX", static_cast<unsigned long long>(p.tex0)))},
                     {"tex1_raw", Val::str(cfmt("0x%016llX", static_cast<unsigned long long>(p.tex1)))},
                     {"gs_regs", Val::str(cfmt("0x%08X", p.regs))}, {"texclut", Val::str(cfmt("0x%08X", p.texclut))}});
    if (p.indexed) {
        d.d->set("palette_format", Val::str(clut_format(p.clut_type & 0x3F))); d.d->set("palette_colors", Val::integer(p.clut_colors));
        d.d->set("palettes", Val::integer(p.palettes)); d.d->set("palette_order", Val::str(order_of(p)));
        d.d->set("clut_type", Val::str(cfmt("0x%02X", p.clut_type)));
    }
    return d;
}

inline int cmd_info(const Val& a) {
    const std::string file = S(a, "file");
    const std::string data = read_file(file, "TIM2");
    const auto [hd, pics] = parse_tim2(data, 0, basename(file));
    if (B(a, "json")) {
        List l; for (const auto& p : pics) l.push_back(pic_info(p));
        out(dumps2(dict_of({{"file", Val::str(basename(file))}, {"version", Val::integer(hd.version)}, {"align", Val::integer(hd.align)},
                            {"size", Val::integer(static_cast<long long>(data.size()))}, {"pictures", Val::list(std::move(l))}})));
        return 0;
    }
    out(basename(file) + cfmt(": TIM2 version %d, %d picture(s), %d-byte alignment, %zu bytes", hd.version, hd.count, hd.align, data.size()));
    for (const auto& p : pics) {
        const Val t = tex0_fields(p.tex0);
        out(cfmt("\n  picture %lld at 0x%llX: %dx%d ", p.index, static_cast<unsigned long long>(p.offset), p.width, p.height) + type_name(p.image_type) +
            (p.mips > 1 ? cfmt(", %d mip levels", p.mips) : std::string()));
        if (p.indexed)
            out(cfmt("    palette   %d x ", p.clut_colors) + clut_format(p.clut_type & 0x3F) + cfmt(" colours (%d palette(s)), ", p.palettes) + order_of(p) + " order");
        const long long tw = get(t, "TW").i, th = get(t, "TH").i;
        out("    TEX0      PSM " + S(t, "PSM") + cfmt(", TW %lld (%lld px), TH %lld (%lld px), TBW %lld, CPSM ", tw, 1LL << tw, th, 1LL << th, get(t, "TBW").i) +
            S(t, "CPSM") + cfmt(", CSM %lld", get(t, "CSM").i));
    }
    if (static_cast<long long>(data.size()) > hd.end) out(cfmt("\n  %lld extra bytes after the last picture", static_cast<long long>(data.size()) - hd.end));
    return 0;
}

// os.path.splitext(path)[0]
inline std::string splitext0(const std::string& path) {
    const auto sep = path.find_last_of(SEP_CHARS);
    const std::size_t b = sep == std::string::npos ? 0 : sep + 1;
    const auto dot = path.rfind('.');
    if (dot == std::string::npos || dot < b) return path;
    if (path.find_first_not_of('.', b) >= dot) return path;   // leading dots of the base name do not start an extension
    return path.substr(0, dot);
}
inline bool tm2_name(const std::string& f) { const std::string l = lower(f); return ends_with(l, ".tm2") || ends_with(l, ".tim2"); }
// os.walk: top-down, symlinked folders listed but not entered; (path, path relative to the top)
inline void walk(const std::string& dir, const std::string& rel, std::vector<std::pair<std::string, std::string>>& o) {
    std::error_code ec; std::vector<std::string> files, dirs;
    for (std::filesystem::directory_iterator it(fspath(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = u8(it->path().filename());
        std::error_code e2; const bool link = it->is_symlink(e2); const bool d = it->is_directory(e2);
        if (d) { if (!link) dirs.push_back(n); }
        else files.push_back(n);
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) if (tm2_name(f)) o.emplace_back(path_join(dir, f), rel.empty() ? f : rel + SEP + f);
    for (const auto& d : dirs) walk(path_join(dir, d), rel.empty() ? d : rel + SEP + d, o);
}

inline int cmd_topng(const Val& a) {
    const std::string input = S(a, "input"), outdir = S(a, "out");
    const bool isdir = is_dir(input);
    std::vector<std::pair<std::string, std::string>> files;   // (path, rel)
    if (isdir) { walk(input, "", files); std::sort(files.begin(), files.end()); }
    else files.emplace_back(input, basename(input));
    if (files.empty()) fail("no .tm2 files under " + input);
    makedirs(outdir);
    const long long palette_arg = get(a, "palette").i;
    const bool all_pal = B(a, "all_palettes"), mips = B(a, "mips"), as_rgba = B(a, "rgba");
    std::vector<std::string> written, problems;
    for (const auto& [path, rel] : files) {
        const std::string stem = splitext0(rel);
        try {
            const std::string data = read_file(path, "TIM2");
            const auto pics = parse_tim2(data, 0, rel).second;
            for (const auto& p : pics) {
                std::vector<long long> pals;
                if (all_pal && p.indexed) for (int k = 0; k < p.palettes; ++k) pals.push_back(k);
                else pals.push_back(p.indexed ? palette_arg : 0);
                for (int lv = 0; lv < (mips ? p.mips : 1); ++lv)
                    for (const long long pal_i : pals) {
                        const Image im = decode_picture(data, p, lv, pal_i);
                        std::string name = stem + (pics.size() > 1 ? cfmt("_%lld", p.index) : std::string());
                        if (p.indexed && (all_pal || pal_i)) name += cfmt("_pal%lld", pal_i);
                        if (lv) name += cfmt("_mip%d", lv);
                        const std::string dst = path_join(outdir, name + ".png");
                        const std::string dd = dirname(dst); makedirs(dd.empty() ? std::string(".") : dd);
                        write_file(dst, encode_png(im, im.has_idx && !as_rgba));
                        written.push_back(dst);
                    }
            }
        } catch (const InputError& e) {
            if (!isdir) throw;
            problems.push_back(rel + ": " + e.what());
        }
    }
    for (const auto& wpath : written) out("wrote " + wpath);
    std::fflush(stdout);
    for (const auto& prob : problems) std::fprintf(stderr, "skipped %s\n", prob.c_str());
    out(cfmt("%zu PNG(s) from %zu file(s). Repaint them with your own art, keep the size, then: tim2.py frompng", written.size(), files.size()));
    const std::string pat = R"(_\d+(_pal\d+)?(_mip\d+)?\.png$)";
    const re::Regex& rx = re::compile(pat);
    for (const auto& x : written) if (rx.search(x)) { out("(name_N.png is picture N of a multi-picture file: frompng --picture N; _palN: --palette N)"); break; }
    return problems.empty() ? 0 : 1;
}

inline int cmd_frompng(const Val& a) {
    const std::string original = S(a, "original"), pngp = S(a, "png"), o = S(a, "out");
    const std::string data = read_file(original, "TIM2");
    const auto pics = parse_tim2(data, 0, basename(original)).second;
    const long long picture = get(a, "picture").i, pal = get(a, "palette").i;
    if (!(0 <= picture && picture < static_cast<long long>(pics.size()))) fail(cfmt("picture %lld does not exist (the file has %zu)", picture, pics.size()));
    const Picture& p = pics[static_cast<std::size_t>(picture)];
    if (p.indexed && !(0 <= pal && pal < p.palettes)) fail(cfmt("palette %lld does not exist (picture %lld has %d)", pal, p.index, p.palettes));
    const Image png = decode_png(read_file(pngp, "PNG"), basename(pngp));
    const auto [nw, notes] = rebuild(data, p, png, p.indexed ? pal : 0, B(a, "quantize"), B(a, "keep_mips"));
    if (abspath(o) == abspath(original) && !B(a, "force")) fail("--out is the original file: write a new file (or pass --force; keep a backup)");
    if (exists(o) && !B(a, "force")) fail(o + " exists: pass --force to overwrite it");
    makedirs(dirname(abspath(o)));
    write_file(o, nw);
    out("wrote " + o + cfmt(" (%zu bytes, same size and format as ", nw.size()) + basename(original) + "; GS registers unchanged)");
    for (const auto& n : notes) out("  " + n);
    return 0;
}

// a file read with seeks (scan and insert stream big files)
struct RFile {
    std::FILE* f = nullptr;
    explicit RFile(const std::string& p) { f = open_file(p, "rb"); if (!f) throw OSError(errno ? errno : ENOENT, p); }
    ~RFile() { if (f) std::fclose(f); }
    RFile(const RFile&) = delete; RFile& operator=(const RFile&) = delete;
    void seek(long long at) {
        if (at < 0) throw PyErr("OSError", "[Errno 22] Invalid argument");
#ifdef _WIN32
        _fseeki64(f, at, SEEK_SET);
#else
        fseeko(f, static_cast<off_t>(at), SEEK_SET);
#endif
    }
    std::string read(long long n) { std::string s(static_cast<std::size_t>(std::max(0LL, n)), '\0'); const std::size_t got = std::fread(s.data(), 1, s.size(), f); s.resize(got); return s; }
};
inline long long file_size(const std::string& p) { std::error_code ec; const auto n = std::filesystem::file_size(fspath(p), ec); return ec ? 0 : static_cast<long long>(n); }

struct Blob { long long off, size; Header hd; std::vector<Picture> pics; };
inline std::optional<Blob> probe(RFile& f, long long off, long long size, int fmt_id, int count) {
    long long pos = off + (fmt_id == 1 ? 0x80 : 0x10);
    long long total_all = pos - off;
    for (int k = 0; k < count; ++k) {
        if (pos + 0x30 > size) return std::nullopt;
        f.seek(pos);
        const std::string h = f.read(0x30);
        if (h.size() < 0x30) return std::nullopt;
        const long long total = rd32(h, 0), clut_size = rd32(h, 4), image_size = rd32(h, 8), header_size = rd16(h, 12);
        const int image_type = static_cast<int>(rd8(h, 0x13)); const long long width = rd16(h, 0x14), height = rd16(h, 0x16);
        if (!type_name(image_type) || header_size < 0x30 || width == 0 || height == 0) return std::nullopt;
        if (total < header_size + image_size + clut_size || pos + total > size) return std::nullopt;
        if (image_size < (width * height * type_bpp(image_type) + 7) / 8) return std::nullopt;
        pos += total; total_all += total;
    }
    f.seek(off);
    const std::string data = f.read(total_all);
    try {
        auto [hd, pics] = parse_tim2(data, 0, "blob");
        return Blob{off, total_all, hd, std::move(pics)};
    } catch (const InputError&) { return std::nullopt; }
}
inline std::vector<Blob> find_blobs(const std::string& path, long long limit = 0) {
    const long long size = file_size(path);
    std::vector<Blob> found;
    RFile f(path);
    const long long chunk_n = std::max(CHUNK, 64LL);
    long long pos = 0, skip_until = 0; std::string tail;
    while (pos < size) {
        f.seek(pos);
        const std::string block = f.read(chunk_n);
        if (block.empty()) break;
        const std::string buf = tail + block;
        const long long base = pos - static_cast<long long>(tail.size());
        for (std::size_t i = buf.find(MAGIC); i != std::string::npos; i = buf.find(MAGIC, i + 1)) {
            const long long off = base + static_cast<long long>(i);
            if (off < skip_until) continue;
            f.seek(off);
            const std::string head = f.read(16);
            std::optional<Blob> blob;
            if (head.size() == 16) {
                const int fmt_id = static_cast<int>(rd8(head, 5)), count = static_cast<int>(rd16(head, 6));
                if ((fmt_id == 0 || fmt_id == 1) && 0 < count && count <= 4096) blob = probe(f, off, size, fmt_id, count);
            }
            if (blob) {
                skip_until = off + blob->size;
                found.push_back(std::move(*blob));
                if (limit && static_cast<long long>(found.size()) >= limit) return found;
            }
        }
        tail = buf.size() >= 3 ? buf.substr(buf.size() - 3) : buf;
        pos += static_cast<long long>(block.size());
    }
    return found;
}

inline int cmd_scan(const Val& a) {
    const std::string file = S(a, "file");
    if (!is_file(file)) fail("file not found: " + file);
    const auto blobs = find_blobs(file, get(a, "limit").i);
    List rows;
    for (const auto& b : blobs) {
        const Picture& p = b.pics[0];
        rows.push_back(dict_of({{"offset", Val::str(cfmt("0x%llX", static_cast<unsigned long long>(b.off)))}, {"size", Val::integer(b.size)},
                                {"pictures", Val::integer(static_cast<long long>(b.pics.size()))},
                                {"first", Val::str(cfmt("%dx%d ", p.width, p.height) + type_name(p.image_type))}}));
    }
    const std::string extract = S(a, "extract");
    if (!get(a, "extract").is_none() && !extract.empty()) {
        makedirs(extract);
        const std::string stem = basename(file);
        RFile f(file);
        for (std::size_t k = 0; k < blobs.size(); ++k) {
            f.seek(blobs[k].off);
            const std::string dst = path_join(extract, stem + cfmt("_%08llX.tm2", static_cast<unsigned long long>(blobs[k].off)));
            write_file(dst, f.read(blobs[k].size));
            rows[k].d->set("extracted", Val::str(dst));
        }
    }
    const bool any = !rows.empty();
    if (B(a, "json")) { out(dumps2(dict_of({{"file", Val::str(basename(file))}, {"blobs", Val::list(rows)}}))); return any ? 0 : 1; }
    out(basename(file) + cfmt(": %zu TIM2 blob(s)", rows.size()));
    for (const auto& r : rows) {
        const Val* ex = r.d->get("extracted");
        out(cfmt("  %-12s %9lld bytes  %lld picture(s), first ", S(r, "offset").c_str(), get(r, "size").i, get(r, "pictures").i) + S(r, "first") +
            (ex ? "  -> " + ex->s : std::string()));
    }
    if (any && extract.empty()) out("Extract them with --extract DIR, edit, then put one back with: tim2.py insert");
    return any ? 0 : 1;
}

inline int cmd_insert(const Val& a) {
    const std::string offs = S(a, "offset"), container = S(a, "container"), tm2 = S(a, "tm2"), o = S(a, "out");
    const auto offv = hex_int(offs);
    if (!offv) fail("--offset is not a hex number: " + str_repr(offs));
    const long long off = *offv;
    if (!is_file(container)) fail("file not found: " + container);
    const std::string nw = read_file(tm2, "TIM2");
    const auto new_pics = parse_tim2(nw, 0, basename(tm2)).second;
    std::optional<Blob> blob;
    {
        RFile f(container);
        const long long size = file_size(container);
        if (off + 16 > size) fail(cfmt("offset 0x%llX is past the end of ", static_cast<unsigned long long>(off)) + basename(container));
        f.seek(off);
        const std::string head = f.read(16);
        if (head.compare(0, 4, MAGIC) != 0) fail(cfmt("there is no TIM2 at 0x%llX in ", static_cast<unsigned long long>(off)) + basename(container) + " (tim2.py scan lists the offsets)");
        const int fmt_id = static_cast<int>(rd8(head, 5)), count = static_cast<int>(rd16(head, 6));
        if ((fmt_id == 0 || fmt_id == 1) && count) blob = probe(f, off, size, fmt_id, count);
    }
    if (!blob) fail(cfmt("the TIM2 at 0x%llX in ", static_cast<unsigned long long>(off)) + basename(container) + " is not readable");
    if (static_cast<long long>(nw.size()) != blob->size)
        fail(basename(tm2) + cfmt(" is %zu bytes but the TIM2 at 0x%llX is %lld: it must be exactly the same size", nw.size(), static_cast<unsigned long long>(off), blob->size));
    for (std::size_t k = 0; k < std::min(blob->pics.size(), new_pics.size()); ++k) {
        const Picture &p = blob->pics[k], &q = new_pics[k];
        if (p.width != q.width || p.height != q.height || p.image_type != q.image_type || p.clut_colors != q.clut_colors)
            fail(cfmt("picture %lld differs in size or format from the one in the container", p.index));
    }
    const bool same = abspath(o) == abspath(container);
    if (same && !B(a, "force")) fail("--out is the container itself: write a new file (or pass --force; keep a backup)");
    if (exists(o) && !B(a, "force")) fail(o + " exists: pass --force to overwrite it");
    if (!same) {
        makedirs(dirname(abspath(o)));
        std::error_code ec; std::filesystem::copy_file(fspath(container), fspath(o), std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) throw OSError(ec.value(), o);
    }
    errno = 0;
    std::FILE* f = open_file(o, "r+b");
    if (!f) throw OSError(errno ? errno : EACCES, o);
#ifdef _WIN32
    _fseeki64(f, off, SEEK_SET);
#else
    fseeko(f, static_cast<off_t>(off), SEEK_SET);
#endif
    std::fwrite(nw.data(), 1, nw.size(), f); std::fclose(f);
    out("wrote " + o + cfmt(": %zu bytes at 0x%llX replaced, file size unchanged", nw.size(), static_cast<unsigned long long>(off)));
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p(PROG, "PS2 TIM2 textures: dump to PNG, repaint, rebuild at the same size and format, find them inside bigger files.");
    p.epilog = "Exit codes: 0 ok, 1 nothing found or some files skipped, 2 bad input.";
    p.subcommands("cmd", false);
    { Parser& s = p.sub("info"); s.help_line = "list the pictures in a .tm2";
      s.pos("file");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("topng"); s.help_line = "convert a .tm2 (or every .tm2 under a folder) to PNG";
      s.pos("input").help("a .tm2 file or a folder");
      s.opt({"--out"}).required().help("output folder");
      s.opt({"--rgba"}).store_true().help("write indexed textures as RGBA instead of palette PNGs");
      s.opt({"--palette"}).integer().def(Val::integer(0)).help("which palette of an indexed texture (default 0)");
      s.opt({"--all-palettes"}).store_true().help("one PNG per palette");
      s.opt({"--mips"}).store_true().help("also write the smaller mip levels"); }
    { Parser& s = p.sub("frompng"); s.help_line = "rebuild a .tm2 from your edited PNG (same size and format)";
      s.pos("original").help("the original .tm2");
      s.pos("png").help("your edited PNG, same width and height");
      s.opt({"--out"}).required().help("the new .tm2");
      s.opt({"--picture"}).integer().def(Val::integer(0)).help("which picture in a multi-picture file (default 0)");
      s.opt({"--palette"}).integer().def(Val::integer(0)).help("which palette to rewrite (default 0)");
      s.opt({"--quantize"}).store_true().help("reduce extra colours with a median cut");
      s.opt({"--keep-mips"}).store_true().help("keep the original smaller mip levels");
      s.opt({"--force"}).store_true().help("overwrite --out if it exists"); }
    { Parser& s = p.sub("scan"); s.help_line = "find TIM2 blobs inside a bigger file";
      s.pos("file");
      s.opt({"--extract"}).metavar("DIR").help("write each blob to DIR as <file>_<offset>.tm2");
      s.opt({"--limit"}).integer().def(Val::integer(0)).help("stop after N blobs");
      s.opt({"--json"}).store_true().help("machine-readable output"); }
    { Parser& s = p.sub("insert"); s.help_line = "put an edited .tm2 back into a bigger file at the same offset";
      s.pos("container").help("the archive or file that holds the texture");
      s.opt({"--offset"}).required().help("hex offset from tim2.py scan");
      s.opt({"--tm2"}).required().help("the edited .tm2 (same size as the one it replaces)");
      s.opt({"--out"}).required().help("the new container file");
      s.opt({"--force"}).store_true().help("overwrite --out if it exists"); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    return guard(std::string(PROG) + ": error", [&]() -> int {
        if (cmd == "info") return cmd_info(a);
        if (cmd == "topng") return cmd_topng(a);
        if (cmd == "frompng") return cmd_frompng(a);
        if (cmd == "scan") return cmd_scan(a);
        return cmd_insert(a);
    });
}
PACKTOOL("console-graphics/tim2", main);
}  // namespace packtools::console_tim2
