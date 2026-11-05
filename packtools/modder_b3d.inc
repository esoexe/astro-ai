// modder_b3d.inc - `um b3d`: C++ ports of universal-modder's skills/blitz3d-decomp/scripts/roblox/ (Rehan and
// universal-modder contributors, MIT), the animated .b3d viewmodel -> Roblox workflow:
//   um b3d dump   MODEL.b3d OUT.json                       b3d_skin.py: node tree, skin weights, keys, SEQS -> JSON
//   um b3d anim   IN.json OUT.luau --scale S --name NAME    vm_anim_data.py: keyframes as a Luau data module
//   um b3d build  IN.json OUT_DIR --scale S [--tex B=P]     vm_build.py: a skinned, rigged model for Roblox
//   um b3d render IN.json OUT_DIR HFOV FRAME...             vm_render.py: frames from the viewmodel camera
// dump and anim give the same output as the Python originals. build and render needed Blender (Python inside it)
// upstream; here they are C++: build writes a skinned glTF binary (NAME.glb, bind pose; Roblox's 3D importer reads
// glTF as well as FBX) plus NAME.preview.glb with the keyframes as an animation, instead of NAME.fbx + NAME.blend;
// render poses the skin from the keys itself and draws it with the built-in rasteriser (modder_render3d.inc).
namespace packtools::modder::b3d {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::r3d;

// ------------------------------------------------------------------ reader (b3d_skin.py)
struct Reader {
    const std::string* data = nullptr; std::size_t pos = 0, end = 0;
    std::size_t left() const { return end > pos ? end - pos : 0; }
    void need(std::size_t n) const { if (pos + n > data->size()) die("b3d: unexpected end of file at offset " + std::to_string(pos)); }
    long long i32() { need(4); const uint32_t v = Gltf::le32(*data, pos); pos += 4; return static_cast<int32_t>(v); }
    double f32() { need(4); const uint32_t b = Gltf::le32(*data, pos); float f; std::memcpy(&f, &b, 4); pos += 4; return f; }
    Val f32s(std::size_t n) { Val l = Val::list(); for (std::size_t k = 0; k < n; ++k) l.l->push_back(Val::flt(f32())); return l; }
    std::string cstr() {
        const std::size_t z = data->find('\0', pos); if (z == std::string::npos) die("b3d: unterminated string at offset " + std::to_string(pos));
        std::string s; for (std::size_t k = pos; k < z; ++k) { const unsigned char c = static_cast<unsigned char>((*data)[k]); if (c < 0x80) s += static_cast<char>(c); else { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); } }   // latin-1
        pos = z + 1; return s;
    }
    template <class F> void chunks(F&& f) {
        while (left() >= 8) {
            std::string tag; for (int k = 0; k < 4; ++k) { const unsigned char c = static_cast<unsigned char>((*data)[pos + static_cast<std::size_t>(k)]); if (c < 0x80) tag += static_cast<char>(c); else { tag += static_cast<char>(0xC0 | (c >> 6)); tag += static_cast<char>(0x80 | (c & 0x3F)); } }
            const long long size = static_cast<int32_t>(Gltf::le32(*data, pos + 4));
            const std::size_t start = pos + 8; const long long nxt = static_cast<long long>(start) + size;
            if (size < 0) die("b3d: negative chunk size in " + tag);
            pos = static_cast<std::size_t>(nxt);
            Reader r; r.data = data; r.pos = start; r.end = std::min<std::size_t>(static_cast<std::size_t>(nxt), data->size());
            f(tag, r);
        }
    }
};
inline Val ints(std::initializer_list<long long> v) { Val l = Val::list(); for (long long x : v) l.l->push_back(Val::integer(x)); return l; }
inline Val read_texs(Reader& r) {
    Val out = Val::list();
    while (r.left() > 0) {
        Val t = Val::dict(); t.d->set("file", Val::str(r.cstr()));
        const long long flags = r.i32(), blend = r.i32(); t.d->set("flags", Val::integer(flags)); t.d->set("blend", Val::integer(blend));
        t.d->set("pos", r.f32s(2)); t.d->set("scale", r.f32s(2)); t.d->set("rot", Val::flt(r.f32()));
        out.l->push_back(t);
    }
    return out;
}
inline Val read_brus(Reader& r) {
    const long long n = r.i32(); Val out = Val::list();
    while (r.left() > 0) {
        Val b = Val::dict(); b.d->set("name", Val::str(r.cstr())); b.d->set("rgba", r.f32s(4)); b.d->set("shininess", Val::flt(r.f32()));
        const long long blend = r.i32(), fx = r.i32(); b.d->set("blend", Val::integer(blend)); b.d->set("fx", Val::integer(fx));
        Val tx = Val::list(); for (long long k = 0; k < std::max(0LL, n); ++k) tx.l->push_back(Val::integer(r.i32()));
        b.d->set("textures", tx); out.l->push_back(b);
    }
    return out;
}
inline Val read_mesh(Reader& r) {
    Val mesh = Val::dict(); mesh.d->set("brush", Val::integer(r.i32())); mesh.d->set("verts", Val()); mesh.d->set("surfaces", Val::list());
    r.chunks([&](const std::string& tag, Reader& c) {
        if (tag == "VRTS") {
            const long long flags = c.i32(), sets = c.i32(), size = c.i32();
            Val pos = Val::list(), nrm = Val::list(), col = Val::list(), uv = Val::list();
            while (c.left() > 0) {
                pos.l->push_back(c.f32s(3));
                if (flags & 1) nrm.l->push_back(c.f32s(3));
                if (flags & 2) col.l->push_back(c.f32s(4));
                Val first; bool have = false;
                for (long long s = 0; s < sets; ++s) { Val coords = size > 1 ? c.f32s(static_cast<std::size_t>(size)) : c.f32s(1); if (!have) { first = coords; have = true; } }
                if (have) uv.l->push_back(first); else { Val z = Val::list(); z.l->push_back(Val::flt(0.0)); z.l->push_back(Val::flt(0.0)); uv.l->push_back(z); }
            }
            Val v = Val::dict(); v.d->set("pos", pos); v.d->set("normal", nrm); v.d->set("color", col); v.d->set("uv", uv);
            mesh.d->set("verts", v);
        } else if (tag == "TRIS") {
            const long long brush = c.i32(); Val tris = Val::list();
            while (c.left() > 0) { const long long a = c.i32(), b = c.i32(), d = c.i32(); tris.l->push_back(ints({a, b, d})); }
            Val s = Val::dict(); s.d->set("brush", Val::integer(brush)); s.d->set("tris", tris); mesh.d->get("surfaces")->l->push_back(s);
        }
    });
    return mesh;
}
inline Val read_node(Reader& r) {
    Val node = Val::dict(); node.d->set("name", Val::str(r.cstr())); node.d->set("pos", r.f32s(3)); node.d->set("scale", r.f32s(3)); node.d->set("rot", r.f32s(4)); node.d->set("children", Val::list());
    Val keys;   // dict str(frame) -> slot, while reading
    r.chunks([&](const std::string& tag, Reader& c) {
        if (tag == "MESH") node.d->set("mesh", read_mesh(c));
        else if (tag == "BONE") { Val w = Val::list(); while (c.left() > 0) { Val e = Val::list(); e.l->push_back(Val::integer(c.i32())); e.l->push_back(Val::flt(c.f32())); w.l->push_back(e); } node.d->set("bone", w); }
        else if (tag == "KEYS") {
            const long long flags = c.i32();
            if (!node.d->has("keys")) { keys = Val::dict(); node.d->set("keys", Val()); }
            while (c.left() > 0) {
                Val k = Val::dict(); const long long fr = c.i32(); k.d->set("frame", Val::integer(fr));
                if (flags & 1) k.d->set("pos", c.f32s(3));
                if (flags & 2) k.d->set("scale", c.f32s(3));
                if (flags & 4) k.d->set("rot", c.f32s(4));
                const std::string key = std::to_string(fr);
                if (!keys.d->has(key)) { Val slot = Val::dict(); slot.d->set("frame", Val::integer(fr)); keys.d->set(key, slot); }
                Val* slot = keys.d->get(key); for (const auto& [kk, vv] : k.d->items) slot->d->set(kk, vv);
            }
        } else if (tag == "ANIM") {
            const long long flags = c.i32(), frames = c.i32(); Val a = Val::dict(); a.d->set("flags", Val::integer(flags)); a.d->set("frames", Val::integer(frames)); a.d->set("fps", Val::flt(c.f32())); node.d->set("anim", a);
        } else if (tag == "SEQS") {
            Val s = Val::dict(); s.d->set("name", Val::str(c.cstr()));
            std::vector<long long> vals; const std::size_t n = c.left() / 4; for (std::size_t k = 0; k < n; ++k) vals.push_back(c.i32());
            if (vals.size() < 2) die("b3d: SEQS chunk with fewer than two frame numbers");
            s.d->set("first", Val::integer(vals[0])); s.d->set("last", Val::integer(vals[1]));
            Val rest = Val::list(); for (std::size_t k = 2; k < vals.size(); ++k) rest.l->push_back(Val::integer(vals[k])); s.d->set("rest", rest);
            if (!node.d->has("seqs")) node.d->set("seqs", Val::list());
            node.d->get("seqs")->l->push_back(s);
        } else if (tag == "NODE") node.d->get("children")->l->push_back(read_node(c));
        else { if (!node.d->has("unknown")) node.d->set("unknown", Val::list()); node.d->get("unknown")->l->push_back(Val::str(tag)); }
    });
    if (node.d->has("keys")) {
        std::vector<Val> ks; for (const auto& [k, v] : keys.d->items) ks.push_back(v);
        std::stable_sort(ks.begin(), ks.end(), [](const Val& a, const Val& b) { return get(a, "frame").i < get(b, "frame").i; });
        node.d->set("keys", Val::list(ks));
    }
    return node;
}
inline std::string tuple_repr(const std::vector<std::string>& parts) { std::string o = "("; for (std::size_t k = 0; k < parts.size(); ++k) { if (k) o += ", "; o += parts[k]; } return o + (parts.size() == 1 ? ",)" : ")"); }
inline void walk(const Val& n, int d) {
    std::vector<std::string> extra;
    if (has(n, "mesh")) {
        const Val m = get(n, "mesh"); std::string s = "[";
        bool first = true; for (const auto& sf : *get(m, "surfaces").l) { if (!first) s += ", "; first = false; s += tuple_repr({std::to_string(get(sf, "brush").i), std::to_string(get(sf, "tris").size())}); }
        extra.push_back("MESH " + std::to_string(get(get(m, "verts"), "pos").size()) + " verts, surfaces " + s + "]");
    }
    if (has(n, "bone")) extra.push_back("bone " + std::to_string(get(n, "bone").size()));
    if (has(n, "keys")) extra.push_back("keys " + std::to_string(get(n, "keys").size()));
    if (has(n, "anim")) extra.push_back("anim " + repr(get(n, "anim")));
    if (has(n, "seqs")) { std::string s = "["; bool first = true; for (const auto& q : *get(n, "seqs").l) { if (!first) s += ", "; first = false; s += tuple_repr({repr(get(q, "name")), std::to_string(get(q, "first").i), std::to_string(get(q, "last").i)}); } extra.push_back("seqs " + s + "]"); }
    std::string line(static_cast<std::size_t>(d) * 2, ' '); line += get(n, "name").s; line += " ";
    for (std::size_t k = 0; k < extra.size(); ++k) { if (k) line += " | "; line += extra[k]; }
    print(line);
    for (const auto& c : *get(n, "children").l) walk(c, d + 1);
}
inline void dump(const std::string& src, const std::string& dst) {
    if (!isfile(src)) die("no such file: " + src);
    const std::string data = slurp(src);
    Reader top; top.data = &data; top.pos = 0; top.end = data.size();
    Val out = Val::dict(); out.d->set("texs", Val::list()); out.d->set("brus", Val::list()); out.d->set("root", Val());
    top.chunks([&](const std::string& tag, Reader& r) {
        if (tag != "BB3D") die("not a Blitz3D file: top chunk " + repr(Val::str(tag)) + " (expected 'BB3D')");
        out.d->set("version", Val::integer(r.i32()));
        r.chunks([&](const std::string& t, Reader& c) {
            if (t == "TEXS") out.d->set("texs", read_texs(c));
            else if (t == "BRUS") out.d->set("brus", read_brus(c));
            else if (t == "NODE") out.d->set("root", read_node(c));
        });
    });
    spit(dst, dumps(out));
    std::string tx = "["; bool first = true; for (const auto& t : *get(out, "texs").l) { if (!first) tx += ", "; first = false; tx += repr(get(t, "file")); } print("texs " + tx + "]");
    std::string br = "["; first = true;
    for (const auto& b : *get(out, "brus").l) { if (!first) br += ", "; first = false; br += tuple_repr({repr(get(b, "name")), repr(get(b, "textures")), std::to_string(get(b, "blend").i), std::to_string(get(b, "fx").i)}); }
    print("brus " + br + "]");
    if (get(out, "root").is_none()) die("the file has no root NODE");
    walk(get(out, "root"), 0);
}

// ------------------------------------------------------------------ shared: nodes, names, spaces
struct NodeRef { Val n; std::string uname; int parent = -1; };   // parent: index into the list, -1 for the root
inline std::vector<NodeRef> collect(const Val& root) {   // depth-first, unique names (the b3d can have two Scene_Root nodes)
    std::vector<NodeRef> out; std::map<std::string, long long> seen;
    std::function<void(const Val&, int)> rec = [&](const Val& n, int parent) {
        std::string name = get(n, "name").s;
        const auto it = seen.find(name);
        if (it != seen.end()) { it->second += 1; name = name + "_" + std::to_string(it->second); } else seen[name] = 0;
        out.push_back({n, name, parent}); const int me = static_cast<int>(out.size()) - 1;
        for (const auto& c : *get(n, "children", Val::list()).l) rec(c, me);
    };
    rec(root, -1);
    return out;
}
inline std::array<double, 3> vec3(const Val& v) { return {v[0].num(), v[1].num(), v[2].num()}; }
inline std::array<double, 4> vec4(const Val& v) { return {v[0].num(), v[1].num(), v[2].num(), v[3].num()}; }
// Blitz applies the conjugate of the stored quaternion (w, x, y, z)
inline M4 blitz_local(const std::array<double, 3>& pos, const std::array<double, 4>& rot, double scale) {
    return translate(v3(pos[0] * scale, pos[1] * scale, pos[2] * scale)) * quat(-rot[1], -rot[2], -rot[3], rot[0]);
}
inline M4 mirror_x() { M4 d; d.at(0, 0) = -1; return d; }   // D = diag(-1, 1, 1): Roblox model space = (-x, y, z) of Blitz
inline std::array<double, 4> mat_quat(const M4& m) {   // rotation part -> (x, y, z, w)
    const double tr = m.at(0, 0) + m.at(1, 1) + m.at(2, 2); double x, y, z, w;
    if (tr > 0) { const double s = std::sqrt(tr + 1.0) * 2; w = 0.25 * s; x = (m.at(2, 1) - m.at(1, 2)) / s; y = (m.at(0, 2) - m.at(2, 0)) / s; z = (m.at(1, 0) - m.at(0, 1)) / s; }
    else if (m.at(0, 0) > m.at(1, 1) && m.at(0, 0) > m.at(2, 2)) { const double s = std::sqrt(1.0 + m.at(0, 0) - m.at(1, 1) - m.at(2, 2)) * 2; w = (m.at(2, 1) - m.at(1, 2)) / s; x = 0.25 * s; y = (m.at(0, 1) + m.at(1, 0)) / s; z = (m.at(0, 2) + m.at(2, 0)) / s; }
    else if (m.at(1, 1) > m.at(2, 2)) { const double s = std::sqrt(1.0 + m.at(1, 1) - m.at(0, 0) - m.at(2, 2)) * 2; w = (m.at(0, 2) - m.at(2, 0)) / s; x = (m.at(0, 1) + m.at(1, 0)) / s; y = 0.25 * s; z = (m.at(1, 2) + m.at(2, 1)) / s; }
    else { const double s = std::sqrt(1.0 + m.at(2, 2) - m.at(0, 0) - m.at(1, 1)) * 2; w = (m.at(1, 0) - m.at(0, 1)) / s; x = (m.at(0, 2) + m.at(2, 0)) / s; y = (m.at(1, 2) + m.at(2, 1)) / s; z = 0.25 * s; }
    const double n = std::sqrt(x * x + y * y + z * z + w * w); if (w < 0) { x = -x; y = -y; z = -z; w = -w; }
    return {x / n, y / n, z / n, w / n};
}
inline std::pair<long long, long long> seq_range(const Val& root, bool required, const std::string& tool) {
    const Val seqs = get(root, "seqs", Val::list());
    if (!seqs.size()) { if (required) die(tool + ": the root node has no seqs: add one to the JSON first, e.g. \"seqs\": [{\"name\": \"all\", \"first\": <first keyed frame>, \"last\": <last>}]"); return {1, 1}; }
    long long a = get(seqs[0], "first").i, b = get(seqs[0], "last").i;
    for (const auto& s : *seqs.l) { a = std::min(a, get(s, "first").i); b = std::max(b, get(s, "last").i); }
    return {a, b};
}
inline Val load_json(const std::string& p) { if (!isfile(p)) die("no such file: " + p); Val v = loads(slurp(p)); if (get(v, "root").t != Val::DICT) die(p + ": not a `um b3d dump` JSON (no root node)"); return v; }

// ------------------------------------------------------------------ anim (vm_anim_data.py)
inline void anim(const std::string& src, const std::string& dst, double scale, const std::string& name) {
    const Val data = load_json(src); const Val root = get(data, "root");
    const Val seqs = get(root, "seqs", Val::list());
    const auto [first, last] = seq_range(root, true, "anim");
    const double POS_Q = 16000.0;
    const std::vector<NodeRef> all = collect(root);
    std::string blob; std::vector<std::string> rows;
    using Fr = std::pair<std::array<double, 3>, std::array<double, 4>>;   // p, q (w, x, y, z)
    auto convert = [&](const std::array<double, 3>& pos, const std::array<double, 4>& rot) -> Fr {
        // Blitz applies conj(q) = (w, -x, -y, -z); conjugating by D = diag(-1, 1, 1) maps (w, x, y, z) to (w, x, -y, -z)
        std::array<double, 4> q = {rot[0], -rot[1], rot[2], rot[3]};
        const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]); for (double& c : q) c /= n;
        if (q[0] < 0) for (double& c : q) c = -c;
        return {{-pos[0] * scale, pos[1] * scale, pos[2] * scale}, q};
    };
    // python: index = len(nodes) + 1 for non-root nodes (1-based in the module), parent index as given by the walk
    std::vector<int> module_index(all.size(), 0); int next = 1; for (std::size_t k = 1; k < all.size(); ++k) module_index[k] = next++;
    for (std::size_t k = 1; k < all.size(); ++k) {
        const NodeRef& nr = all[k]; const int parent = nr.parent <= 0 ? 0 : module_index[static_cast<std::size_t>(nr.parent)];
        std::map<long long, Val> keys; for (const auto& kv : *get(nr.n, "keys", Val::list()).l) keys[get(kv, "frame").i] = kv;
        std::vector<Fr> frames;
        std::array<double, 3> cp = vec3(get(nr.n, "pos")); std::array<double, 4> cr = vec4(get(nr.n, "rot"));
        for (long long f = first; f <= last; ++f) {
            const auto it = keys.find(f);
            if (it != keys.end()) { if (has(it->second, "pos")) cp = vec3(get(it->second, "pos")); if (has(it->second, "rot")) cr = vec4(get(it->second, "rot")); }
            frames.push_back(convert(cp, cr));
        }
        for (std::size_t i = 1; i < frames.size(); ++i) {   // keep quaternion signs continuous so interpolation takes the short way
            double d = 0; for (int c = 0; c < 4; ++c) d += frames[i].second[static_cast<std::size_t>(c)] * frames[i - 1].second[static_cast<std::size_t>(c)];
            if (d < 0) for (double& c : frames[i].second) c = -c;
        }
        bool is_static = true;
        for (const auto& fr : frames) { double mx = 0; for (int c = 0; c < 3; ++c) mx = std::max(mx, std::fabs(fr.first[static_cast<std::size_t>(c)] - frames[0].first[static_cast<std::size_t>(c)])); for (int c = 0; c < 4; ++c) mx = std::max(mx, std::fabs(fr.second[static_cast<std::size_t>(c)] - frames[0].second[static_cast<std::size_t>(c)])); if (!(mx < 1e-6)) { is_static = false; break; } }
        const std::size_t use = is_static ? std::min<std::size_t>(1, frames.size()) : frames.size();
        for (std::size_t i = 0; i < use; ++i) {
            const auto& [p, q] = frames[i];
            const double vals_d[7] = {p[0] * POS_Q, p[1] * POS_Q, p[2] * POS_Q, q[1] * 32767, q[2] * 32767, q[3] * 32767, q[0] * 32767};
            for (double vd : vals_d) {
                const double r = std::nearbyint(vd);   // round half to even, like Python's round()
                if (r < -32768 || r > 32767) die("anim: value out of int16 range for node " + nr.uname + " (lower --scale)");
                const auto v = static_cast<int16_t>(r); blob += static_cast<char>(static_cast<uint16_t>(v) & 0xFF); blob += static_cast<char>((static_cast<uint16_t>(v) >> 8) & 0xFF);
            }
        }
        const Fr rest = convert(vec3(get(nr.n, "pos")), vec4(get(nr.n, "rot")));
        rows.push_back("\t{ \"" + nr.uname + "\", " + std::to_string(parent) + ", { " + fmt_f(rest.first[0], 6) + ", " + fmt_f(rest.first[1], 6) + ", " + fmt_f(rest.first[2], 6) + " }, { " +
                       fmt_f(rest.second[1], 6) + ", " + fmt_f(rest.second[2], 6) + ", " + fmt_f(rest.second[3], 6) + ", " + fmt_f(rest.second[0], 6) + " }, " + (is_static ? "true" : "false") + " },");
    }
    const Val fps = get(get(root, "anim", Val::dict()), "fps", Val::integer(20));
    std::vector<std::string> lines = {
        "-- GENERATED by vm_anim_data.py from the " + name + " model's keyframes; keep the model's credit and licence with it.",
        "-- Do not edit: re-run the tool. Layout and spaces: see the tool's docstring.",
        "return {", "\tName = " + dumps(Val::str(name)) + ",", "\tScale = " + float_repr(scale) + ",", "\tFps = " + pystr(fps) + ",",
        "\tFirst = " + std::to_string(first) + ",", "\tLast = " + std::to_string(last) + ",", "\tPosQ = " + float_repr(POS_Q) + ",", "\tSeqs = {"};
    for (const auto& s : *seqs.l) lines.push_back("\t\t" + get(s, "name").s + " = { " + std::to_string(get(s, "first").i) + ", " + std::to_string(get(s, "last").i) + " },");
    lines.push_back("\t},"); lines.push_back("\t-- { name, parent (0 = mesh root), rest pos, rest quat {x, y, z, w}, static }"); lines.push_back("\tNodes = {");
    for (const auto& r : rows) lines.push_back("\t" + r);
    lines.push_back("\t},"); lines.push_back("\tBlob = \"" + b64encode(blob) + "\","); lines.push_back("}"); lines.push_back("");
    std::string text; for (std::size_t k = 0; k < lines.size(); ++k) { if (k) text += "\n"; text += lines[k]; }
    spit(dst, text);
    long long stat = 0; for (const auto& r : rows) if (ends(r, "true },")) ++stat;
    long long chars = 0; for (const auto& l : lines) chars += static_cast<long long>(cplen(l)) + 1;
    Val o = Val::dict(); o.d->set("nodes", Val::integer(static_cast<long long>(all.size()) - 1)); o.d->set("static", Val::integer(stat)); o.d->set("bytes", Val::integer(static_cast<long long>(blob.size()))); o.d->set("luau_chars", Val::integer(chars));
    print(dumps(o));
}

// ------------------------------------------------------------------ textures per brush
inline std::map<std::string, std::string> tex_map(const Val& a) {
    std::map<std::string, std::string> m;
    const Val t = get(a, "tex"); if (t.t == Val::LIST) for (const auto& e : *t.l) { const auto k = e.s.find('='); if (k == std::string::npos) die("--tex wants BRUSH=PATH, got " + e.s); m[e.s.substr(0, k)] = e.s.substr(k + 1); }
    return m;
}
// the texture for brush bi: --tex BRUSH=PATH first, else --tex-dir + the file name the b3d's TEXS gives the brush
inline std::string brush_texture(const Val& data, std::size_t bi, const std::map<std::string, std::string>& tex, const std::string& tex_dir, int* tex_flags = nullptr) {
    const Val brus = get(data, "brus", Val::list()); if (bi >= brus.size()) return "";
    const Val b = brus[bi]; const auto it = tex.find(get(b, "name").s);
    const Val texs = get(data, "texs", Val::list()); const Val tl = get(b, "textures", Val::list());
    long long ti = tl.size() ? tl[0].i : -1;
    if (tex_flags) *tex_flags = ti >= 0 && static_cast<std::size_t>(ti) < texs.size() ? static_cast<int>(get(texs[static_cast<std::size_t>(ti)], "flags").i) : 0;
    if (it != tex.end()) return it->second;
    if (tex_dir.empty() || ti < 0 || static_cast<std::size_t>(ti) >= texs.size()) return "";
    std::string f = replace_all(get(texs[static_cast<std::size_t>(ti)], "file").s, "\\", "/"); const auto s = f.rfind('/'); if (s != std::string::npos) f = f.substr(s + 1);
    const std::string p = path_join(tex_dir, f); return isfile(p) ? p : "";
}

// ------------------------------------------------------------------ build (vm_build.py, glTF instead of FBX/.blend)
struct GlbWriter {
    std::string bin; Val views = Val::list(), accs = Val::list();
    static void f32(std::string& s, double v) { const float f = static_cast<float>(v); char b[4]; std::memcpy(b, &f, 4); s.append(b, 4); }
    static void u16(std::string& s, unsigned v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); }
    static void u32(std::string& s, uint32_t v) { for (int k = 0; k < 4; ++k) s += static_cast<char>((v >> (8 * k)) & 0xFF); }
    long long view(const std::string& bytes, long long target = 0) {
        while (bin.size() % 4) bin += '\0';
        Val v = Val::dict(); v.d->set("buffer", Val::integer(0)); v.d->set("byteOffset", Val::integer(static_cast<long long>(bin.size()))); v.d->set("byteLength", Val::integer(static_cast<long long>(bytes.size())));
        if (target) v.d->set("target", Val::integer(target));
        bin += bytes; views.l->push_back(v); return static_cast<long long>(views.size()) - 1;
    }
    long long accessor(const std::string& bytes, long long ctype, long long count, const char* type, long long target = 0, const std::vector<double>* mn = nullptr, const std::vector<double>* mx = nullptr) {
        Val a = Val::dict(); a.d->set("bufferView", Val::integer(view(bytes, target))); a.d->set("componentType", Val::integer(ctype)); a.d->set("count", Val::integer(count)); a.d->set("type", Val::str(type));
        if (mn && mx) { Val l1 = Val::list(), l2 = Val::list(); for (double d : *mn) l1.l->push_back(Val::flt(static_cast<double>(static_cast<float>(d)))); for (double d : *mx) l2.l->push_back(Val::flt(static_cast<double>(static_cast<float>(d)))); a.d->set("min", l1); a.d->set("max", l2); }
        accs.l->push_back(a); return static_cast<long long>(accs.size()) - 1;
    }
    std::string finish(Val j) {
        while (bin.size() % 4) bin += '\0';
        Val b = Val::dict(); b.d->set("byteLength", Val::integer(static_cast<long long>(bin.size()))); j.d->set("buffers", Val::list({b}));
        j.d->set("bufferViews", views); j.d->set("accessors", accs);
        DumpOpts o; o.compact = true; std::string js = dumps(j, o); while (js.size() % 4) js += ' ';
        std::string out = "glTF"; u32(out, 2); u32(out, static_cast<uint32_t>(12 + 8 + js.size() + 8 + bin.size()));
        u32(out, static_cast<uint32_t>(js.size())); u32(out, 0x4E4F534Au); out += js; u32(out, static_cast<uint32_t>(bin.size())); u32(out, 0x004E4942u); out += bin;
        return out;
    }
};
inline Val nums(std::initializer_list<double> v) { Val l = Val::list(); for (double x : v) l.l->push_back(Val::flt(x)); return l; }

inline void build(const Val& a) {
    const std::string src = S(a, "src"), outdir = S(a, "out"); const double scale = get(a, "scale").num();
    const Val data = load_json(src); const Val root = get(data, "root");
    std::string NAME = u8native(P(src).stem());
    const auto tex = tex_map(a); const std::string tex_dir = S(a, "tex_dir");
    const std::vector<NodeRef> all = collect(root);
    const M4 D = mirror_x();
    auto G = [&](const M4& m) { return D * m * D; };
    // rest matrices in model space (D * Blitz * scale); the rig node (glTF node 0) is the identity, like vm_build's armature object
    std::vector<M4> rest_local(all.size()), rest_world(all.size());
    for (std::size_t k = 0; k < all.size(); ++k) {
        rest_local[k] = G(blitz_local(vec3(get(all[k].n, "pos")), vec4(get(all[k].n, "rot")), scale));
        rest_world[k] = all[k].parent >= 0 ? rest_world[static_cast<std::size_t>(all[k].parent)] * rest_local[k] : rest_local[k];
    }
    if (!has(root, "mesh")) die("build: the root node has no mesh");
    const Val mesh = get(root, "mesh"); const Val V = get(mesh, "verts");
    if (V.t != Val::DICT) die("build: the root mesh has no vertices");
    const Val vpos = get(V, "pos"), vnrm = get(V, "normal"), vuv = get(V, "uv");
    // glTF nodes: 0 = rig (identity, joint 0 for unweighted vertices), then one per b3d node (the root b3d node is a plain
    // node, every other node is a joint), then the mesh nodes
    Val nodes = Val::list();
    { Val rig = Val::dict(); rig.d->set("name", Val::str(NAME + "_rig")); rig.d->set("children", Val::list()); nodes.l->push_back(rig); }
    auto trs = [&](Val& nd, const M4& m) {
        const auto q = mat_quat(m);
        nd.d->set("translation", nums({m.at(0, 3), m.at(1, 3), m.at(2, 3)})); nd.d->set("rotation", nums({q[0], q[1], q[2], q[3]}));
    };
    for (std::size_t k = 0; k < all.size(); ++k) {
        Val nd = Val::dict(); nd.d->set("name", Val::str(all[k].uname));
        // vm_build: bones carry rest_world (root's transform included) while the mesh stays in armature space
        trs(nd, rest_local[k]);
        nodes.l->push_back(nd);
        const long long gi = static_cast<long long>(k) + 1;
        if (all[k].parent < 0) nodes[0].d->get("children")->l->push_back(Val::integer(gi));
        else { Val& p = nodes[static_cast<std::size_t>(all[k].parent) + 1]; if (!p.d->has("children")) p.d->set("children", Val::list()); p.d->get("children")->l->push_back(Val::integer(gi)); }
    }
    // joints: rig + every non-root b3d node
    std::vector<long long> joints = {0}; std::vector<M4> joint_world = {M4{}};
    std::map<std::string, long long> joint_of;
    for (std::size_t k = 1; k < all.size(); ++k) { joint_of[all[k].uname] = static_cast<long long>(joints.size()); joints.push_back(static_cast<long long>(k) + 1); joint_world.push_back(rest_world[k]); }
    // weights per b3d vertex
    std::map<long long, std::vector<std::pair<long long, double>>> weights;
    for (std::size_t k = 0; k < all.size(); ++k) for (const auto& e : *get(all[k].n, "bone", Val::list()).l) {
        const long long vid = e[0].i; const double w = e[1].num();
        if (k == 0) continue;   // the root is not a bone in vm_build either
        weights[vid].push_back({joint_of[all[k].uname], w});
    }
    GlbWriter gw; Val meshes = Val::list(), materials = Val::list(), textures = Val::list(), images = Val::list(), mesh_nodes = Val::list();
    Val report = Val::list(); std::map<std::size_t, long long> mat_of_brush;
    const Val brus = get(data, "brus", Val::list());
    auto material = [&](std::size_t bi) -> long long {
        const auto it = mat_of_brush.find(bi); if (it != mat_of_brush.end()) return it->second;
        Val m = Val::dict(); const std::string bname = bi < brus.size() && !get(brus[bi], "name").s.empty() ? get(brus[bi], "name").s : "brush" + std::to_string(bi);
        m.d->set("name", Val::str(bname));
        Val pbr = Val::dict(); Val rgba = bi < brus.size() ? get(brus[bi], "rgba") : Val();
        pbr.d->set("baseColorFactor", rgba.t == Val::LIST && rgba.size() == 4 ? nums({rgba[0].num(), rgba[1].num(), rgba[2].num(), rgba[3].num()}) : nums({1, 1, 1, 1}));
        pbr.d->set("metallicFactor", Val::flt(0.0)); pbr.d->set("roughnessFactor", Val::flt(0.8));
        int flags = 0; const std::string tp = brush_texture(data, bi, tex, tex_dir, &flags);
        if (!tp.empty()) {
            if (!isfile(tp)) die("build: texture not found: " + tp);
            const std::string blob = slurp(tp); const bool png = blob.size() >= 8 && blob.compare(0, 8, img::PNG_SIG) == 0;
            const bool jpg = blob.size() >= 2 && static_cast<unsigned char>(blob[0]) == 0xFF && static_cast<unsigned char>(blob[1]) == 0xD8;
            std::string bytes = blob, mime = png ? "image/png" : "image/jpeg";
            if (!png && !jpg) { const img::Image im = img::load(tp); bytes = img::encode_png_raw(im.w, im.h, 6, im.px); mime = "image/png"; }   // BMP/TGA/DDS... via ffmpeg
            Val im = Val::dict(); im.d->set("bufferView", Val::integer(gw.view(bytes))); im.d->set("mimeType", Val::str(mime)); im.d->set("name", Val::str(u8native(P(tp).filename()))); images.l->push_back(im);
            Val t = Val::dict(); t.d->set("source", Val::integer(static_cast<long long>(images.size()) - 1)); textures.l->push_back(t);
            Val ti = Val::dict(); ti.d->set("index", Val::integer(static_cast<long long>(textures.size()) - 1)); pbr.d->set("baseColorTexture", ti);
            if (flags & 4) { m.d->set("alphaMode", Val::str("MASK")); m.d->set("alphaCutoff", Val::flt(0.5)); }
            else if (flags & 2) m.d->set("alphaMode", Val::str("BLEND"));
        }
        m.d->set("pbrMetallicRoughness", pbr); m.d->set("doubleSided", Val::boolean(false));
        materials.l->push_back(m); return mat_of_brush[bi] = static_cast<long long>(materials.size()) - 1;
    };
    for (const auto& surf : *get(mesh, "surfaces").l) {
        const std::size_t bi = static_cast<std::size_t>(std::max(0LL, get(surf, "brush").i));
        std::set<long long> used_set; for (const auto& t : *get(surf, "tris").l) for (std::size_t c = 0; c < 3; ++c) used_set.insert(t[c].i);
        const std::vector<long long> used(used_set.begin(), used_set.end());
        std::map<long long, uint32_t> remap; for (std::size_t k = 0; k < used.size(); ++k) remap[used[k]] = static_cast<uint32_t>(k);
        for (long long u : used) if (u < 0 || static_cast<std::size_t>(u) >= vpos.size()) die("build: triangle uses vertex " + std::to_string(u) + ", past the end of the vertex list");
        std::string pb, nb, ub, jb, wb, ib; std::vector<double> mn = {1e300, 1e300, 1e300}, mx = {-1e300, -1e300, -1e300};
        const bool has_n = vnrm.size() == vpos.size() && vnrm.size() > 0;
        std::set<long long> groups;
        for (long long u : used) {
            const auto p = vec3(vpos[static_cast<std::size_t>(u)]); const double g[3] = {-p[0] * scale, p[1] * scale, p[2] * scale};
            for (int c = 0; c < 3; ++c) { GlbWriter::f32(pb, g[c]); mn[static_cast<std::size_t>(c)] = std::min(mn[static_cast<std::size_t>(c)], static_cast<double>(static_cast<float>(g[c]))); mx[static_cast<std::size_t>(c)] = std::max(mx[static_cast<std::size_t>(c)], static_cast<double>(static_cast<float>(g[c]))); }
            if (has_n) { const auto n = vec3(vnrm[static_cast<std::size_t>(u)]); const V3 nn = normalize(v3(-n[0], n[1], n[2])); GlbWriter::f32(nb, nn.x); GlbWriter::f32(nb, nn.y); GlbWriter::f32(nb, nn.z); }
            const Val uvv = static_cast<std::size_t>(u) < vuv.size() ? vuv[static_cast<std::size_t>(u)] : Val::list();
            GlbWriter::f32(ub, uvv.size() > 0 ? uvv[0].num() : 0.0); GlbWriter::f32(ub, uvv.size() > 1 ? uvv[1].num() : 0.0);
            auto ws = weights.count(u) ? weights[u] : std::vector<std::pair<long long, double>>{};
            std::stable_sort(ws.begin(), ws.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
            if (ws.size() > 4) ws.resize(4);   // Roblox (and glTF's JOINTS_0) take 4 influences
            double sum = 0; for (const auto& w : ws) sum += w.second;
            if (ws.empty() || sum <= 0) { ws = {{0, 1.0}}; sum = 1.0; }
            for (std::size_t c = 0; c < 4; ++c) { GlbWriter::u16(jb, c < ws.size() ? static_cast<unsigned>(ws[c].first) : 0u); GlbWriter::f32(wb, c < ws.size() ? ws[c].second / sum : 0.0); if (c < ws.size() && ws[c].first > 0) groups.insert(ws[c].first); }
        }
        long long nf = 0;
        for (const auto& t : *get(surf, "tris").l) { GlbWriter::u32(ib, remap[t[0].i]); GlbWriter::u32(ib, remap[t[2].i]); GlbWriter::u32(ib, remap[t[1].i]); ++nf; }   // the reflection flips handedness
        const long long nv = static_cast<long long>(used.size());
        Val attrs = Val::dict();
        attrs.d->set("POSITION", Val::integer(gw.accessor(pb, 5126, nv, "VEC3", 34962, &mn, &mx)));
        if (has_n) attrs.d->set("NORMAL", Val::integer(gw.accessor(nb, 5126, nv, "VEC3", 34962)));
        attrs.d->set("TEXCOORD_0", Val::integer(gw.accessor(ub, 5126, nv, "VEC2", 34962)));
        attrs.d->set("JOINTS_0", Val::integer(gw.accessor(jb, 5123, nv, "VEC4", 34962)));
        attrs.d->set("WEIGHTS_0", Val::integer(gw.accessor(wb, 5126, nv, "VEC4", 34962)));
        Val prim = Val::dict(); prim.d->set("attributes", attrs); prim.d->set("indices", Val::integer(gw.accessor(ib, 5125, nf * 3, "SCALAR", 34963))); prim.d->set("material", Val::integer(material(bi)));
        const std::string bname = bi < brus.size() && !get(brus[bi], "name").s.empty() ? get(brus[bi], "name").s : "brush" + std::to_string(bi);
        Val m = Val::dict(); m.d->set("name", Val::str(bname)); m.d->set("primitives", Val::list({prim})); meshes.l->push_back(m);
        Val mn_ = Val::dict(); mn_.d->set("name", Val::str(bname)); mn_.d->set("mesh", Val::integer(static_cast<long long>(meshes.size()) - 1)); mn_.d->set("skin", Val::integer(0));
        nodes.l->push_back(mn_); mesh_nodes.l->push_back(Val::integer(static_cast<long long>(nodes.size()) - 1));
        if (nf > 20000) eprint("um: warning: " + bname + " has " + std::to_string(nf) + " triangles; Roblox allows 20,000 per mesh");
        Val r = Val::list(); r.l->push_back(Val::str(bname)); r.l->push_back(Val::integer(nf)); r.l->push_back(Val::integer(nv)); r.l->push_back(Val::integer(static_cast<long long>(groups.size()))); report.l->push_back(r);
    }
    // skin
    std::string ibm; for (const auto& w : joint_world) { const M4 inv = inverse(w); for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) GlbWriter::f32(ibm, inv.at(r, c)); }
    Val skin = Val::dict(); skin.d->set("name", Val::str(NAME + "_rig")); skin.d->set("skeleton", Val::integer(0));
    { Val jl = Val::list(); for (long long jn : joints) jl.l->push_back(Val::integer(jn)); skin.d->set("joints", jl); }
    skin.d->set("inverseBindMatrices", Val::integer(gw.accessor(ibm, 5126, static_cast<long long>(joints.size()), "MAT4")));
    Val asset = Val::dict(); asset.d->set("version", Val::str("2.0")); asset.d->set("generator", Val::str("universal-modder um b3d build (C++)"));
    auto doc = [&](GlbWriter& w, bool with_anim, long long& first_o, long long& last_o) {
        Val j = Val::dict(); j.d->set("asset", asset); j.d->set("scene", Val::integer(0));
        Val sc = Val::dict(); Val roots = Val::list({Val::integer(0)}); for (const auto& mnode : *mesh_nodes.l) roots.l->push_back(mnode); sc.d->set("nodes", roots); j.d->set("scenes", Val::list({sc}));
        j.d->set("nodes", nodes); j.d->set("meshes", meshes); j.d->set("materials", materials);
        if (images.size()) { j.d->set("images", images); j.d->set("textures", textures); }
        Val sk = skin; j.d->set("skins", Val::list({sk}));
        const auto [first, last] = seq_range(root, false, "build"); first_o = first; last_o = last;
        if (with_anim) {
            const double fps = get(get(root, "anim", Val::dict()), "fps", Val::integer(20)).num();
            Val samplers = Val::list(), channels = Val::list();
            for (std::size_t k = 1; k < all.size(); ++k) {
                std::map<long long, Val> ks; for (const auto& kv : *get(all[k].n, "keys", Val::list()).l) ks[get(kv, "frame").i] = kv;
                std::string tb, tr, rr; long long n = 0;
                for (long long f = first; f <= last; ++f) {
                    const auto it = ks.find(f); if (it == ks.end()) continue;
                    const Val& kv = it->second;
                    const M4 local = G(blitz_local(vec3(has(kv, "pos") ? get(kv, "pos") : get(all[k].n, "pos")), vec4(has(kv, "rot") ? get(kv, "rot") : get(all[k].n, "rot")), scale));
                    GlbWriter::f32(tb, static_cast<double>(f - first) / std::max(fps, 1e-6));
                    GlbWriter::f32(tr, local.at(0, 3)); GlbWriter::f32(tr, local.at(1, 3)); GlbWriter::f32(tr, local.at(2, 3));
                    const auto q = mat_quat(local); for (double c : q) GlbWriter::f32(rr, c);
                    ++n;
                }
                if (!n) continue;
                float t0, t1; std::memcpy(&t0, tb.data(), 4); std::memcpy(&t1, tb.data() + tb.size() - 4, 4);
                const std::vector<double> tmn = {t0}, tmx = {t1};
                const long long ta = w.accessor(tb, 5126, n, "SCALAR", 0, &tmn, &tmx);
                for (int ch = 0; ch < 2; ++ch) {
                    Val s = Val::dict(); s.d->set("input", Val::integer(ta)); s.d->set("output", Val::integer(w.accessor(ch == 0 ? tr : rr, 5126, n, ch == 0 ? "VEC3" : "VEC4"))); s.d->set("interpolation", Val::str("LINEAR"));
                    samplers.l->push_back(s);
                    Val tg = Val::dict(); tg.d->set("node", Val::integer(static_cast<long long>(k) + 1)); tg.d->set("path", Val::str(ch == 0 ? "translation" : "rotation"));
                    Val c = Val::dict(); c.d->set("sampler", Val::integer(static_cast<long long>(samplers.size()) - 1)); c.d->set("target", tg); channels.l->push_back(c);
                }
            }
            if (channels.size()) { Val an = Val::dict(); an.d->set("name", Val::str(NAME + "_all")); an.d->set("samplers", samplers); an.d->set("channels", channels); j.d->set("animations", Val::list({an})); }
        }
        return j;
    };
    makedirs(outdir);
    long long first = 1, last = 1;
    { GlbWriter w = gw; Val j = doc(w, false, first, last); spit(path_join(outdir, NAME + ".glb"), w.finish(j)); }
    { GlbWriter w = gw; Val j = doc(w, true, first, last); spit(path_join(outdir, NAME + ".preview.glb"), w.finish(j)); }
    Val o = Val::dict(); o.d->set("bones", Val::integer(static_cast<long long>(all.size()) - 1)); o.d->set("meshes", report); o.d->set("frames", ints({first, last}));
    o.d->set("files", Val::list({Val::str(path_join(outdir, NAME + ".glb")), Val::str(path_join(outdir, NAME + ".preview.glb"))}));
    print("VMBUILD " + dumps(o));
}

// ------------------------------------------------------------------ render (vm_render.py)
inline std::array<double, 4> slerp(std::array<double, 4> a, std::array<double, 4> b, double t) {   // (w, x, y, z)
    double d = 0; for (int k = 0; k < 4; ++k) d += a[static_cast<std::size_t>(k)] * b[static_cast<std::size_t>(k)];
    if (d < 0) { for (double& c : b) c = -c; d = -d; }
    std::array<double, 4> o{};
    if (d > 0.9995) { for (int k = 0; k < 4; ++k) o[static_cast<std::size_t>(k)] = a[static_cast<std::size_t>(k)] + (b[static_cast<std::size_t>(k)] - a[static_cast<std::size_t>(k)]) * t; }
    else { const double th = std::acos(std::clamp(d, -1.0, 1.0)), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s; for (int k = 0; k < 4; ++k) o[static_cast<std::size_t>(k)] = wa * a[static_cast<std::size_t>(k)] + wb * b[static_cast<std::size_t>(k)]; }
    const double n = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2] + o[3] * o[3]); for (double& c : o) c /= n; return o;
}
inline void render(const Val& a) {
    const std::string src = S(a, "src"), outdir = S(a, "out"); const double hfov = get(a, "hfov").num(), scale = get(a, "scale").num();
    std::vector<long long> frames; for (const auto& f : *get(a, "frames").l) frames.push_back(f.i);
    const auto [W, H] = parse_size(S(a, "size"));
    const Val data = load_json(src); const Val root = get(data, "root");
    if (!has(root, "mesh")) die("render: the root node has no mesh");
    const auto tex = tex_map(a); const std::string tex_dir = S(a, "tex_dir");
    const std::vector<NodeRef> all = collect(root);
    std::vector<M4> rest_world(all.size());
    for (std::size_t k = 0; k < all.size(); ++k) { const M4 l = blitz_local(vec3(get(all[k].n, "pos")), vec4(get(all[k].n, "rot")), scale); rest_world[k] = all[k].parent >= 0 ? rest_world[static_cast<std::size_t>(all[k].parent)] * l : l; }
    const Val mesh = get(root, "mesh"); const Val V = get(mesh, "verts"); const Val vpos = get(V, "pos"), vnrm = get(V, "normal"), vuv = get(V, "uv");
    // scene in armature space (Blitz axes, x scale); vertices skinned per frame
    Scene sc; std::vector<std::size_t> vid;   // b3d vertex of each scene vertex
    const Val brus = get(data, "brus", Val::list());
    std::map<std::size_t, int> mat_of;
    for (const auto& surf : *get(mesh, "surfaces").l) {
        const std::size_t bi = static_cast<std::size_t>(std::max(0LL, get(surf, "brush").i));
        if (!mat_of.count(bi)) {
            Mat m; m.base[0] = m.base[1] = m.base[2] = 0.8f; m.base[3] = 1.0f; int flags = 0;
            const std::string tp = brush_texture(data, bi, tex, tex_dir, &flags);
            if (!tp.empty()) { const img::Image im = img::load(tp); Tex t; t.w = im.w; t.h = im.h; t.px = im.px; sc.texs.push_back(std::move(t)); m.tex = static_cast<int>(sc.texs.size()) - 1; m.base[0] = m.base[1] = m.base[2] = 1.0f; m.alpha_mode = flags & 4 ? 1 : flags & 2 ? 2 : 0; }
            sc.mats.push_back(m); mat_of[bi] = static_cast<int>(sc.mats.size()) - 1;
        }
        for (const auto& t : *get(surf, "tris").l) {
            for (std::size_t c = 0; c < 3; ++c) {
                const long long u = t[c].i; if (u < 0 || static_cast<std::size_t>(u) >= vpos.size()) die("render: triangle uses a vertex past the end of the vertex list");
                Vtx x; const auto p = vec3(vpos[static_cast<std::size_t>(u)]); x.p = v3(p[0] * scale, p[1] * scale, p[2] * scale);
                if (vnrm.size() == vpos.size()) { const auto n = vec3(vnrm[static_cast<std::size_t>(u)]); x.n = v3(n[0], n[1], n[2]); }
                const Val uvv = static_cast<std::size_t>(u) < vuv.size() ? vuv[static_cast<std::size_t>(u)] : Val::list();
                x.u = static_cast<float>(uvv.size() > 0 ? uvv[0].num() : 0.0); x.v = static_cast<float>(uvv.size() > 1 ? uvv[1].num() : 0.0);
                sc.v.push_back(x); vid.push_back(static_cast<std::size_t>(u));
            }
            sc.tri_mat.push_back(mat_of[bi]); sc.tri_flat.push_back(vnrm.size() == vpos.size() ? 0 : 1);
        }
    }
    std::vector<std::vector<std::pair<std::size_t, double>>> wts(vpos.size());
    for (std::size_t k = 1; k < all.size(); ++k) for (const auto& e : *get(all[k].n, "bone", Val::list()).l) { const long long v = e[0].i; if (v >= 0 && static_cast<std::size_t>(v) < wts.size()) wts[static_cast<std::size_t>(v)].push_back({k, e[1].num()}); }
    // pose at a frame: keyed values, linear/slerp between keys (Blitz interpolates the same way)
    auto pose_local = [&](std::size_t k, long long f) -> M4 {
        const Val keys = get(all[k].n, "keys", Val::list());
        const auto rp = vec3(get(all[k].n, "pos")); const auto rr = vec4(get(all[k].n, "rot"));
        if (k == 0 || !keys.size()) return blitz_local(rp, rr, scale);
        auto kp = [&](const Val& kv) { return has(kv, "pos") ? vec3(get(kv, "pos")) : rp; };
        auto kr = [&](const Val& kv) { return has(kv, "rot") ? vec4(get(kv, "rot")) : rr; };
        std::size_t hi = 0; while (hi < keys.size() && get(keys[hi], "frame").i < f) ++hi;
        if (hi < keys.size() && get(keys[hi], "frame").i == f) return blitz_local(kp(keys[hi]), kr(keys[hi]), scale);
        if (hi == 0) return blitz_local(kp(keys[0]), kr(keys[0]), scale);
        if (hi == keys.size()) return blitz_local(kp(keys[hi - 1]), kr(keys[hi - 1]), scale);
        const Val& ka = keys[hi - 1]; const Val& kb = keys[hi];
        const double t = static_cast<double>(f - get(ka, "frame").i) / static_cast<double>(get(kb, "frame").i - get(ka, "frame").i);
        const auto pa = kp(ka), pb = kp(kb); std::array<double, 3> p{}; for (int c = 0; c < 3; ++c) p[static_cast<std::size_t>(c)] = pa[static_cast<std::size_t>(c)] + (pb[static_cast<std::size_t>(c)] - pa[static_cast<std::size_t>(c)]) * t;
        return blitz_local(p, slerp(kr(ka), kr(kb), t), scale);
    };
    makedirs(outdir);
    Cam c; c.persp = true; c.pos = v3(0, 0, 0); c.right = v3(1, 0, 0); c.up = v3(0, 1, 0); c.fwd = v3(0, 0, 1); c.near_ = 0.001;
    const int ss = 3; c.f = (static_cast<double>(W) * ss / 2.0) / std::tan(rad(hfov) / 2); c.cx = W * ss / 2.0; c.cy = H * ss / 2.0;
    Light L; L.dir = normalize(v3(0.35, -0.55, 1.0)); L.sun = 1.7; L.ambient = 0.42; L.cast = false;   // a studio-like key light from behind the camera
    Target tg; tg.W = static_cast<int>(W); tg.H = static_cast<int>(H); tg.ss = ss; tg.opaque = true; tg.bg[0] = tg.bg[1] = tg.bg[2] = 0.05;
    const int threads = default_threads(static_cast<std::size_t>(W * H) * ss * ss * 16 + sc.v.size() * 48);
    parallel_jobs(frames.size(), threads, [&](std::size_t q) {
        const long long f = frames[q];
        std::vector<M4> world(all.size());
        for (std::size_t k = 0; k < all.size(); ++k) { const M4 l = pose_local(k, f); world[k] = all[k].parent >= 0 ? world[static_cast<std::size_t>(all[k].parent)] * l : l; }
        std::vector<M4> skin(all.size()); for (std::size_t k = 0; k < all.size(); ++k) skin[k] = world[k] * inverse(rest_world[k]);
        std::vector<V3> Pp(sc.v.size()), Np(sc.v.size());
        for (std::size_t i = 0; i < sc.v.size(); ++i) {
            const auto& ws = wts[vid[i]];
            if (ws.empty()) { Pp[i] = sc.v[i].p; Np[i] = sc.v[i].n; continue; }   // unweighted: stays in armature space, like vm_build
            V3 p, n; double sum = 0;
            for (const auto& [k, w] : ws) { p = p + xpoint(skin[k], sc.v[i].p) * w; n = n + xdir(skin[k], sc.v[i].n) * w; sum += w; }
            Pp[i] = sum > 0 ? p * (1.0 / sum) : sc.v[i].p; Np[i] = normalize(n);
        }
        char name[32]; std::snprintf(name, sizeof name, "f%03lld.png", f);
        img::save(render_main(sc, Pp, Np, c, L, nullptr, tg), path_join(outdir, name));
    });
    print("rendered " + std::to_string(frames.size()) + " frames -> " + outdir + "  (pack a contact sheet with `um sprite sheet`)");
}

inline const char* DOC =
    "Animated Blitz3D .b3d models into Roblox (the blitz3d-decomp skill's references/b3d-to-roblox.md).\n\n"
    "    um b3d dump MODEL.b3d OUT.json                         # node tree, meshes per brush, BONE weights, KEYS, ANIM, SEQS\n"
    "    um b3d anim OUT.json VM.luau --scale 0.06 --name NAME   # keyframes as a Luau data module (int16, base64)\n"
    "    um b3d build OUT.json build/ --scale 0.06 --tex brush=arms.png\n"
    "    um b3d render OUT.json frames/ 74 1 45 60 120            # frames from the viewmodel camera (Blitz origin, looking +z)\n\n"
    "build writes build/NAME.glb (a skinned model: one bone per node, one mesh per brush, 4 influences per vertex, the\n"
    "bind pose) and NAME.preview.glb (the same with the keyframes as an animation, for any glTF viewer). Model space\n"
    "is (-x, y, z) of Blitz times --scale and the bones keep the node frames. Upstream made an FBX in Blender; Roblox's\n"
    "3D importer reads glTF too - check one bone's position against the JSON x scale after importing.\n"
    "Only convert models you made or have a licence for, and keep the author's credit and licence with them.\n";
inline void reg(Parser& sub) {
    Parser& p = sub.sub("b3d", DOC); p.help_line = "Blitz3D .b3d viewmodels: dump, Luau keyframes, a skinned glTF for Roblox, preview frames"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("dump"); q.help_line = "dump a .b3d (skeleton, skin weights, keyframes, SEQS) to JSON"; q.pos("src"); q.pos("dst"); }
    { Parser& q = p.sub("anim"); q.help_line = "write the keyframes as a Luau data module"; q.pos("src"); q.pos("dst"); q.opt({"--scale"}).number().required(); q.opt({"--name"}).required(); }
    { Parser& q = p.sub("build"); q.help_line = "build a skinned glTF (GLB) for Roblox from the JSON"; q.pos("src"); q.pos("out"); q.opt({"--scale"}).number().def_float(1.0);
      q.opt({"--tex"}).append().help("BRUSH=PATH: the texture for a brush (repeatable)"); q.opt({"--tex-dir"}).help("folder with the textures the b3d names (used when --tex doesn't name the brush)"); }
    { Parser& q = p.sub("render"); q.help_line = "render frames from the viewmodel camera"; q.pos("src"); q.pos("out"); q.pos("hfov").number(); q.pos("frames").nargs("+").integer();
      q.opt({"--scale"}).number().def_float(1.0); q.opt({"--size"}).def("640x360"); q.opt({"--tex"}).append().help("BRUSH=PATH (repeatable)"); q.opt({"--tex-dir"}); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "dump") dump(S(a, "src"), S(a, "dst"));
    else if (c == "anim") anim(S(a, "src"), S(a, "dst"), get(a, "scale").num(), S(a, "name"));
    else if (c == "build") build(a);
    else if (c == "render") render(a);
    return 0;
}
}  // namespace packtools::modder::b3d
