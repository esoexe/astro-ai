// modder_font.inc - the drawing layer under `um video` (the parts of Pillow's ImageFont / ImageDraw / ImageFilter the
// original uses): a TrueType (glyf) reader and anti-aliased rasterizer, text length / bbox / drawing with Pillow's
// "la" anchor, rounded rectangles, ellipses and GaussianBlur (Pillow's three-pass extended box blur).
// Not FreeType: no hinting, so glyph shapes can differ from Pillow's by a fraction of a pixel; advances are rounded
// to whole pixels the way hinted FreeType reports them.
namespace packtools::modder::img {

struct Font {
    std::string d; bool ok = false;
    int upem = 1000, asc = 0, desc = 0, nhm = 0, nglyphs = 0, loca_long = 0;
    std::size_t glyf = 0, loca = 0, hmtx = 0, cmap_sub = 0; int cmap_fmt = 0;
    uint32_t u8(std::size_t a) const { return a < d.size() ? static_cast<unsigned char>(d[a]) : 0u; }
    uint32_t u16(std::size_t a) const { return (u8(a) << 8) | u8(a + 1); }
    int s16(std::size_t a) const { return static_cast<int16_t>(static_cast<uint16_t>(u16(a))); }
    uint32_t u32(std::size_t a) const { return (u16(a) << 16) | u16(a + 2); }
    std::size_t table(const char* tag) const {
        const uint32_t n = u16(4);
        for (uint32_t i = 0; i < n; ++i) { const std::size_t r = 12 + 16 * i; if (d.compare(r, 4, tag, 4) == 0) return u32(r + 8); }
        return 0;
    }
    bool load(const std::string& path) {
        d = slurp(path); if (d.size() < 12) return false;
        if (d.compare(0, 4, "ttcf") == 0) return false;   // collections: pass a single .ttf
        const std::size_t head = table("head"), hhea = table("hhea"), maxp = table("maxp"), cmap = table("cmap");
        glyf = table("glyf"); loca = table("loca"); hmtx = table("hmtx");
        if (!head || !hhea || !maxp || !cmap || !glyf || !loca || !hmtx) return false;
        upem = static_cast<int>(u16(head + 18)); loca_long = s16(head + 50); nglyphs = static_cast<int>(u16(maxp + 4));
        asc = s16(hhea + 4); desc = s16(hhea + 6); nhm = static_cast<int>(u16(hhea + 34));
        const std::size_t os2 = table("OS/2");
        if (os2 && (u16(os2 + 62) & (1u << 7))) { asc = s16(os2 + 68); desc = s16(os2 + 70); }   // USE_TYPO_METRICS
        const uint32_t nsub = u16(cmap + 2); std::size_t best4 = 0, best12 = 0;
        for (uint32_t i = 0; i < nsub; ++i) {
            const std::size_t r = cmap + 4 + 8 * i; const uint32_t pid = u16(r), eid = u16(r + 2); const std::size_t off = cmap + u32(r + 4);
            const uint32_t fmt = u16(off);
            if (fmt == 12 && ((pid == 3 && eid == 10) || pid == 0)) best12 = off;
            if (fmt == 4 && ((pid == 3 && (eid == 1 || eid == 0)) || pid == 0)) best4 = off;
        }
        if (best12) { cmap_sub = best12; cmap_fmt = 12; } else if (best4) { cmap_sub = best4; cmap_fmt = 4; } else return false;
        ok = upem > 0; return ok;
    }
    uint32_t glyph(uint32_t cp) const {
        if (cmap_fmt == 12) {
            const uint32_t n = u32(cmap_sub + 12);
            for (uint32_t i = 0; i < n; ++i) { const std::size_t r = cmap_sub + 16 + 12 * i; const uint32_t a = u32(r), b = u32(r + 4); if (cp >= a && cp <= b) return u32(r + 8) + (cp - a); }
            return 0;
        }
        if (cp > 0xFFFF) return 0;
        const uint32_t segx2 = u16(cmap_sub + 6); const std::size_t ends = cmap_sub + 14, starts = ends + segx2 + 2, deltas = starts + segx2, ranges = deltas + segx2;
        for (uint32_t i = 0; i < segx2; i += 2) {
            if (cp > u16(ends + i)) continue;
            const uint32_t st = u16(starts + i); if (cp < st) return 0;
            const uint32_t ro = u16(ranges + i);
            if (!ro) return (cp + u16(deltas + i)) & 0xFFFF;
            const uint32_t g = u16(ranges + i + ro + 2 * (cp - st)); return g ? (g + u16(deltas + i)) & 0xFFFF : 0;
        }
        return 0;
    }
    int advance(uint32_t g) const { if (nhm <= 0) return 0; const uint32_t k = g < static_cast<uint32_t>(nhm) ? g : static_cast<uint32_t>(nhm - 1); return static_cast<int>(u16(hmtx + 4 * k)); }
    std::pair<std::size_t, std::size_t> span(uint32_t g) const {
        if (g >= static_cast<uint32_t>(nglyphs)) return {0, 0};
        const std::size_t a = loca_long ? u32(loca + 4 * g) : u16(loca + 2 * g) * 2u, b = loca_long ? u32(loca + 4 * g + 4) : u16(loca + 2 * g + 2) * 2u;
        return {glyf + a, glyf + b};
    }
    struct Pt { double x, y; bool on; };
    // contours in font units, transformed by m = (a, b, c, d, e, f): x' = a x + c y + e, y' = b x + d y + f
    void outline(uint32_t g, std::vector<std::vector<Pt>>& out, const std::array<double, 6>& m, int depth = 0) const {
        const auto [a, b] = span(g); if (b <= a || depth > 8) return;
        const int nc = s16(a);
        if (nc >= 0) {
            std::vector<uint32_t> ends; for (int i = 0; i < nc; ++i) ends.push_back(u16(a + 10 + 2 * static_cast<std::size_t>(i)));
            const uint32_t npts = ends.empty() ? 0 : ends.back() + 1;
            std::size_t p = a + 10 + 2 * static_cast<std::size_t>(nc); p += 2 + u16(p);
            std::vector<uint8_t> fl; while (fl.size() < npts) { const uint8_t f = static_cast<uint8_t>(u8(p++)); fl.push_back(f); if (f & 8) { uint32_t r = u8(p++); while (r-- && fl.size() < npts) fl.push_back(f); } }
            std::vector<int> xs(npts), ys(npts); int v = 0;
            for (uint32_t i = 0; i < npts; ++i) { const uint8_t f = fl[i]; if (f & 2) { const int dv = static_cast<int>(u8(p++)); v += (f & 16) ? dv : -dv; } else if (!(f & 16)) { v += s16(p); p += 2; } xs[i] = v; }
            v = 0;
            for (uint32_t i = 0; i < npts; ++i) { const uint8_t f = fl[i]; if (f & 4) { const int dv = static_cast<int>(u8(p++)); v += (f & 32) ? dv : -dv; } else if (!(f & 32)) { v += s16(p); p += 2; } ys[i] = v; }
            uint32_t s = 0;
            for (uint32_t e : ends) {
                std::vector<Pt> c;
                for (uint32_t i = s; i <= e && i < npts; ++i) c.push_back({m[0] * xs[i] + m[2] * ys[i] + m[4], m[1] * xs[i] + m[3] * ys[i] + m[5], (fl[i] & 1) != 0});
                if (!c.empty()) out.push_back(std::move(c));
                s = e + 1;
            }
            return;
        }
        std::size_t p = a + 10;
        for (;;) {
            const uint32_t flags = u16(p), gi = u16(p + 2); p += 4;
            double dx = 0, dy = 0;
            if (flags & 1) { if (flags & 2) { dx = s16(p); dy = s16(p + 2); } p += 4; }
            else { if (flags & 2) { dx = static_cast<int8_t>(u8(p)); dy = static_cast<int8_t>(u8(p + 1)); } p += 2; }
            double sa = 1, sb = 0, sc = 0, sd = 1;
            auto f2 = [&](std::size_t at) { return s16(at) / 16384.0; };
            if (flags & 8) { sa = sd = f2(p); p += 2; }
            else if (flags & 0x40) { sa = f2(p); sd = f2(p + 2); p += 4; }
            else if (flags & 0x80) { sa = f2(p); sb = f2(p + 2); sc = f2(p + 4); sd = f2(p + 6); p += 8; }
            const std::array<double, 6> cm = {m[0] * sa + m[2] * sb, m[1] * sa + m[3] * sb, m[0] * sc + m[2] * sd, m[1] * sc + m[3] * sd, m[0] * dx + m[2] * dy + m[4], m[1] * dx + m[3] * dy + m[5]};
            outline(gi, out, cm, depth + 1);
            if (!(flags & 0x20)) break;
        }
    }
};
inline std::map<std::string, std::shared_ptr<Font>>& font_cache() { static std::map<std::string, std::shared_ptr<Font>> c; return c; }

struct SizedFont {
    std::shared_ptr<Font> f; int size = 10; double scale = 0.01; long long asc_px = 0;
    long long adv_px(uint32_t g) const { return round_int(f->advance(g) * scale); }
};
inline SizedFont truetype(const std::string& path, int size) {
    auto& c = font_cache()[path];
    if (!c) { c = std::make_shared<Font>(); if (!c->load(path)) die("cannot open font " + path + " (a TrueType .ttf with glyf outlines is needed)"); }
    SizedFont s; s.f = c; s.size = size; s.scale = static_cast<double>(size) / c->upem; s.asc_px = static_cast<long long>(std::ceil(c->asc * s.scale - 1e-9));
    return s;
}
inline std::vector<uint32_t> codepoints(const std::string& s) {
    std::vector<uint32_t> o; const std::string t = decode_utf8_replace(s);
    for (std::size_t i = 0; i < t.size();) {
        const unsigned char c = static_cast<unsigned char>(t[i]); uint32_t cp = c; int n = 0;
        if (c >= 0xF0) { cp = c & 7; n = 3; } else if (c >= 0xE0) { cp = c & 15; n = 2; } else if (c >= 0xC0) { cp = c & 31; n = 1; }
        ++i; for (int k = 0; k < n && i < t.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(t[i]) & 0x3F);
        o.push_back(cp);
    }
    return o;
}
inline double textlength(const SizedFont& f, const std::string& text) {
    long long w = 0; for (uint32_t cp : codepoints(text)) if (cp != '\n') w += f.adv_px(f.f->glyph(cp)); return static_cast<double>(w);
}
// getbbox(text) with the "la" anchor: (left, top, right, bottom) in pixels from the drawing origin
inline std::array<long long, 4> getbbox(const SizedFont& f, const std::string& text) {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18; long long pen = 0;
    for (uint32_t cp : codepoints(text)) {
        const uint32_t g = f.f->glyph(cp); std::vector<std::vector<Font::Pt>> cs;
        f.f->outline(g, cs, {f.scale, 0, 0, -f.scale, static_cast<double>(pen), static_cast<double>(f.asc_px)});
        for (const auto& c : cs) for (const auto& p : c) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
        pen += f.adv_px(g);
    }
    if (x0 > x1) return {0, 0, pen, 0};
    return {static_cast<long long>(std::floor(x0)), static_cast<long long>(std::floor(y0)), std::max(pen, static_cast<long long>(std::ceil(x1))), static_cast<long long>(std::ceil(y1))};
}
// signed-area coverage rasterizer (non-zero, accumulated per row)
struct Raster {
    long long w, h; std::vector<float> a;
    Raster(long long W, long long H) : w(W), h(H), a(static_cast<std::size_t>(W * H + W + 4), 0.0f) {}
    void line(double x0_, double y0_, double x1_, double y1_) {
        if (std::fabs(y0_ - y1_) <= 1e-9) return;
        double dir = 1.0; if (y0_ > y1_) { std::swap(x0_, x1_); std::swap(y0_, y1_); dir = -1.0; }
        const double dxdy = (x1_ - x0_) / (y1_ - y0_); double x = x0_;
        long long ys = static_cast<long long>(std::floor(y0_)); if (y0_ < 0) { x -= y0_ * dxdy; ys = 0; }
        const long long ye = std::min<long long>(h, static_cast<long long>(std::ceil(y1_)));
        for (long long y = ys; y < ye; ++y) {
            const double dy = std::min(static_cast<double>(y + 1), y1_) - std::max(static_cast<double>(y), y0_);
            const double xnext = x + dxdy * dy, dd = dy * dir;
            double lo = std::min(x, xnext), hi = std::max(x, xnext);
            lo = std::clamp(lo, 0.0, static_cast<double>(w - 1)); hi = std::clamp(hi, 0.0, static_cast<double>(w - 1) + 0.999);
            const double lof = std::floor(lo); const long long li = static_cast<long long>(lof); const double hic = std::ceil(hi); const long long hi_i = static_cast<long long>(hic);
            float* row = &a[static_cast<std::size_t>(y * w)];
            if (hi_i <= li + 1) {
                const double xm = 0.5 * (std::clamp(x, 0.0, static_cast<double>(w)) + std::clamp(xnext, 0.0, static_cast<double>(w))) - lof;
                row[li] += static_cast<float>(dd - dd * xm); row[li + 1] += static_cast<float>(dd * xm);
            } else {
                const double s = 1.0 / (hi - lo), x0f = lo - lof, a0 = 0.5 * s * (1.0 - x0f) * (1.0 - x0f), x1f = hi - hic + 1.0, am = 0.5 * s * x1f * x1f;
                row[li] += static_cast<float>(dd * a0);
                if (hi_i == li + 2) row[li + 1] += static_cast<float>(dd * (1.0 - a0 - am));
                else {
                    const double a1 = s * (1.5 - x0f); row[li + 1] += static_cast<float>(dd * (a1 - a0));
                    for (long long xi = li + 2; xi < hi_i - 1; ++xi) row[xi] += static_cast<float>(dd * s);
                    const double a2 = a1 + static_cast<double>(hi_i - li - 3) * s; row[hi_i - 1] += static_cast<float>(dd * (1.0 - a2 - am));
                }
                row[hi_i] += static_cast<float>(dd * am);
            }
            x = xnext;
        }
    }
    void quad(double x0, double y0, double cx, double cy, double x1, double y1) {
        const double ddx = x0 - 2 * cx + x1, ddy = y0 - 2 * cy + y1, dev = ddx * ddx + ddy * ddy;
        if (dev < 0.333) { line(x0, y0, x1, y1); return; }
        const int n = 1 + static_cast<int>(std::floor(std::sqrt(std::sqrt(3.0 * dev))));
        double px = x0, py = y0;
        for (int i = 1; i <= n; ++i) { const double t = static_cast<double>(i) / n, u = 1 - t; const double nx = u * u * x0 + 2 * u * t * cx + t * t * x1, ny = u * u * y0 + 2 * u * t * cy + t * t * y1; line(px, py, nx, ny); px = nx; py = ny; }
    }
    std::vector<uint8_t> coverage() const {
        std::vector<uint8_t> o(static_cast<std::size_t>(w * h)); double acc = 0;   // one running sum over the whole buffer (a row spills into the next)
        for (long long y = 0; y < h; ++y) { for (long long x = 0; x < w; ++x) { acc += a[static_cast<std::size_t>(y * w + x)]; const double v = std::min(1.0, std::fabs(acc)); o[static_cast<std::size_t>(y * w + x)] = static_cast<uint8_t>(v * 255.0 + 0.5); } }
        return o;
    }
};
inline void fill_contours(Raster& r, const std::vector<std::vector<Font::Pt>>& cs) {
    for (const auto& c : cs) {
        const std::size_t n = c.size(); if (n < 2) continue;
        // start on an on-curve point (or the midpoint of two off-curve points)
        std::size_t s = 0; while (s < n && !c[s].on) ++s;
        Font::Pt start = s < n ? c[s] : Font::Pt{(c[0].x + c[1].x) / 2, (c[0].y + c[1].y) / 2, true};
        if (s >= n) s = 0;
        Font::Pt cur = start; bool have_ctrl = false; Font::Pt ctrl{};
        for (std::size_t k = 1; k <= n; ++k) {
            const Font::Pt& p = c[(s + k) % n];
            if (p.on) { if (have_ctrl) r.quad(cur.x, cur.y, ctrl.x, ctrl.y, p.x, p.y); else r.line(cur.x, cur.y, p.x, p.y); cur = p; have_ctrl = false; }
            else if (have_ctrl) { const Font::Pt mid{(ctrl.x + p.x) / 2, (ctrl.y + p.y) / 2, true}; r.quad(cur.x, cur.y, ctrl.x, ctrl.y, mid.x, mid.y); cur = mid; ctrl = p; }
            else { ctrl = p; have_ctrl = true; }
        }
        if (have_ctrl) r.quad(cur.x, cur.y, ctrl.x, ctrl.y, start.x, start.y);
        else if (cur.x != start.x || cur.y != start.y) r.line(cur.x, cur.y, start.x, start.y);
    }
}
// draw.text((x, y), text, font=f, fill=ink): glyphs are rendered one by one and combined with max(), like Pillow's mask
inline void draw_text(Image& im, double x, double y, const std::string& text, const SizedFont& f, const std::array<int, 4>& ink, bool rgb_image = false) {
    long long pen = 0; std::vector<uint8_t> mask(static_cast<std::size_t>(im.w * im.h), 0);
    for (uint32_t cp : codepoints(text)) {
        const uint32_t g = f.f->glyph(cp); std::vector<std::vector<Font::Pt>> cs;
        f.f->outline(g, cs, {f.scale, 0, 0, -f.scale, x + static_cast<double>(pen), y + static_cast<double>(f.asc_px)});
        pen += f.adv_px(g);
        if (cs.empty()) continue;
        double bx0 = 1e18, by0 = 1e18, bx1 = -1e18, by1 = -1e18;
        for (const auto& c : cs) for (const auto& p : c) { bx0 = std::min(bx0, p.x); bx1 = std::max(bx1, p.x); by0 = std::min(by0, p.y); by1 = std::max(by1, p.y); }
        const long long ox = static_cast<long long>(std::floor(bx0)) - 1, oy = static_cast<long long>(std::floor(by0)) - 1;
        const long long gw = static_cast<long long>(std::ceil(bx1)) - ox + 2, gh = static_cast<long long>(std::ceil(by1)) - oy + 2;
        if (gw <= 0 || gh <= 0 || gw > 8192 || gh > 8192) continue;
        for (auto& c : cs) for (auto& p : c) { p.x -= static_cast<double>(ox); p.y -= static_cast<double>(oy); }
        Raster r(gw, gh); fill_contours(r, cs); const auto cov = r.coverage();
        for (long long yy = 0; yy < gh; ++yy) { const long long Y = oy + yy; if (Y < 0 || Y >= im.h) continue;
            for (long long xx = 0; xx < gw; ++xx) { const long long X = ox + xx; if (X < 0 || X >= im.w) continue;
                uint8_t& m = mask[static_cast<std::size_t>(Y * im.w + X)]; m = std::max(m, cov[static_cast<std::size_t>(yy * gw + xx)]); } }
    }
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const unsigned m = mask[i]; if (!m) continue;
        uint8_t* o = &im.px[i * 4];
        if (rgb_image) { for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(div255(o[k] * (255 - m) + static_cast<unsigned>(ink[static_cast<std::size_t>(k)]) * m)); continue; }
        const unsigned cm = 255 - (255 - m) * o[3] / 255;   // colour takes the full ink where the canvas is transparent
        for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(div255(o[k] * (255 - cm) + static_cast<unsigned>(ink[static_cast<std::size_t>(k)]) * cm));
        o[3] = static_cast<uint8_t>(div255(o[3] * (255 - m) + static_cast<unsigned>(ink[3]) * m));
    }
}
// ------------------------------------------------------------------ shapes (no anti-aliasing; the fill replaces pixels)
inline void set_px(Image& im, long long x, long long y, const std::array<int, 4>& c) { if (x < 0 || y < 0 || x >= im.w || y >= im.h) return; uint8_t* p = im.at(x, y); for (int k = 0; k < 4; ++k) p[k] = static_cast<uint8_t>(c[static_cast<std::size_t>(k)]); }
inline void rectangle(Image& im, long long x0, long long y0, long long x1, long long y1, const std::array<int, 4>& c) {
    for (long long y = std::max(0LL, y0); y <= std::min(im.h - 1, y1); ++y) for (long long x = std::max(0LL, x0); x <= std::min(im.w - 1, x1); ++x) set_px(im, x, y, c);
}
inline void rounded_rectangle(Image& im, long long x0, long long y0, long long x1, long long y1, long long r, const std::array<int, 4>& c) {
    r = std::min(r, std::min(x1 - x0, y1 - y0) / 2); if (r < 0) r = 0;
    for (long long y = y0; y <= y1; ++y) for (long long x = x0; x <= x1; ++x) {
        long long cx = -1, cy = -1;
        if (x < x0 + r) cx = x0 + r; else if (x > x1 - r) cx = x1 - r;
        if (y < y0 + r) cy = y0 + r; else if (y > y1 - r) cy = y1 - r;
        if (cx >= 0 && cy >= 0 && (x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r + r) continue;
        set_px(im, x, y, c);
    }
}
inline void ellipse(Image& im, double x0, double y0, double x1, double y1, const std::array<int, 4>& c) {
    const double cx = (x0 + x1 + 1) / 2.0, cy = (y0 + y1 + 1) / 2.0, rx = (x1 - x0 + 1) / 2.0, ry = (y1 - y0 + 1) / 2.0;
    if (rx <= 0 || ry <= 0) return;
    for (long long y = std::max(0LL, static_cast<long long>(std::floor(y0))); y <= std::min(im.h - 1, static_cast<long long>(std::ceil(y1))); ++y)
        for (long long x = std::max(0LL, static_cast<long long>(std::floor(x0))); x <= std::min(im.w - 1, static_cast<long long>(std::ceil(x1))); ++x) {
            const double dx = (static_cast<double>(x) + 0.5 - cx) / rx, dy = (static_cast<double>(y) + 0.5 - cy) / ry;
            if (dx * dx + dy * dy <= 1.0) set_px(im, x, y, c);
        }
}
// ------------------------------------------------------------------ GaussianBlur (Pillow BoxBlur.c)
inline double gaussian_box_radius(double radius, int passes) {
    const double sigma2 = radius * radius / passes, L = std::sqrt(12.0 * sigma2 + 1.0), l = std::floor((L - 1.0) / 2.0);
    double a = (2 * l + 1) * (l * (l + 1) - 3 * sigma2); a /= 6 * (sigma2 - (l + 1) * (l + 1));
    return l + a;
}
inline void box_line(const uint8_t* in, uint8_t* out, long long n, std::size_t stride, double fradius) {
    const long long r = static_cast<long long>(fradius);
    const uint32_t ww = static_cast<uint32_t>((1u << 24) / (fradius * 2 + 1)), fw = ((1u << 24) - static_cast<uint32_t>(r * 2 + 1) * ww) / 2;
    std::vector<uint8_t> src(static_cast<std::size_t>(n)); for (long long i = 0; i < n; ++i) src[static_cast<std::size_t>(i)] = in[static_cast<std::size_t>(i) * stride];
    auto at = [&](long long i) -> uint64_t { return src[static_cast<std::size_t>(std::clamp(i, 0LL, n - 1))]; };
    uint64_t acc = 0; for (long long k = -r; k <= r; ++k) acc += at(k);
    for (long long x = 0; x < n; ++x) {
        const uint64_t bulk = acc * ww + (at(x - r - 1) + at(x + r + 1)) * fw;
        out[static_cast<std::size_t>(x) * stride] = static_cast<uint8_t>(std::min<uint64_t>(255, (bulk + (1u << 23)) >> 24));
        acc += at(x + r + 1); acc -= at(x - r);
    }
}
inline Image gaussian_blur(const Image& src, double radius, int passes = 3) {
    if (radius <= 0) return src;
    const double br = gaussian_box_radius(radius, passes);
    Image a = src;
    for (int p = 0; p < passes; ++p) { Image o = a; for (long long y = 0; y < a.h; ++y) for (int c = 0; c < 4; ++c) box_line(a.at(0, y) + c, o.at(0, y) + c, a.w, 4, br); a = std::move(o); }
    for (int p = 0; p < passes; ++p) { Image o = a; for (long long x = 0; x < a.w; ++x) for (int c = 0; c < 4; ++c) box_line(a.at(x, 0) + c, o.at(x, 0) + c, a.h, static_cast<std::size_t>(a.w) * 4, br); a = std::move(o); }
    return a;
}
inline void alpha_composite_at(Image& dst, const Image& src, long long ox, long long oy) {
    Image region = crop(dst, {ox, oy, ox + src.w, oy + src.h}); alpha_composite(region, src);
    for (long long y = 0; y < src.h; ++y) for (long long x = 0; x < src.w; ++x) { const long long X = ox + x, Y = oy + y; if (X >= 0 && Y >= 0 && X < dst.w && Y < dst.h) std::memcpy(dst.at(X, Y), region.at(x, y), 4); }
}
inline void save_rgb(const Image& im, const std::string& path) {
    std::vector<uint8_t> rgb; rgb.reserve(static_cast<std::size_t>(im.w * im.h * 3));
    for (std::size_t i = 0; i < im.px.size(); i += 4) { rgb.push_back(im.px[i]); rgb.push_back(im.px[i + 1]); rgb.push_back(im.px[i + 2]); }
    save_bytes_as(path, encode_png_raw(im.w, im.h, 2, rgb));
}
}  // namespace packtools::modder::img
