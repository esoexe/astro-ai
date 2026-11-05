// modder_img.inc - the image layer under `um sprite` and `um iwi`: the parts of Pillow and numpy the original uses,
// written in C++. RGBA 8-bit images; PNG read (every bit depth and colour type, tRNS, Adam7) and write; other formats
// (JPEG, WebP, GIF, BMP, TGA, ...) go through ffmpeg when it is installed. Resampling follows Pillow's
// ImagingResample (Lanczos/box convolution in 22-bit fixed point on premultiplied RGBa; nearest via the affine scale),
// rotate/transform, alpha_composite and paste-with-mask use Pillow's integer formulas. quantize(MEDIANCUT) is a median
// cut in the spirit of Pillow's Quant.c, not a bit-exact copy of it.
#include <complex>
#include <cstring>
#include <deque>
#include <optional>
#include <random>
#include <unordered_map>

namespace packtools::modder::img {
using namespace pyrt;
using namespace packtools::modder;

struct Image {
    long long w = 0, h = 0; std::vector<uint8_t> px;   // RGBA rows
    Image() = default;
    Image(long long W, long long H, std::array<uint8_t, 4> fill = {0, 0, 0, 0}) : w(W), h(H), px(static_cast<std::size_t>(std::max(0LL, W) * std::max(0LL, H)) * 4) {
        for (std::size_t i = 0; i < px.size(); i += 4) for (int k = 0; k < 4; ++k) px[i + static_cast<std::size_t>(k)] = fill[static_cast<std::size_t>(k)];
    }
    uint8_t* at(long long x, long long y) { return &px[static_cast<std::size_t>((y * w + x) * 4)]; }
    const uint8_t* at(long long x, long long y) const { return &px[static_cast<std::size_t>((y * w + x) * 4)]; }
};
struct Gray { long long w = 0, h = 0; std::vector<uint8_t> px; };
using Box = std::array<long long, 4>;   // (left, upper, right, lower)

// ------------------------------------------------------------------ PNG
inline const std::string PNG_SIG("\x89PNG\r\n\x1a\n", 8);
inline uint32_t be32(const std::string& s, std::size_t at) {
    uint32_t v = 0; for (int k = 0; k < 4; ++k) v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<std::size_t>(k)]); return v;
}
inline std::string be32s(uint32_t v) { std::string o(4, '\0'); for (int k = 0; k < 4; ++k) o[static_cast<std::size_t>(k)] = static_cast<char>((v >> (24 - 8 * k)) & 0xFF); return o; }
inline std::string png_chunk(const std::string& kind, const std::string& payload) {
    return be32s(static_cast<uint32_t>(payload.size())) + kind + payload + be32s(zip::crc32(kind + payload));
}
inline int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}
inline void unfilter(std::string& cur, const std::string& prev, unsigned ft, std::size_t bpp, const std::string& name) {
    auto U = [](const std::string& s, std::size_t i) { return static_cast<int>(static_cast<unsigned char>(s[i])); };
    const std::size_t st = cur.size();
    if (ft == 0) return;
    if (ft == 1) { for (std::size_t i = bpp; i < st; ++i) cur[i] = static_cast<char>((U(cur, i) + U(cur, i - bpp)) & 0xFF); }
    else if (ft == 2) { for (std::size_t i = 0; i < st; ++i) cur[i] = static_cast<char>((U(cur, i) + U(prev, i)) & 0xFF); }
    else if (ft == 3) { for (std::size_t i = 0; i < st; ++i) { const int l = i >= bpp ? U(cur, i - bpp) : 0; cur[i] = static_cast<char>((U(cur, i) + ((l + U(prev, i)) >> 1)) & 0xFF); } }
    else if (ft == 4) {
        for (std::size_t i = 0; i < st; ++i) {
            const int l = i >= bpp ? U(cur, i - bpp) : 0, ul = i >= bpp ? U(prev, i - bpp) : 0;
            cur[i] = static_cast<char>((U(cur, i) + paeth(l, U(prev, i), ul)) & 0xFF);
        }
    } else die("cannot identify image file '" + name + "' (unknown PNG row filter)");
}
// PNG bytes -> RGBA, converted the way Image.open(p).convert("RGBA") does (tRNS applied, 16-bit -> high byte)
inline Image decode_png(const std::string& blob, const std::string& name) {
    auto bad = [&](const std::string& why) -> void { die("cannot read image '" + name + "': " + why); };
    if (blob.size() < 8 || blob.compare(0, 8, PNG_SIG) != 0) bad("not a PNG");
    std::size_t pos = 8; bool have_ihdr = false; uint32_t W = 0, H = 0; int depth = 0, ctype = 0, interlace = 0;
    std::vector<std::array<uint8_t, 4>> plte; std::string trns, idat;
    while (pos + 12 <= blob.size()) {
        const uint32_t len = be32(blob, pos); const std::string kind = blob.substr(pos + 4, 4);
        if (pos + 12 + len > blob.size()) bad("truncated chunk " + kind);
        const std::string payload = blob.substr(pos + 8, len);
        if (kind == "IHDR") {
            if (payload.size() != 13) bad("bad IHDR");
            have_ihdr = true; W = be32(payload, 0); H = be32(payload, 4);
            depth = static_cast<unsigned char>(payload[8]); ctype = static_cast<unsigned char>(payload[9]); interlace = static_cast<unsigned char>(payload[12]);
        } else if (kind == "PLTE") {
            plte.clear(); for (std::size_t i = 0; i + 2 < payload.size(); i += 3)
                plte.push_back({static_cast<uint8_t>(payload[i]), static_cast<uint8_t>(payload[i + 1]), static_cast<uint8_t>(payload[i + 2]), 255});
        } else if (kind == "tRNS") trns = payload;
        else if (kind == "IDAT") idat += payload;
        else if (kind == "IEND") break;
        pos += 12 + len;
    }
    if (!have_ihdr) bad("no IHDR");
    const int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch) bad("unknown colour type");
    const bool depth_ok = (ctype == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) || (ctype == 3 && depth >= 1 && depth <= 8 && (depth & (depth - 1)) == 0) ||
                          ((ctype == 2 || ctype == 4 || ctype == 6) && (depth == 8 || depth == 16));
    if (!depth_ok) bad("unsupported bit depth");
    if (ctype == 3 && plte.empty()) bad("palette PNG without a palette");
    if (static_cast<uint64_t>(W) * H > (1ull << 28)) bad("image too large");
    std::string raw;
    try { raw = zip::zlib_decompress(idat); } catch (const PyErr&) { bad("corrupt image data"); }
    for (std::size_t i = 0; i < trns.size() && i < plte.size() && ctype == 3; ++i) plte[i][3] = static_cast<uint8_t>(trns[i]);
    const int bits = ch * depth; const std::size_t bpp = static_cast<std::size_t>(std::max(1, bits / 8));
    Image im(W, H);
    // tRNS key colour for grey / RGB (compared at the file's bit depth)
    const bool key_grey = ctype == 0 && trns.size() >= 2, key_rgb = ctype == 2 && trns.size() >= 6;
    auto sample = [&](const std::string& row, uint64_t x, int c) -> unsigned {   // one channel value at the file depth
        if (depth == 8) return static_cast<unsigned char>(row[static_cast<std::size_t>(x * static_cast<uint64_t>(ch) + static_cast<uint64_t>(c))]);
        if (depth == 16) { const std::size_t at = static_cast<std::size_t>((x * static_cast<uint64_t>(ch) + static_cast<uint64_t>(c)) * 2); return (static_cast<unsigned>(static_cast<unsigned char>(row[at])) << 8) | static_cast<unsigned char>(row[at + 1]); }
        const uint64_t bit = x * static_cast<uint64_t>(depth); const unsigned byte = static_cast<unsigned char>(row[static_cast<std::size_t>(bit / 8)]);
        return (byte >> (8 - depth - static_cast<int>(bit % 8))) & ((1u << depth) - 1);
    };
    auto to8 = [&](unsigned v) -> uint8_t { return static_cast<uint8_t>(depth == 16 ? v >> 8 : depth == 8 ? v : depth == 4 ? v * 17 : depth == 2 ? v * 85 : v * 255); };
    auto put_row = [&](const std::string& row, uint64_t y, uint64_t x0, uint64_t dx, uint64_t n) {
        for (uint64_t k = 0; k < n; ++k) {
            uint8_t* o = im.at(static_cast<long long>(x0 + k * dx), static_cast<long long>(y));
            if (ctype == 3) {
                const unsigned i = sample(row, k, 0);
                if (i >= plte.size()) bad("palette index out of range");
                for (int c = 0; c < 4; ++c) o[c] = plte[i][static_cast<std::size_t>(c)];
            } else if (ctype == 0 || ctype == 4) {
                const unsigned g = sample(row, k, 0); const uint8_t v = to8(g);
                o[0] = o[1] = o[2] = v; o[3] = ctype == 4 ? to8(sample(row, k, 1)) : 255;
                if (key_grey && g == ((static_cast<unsigned>(static_cast<unsigned char>(trns[0])) << 8) | static_cast<unsigned char>(trns[1]))) o[3] = 0;
            } else {
                unsigned v[4] = {0, 0, 0, 0}; for (int c = 0; c < ch; ++c) v[c] = sample(row, k, c);
                for (int c = 0; c < 3; ++c) o[c] = to8(v[c]); o[3] = ctype == 6 ? to8(v[3]) : 255;
                if (key_rgb) {
                    bool eq = true; for (int c = 0; c < 3; ++c) if (v[c] != ((static_cast<unsigned>(static_cast<unsigned char>(trns[static_cast<std::size_t>(2 * c)])) << 8) | static_cast<unsigned char>(trns[static_cast<std::size_t>(2 * c + 1)]))) eq = false;
                    if (eq) o[3] = 0;
                }
            }
        }
    };
    std::size_t at = 0;
    auto pass = [&](uint64_t x0, uint64_t y0, uint64_t dx, uint64_t dy) {
        if (x0 >= W || y0 >= H) return;
        const uint64_t pw = (W - x0 + dx - 1) / dx, ph = (H - y0 + dy - 1) / dy;
        const std::size_t st = static_cast<std::size_t>((pw * static_cast<uint64_t>(bits) + 7) / 8);
        std::string prev(st, '\0');
        for (uint64_t r = 0; r < ph; ++r) {
            if (at + 1 + st > raw.size()) bad("image data is truncated");
            const unsigned ft = static_cast<unsigned char>(raw[at]); std::string cur = raw.substr(at + 1, st); at += 1 + st;
            unfilter(cur, prev, ft, bpp, name);
            put_row(cur, y0 + r * dy, x0, dx, pw); prev.swap(cur);
        }
    };
    if (interlace) { pass(0, 0, 8, 8); pass(4, 0, 8, 8); pass(0, 4, 4, 8); pass(2, 0, 4, 4); pass(0, 2, 2, 4); pass(1, 0, 2, 2); pass(0, 1, 1, 2); }
    else pass(0, 0, 1, 1);
    return im;
}
inline std::string filter_rows(const std::vector<std::string>& rows, std::size_t bpp) {   // per-row filter by the minimum-sum heuristic
    std::string out; std::string prev(rows.empty() ? 0 : rows[0].size(), '\0');
    for (const auto& r : rows) {
        const std::size_t n = r.size(); std::string best; long long best_sum = -1;
        for (int ft = 0; ft < 5; ++ft) {
            std::string f(n + 1, '\0'); f[0] = static_cast<char>(ft); long long sum = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const int x = static_cast<unsigned char>(r[i]), a = i >= bpp ? static_cast<unsigned char>(r[i - bpp]) : 0, b = static_cast<unsigned char>(prev[i]), c = i >= bpp ? static_cast<unsigned char>(prev[i - bpp]) : 0;
                const int pred = ft == 0 ? 0 : ft == 1 ? a : ft == 2 ? b : ft == 3 ? (a + b) >> 1 : paeth(a, b, c);
                const int v = (x - pred) & 0xFF; f[i + 1] = static_cast<char>(v); sum += v < 128 ? v : 256 - v;
            }
            if (best_sum < 0 || sum < best_sum) { best_sum = sum; best.swap(f); }
        }
        out += best; prev = r;
    }
    return out;
}
inline std::string encode_png_raw(long long w, long long h, int ctype, const std::vector<uint8_t>& px) {
    const int ch = ctype == 6 ? 4 : ctype == 2 ? 3 : 1;
    std::vector<std::string> rows; rows.reserve(static_cast<std::size_t>(h));
    for (long long y = 0; y < h; ++y) rows.emplace_back(reinterpret_cast<const char*>(px.data()) + static_cast<std::size_t>(y * w * ch), static_cast<std::size_t>(w * ch));
    const std::string hd = be32s(static_cast<uint32_t>(w)) + be32s(static_cast<uint32_t>(h)) + std::string{8, static_cast<char>(ctype), 0, 0, 0};
    return PNG_SIG + png_chunk("IHDR", hd) + png_chunk("IDAT", zip::zlib_compress(filter_rows(rows, static_cast<std::size_t>(ch)))) + png_chunk("IEND", "");
}

// ------------------------------------------------------------------ load / save
inline std::string ext_lower(const std::string& p) { return lower(u8native(P(p).extension())); }
inline Image load(const std::string& path) {
    if (!isfile(path)) die("cannot open image: " + path + " (no such file)");
    const std::string blob = slurp(path);
    if (blob.size() >= 8 && blob.compare(0, 8, PNG_SIG) == 0) return decode_png(blob, path);
    if (which_tool("ffmpeg").empty())
        die("cannot identify image file '" + path + "': PNG is read built in; install ffmpeg to read JPEG, WebP, GIF, BMP and other formats");
    const auto r = mk::run_proc({which_tool("ffmpeg"), "-v", "error", "-nostdin", "-i", path, "-frames:v", "1", "-pix_fmt", "rgba", "-f", "image2pipe", "-vcodec", "png", "-"});
    if (r.code != 0 || r.out.size() < 8) die("cannot identify image file '" + path + "' (ffmpeg could not decode it)");
    return decode_png(r.out, path);
}
inline void save_bytes_as(const std::string& path, const std::string& png) {
    const std::string e = ext_lower(path);
    if (e == ".png") { spit(path, png); return; }
    if (which_tool("ffmpeg").empty()) die("cannot save " + path + ": PNG is written built in; install ffmpeg for other formats (or save as .png)");
    const std::string tmp = path + ".um-tmp.png"; spit(tmp, png);
    const auto r = mk::run_proc({which_tool("ffmpeg"), "-v", "error", "-nostdin", "-y", "-i", tmp, "-frames:v", "1", "-update", "1", path});
    std::error_code ec; fs::remove(P(tmp), ec);
    if (r.code != 0) die("ffmpeg could not write " + path + ": " + strip(decode_utf8_replace(r.err)));
}
inline void save(const Image& im, const std::string& path) { save_bytes_as(path, encode_png_raw(im.w, im.h, 6, im.px)); }
inline void save_gray(const Gray& g, const std::string& path) { save_bytes_as(path, encode_png_raw(g.w, g.h, 0, g.px)); }

// ------------------------------------------------------------------ basic ops
inline std::optional<Box> getbbox(const Image& im) {   // alpha-only, like Image.getbbox() on RGBA
    long long x0 = im.w, y0 = im.h, x1 = -1, y1 = -1;
    for (long long y = 0; y < im.h; ++y) for (long long x = 0; x < im.w; ++x) if (im.at(x, y)[3]) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 < 0) return std::nullopt;
    return Box{x0, y0, x1 + 1, y1 + 1};
}
inline Image crop(const Image& im, const Box& b) {   // outside the image reads as transparent black
    Image o(std::max(0LL, b[2] - b[0]), std::max(0LL, b[3] - b[1]));
    for (long long y = 0; y < o.h; ++y) for (long long x = 0; x < o.w; ++x) {
        const long long sx = b[0] + x, sy = b[1] + y;
        if (sx >= 0 && sy >= 0 && sx < im.w && sy < im.h) std::memcpy(o.at(x, y), im.at(sx, sy), 4);
    }
    return o;
}
inline void paste(Image& dst, const Image& src, long long ox, long long oy) {   // plain paste: copies RGBA, clipped
    for (long long y = 0; y < src.h; ++y) { const long long dy = oy + y; if (dy < 0 || dy >= dst.h) continue;
        for (long long x = 0; x < src.w; ++x) { const long long dx = ox + x; if (dx < 0 || dx >= dst.w) continue; std::memcpy(dst.at(dx, dy), src.at(x, y), 4); } }
}
inline unsigned div255(unsigned a) { const unsigned t = a + 128; return ((t >> 8) + t) >> 8; }
inline void paste_masked(Image& dst, const Image& src, long long ox, long long oy) {   // paste(im, box, im): the alpha as the mask
    for (long long y = 0; y < src.h; ++y) { const long long dy = oy + y; if (dy < 0 || dy >= dst.h) continue;
        for (long long x = 0; x < src.w; ++x) { const long long dx = ox + x; if (dx < 0 || dx >= dst.w) continue;
            const uint8_t* s = src.at(x, y); uint8_t* d = dst.at(dx, dy); const unsigned m = s[3];
            for (int k = 0; k < 4; ++k) d[k] = static_cast<uint8_t>(div255(d[k] * (255 - m) + s[k] * m)); } }
}
inline void alpha_composite(Image& dst, const Image& src) {   // Image.alpha_composite (same size)
    const unsigned PB = 7;
    auto sh = [](unsigned a) { return ((a >> 8) + a) >> 8; };
    for (std::size_t i = 0; i < dst.px.size() && i < src.px.size(); i += 4) {
        const uint8_t* s = &src.px[i]; uint8_t* d = &dst.px[i];
        if (s[3] == 0) continue;
        const unsigned blend = d[3] * (255u - s[3]), outa255 = s[3] * 255u + blend;
        const unsigned coef1 = static_cast<unsigned>(static_cast<unsigned long long>(s[3]) * 255u * 255u * (1u << PB) / outa255), coef2 = 255u * (1u << PB) - coef1;
        for (int k = 0; k < 3; ++k) { const unsigned t = s[k] * coef1 + d[k] * coef2; d[k] = static_cast<uint8_t>(sh(t + (0x80u << PB)) >> PB); }
        d[3] = static_cast<uint8_t>(sh(outa255 + 0x80));
    }
}
inline Image mirror(const Image& im) { Image o(im.w, im.h); for (long long y = 0; y < im.h; ++y) for (long long x = 0; x < im.w; ++x) std::memcpy(o.at(im.w - 1 - x, y), im.at(x, y), 4); return o; }
inline Image flip(const Image& im) { Image o(im.w, im.h); for (long long y = 0; y < im.h; ++y) for (long long x = 0; x < im.w; ++x) std::memcpy(o.at(x, im.h - 1 - y), im.at(x, y), 4); return o; }
inline Image transpose_rot(const Image& im, int quarter) {   // ROTATE_90 / 180 / 270 (counter-clockwise)
    if (quarter == 2) { Image o(im.w, im.h); for (long long y = 0; y < im.h; ++y) for (long long x = 0; x < im.w; ++x) std::memcpy(o.at(im.w - 1 - x, im.h - 1 - y), im.at(x, y), 4); return o; }
    Image o(im.h, im.w);
    for (long long y = 0; y < im.h; ++y) for (long long x = 0; x < im.w; ++x) {
        if (quarter == 1) std::memcpy(o.at(y, im.w - 1 - x), im.at(x, y), 4);   // 90: (x, y) -> (y, w-1-x)
        else std::memcpy(o.at(im.h - 1 - y, x), im.at(x, y), 4);                 // 270
    }
    return o;
}
inline int coord(double v) { return v < 0.0 ? -1 : static_cast<int>(v); }
inline Image affine_nearest(const Image& im, long long W, long long H, const std::array<double, 6>& a) {   // fill: transparent
    Image o(W, H);
    if (a[1] == 0 && a[3] == 0) {   // ImagingScaleAffine
        double yo = a[5] + a[4] * 0.5;
        std::vector<int> xin(static_cast<std::size_t>(std::max(0LL, W))); double xo = a[2] + a[0] * 0.5;
        for (long long x = 0; x < W; ++x) { xin[static_cast<std::size_t>(x)] = coord(xo); xo += a[0]; }
        for (long long y = 0; y < H; ++y, yo += a[4]) { const int yi = coord(yo); if (yi < 0 || yi >= im.h) continue;
            for (long long x = 0; x < W; ++x) { const int xi = xin[static_cast<std::size_t>(x)]; if (xi >= 0 && xi < im.w) std::memcpy(o.at(x, y), im.at(xi, yi), 4); } }
        return o;
    }
    double xo = a[2] + a[1] * 0.5 + a[0] * 0.5, yo = a[5] + a[4] * 0.5 + a[3] * 0.5;
    for (long long y = 0; y < H; ++y) {
        double xx = xo, yy = yo;
        for (long long x = 0; x < W; ++x) {
            const int xi = coord(xx), yi = coord(yy);
            if (xi >= 0 && xi < im.w && yi >= 0 && yi < im.h) std::memcpy(o.at(x, y), im.at(xi, yi), 4);
            xx += a[0]; yy += a[3];
        }
        xo += a[1]; yo += a[4];
    }
    return o;
}
inline Image resize_nearest(const Image& im, long long W, long long H) {
    if (W == im.w && H == im.h) return im;
    return affine_nearest(im, W, H, {static_cast<double>(im.w) / static_cast<double>(W), 0, 0, 0, static_cast<double>(im.h) / static_cast<double>(H), 0});
}
// Image.rotate(angle, resample=NEAREST, expand=...)
inline Image rotate(const Image& im, double angle, bool expand) {
    angle = std::fmod(angle, 360.0); if (angle < 0) angle += 360.0;
    if (angle == 0) return im;
    if (angle == 180) return transpose_rot(im, 2);
    if ((angle == 90 || angle == 270) && (expand || im.w == im.h)) return transpose_rot(im, angle == 90 ? 1 : 3);
    long long w = im.w, h = im.h; const double cx = static_cast<double>(w) / 2.0, cy = static_cast<double>(h) / 2.0;
    const double rad = -angle * 3.14159265358979323846 / 180.0;
    std::array<double, 6> m = {round_n(std::cos(rad), 15), round_n(std::sin(rad), 15), 0.0, round_n(-std::sin(rad), 15), round_n(std::cos(rad), 15), 0.0};
    auto tf = [&](double x, double y) { return std::pair<double, double>{m[0] * x + m[1] * y + m[2], m[3] * x + m[4] * y + m[5]}; };
    { const auto [a, b] = tf(-cx, -cy); m[2] = a; m[5] = b; }
    m[2] += cx; m[5] += cy;
    if (expand) {
        std::vector<double> xs, ys;
        for (const auto& [x, y] : std::vector<std::pair<double, double>>{{0, 0}, {static_cast<double>(w), 0}, {static_cast<double>(w), static_cast<double>(h)}, {0, static_cast<double>(h)}}) { const auto [a, b] = tf(x, y); xs.push_back(a); ys.push_back(b); }
        const long long nw = static_cast<long long>(std::ceil(*std::max_element(xs.begin(), xs.end())) - std::floor(*std::min_element(xs.begin(), xs.end())));
        const long long nh = static_cast<long long>(std::ceil(*std::max_element(ys.begin(), ys.end())) - std::floor(*std::min_element(ys.begin(), ys.end())));
        const auto [a, b] = tf(-static_cast<double>(nw - w) / 2.0, -static_cast<double>(nh - h) / 2.0); m[2] = a; m[5] = b;
        w = nw; h = nh;
    }
    return affine_nearest(im, w, h, m);
}

// ------------------------------------------------------------------ convolution resampling (Pillow's Resample.c)
inline double f_box(double x) { return (x > -0.5 && x <= 0.5) ? 1.0 : 0.0; }
inline double sinc_(double x) { if (x == 0.0) return 1.0; x *= 3.14159265358979323846; return std::sin(x) / x; }
inline double f_lanczos(double x) { return (-3.0 <= x && x < 3.0) ? sinc_(x) * sinc_(x / 3.0) : 0.0; }
struct Coeffs { std::vector<int> lo, n; std::vector<int> k; int ksize = 0; };
inline Coeffs precompute(long long in_size, long long out_size, double support0, double (*filt)(double)) {
    const double scale = static_cast<double>(in_size) / static_cast<double>(out_size), filterscale = std::max(scale, 1.0), support = support0 * filterscale;
    Coeffs c; c.ksize = static_cast<int>(std::ceil(support)) * 2 + 1;
    c.lo.resize(static_cast<std::size_t>(out_size)); c.n.resize(static_cast<std::size_t>(out_size)); c.k.assign(static_cast<std::size_t>(out_size * c.ksize), 0);
    std::vector<double> kd(static_cast<std::size_t>(c.ksize));
    for (long long xx = 0; xx < out_size; ++xx) {
        const double center = (static_cast<double>(xx) + 0.5) * scale, ss = 1.0 / filterscale; double ww = 0.0;
        int xmin = static_cast<int>(center - support + 0.5); if (xmin < 0) xmin = 0;
        int xmax = static_cast<int>(center + support + 0.5); if (xmax > in_size) xmax = static_cast<int>(in_size); xmax -= xmin;
        for (int x = 0; x < xmax; ++x) { const double wgt = filt((x + xmin - center + 0.5) * ss); kd[static_cast<std::size_t>(x)] = wgt; ww += wgt; }
        for (int x = 0; x < xmax; ++x) {
            const double v = ww != 0.0 ? kd[static_cast<std::size_t>(x)] / ww : kd[static_cast<std::size_t>(x)];
            c.k[static_cast<std::size_t>(xx * c.ksize + x)] = v < 0 ? static_cast<int>(-0.5 + v * (1 << 22)) : static_cast<int>(0.5 + v * (1 << 22));
        }
        c.lo[static_cast<std::size_t>(xx)] = xmin; c.n[static_cast<std::size_t>(xx)] = xmax;
    }
    return c;
}
inline uint8_t clip8(long long v) { if (v >= (1LL << 22) << 8) return 255; if (v <= 0) return 0; return static_cast<uint8_t>(v >> 22); }
inline Image resample_pass(const Image& im, long long W, long long H, bool horizontal, double support, double (*filt)(double)) {
    const Coeffs c = precompute(horizontal ? im.w : im.h, horizontal ? W : H, support, filt);
    Image o(W, H);
    for (long long y = 0; y < H; ++y) for (long long x = 0; x < W; ++x) {
        const long long t = horizontal ? x : y; const int lo = c.lo[static_cast<std::size_t>(t)], n = c.n[static_cast<std::size_t>(t)];
        const int* k = &c.k[static_cast<std::size_t>(t * c.ksize)];
        long long ss[4] = {1LL << 21, 1LL << 21, 1LL << 21, 1LL << 21};
        for (int i = 0; i < n; ++i) { const uint8_t* s = horizontal ? im.at(lo + i, y) : im.at(x, lo + i); for (int ch = 0; ch < 4; ++ch) ss[ch] += static_cast<long long>(s[ch]) * k[i]; }
        uint8_t* d = o.at(x, y); for (int ch = 0; ch < 4; ++ch) d[ch] = clip8(ss[ch]);
    }
    return o;
}
inline Image premultiply(Image im) {   // RGBA -> RGBa
    for (std::size_t i = 0; i < im.px.size(); i += 4) { const unsigned a = im.px[i + 3]; for (int k = 0; k < 3; ++k) im.px[i + static_cast<std::size_t>(k)] = static_cast<uint8_t>(div255(im.px[i + static_cast<std::size_t>(k)] * a)); }
    return im;
}
inline Image unpremultiply(Image im) {   // RGBa -> RGBA
    for (std::size_t i = 0; i < im.px.size(); i += 4) { const unsigned a = im.px[i + 3]; if (a == 0 || a == 255) continue;
        for (int k = 0; k < 3; ++k) { const unsigned v = 255u * im.px[i + static_cast<std::size_t>(k)] / a; im.px[i + static_cast<std::size_t>(k)] = static_cast<uint8_t>(std::min(255u, v)); } }
    return im;
}
enum class Filter { NEAREST, BOX, LANCZOS };
inline Image resize(const Image& im, long long W, long long H, Filter f) {
    if (W < 1 || H < 1) die("height and width must be > 0");
    if (W == im.w && H == im.h) return im;
    if (f == Filter::NEAREST) return resize_nearest(im, W, H);
    const double support = f == Filter::BOX ? 0.5 : 3.0; double (*fn)(double) = f == Filter::BOX ? f_box : f_lanczos;
    Image t = premultiply(im);
    if (W != t.w) t = resample_pass(t, W, t.h, true, support, fn);
    if (H != t.h) t = resample_pass(t, t.w, H, false, support, fn);
    return unpremultiply(std::move(t));
}

// ------------------------------------------------------------------ colours
inline uint32_t pack(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3]; }
// getcolors(1 << 20): (count, colour) or nullopt past 2^20 colours
inline std::optional<std::vector<std::pair<long long, uint32_t>>> getcolors(const Image& im) {
    std::unordered_map<uint32_t, long long> m;
    for (std::size_t i = 0; i < im.px.size(); i += 4) { ++m[pack(&im.px[i])]; if (m.size() > (1u << 20)) return std::nullopt; }
    std::vector<std::pair<long long, uint32_t>> v(m.begin(), m.end());
    std::vector<std::pair<long long, uint32_t>> out; out.reserve(v.size()); for (const auto& [c, n] : v) out.emplace_back(n, c);
    return out;
}
// median cut to n colours on RGB (alpha ignored), every pixel takes its box's average colour
inline void quantize_mediancut(Image& im, int n) {
    std::unordered_map<uint32_t, long long> hist;
    for (std::size_t i = 0; i < im.px.size(); i += 4) ++hist[(static_cast<uint32_t>(im.px[i]) << 16) | (static_cast<uint32_t>(im.px[i + 1]) << 8) | im.px[i + 2]];
    struct Ent { uint8_t c[3]; long long n; };
    std::vector<Ent> ents; for (const auto& [c, k] : hist) ents.push_back({{static_cast<uint8_t>(c >> 16), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c)}, k});
    std::sort(ents.begin(), ents.end(), [](const Ent& a, const Ent& b) { return std::memcmp(a.c, b.c, 3) < 0; });
    struct BoxR { std::size_t a, b; };   // [a, b) into ents
    auto range = [&](const BoxR& bx, int ch) { int lo = 255, hi = 0; for (std::size_t i = bx.a; i < bx.b; ++i) { lo = std::min<int>(lo, ents[i].c[ch]); hi = std::max<int>(hi, ents[i].c[ch]); } return hi - lo; };
    auto volume = [&](const BoxR& bx) { long long v = 1; for (int ch = 0; ch < 3; ++ch) v *= range(bx, ch) + 1; return v; };
    std::vector<BoxR> boxes{{0, ents.size()}};
    while (static_cast<int>(boxes.size()) < n) {
        std::size_t pick = boxes.size(); long long best = 1;
        for (std::size_t i = 0; i < boxes.size(); ++i) if (boxes[i].b - boxes[i].a > 1) { const long long v = volume(boxes[i]); if (v > best || pick == boxes.size()) { best = v; pick = i; } }
        if (pick == boxes.size()) break;
        BoxR bx = boxes[pick]; int ch = 0, wide = -1; for (int k = 0; k < 3; ++k) { const int r = range(bx, k); if (r > wide) { wide = r; ch = k; } }
        std::sort(ents.begin() + static_cast<long>(bx.a), ents.begin() + static_cast<long>(bx.b), [&](const Ent& x, const Ent& y) { return x.c[ch] < y.c[ch]; });
        long long total = 0; for (std::size_t i = bx.a; i < bx.b; ++i) total += ents[i].n;
        long long acc = 0; std::size_t cut = bx.a;
        for (std::size_t i = bx.a; i < bx.b; ++i) { acc += ents[i].n; if (acc * 2 >= total) { cut = i + 1; break; } }
        if (cut >= bx.b) cut = bx.b - 1; if (cut <= bx.a) cut = bx.a + 1;
        boxes[pick] = {bx.a, cut}; boxes.push_back({cut, bx.b});
    }
    std::unordered_map<uint32_t, uint32_t> map;
    for (const auto& bx : boxes) {
        double s[3] = {0, 0, 0}; long long cnt = 0;
        for (std::size_t i = bx.a; i < bx.b; ++i) { for (int k = 0; k < 3; ++k) s[k] += static_cast<double>(ents[i].c[k]) * static_cast<double>(ents[i].n); cnt += ents[i].n; }
        uint32_t avg = 0; for (int k = 0; k < 3; ++k) avg = (avg << 8) | static_cast<uint32_t>(std::min(255.0, std::floor(0.5 + s[k] / static_cast<double>(std::max(1LL, cnt)))));
        for (std::size_t i = bx.a; i < bx.b; ++i) map[(static_cast<uint32_t>(ents[i].c[0]) << 16) | (static_cast<uint32_t>(ents[i].c[1]) << 8) | ents[i].c[2]] = avg;
    }
    for (std::size_t i = 0; i < im.px.size(); i += 4) {
        const uint32_t v = map[(static_cast<uint32_t>(im.px[i]) << 16) | (static_cast<uint32_t>(im.px[i + 1]) << 8) | im.px[i + 2]];
        im.px[i] = static_cast<uint8_t>(v >> 16); im.px[i + 1] = static_cast<uint8_t>(v >> 8); im.px[i + 2] = static_cast<uint8_t>(v);
    }
}
inline std::string tup(std::initializer_list<long long> v) { std::string o = "("; bool first = true; for (long long x : v) { if (!first) o += ", "; o += std::to_string(x); first = false; } return o + ")"; }
}  // namespace packtools::modder::img
