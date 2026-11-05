// modder_render3d.inc - `um render3d`: a C++ port of universal-modder's um/render3d.py + um/blender/render_sprites.py
// (Rehan and universal-modder contributors, MIT). Upstream scripts Blender (Python inside Blender, Cycles/EEVEE);
// this port needs neither: it reads the GLB itself (glTF 2.0: meshes, node transforms, skins at their bind pose,
// base colour factors and textures, vertex colours, alpha mask/blend, emission) and renders it with a built-in
// software rasteriser: the same cameras, presets, motions, normalisation and file names as upstream, a sun with
// soft shadows (shadow map + PCSS-style penumbra), uniform ambient light, supersampled anti-aliasing on a
// transparent background, and the optional shadow-only pass (_s.png, black with the shadow as alpha).
// It is a rasteriser, not a path tracer: no bounce light or ambient occlusion, so frames look flatter than Cycles.
// Also shared by `um b3d render` (modder_b3d.inc).
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

namespace packtools::modder::r3d {
using namespace pyrt;
using namespace packtools::modder;

constexpr double PI_ = 3.14159265358979323846;
inline double rad(double d) { return d * PI_ / 180.0; }

// ------------------------------------------------------------------ math
struct V3 { double x = 0, y = 0, z = 0; };
inline V3 v3(double x, double y, double z) { V3 v; v.x = x; v.y = y; v.z = z; return v; }
inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator*(V3 a, double k) { return v3(a.x * k, a.y * k, a.z * k); }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) { const double l = length(a); return l > 1e-20 ? a * (1.0 / l) : v3(0, 0, 1); }

struct M4 {   // row-major, column vectors: p' = M p
    double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    double& at(int r, int c) { return m[r * 4 + c]; }
    double at(int r, int c) const { return m[r * 4 + c]; }
};
inline M4 operator*(const M4& a, const M4& b) {
    M4 o; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { double s = 0; for (int k = 0; k < 4; ++k) s += a.at(r, k) * b.at(k, c); o.at(r, c) = s; } return o;
}
inline V3 xpoint(const M4& a, V3 p) {
    const double w = a.at(3, 0) * p.x + a.at(3, 1) * p.y + a.at(3, 2) * p.z + a.at(3, 3);
    V3 o = v3(a.at(0, 0) * p.x + a.at(0, 1) * p.y + a.at(0, 2) * p.z + a.at(0, 3), a.at(1, 0) * p.x + a.at(1, 1) * p.y + a.at(1, 2) * p.z + a.at(1, 3),
              a.at(2, 0) * p.x + a.at(2, 1) * p.y + a.at(2, 2) * p.z + a.at(2, 3));
    if (w != 0.0 && w != 1.0) o = o * (1.0 / w);
    return o;
}
inline V3 xdir(const M4& a, V3 p) { return v3(a.at(0, 0) * p.x + a.at(0, 1) * p.y + a.at(0, 2) * p.z, a.at(1, 0) * p.x + a.at(1, 1) * p.y + a.at(1, 2) * p.z, a.at(2, 0) * p.x + a.at(2, 1) * p.y + a.at(2, 2) * p.z); }
inline M4 translate(V3 t) { M4 o; o.at(0, 3) = t.x; o.at(1, 3) = t.y; o.at(2, 3) = t.z; return o; }
inline M4 scale3(V3 s) { M4 o; o.at(0, 0) = s.x; o.at(1, 1) = s.y; o.at(2, 2) = s.z; return o; }
inline M4 rot_x(double a) { M4 o; const double c = std::cos(a), s = std::sin(a); o.at(1, 1) = c; o.at(1, 2) = -s; o.at(2, 1) = s; o.at(2, 2) = c; return o; }
inline M4 rot_y(double a) { M4 o; const double c = std::cos(a), s = std::sin(a); o.at(0, 0) = c; o.at(0, 2) = s; o.at(2, 0) = -s; o.at(2, 2) = c; return o; }
inline M4 rot_z(double a) { M4 o; const double c = std::cos(a), s = std::sin(a); o.at(0, 0) = c; o.at(0, 1) = -s; o.at(1, 0) = s; o.at(1, 1) = c; return o; }
inline M4 euler_xyz(double x, double y, double z) { return rot_z(z) * rot_y(y) * rot_x(x); }   // Blender's XYZ Euler
inline M4 quat(double x, double y, double z, double w) {   // unit quaternion (x, y, z, w) -> rotation
    const double n = std::sqrt(x * x + y * y + z * z + w * w); if (n > 0) { x /= n; y /= n; z /= n; w /= n; }
    M4 o;
    o.at(0, 0) = 1 - 2 * (y * y + z * z); o.at(0, 1) = 2 * (x * y - z * w); o.at(0, 2) = 2 * (x * z + y * w);
    o.at(1, 0) = 2 * (x * y + z * w); o.at(1, 1) = 1 - 2 * (x * x + z * z); o.at(1, 2) = 2 * (y * z - x * w);
    o.at(2, 0) = 2 * (x * z - y * w); o.at(2, 1) = 2 * (y * z + x * w); o.at(2, 2) = 1 - 2 * (x * x + y * y);
    return o;
}
inline M4 inverse(const M4& a) {   // Gauss-Jordan with partial pivoting
    double t[4][8];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 8; ++c) t[r][c] = c < 4 ? a.at(r, c) : (c - 4 == r ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int piv = c; for (int r = c + 1; r < 4; ++r) if (std::fabs(t[r][c]) > std::fabs(t[piv][c])) piv = r;
        if (std::fabs(t[piv][c]) < 1e-300) return M4{};
        if (piv != c) for (int k = 0; k < 8; ++k) std::swap(t[piv][k], t[c][k]);
        const double d = t[c][c]; for (int k = 0; k < 8; ++k) t[c][k] /= d;
        for (int r = 0; r < 4; ++r) if (r != c) { const double f = t[r][c]; if (f != 0.0) for (int k = 0; k < 8; ++k) t[r][k] -= f * t[c][k]; }
    }
    M4 o; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) o.at(r, c) = t[r][c + 4]; return o;
}
inline M4 transpose_inv3(const M4& a) {   // normal matrix: inverse transpose of the upper 3x3
    M4 l; for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) l.at(r, c) = a.at(r, c);
    const M4 i = inverse(l); M4 o; for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) o.at(r, c) = i.at(c, r); return o;
}

// ------------------------------------------------------------------ colour
inline const std::array<float, 256>& srgb_lut() {
    static const std::array<float, 256> lut = [] { std::array<float, 256> l{}; for (int k = 0; k < 256; ++k) { const double c = k / 255.0; l[static_cast<std::size_t>(k)] = static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4)); } return l; }();
    return lut;
}
inline uint8_t to_srgb8(double lin) {   // "Standard" view transform: clip, then the sRGB curve
    if (!(lin > 0.0)) return 0; if (lin >= 1.0) return 255;
    const double c = lin <= 0.0031308 ? lin * 12.92 : 1.055 * std::pow(lin, 1.0 / 2.4) - 0.055;
    return static_cast<uint8_t>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0));
}

// ------------------------------------------------------------------ scene
struct Tex {
    long long w = 0, h = 0; std::vector<uint8_t> px;   // sRGB RGBA
    bool rep_s = true, rep_t = true, mir_s = false, mir_t = false;
    void sample(double u, double v, float out[4]) const {   // bilinear on linearised texels (colour), alpha linear
        if (w <= 0 || h <= 0) { out[0] = out[1] = out[2] = out[3] = 1.0f; return; }
        const auto& L = srgb_lut();
        const double fx = u * static_cast<double>(w) - 0.5, fy = v * static_cast<double>(h) - 0.5;
        const double x0f = std::floor(fx), y0f = std::floor(fy); const double ax = fx - x0f, ay = fy - y0f;
        auto wrap = [](long long k, long long n, bool rep, bool mir) -> long long {
            if (rep) { long long m = k % n; if (m < 0) m += n; return m; }
            if (mir) { const long long p = 2 * n; long long m = k % p; if (m < 0) m += p; return m < n ? m : p - 1 - m; }
            return std::clamp(k, 0LL, n - 1);
        };
        const long long x0 = static_cast<long long>(x0f), y0 = static_cast<long long>(y0f);
        const long long xs[2] = {wrap(x0, w, rep_s, mir_s), wrap(x0 + 1, w, rep_s, mir_s)}, ys[2] = {wrap(y0, h, rep_t, mir_t), wrap(y0 + 1, h, rep_t, mir_t)};
        double acc[4] = {0, 0, 0, 0};
        for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) {
            const double wt = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
            const uint8_t* p = &px[static_cast<std::size_t>((ys[j] * w + xs[i]) * 4)];
            acc[0] += wt * L[p[0]]; acc[1] += wt * L[p[1]]; acc[2] += wt * L[p[2]]; acc[3] += wt * (p[3] / 255.0);
        }
        for (int k = 0; k < 4; ++k) out[k] = static_cast<float>(acc[k]);
    }
};
struct Mat {
    float base[4] = {0.8f, 0.8f, 0.8f, 1.0f};   // Blender's default material when a primitive has none
    int tex = -1; float emis[3] = {0, 0, 0}; int emis_tex = -1;
    int alpha_mode = 0;   // 0 opaque, 1 mask, 2 blend
    float cutoff = 0.5f; bool unlit = false;
};
struct Vtx { V3 p, n; float u = 0, v = 0; float c[4] = {1, 1, 1, 1}; };
struct Scene {
    std::vector<Vtx> v;            // 3 per triangle
    std::vector<int> tri_mat;      // material per triangle
    std::vector<uint8_t> tri_flat; // 1: no normals in the file, use the face normal
    std::vector<Mat> mats; std::vector<Tex> texs;
    std::vector<V3> box_pts;       // object bounding-box corners (for normalising, like Blender's bound_box)
    std::size_t tris() const { return tri_mat.size(); }
};

// ------------------------------------------------------------------ base64 (data: URIs, Luau blobs)
inline std::string b64decode(const std::string& in) {
    auto val = [](unsigned char c) -> int { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26; if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+' || c == '-') return 62; if (c == '/' || c == '_') return 63; return -1; };
    std::string out; unsigned buf = 0; int bits = 0;
    for (unsigned char c : in) { const int d = val(c); if (d < 0) continue; buf = (buf << 6) | static_cast<unsigned>(d); bits += 6; if (bits >= 8) { bits -= 8; out += static_cast<char>((buf >> bits) & 0xFF); } }
    return out;
}
inline std::string b64encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; std::size_t k = 0;
    for (; k + 2 < in.size(); k += 3) { const unsigned n = (static_cast<unsigned char>(in[k]) << 16) | (static_cast<unsigned char>(in[k + 1]) << 8) | static_cast<unsigned char>(in[k + 2]); o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63]; o += T[(n >> 6) & 63]; o += T[n & 63]; }
    if (k + 1 == in.size()) { const unsigned n = static_cast<unsigned char>(in[k]) << 16; o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63]; o += "=="; }
    else if (k + 2 == in.size()) { const unsigned n = (static_cast<unsigned char>(in[k]) << 16) | (static_cast<unsigned char>(in[k + 1]) << 8); o += T[(n >> 18) & 63]; o += T[(n >> 12) & 63]; o += T[(n >> 6) & 63]; o += '='; }
    return o;
}

// decode PNG built in; JPEG/WebP/others through ffmpeg (img::load) via a temporary file
inline Tex decode_image(const std::string& blob, const std::string& mime, const std::string& label, const std::string& tmpdir) {
    img::Image im;
    if (blob.size() >= 8 && blob.compare(0, 8, img::PNG_SIG) == 0) im = img::decode_png(blob, label);
    else {
        static std::atomic<int> seq{0};
        std::string ext = mime.find("jpeg") != std::string::npos || mime.find("jpg") != std::string::npos ? ".jpg" : mime.find("webp") != std::string::npos ? ".webp" : mime.find("ktx2") != std::string::npos ? ".ktx2" : ".img";
        if (blob.size() >= 3 && static_cast<unsigned char>(blob[0]) == 0xFF && static_cast<unsigned char>(blob[1]) == 0xD8) ext = ".jpg";
        if (ext == ".ktx2") die("texture " + label + " is KTX2/Basis (KHR_texture_basisu): convert the textures to PNG or JPEG first (for example `gltf-transform png in.glb out.glb`)");
        const std::string tmp = path_join(tmpdir, ".um-r3d-tex-" + std::to_string(++seq) + ext);
        spit(tmp, blob);
        try { im = img::load(tmp); } catch (...) { std::error_code ec; fs::remove(P(tmp), ec); throw; }
        std::error_code ec; fs::remove(P(tmp), ec);
    }
    Tex t; t.w = im.w; t.h = im.h; t.px = std::move(im.px); return t;
}

// ------------------------------------------------------------------ glTF 2.0 / GLB reader
struct Gltf {
    Val j; std::vector<std::string> buffers; std::string dir, label;
    static uint32_t le32(const std::string& s, std::size_t at) { return static_cast<uint32_t>(static_cast<unsigned char>(s[at])) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 3])) << 24); }
    const Val& arr(const char* k) const { static const Val empty = Val::list(); const Val* p = j.t == Val::DICT ? j.d->get(k) : nullptr; return p && p->t == Val::LIST ? *p : empty; }
    static double num(const Val& o, const char* k, double dflt) { const Val v = get(o, k); return v.is_real() ? v.num() : dflt; }
    static long long inum(const Val& o, const char* k, long long dflt) { const Val v = get(o, k); return v.is_real() ? static_cast<long long>(v.num()) : dflt; }

    std::string uri_bytes(const std::string& uri) const {
        if (starts(uri, "data:")) { const auto c = uri.find(','); if (c == std::string::npos) die(label + ": bad data URI"); return uri.substr(0, c).find(";base64") != std::string::npos ? b64decode(uri.substr(c + 1)) : uri.substr(c + 1); }
        std::string rel; for (std::size_t k = 0; k < uri.size(); ++k) {   // percent-decode
            if (uri[k] == '%' && k + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[k + 1])) && std::isxdigit(static_cast<unsigned char>(uri[k + 2]))) { rel += static_cast<char>(std::stoi(uri.substr(k + 1, 2), nullptr, 16)); k += 2; }
            else rel += uri[k];
        }
        const std::string p = path_join(dir, rel);
        if (!isfile(p)) die(label + ": missing external file " + rel);
        return slurp(p);
    }
    void open(const std::string& path) {
        label = path; dir = u8native(P(path).parent_path()); if (dir.empty()) dir = ".";
        if (!isfile(path)) die("no such file: " + path);
        const std::string data = slurp(path);
        std::string bin; bool have_bin = false;
        if (data.size() >= 12 && data.compare(0, 4, "glTF") == 0) {
            if (le32(data, 4) != 2) die(path + ": only glTF 2.0 GLB files are supported (version " + std::to_string(le32(data, 4)) + ")");
            std::size_t at = 12; std::string json;
            while (at + 8 <= data.size()) {
                const uint32_t len = le32(data, at), kind = le32(data, at + 4); at += 8;
                if (at + len > data.size()) die(path + ": truncated GLB chunk");
                if (kind == 0x4E4F534Au) json = data.substr(at, len); else if (kind == 0x004E4942u && !have_bin) { bin = data.substr(at, len); have_bin = true; }
                at += (len + 3) & ~3u;
            }
            if (json.empty()) die(path + ": GLB without a JSON chunk");
            j = loads(json);
        } else {
            try { j = loads(data); } catch (...) { die(path + ": not a GLB or glTF file"); }
        }
        if (j.t != Val::DICT) die(path + ": not a glTF document");
        for (const char* ext : {"KHR_draco_mesh_compression", "EXT_meshopt_compression", "KHR_mesh_quantization_unsupported"}) {
            for (const auto& e : *arr("extensionsRequired").l) if (e.t == Val::STR && e.s == ext)
                die(path + ": uses " + std::string(ext) + ", which this renderer doesn't decode: write the GLB without mesh compression (Blender: untick Draco; or `gltf-transform copy in.glb out.glb` after decompressing)");
        }
        const Val& bufs = arr("buffers");
        for (std::size_t k = 0; k < bufs.size(); ++k) {
            const Val& b = bufs[k]; const Val uri = get(b, "uri");
            if (uri.t == Val::STR) buffers.push_back(uri_bytes(uri.s));
            else if (k == 0 && have_bin) buffers.push_back(bin);
            else die(path + ": buffer " + std::to_string(k) + " has no data");
        }
    }
    std::string view_bytes(long long vi, std::size_t& stride) const {
        const Val& views = arr("bufferViews"); if (vi < 0 || static_cast<std::size_t>(vi) >= views.size()) die(label + ": bad bufferView index");
        const Val& bv = views[static_cast<std::size_t>(vi)];
        const long long bi = inum(bv, "buffer", 0), off = inum(bv, "byteOffset", 0), len = inum(bv, "byteLength", 0);
        if (bi < 0 || static_cast<std::size_t>(bi) >= buffers.size()) die(label + ": bad buffer index");
        const std::string& b = buffers[static_cast<std::size_t>(bi)];
        if (off < 0 || len < 0 || static_cast<std::size_t>(off + len) > b.size()) die(label + ": bufferView past the end of its buffer");
        stride = static_cast<std::size_t>(inum(bv, "byteStride", 0));
        return b.substr(static_cast<std::size_t>(off), static_cast<std::size_t>(len));
    }
    static int comps(const std::string& type) { if (type == "SCALAR") return 1; if (type == "VEC2") return 2; if (type == "VEC3") return 3; if (type == "VEC4") return 4; if (type == "MAT2") return 4; if (type == "MAT3") return 9; if (type == "MAT4") return 16; return 0; }
    static int csize(long long ct) { switch (ct) { case 5120: case 5121: return 1; case 5122: case 5123: return 2; case 5125: case 5126: return 4; default: return 0; } }
    static double read_one(const std::string& s, std::size_t at, long long ct, bool norm) {
        auto u8 = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
        switch (ct) {
            case 5120: { const auto v = static_cast<int8_t>(u8(at)); return norm ? std::max(v / 127.0, -1.0) : v; }
            case 5121: { const auto v = u8(at); return norm ? v / 255.0 : v; }
            case 5122: { const auto v = static_cast<int16_t>(u8(at) | (u8(at + 1) << 8)); return norm ? std::max(v / 32767.0, -1.0) : v; }
            case 5123: { const auto v = static_cast<uint16_t>(u8(at) | (u8(at + 1) << 8)); return norm ? v / 65535.0 : v; }
            case 5125: { const uint32_t v = le32(s, at); return norm ? v / 4294967295.0 : static_cast<double>(v); }
            case 5126: { const uint32_t b = le32(s, at); float f; std::memcpy(&f, &b, 4); return f; }
            default: return 0;
        }
    }
    std::vector<double> accessor(long long ai, int& nc) const {   // flattened values, nc components each
        const Val& accs = arr("accessors"); if (ai < 0 || static_cast<std::size_t>(ai) >= accs.size()) die(label + ": bad accessor index");
        const Val& a = accs[static_cast<std::size_t>(ai)];
        nc = comps(get(a, "type").s); const long long ct = inum(a, "componentType", 5126), count = inum(a, "count", 0); const bool norm = truthy(get(a, "normalized"));
        const int cs = csize(ct); if (!nc || !cs || count < 0) die(label + ": unsupported accessor layout");
        std::vector<double> out(static_cast<std::size_t>(count) * static_cast<std::size_t>(nc), 0.0);
        if (has(a, "bufferView")) {
            std::size_t stride = 0; const std::string b = view_bytes(inum(a, "bufferView", 0), stride);
            const std::size_t off = static_cast<std::size_t>(inum(a, "byteOffset", 0)), el = static_cast<std::size_t>(cs) * static_cast<std::size_t>(nc);
            if (!stride) stride = el;
            if (count > 0 && off + stride * static_cast<std::size_t>(count - 1) + el > b.size()) die(label + ": accessor past the end of its bufferView");
            for (long long k = 0; k < count; ++k) for (int c = 0; c < nc; ++c)
                out[static_cast<std::size_t>(k) * static_cast<std::size_t>(nc) + static_cast<std::size_t>(c)] = read_one(b, off + stride * static_cast<std::size_t>(k) + static_cast<std::size_t>(c * cs), ct, norm);
        }
        const Val sp = get(a, "sparse");
        if (sp.t == Val::DICT) {
            const long long n = inum(sp, "count", 0); const Val si = get(sp, "indices"), sv = get(sp, "values");
            std::size_t s1 = 0, s2 = 0; const std::string ib = view_bytes(inum(si, "bufferView", 0), s1), vb = view_bytes(inum(sv, "bufferView", 0), s2);
            const long long ict = inum(si, "componentType", 5125); const int ics = csize(ict);
            const std::size_t io = static_cast<std::size_t>(inum(si, "byteOffset", 0)), vo = static_cast<std::size_t>(inum(sv, "byteOffset", 0));
            for (long long k = 0; k < n; ++k) {
                const std::size_t ia = io + static_cast<std::size_t>(k * ics); if (ia + static_cast<std::size_t>(ics) > ib.size()) die(label + ": sparse indices out of range");
                const long long idx = static_cast<long long>(read_one(ib, ia, ict, false)); if (idx < 0 || idx >= count) continue;
                for (int c = 0; c < nc; ++c) { const std::size_t va = vo + static_cast<std::size_t>((k * nc + c) * cs); if (va + static_cast<std::size_t>(cs) > vb.size()) die(label + ": sparse values out of range"); out[static_cast<std::size_t>(idx * nc + c)] = read_one(vb, va, ct, norm); }
            }
        }
        return out;
    }
    M4 local(const Val& n) const {
        const Val mx = get(n, "matrix");
        if (mx.t == Val::LIST && mx.size() == 16) { M4 o; for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) o.at(r, c) = mx[static_cast<std::size_t>(c * 4 + r)].num(); return o; }   // column-major
        M4 o; const Val t = get(n, "translation"), r = get(n, "rotation"), s = get(n, "scale");
        if (t.t == Val::LIST && t.size() == 3) o = translate(v3(t[0].num(), t[1].num(), t[2].num()));
        if (r.t == Val::LIST && r.size() == 4) o = o * quat(r[0].num(), r[1].num(), r[2].num(), r[3].num());
        if (s.t == Val::LIST && s.size() == 3) o = o * scale3(v3(s[0].num(), s[1].num(), s[2].num()));
        return o;
    }
};

// glTF is +Y up; Blender (and render_sprites.py's maths) is +Z up: (x, y, z)_blender = (x, -z, y)_gltf
inline M4 y_up_to_z_up() { M4 c; c.at(1, 1) = 0; c.at(1, 2) = -1; c.at(2, 1) = 1; c.at(2, 2) = 0; return c; }

inline Scene load_glb(const std::string& path, const std::string& tmpdir) {
    Gltf g; g.open(path);
    Scene sc;
    // textures and materials
    const Val& images = g.arr("images"); const Val& textures = g.arr("textures"); const Val& samplers = g.arr("samplers");
    std::vector<int> tex_of_image(images.size(), -2);
    auto texture = [&](const Val& info) -> int {
        if (info.t != Val::DICT) return -1;
        const long long ti = Gltf::inum(info, "index", -1); if (ti < 0 || static_cast<std::size_t>(ti) >= textures.size()) return -1;
        const Val& t = textures[static_cast<std::size_t>(ti)];
        long long src = Gltf::inum(t, "source", -1);
        if (src < 0) { const Val ex = get(t, "extensions"); for (const char* e : {"EXT_texture_webp", "KHR_texture_basisu", "MSFT_texture_dds"}) if (has(ex, e)) src = Gltf::inum(get(ex, e), "source", -1); }
        if (src < 0 || static_cast<std::size_t>(src) >= images.size()) return -1;
        int& slot = tex_of_image[static_cast<std::size_t>(src)];
        if (slot == -2) {
            const Val& im = images[static_cast<std::size_t>(src)]; std::string blob;
            if (has(im, "bufferView")) { std::size_t st = 0; blob = g.view_bytes(Gltf::inum(im, "bufferView", 0), st); }
            else if (get(im, "uri").t == Val::STR) blob = g.uri_bytes(get(im, "uri").s);
            if (blob.empty()) { slot = -1; return -1; }
            Tex tx = decode_image(blob, get(im, "mimeType").t == Val::STR ? get(im, "mimeType").s : get(im, "uri").s, path + " image " + std::to_string(src), tmpdir);
            const long long si = Gltf::inum(t, "sampler", -1);
            if (si >= 0 && static_cast<std::size_t>(si) < samplers.size()) {
                const long long ws = Gltf::inum(samplers[static_cast<std::size_t>(si)], "wrapS", 10497), wt = Gltf::inum(samplers[static_cast<std::size_t>(si)], "wrapT", 10497);
                tx.rep_s = ws == 10497; tx.mir_s = ws == 33648; tx.rep_t = wt == 10497; tx.mir_t = wt == 33648;
            }
            sc.texs.push_back(std::move(tx)); slot = static_cast<int>(sc.texs.size()) - 1;
        }
        return slot;
    };
    for (const auto& m : *g.arr("materials").l) {
        Mat mt; const Val pbr = get(m, "pbrMetallicRoughness");
        mt.base[0] = mt.base[1] = mt.base[2] = mt.base[3] = 1.0f;
        const Val bf = get(pbr, "baseColorFactor"); if (bf.t == Val::LIST && bf.size() == 4) for (std::size_t k = 0; k < 4; ++k) mt.base[k] = static_cast<float>(bf[k].num());
        mt.tex = texture(get(pbr, "baseColorTexture"));
        const Val sg = get(get(m, "extensions"), "KHR_materials_pbrSpecularGlossiness");
        if (sg.t == Val::DICT) { const Val df = get(sg, "diffuseFactor"); if (df.t == Val::LIST && df.size() == 4) for (std::size_t k = 0; k < 4; ++k) mt.base[k] = static_cast<float>(df[k].num()); if (mt.tex < 0) mt.tex = texture(get(sg, "diffuseTexture")); }
        const Val ef = get(m, "emissiveFactor"); if (ef.t == Val::LIST && ef.size() == 3) for (std::size_t k = 0; k < 3; ++k) mt.emis[k] = static_cast<float>(ef[k].num());
        const Val es = get(get(m, "extensions"), "KHR_materials_emissive_strength"); if (es.t == Val::DICT) for (float& e : mt.emis) e *= static_cast<float>(Gltf::num(es, "emissiveStrength", 1.0));
        mt.emis_tex = texture(get(m, "emissiveTexture"));
        const std::string am = get(m, "alphaMode").t == Val::STR ? get(m, "alphaMode").s : "OPAQUE";
        mt.alpha_mode = am == "MASK" ? 1 : am == "BLEND" ? 2 : 0; mt.cutoff = static_cast<float>(Gltf::num(m, "alphaCutoff", 0.5));
        mt.unlit = has(get(m, "extensions"), "KHR_materials_unlit");
        sc.mats.push_back(mt);
    }
    const int default_mat = static_cast<int>(sc.mats.size()); sc.mats.push_back(Mat{});

    // node world matrices
    const Val& nodes = g.arr("nodes"); const std::size_t N = nodes.size();
    std::vector<int> parent(N, -1);
    for (std::size_t k = 0; k < N; ++k) for (const auto& c : *get(nodes[k], "children", Val::list()).l) { const long long ci = static_cast<long long>(c.num()); if (ci >= 0 && static_cast<std::size_t>(ci) < N) parent[static_cast<std::size_t>(ci)] = static_cast<int>(k); }
    std::vector<M4> world(N); std::vector<int> state(N, 0);
    std::function<void(std::size_t)> solve = [&](std::size_t k) {
        if (state[k] == 2) return; if (state[k] == 1) die(path + ": node hierarchy has a cycle");
        state[k] = 1; const M4 l = g.local(nodes[k]);
        if (parent[k] >= 0) { solve(static_cast<std::size_t>(parent[k])); world[k] = world[static_cast<std::size_t>(parent[k])] * l; } else world[k] = l;
        state[k] = 2;
    };
    for (std::size_t k = 0; k < N; ++k) solve(k);
    // which nodes are in the scene
    std::vector<uint8_t> in_scene(N, 0);
    const Val& scenes = g.arr("scenes");
    std::vector<long long> roots;
    if (!scenes.size()) { for (std::size_t k = 0; k < N; ++k) if (parent[k] < 0) roots.push_back(static_cast<long long>(k)); }
    else { const long long si = std::clamp(Gltf::inum(g.j, "scene", 0), 0LL, static_cast<long long>(scenes.size()) - 1); for (const auto& r : *get(scenes[static_cast<std::size_t>(si)], "nodes", Val::list()).l) roots.push_back(static_cast<long long>(r.num())); }
    std::function<void(long long)> mark = [&](long long k) { if (k < 0 || static_cast<std::size_t>(k) >= N || in_scene[static_cast<std::size_t>(k)]) return; in_scene[static_cast<std::size_t>(k)] = 1; for (const auto& c : *get(nodes[static_cast<std::size_t>(k)], "children", Val::list()).l) mark(static_cast<long long>(c.num())); };
    for (long long r : roots) mark(r);

    const M4 C = y_up_to_z_up();
    const Val& meshes = g.arr("meshes"); const Val& skins = g.arr("skins");
    for (std::size_t k = 0; k < N; ++k) {
        if (!in_scene[k]) continue;
        const long long mi = Gltf::inum(nodes[k], "mesh", -1); if (mi < 0 || static_cast<std::size_t>(mi) >= meshes.size()) continue;
        const long long ski = Gltf::inum(nodes[k], "skin", -1);
        std::vector<M4> joint_m;   // skinned: joint world * inverse bind (the node's own transform is ignored, per the spec)
        if (ski >= 0 && static_cast<std::size_t>(ski) < skins.size()) {
            const Val& sk = skins[static_cast<std::size_t>(ski)]; const Val js = get(sk, "joints", Val::list());
            std::vector<double> ibm; int nc = 16; if (has(sk, "inverseBindMatrices")) ibm = g.accessor(Gltf::inum(sk, "inverseBindMatrices", 0), nc);
            for (std::size_t q = 0; q < js.size(); ++q) {
                const long long jn = static_cast<long long>(js[q].num()); M4 jw = jn >= 0 && static_cast<std::size_t>(jn) < N ? world[static_cast<std::size_t>(jn)] : M4{};
                M4 ib; if (nc == 16 && ibm.size() >= (q + 1) * 16) for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) ib.at(r, c) = ibm[q * 16 + static_cast<std::size_t>(c * 4 + r)];
                joint_m.push_back(jw * ib);
            }
        }
        const M4 W = C * world[k];
        V3 lo = v3(1e300, 1e300, 1e300), hi = v3(-1e300, -1e300, -1e300); bool any = false;
        std::vector<V3> skinned_pts;
        for (const auto& prim : *get(meshes[static_cast<std::size_t>(mi)], "primitives", Val::list()).l) {
            const long long mode = Gltf::inum(prim, "mode", 4);
            if (mode != 4 && mode != 5 && mode != 6) continue;   // points and lines don't render in Blender either
            const Val at = get(prim, "attributes"); if (!has(at, "POSITION")) continue;
            int ncp = 0, ncn = 0, ncu = 0, ncc = 0, ncj = 0, ncw = 0;
            const std::vector<double> pos = g.accessor(Gltf::inum(at, "POSITION", 0), ncp);
            if (ncp != 3) continue;
            const std::size_t nv = pos.size() / 3;
            std::vector<double> nrm, uv, col, jo, we;
            if (has(at, "NORMAL")) { nrm = g.accessor(Gltf::inum(at, "NORMAL", 0), ncn); if (ncn != 3 || nrm.size() / 3 != nv) nrm.clear(); }
            if (has(at, "TEXCOORD_0")) { uv = g.accessor(Gltf::inum(at, "TEXCOORD_0", 0), ncu); if (ncu != 2 || uv.size() / 2 != nv) uv.clear(); }
            if (has(at, "COLOR_0")) { col = g.accessor(Gltf::inum(at, "COLOR_0", 0), ncc); if ((ncc != 3 && ncc != 4) || col.size() / static_cast<std::size_t>(ncc) != nv) col.clear(); }
            const bool skinned = !joint_m.empty() && has(at, "JOINTS_0") && has(at, "WEIGHTS_0");
            if (skinned) { jo = g.accessor(Gltf::inum(at, "JOINTS_0", 0), ncj); we = g.accessor(Gltf::inum(at, "WEIGHTS_0", 0), ncw); if (ncj != 4 || ncw != 4 || jo.size() / 4 != nv || we.size() / 4 != nv) { jo.clear(); we.clear(); } }
            // vertex positions/normals in Blender world space
            std::vector<V3> P_(nv), N_(nv);
            for (std::size_t q = 0; q < nv; ++q) {
                const V3 p = v3(pos[q * 3], pos[q * 3 + 1], pos[q * 3 + 2]); const V3 n = nrm.empty() ? v3(0, 0, 1) : v3(nrm[q * 3], nrm[q * 3 + 1], nrm[q * 3 + 2]);
                if (!jo.empty()) {
                    M4 S; for (double& e : S.m) e = 0; double wsum = 0;
                    for (std::size_t c = 0; c < 4; ++c) { const double w = we[q * 4 + c]; const std::size_t ji = static_cast<std::size_t>(jo[q * 4 + c]); if (w <= 0 || ji >= joint_m.size()) continue; for (int e = 0; e < 16; ++e) S.m[e] += w * joint_m[ji].m[e]; wsum += w; }
                    if (wsum <= 0) S = world[k]; else if (std::fabs(wsum - 1.0) > 1e-6) for (double& e : S.m) e /= wsum;
                    const M4 CS = C * S; P_[q] = xpoint(CS, p); N_[q] = normalize(xdir(transpose_inv3(CS), n));
                    skinned_pts.push_back(P_[q]);
                } else {
                    P_[q] = xpoint(W, p); N_[q] = normalize(xdir(transpose_inv3(W), n));
                    lo = v3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)); hi = v3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)); any = true;
                }
            }
            // indices
            std::vector<std::size_t> idx;
            if (has(prim, "indices")) { int nci = 0; const auto iv = g.accessor(Gltf::inum(prim, "indices", 0), nci); idx.reserve(iv.size()); for (double d : iv) idx.push_back(static_cast<std::size_t>(d)); }
            else { idx.resize(nv); for (std::size_t q = 0; q < nv; ++q) idx[q] = q; }
            std::vector<std::array<std::size_t, 3>> tris;
            if (mode == 4) for (std::size_t q = 0; q + 2 < idx.size(); q += 3) tris.push_back({idx[q], idx[q + 1], idx[q + 2]});
            else if (mode == 5) for (std::size_t q = 0; q + 2 < idx.size(); ++q) tris.push_back(q % 2 ? std::array<std::size_t, 3>{idx[q + 1], idx[q], idx[q + 2]} : std::array<std::size_t, 3>{idx[q], idx[q + 1], idx[q + 2]});
            else for (std::size_t q = 1; q + 1 < idx.size(); ++q) tris.push_back({idx[0], idx[q], idx[q + 1]});
            const long long pm = Gltf::inum(prim, "material", -1);
            const int mat = pm >= 0 && static_cast<std::size_t>(pm) < sc.mats.size() - 1 ? static_cast<int>(pm) : default_mat;
            for (const auto& t : tris) {
                if (t[0] >= nv || t[1] >= nv || t[2] >= nv) continue;
                for (std::size_t c = 0; c < 3; ++c) {
                    Vtx x; const std::size_t q = t[c]; x.p = P_[q]; x.n = N_[q];
                    if (!uv.empty()) { x.u = static_cast<float>(uv[q * 2]); x.v = static_cast<float>(uv[q * 2 + 1]); }
                    if (!col.empty()) for (std::size_t e = 0; e < static_cast<std::size_t>(ncc); ++e) x.c[e] = static_cast<float>(col[q * static_cast<std::size_t>(ncc) + e]);
                    sc.v.push_back(x);
                }
                sc.tri_mat.push_back(mat); sc.tri_flat.push_back(nrm.empty() ? 1 : 0);
            }
        }
        if (any) for (int c = 0; c < 8; ++c) sc.box_pts.push_back(xpoint(W, v3(c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z)));
        for (const auto& p : skinned_pts) sc.box_pts.push_back(p);
    }
    if (!sc.tris()) die(path + ": no triangle meshes in the scene");
    return sc;
}
}  // namespace packtools::modder::r3d

// ------------------------------------------------------------------ rasteriser
namespace packtools::modder::r3d {
struct Cam {
    bool persp = false; V3 pos, right = v3(1, 0, 0), up = v3(0, 0, 1), fwd = v3(0, 1, 0);
    double f = 1;               // ortho: buffer px per unit; persp: focal length in buffer px
    double cx = 0, cy = 0;      // principal point (buffer px, y down)
    double near_ = 1e-4;
    V3 view(V3 p) const { const V3 d = p - pos; return v3(dot(d, right), dot(d, up), dot(d, fwd)); }
    void screen(V3 vw, double& sx, double& sy) const { if (persp) { sx = cx + f * vw.x / vw.z; sy = cy - f * vw.y / vw.z; } else { sx = cx + f * vw.x; sy = cy - f * vw.y; } }
    V3 ray_dir(double sx, double sy) const { return persp ? normalize(fwd + right * ((sx - cx) / f) + up * ((cy - sy) / f)) : fwd; }
    V3 ray_origin(double sx, double sy) const { return persp ? pos : pos + right * ((sx - cx) / f) + up * ((cy - sy) / f); }
};
struct Light { V3 dir = v3(0, 0, -1); double sun = 4.0, ambient = 0.9, half_angle = rad(4.0); bool cast = true; };

template <class F> inline void raster_tri(double x0, double y0, double x1, double y1, double x2, double y2, int W, int H, F&& f) {
    const double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (!(std::fabs(area) > 1e-12)) return;
    const double lo_x = std::min({x0, x1, x2}), hi_x = std::max({x0, x1, x2}), lo_y = std::min({y0, y1, y2}), hi_y = std::max({y0, y1, y2});
    if (hi_x < 0 || hi_y < 0 || lo_x > W || lo_y > H) return;
    const int ax = std::max(0, static_cast<int>(std::floor(lo_x))), bx = std::min(W - 1, static_cast<int>(std::ceil(hi_x)));
    const int ay = std::max(0, static_cast<int>(std::floor(lo_y))), by = std::min(H - 1, static_cast<int>(std::ceil(hi_y)));
    const double inv = 1.0 / area, eps = -1e-9;
    for (int y = ay; y <= by; ++y) {
        const double py = y + 0.5;
        for (int x = ax; x <= bx; ++x) {
            const double px = x + 0.5;
            const double w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) * inv;
            if (w0 < eps) continue;
            const double w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) * inv;
            if (w1 < eps) continue;
            const double w2 = 1.0 - w0 - w1;
            if (w2 < eps) continue;
            f(x, y, w0, w1, w2);
        }
    }
}
inline double hash01(uint32_t a, uint32_t b, uint32_t c) {   // deterministic per-sample noise
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return (h >> 8) * (1.0 / 16777216.0);
}

// sun shadow map over the posed model, looked up with a PCSS-style soft penumbra (sun angle like Blender's sun.angle)
struct ShadowMap {
    int R = 0; V3 lx, ly, lz; double x0 = 0, y0 = 0, texel = 1, dmin = 0; std::vector<float> depth;
    bool ok() const { return R > 0; }
    void build(const std::vector<V3>& P, const Light& L, int res) {
        lz = normalize(L.dir); const V3 helper = std::fabs(lz.z) < 0.95 ? v3(0, 0, 1) : v3(1, 0, 0);
        lx = normalize(cross(helper, lz)); ly = cross(lz, lx);
        double ax = 1e300, bx = -1e300, ay = 1e300, by = -1e300; dmin = 1e300;
        for (const auto& p : P) { const double u = dot(p, lx), v = dot(p, ly), d = dot(p, lz); ax = std::min(ax, u); bx = std::max(bx, u); ay = std::min(ay, v); by = std::max(by, v); dmin = std::min(dmin, d); }
        if (P.empty()) { R = 0; return; }
        const double ext = std::max({bx - ax, by - ay, 1e-6}) * 1.04;
        R = res; texel = ext / R; x0 = (ax + bx) / 2 - ext / 2; y0 = (ay + by) / 2 - ext / 2;
        depth.assign(static_cast<std::size_t>(R) * static_cast<std::size_t>(R), std::numeric_limits<float>::infinity());
        for (std::size_t t = 0; t + 2 < P.size(); t += 3) {
            double sx[3], sy[3], sd[3];
            for (int c = 0; c < 3; ++c) { const V3 p = P[t + static_cast<std::size_t>(c)]; sx[c] = (dot(p, lx) - x0) / texel; sy[c] = (dot(p, ly) - y0) / texel; sd[c] = dot(p, lz); }
            raster_tri(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], R, R, [&](int x, int y, double w0, double w1, double w2) {
                float& z = depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(R) + static_cast<std::size_t>(x)];
                const float d = static_cast<float>(w0 * sd[0] + w1 * sd[1] + w2 * sd[2]); if (d < z) z = d;
            });
        }
    }
    float at(double u, double v) const {
        const long long x = static_cast<long long>(std::floor(u)), y = static_cast<long long>(std::floor(v));
        if (x < 0 || y < 0 || x >= R || y >= R) return std::numeric_limits<float>::infinity();
        return depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(R) + static_cast<std::size_t>(x)];
    }
    // fraction of the sun visible from p (normal n faces the sun side)
    double vis(V3 p, V3 n, double half_angle, double rot) const {
        if (!ok()) return 1.0;
        const double ndl = std::max(1e-3, -dot(n, lz));
        const V3 q = p + n * (texel * 1.5);
        const double u = (dot(q, lx) - x0) / texel, v = (dot(q, ly) - y0) / texel, d = dot(q, lz);
        const double slope = std::sqrt(std::max(0.0, 1.0 - ndl * ndl)) / ndl;
        const double bias = texel * (1.0 + std::min(4.0, slope));
        const double t = std::tan(half_angle);
        const double search = std::clamp((d - dmin) * t / texel, 1.0, 96.0);
        if (u < -search || v < -search || u > R + search || v > R + search) return 1.0;
        const double ga = 2.39996322972865332;   // golden angle spiral, turned per sample
        double bsum = 0; int bn = 0;
        for (int k = 0; k < 16; ++k) {
            const double r = std::sqrt((k + 0.5) / 16.0) * search, a = k * ga + rot;
            const float z = at(u + r * std::cos(a), v + r * std::sin(a));
            if (z < d - bias) { bsum += z; ++bn; }
        }
        if (!bn) return 1.0;
        const double pen = std::clamp((d - bsum / bn) * t / texel, 0.6, 96.0);
        int lit = 0; const int NS = 24;
        for (int k = 0; k < NS; ++k) {
            const double r = std::sqrt((k + 0.5) / NS) * pen, a = k * ga + rot + 0.5;
            if (!(at(u + r * std::cos(a), v + r * std::sin(a)) < d - bias)) ++lit;
        }
        return static_cast<double>(lit) / NS;
    }
};

struct Target { int W = 0, H = 0, ss = 1; bool opaque = false; double bg[3] = {0, 0, 0}; };

// Render the posed scene (P/N = posed positions and normals, one per scene vertex). Returns RGBA 8-bit.
inline img::Image render_main(const Scene& sc, const std::vector<V3>& P, const std::vector<V3>& N, const Cam& cam, const Light& L, const ShadowMap* sm, const Target& tg) {
    const int BW = tg.W * tg.ss, BH = tg.H * tg.ss; const std::size_t NB = static_cast<std::size_t>(BW) * static_cast<std::size_t>(BH);
    std::vector<float> zb(NB, std::numeric_limits<float>::infinity()); std::vector<int32_t> tb(NB, -1); std::vector<float> b1(NB, 0.0f), b2(NB, 0.0f);
    struct CV { V3 vw; double b[3]; };
    for (std::size_t t = 0; t < sc.tris(); ++t) {
        const Mat& mt = sc.mats[static_cast<std::size_t>(sc.tri_mat[t])];
        CV in[3];
        for (int c = 0; c < 3; ++c) { in[c].vw = cam.view(P[t * 3 + static_cast<std::size_t>(c)]); in[c].b[0] = c == 0; in[c].b[1] = c == 1; in[c].b[2] = c == 2; }
        CV poly[4]; int np = 0;
        if (cam.persp) {   // clip against the near plane (z >= near)
            for (int c = 0; c < 3; ++c) {
                const CV& a = in[c]; const CV& b = in[(c + 1) % 3];
                const bool ia = a.vw.z >= cam.near_, ib = b.vw.z >= cam.near_;
                if (ia) poly[np++] = a;
                if (ia != ib && np < 4) {
                    const double s = (cam.near_ - a.vw.z) / (b.vw.z - a.vw.z); CV m;
                    m.vw = a.vw + (b.vw - a.vw) * s; for (int e = 0; e < 3; ++e) m.b[e] = a.b[e] + (b.b[e] - a.b[e]) * s;
                    poly[np++] = m;
                }
            }
        } else { for (int c = 0; c < 3; ++c) poly[c] = in[c]; np = 3; }
        for (int k = 1; k + 1 < np; ++k) {
            const CV* q[3] = {&poly[0], &poly[k], &poly[k + 1]};
            double sx[3], sy[3];
            for (int c = 0; c < 3; ++c) cam.screen(q[c]->vw, sx[c], sy[c]);
            raster_tri(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], BW, BH, [&](int x, int y, double l0, double l1, double l2) {
                double w[3] = {l0, l1, l2}, z;
                if (cam.persp) { double s = 0; for (int c = 0; c < 3; ++c) { w[c] /= q[c]->vw.z; s += w[c]; } for (double& e : w) e /= s; z = 1.0 / s; }
                else z = l0 * q[0]->vw.z + l1 * q[1]->vw.z + l2 * q[2]->vw.z;
                const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(BW) + static_cast<std::size_t>(x);
                if (!(z < zb[at])) return;
                double ob[3]; for (int e = 0; e < 3; ++e) ob[e] = w[0] * q[0]->b[e] + w[1] * q[1]->b[e] + w[2] * q[2]->b[e];
                if (mt.alpha_mode) {
                    double al = mt.base[3];
                    float tc[4] = {1, 1, 1, 1};
                    if (mt.tex >= 0) { double u = 0, v = 0; for (int e = 0; e < 3; ++e) { u += ob[e] * sc.v[t * 3 + static_cast<std::size_t>(e)].u; v += ob[e] * sc.v[t * 3 + static_cast<std::size_t>(e)].v; } sc.texs[static_cast<std::size_t>(mt.tex)].sample(u, v, tc); }
                    double vc = 0; for (int e = 0; e < 3; ++e) vc += ob[e] * sc.v[t * 3 + static_cast<std::size_t>(e)].c[3];
                    al *= tc[3] * vc;
                    if (mt.alpha_mode == 1 ? al < mt.cutoff : al < hash01(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(t))) return;
                }
                zb[at] = static_cast<float>(z); tb[at] = static_cast<int32_t>(t); b1[at] = static_cast<float>(ob[1]); b2[at] = static_cast<float>(ob[2]);
            });
        }
    }
    // shade and resolve
    std::vector<double> acc(static_cast<std::size_t>(tg.W) * static_cast<std::size_t>(tg.H) * 4, 0.0);
    const V3 tolight = normalize(L.dir) * -1.0;
    for (int y = 0; y < BH; ++y) for (int x = 0; x < BW; ++x) {
        const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(BW) + static_cast<std::size_t>(x);
        const int32_t t = tb[at]; if (t < 0) continue;
        const std::size_t T3 = static_cast<std::size_t>(t) * 3; const Vtx* vv = &sc.v[T3];
        const double w1 = b1[at], w2 = b2[at], w0 = 1.0 - w1 - w2;
        const Mat& mt = sc.mats[static_cast<std::size_t>(sc.tri_mat[static_cast<std::size_t>(t)])];
        const V3 p = P[T3] * w0 + P[T3 + 1] * w1 + P[T3 + 2] * w2;
        V3 n = sc.tri_flat[static_cast<std::size_t>(t)] ? normalize(cross(P[T3 + 1] - P[T3], P[T3 + 2] - P[T3])) : normalize(N[T3] * w0 + N[T3 + 1] * w1 + N[T3 + 2] * w2);
        const V3 vd = cam.persp ? normalize(p - cam.pos) : cam.fwd;
        if (dot(n, vd) > 0) n = n * -1.0;   // backfaces shade with the flipped normal, like Cycles
        const double u = w0 * vv[0].u + w1 * vv[1].u + w2 * vv[2].u, v = w0 * vv[0].v + w1 * vv[1].v + w2 * vv[2].v;
        float tc[4] = {1, 1, 1, 1}; if (mt.tex >= 0) sc.texs[static_cast<std::size_t>(mt.tex)].sample(u, v, tc);
        double base[3]; for (int e = 0; e < 3; ++e) base[e] = mt.base[e] * tc[e] * (w0 * vv[0].c[e] + w1 * vv[1].c[e] + w2 * vv[2].c[e]);
        double col[3];
        if (mt.unlit) for (int e = 0; e < 3; ++e) col[e] = base[e];
        else {
            const double ndl = std::max(0.0, dot(n, tolight));
            double vis = 1.0;
            if (ndl > 0 && sm && L.cast) vis = sm->vis(p, n, L.half_angle, hash01(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 7u) * 6.2831853);
            const double k = L.ambient + L.sun / PI_ * ndl * vis;
            for (int e = 0; e < 3; ++e) col[e] = base[e] * k;
        }
        float et[4] = {1, 1, 1, 1}; if (mt.emis_tex >= 0) sc.texs[static_cast<std::size_t>(mt.emis_tex)].sample(u, v, et);
        for (int e = 0; e < 3; ++e) col[e] += mt.emis[e] * et[e];
        double* a = &acc[(static_cast<std::size_t>(y / tg.ss) * static_cast<std::size_t>(tg.W) + static_cast<std::size_t>(x / tg.ss)) * 4];
        a[0] += col[0]; a[1] += col[1]; a[2] += col[2]; a[3] += 1.0;
    }
    img::Image out(tg.W, tg.H);
    const double nss = static_cast<double>(tg.ss) * tg.ss;
    for (std::size_t k = 0; k < static_cast<std::size_t>(tg.W) * static_cast<std::size_t>(tg.H); ++k) {
        const double* a = &acc[k * 4]; uint8_t* o = &out.px[k * 4];
        const double cov = a[3] / nss;
        if (tg.opaque) { for (int e = 0; e < 3; ++e) o[e] = to_srgb8(a[e] / nss + tg.bg[e] * (1.0 - cov)); o[3] = 255; }
        else if (a[3] > 0) { for (int e = 0; e < 3; ++e) o[e] = to_srgb8(a[e] / a[3]); o[3] = static_cast<uint8_t>(std::lround(std::clamp(cov, 0.0, 1.0) * 255.0)); }
    }
    return out;
}

// shadow-only pass: the shadow the (camera-invisible) model casts on the ground plane z = 0, as black with alpha
inline img::Image render_shadow(const Cam& cam, const Light& L, const ShadowMap& sm, const Target& tg) {
    img::Image out(tg.W, tg.H);
    const double sun_term = L.sun / PI_ * std::max(0.0, -normalize(L.dir).z);
    if (!(sun_term > 0) || !sm.ok()) return out;
    const double k = sun_term / (sun_term + L.ambient);
    const int BW = tg.W * tg.ss, BH = tg.H * tg.ss; const V3 up = v3(0, 0, 1);
    std::vector<double> acc(static_cast<std::size_t>(tg.W) * static_cast<std::size_t>(tg.H), 0.0);
    for (int y = 0; y < BH; ++y) for (int x = 0; x < BW; ++x) {
        const double sx = x + 0.5, sy = y + 0.5;
        const V3 o = cam.ray_origin(sx, sy), d = cam.ray_dir(sx, sy);
        if (!(d.z < -1e-9)) continue;
        const double t = -o.z / d.z; if (t <= 0) continue;
        const V3 g = o + d * t;
        const double vis = sm.vis(g, up, L.half_angle, hash01(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 11u) * 6.2831853);
        acc[static_cast<std::size_t>(y / tg.ss) * static_cast<std::size_t>(tg.W) + static_cast<std::size_t>(x / tg.ss)] += (1.0 - vis) * k;
    }
    const double nss = static_cast<double>(tg.ss) * tg.ss;
    for (std::size_t q = 0; q < acc.size(); ++q) out.px[q * 4 + 3] = static_cast<uint8_t>(std::lround(std::clamp(acc[q] / nss, 0.0, 1.0) * 255.0));
    return out;
}

// run jobs on worker threads; the first error is rethrown on the calling thread
inline void parallel_jobs(std::size_t n, int threads, const std::function<void(std::size_t)>& job) {
    std::atomic<std::size_t> next{0}; std::exception_ptr err; std::mutex mu; std::atomic<bool> stop{false};
    auto worker = [&] {
        for (;;) {
            if (stop) return; const std::size_t k = next++; if (k >= n) return;
            try { job(k); } catch (...) { std::lock_guard<std::mutex> g(mu); if (!err) err = std::current_exception(); stop = true; return; }
        }
    };
    threads = std::max(1, std::min<int>(threads, static_cast<int>(std::min<std::size_t>(n, 64))));
    if (threads == 1) worker();
    else { std::vector<std::thread> ts; for (int k = 0; k < threads; ++k) ts.emplace_back(worker); for (auto& t : ts) t.join(); }
    if (err) std::rethrow_exception(err);
}
inline int default_threads(std::size_t bytes_per_job) {
    if (!env("UM_RENDER_THREADS").empty()) { const auto v = py_int(env("UM_RENDER_THREADS")); if (v && *v > 0) return static_cast<int>(std::min(64LL, *v)); }
    const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const int by_mem = static_cast<int>(std::max<std::size_t>(1, (768ull << 20) / std::max<std::size_t>(bytes_per_job, 1)));
    return std::max(1, std::min({hw, by_mem, 16}));
}
}  // namespace packtools::modder::r3d

// ------------------------------------------------------------------ um render3d
namespace packtools::modder::render3d {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::r3d;

struct Preset { const char* name; bool ortho; double elevation_deg; long long headings; double start_deg; bool clockwise; double fov_deg; };
inline const std::vector<Preset>& presets() {
    static const std::vector<Preset> p = {
        {"aoe2", true, 30, 16, 0, true, 35}, {"iso8", true, 30, 8, 0, true, 35}, {"trueiso", true, 35.264, 8, 0, true, 35},
        {"topdown", true, 89.9, 8, 0, true, 35}, {"side", true, 0.01, 2, 0, true, 35}, {"turntable", false, 20, 24, 0, false, 30},
    };
    return p;
}
struct Motion { std::string kind; std::map<std::string, double> v; double g(const char* k, double d) const { const auto it = v.find(k); return it == v.end() ? d : it->second; } };
inline const std::vector<std::pair<std::string, std::optional<Motion>>>& motions() {
    static const std::vector<std::pair<std::string, std::optional<Motion>>> m = {
        {"bob", Motion{"bob", {{"amp", 1.0}, {"cycles", 1}}}}, {"walk", Motion{"bob", {{"amp", 0.8}, {"pitch", 0.8}, {"cycles", 2}}}},
        {"lunge", Motion{"lunge", {{"dist", 12}, {"pitch", -3}}}}, {"die", Motion{"die", {{"roll", 80}, {"sink", 3}}}}, {"wreck", Motion{"wreck", {{"roll", 80}, {"sink", 3}}}},
        {"spin", Motion{"spin", {{"turns", 1}}}}, {"still", std::nullopt},
    };
    return m;
}
struct Anim { std::string name; long long frames = 1; std::optional<Motion> motion; };
inline std::vector<Anim> parse_anims(const std::string& spec) {   // 'idle:10:bob,walk:12:walk,death:20:die'
    std::vector<Anim> out;
    for (const auto& part : split(spec, ",")) {
        const auto bits = split(part, ":");
        Anim a; a.name = bits[0];
        if (bits.size() > 1) { const auto v = py_int(strip(bits[1])); if (!v) die("bad frame count in --anims: " + part); a.frames = *v; }
        const std::string mk = bits.size() > 2 ? bits[2] : "still";
        for (const auto& [k, m] : motions()) if (k == mk) a.motion = m;
        bool replaced = false; for (auto& o : out) if (o.name == a.name) { o = a; replaced = true; }
        if (!replaced) out.push_back(a);
    }
    return out;
}
inline void pose(const std::optional<Motion>& mo, double t, double heading, V3& loc, double rot[3]) {   // per-animation transform, t in [0, 1)
    static const Motion none; const Motion& m = mo ? *mo : none;
    loc = v3(0, 0, m.g("hover", 0)); rot[0] = 0; rot[1] = 0; rot[2] = heading;
    if (m.kind == "bob") { loc.z += m.g("amp", 1.5) * std::sin(2 * PI_ * t * m.g("cycles", 1)); rot[1] += rad(m.g("pitch", 0)) * std::sin(2 * PI_ * t * m.g("cycles", 1) + 1); }
    else if (m.kind == "lunge") { const double u = std::pow(std::sin(PI_ * t), 2); loc = loc + v3(std::cos(heading), std::sin(heading), 0) * (m.g("dist", 12) * u); rot[1] += rad(m.g("pitch", -4)) * u; loc.z += m.g("hop", 0) * u; }
    else if (m.kind == "die") { const double u = std::min(1.0, t * m.g("speed", 1.4)); rot[0] += rad(m.g("roll", 70)) * u; loc.z -= m.g("hover", 0) * u + m.g("sink", 2) * u; }
    else if (m.kind == "wreck") { rot[0] += rad(m.g("roll", 70)); loc.z = -m.g("sink", 2); }
    else if (m.kind == "spin") rot[2] += 2 * PI_ * t * m.g("turns", 1);
}

struct Opts {
    std::string glb, out, preset = "aoe2", canvas = "200", anims = "idle:1", engine = "cycles", measure = "length";
    double length = 80, forward_yaw = 0, sun = 4.0, ambient = 0.9, sun_elev = 40, sun_azim = 40, softness = 8;
    long long samples = 40; bool shadows = false;
    std::optional<double> elevation, ground_y; std::optional<long long> headings;
};

inline std::string render(const Opts& o) {
    const std::string out = u8native(fs::absolute(P(o.out))); makedirs(out);
    const Preset* pr = nullptr; for (const auto& p : presets()) if (o.preset == p.name) pr = &p;
    if (!pr) die("unknown preset " + o.preset);
    Preset cam = *pr; if (o.elevation) cam.elevation_deg = *o.elevation; if (o.headings) cam.headings = *o.headings;
    if (cam.headings < 1) die("--headings must be at least 1");
    const auto [CW, CH] = parse_size(o.canvas);
    if (CW < 1 || CH < 1 || CW > 8192 || CH > 8192) die("--canvas must be between 1 and 8192 px");
    std::optional<double> ground_y = o.ground_y; if (!ground_y && o.preset == "side") ground_y = static_cast<double>(CH) * 0.92;   // side view: stand on the bottom of the frame
    const std::vector<Anim> anims = parse_anims(o.anims);
    for (const auto& a : anims) if (a.frames < 1) die("each animation needs at least 1 frame: " + a.name);
    int ss = std::clamp(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(std::max(1LL, o.samples))))), 1, 8);
    while (ss > 1 && static_cast<double>(CW) * CH * ss * ss > 16e6) --ss;

    Scene sc = load_glb(o.glb, out);
    // ---- model: centre on its ground point, longest horizontal side = length_px units (1 unit = 1 px at 1x)
    V3 lo = v3(1e300, 1e300, 1e300), hi = v3(-1e300, -1e300, -1e300);
    for (const auto& p : sc.box_pts) { lo = v3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)); hi = v3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)); }
    const double size = o.measure == "length" ? std::max(hi.x - lo.x, hi.y - lo.y) : hi.z - lo.z;
    const double s = o.length / std::max(size, 1e-6);
    const M4 root = translate(v3(-(lo.x + hi.x) / 2 * s, -(lo.y + hi.y) / 2 * s, -lo.z * s)) * scale3(v3(s, s, s));
    const M4 fwd = rot_z(rad(o.forward_yaw));
    const double height = (hi.z - lo.z) * s;

    // ---- camera (render_sprites.py: location (0, -d cos e, d sin e), rotation (90 deg - e, 0, 0))
    const double elev = rad(cam.elevation_deg); const int S_ = ss; const double maxdim = static_cast<double>(std::max(CW, CH));
    Cam c; c.persp = !cam.ortho; c.right = v3(1, 0, 0); c.up = v3(0, std::sin(elev), std::cos(elev)); c.fwd = v3(0, std::cos(elev), -std::sin(elev));
    double dist = 2000;
    if (cam.ortho) { c.f = S_; c.cx = CW * S_ / 2.0; c.cy = CH * S_ / 2.0; if (ground_y) c.cy = *ground_y * S_; }
    else {
        const double fov = rad(cam.fov_deg);
        dist = std::max(o.length, height) * 1.3 / (2 * std::tan(fov / 2));
        c.f = maxdim * S_ / 2.0 / std::tan(fov / 2); c.cx = CW * S_ / 2.0; c.cy = CH * S_ / 2.0; c.near_ = dist * 1e-4;
    }
    c.pos = v3(0, -dist * std::cos(elev), dist * std::sin(elev));
    if (c.persp) c.pos.z += height / 2;   // aimed at the model's middle; the view direction is unchanged

    Light L; const double a = rad(90 - o.sun_elev), az = rad(o.sun_azim);
    L.dir = v3(-std::sin(a) * std::sin(az), std::sin(a) * std::cos(az), -std::cos(a)); L.sun = o.sun; L.ambient = o.ambient; L.half_angle = rad(o.softness / 2);

    struct Job { std::size_t anim; long long k, f; };
    std::vector<Job> jobs;
    for (std::size_t ai = 0; ai < anims.size(); ++ai) for (long long k = 0; k < cam.headings; ++k) for (long long f = 0; f < anims[ai].frames; ++f) jobs.push_back({ai, k, f});
    const long long n_images = static_cast<long long>(jobs.size()) * (o.shadows ? 2 : 1);
    const std::size_t per_job = static_cast<std::size_t>(CW * CH) * static_cast<std::size_t>(ss * ss) * 16 + (16u << 20) + sc.v.size() * 48;
    const int threads = default_threads(per_job);
    print("rendering " + std::to_string(n_images) + " images with the built-in renderer (" + std::to_string(ss) + "x" + std::to_string(ss) + " samples per pixel, " + std::to_string(threads) + (threads == 1 ? " thread" : " threads") + ") -> " + out);
    const double start = rad(cam.start_deg), sign = cam.clockwise ? -1.0 : 1.0;
    Target tg; tg.W = static_cast<int>(CW); tg.H = static_cast<int>(CH); tg.ss = ss;
    parallel_jobs(jobs.size(), threads, [&](std::size_t q) {
        const Job& jb = jobs[q]; const Anim& an = anims[jb.anim];
        const double heading = start + sign * 2 * PI_ * static_cast<double>(jb.k) / static_cast<double>(cam.headings);
        V3 loc; double rot[3]; pose(an.motion, static_cast<double>(jb.f) / static_cast<double>(an.frames), heading, loc, rot);
        const M4 M = translate(loc) * euler_xyz(rot[0], rot[1], rot[2]) * fwd * root;
        const M4 NM = transpose_inv3(M);
        std::vector<V3> Pp(sc.v.size()), Np(sc.v.size());
        for (std::size_t k = 0; k < sc.v.size(); ++k) { Pp[k] = xpoint(M, sc.v[k].p); Np[k] = normalize(xdir(NM, sc.v[k].n)); }
        ShadowMap sm; if (L.sun > 0) sm.build(Pp, L, 2048);
        char base[64]; std::snprintf(base, sizeof base, "_%02lld_%03lld", jb.k, jb.f);
        img::save(render_main(sc, Pp, Np, c, L, &sm, tg), path_join(out, an.name + base + ".png"));
        if (o.shadows) img::save(render_shadow(c, L, sm, tg), path_join(out, an.name + base + "_s.png"));
    });
    print("rendered " + std::to_string(n_images) + " images -> " + out);
    return out;
}

inline const char* DOC =
    "Render a 3D model (GLB) into sprite frames from a game's camera.\n\n"
    "    um render3d model.glb out/ --preset aoe2 --length 80 --anims idle:10:bob,walk:12:bob,attack:16:lunge,death:20:die --shadows\n"
    "    um render3d model.glb out/ --preset side --canvas 128 --length 110            # platformer: facing right + left\n"
    "    um render3d model.glb out/ --preset iso8 --canvas 256x256 --length 150\n\n"
    "This is how the AoE2 San Franciscans units were made: fal concept art -> `um fal model3d` (image-to-3D)\n"
    "-> these renders -> the engine's sprite format. Rendering from 3D keeps every facing and frame consistent,\n"
    "which per-frame image generation can't. Presets (camera elevation, headings, direction order):\n"
    "  aoe2      ortho 30 deg, 16 headings clockwise from east (AoE2 DE / Genie unit order)\n"
    "  iso8      ortho 30 deg (2:1 diamond tiles), 8 headings clockwise from east\n"
    "  trueiso   ortho 35.264 deg, 8 headings\n"
    "  topdown   ortho 90 deg, 8 headings (twin-stick / top-down shooters)\n"
    "  side      ortho 0 deg, 2 headings: facing right, facing left (platformers, Terraria-style)\n"
    "  turntable persp 20 deg, 24 headings (icons, previews, promo spins)\n"
    "A.S.T.R.O AI's C++ port renders with a built-in software rasteriser: no Blender, no Python. Sun with soft\n"
    "shadows, uniform ambient light, supersampled edges on a transparent background; no bounce light, so it\n"
    "looks flatter than Cycles. Reads GLB/glTF 2.0 (textures: PNG built in; JPEG/WebP need ffmpeg; Draco/meshopt\n"
    "compressed meshes must be decompressed first). Frames: <out>/<anim>_<heading>_<frame>.png (+ _s shadow pass).\n"
    "Pack them with `um sprite sheet`, or with an engine-specific writer.\n";

inline void reg(Parser& sub) {
    Parser& p = sub.sub("render3d", DOC); p.help_line = "render a GLB into sprite frames from a game's camera (built-in renderer)"; p.raw_description = true;
    p.pos("glb"); p.pos("out");
    std::vector<std::string> names; for (const auto& x : presets()) names.push_back(x.name);
    p.opt({"--preset"}).def("aoe2").choices(names);
    p.opt({"--canvas"}).def("200").help("frame size px: N or WxH");
    p.opt({"--length"}).number().def_float(80).help("model's longest horizontal side (or height with --measure height) in px");
    p.opt({"--measure"}).def("length").choices({"length", "height"});
    p.opt({"--forward-yaw"}).number().def_float(0).help("degrees to turn the model so its front faces +X (east)");
    std::string ms; for (const auto& [k, m] : motions()) ms += (ms.empty() ? "" : ",") + k;
    p.opt({"--anims"}).def("idle:1").help("name:frames:motion,... motions: " + ms);
    p.opt({"--shadows"}).store_true().help("also render a shadow-only pass (_s.png)");
    p.opt({"--samples"}).integer().def(40LL).help("anti-aliasing: about this many samples per pixel (supersampling, up to 8x8)");
    p.opt({"--engine"}).def("cycles").choices({"cycles", "eevee"}).help("kept for compatibility: both use the built-in renderer");
    p.opt({"--elevation"}).number().help("override camera elevation (deg)");
    p.opt({"--headings"}).integer().help("override number of facings");
    p.opt({"--sun"}).number().def_float(4.0);
    p.opt({"--ambient"}).number().def_float(0.9);
    p.opt({"--sun-elev"}).number().def_float(40).help("sun elevation (deg)");
    p.opt({"--sun-azim"}).number().def_float(40).help("sun azimuth (deg)");
    p.opt({"--softness"}).number().def_float(8).help("sun angular size (deg): bigger = softer shadow edges");
    p.opt({"--ground-y"}).number().help("px from the top where the ground point lands (default: canvas centre)");
}
inline int main_(const Val& a) {
    Opts o; o.glb = S(a, "glb"); o.out = S(a, "out"); o.preset = S(a, "preset"); o.canvas = S(a, "canvas"); o.anims = S(a, "anims"); o.engine = S(a, "engine"); o.measure = S(a, "measure");
    o.length = get(a, "length").num(); o.forward_yaw = get(a, "forward_yaw").num(); o.sun = get(a, "sun").num(); o.ambient = get(a, "ambient").num();
    o.sun_elev = get(a, "sun_elev").num(); o.sun_azim = get(a, "sun_azim").num(); o.softness = get(a, "softness").num();
    o.samples = get(a, "samples").i; o.shadows = truthy(get(a, "shadows"));
    if (!get(a, "elevation").is_none()) o.elevation = get(a, "elevation").num();
    if (!get(a, "ground_y").is_none()) o.ground_y = get(a, "ground_y").num();
    if (!get(a, "headings").is_none()) o.headings = get(a, "headings").i;
    render(o);
    return 0;
}
}  // namespace packtools::modder::render3d
