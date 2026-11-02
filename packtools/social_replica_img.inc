// replica-diff/imgdiff (replica-skill, Jake Schincariol, MIT), ported to C++: same numbers, same report, same exit codes.
// Own PNG reader (8 and 16 bit, grey / RGB / palette / alpha, the five filters; interlaced refused) on pyrt::zip's
// inflate. The --out diff image holds the same pixels; its compressed bytes differ from zlib's level-6 encoder.
namespace packtools::rp_imgdiff {
using namespace pyrt;
using namespace packtools::rp;

struct PngError : PyErr { explicit PngError(const std::string& m) : PyErr("PngError", m) {} };
struct RGB { uint8_t r, g, b; };
using Rows = std::vector<std::vector<RGB>>;
struct Img { long long w = 0, h = 0; Rows rows; };
inline const std::string SIG = std::string("\x89PNG\r\n\x1a\n", 8);

inline uint32_t be32(const std::string& s, std::size_t at) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(s[at])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(s[at + 3]));
}
inline int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}
inline std::vector<std::string> unfilter(const std::string& raw, unsigned long long height, unsigned long long stride, unsigned long long bpp) {
    std::vector<std::string> rows; std::string prev; unsigned long long pos = 0;
    for (unsigned long long y = 0; y < height; ++y) {
        if (pos + 1 + stride > raw.size()) throw PngError("image data is truncated");
        if (prev.empty() && stride) prev.assign(stride, '\0');
        const int ftype = static_cast<unsigned char>(raw[pos]);
        std::string line = raw.substr(pos + 1, stride);
        pos += 1 + stride;
        auto L = [&](unsigned long long i) { return static_cast<int>(static_cast<unsigned char>(line[i])); };
        auto P = [&](unsigned long long i) { return static_cast<int>(static_cast<unsigned char>(prev[i])); };
        if (ftype == 1) { for (unsigned long long i = bpp; i < stride; ++i) line[i] = static_cast<char>((L(i) + L(i - bpp)) & 0xFF); }
        else if (ftype == 2) { for (unsigned long long i = 0; i < stride; ++i) line[i] = static_cast<char>((L(i) + P(i)) & 0xFF); }
        else if (ftype == 3) {
            for (unsigned long long i = 0; i < stride; ++i) { const int left = i >= bpp ? L(i - bpp) : 0; line[i] = static_cast<char>((L(i) + ((left + P(i)) >> 1)) & 0xFF); }
        } else if (ftype == 4) {
            for (unsigned long long i = 0; i < stride; ++i) {
                const int left = i >= bpp ? L(i - bpp) : 0, upleft = i >= bpp ? P(i - bpp) : 0;
                line[i] = static_cast<char>((L(i) + paeth(left, P(i), upleft)) & 0xFF);
            }
        } else if (ftype != 0) throw PngError("unknown filter type " + i2s(ftype));
        rows.push_back(line);
        prev = rows.back();
    }
    return rows;
}
inline std::vector<int> samples(const std::string& line, unsigned long long width, int channels, int depth) {   // one scanline as samples
    const unsigned long long n = width * static_cast<unsigned long long>(channels);
    std::vector<int> out;
    if (depth == 8) { for (unsigned long long i = 0; i < n && i < line.size(); ++i) out.push_back(static_cast<unsigned char>(line[i])); return out; }
    if (depth == 16) { for (unsigned long long i = 0; i < n; ++i) out.push_back(static_cast<unsigned char>(line[2 * i])); return out; }
    const int per_byte = 8 / depth, mask = (1 << depth) - 1;
    for (unsigned char byte : line)
        for (int k = 0; k < per_byte; ++k) {
            out.push_back((byte >> (8 - depth * (k + 1))) & mask);
            if (out.size() == n) return out;
        }
    return out;
}
inline RGB over_white(int r, int g, int b, int a) {
    if (a == 255) return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    const int inv = 255 - a;
    return {static_cast<uint8_t>((r * a + 255 * inv) / 255), static_cast<uint8_t>((g * a + 255 * inv) / 255), static_cast<uint8_t>((b * a + 255 * inv) / 255)};
}
inline Img read_png(const std::string& path) {   // alpha is composited over white, the way a screenshot of a page renders
    const std::string data = read_text(path, false, true);
    if (data.compare(0, 8, SIG) != 0) throw PngError(path + " is not a PNG");
    unsigned long long pos = 8;
    bool have_ihdr = false; uint32_t width = 0, height = 0; int depth = 0, color = 0, interlace = 0;
    std::optional<std::vector<std::vector<int>>> plte; std::string trns, idat;
    while (pos + 8 <= data.size()) {
        const uint32_t length = be32(data, pos);
        const std::string ctype = data.substr(pos + 4, 4);
        const std::string chunk = data.substr(pos + 8, length);
        pos += 12ULL + length;
        if (ctype == "IHDR") {
            if (chunk.size() != 13) throw PyErr("struct.error", "unpack requires a buffer of 13 bytes");
            have_ihdr = true; width = be32(chunk, 0); height = be32(chunk, 4);
            depth = static_cast<unsigned char>(chunk[8]); color = static_cast<unsigned char>(chunk[9]); interlace = static_cast<unsigned char>(chunk[12]);
        } else if (ctype == "PLTE") {
            std::vector<std::vector<int>> p;
            for (std::size_t i = 0; i < chunk.size(); i += 3) { std::vector<int> t; for (std::size_t k = i; k < i + 3 && k < chunk.size(); ++k) t.push_back(static_cast<unsigned char>(chunk[k])); p.push_back(t); }
            plte = p;
        } else if (ctype == "tRNS") trns = chunk;
        else if (ctype == "IDAT") idat += chunk;
        else if (ctype == "IEND") break;
    }
    if (!have_ihdr) throw PngError(path + " has no IHDR chunk");
    if (interlace) throw PngError(path + " is interlaced. Re-save it without interlacing (on a Mac: sips -s format png in.png --out out.png)");
    int channels = 0;
    switch (color) { case 0: channels = 1; break; case 2: channels = 3; break; case 3: channels = 1; break; case 4: channels = 2; break; case 6: channels = 4; break; default: break; }
    if (!channels) throw PngError("unsupported PNG colour type " + i2s(color));
    if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) throw PngError("unsupported bit depth " + i2s(depth));
    if (color == 3 && !plte) throw PngError("palette PNG without a palette");
    const unsigned long long bits = static_cast<unsigned long long>(depth) * static_cast<unsigned long long>(channels);
    const unsigned long long bpp = std::max(1ULL, bits / 8), stride = (width * bits + 7) / 8;
    const std::string raw = zip::zlib_decompress(idat);
    const auto lines = unfilter(raw, height, stride, bpp);
    const int scale = depth < 8 ? 255 / ((1 << depth) - 1) : 1;
    const std::string alpha_pal = color == 3 ? trns : std::string();
    Img img; img.w = width; img.h = height;
    for (const auto& line : lines) {
        const auto s = samples(line, width, channels, depth);
        std::vector<RGB> row;
        if (color == 0) { for (int v : s) { const auto g = static_cast<uint8_t>(v * scale); row.push_back({g, g, g}); } }
        else if (color == 2) { for (std::size_t i = 0; i < s.size(); i += 3) if (i + 2 >= s.size()) throw PyErr("IndexError", "list index out of range"); else row.push_back({static_cast<uint8_t>(s[i]), static_cast<uint8_t>(s[i + 1]), static_cast<uint8_t>(s[i + 2])}); }
        else if (color == 3) {
            for (int idx : s) {
                int r = 0, g = 0, b = 0;
                if (static_cast<std::size_t>(idx) < plte->size()) {
                    const auto& t = (*plte)[static_cast<std::size_t>(idx)];
                    if (t.size() != 3) throw PyErr("ValueError", "not enough values to unpack (expected 3, got " + i2s(static_cast<long long>(t.size())) + ")");
                    r = t[0]; g = t[1]; b = t[2];
                }
                const int a = static_cast<std::size_t>(idx) < alpha_pal.size() ? static_cast<unsigned char>(alpha_pal[static_cast<std::size_t>(idx)]) : 255;
                row.push_back(over_white(r, g, b, a));
            }
        } else if (color == 4) { for (std::size_t i = 0; i < s.size(); i += 2) if (i + 1 >= s.size()) throw PyErr("IndexError", "list index out of range"); else row.push_back(over_white(s[i], s[i], s[i], s[i + 1])); }
        else { for (std::size_t i = 0; i < s.size(); i += 4) if (i + 3 >= s.size()) throw PyErr("IndexError", "list index out of range"); else row.push_back(over_white(s[i], s[i + 1], s[i + 2], s[i + 3])); }
        img.rows.push_back(std::move(row));
    }
    return img;
}
inline void write_png(const std::string& path, long long width, long long height, const Rows& rows) {   // 8 bit RGB
    std::FILE* f = open_file(path, "wb");
    if (!f) throw OSError(errno ? errno : EACCES, path);
    if (width < 0 || height < 0 || width > 0xFFFFFFFFLL || height > 0xFFFFFFFFLL) { std::fclose(f); throw PyErr("struct.error", "'I' format requires 0 <= number <= 4294967295"); }
    std::string raw;
    for (const auto& row : rows) { raw += '\0'; for (const auto& p : row) { raw += static_cast<char>(p.r); raw += static_cast<char>(p.g); raw += static_cast<char>(p.b); } }
    const auto u32 = [](uint32_t v) { std::string o(4, '\0'); for (int k = 0; k < 4; ++k) o[static_cast<std::size_t>(k)] = static_cast<char>((v >> (24 - 8 * k)) & 0xFF); return o; };
    const auto chunk = [&](const std::string& kind, const std::string& body) { return u32(static_cast<uint32_t>(body.size())) + kind + body + u32(zip::crc32(kind + body)); };
    const std::string ihdr = u32(static_cast<uint32_t>(width)) + u32(static_cast<uint32_t>(height)) + std::string("\x08\x02\x00\x00\x00", 5);
    const std::string out = SIG + chunk("IHDR", ihdr) + chunk("IDAT", zip::zlib_compress(raw)) + chunk("IEND", "");
    std::fwrite(out.data(), 1, out.size(), f); std::fclose(f);
}

// ------------------------------------------------------------------- comparing
inline Rows resize(const Rows& rows, long long width, long long height, long long new_w, long long new_h) {   // nearest neighbour
    std::vector<long long> xs, ys;
    for (long long x = 0; x < new_w; ++x) xs.push_back(std::min(width - 1, static_cast<long long>(static_cast<double>(x * width) / static_cast<double>(new_w))));
    for (long long y = 0; y < new_h; ++y) ys.push_back(std::min(height - 1, static_cast<long long>(static_cast<double>(y * height) / static_cast<double>(new_h))));
    Rows out;
    for (long long y : ys) {
        if (y < 0 || static_cast<std::size_t>(y) >= rows.size()) throw PyErr("IndexError", "list index out of range");
        const auto& src = rows[static_cast<std::size_t>(y)];
        std::vector<RGB> r; r.reserve(xs.size());
        for (long long x : xs) { if (x < 0 || static_cast<std::size_t>(x) >= src.size()) throw PyErr("IndexError", "list index out of range"); r.push_back(src[static_cast<std::size_t>(x)]); }
        out.push_back(std::move(r));
    }
    return out;
}
inline Img to_width(const Img& img, long long target) {
    if (img.w == target) return img;
    if (img.w == 0) throw PyErr("ZeroDivisionError", "float division by zero");
    const long long new_h = std::max(1LL, static_cast<long long>(std::nearbyint(static_cast<double>(img.h * target) / static_cast<double>(img.w))));
    Img o; o.w = target; o.h = new_h; o.rows = resize(img.rows, img.w, img.h, target, new_h);
    return o;
}
using Grid = std::vector<std::vector<int>>;
inline Grid gray(const Rows& rows) {
    Grid g; for (const auto& row : rows) { std::vector<int> r; r.reserve(row.size()); for (const auto& p : row) r.push_back((299 * p.r + 587 * p.g + 114 * p.b) / 1000); g.push_back(std::move(r)); }
    return g;
}
inline Grid edges(const Grid& g, int threshold) {
    const std::size_t h = g.size(), w = h ? g[0].size() : 0;
    Grid out;
    for (std::size_t y = 0; y < h; ++y) {
        const auto& row = g[y]; const auto& below = y + 1 < h ? g[y + 1] : row;
        std::vector<int> e(w);
        for (std::size_t x = 0; x < w; ++x) { const int right = x + 1 < w ? row[x + 1] : row[x]; e[x] = std::abs(right - row[x]) + std::abs(below[x] - row[x]) > threshold ? 1 : 0; }
        out.push_back(std::move(e));
    }
    return out;
}
struct Region { long long r0, c0, r1, c1, n; };
inline std::vector<Region> regions_of(const std::vector<std::vector<bool>>& flags, long long n_rows, long long n_cols) {   // 4-connected groups
    std::set<std::pair<long long, long long>> seen; std::vector<Region> out;
    for (long long r = 0; r < n_rows; ++r)
        for (long long c = 0; c < n_cols; ++c) {
            if (!flags[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] || seen.count({r, c})) continue;
            std::vector<std::pair<long long, long long>> stack = {{r, c}}, cells; seen.insert({r, c});
            while (!stack.empty()) {
                const auto [cr, cc] = stack.back(); stack.pop_back(); cells.push_back({cr, cc});
                for (const auto& [nr, nc] : {std::pair<long long, long long>{cr + 1, cc}, {cr - 1, cc}, {cr, cc + 1}, {cr, cc - 1}})
                    if (nr >= 0 && nr < n_rows && nc >= 0 && nc < n_cols && flags[static_cast<std::size_t>(nr)][static_cast<std::size_t>(nc)] && !seen.count({nr, nc})) { seen.insert({nr, nc}); stack.push_back({nr, nc}); }
            }
            Region g{cells[0].first, cells[0].second, cells[0].first, cells[0].second, static_cast<long long>(cells.size())};
            for (const auto& [a, b] : cells) { g.r0 = std::min(g.r0, a); g.c0 = std::min(g.c0, b); g.r1 = std::max(g.r1, a); g.c1 = std::max(g.c1, b); }
            out.push_back(g);
        }
    std::stable_sort(out.begin(), out.end(), [](const Region& a, const Region& b) { return a.n > b.n; });
    return out;
}
inline std::string where(long long r0, long long c0, long long r1, long long c1, long long n_rows, long long n_cols) {
    const double mid_r = n_rows > 1 ? static_cast<double>(r0 + r1) / 2.0 / static_cast<double>(std::max(1LL, n_rows - 1)) : 0.5;
    const double mid_c = n_cols > 1 ? static_cast<double>(c0 + c1) / 2.0 / static_cast<double>(std::max(1LL, n_cols - 1)) : 0.5;
    const std::string v = mid_r < 0.34 ? "top" : (mid_r > 0.66 ? "bottom" : "middle");
    const std::string hz = mid_c < 0.34 ? "left" : (mid_c > 0.66 ? "right" : "centre");
    return v + " " + hz;
}
inline std::string verdict(double score) { return score >= 90 ? "matches" : score >= 75 ? "close" : score >= 50 ? "partly" : "different"; }
inline long long floordiv(long long a, long long b) {
    if (b == 0) throw PyErr("ZeroDivisionError", "integer division or modulo by zero");
    long long q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q;
}
inline std::pair<Val, Rows> compare(const Img& img_a, const Img& img_b, const std::string& mode, long long width, long long cols, long long tolerance, int edge_threshold = 40) {
    const long long wa = img_a.w, ha = img_a.h, wb = img_b.w, hb = img_b.h;
    const long long target = std::min({width, wa, wb});
    const Img a = to_width(img_a, target), b = to_width(img_b, target);
    const long long w = target, h = std::min(a.h, b.h);
    const Rows ra(a.rows.begin(), a.rows.begin() + static_cast<long>(std::min<std::size_t>(a.rows.size(), static_cast<std::size_t>(h))));
    const Rows rb(b.rows.begin(), b.rows.begin() + static_cast<long>(std::min<std::size_t>(b.rows.size(), static_cast<std::size_t>(h))));
    const double height_delta = a.h ? static_cast<double>(b.h - a.h) / static_cast<double>(a.h) * 100.0 : 0.0;
    if (w == 0) throw PyErr("ZeroDivisionError", "float division by zero");
    const double to_orig = static_cast<double>(wa) / static_cast<double>(w);
    const Grid ga = gray(ra);
    Rows dim; for (const auto& row : ga) { std::vector<RGB> r; for (int v : row) { const auto c = static_cast<uint8_t>(200 + v / 5); r.push_back({c, c, c}); } dim.push_back(std::move(r)); }
    Val report = sdict({{"mode", Val::str(mode)}, {"original", sdict({{"width", Val::integer(wa)}, {"height", Val::integer(ha)}})},
                        {"clone", sdict({{"width", Val::integer(wb)}, {"height", Val::integer(hb)}})},
                        {"compared_at", sdict({{"width", Val::integer(w)}, {"height", Val::integer(h)}})}, {"height_delta_pct", Val::flt(round_n(height_delta, 1))}});
    double score = 100.0; long long cell = 1, n_rows = 0;
    std::vector<std::vector<bool>> flags;
    const auto make_flags = [&]() { cell = std::max(1LL, floordiv(w, cols)); n_rows = static_cast<long long>(std::ceil(static_cast<double>(h) / static_cast<double>(cell)));
                                    flags.assign(static_cast<std::size_t>(std::max(0LL, n_rows)), std::vector<bool>(static_cast<std::size_t>(std::max(0LL, cols)), false)); };
    if (mode == "pixel") {
        long long changed = 0; std::vector<std::vector<bool>> px;
        for (long long y = 0; y < h; ++y) {
            std::vector<bool> fr;
            for (long long x = 0; x < w; ++x) {
                const RGB& pa = ra[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]; const RGB& pb = rb[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                const int d = std::max({std::abs(pa.r - pb.r), std::abs(pa.g - pb.g), std::abs(pa.b - pb.b)});
                const bool hit = d > tolerance; fr.push_back(hit);
                if (hit) { ++changed; dim[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = {220, 30, 30}; }
            }
            px.push_back(std::move(fr));
        }
        long long total = w * h; if (total == 0) total = 1;
        score = 100.0 * (1 - static_cast<double>(changed) / static_cast<double>(total));
        report.d->set("changed_pixels", Val::integer(changed));
        make_flags();   // summarise pixel hits as grid regions so the report stays readable
        for (long long r = 0; r < n_rows; ++r)
            for (long long c = 0; c < cols; ++c) {
                long long hits = 0, area = 0;
                for (long long y = r * cell; y < std::min(h, (r + 1) * cell); ++y)
                    for (long long x = c * cell; x < std::min(w, (c + 1) * cell); ++x) { ++area; hits += px[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] ? 1 : 0; }
                flags[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = area > 0 && static_cast<double>(hits) / static_cast<double>(area) > 0.02;
            }
    } else {
        const Grid ea = edges(ga, edge_threshold), eb = edges(gray(rb), edge_threshold);
        make_flags();
        std::vector<double> sims;
        for (long long r = 0; r < n_rows; ++r)
            for (long long c = 0; c < cols; ++c) {
                const long long y0 = r * cell, y1 = std::min(h, (r + 1) * cell), x0 = c * cell, x1 = std::min(w, (c + 1) * cell);
                const long long area = (y1 - y0) * (x1 - x0);
                if (area <= 0) continue;
                long long sa = 0, sb = 0;
                for (long long y = y0; y < y1; ++y) for (long long x = x0; x < x1; ++x) { sa += ea[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]; sb += eb[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]; }
                const double da = static_cast<double>(sa) / static_cast<double>(area), db = static_cast<double>(sb) / static_cast<double>(area);
                const double top = std::max(da, db);
                if (top < 0.01) continue;   // empty in both: says nothing about the layout
                const double sim = 1.0 - std::fabs(da - db) / top;
                sims.push_back(sim);
                if (sim < 0.6) {
                    flags[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] = true;
                    for (long long y = y0; y < y1; ++y)
                        for (long long x = x0; x < x1; ++x) {
                            RGB& p = dim[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]; const int g0 = p.r;
                            p = {static_cast<uint8_t>(std::min(255, g0 + 30)), static_cast<uint8_t>(g0 / 3), static_cast<uint8_t>(g0 / 3)};
                        }
                }
            }
        if (!sims.empty()) { double s = 0; for (double x : sims) s += x; score = 100.0 * s / static_cast<double>(sims.size()); }
    }
    Val regions = Val::list();
    const long long nfl = static_cast<long long>(flags.size());
    for (const auto& g : regions_of(flags, nfl, cols)) {
        const long long x = static_cast<long long>(static_cast<double>(g.c0 * cell) * to_orig), y = static_cast<long long>(static_cast<double>(g.r0 * cell) * to_orig);
        regions.l->push_back(sdict({{"x", Val::integer(x)}, {"y", Val::integer(y)},
                                    {"w", Val::integer(std::min(wa - x, static_cast<long long>(static_cast<double>((g.c1 - g.c0 + 1) * cell) * to_orig)))},
                                    {"h", Val::integer(std::min(ha - y, static_cast<long long>(static_cast<double>((g.r1 - g.r0 + 1) * cell) * to_orig)))},
                                    {"cells", Val::integer(g.n)}, {"where", Val::str(where(g.r0, g.c0, g.r1, g.c1, nfl, cols))}}));
    }
    report.d->set("score", Val::flt(round_n(score, 1)));
    report.d->set("verdict", Val::str(verdict(score)));
    report.d->set("regions", regions);
    return {report, dim};
}
inline std::string render_text(const Val& report, const std::string& a_name, const std::string& b_name) {
    std::vector<std::string> lines;
    const double sc = get(report, "score").f;
    const long long bar = static_cast<long long>(std::nearbyint(sc / 100.0 * 24));
    lines.push_back("  " + a_name + " vs " + b_name);
    lines.push_back("  " + ljust(upper(get(report, "mode").s), 6) + " " + std::string(static_cast<std::size_t>(std::max(0LL, bar)), '#') +
                    std::string(static_cast<std::size_t>(std::max(0LL, 24 - bar)), '.') + "  " + pct5(sc, 1) + "  " + upper(get(report, "verdict").s));
    const double hd = get(report, "height_delta_pct").f;
    if (std::fabs(hd) >= 5) lines.push_back("  height: the clone is " + fmt_f(std::fabs(hd), 0) + "% " + (hd > 0 ? "taller" : "shorter") + " than the original");
    const List& regs = *get(report, "regions").l;
    if (!regs.empty()) {
        lines.push_back("  where they differ (original's pixels, biggest first):");
        for (std::size_t k = 0; k < regs.size() && k < 8; ++k)
            lines.push_back("    " + ljust(get(regs[k], "where").s, 14) + " x=" + i2s(get(regs[k], "x").i) + " y=" + i2s(get(regs[k], "y").i) + " w=" +
                            i2s(get(regs[k], "w").i) + " h=" + i2s(get(regs[k], "h").i));
    } else {
        lines.push_back("  no differing regions");
    }
    return join(lines, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("imgdiff", "Screenshot diff for replica-diff. Standard library only.");
    p.pos("original").help("screenshot of the original app (PNG)");
    p.pos("clone").help("the same screen in your clone (PNG)");
    p.opt({"--mode"}).choices({"layout", "pixel"}).def("layout");
    p.opt({"--width"}).integer().def(480).help("compare at this width (default 480)");
    p.opt({"--cols"}).integer().def(12).help("grid columns (default 12)");
    p.opt({"--tolerance"}).integer().def(24).help("pixel mode: per-channel difference ignored (default 24)");
    p.opt({"--out"}).help("write a diff image here (PNG)");
    p.opt({"--json"}).store_true().help("print JSON instead of text");
    p.opt({"--fail-under"}).number().help("exit 1 when the score is below this");
    const Val a = parse_args(p, argv);
    const std::string orig = S(a, "original"), clone = S(a, "clone"), outp = S(a, "out");
    Img ia, ib;
    try { ia = read_png(orig); ib = read_png(clone); }
    catch (const PyErr& e) {
        if (e.type != "PngError" && e.type != "OSError" && e.type != "zlib.error") throw;
        eprint(std::string("imgdiff: ") + e.what()); return 2;
    }
    auto [report, diff] = compare(ia, ib, S(a, "mode"), get(a, "width").i, get(a, "cols").i, get(a, "tolerance").i);
    report.d->set("files", sdict({{"original", Val::str(orig)}, {"clone", Val::str(clone)}}));
    if (!outp.empty()) {
        write_png(outp, get(get(report, "compared_at"), "width").i, get(get(report, "compared_at"), "height").i, diff);
        report.d->set("diff_image", Val::str(outp));
    }
    if (B(a, "json")) print_json(report);
    else {
        print(render_text(report, orig, clone));
        if (!outp.empty()) print("  diff image: " + outp);
    }
    const Val fu = get(a, "fail_under");
    if (!fu.is_none() && get(report, "score").f < fu.num()) return 1;
    return 0;
}
PACKTOOL("replica-diff/imgdiff", main);
}  // namespace packtools::rp_imgdiff
