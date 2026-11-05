// modder_formats.inc - C++ ports of the reusable file-format tools in universal-modder's examples (Rehan and
// universal-modder contributors, MIT), so they run without Python, numpy or Pillow:
//   um sld   examples/aoe2-de-civ/sld.py            Age of Empires II DE .sld sprites: info, roundtrip, to-png, and
//                                                    `write` (sld.write() from the command line: PNG frames -> .sld)
//   um vpk   examples/outer-wilds-portal-gun/tools/vpk.py   Valve VPK v1/v2 archives: list and extract
//   um vtf   examples/outer-wilds-portal-gun/tools/vtf.py   Valve VTF 7.0-7.5 textures: the top mip as a PNG
// The rest of the examples (one mod's build scripts, the Minecraft passthrough hosts and video directors, the Portal 2
// model converter) are case studies tied to one setup and stay in the upstream repository.
#include <map>
#include <tuple>
namespace packtools::modder::fmtutil {
using namespace pyrt;
inline uint16_t u16(const std::string& d, std::size_t at) { return static_cast<uint16_t>(static_cast<unsigned char>(d[at]) | (static_cast<unsigned char>(d[at + 1]) << 8)); }
inline int16_t s16(const std::string& d, std::size_t at) { return static_cast<int16_t>(u16(d, at)); }
inline uint32_t u32(const std::string& d, std::size_t at) { return static_cast<uint32_t>(u16(d, at)) | (static_cast<uint32_t>(u16(d, at + 2)) << 16); }
inline void put16(std::string& o, unsigned v) { o += static_cast<char>(v & 0xFF); o += static_cast<char>((v >> 8) & 0xFF); }
inline void put32(std::string& o, uint32_t v) { for (int k = 0; k < 4; ++k) o += static_cast<char>((v >> (8 * k)) & 0xFF); }
inline std::array<int, 3> c565(unsigned c) {   // bit replication, like sld.py and Pillow's BCn decoder
    const int r = static_cast<int>((c >> 11) & 31), g = static_cast<int>((c >> 5) & 63), b = static_cast<int>(c & 31);
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}
// BC1 colour block -> 16 RGBA. separate_alpha: always 4 colours (the colour half of DXT3/DXT5)
inline void bc1_decode(const unsigned char* b, uint8_t out[16][4], bool separate_alpha = false) {
    const unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
    const uint32_t bits = static_cast<uint32_t>(b[4]) | (static_cast<uint32_t>(b[5]) << 8) | (static_cast<uint32_t>(b[6]) << 16) | (static_cast<uint32_t>(b[7]) << 24);
    const auto A = c565(c0), B = c565(c1); const bool four = c0 > c1 || separate_alpha;
    int pal[4][4];
    for (int k = 0; k < 3; ++k) {
        pal[0][k] = A[static_cast<std::size_t>(k)]; pal[1][k] = B[static_cast<std::size_t>(k)];
        pal[2][k] = four ? (2 * A[static_cast<std::size_t>(k)] + B[static_cast<std::size_t>(k)]) / 3 : (A[static_cast<std::size_t>(k)] + B[static_cast<std::size_t>(k)]) / 2;
        pal[3][k] = four ? (A[static_cast<std::size_t>(k)] + 2 * B[static_cast<std::size_t>(k)]) / 3 : 0;
    }
    pal[0][3] = pal[1][3] = pal[2][3] = 255; pal[3][3] = four ? 255 : 0;
    for (int i = 0; i < 16; ++i) { const int s = static_cast<int>((bits >> (2 * i)) & 3); for (int k = 0; k < 4; ++k) out[i][k] = static_cast<uint8_t>(pal[s][k]); }
}
// BC4 (and the alpha half of DXT5) -> 16 values
inline void bc4_decode(const unsigned char* b, uint8_t out[16]) {
    const int a0 = b[0], a1 = b[1]; uint64_t bits = 0;
    for (int k = 0; k < 6; ++k) bits |= static_cast<uint64_t>(b[2 + k]) << (8 * k);
    int pal[8] = {a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1) for (int i = 1; i <= 6; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else { for (int i = 1; i <= 4; ++i) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5; pal[6] = 0; pal[7] = 255; }
    for (int i = 0; i < 16; ++i) out[i] = static_cast<uint8_t>(pal[(bits >> (3 * i)) & 7]);
}
}  // namespace packtools::modder::fmtutil

// =====================================================================================================================
namespace packtools::modder::sld {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::img;
using namespace packtools::modder::fmtutil;

inline const char* DOC =
    "Age of Empires II DE .sld sprites: read and write (a C++ port of universal-modder's examples/aoe2-de-civ/sld.py).\n\n"
    "    um sld info u_cav_knight_idleA_x1.sld          # header, canvas, hotspot, layers\n"
    "    um sld roundtrip u_cav_knight_idleA_x1.sld     # decode -> encode -> decode: what the encoder loses\n"
    "    um sld to-png u_cav_knight_idleA_x1.sld out/   # every frame as a PNG (shadow under it, player colour blue)\n"
    "    um sld write unit.sld frames/idle_*.png --hotspot 64,96 --shadows\n"
    "                                                   # PNG frames -> .sld; --shadows reads <frame>_s.png (um render3d)\n\n"
    "Reverse-engineered from the game's own files (checked by decoding stock sprites):\n"
    "  header   \"SLDX\" u16 version(4) u16 n_frames u16 0 u16 0x10 u32 0xff\n"
    "           (7490 of the 7743 stock files; most others have 0x0e and a u16 of 100-775 after the 0xff -\n"
    "           meaning unknown, and the game is happy with ours)\n"
    "  frame    u16 canvas_w, canvas_h  i16 hotspot_x, hotspot_y  u8 layer_mask  u8 0  u16 frame_index\n"
    "  layers   in bit order: 0x01 main (BC1), 0x02 shadow (BC4), 0x04 ?, 0x08 damage mask (BC1), 0x10 player colour (BC4)\n"
    "           each: u32 length (incl. itself, padded to 4), [u16 x1 y1 x2 y2 for main/shadow], u8 flags, u8 ?,\n"
    "                 u16 n_cmds, n_cmds x (u8 skip, u8 draw) over the layer's 4x4 blocks in row order, blocks (8 bytes each)\n"
    "           flags & 0x80: blocks not drawn this frame are copied from the previous frame's layer\n"
    "           layers without their own box use the main layer's box.\n";

enum : int { MAIN = 0x01, SHADOW = 0x02, UNK4 = 0x04, DAMAGE = 0x08, PLAYER = 0x10 };
constexpr int LAYER_ORDER[5] = {MAIN, SHADOW, UNK4, DAMAGE, PLAYER};
inline bool has_box(int l) { return l == MAIN || l == SHADOW; }
inline bool bc1_layer(int l) { return l == MAIN || l == DAMAGE; }
inline std::string layer_name(int l) { return l == MAIN ? "main" : l == SHADOW ? "shadow" : l == UNK4 ? "0x04" : l == DAMAGE ? "damage" : "player"; }

// ------------------------------------------------------------------ encoders (sld.py's bc1_encode / bc4_encode)
inline int to565(const float* c) {
    int v[3]; for (int k = 0; k < 3; ++k) { float x = std::min(255.0f, std::max(0.0f, c[k])); v[k] = static_cast<int>(x); }
    return ((v[0] >> 3) << 11) | ((v[1] >> 2) << 5) | (v[2] >> 3);
}
inline void bc1_encode(const uint8_t px[16][4], unsigned char out[8]) {
    float rgb[16][3]; bool opaque[16]; float cnt = 0, mean[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) { for (int k = 0; k < 3; ++k) rgb[i][k] = px[i][k]; opaque[i] = px[i][3] >= 128; if (opaque[i]) { cnt += 1; for (int k = 0; k < 3; ++k) mean[k] += rgb[i][k]; } }
    cnt = std::max(cnt, 1.0f); for (float& m : mean) m /= cnt;
    float cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (int i = 0; i < 16; ++i) if (opaque[i]) for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) cov[a][b] += (rgb[i][a] - mean[a]) * (rgb[i][b] - mean[b]);
    float axis[3] = {1, 1, 1};
    for (int it = 0; it < 8; ++it) {   // power iteration
        float n[3]; for (int a = 0; a < 3; ++a) n[a] = cov[a][0] * axis[0] + cov[a][1] * axis[1] + cov[a][2] * axis[2];
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-6f;
        for (int a = 0; a < 3; ++a) axis[a] = n[a] / len;
    }
    float lo = 0, hi = 0; bool any = false, has_transp = false;
    for (int i = 0; i < 16; ++i) {
        if (!opaque[i]) { has_transp = true; continue; }
        const float p = (rgb[i][0] - mean[0]) * axis[0] + (rgb[i][1] - mean[1]) * axis[1] + (rgb[i][2] - mean[2]) * axis[2];
        if (!any) { lo = hi = p; any = true; } else { lo = std::min(lo, p); hi = std::max(hi, p); }
    }
    float ehi[3], elo[3]; for (int k = 0; k < 3; ++k) { ehi[k] = mean[k] + axis[k] * hi; elo[k] = mean[k] + axis[k] * lo; }
    int e0 = to565(ehi), e1 = to565(elo);
    const bool swap = has_transp ? e0 > e1 : e0 < e1;   // 4-colour mode needs c0 > c1, transparent mode c0 <= c1
    int c0 = swap ? e1 : e0, c1 = swap ? e0 : e1;
    const bool same = c0 == c1 && !has_transp;
    if (same && c0 > 0) c1 = c0 - 1;   // keep 4-colour mode valid for flat blocks
    if (same && c0 == 0) c0 = 1;
    const auto A = c565(static_cast<unsigned>(c0)), B = c565(static_cast<unsigned>(c1)); const bool four = c0 > c1;
    float pal[4][3];
    for (int k = 0; k < 3; ++k) {
        const float a = static_cast<float>(A[static_cast<std::size_t>(k)]), b = static_cast<float>(B[static_cast<std::size_t>(k)]);
        pal[0][k] = a; pal[1][k] = b;
        pal[2][k] = four ? (2 * a + b) / 3 : (a + b) / 2;
        pal[3][k] = four ? (a + 2 * b) / 3 : 1e6f;   // index 3 unusable for colour
    }
    uint32_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        int sel = 3;
        if (opaque[i]) {
            float best = 0; sel = 0;
            for (int s = 0; s < 4; ++s) { float d = 0; for (int k = 0; k < 3; ++k) { const float t = rgb[i][k] - pal[s][k]; d += t * t; } if (s == 0 || d < best) { best = d; sel = s; } }
        }
        bits |= static_cast<uint32_t>(sel) << (2 * i);
    }
    out[0] = static_cast<unsigned char>(c0 & 255); out[1] = static_cast<unsigned char>(c0 >> 8);
    out[2] = static_cast<unsigned char>(c1 & 255); out[3] = static_cast<unsigned char>(c1 >> 8);
    for (int k = 0; k < 4; ++k) out[4 + k] = static_cast<unsigned char>((bits >> (8 * k)) & 255);
}
inline void bc4_encode(const uint8_t v[16], unsigned char out[8]) {   // 8-value mode between the block's min and max
    int a0 = 0, a1 = 255; for (int i = 0; i < 16; ++i) { a0 = std::max(a0, static_cast<int>(v[i])); a1 = std::min(a1, static_cast<int>(v[i])); }
    const bool flat = a0 == a1;
    if (flat && a0 < 255) a0 += 1;
    if (flat && a0 == a1) a1 -= 1;
    int pal[8] = {a0, a1, 0, 0, 0, 0, 0, 0}; for (int i = 1; i <= 6; ++i) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) { int sel = 0, best = 1 << 30; for (int s = 0; s < 8; ++s) { const int d = std::abs(v[i] - pal[s]); if (d < best) { best = d; sel = s; } } bits |= static_cast<uint64_t>(sel) << (3 * i); }
    out[0] = static_cast<unsigned char>(a0); out[1] = static_cast<unsigned char>(a1);
    for (int k = 0; k < 6; ++k) out[2 + k] = static_cast<unsigned char>((bits >> (8 * k)) & 255);
}

// ------------------------------------------------------------------ reading
struct Plane { long long w = 0, h = 0; std::vector<uint8_t> px; bool present() const { return w > 0; } };   // one channel
struct RawLayer { std::array<int, 4> box{}; int flags = 0, unk = 0; std::size_t ncmds = 0, nblocks = 0; };
struct Frame {
    int w = 0, h = 0, hx = 0, hy = 0;
    Image rgba, damage;   // empty (w == 0) when the layer is absent
    Plane shadow, player;
    std::map<int, RawLayer> raw;
};
struct Grid { int gw = 0, gh = 0, ch = 0; std::vector<uint8_t> v; };   // gh x gw blocks of 16 pixels x ch

inline std::vector<Frame> read_bytes(const std::string& data, const std::string& name) {
    if (data.size() < 16 || data.compare(0, 4, "SLDX") != 0) die(name + ": not an SLD file (no SLDX header)");
    const int n = u16(data, 6); std::size_t pos = 16;
    std::vector<Frame> frames; std::map<int, Grid> prev;
    auto need = [&](std::size_t at, std::size_t len) { if (at + len > data.size()) die(name + ": truncated SLD at byte " + std::to_string(at)); };
    for (int fi = 0; fi < n; ++fi) {
        need(pos, 12);
        Frame f; f.w = u16(data, pos); f.h = u16(data, pos + 2); f.hx = s16(data, pos + 4); f.hy = s16(data, pos + 6);
        const int mask = static_cast<unsigned char>(data[pos + 8]); pos += 12;
        std::optional<std::array<int, 4>> main_box;
        for (int layer : LAYER_ORDER) {
            if (!(mask & layer)) continue;
            need(pos, 4);
            const uint32_t length = u32(data, pos); const std::size_t start = pos; std::size_t p = pos + 4;
            std::array<int, 4> box{};
            if (has_box(layer)) { need(p, 8); box = {u16(data, p), u16(data, p + 2), u16(data, p + 4), u16(data, p + 6)}; p += 8; if (layer == MAIN) main_box = box; }
            else if (main_box) box = *main_box;
            else die(name + ": frame " + std::to_string(fi) + " has a " + layer_name(layer) + " layer but no main layer box");
            need(p, 4);
            RawLayer rl; rl.box = box; rl.flags = static_cast<unsigned char>(data[p]); rl.unk = static_cast<unsigned char>(data[p + 1]); rl.ncmds = u16(data, p + 2); p += 4;
            need(p, 2 * rl.ncmds);
            const std::size_t cmds_at = p; p += 2 * rl.ncmds;
            for (std::size_t c = 0; c < rl.ncmds; ++c) rl.nblocks += static_cast<unsigned char>(data[cmds_at + 2 * c + 1]);
            need(p, 8 * rl.nblocks);
            const std::size_t blocks_at = p;
            f.raw[layer] = rl;
            pos = start + ((static_cast<std::size_t>(length) + 3) & ~static_cast<std::size_t>(3));
            if (length < 4) die(name + ": bad layer length in frame " + std::to_string(fi));
            // decode into a block grid
            const int gw = std::max(0, (box[2] - box[0]) / 4), gh = std::max(0, (box[3] - box[1]) / 4);
            const bool bc1 = bc1_layer(layer); const int ch = bc1 ? 4 : 1;
            Grid grid;
            if ((rl.flags & 0x80) && prev.count(layer) && prev[layer].gw == gw && prev[layer].gh == gh) grid = prev[layer];
            else { grid.gw = gw; grid.gh = gh; grid.ch = ch; grid.v.assign(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh) * 16 * static_cast<std::size_t>(ch), 0); }
            std::size_t k = 0, b = 0; const std::size_t cells = static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
            for (std::size_t c = 0; c < rl.ncmds; ++c) {
                k += static_cast<unsigned char>(data[cmds_at + 2 * c]);
                for (int d = 0; d < static_cast<unsigned char>(data[cmds_at + 2 * c + 1]); ++d) {
                    if (k >= cells) die(name + ": frame " + std::to_string(fi) + " " + layer_name(layer) + " layer draws outside its box");
                    const auto* blk = reinterpret_cast<const unsigned char*>(data.data() + blocks_at + 8 * b);
                    uint8_t* cell = &grid.v[k * 16 * static_cast<std::size_t>(ch)];
                    if (bc1) { uint8_t t[16][4]; bc1_decode(blk, t); std::memcpy(cell, t, 64); }
                    else { uint8_t t[16]; bc4_decode(blk, t); std::memcpy(cell, t, 16); }
                    ++k; ++b;
                }
            }
            prev[layer] = grid;
            // blocks -> canvas
            const int x1 = box[0], y1 = box[1];
            if (bc1) {
                Image canvas(f.w, f.h);
                for (int by = 0; by < gh; ++by) for (int bx = 0; bx < gw; ++bx) for (int i = 0; i < 16; ++i) {
                    const int x = x1 + bx * 4 + (i % 4), y = y1 + by * 4 + (i / 4);
                    if (x < f.w && y < f.h) std::memcpy(canvas.at(x, y), &grid.v[((static_cast<std::size_t>(by) * static_cast<std::size_t>(gw) + static_cast<std::size_t>(bx)) * 16 + static_cast<std::size_t>(i)) * 4], 4);
                }
                (layer == MAIN ? f.rgba : f.damage) = std::move(canvas);
            } else if (layer != UNK4) {
                Plane canvas; canvas.w = f.w; canvas.h = f.h; canvas.px.assign(static_cast<std::size_t>(f.w) * static_cast<std::size_t>(f.h), 0);
                for (int by = 0; by < gh; ++by) for (int bx = 0; bx < gw; ++bx) for (int i = 0; i < 16; ++i) {
                    const int x = x1 + bx * 4 + (i % 4), y = y1 + by * 4 + (i / 4);
                    if (x < f.w && y < f.h) canvas.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(f.w) + static_cast<std::size_t>(x)] = grid.v[(static_cast<std::size_t>(by) * static_cast<std::size_t>(gw) + static_cast<std::size_t>(bx)) * 16 + static_cast<std::size_t>(i)];
                }
                (layer == SHADOW ? f.shadow : f.player) = std::move(canvas);
            }
        }
        frames.push_back(std::move(f));
    }
    return frames;
}
inline std::string must_read(const std::string& path) {
    if (!isfile(path)) die("no such file: " + path);
    return slurp(path);
}

// ------------------------------------------------------------------ writing
struct Src { Image rgba; int hx = 0, hy = 0; Plane player, shadow; };
inline std::string layer_bytes(const std::array<int, 4>& box, const std::vector<char>& drawn, const std::string& encoded, bool with_box, int flags = 0, int unk = 1) {
    std::vector<std::pair<int, int>> cmds; std::size_t k = 0; const std::size_t n = drawn.size();
    while (k < n) {
        int skip = 0; while (k < n && !drawn[k] && skip < 255) { ++skip; ++k; }
        int draw = 0; while (k < n && drawn[k] && draw < 255) { ++draw; ++k; }
        cmds.emplace_back(skip, draw);
    }
    std::string body;
    if (with_box) for (int v : box) put16(body, static_cast<unsigned>(v));
    body += static_cast<char>(flags); body += static_cast<char>(unk); put16(body, static_cast<unsigned>(cmds.size()));
    for (const auto& [s, d] : cmds) { body += static_cast<char>(s); body += static_cast<char>(d); }
    body += encoded;
    const std::size_t length = 4 + body.size(), pad = (4 - length % 4) % 4;
    std::string o; put32(o, static_cast<uint32_t>(length)); o += body; o.append(pad, '\0');
    return o;
}
template <class F> std::optional<std::array<int, 4>> mask_box(int w, int h, F on) {
    int x0 = w, y0 = h, xm = -1, ym = -1;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (on(x, y)) { x0 = std::min(x0, x); y0 = std::min(y0, y); xm = std::max(xm, x); ym = std::max(ym, y); }
    if (xm < 0) return std::nullopt;
    return std::array<int, 4>{(x0 / 4) * 4, (y0 / 4) * 4, ((xm + 4) / 4) * 4, ((ym + 4) / 4) * 4};
}
inline std::string write_bytes(const std::vector<Src>& frames) {
    std::string out = "SLDX"; put16(out, 4); put16(out, static_cast<unsigned>(frames.size())); put16(out, 0); put16(out, 0x10); put32(out, 0xFF);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const Src& fr = frames[i]; const int w = static_cast<int>(fr.rgba.w), h = static_cast<int>(fr.rgba.h);
        if (w % 4 || h % 4) die("frame " + std::to_string(i) + ": canvas " + std::to_string(w) + "x" + std::to_string(h) + " is not a multiple of 4 - pad it");
        const auto box = mask_box(w, h, [&](int x, int y) { return fr.rgba.at(x, y)[3] >= 128; }).value_or(std::array<int, 4>{0, 0, 4, 4});
        int mask = MAIN | UNK4;   // the stock files always carry the (empty) 0x04 layer
        std::optional<std::array<int, 4>> sbox;
        if (fr.shadow.present()) sbox = mask_box(w, h, [&](int x, int y) { return fr.shadow.px[static_cast<std::size_t>(y * w + x)] > 8; });
        if (sbox) mask |= SHADOW;
        bool player = false; if (fr.player.present()) for (uint8_t v : fr.player.px) if (v > 8) { player = true; break; }
        if (player) mask |= PLAYER;
        put16(out, static_cast<unsigned>(w)); put16(out, static_cast<unsigned>(h)); put16(out, static_cast<unsigned>(static_cast<uint16_t>(fr.hx))); put16(out, static_cast<unsigned>(static_cast<uint16_t>(fr.hy)));
        out += static_cast<char>(mask); out += '\0'; put16(out, static_cast<unsigned>(i));
        auto tiles = [&](const std::array<int, 4>& b, auto&& cell) { for (int by = b[1]; by < b[3]; by += 4) for (int bx = b[0]; bx < b[2]; bx += 4) cell(bx, by); };
        // main
        std::vector<char> drawn; std::string enc;
        tiles(box, [&](int bx, int by) {
            uint8_t t[16][4]; bool any = false;
            for (int j = 0; j < 16; ++j) { std::memcpy(t[j], fr.rgba.at(bx + j % 4, by + j / 4), 4); any = any || t[j][3] >= 128; }
            drawn.push_back(any ? 1 : 0);
            if (any) { unsigned char o[8]; bc1_encode(t, o); enc.append(reinterpret_cast<char*>(o), 8); }
        });
        out += layer_bytes(box, drawn, enc, true);
        if (sbox) {
            std::vector<char> sd; std::string se;
            tiles(*sbox, [&](int bx, int by) {
                uint8_t t[16]; bool any = false;
                for (int j = 0; j < 16; ++j) { t[j] = fr.shadow.px[static_cast<std::size_t>((by + j / 4) * w + bx + j % 4)]; any = any || t[j] > 8; }
                sd.push_back(any ? 1 : 0);
                if (any) { unsigned char o[8]; bc4_encode(t, o); se.append(reinterpret_cast<char*>(o), 8); }
            });
            out += layer_bytes(*sbox, sd, se, true, 1);
        }
        put32(out, 8); out += static_cast<char>(5); out += '\0'; put16(out, 0);   // layer 0x04: flags 5, no blocks
        if (player) {
            std::vector<char> pd; std::string pe; std::size_t ti = 0;
            tiles(box, [&](int bx, int by) {
                uint8_t t[16]; bool any = false;
                for (int j = 0; j < 16; ++j) { t[j] = fr.player.px[static_cast<std::size_t>((by + j / 4) * w + bx + j % 4)]; any = any || t[j] > 8; }
                any = any && drawn[ti++];
                pd.push_back(any ? 1 : 0);
                if (any) { unsigned char o[8]; bc4_encode(t, o); pe.append(reinterpret_cast<char*>(o), 8); }
            });
            out += layer_bytes(box, pd, pe, false, 1);
        }
    }
    return out;
}

// ------------------------------------------------------------------ commands
inline void info(const std::string& path) {
    const std::string data = must_read(path);
    const auto frames = read_bytes(data, path);
    char hex[9]; std::snprintf(hex, sizeof hex, "%02x%02x%02x%02x", static_cast<unsigned char>(data[12]), static_cast<unsigned char>(data[13]), static_cast<unsigned char>(data[14]), static_cast<unsigned char>(data[15]));
    char f4[16]; std::snprintf(f4, sizeof f4, "0x%02x", u16(data, 10));
    const int n = u16(data, 6);
    print(path + ": " + std::to_string(data.size()) + " bytes, version " + std::to_string(u16(data, 4)) + ", " + std::to_string(n) + " frames, header " + f4 + " " + hex);
    std::vector<std::pair<std::array<int, 4>, int>> canv;   // Counter(...).most_common(4)
    for (const auto& f : frames) {
        const std::array<int, 4> key{f.w, f.h, f.hx, f.hy}; bool found = false;
        for (auto& [k2, c] : canv) if (k2 == key) { ++c; found = true; break; }
        if (!found) canv.emplace_back(key, 1);
    }
    std::stable_sort(canv.begin(), canv.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
    for (std::size_t i = 0; i < canv.size() && i < 4; ++i)
        print("  canvas " + std::to_string(canv[i].first[0]) + "x" + std::to_string(canv[i].first[1]) + ", hotspot (" + std::to_string(canv[i].first[2]) + ", " + std::to_string(canv[i].first[3]) + "): " + std::to_string(canv[i].second) + " frames");
    if (n > 1 && (n - 1) % 16 == 0) print("  = 16 angles x " + std::to_string((n - 1) / 16) + " frames + 1 trailing frame, like the stock unit files");
    std::map<int, long long> count, blocks, delta;
    for (const auto& f : frames) for (const auto& [layer, rl] : f.raw) { ++count[layer]; blocks[layer] += static_cast<long long>(rl.nblocks); delta[layer] += (rl.flags & 0x80) ? 1 : 0; }
    for (int layer : LAYER_ORDER) if (count[layer]) {
        std::string nm = layer_name(layer); nm.resize(std::max<std::size_t>(nm.size(), 6), ' ');
        print("  layer " + nm + " in " + std::to_string(count[layer]) + " frames, " + std::to_string(blocks[layer]) + " blocks, " + std::to_string(delta[layer]) + " frames reuse blocks of the previous one (flag 0x80)");
    }
}
inline Plane pad_plane(const Plane& p, int w4, int h4) {
    Plane o; if (!p.present()) return o;
    o.w = w4; o.h = h4; o.px.assign(static_cast<std::size_t>(w4) * static_cast<std::size_t>(h4), 0);
    for (long long y = 0; y < p.h; ++y) std::memcpy(&o.px[static_cast<std::size_t>(y * w4)], &p.px[static_cast<std::size_t>(y * p.w)], static_cast<std::size_t>(p.w));
    return o;
}
inline Image pad_image(const Image& im, int w4, int h4) { Image o(w4, h4); if (im.w) paste(o, im, 0, 0); return o; }
inline double roundtrip(const std::string& path) {
    const std::string data = must_read(path);
    const auto frames = read_bytes(data, path);
    std::vector<Src> src;
    for (const auto& f : frames) {
        const int h4 = (f.h + 3) / 4 * 4, w4 = (f.w + 3) / 4 * 4;   // write() works in whole 4x4 blocks
        Src s; s.rgba = pad_image(f.rgba, w4, h4); s.hx = f.hx; s.hy = f.hy; s.player = pad_plane(f.player, w4, h4); s.shadow = pad_plane(f.shadow, w4, h4);
        src.push_back(std::move(s));
    }
    const std::string enc = write_bytes(src);
    const auto again = read_bytes(enc, path);
    long long rgb_sum = 0, rgb_n = 0, alpha_bad = 0, sh_sum = 0, sh_n = 0, pl_sum = 0, pl_n = 0;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const Src& a = src[i]; const Frame& b = again[i]; const std::size_t npx = static_cast<std::size_t>(a.rgba.w * a.rgba.h);
        for (std::size_t p = 0; p < npx; ++p) {
            const bool op = a.rgba.px[p * 4 + 3] >= 128, op2 = b.rgba.px[p * 4 + 3] >= 128;
            if (op) { for (int k = 0; k < 3; ++k) rgb_sum += std::abs(static_cast<int>(a.rgba.px[p * 4 + static_cast<std::size_t>(k)]) - static_cast<int>(b.rgba.px[p * 4 + static_cast<std::size_t>(k)])); rgb_n += 3; }
            if (op != op2) ++alpha_bad;
            if (a.shadow.present()) {
                const int s0 = a.shadow.px[p], s1 = b.shadow.present() ? b.shadow.px[p] : 0;
                if (s0 > 0 || s1 > 0) { sh_sum += std::abs(s0 - s1); ++sh_n; }
            }
            if (a.player.present() && op) { const int s0 = a.player.px[p], s1 = b.player.present() ? b.player.px[p] : 0; pl_sum += std::abs(s0 - s1); ++pl_n; }
        }
    }
    const double mae = static_cast<double>(rgb_sum) / static_cast<double>(std::max(rgb_n, 1LL));
    print("roundtrip " + path + ": " + std::to_string(frames.size()) + " frames, " + std::to_string(data.size()) + " -> " + std::to_string(enc.size()) + " bytes (no inter-frame reuse)");
    print("  main   RGB mean abs error " + fmt_f(mae, 2) + "/255 over " + std::to_string(rgb_n / 3) + " opaque pixels, " + std::to_string(alpha_bad) + " pixels changed opacity");
    if (sh_n) print("  shadow mean abs error " + fmt_f(static_cast<double>(sh_sum) / static_cast<double>(sh_n), 2) + "/255");
    if (pl_n) print("  player mean abs error " + fmt_f(static_cast<double>(pl_sum) / static_cast<double>(pl_n), 2) + "/255");
    for (const auto& f : frames) if (f.damage.w) { print("  (the damage layer isn't written)"); break; }
    return mae;
}
inline Image preview(const Frame& f) {   // roughly as the game draws it for a blue player
    const int pr[3] = {45, 90, 230};
    Image im(f.w, f.h);
    if (f.shadow.present()) { Image s(f.w, f.h); for (std::size_t p = 0; p < f.shadow.px.size(); ++p) s.px[p * 4 + 3] = f.shadow.px[p]; alpha_composite(im, s); }
    if (f.rgba.w) {
        Image rgba = f.rgba;
        if (f.player.present()) for (std::size_t p = 0; p < f.player.px.size(); ++p) {
            const double k = f.player.px[p] / 255.0; uint8_t* c = &rgba.px[p * 4];
            const double m = (static_cast<double>(c[0]) + c[1] + c[2]) / 3.0 / 128.0;
            for (int q = 0; q < 3; ++q) { const double v = c[q] * (1 - k) + pr[q] * m * k; c[q] = static_cast<uint8_t>(std::min(255.0, std::max(0.0, v))); }
        }
        alpha_composite(im, rgba);
    }
    return im;
}
inline void to_png(const std::string& path, const std::string& outdir, bool layers) {
    const auto frames = read_bytes(must_read(path), path);
    makedirs(outdir);
    const std::string stem = u8native(P(path).stem());
    char num[32];
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const Frame& f = frames[i]; std::snprintf(num, sizeof num, "_%04zu", i);
        const std::string base = u8native(P(outdir) / (stem + num));
        save(preview(f), base + ".png");
        if (layers) {
            if (f.rgba.w) save(f.rgba, base + "_main.png");
            if (f.shadow.present()) save_gray(Gray{f.shadow.w, f.shadow.h, f.shadow.px}, base + "_shadow.png");
            if (f.player.present()) save_gray(Gray{f.player.w, f.player.h, f.player.px}, base + "_player.png");
            if (f.damage.w) save(f.damage, base + "_damage.png");
        }
    }
    print("wrote " + std::to_string(frames.size()) + " frames to " + path_str(outdir));
}
inline Plane alpha_plane(const Image& im) { Plane p; p.w = im.w; p.h = im.h; p.px.resize(static_cast<std::size_t>(im.w * im.h)); for (std::size_t i = 0; i < p.px.size(); ++i) p.px[i] = im.px[i * 4 + 3]; return p; }
inline void write_cmd(const std::string& out, const std::vector<std::string>& pngs, const std::string& hotspot, bool shadows, const std::string& player_suffix, bool pad) {
    const auto parts = split(hotspot, ",");
    int hx = 0, hy = 0;
    try { if (parts.size() != 2) throw 0; std::size_t u1 = 0, u2 = 0; hx = std::stoi(strip(parts[0]), &u1); hy = std::stoi(strip(parts[1]), &u2); if (u1 != strip(parts[0]).size() || u2 != strip(parts[1]).size()) throw 0; }
    catch (...) { die("--hotspot wants X,Y (pixels from the canvas's top left), got " + hotspot); return; }
    std::vector<Src> frames; int with_shadow = 0, with_player = 0;
    auto side = [](const std::string& png, const std::string& suffix) { const fs::path p = P(png); return u8native(p.parent_path() / (u8native(p.stem()) + suffix + u8native(p.extension()))); };
    for (const auto& png : pngs) {
        if (!isfile(png)) die("no such file: " + png);
        Src s; s.rgba = load(png); s.hx = hx; s.hy = hy;
        if (shadows) { const std::string sp = side(png, "_s"); if (isfile(sp)) { s.shadow = alpha_plane(load(sp)); ++with_shadow; if (s.shadow.w != s.rgba.w || s.shadow.h != s.rgba.h) die(sp + ": not the same size as " + png); } }
        if (!player_suffix.empty()) { const std::string pp = side(png, player_suffix); if (isfile(pp)) { s.player = alpha_plane(load(pp)); ++with_player; if (s.player.w != s.rgba.w || s.player.h != s.rgba.h) die(pp + ": not the same size as " + png); } }
        if (pad && (s.rgba.w % 4 || s.rgba.h % 4)) {
            const int w4 = static_cast<int>((s.rgba.w + 3) / 4 * 4), h4 = static_cast<int>((s.rgba.h + 3) / 4 * 4);
            s.rgba = pad_image(s.rgba, w4, h4); s.shadow = pad_plane(s.shadow, w4, h4); s.player = pad_plane(s.player, w4, h4);
        }
        frames.push_back(std::move(s));
    }
    const std::string enc = write_bytes(frames);
    spit(out, enc);
    print("wrote " + out + ": " + std::to_string(frames.size()) + " frames, " + std::to_string(enc.size()) + " bytes, hotspot (" + std::to_string(hx) + ", " + std::to_string(hy) + ")" +
          (shadows ? ", " + std::to_string(with_shadow) + " with a shadow" : std::string()) + (player_suffix.empty() ? std::string() : ", " + std::to_string(with_player) + " with a player-colour mask"));
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("sld", DOC); p.help_line = "Age of Empires II DE .sld sprites: info, roundtrip, to-png, write"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("info"); q.help_line = "header, canvas, hotspot and layers"; q.pos("files").nargs("+"); }
    { Parser& q = p.sub("roundtrip"); q.help_line = "decode -> encode -> decode, print the mean abs error"; q.pos("files").nargs("+"); }
    { Parser& q = p.sub("to-png"); q.help_line = "every frame as a PNG"; q.pos("file"); q.pos("outdir");
      q.opt({"--layers"}).store_true().help("also each layer on its own (main, shadow, player, damage)"); }
    { Parser& q = p.sub("write"); q.help_line = "PNG frames (in the order given) -> one .sld"; q.pos("output"); q.pos("frames").nargs("+");
      q.opt({"--hotspot"}).required().help("X,Y: the unit's ground point on the canvas, the same for every frame");
      q.opt({"--shadows"}).store_true().help("add a shadow layer from <frame>_s.png's alpha when it exists (um render3d --shadows)");
      q.opt({"--player-suffix"}).def("").help("add a player-colour layer from <frame><suffix>.png's alpha when it exists (e.g. _p)");
      q.opt({"--pad"}).store_true().help("pad canvases to a multiple of 4 on the right and bottom instead of stopping"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "info" || c == "roundtrip") { for (const auto& v : *get(a, "files").l) { if (c == "info") info(v.s); else roundtrip(v.s); } }
    else if (c == "to-png") to_png(S(a, "file"), S(a, "outdir"), truthy(get(a, "layers")));
    else if (c == "write") { std::vector<std::string> fr; for (const auto& v : *get(a, "frames").l) fr.push_back(v.s); write_cmd(S(a, "output"), fr, S(a, "hotspot"), truthy(get(a, "shadows")), S(a, "player_suffix"), truthy(get(a, "pad"))); }
    return 0;
}
}  // namespace packtools::modder::sld

// =====================================================================================================================
namespace packtools::modder::vpk {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::fmtutil;

inline const char* DOC =
    "Valve VPK v1/v2 archives (`*_dir.vpk` + `*_NNN.vpk`): list and extract. A C++ port of universal-modder's\n"
    "examples/outer-wilds-portal-gun/tools/vpk.py.\n\n"
    "    um vpk list pak01_dir.vpk [--find portalgun]\n"
    "    um vpk extract pak01_dir.vpk models/weapons/v_portalgun.mdl ... --out extracted/\n\n"
    "Paths are matched case-insensitively, like the game. Extract from your own install only, and keep what you\n"
    "extract out of anything you share.\n";

struct Entry { unsigned arch = 0; uint32_t off = 0, length = 0; std::string pre; };
struct Archive {
    std::string dir_path, base, data; std::size_t data_offset = 0;
    std::vector<std::pair<std::string, Entry>> entries;   // file order
    std::map<std::string, std::size_t> index;
};
inline Archive open_vpk(const std::string& dir_path) {
    Archive v; v.dir_path = dir_path;
    if (!dir_path.ends_with("_dir.vpk")) die(dir_path + ": pass the *_dir.vpk file");
    if (!isfile(dir_path)) die("no such file: " + dir_path);
    v.base = dir_path.substr(0, dir_path.size() - 8); v.data = slurp(dir_path);
    const std::string& d = v.data;
    if (d.size() < 12 || u32(d, 0) != 0x55AA1234u) die(dir_path + ": not a VPK");
    const uint32_t version = u32(d, 4), tree = u32(d, 8);
    const std::size_t header = version == 1 ? 12 : 28; v.data_offset = header + tree;
    std::size_t p = header;
    auto cstr = [&]() { const auto e = d.find('\0', p); if (e == std::string::npos) die(dir_path + ": truncated directory tree"); std::string s = d.substr(p, e - p); p = e + 1; return s; };
    while (true) {
        const std::string ext = cstr(); if (ext.empty()) break;
        while (true) {
            const std::string path = cstr(); if (path.empty()) break;
            while (true) {
                const std::string name = cstr(); if (name.empty()) break;
                if (p + 18 > d.size()) die(dir_path + ": truncated directory entry");
                Entry e; const unsigned preload = u16(d, p + 4); e.arch = u16(d, p + 6); e.off = u32(d, p + 8); e.length = u32(d, p + 12); p += 18;
                if (p + preload > d.size()) die(dir_path + ": truncated preload data");
                e.pre = d.substr(p, preload); p += preload;
                const std::string full = ext != " " ? (path != " " ? path + "/" : std::string()) + name + "." + ext : name;
                const std::string key = lower(full);
                if (!v.index.count(key)) { v.index[key] = v.entries.size(); v.entries.emplace_back(key, e); }
                else v.entries[v.index[key]].second = e;
            }
        }
    }
    return v;
}
inline std::string read_entry(const Archive& v, const std::string& path) {
    const auto it = v.index.find(lower(replace_all(path, "\\", "/")));
    if (it == v.index.end()) die(v.dir_path + ": no " + path + " in this archive (see `um vpk list`)");
    const Entry& e = v.entries[it->second].second;
    if (e.length == 0) return e.pre;
    std::string body;
    if (e.arch == 0x7FFF) {
        if (v.data_offset + e.off + e.length > v.data.size()) die(v.dir_path + ": " + path + " runs past the end of the file");
        body = v.data.substr(v.data_offset + e.off, e.length);
    } else {
        char num[16]; std::snprintf(num, sizeof num, "_%03u", e.arch);
        const std::string part = v.base + num + ".vpk";
        std::ifstream in(P(part), std::ios::binary);
        if (!in) die("missing archive part " + part + " (needed for " + path + ")");
        in.seekg(e.off); body.resize(e.length); in.read(body.data(), e.length);
        if (static_cast<uint32_t>(in.gcount()) != e.length) die(part + ": " + path + " runs past the end of the file");
    }
    return e.pre + body;
}
inline bool safe_rel(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find(':') != std::string::npos) return false;
    for (const auto& part : split(p, "/")) if (part == ".." ) return false;
    return true;
}
inline void reg(Parser& sub) {
    Parser& p = sub.sub("vpk", DOC); p.help_line = "Valve VPK archives: list and extract"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("list"); q.help_line = "every file (lower-case path) and its size"; q.pos("vpk"); q.opt({"--find"}).def("").help("only paths containing this text"); }
    { Parser& q = p.sub("extract"); q.help_line = "copy files out of the archive"; q.pos("vpk"); q.pos("paths").nargs("*");
      q.opt({"--out"}).def(".").help("folder to write into (keeps the archive's folders)");
      q.opt({"--find"}).def("").help("also every path containing this text");
      q.opt({"--all"}).store_true().help("every file"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd"); const Archive v = open_vpk(S(a, "vpk")); const std::string needle = lower(S(a, "find"));
    if (c == "list") {
        std::vector<std::string> keys; for (const auto& [k, e] : v.entries) if (needle.empty() || k.find(needle) != std::string::npos) keys.push_back(k);
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys) { const Entry& e = v.entries[v.index.at(k)].second; print(k + "\t" + std::to_string(e.pre.size() + e.length)); }
        print(std::to_string(keys.size()) + " files" + (needle.empty() ? std::string() : " matching " + S(a, "find")));
    } else {
        std::vector<std::string> want; for (const auto& x : *get(a, "paths").l) want.push_back(lower(replace_all(x.s, "\\", "/")));
        if (truthy(get(a, "all")) || !needle.empty()) for (const auto& [k, e] : v.entries) if (truthy(get(a, "all")) || k.find(needle) != std::string::npos) want.push_back(k);
        if (want.empty()) die("nothing to extract: give paths, --find TEXT or --all");
        std::sort(want.begin(), want.end()); want.erase(std::unique(want.begin(), want.end()), want.end());
        long long bytes = 0;
        for (const auto& k : want) {
            if (!safe_rel(k)) die("refusing to write outside --out: " + k);
            const std::string body = read_entry(v, k), dst = u8native(P(S(a, "out")) / P(k));
            makedirs(u8native(P(dst).parent_path())); spit(dst, body); bytes += static_cast<long long>(body.size());
        }
        print("extracted " + std::to_string(want.size()) + " files, " + std::to_string(bytes) + " bytes -> " + path_str(S(a, "out")));
    }
    return 0;
}
}  // namespace packtools::modder::vpk

// =====================================================================================================================
namespace packtools::modder::vtf {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::img;
using namespace packtools::modder::fmtutil;

inline const char* DOC =
    "Valve Texture Format (VTF 7.0-7.5): decode the top mip to a PNG. A C++ port of universal-modder's\n"
    "examples/outer-wilds-portal-gun/tools/vtf.py.\n\n"
    "    um vtf info texture.vtf\n"
    "    um vtf to-png texture.vtf texture.png [--frame N]\n\n"
    "Formats: DXT1, DXT3, DXT5, RGBA8888, ABGR8888, ARGB8888, BGRA8888, BGRX8888, RGB888, BGR888, I8, IA88.\n";

enum : int { DXT1 = 13, DXT3 = 14, DXT5 = 15 };
inline int pixel_bytes(int fmt) {
    switch (fmt) { case 0: case 1: case 11: case 12: case 16: return 4; case 2: case 3: return 3; case 5: return 1; case 6: return 2; default: return 0; }
}
inline const char* fmt_name(int f) {
    switch (f) {
        case 0: return "RGBA8888"; case 1: return "ABGR8888"; case 2: return "RGB888"; case 3: return "BGR888"; case 5: return "I8"; case 6: return "IA88";
        case 11: return "ARGB8888"; case 12: return "BGRA8888"; case 13: return "DXT1"; case 14: return "DXT3"; case 15: return "DXT5"; case 16: return "BGRX8888"; default: return "unsupported";
    }
}
inline std::size_t size_of(int fmt, long long w, long long h) {
    const auto bw = static_cast<std::size_t>(std::max(1LL, (w + 3) / 4)), bh = static_cast<std::size_t>(std::max(1LL, (h + 3) / 4));
    if (fmt == DXT1) return bw * bh * 8;
    if (fmt == DXT3 || fmt == DXT5) return bw * bh * 16;
    if (pixel_bytes(fmt)) return static_cast<std::size_t>(w * h) * static_cast<std::size_t>(pixel_bytes(fmt));
    die("unsupported VTF format " + std::to_string(fmt)); return 0;
}
struct Header { unsigned major = 0, minor = 0, header_size = 0; int w = 0, h = 0, frames = 1, mips = 1, hi_fmt = 0, lo_fmt = -1, lo_w = 0, lo_h = 0, faces = 1, depth = 1; uint32_t flags = 0; };
inline Header header(const std::string& d) {
    if (d.size() < 64 || d.compare(0, 4, std::string("VTF\0", 4)) != 0) die("not a VTF");
    Header hd; hd.major = u32(d, 4); hd.minor = u32(d, 8); hd.header_size = u32(d, 12);
    hd.w = u16(d, 16); hd.h = u16(d, 18); hd.flags = u32(d, 20); hd.frames = std::max(1, static_cast<int>(u16(d, 24)));
    hd.hi_fmt = static_cast<int32_t>(u32(d, 52)); hd.mips = static_cast<unsigned char>(d[56]); hd.lo_fmt = static_cast<int32_t>(u32(d, 57));
    hd.lo_w = static_cast<unsigned char>(d[61]); hd.lo_h = static_cast<unsigned char>(d[62]);
    hd.faces = (hd.flags & 0x4000) ? 6 : 1;   // envmap
    hd.depth = hd.minor >= 2 && d.size() >= 65 ? std::max(1, static_cast<int>(u16(d, 63))) : 1;
    return hd;
}
inline Image decode(const std::string& d, int frame) {
    const Header hd = header(d); std::size_t off = 0;
    if (hd.minor >= 3) {
        if (d.size() < 72) die("truncated VTF header");
        const uint32_t nres = u32(d, 68); bool found = false;
        for (uint32_t i = 0; i < nres; ++i) {
            const std::size_t at = 80 + 8 * static_cast<std::size_t>(i);
            if (at + 8 > d.size()) die("truncated VTF resource table");
            if (d.compare(at, 3, std::string("\x30\x00\x00", 3)) == 0) { off = u32(d, at + 4); found = true; }
        }
        if (!found) die("VTF without image resource");
    } else off = hd.header_size + (hd.lo_fmt >= 0 && hd.lo_w ? size_of(hd.lo_fmt, hd.lo_w, hd.lo_h) : 0);
    const auto per = static_cast<std::size_t>(hd.frames) * static_cast<std::size_t>(hd.faces) * static_cast<std::size_t>(hd.depth);
    for (int m = hd.mips - 1; m > 0; --m) off += size_of(hd.hi_fmt, std::max(1, hd.w >> m), std::max(1, hd.h >> m)) * per;   // smallest first
    const std::size_t one = size_of(hd.hi_fmt, hd.w, hd.h);
    off += one * static_cast<std::size_t>(hd.faces) * static_cast<std::size_t>(hd.depth) * static_cast<std::size_t>(std::min(frame, hd.frames - 1));
    if (off + one > d.size()) die("truncated VTF: the top mip needs bytes " + std::to_string(off) + "-" + std::to_string(off + one) + " of " + std::to_string(d.size()));
    const auto* raw = reinterpret_cast<const unsigned char*>(d.data() + off);
    Image im(hd.w, hd.h);
    if (hd.hi_fmt == DXT1 || hd.hi_fmt == DXT3 || hd.hi_fmt == DXT5) {
        const int bw = (hd.w + 3) / 4, bh = (hd.h + 3) / 4; const std::size_t bs = hd.hi_fmt == DXT1 ? 8 : 16;
        for (int by = 0; by < bh; ++by) for (int bx = 0; bx < bw; ++bx) {
            const unsigned char* blk = raw + (static_cast<std::size_t>(by) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(bx)) * bs;
            uint8_t px[16][4];
            if (hd.hi_fmt == DXT1) bc1_decode(blk, px);
            else {
                bc1_decode(blk + 8, px, true);
                if (hd.hi_fmt == DXT3) for (int i = 0; i < 16; ++i) { const int nib = (blk[i / 2] >> (4 * (i % 2))) & 15; px[i][3] = static_cast<uint8_t>(nib * 17); }
                else { uint8_t al[16]; bc4_decode(blk, al); for (int i = 0; i < 16; ++i) px[i][3] = al[i]; }
            }
            for (int i = 0; i < 16; ++i) { const int x = bx * 4 + i % 4, y = by * 4 + i / 4; if (x < hd.w && y < hd.h) std::memcpy(im.at(x, y), px[i], 4); }
        }
        return im;
    }
    const int bpp = pixel_bytes(hd.hi_fmt);
    for (long long i = 0; i < static_cast<long long>(hd.w) * hd.h; ++i) {
        const unsigned char* s = raw + i * bpp; uint8_t* o = &im.px[static_cast<std::size_t>(i) * 4];
        switch (hd.hi_fmt) {
            case 0: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3]; break;          // RGBA
            case 1: o[0] = s[3]; o[1] = s[2]; o[2] = s[1]; o[3] = s[0]; break;          // ABGR
            case 11: o[0] = s[1]; o[1] = s[2]; o[2] = s[3]; o[3] = s[0]; break;         // ARGB
            case 12: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; break;         // BGRA
            case 16: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; break;          // BGRX
            case 2: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;           // RGB
            case 3: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; break;           // BGR
            case 5: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;                       // I8
            case 6: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;                      // IA88
            default: break;
        }
    }
    return im;
}
inline void reg(Parser& sub) {
    Parser& p = sub.sub("vtf", DOC); p.help_line = "Valve VTF textures: info, top mip to PNG"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("info"); q.help_line = "version, size, format, mips, frames"; q.pos("file"); }
    { Parser& q = p.sub("to-png"); q.help_line = "the top mip as an RGBA PNG"; q.pos("file"); q.pos("output"); q.opt({"--frame"}).integer().def(0).help("animation frame (default 0)"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd"), file = S(a, "file");
    if (!isfile(file)) die("no such file: " + file);
    const std::string d = slurp(file);
    if (c == "info") {
        const Header hd = header(d);
        print(file + ": VTF " + std::to_string(hd.major) + "." + std::to_string(hd.minor) + ", " + std::to_string(hd.w) + "x" + std::to_string(hd.h) + ", " + fmt_name(hd.hi_fmt) + " (" + std::to_string(hd.hi_fmt) + "), " +
              std::to_string(hd.mips) + " mips, " + std::to_string(hd.frames) + " frames" + (hd.faces == 6 ? ", cube map" : "") + (hd.depth > 1 ? ", depth " + std::to_string(hd.depth) : std::string()));
    } else {
        const Image im = decode(d, std::max(0, static_cast<int>(get(a, "frame").i)));
        save(im, S(a, "output"));
        print("wrote " + S(a, "output") + " " + std::to_string(im.w) + "x" + std::to_string(im.h));
    }
    return 0;
}
}  // namespace packtools::modder::vtf
