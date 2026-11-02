// Writes the PNG fixtures for packtools/tests/replica.sh (replica-diff/imgdiff).
// Uses the system zlib (level 6, like Python's zlib.compress) so the tool's own
// inflate is checked against an independent deflate.
//   g++ -std=c++20 -O1 packtools/tests/fixtures/mkpng.cpp -o /tmp/mkpng -lz
//   /tmp/mkpng packtools/tests/fixtures/replica-imgdiff
// The page()/rect()/encode() helpers mirror the upstream replica tests.
#include <zlib.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;
struct RGB { int r, g, b; };
using Rows = std::vector<std::vector<RGB>>;
static std::string DIR;

static Rows canvas(int w, int h, RGB c = {255, 255, 255}) { return Rows(static_cast<size_t>(h), std::vector<RGB>(static_cast<size_t>(w), c)); }

static void rect(Rows& rows, int x, int y, int w, int h, RGB c) {
    int H = static_cast<int>(rows.size()), W = static_cast<int>(rows[0].size());
    for (int yy = y; yy < std::min(H, y + h); ++yy)
        for (int xx = x; xx < std::min(W, x + w); ++xx) rows[static_cast<size_t>(yy)][static_cast<size_t>(xx)] = c;
}

static Rows page(int w = 240, int h = 360, RGB header = {20, 20, 60}, RGB accent = {40, 110, 230},
                 RGB text = {30, 30, 30}, int button_y = 250) {
    Rows rows = canvas(w, h);
    rect(rows, 0, 0, w, 40, header);
    for (int i = 0; i < 6; ++i) rect(rows, 20, 70 + i * 22, w - 60 - (i % 3) * 30, 8, text);
    rect(rows, 20, button_y, 120, 36, accent);
    return rows;
}

static Rows resize(const Rows& rows, int width, int height, int nw, int nh) {
    Rows out;
    for (int y = 0; y < nh; ++y) {
        int sy = std::min(height - 1, static_cast<int>(static_cast<double>(y) * height / nh));
        std::vector<RGB> r;
        for (int x = 0; x < nw; ++x) r.push_back(rows[static_cast<size_t>(sy)][static_cast<size_t>(std::min(width - 1, static_cast<int>(static_cast<double>(x) * width / nw)))]);
        out.push_back(r);
    }
    return out;
}

static void be32(Bytes& b, uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s)); }

static void chunk(Bytes& out, const char* kind, const Bytes& body) {
    be32(out, static_cast<uint32_t>(body.size()));
    Bytes kb(kind, kind + 4);
    kb.insert(kb.end(), body.begin(), body.end());
    out.insert(out.end(), kb.begin(), kb.end());
    be32(out, static_cast<uint32_t>(crc32(0, kb.data(), static_cast<uInt>(kb.size()))));
}

static Bytes zcompress(const Bytes& raw) {
    uLongf n = compressBound(static_cast<uLong>(raw.size()));
    Bytes z(n);
    if (compress2(z.data(), &n, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) { std::fprintf(stderr, "compress failed\n"); std::exit(1); }
    z.resize(n);
    return z;
}

static void save(const std::string& name, const Bytes& data) {
    std::string p = DIR + "/" + name;
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) { std::perror(p.c_str()); std::exit(1); }
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

struct Enc {
    int w = 0, h = 0, ctype = 2, depth = 8, interlace = 0;
    int ftype = 0;          // filter for every row; -1 = row y uses y % 5
    std::vector<RGB> plte;
    Bytes trns;
    bool with_plte = false;
    long trim = 0;          // drop this many bytes from the end of the raw data
    int bad_filter = -1;    // force this filter byte on row 0
};

static Bytes ihdr(int w, int h, int depth, int ctype, int interlace) {
    Bytes b;
    be32(b, static_cast<uint32_t>(w));
    be32(b, static_cast<uint32_t>(h));
    b.push_back(static_cast<uint8_t>(depth));
    b.push_back(static_cast<uint8_t>(ctype));
    b.push_back(0); b.push_back(0);
    b.push_back(static_cast<uint8_t>(interlace));
    return b;
}

static Bytes sig() { return Bytes{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}; }

static void encode(const std::string& name, const Enc& e, const std::vector<Bytes>& samples) {
    int channels = e.ctype == 0 ? 1 : e.ctype == 2 ? 3 : e.ctype == 3 ? 1 : e.ctype == 4 ? 2 : 4;
    int bpp = std::max(1, e.depth * channels / 8);
    Bytes raw, prev;
    int y = 0;
    for (const Bytes& line : samples) {
        if (prev.size() != line.size()) prev.assign(line.size(), 0);
        int ft = e.ftype < 0 ? y % 5 : e.ftype;
        Bytes out;
        for (size_t i = 0; i < line.size(); ++i) {
            int v = line[i];
            int left = i >= static_cast<size_t>(bpp) ? line[i - static_cast<size_t>(bpp)] : 0;
            int up = prev[i];
            int ul = i >= static_cast<size_t>(bpp) ? prev[i - static_cast<size_t>(bpp)] : 0;
            int f = ft == 0 ? v : ft == 1 ? v - left : ft == 2 ? v - up : ft == 3 ? v - ((left + up) >> 1) : v - paeth(left, up, ul);
            out.push_back(static_cast<uint8_t>(f & 0xFF));
        }
        raw.push_back(static_cast<uint8_t>(y == 0 && e.bad_filter >= 0 ? e.bad_filter : ft));
        raw.insert(raw.end(), out.begin(), out.end());
        prev = line;
        ++y;
    }
    if (e.trim > 0) raw.resize(raw.size() - static_cast<size_t>(e.trim));
    Bytes data = sig();
    chunk(data, "IHDR", ihdr(e.w, e.h, e.depth, e.ctype, e.interlace));
    if (!e.plte.empty()) {
        Bytes p;
        for (const RGB& c : e.plte) { p.push_back(static_cast<uint8_t>(c.r)); p.push_back(static_cast<uint8_t>(c.g)); p.push_back(static_cast<uint8_t>(c.b)); }
        chunk(data, "PLTE", p);
    }
    if (!e.trns.empty()) chunk(data, "tRNS", e.trns);
    chunk(data, "IDAT", zcompress(raw));
    chunk(data, "IEND", {});
    save(name, data);
}

static std::vector<Bytes> rgb_samples(const Rows& rows) {
    std::vector<Bytes> s;
    for (const auto& r : rows) {
        Bytes b;
        for (const RGB& p : r) { b.push_back(static_cast<uint8_t>(p.r)); b.push_back(static_cast<uint8_t>(p.g)); b.push_back(static_cast<uint8_t>(p.b)); }
        s.push_back(b);
    }
    return s;
}

static void rgb(const std::string& name, const Rows& rows, int ftype = -1) {
    Enc e;
    e.w = static_cast<int>(rows[0].size());
    e.h = static_cast<int>(rows.size());
    e.ftype = ftype;
    encode(name, e, rgb_samples(rows));
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: mkpng OUTDIR\n"); return 2; }
    DIR = argv[1];

    // ---- upstream Compare / Cli fixtures (every row cycles the 5 filters)
    rgb("page.png", page());
    rgb("page_rebrand.png", page(240, 360, {120, 20, 40}, {20, 160, 90}, {60, 50, 40}));
    rgb("page_moved.png", page(240, 360, {20, 20, 60}, {40, 110, 230}, {30, 30, 30}, 310));
    rgb("blank.png", canvas(240, 360));
    Rows small = page(120, 180);
    rgb("page_small.png", small);
    rgb("page_retina.png", resize(small, 120, 180, 240, 360));
    Rows taller = page();
    Rows extra = canvas(240, 90);
    taller.insert(taller.end(), extra.begin(), extra.end());
    rgb("page_taller.png", taller);

    // ---- upstream ReadWrite fixtures, each with an RGB twin to compare against
    {
        const int w = 7, h = 5;
        std::vector<Bytes> flat;
        Rows ref;
        for (int y = 0; y < h; ++y) {
            Bytes b;
            std::vector<RGB> r;
            for (int x = 0; x < w; ++x) {
                RGB c{x * 30 % 256, y * 50 % 256, (x * y * 17) % 256};
                b.push_back(static_cast<uint8_t>(c.r)); b.push_back(static_cast<uint8_t>(c.g)); b.push_back(static_cast<uint8_t>(c.b)); b.push_back(255);
                r.push_back(c);
            }
            flat.push_back(b);
            ref.push_back(r);
        }
        for (int f = 0; f < 5; ++f) {
            Enc e; e.w = w; e.h = h; e.ctype = 6; e.ftype = f;
            encode("filter" + std::to_string(f) + ".png", e, flat);
        }
        rgb("filter_ref.png", ref, 0);
    }
    { Enc e; e.w = 1; e.h = 1; e.ctype = 6; e.ftype = 0; encode("alpha0.png", e, {Bytes{0, 0, 0, 0}}); }
    rgb("white1.png", canvas(1, 1), 0);
    { Enc e; e.w = 3; e.h = 1; e.ctype = 0; e.ftype = 0; encode("grey.png", e, {Bytes{0, 128, 255}}); }
    rgb("grey_ref.png", Rows{{{0, 0, 0}, {128, 128, 128}, {255, 255, 255}}}, 0);
    { Enc e; e.w = 4; e.h = 1; e.ctype = 3; e.depth = 2; e.ftype = 0; e.plte = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {9, 9, 9}}; encode("pal2.png", e, {Bytes{0x1B}}); }
    rgb("pal2_ref.png", Rows{{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {9, 9, 9}}}, 0);
    { Enc e; e.w = 1; e.h = 1; e.ctype = 2; e.depth = 16; e.ftype = 0; encode("rgb16.png", e, {Bytes{0x12, 0x34, 0xAB, 0xCD, 0xEF, 0x01}}); }
    rgb("rgb16_ref.png", Rows{{{0x12, 0xAB, 0xEF}}}, 0);
    { Enc e; e.w = 1; e.h = 1; e.ctype = 2; e.interlace = 1; e.ftype = 0; encode("interlaced.png", e, {Bytes{1, 2, 3}}); }
    save("gif.png", Bytes{'G', 'I', 'F', '8', '9', 'a'});

    // ---- own fixtures: a 16x16 black square on 64x48 white (hand-computed expectations)
    Rows sq = canvas(64, 48);
    rect(sq, 8, 8, 16, 16, {0, 0, 0});
    rgb("sq.png", sq);
    Rows sq_moved = canvas(64, 48);
    rect(sq_moved, 16, 8, 16, 16, {0, 0, 0});
    rgb("sq_moved.png", sq_moved);
    Rows sq_blue = canvas(64, 48, {230, 230, 230});
    rect(sq_blue, 8, 8, 16, 16, {0, 0, 128});
    rgb("sq_blue.png", sq_blue);
    rgb("sq_blank.png", canvas(64, 48));
    Rows sq2 = canvas(128, 96);
    rect(sq2, 16, 16, 32, 32, {0, 0, 0});
    rgb("sq_2x.png", sq2);
    // the same square in other encodings
    {
        std::vector<Bytes> g16, b1, ga, rgba, pal4, palt;
        for (int y = 0; y < 48; ++y) {
            Bytes a, b, c, d, p, q;
            for (int x = 0; x < 64; ++x) {
                bool in = x >= 8 && x < 24 && y >= 8 && y < 24;
                // 16-bit grey: high byte decides (0x00FF -> 0, 0xFF00 -> 255)
                if (in) { a.push_back(0x00); a.push_back(0xFF); } else { a.push_back(0xFF); a.push_back(0x00); }
                c.push_back(in ? 0 : 255); c.push_back(in ? 128 : 255);     // grey+alpha: 0 at alpha 128 -> 127
                d.push_back(0); d.push_back(0); d.push_back(0); d.push_back(in ? 255 : 0); // transparent -> white
            }
            for (int x = 0; x < 64; x += 8) {
                int v = 0;
                for (int k = 0; k < 8; ++k) { bool in = x + k >= 8 && x + k < 24 && y >= 8 && y < 24; v = (v << 1) | (in ? 0 : 1); }
                b.push_back(static_cast<uint8_t>(v));
            }
            for (int x = 0; x < 64; x += 2) {
                auto idx = [&](int xx) { bool in = xx >= 8 && xx < 24 && y >= 8 && y < 24; return in ? 5 : 0; }; // 5 is past the palette -> black
                p.push_back(static_cast<uint8_t>((idx(x) << 4) | idx(x + 1)));
                auto idt = [&](int xx) { bool in = xx >= 8 && xx < 24 && y >= 8 && y < 24; return in ? 1 : 0; };
                q.push_back(static_cast<uint8_t>((idt(x) << 4) | idt(x + 1)));
            }
            g16.push_back(a); b1.push_back(b); ga.push_back(c); rgba.push_back(d); pal4.push_back(p); palt.push_back(q);
        }
        { Enc e; e.w = 64; e.h = 48; e.ctype = 0; e.depth = 16; e.ftype = -1; encode("sq_grey16.png", e, g16); }
        { Enc e; e.w = 64; e.h = 48; e.ctype = 0; e.depth = 1; e.ftype = -1; encode("sq_bit1.png", e, b1); }
        { Enc e; e.w = 64; e.h = 48; e.ctype = 4; e.ftype = -1; encode("sq_greyalpha.png", e, ga); }
        { Enc e; e.w = 64; e.h = 48; e.ctype = 6; e.ftype = -1; encode("sq_rgba.png", e, rgba); }
        { Enc e; e.w = 64; e.h = 48; e.ctype = 3; e.depth = 4; e.ftype = -1; e.plte = {{255, 255, 255}, {200, 0, 0}, {0, 200, 0}}; encode("sq_pal4.png", e, pal4); }
        // palette with tRNS: index 1 is black at alpha 0 -> composites to white, so this is blank
        { Enc e; e.w = 64; e.h = 48; e.ctype = 3; e.depth = 4; e.ftype = -1; e.plte = {{255, 255, 255}, {0, 0, 0}}; e.trns = {255, 0}; encode("sq_paltrns.png", e, palt); }
        Rows sq127 = canvas(64, 48);
        rect(sq127, 8, 8, 16, 16, {127, 127, 127});
        rgb("sq_127.png", sq127);
    }

    // ---- expected --out diff images (dim grey = 200 + grey // 5)
    {
        auto dimv = [&](int x, int y) { return 200 + (sq[static_cast<size_t>(y)][static_cast<size_t>(x)].r == 0 ? 0 : 255 / 5); };
        Rows px = canvas(64, 48), lay = canvas(64, 48);
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x) {
                int g = dimv(x, y);
                bool a = x >= 8 && x < 24 && y >= 8 && y < 24, b = x >= 16 && x < 32 && y >= 8 && y < 24;
                px[static_cast<size_t>(y)][static_cast<size_t>(x)] = a != b ? RGB{220, 30, 30} : RGB{g, g, g};
                // sq vs sq_blank, layout: the 12-cell ring r1..4 x c1..4 (cell 5) is flagged
                int r = y / 5, c = x / 5;
                bool ring = x < 60 && r >= 1 && r <= 4 && c >= 1 && c <= 4 && (r == 1 || r == 4 || c == 1 || c == 4);
                lay[static_cast<size_t>(y)][static_cast<size_t>(x)] = ring ? RGB{std::min(255, g + 30), g / 3, g / 3} : RGB{g, g, g};
            }
        rgb("expect_pixel_diff.png", px);
        rgb("expect_layout_diff.png", lay);
    }

    // ---- broken files
    {
        std::vector<Bytes> one = {Bytes{1, 2, 3, 4, 5, 6}};
        { Enc e; e.w = 2; e.h = 1; e.ftype = 0; e.trim = 2; encode("bad_truncated.png", e, one); }
        { Enc e; e.w = 2; e.h = 1; e.ftype = 0; e.bad_filter = 7; encode("bad_filter.png", e, one); }
        { Enc e; e.w = 1; e.h = 1; e.ctype = 5; e.ftype = 0; encode("bad_ctype.png", e, {Bytes{1, 2}}); }
        { Enc e; e.w = 1; e.h = 1; e.ctype = 0; e.depth = 3; e.ftype = 0; encode("bad_depth.png", e, {Bytes{1}}); }
        { Enc e; e.w = 1; e.h = 1; e.ctype = 3; e.ftype = 0; encode("bad_noplte.png", e, {Bytes{0}}); }
        Bytes d = sig();
        chunk(d, "IEND", {});
        save("bad_noihdr.png", d);
        d = sig();
        chunk(d, "IHDR", Bytes(12, 0));
        chunk(d, "IEND", {});
        save("bad_ihdr12.png", d);
        d = sig();
        chunk(d, "IHDR", ihdr(1, 1, 8, 2, 0));
        chunk(d, "IDAT", Bytes{0x12, 0x34, 0x56});
        chunk(d, "IEND", {});
        save("bad_zlib.png", d);
    }
    std::printf("ok\n");
    return 0;
}
