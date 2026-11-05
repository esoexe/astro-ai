// modder_sprite.inc - `um sprite` and `um iwi`: C++ ports of universal-modder's um/sprite.py and um/iwi.py
// (Rehan and universal-modder contributors, MIT), on the image layer in modder_img.inc.
namespace packtools::modder::sprite {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::img;

inline const char* DOC =
    "2D sprite tools: turn generated art into the exact frames a game expects.\n\n"
    "    um sprite info in.png                               # size, mode, alpha coverage, bbox, colours\n"
    "    um sprite cutout in.png out.png                     # flat background -> transparent (flood fill from the border)\n"
    "    um sprite fit in.png out.png --size 64x26           # trim + nearest-neighbour fit into a frame (pixel art)\n"
    "    um sprite fit in.png out.png --size 38x34 --anchor bottom --smooth\n"
    "    um sprite pixelate in.png out.png --size 32x32 --colors 16 --outline\n"
    "    um sprite palette in.png out.png --from ref.png     # snap colours to a game's palette\n"
    "    um sprite sheet out.png f1.png f2.png ... --cols 4  # pack frames (grid); --vertical for a Terraria-style strip\n"
    "    um sprite slice sheet.png outdir --frame 32x32      # split a sheet into frames\n"
    "    um sprite frames in.png outdir --n 3 --kind bob     # quick idle animation from one frame (bob/squash/wobble/flash)\n"
    "    um sprite team-mask in.png out.png --hue blue       # saturated accent -> player-colour mask (+ desaturated sprite)\n"
    "    um sprite seamless in.png out.png                   # make a texture tile (offset + cross-blend)\n"
    "    um sprite tile-preview in.png out.png               # 3x3 tiling preview to check seams\n"
    "    um sprite preview in.png out.png --scale 4          # checkerboard + scale, for looking at it\n\n"
    "Rules of thumb (learned the hard way): generate on a flat background or with real transparency, cut\n"
    "out with a border flood fill (keeps interior whites like eyes), trim, then scale ONCE with nearest\n"
    "neighbour to the engine's frame size. Keep facing directions consistent with the engine's convention\n"
    "(e.g. Terraria items point right, NPC sprites face left). Frame layout comes from the engine's draw code.\n\n"
    "PNG is read and written built in; other formats go through ffmpeg.\n";

using RGB = std::array<int, 3>;
using RGBA = std::array<int, 4>;

inline const uint8_t* px_wrap(const Image& im, long long x, long long y) {   // PixelAccess: negative indexes wrap
    if (x < 0) x += im.w; if (y < 0) y += im.h;
    x = std::clamp(x, 0LL, im.w - 1); y = std::clamp(y, 0LL, im.h - 1);
    return im.at(x, y);
}
inline RGB border_color(const Image& im, int k = 4) {
    std::vector<int> ch[3];
    const long long corners[4][2] = {{0, 0}, {im.w - k, 0}, {0, im.h - k}, {im.w - k, im.h - k}};
    for (const auto& c : corners) for (long long x = c[0]; x < c[0] + k; ++x) for (long long y = c[1]; y < c[1] + k; ++y) {
        const uint8_t* p = px_wrap(im, x, y); for (int i = 0; i < 3; ++i) ch[i].push_back(p[i]);
    }
    RGB out{}; for (int i = 0; i < 3; ++i) { std::sort(ch[i].begin(), ch[i].end()); out[static_cast<std::size_t>(i)] = ch[i][ch[i].size() / 2]; }
    return out;
}
inline Image crop_bbox(const Image& im) { const auto b = getbbox(im); return crop(im, b ? *b : Box{0, 0, 1, 1}); }
inline int min_alpha(const Image& im) { int m = 255; for (std::size_t i = 3; i < im.px.size(); i += 4) m = std::min<int>(m, im.px[i]); return m; }

inline Image cutout(Image im, int tol, const std::optional<RGBA>& bg_in, const std::optional<long long>& grey, int spread, bool holes, double keep_top) {
    if (min_alpha(im) < 250 && !bg_in) return crop_bbox(im);   // already has transparency: just trim
    im = crop(im, {0, 0, im.w, static_cast<long long>(static_cast<double>(im.h) * keep_top)});
    const RGB bg = bg_in ? RGB{(*bg_in)[0], (*bg_in)[1], (*bg_in)[2]} : border_color(im);
    const long long w = im.w, h = im.h;
    auto is_bg = [&](const uint8_t* c) {
        const int r = c[0], g = c[1], b = c[2];
        if (std::abs(r - bg[0]) + std::abs(g - bg[1]) + std::abs(b - bg[2]) <= tol * 3) return true;
        return grey && std::min({r, g, b}) >= *grey && std::max({r, g, b}) - std::min({r, g, b}) < spread;
    };
    std::vector<uint8_t> seen(static_cast<std::size_t>(w * h));
    std::deque<std::pair<long long, long long>> q;
    for (long long x = 0; x < w; ++x) { q.emplace_back(x, 0); q.emplace_back(x, h - 1); }
    for (long long y = 0; y < h; ++y) { q.emplace_back(0, y); q.emplace_back(w - 1, y); }
    while (!q.empty()) {
        const auto [x, y] = q.front(); q.pop_front();
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        const std::size_t i = static_cast<std::size_t>(y * w + x);
        if (seen[i]) continue; seen[i] = 1;
        uint8_t* c = im.at(x, y);
        if (!is_bg(c)) continue;
        c[3] = 0;
        const long long nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
        for (const auto& n : nb) if (n[0] >= 0 && n[0] < w && n[1] >= 0 && n[1] < h && !seen[static_cast<std::size_t>(n[1] * w + n[0])]) q.emplace_back(n[0], n[1]);
    }
    if (holes) for (long long y = 0; y < h; ++y) for (long long x = 0; x < w; ++x) { uint8_t* c = im.at(x, y); if (c[3] && is_bg(c)) c[3] = 0; }
    return crop_bbox(im);
}
inline Image hard_alpha(Image im, int thresh = 128) { for (std::size_t i = 3; i < im.px.size(); i += 4) im.px[i] = im.px[i] >= thresh ? 255 : 0; return im; }
inline Image trim(const Image& im, long long pad = 0) {
    const auto b = getbbox(im); if (!b) return im;
    Image c = crop(im, *b);
    if (pad) { Image o(c.w + 2 * pad, c.h + 2 * pad); paste(o, c, pad, pad); return o; }
    return c;
}
inline Image fit(const Image& in, long long w, long long h, const std::string& anchor, bool smooth, bool allow_upscale) {
    const Image im = trim(in);
    double s = std::min(static_cast<double>(w) / static_cast<double>(im.w), static_cast<double>(h) / static_cast<double>(im.h));
    if (!allow_upscale) s = std::min(s, 1.0);
    const Image small = resize(im, std::max(1LL, round_int(static_cast<double>(im.w) * s)), std::max(1LL, round_int(static_cast<double>(im.h) * s)), smooth ? Filter::LANCZOS : Filter::NEAREST);
    Image frame(w, h);
    const long long x = floordiv(w - small.w, 2);
    const long long y = anchor == "bottom" ? h - small.h : anchor == "top" ? 0 : floordiv(h - small.h, 2);
    paste(frame, small, x, y);
    return frame;
}
inline Image add_outline(const Image& im, const RGBA& color) {
    Image out = im;
    for (long long y = 0; y < im.h; ++y) for (long long x = 0; x < im.w; ++x) {
        if (im.at(x, y)[3]) continue;
        const long long nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
        for (const auto& n : nb) if (n[0] >= 0 && n[0] < im.w && n[1] >= 0 && n[1] < im.h && im.at(n[0], n[1])[3]) {
            uint8_t* d = out.at(x, y); for (int k = 0; k < 4; ++k) d[k] = static_cast<uint8_t>(color[static_cast<std::size_t>(k)]); break;
        }
    }
    return out;
}
inline Image pixelate(const Image& in, long long w, long long h, int colors, const std::optional<RGBA>& outline, int alpha_thresh = 110) {
    Image im = trim(in);
    const double s = std::min(static_cast<double>(w) / static_cast<double>(im.w), static_cast<double>(h) / static_cast<double>(im.h));
    const long long tw = std::max(1LL, round_int(static_cast<double>(im.w) * s)), th = std::max(1LL, round_int(static_cast<double>(im.h) * s));
    // premultiply (numpy float32) so transparent pixels don't bleed their colour into edges, then a BOX resize
    for (std::size_t i = 0; i < im.px.size(); i += 4) {
        const float a = static_cast<float>(im.px[i + 3]) / 255.0f;
        for (int k = 0; k < 3; ++k) im.px[i + static_cast<std::size_t>(k)] = static_cast<uint8_t>(std::clamp(static_cast<float>(im.px[i + static_cast<std::size_t>(k)]) * a, 0.0f, 255.0f));
    }
    Image small = resize(im, tw, th, Filter::BOX);
    for (std::size_t i = 0; i < small.px.size(); i += 4) {
        const float a = static_cast<float>(small.px[i + 3]);
        for (int k = 0; k < 3; ++k) {
            const float v = a > 0 ? static_cast<float>(small.px[i + static_cast<std::size_t>(k)]) * 255.0f / std::max(a, 1.0f) : 0.0f;
            small.px[i + static_cast<std::size_t>(k)] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
        }
    }
    small = hard_alpha(small, alpha_thresh);
    if (colors) quantize_mediancut(small, colors);
    if (outline) small = add_outline(small, *outline);
    Image frame(w, h);
    paste_masked(frame, small, floordiv(w - small.w, 2), floordiv(h - small.h, 2));
    return frame;
}
inline std::vector<RGB> palette_from(const Image& ref, long long max_colors = 64) {
    auto cols = getcolors(ref); std::vector<RGB> out;
    if (!cols) return out;
    std::sort(cols->begin(), cols->end(), [](const auto& a, const auto& b) { return a > b; });   // sorted(reverse=True): count, then colour
    for (const auto& [n, c] : *cols) { if ((c & 0xFF) == 0) continue; if (static_cast<long long>(out.size()) >= max_colors) break;
        out.push_back({static_cast<int>(c >> 24), static_cast<int>((c >> 16) & 0xFF), static_cast<int>((c >> 8) & 0xFF)}); }
    return out;
}
inline Image snap_palette(Image im, const std::vector<RGB>& pal) {
    if (pal.empty()) die("the reference image has no opaque colours");
    for (std::size_t i = 0; i < im.px.size(); i += 4) {
        long long best = -1; std::size_t bi = 0;
        for (std::size_t j = 0; j < pal.size(); ++j) {
            long long d = 0; for (int k = 0; k < 3; ++k) { const long long t = static_cast<long long>(im.px[i + static_cast<std::size_t>(k)]) - pal[j][static_cast<std::size_t>(k)]; d += t * t; }
            if (best < 0 || d < best) { best = d; bi = j; }
        }
        for (int k = 0; k < 3; ++k) im.px[i + static_cast<std::size_t>(k)] = static_cast<uint8_t>(pal[bi][static_cast<std::size_t>(k)]);
    }
    return im;
}
inline Image sheet(const std::vector<Image>& frames, long long cols, long long pad, bool vertical) {
    long long fw = 0, fh = 0; for (const auto& f : frames) { fw = std::max(fw, f.w); fh = std::max(fh, f.h); }
    if (vertical) cols = 1;
    if (!cols) cols = static_cast<long long>(frames.size());
    const long long rows = -floordiv(-static_cast<long long>(frames.size()), cols);
    Image out(cols * (fw + pad) - pad, rows * (fh + pad) - pad);
    for (std::size_t i = 0; i < frames.size(); ++i) paste(out, frames[i], (static_cast<long long>(i) % cols) * (fw + pad), (static_cast<long long>(i) / cols) * (fh + pad));
    return out;
}
inline std::vector<Image> slice_sheet(const Image& im, long long fw, long long fh, long long pad) {
    std::vector<Image> frames;
    if (fw + pad <= 0 || fh + pad <= 0) die("frame size plus pad must be positive");
    for (long long y = 0; y < im.h - fh + 1; y += fh + pad) for (long long x = 0; x < im.w - fw + 1; x += fw + pad) {
        Image f = crop(im, {x, y, x + fw, y + fh}); if (getbbox(f)) frames.push_back(std::move(f));
    }
    return frames;
}
inline Image offset(const Image& im, long long dx, long long dy) {   // ImageChops.offset: wraps around
    Image o(im.w, im.h);
    for (long long y = 0; y < im.h; ++y) for (long long x = 0; x < im.w; ++x) {
        const long long sx = ((x - dx) % im.w + im.w) % im.w, sy = ((y - dy) % im.h + im.h) % im.h; std::memcpy(o.at(x, y), im.at(sx, sy), 4);
    }
    return o;
}
inline std::vector<Image> simple_frames(const Image& im, long long n, const std::string& kind) {
    std::vector<Image> out; const int bob[4] = {0, -1, 0, 1}; const double wob[4] = {0, -5, 0, 5};
    for (long long k = 0; k < n; ++k) {
        if (kind == "bob") out.push_back(offset(im, 0, bob[k % 4]));
        else if (kind == "squash") {
            const double s = 1 + 0.08 * (k % 2 ? -1.0 : 1.0) * (k > 0 ? 1.0 : 0.0);
            const long long w = round_int(static_cast<double>(im.w) * s), h = round_int(static_cast<double>(im.h) / s);
            const Image sq = resize(im, w, h, Filter::NEAREST);
            Image f(im.w, im.h); paste(f, sq, floordiv(im.w - w, 2), im.h - h); out.push_back(std::move(f));
        } else if (kind == "wobble") out.push_back(rotate(im, wob[k % 4], false));
        else if (kind == "flash") {
            const float factor = static_cast<float>(1 + 0.35 * static_cast<double>(k % 2));
            Image f = im;
            if (factor != 1.0f) for (std::size_t i = 0; i < f.px.size(); i += 4) for (int c = 0; c < 3; ++c) {
                const float t = factor * static_cast<float>(f.px[i + static_cast<std::size_t>(c)]);
                f.px[i + static_cast<std::size_t>(c)] = t <= 0 ? 0 : t >= 255.0f ? 255 : static_cast<uint8_t>(t);
            }
            out.push_back(std::move(f));
        }
    }
    return out;
}
inline std::pair<Image, Gray> team_mask(const Image& im, const std::string& hue, float sat_lo = 0.25f, float val_lo = 0.15f) {
    Image out = im; Gray mask{im.w, im.h, std::vector<uint8_t>(static_cast<std::size_t>(im.w * im.h))};
    for (std::size_t p = 0, i = 0; i < im.px.size(); ++p, i += 4) {
        const float A0 = im.px[i], A1 = im.px[i + 1], A2 = im.px[i + 2];
        const float r = A0 / 255.0f, g = A1 / 255.0f, b = A2 / 255.0f;
        const float mx = std::max(std::max(r, g), b), mn = std::min(std::min(r, g), b);
        const float sat = (mx - mn) / (mx + 1e-6f);
        bool sel = false;
        if (hue == "blue") sel = (b >= r * 1.25f) && (b >= g * 0.9f);
        else if (hue == "red") sel = (r >= g * 1.4f) && (r >= b * 1.4f);
        else if (hue == "green") sel = (g >= r * 1.25f) && (g >= b * 1.1f);
        else sel = (r >= g * 1.4f) && (b >= g * 1.4f);
        const float m = std::clamp((sat - sat_lo) / 0.35f, 0.0f, 1.0f) * (sel ? 1.0f : 0.0f) * (im.px[i + 3] > 0 ? 1.0f : 0.0f) * (mx > val_lo ? 1.0f : 0.0f);
        const float lum = 0.3f * A0 + 0.59f * A1 + 0.11f * A2;
        const float L = std::clamp(lum * 1.15f, 0.0f, 255.0f);
        for (int c = 0; c < 3; ++c) { const float a = im.px[i + static_cast<std::size_t>(c)]; out.px[i + static_cast<std::size_t>(c)] = static_cast<uint8_t>(std::clamp(a * (1 - m) + L * m, 0.0f, 255.0f)); }
        mask.px[p] = static_cast<uint8_t>(m * 255.0f);
    }
    return {out, mask};
}
inline Image seamless(const Image& im, double blend) {
    const long long h = im.h, w = im.w; Image out(w, h);
    for (long long y = 0; y < h; ++y) for (long long x = 0; x < w; ++x) {
        const long long sy = ((y - h / 2) % h + h) % h, sx = ((x - w / 2) % w + w) % w;   // np.roll by (h // 2, w // 2)
        const double dx = static_cast<double>(std::min(x, w - 1 - x)) / (static_cast<double>(w) * blend);
        const double dy = static_cast<double>(std::min(y, h - 1 - y)) / (static_cast<double>(h) * blend);
        const double wgt = std::clamp(std::min(dx, dy), 0.0, 1.0);
        const uint8_t* a = im.at(x, y); const uint8_t* s = im.at(sx, sy); uint8_t* o = out.at(x, y);
        for (int k = 0; k < 4; ++k) o[k] = static_cast<uint8_t>(std::clamp(a[k] * wgt + s[k] * (1 - wgt), 0.0, 255.0));
    }
    return out;
}
inline Image tile_preview(const Image& im, long long n) {
    Image out(im.w * n, im.h * n);
    for (long long y = 0; y < n; ++y) for (long long x = 0; x < n; ++x) paste(out, im, x * im.w, y * im.h);
    return out;
}
inline Image preview(const Image& im, long long scale, long long checker = 8) {
    const Image big = resize(im, im.w * scale, im.h * scale, Filter::NEAREST);
    Image bg(big.w, big.h, {200, 200, 200, 255});
    const long long c = checker * std::max(1LL, scale / 2);
    for (long long y = 0; y < big.h; ++y) for (long long x = 0; x < big.w; ++x) if (((x / c) + (y / c)) % 2) { uint8_t* p = bg.at(x, y); p[0] = p[1] = p[2] = 150; p[3] = 255; }
    alpha_composite(bg, big);
    return bg;
}
inline std::string info(const Image& im) {
    long long hist[256] = {0}; for (std::size_t i = 3; i < im.px.size(); i += 4) ++hist[im.px[i]];
    const double total = static_cast<double>(im.w * im.h);
    long long hi = 0; for (int k = 250; k < 256; ++k) hi += hist[k];
    const auto colors = getcolors(im); const auto bb = getbbox(im); const RGB cc = border_color(im);
    auto r3 = [](double v) { return float_repr(round_n(v, 3)); };
    return "{'size': " + tup({im.w, im.h}) + ", 'bbox': " + (bb ? tup({(*bb)[0], (*bb)[1], (*bb)[2], (*bb)[3]}) : std::string("None")) +
           ", 'opaque': " + r3(static_cast<double>(hi) / total) + ", 'clear': " + r3(static_cast<double>(hist[0]) / total) +
           ", 'partial': " + r3(1 - static_cast<double>(hi) / total - static_cast<double>(hist[0]) / total) +
           ", 'colors': " + (colors ? std::to_string(colors->size()) : std::string("'>1M'")) + ", 'corner_color': " + tup({cc[0], cc[1], cc[2]}) + "}";
}
inline std::optional<RGBA> color(const std::string& s0) {
    if (s0.empty()) return std::nullopt;
    std::size_t k = 0; while (k < s0.size() && s0[k] == '#') ++k;
    const std::string s = s0.substr(k);
    auto hx = [&](std::size_t at) -> int {
        const std::string t = s.substr(std::min(at, s.size()), 2);
        char* end = nullptr; const long v = std::strtol(t.c_str(), &end, 16);
        if (t.empty() || *end || t[0] == '-' || t[0] == '+' || std::isspace(static_cast<unsigned char>(t[0]))) throw PyErr("ValueError", "invalid literal for int() with base 16: " + repr(Val::str(t)));
        return static_cast<int>(v);
    };
    return RGBA{hx(0), hx(2), hx(4), s.size() == 8 ? hx(6) : 255};
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("sprite", DOC); p.help_line = "cut out, fit, pixelate, recolor and pack 2D sprites"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    auto io = [&](const char* name, const char* help) -> Parser& { Parser& q = p.sub(name); q.help_line = help; q.pos("input"); q.pos("output"); return q; };
    { Parser& q = p.sub("info"); q.help_line = "size, alpha coverage, bbox, colour count"; q.pos("inputs").nargs("+"); }
    { Parser& q = io("cutout", "flat background -> transparent (border flood fill)");
      q.opt({"--tol"}).integer().def(28LL).help("colour distance to the background (0-255 per channel)");
      q.opt({"--bg"}).help("background colour hex (default: corners)");
      q.opt({"--grey"}).integer().help("also clear light greys (soft shadows) with min channel >= this");
      q.opt({"--spread"}).integer().def(24LL);
      q.opt({"--holes"}).store_true().help("also clear enclosed background-coloured areas");
      q.opt({"--keep-top"}).number().def_float(1.0).help("keep this fraction from the top (drop a floor shadow)"); }
    { Parser& q = io("fit", "trim + scale into a WxH frame (nearest neighbour)");
      q.opt({"--size"}).required(); q.opt({"--anchor"}).def("center").choices({"center", "bottom", "top"});
      q.opt({"--smooth"}).store_true().help("Lanczos instead of nearest (painted/HD art)"); q.opt({"--no-upscale"}).store_true(); q.opt({"--hard-alpha"}).store_true(); }
    { Parser& q = io("pixelate", "downscale to true pixel art with a limited palette");
      q.opt({"--size"}).required(); q.opt({"--colors"}).integer().def(16LL).help("0 = keep all colours");
      q.opt({"--outline"}).store_true(); q.opt({"--outline-color"}).def("#141018"); }
    { Parser& q = io("palette", "snap colours to a reference sprite's palette"); q.opt({"--from"}).required(); q.opt({"--max-colors"}).integer().def(64LL); }
    { Parser& q = p.sub("sheet"); q.help_line = "pack frames into a sheet"; q.pos("output"); q.pos("frames").nargs("+");
      q.opt({"--cols"}).integer(); q.opt({"--pad"}).integer().def(0LL); q.opt({"--vertical"}).store_true().help("one column (Terraria NPC/projectile strips)"); }
    { Parser& q = p.sub("slice"); q.help_line = "split a sheet into frames"; q.pos("input"); q.pos("outdir"); q.opt({"--frame"}).required().help("WxH"); q.opt({"--pad"}).integer().def(0LL); }
    { Parser& q = p.sub("frames"); q.help_line = "make a small idle animation from one frame"; q.pos("input"); q.pos("outdir");
      q.opt({"--n"}).integer().def(3LL); q.opt({"--kind"}).def("bob").choices({"bob", "squash", "wobble", "flash"}); }
    { Parser& q = io("team-mask", "player-colour mask from a saturated accent colour"); q.opt({"--hue"}).def("blue").choices({"blue", "red", "green", "magenta"}); }
    { Parser& q = io("seamless", "make a texture tile"); q.opt({"--blend"}).number().def_float(0.25); }
    { Parser& q = io("tile-preview", "NxN tiled preview"); q.opt({"--n"}).integer().def(3LL); }
    { Parser& q = io("preview", "checkerboard + nearest scale-up for viewing"); q.opt({"--scale"}).integer().def(4LL); }
    { Parser& q = io("outline", "1px outline"); q.opt({"--color"}).def("#141018"); }
    { Parser& q = io("hard-alpha", "binary alpha"); q.opt({"--thresh"}).integer().def(128LL); }
    { Parser& q = io("flip", "mirror horizontally (or --vertical)"); q.opt({"--vertical"}).store_true(); }
    { Parser& q = io("rotate", "rotate by degrees (counter-clockwise), nearest"); q.pos("degrees").number(); }
}
inline long long I(const Val& a, const char* k) { return get(a, k).i; }
inline double F(const Val& a, const char* k) { return get(a, k).num(); }
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "info") { for (const auto& v : *get(a, "inputs").l) print(v.s + " " + info(load(v.s))); return 0; }
    if (c == "sheet") {
        std::vector<Image> frames; for (const auto& v : *get(a, "frames").l) frames.push_back(load(v.s));
        save(sheet(frames, get(a, "cols").is_none() ? 0 : I(a, "cols"), I(a, "pad"), truthy(get(a, "vertical"))), S(a, "output"));
        print(S(a, "output")); return 0;
    }
    if (c == "slice") {
        const auto [fw, fh] = parse_size(S(a, "frame")); const std::string out = S(a, "outdir"); makedirs(out);
        const auto fr = slice_sheet(load(S(a, "input")), fw, fh, I(a, "pad"));
        for (std::size_t i = 0; i < fr.size(); ++i) { char nm[32]; std::snprintf(nm, sizeof nm, "frame_%03zu.png", i); save(fr[i], path_join(out, nm)); }
        print(path_str(out)); return 0;
    }
    if (c == "frames") {
        const std::string out = S(a, "outdir"); makedirs(out);
        const std::string stem = u8native(P(S(a, "input")).stem());
        const auto fr = simple_frames(load(S(a, "input")), I(a, "n"), S(a, "kind"));
        for (std::size_t i = 0; i < fr.size(); ++i) save(fr[i], path_join(out, stem + "_" + std::to_string(i) + ".png"));
        print(path_str(out)); return 0;
    }
    Image im = load(S(a, "input"));
    if (c == "cutout") {
        std::optional<long long> grey; if (!get(a, "grey").is_none()) grey = I(a, "grey");
        im = cutout(im, static_cast<int>(I(a, "tol")), color(S(a, "bg")), grey, static_cast<int>(I(a, "spread")), truthy(get(a, "holes")), F(a, "keep_top"));
    } else if (c == "fit") {
        const auto [w, h] = parse_size(S(a, "size"));
        im = fit(im, w, h, S(a, "anchor"), truthy(get(a, "smooth")), !truthy(get(a, "no_upscale")));
        if (truthy(get(a, "hard_alpha"))) im = hard_alpha(im);
    } else if (c == "pixelate") {
        const auto [w, h] = parse_size(S(a, "size"));
        im = pixelate(im, w, h, static_cast<int>(I(a, "colors")), truthy(get(a, "outline")) ? color(S(a, "outline_color")) : std::nullopt);
    } else if (c == "palette") im = snap_palette(im, palette_from(load(S(a, "from")), I(a, "max_colors")));
    else if (c == "team-mask") {
        auto [o, mask] = team_mask(im, S(a, "hue")); im = std::move(o);
        const fs::path op = P(S(a, "output"));
        save_gray(mask, u8native(op.parent_path() / P(u8native(op.stem()) + "_mask.png")));
    }
    else if (c == "seamless") im = seamless(im, F(a, "blend"));
    else if (c == "tile-preview") im = tile_preview(im, I(a, "n"));
    else if (c == "preview") im = preview(im, I(a, "scale"));
    else if (c == "outline") { const auto col = color(S(a, "color")); im = add_outline(im, col ? *col : RGBA{20, 16, 24, 255}); }
    else if (c == "hard-alpha") im = hard_alpha(im, static_cast<int>(I(a, "thresh")));
    else if (c == "flip") im = truthy(get(a, "vertical")) ? flip(im) : mirror(im);
    else if (c == "rotate") im = rotate(im, F(a, "degrees"), true);
    save(im, S(a, "output"));
    print(S(a, "output") + " " + tup({im.w, im.h}));
    return 0;
}
}  // namespace packtools::modder::sprite

namespace packtools::modder::iwi {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::img;

inline const char* DOC =
    "Call of Duty IWI v6 texture converter and decoder.\n\n"
    "    um iwi png2iwi input.png output.iwi [--format dxt1|dxt5] [--mips]\n"
    "    um iwi iwi2png input.iwi output.png\n\n"
    "Converts between standard PNG images and Call of Duty: World at War / CoD4\n"
    "IWI version 6 textures. png2iwi writes DXT1 (diffuse/opaque) or DXT5 (alpha/normal),\n"
    "with the mip chain stored smallest first as the Treyarch T4 / IW 3.0 engine expects;\n"
    "iwi2png reads DXT1, DXT3 and DXT5 2D textures (not cubemaps or uncompressed formats).\n\n"
    "The 28-byte header follows OpenAssetTools' IwiTypes.h: \"IWi\", version 6, format,\n"
    "flags, width, height, depth, then the file size for picmip 0-3. --mips writes the\n"
    "whole chain down to 1x1 (flags 0); without it the file has one level (flags 0x03:\n"
    "no picmip, no mipmaps).\n";

enum : int { FMT_DXT1 = 0x0B, FMT_DXT3 = 0x0C, FMT_DXT5 = 0x0D, FLAG_NOPICMIP = 1, FLAG_NOMIPMAPS = 2, FLAG_CUBEMAP = 4, FLAG_VOLMAP = 8 };

inline void le16(std::string& o, unsigned v) { o += static_cast<char>(v & 0xFF); o += static_cast<char>((v >> 8) & 0xFF); }
inline void le32(std::string& o, uint32_t v) { for (int k = 0; k < 4; ++k) o += static_cast<char>((v >> (8 * k)) & 0xFF); }
inline unsigned rgb565(const int* c) { return static_cast<unsigned>(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)); }
inline std::array<int, 3> from565(unsigned c) { const unsigned r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F; return {static_cast<int>(r * 255 / 31), static_cast<int>(g * 255 / 63), static_cast<int>(b * 255 / 31)}; }

inline std::string encode_dxt1_block(const int px[16][3]) {
    int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], px[i][k]); mx[k] = std::max(mx[k], px[i][k]); }
    unsigned c0 = rgb565(mx), c1 = rgb565(mn); std::string o;
    double cmax[3], cmin[3]; for (int k = 0; k < 3; ++k) { cmax[k] = mx[k]; cmin[k] = mn[k]; }
    if (c0 < c1) { std::swap(c0, c1); for (int k = 0; k < 3; ++k) std::swap(cmax[k], cmin[k]); }
    else if (c0 == c1) { le16(o, c0); le16(o, c1); le32(o, 0); return o; }
    double pal[4][3]; for (int k = 0; k < 3; ++k) { pal[0][k] = cmax[k]; pal[1][k] = cmin[k]; pal[2][k] = (2.0 * cmax[k] + cmin[k]) / 3.0; pal[3][k] = (cmax[k] + 2.0 * cmin[k]) / 3.0; }
    uint32_t mask = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0; double bd = 0;
        for (int j = 0; j < 4; ++j) { double d = 0; for (int k = 0; k < 3; ++k) { const double t = px[i][k] - pal[j][k]; d += t * t; } if (j == 0 || d < bd) { bd = d; best = j; } }
        mask |= static_cast<uint32_t>(best) << (i * 2);
    }
    le16(o, c0); le16(o, c1); le32(o, mask); return o;
}
inline std::string encode_dxt5_alpha_block(const int al[16]) {
    int amin = 255, amax = 0; for (int i = 0; i < 16; ++i) { amin = std::min(amin, al[i]); amax = std::max(amax, al[i]); }
    std::string o; o += static_cast<char>(amax); o += static_cast<char>(amin);
    if (amax == amin) return o + std::string(6, '\0');
    double pal[8] = {static_cast<double>(amax), static_cast<double>(amin)};
    for (int i = 1; i < 7; ++i) pal[i + 1] = static_cast<double>(static_cast<int>(((7 - i) * amax + i * amin) / 7.0));
    uint64_t mask = 0;
    for (int i = 0; i < 16; ++i) { int best = 0; double bd = 0; for (int j = 0; j < 8; ++j) { const double t = al[i] - pal[j], d = t * t; if (j == 0 || d < bd) { bd = d; best = j; } } mask |= static_cast<uint64_t>(best) << (i * 3); }
    for (int k = 0; k < 6; ++k) o += static_cast<char>((mask >> (8 * k)) & 0xFF);
    return o;
}
inline std::string encode_dxt(const Image& im, bool dxt5) {
    const long long pw = (im.w + 3) & ~3LL, ph = (im.h + 3) & ~3LL;
    Image pad(pw, ph); paste(pad, im, 0, 0);
    std::string out;
    for (long long by = 0; by < ph; by += 4) for (long long bx = 0; bx < pw; bx += 4) {
        int px[16][3]; int al[16];
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { const uint8_t* p = pad.at(bx + x, by + y); for (int k = 0; k < 3; ++k) px[y * 4 + x][k] = p[k]; al[y * 4 + x] = p[3]; }
        if (dxt5) out += encode_dxt5_alpha_block(al);
        out += encode_dxt1_block(px);
    }
    return out;
}
inline void png_to_iwi(const std::string& src, const std::string& dst, const std::string& fmt_name, bool mips) {
    if (!isfile(src)) die("source image not found: " + path_str(src));
    const Image im = load(src);
    const bool dxt5 = lower(fmt_name) == "dxt5";
    const int fmt = dxt5 ? FMT_DXT5 : FMT_DXT1, flags = mips ? 0 : (FLAG_NOPICMIP | FLAG_NOMIPMAPS);
    std::vector<Image> levels{im};
    if (mips) { long long cw = im.w, ch = im.h; while (std::max(cw, ch) > 1) { cw = std::max(1LL, cw / 2); ch = std::max(1LL, ch / 2); levels.push_back(resize(im, cw, ch, Filter::LANCZOS)); } }
    std::vector<std::string> enc; for (const auto& l : levels) enc.push_back(encode_dxt(l, dxt5));
    long long total = 28; for (const auto& e : enc) total += static_cast<long long>(e.size());
    std::vector<long long> sizes; long long size = total;
    for (std::size_t level = 0; level < 4; ++level) { sizes.push_back(size); if (level + 1 < enc.size()) size -= static_cast<long long>(enc[level].size()); }
    std::string o = "IWi"; o += static_cast<char>(6); o += static_cast<char>(fmt); o += static_cast<char>(flags);
    if (im.w > 0xFFFF || im.h > 0xFFFF) throw PyErr("struct.error", "'H' format requires 0 <= number <= 65535");
    le16(o, static_cast<unsigned>(im.w)); le16(o, static_cast<unsigned>(im.h)); le16(o, 1);
    for (long long s : sizes) le32(o, static_cast<uint32_t>(s));
    for (auto it = enc.rbegin(); it != enc.rend(); ++it) o += *it;
    spit(dst, o);
    print("Created IWI v6: " + path_str(dst) + " (" + std::to_string(im.w) + "x" + std::to_string(im.h) + ", " + upper(fmt_name) + ", " + commas(std::to_string(total)) + " bytes)");
}
inline void decode_color_block(const std::string& d, std::size_t at, bool four, uint8_t out[16][4]) {
    const unsigned c0 = static_cast<unsigned char>(d[at]) | (static_cast<unsigned>(static_cast<unsigned char>(d[at + 1])) << 8);
    const unsigned c1 = static_cast<unsigned char>(d[at + 2]) | (static_cast<unsigned>(static_cast<unsigned char>(d[at + 3])) << 8);
    uint32_t mask = 0; for (int k = 3; k >= 0; --k) mask = (mask << 8) | static_cast<unsigned char>(d[at + 4 + static_cast<std::size_t>(k)]);
    const auto p0 = from565(c0), p1 = from565(c1);
    double pal[4][3]; int transparent = -1;
    for (int k = 0; k < 3; ++k) { pal[0][k] = p0[static_cast<std::size_t>(k)]; pal[1][k] = p1[static_cast<std::size_t>(k)]; }
    if (c0 > c1 || four) for (int k = 0; k < 3; ++k) { pal[2][k] = (2.0 * pal[0][k] + pal[1][k]) / 3.0; pal[3][k] = (pal[0][k] + 2.0 * pal[1][k]) / 3.0; }
    else { for (int k = 0; k < 3; ++k) { pal[2][k] = (pal[0][k] + pal[1][k]) / 2.0; pal[3][k] = 0; } transparent = 3; }
    for (int i = 0; i < 16; ++i) { const int b = static_cast<int>((mask >> (i * 2)) & 3); for (int k = 0; k < 3; ++k) out[i][k] = static_cast<uint8_t>(pal[b][k]); out[i][3] = b == transparent ? 0 : 255; }
}
inline void iwi_to_png(const std::string& src, const std::string& dst) {
    if (!isfile(src)) die("IWI file not found: " + path_str(src));
    const std::string d = slurp(src);
    if (d.compare(0, 3, "IWi") != 0) die("not a valid IWI file (bad magic)");
    if (d.size() < 28) die("not a valid IWI file (the header is cut short)");
    const int ver = static_cast<unsigned char>(d[3]);
    if (ver != 6) die("unsupported IWI version: " + std::to_string(ver) + " (expected version 6)");
    const int fmt = static_cast<unsigned char>(d[4]), flags = static_cast<unsigned char>(d[5]);
    auto u16 = [&](std::size_t at) { return static_cast<long long>(static_cast<unsigned char>(d[at]) | (static_cast<unsigned>(static_cast<unsigned char>(d[at + 1])) << 8)); };
    const long long w = u16(6), h = u16(8);
    uint32_t mip0_end = 0; for (int k = 3; k >= 0; --k) mip0_end = (mip0_end << 8) | static_cast<unsigned char>(d[12 + static_cast<std::size_t>(k)]);
    if (flags & (FLAG_CUBEMAP | FLAG_VOLMAP)) die("cubemap and volume IWIs aren't supported (2D textures only)");
    const long long bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (fmt != FMT_DXT1 && fmt != FMT_DXT3 && fmt != FMT_DXT5) {
        char m[160]; std::snprintf(m, sizeof m, "unsupported format ID in IWI: 0x%02X (iwi2png reads DXT1 0x0B, DXT3 0x0C and DXT5 0x0D)", fmt); die(m);
    }
    const long long bs = fmt == FMT_DXT1 ? 8 : 16, len = bw * bh * bs, start = static_cast<long long>(mip0_end) - len;
    if (start < 0 || static_cast<long long>(mip0_end) > static_cast<long long>(d.size())) die("IWI data is cut short (the mip 0 level does not fit in the file)");
    Image full(bw * 4, bh * 4); std::size_t off = static_cast<std::size_t>(start);
    for (long long by = 0; by < bh; ++by) for (long long bx = 0; bx < bw; ++bx) {
        uint8_t blk[16][4];
        if (fmt == FMT_DXT1) { decode_color_block(d, off, false, blk); off += 8; }
        else {
            uint8_t al[16];
            if (fmt == FMT_DXT3) { uint64_t bits = 0; for (int k = 7; k >= 0; --k) bits = (bits << 8) | static_cast<unsigned char>(d[off + static_cast<std::size_t>(k)]); for (int i = 0; i < 16; ++i) al[i] = static_cast<uint8_t>(((bits >> (4 * i)) & 0xF) * 17); }
            else {
                const int a0 = static_cast<unsigned char>(d[off]), a1 = static_cast<unsigned char>(d[off + 1]);
                uint64_t mask = 0; for (int k = 5; k >= 0; --k) mask = (mask << 8) | static_cast<unsigned char>(d[off + 2 + static_cast<std::size_t>(k)]);
                double pal[8] = {static_cast<double>(a0), static_cast<double>(a1)};
                if (a0 > a1) for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7.0;
                else { for (int i = 1; i < 5; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5.0; pal[6] = 0.0; pal[7] = 255.0; }
                for (int i = 0; i < 16; ++i) al[i] = static_cast<uint8_t>(static_cast<int>(pal[(mask >> (i * 3)) & 7]));
            }
            decode_color_block(d, off + 8, true, blk); for (int i = 0; i < 16; ++i) blk[i][3] = al[i];
            off += 16;
        }
        for (int i = 0; i < 16; ++i) std::memcpy(full.at(bx * 4 + i % 4, by * 4 + i / 4), blk[i], 4);
    }
    save(crop(full, {0, 0, w, h}), dst);
    print("Decoded " + name_of_path(src) + " -> " + path_str(dst) + " (" + std::to_string(w) + "x" + std::to_string(h) + ")");
}
inline void reg(Parser& sub) {
    Parser& p = sub.sub("iwi", DOC); p.help_line = "convert between PNG and Call of Duty IWI v6 textures"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("png2iwi"); q.help_line = "convert a PNG image into a game-ready IWI v6 file"; q.pos("src").help("input PNG file"); q.pos("dst").help("output IWI file");
      q.opt({"--format"}).choices({"dxt1", "dxt5"}).def("dxt1").help("texture compression (default: dxt1)"); q.opt({"--mips"}).store_true().help("generate reverse mipmap hierarchy"); }
    { Parser& q = p.sub("iwi2png"); q.help_line = "decode an in-game IWI v6 texture into a PNG image"; q.pos("src").help("input IWI file"); q.pos("dst").help("output PNG file"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "png2iwi") png_to_iwi(S(a, "src"), S(a, "dst"), S(a, "format"), truthy(get(a, "mips")));
    else if (c == "iwi2png") iwi_to_png(S(a, "src"), S(a, "dst"));
    return 0;
}
}  // namespace packtools::modder::iwi
