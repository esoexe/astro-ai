// modder_sprixen.inc - `um sprixen`: a C++ port of universal-modder's um/sprixen.py (Rehan and universal-modder contributors, MIT).
// Sprixen (https://sprixen.com) is a separate paid service: an account, an API key and credits are needed.
namespace packtools::modder::sprixen {
using namespace pyrt;
using namespace packtools::modder;

inline const char* DOC =
    "2D game sprites from Sprixen (https://sprixen.com): one style per project, animation sheets, terrain tiles.\n\n"
    "    um sprixen key set                                       # hidden prompt; or set SPRIXEN_API_KEY\n"
    "    um sprixen credits                                       # plan and credits left\n"
    "    um sprixen sprite \"a rusty scrap drone enemy, side view facing left\" --name drone\n"
    "    um sprixen sprite \"a red health potion\" --style pixel_art --size 32x32 --view side_scroll --variants 3\n"
    "    um sprixen animate <sprite_id> --type walk --frames 8 --name drone_walk   # keeps the sprite's facing\n"
    "    um sprixen animate <sprite_id> --type attack --weapon-type sword --weapon-hand right\n"
    "    um sprixen tile \"mossy cobblestone floor\" --projection top_down --name floor   # then: um sprite tile-preview\n"
    "    um sprixen projects                                      # style projects and their sprite counts\n\n"
    "Style lives in a Sprixen project (art style, resolution, view angle, palette), so every sprite generated in\n"
    "the same project shares one look. `sprite` reuses the project named by --project, or one named after the\n"
    "style flags (\"um pixel_art side_scroll\"), and creates it on first use. Sprites come back on a transparent\n"
    "background and print their sprite id: pass it to `animate`, which returns one horizontal strip of frames\n"
    "(frame size = sheet width / frames x sheet height; `um sprite slice` splits it, `um sprite sheet --vertical`\n"
    "restacks it for Terraria-style NPCs).\n\n"
    "Sprixen is a paid service: it needs an account, an API key (https://sprixen.com/settings/api) and credits\n"
    "(sprite 2 per variant, animation 4, premium 8, tile 1); `um sprixen credits` shows what is left. The key is\n"
    "read from SPRIXEN_API_KEY, else from where `um sprixen key set` stored it (the desktop keyring on Linux, an\n"
    "owner-only ~/.secrets/sprixen-key elsewhere), else from SPRIXEN_API_KEY=... in a .env.\n"
    "Files land in --out (default assets/gen), and each call appends a line to <out>/sprixen_manifest.jsonl (ids,\n"
    "prompt, settings, files) so an asset can be traced.\n";

inline const std::string DEFAULT_URL = "https://api.sprixen.com";
inline const std::vector<std::string> STYLES = {"pixel_art", "cartoon", "anime", "hand_drawn", "chibi"};
inline const std::vector<std::string> VIEWS = {"side_scroll", "top_down", "isometric", "front_facing"};
inline const std::vector<std::string> SIZES = {"16x16", "32x32", "64x64", "128x128", "256x256", "512x512"};
inline const std::vector<std::string> ANIMATIONS = {"idle", "walk", "run", "attack", "jump", "death", "hurt", "custom"};
inline const std::vector<std::string> DIRECTIONS = {"right", "left", "up", "down", "up-right", "up-left", "down-right", "down-left"};
inline const std::vector<std::string> KEYRING_ATTRS = {"service", "sprixen.com", "key", "api"};

// ------------------------------------------------------------------ key
inline std::string key_file() { const std::string n = env("UM_SPRIXEN_KEY_FILE"); return expanduser(!n.empty() ? n : "~/.secrets/sprixen-key"); }
inline std::optional<std::string> read_dotenv(const std::string& p) {
    if (!isfile(p)) return std::nullopt;
    const auto m = re::search(R"(^\s*SPRIXEN_API_KEY\s*=\s*['"]?([^'"\s]+))", slurp(p), 8);
    if (!m.ok) return std::nullopt; return m.group(1);
}
inline std::vector<std::pair<std::string, std::function<std::optional<std::string>()>>> key_candidates() {
    std::vector<std::pair<std::string, std::function<std::optional<std::string>()>>> c;
    const std::string named = env("UM_SPRIXEN_KEY_FILE"), kf = key_file();
    c.emplace_back("SPRIXEN_API_KEY environment variable", [] { const std::string v = strip(env("SPRIXEN_API_KEY")); return v.empty() ? std::nullopt : std::optional<std::string>(v); });
    if (!named.empty()) c.emplace_back("key file " + kf + " (UM_SPRIXEN_KEY_FILE)", [kf] { return fal::read_file_key(kf); });
    c.emplace_back("OS keyring (" + join(KEYRING_ATTRS, " ") + ")", [] { return fal::secret_lookup(KEYRING_ATTRS); });
    if (named.empty()) c.emplace_back("key file " + kf, [kf] { return fal::read_file_key(kf); });
    std::error_code ec; const std::string dot = u8native(fs::current_path(ec) / ".env");
    c.emplace_back(".env " + dot, [dot] { return read_dotenv(dot); });
    return c;
}
inline std::string& key_cache() { static std::string k; return k; }
inline std::string api_key() {
    if (key_cache().empty()) for (const auto& [label, read] : key_candidates()) if (const auto k = read()) { key_cache() = *k; break; }
    if (key_cache().empty()) die("no Sprixen key. Create one at https://sprixen.com/settings/api, then `um sprixen key set` (or set SPRIXEN_API_KEY)");
    return key_cache();
}
inline void cmd_key(const Val& a) {
    const std::string action = S(a, "action");
    if (action == "set") {
        const std::string value = !S(a, "from_file").empty() ? strip(slurp(expanduser(S(a, "from_file")))) : strip(getpass_line("Sprixen API key (hidden): "));
        if (value.empty()) die("empty key, nothing stored");
        if (!starts(value, "spx_")) eprint("note: Sprixen keys start with spx_live_; storing it anyway");
        if (truthy(get(a, "file_only"))) print("stored in " + fal::write_key_file(value, key_file()) + " as " + fal::mask(value) + " (plaintext, owner-only)");
        else if (fal::secret_store("Sprixen API key (universal-modder)", KEYRING_ATTRS, value) && fal::secret_lookup(KEYRING_ATTRS) == value)
            print("stored in the OS keyring as " + fal::mask(value) + " (" + join(KEYRING_ATTRS, " ") + ")");
        else if (fal::keyring_available()) die("could not write to the OS keyring (is it unlocked?); `um sprixen key set --file-only` stores the key in a plaintext file instead");
        else print("no OS keyring (no secret-tool); stored in " + fal::write_key_file(value, key_file()) + " as " + fal::mask(value) + " (plaintext, owner-only)");
        if (!env("SPRIXEN_API_KEY").empty()) print("note: SPRIXEN_API_KEY is set in this environment and takes precedence over the stored key");
    } else if (action == "clear") {
        std::vector<std::string> removed;
        if (fal::secret_lookup(KEYRING_ATTRS) && fal::secret_clear(KEYRING_ATTRS)) removed.push_back("OS keyring");
        const std::string kf = key_file();
        if (pexists(kf)) { std::error_code ec; fs::remove(P(kf), ec); removed.push_back(kf); }
        print(removed.empty() ? "nothing to remove" : "removed: " + join(removed, ", "));
    } else {
        std::vector<std::pair<std::string, std::string>> src;
        for (const auto& [label, read] : key_candidates()) if (const auto k = read()) src.emplace_back(label, *k);
        if (src.empty()) print("no Sprixen key configured; run `um sprixen key set`");
        for (std::size_t i = 0; i < src.size(); ++i) print(std::string(i == 0 ? "active  " : "shadowed") + "  " + src[i].first + "  ->  " + fal::mask(src[i].second));
        const std::string kf = key_file();
        print(std::string("keyring     ") + (fal::keyring_available() ? "available" : "not available (no secret-tool)"));
        print("key file    " + kf + " " + (pexists(kf) ? "exists (mode " + fal::file_mode(kf) + ")" : std::string("absent")));
    }
}

// ------------------------------------------------------------------ http
inline std::string base_url(const std::string& url) { const std::string e = env("SPRIXEN_API_URL"); return comfy::rstrip_slash(!url.empty() ? url : !e.empty() ? e : DEFAULT_URL); }
inline std::pair<std::string, Val> error_line(long code, const std::string& body) {
    Val err; try { err = loads(decode_utf8_replace(body)); } catch (const PyErr&) { return {"HTTP " + std::to_string(code) + ": " + decode_utf8_replace(body.substr(0, 500)), Val::dict()}; }
    if (err.t != Val::DICT) err = Val::dict();
    const Val m = get(err, "message"); std::string msg;
    if (m.t == Val::LIST) { std::vector<std::string> parts; for (const auto& x : *m.l) parts.push_back(pystr(x)); msg = join(parts, "; "); }
    else msg = truthy(m) ? pystr(m) : truthy(get(err, "error")) ? pystr(get(err, "error")) : "";
    const Val hint = truthy(get(err, "hint")) ? get(err, "hint") : Val::dict();
    std::string line = "HTTP " + std::to_string(code) + (truthy(get(err, "code")) ? " " + pystr(get(err, "code")) : "") + ": " + msg;
    if (truthy(get(hint, "action"))) line += " (next: " + pystr(get(hint, "action")) + (truthy(get(hint, "url")) ? " " + pystr(get(hint, "url")) : "") + ")";
    return {line, err};
}
inline Val request(const std::string& base, const std::string& method, const std::string& path, const Val* body = nullptr, int retries = 3) {
    std::string data; const std::string* dp = nullptr; if (body) { data = dumps(*body); dp = &data; }
    for (int attempt = 0; attempt <= retries; ++attempt) {
        std::vector<std::string> h = {"Authorization: Bearer " + api_key(), "Accept: application/json", "User-Agent: universal-modder"};
        if (dp) h.push_back("Content-Type: application/json");
        const UmResp r = um_http(method, base + "/v1" + path, h, dp, 120);
        if (r.status == 0) {
            if (r.error.find("imeout") != std::string::npos) die("Sprixen " + method + " " + path + " failed: " + r.error);
            die("no Sprixen API at " + base + " (" + r.error + ")");
        }
        if (r.status >= 200 && r.status < 400) {
            if (r.body.empty()) return Val::dict();
            try { return loads(decode_utf8_replace(r.body)); } catch (const PyErr&) { die("Sprixen " + method + " " + path + " answered with something that isn't JSON"); }
        }
        const auto [line, err] = error_line(r.status, r.body);
        const Val wait_v = get(truthy(get(truthy(get(err, "hint")) ? get(err, "hint") : Val::dict(), "details")) ? get(get(err, "hint"), "details") : Val::dict(), "retryAfterSeconds");
        if (attempt < retries && (r.status == 429 || pystr(get(err, "code")) == "ACTIVE_GENERATION_LIMIT")) {
            double w = 15; if (truthy(wait_v)) { if (wait_v.is_real()) w = wait_v.num(); else if (const auto f = parse_float(pystr(wait_v))) w = *f; }
            eprint("um: " + line + "; retrying in " + fmt_f(w, 0) + " s");
            sleep_s(w); continue;
        }
        die("Sprixen " + method + " " + path + " -> " + line);
    }
    return Val();
}
inline std::string download(const std::string& base, const std::string& url, const std::string& path) {
    const std::string full = starts(url, "http") ? url : base + url;
    const UmResp r = um_http("GET", full, {"User-Agent: universal-modder"}, nullptr, 300);
    if (r.status < 200 || r.status >= 300) die("could not download " + full + ": " + (r.status ? "HTTP Error " + std::to_string(r.status) : r.error));
    spit(path, r.body); return path;
}
inline Val png_size(const std::string& path) {
    const std::string h = head_bytes(path, 24);
    if (h.size() < 24 || h.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return Val();
    auto be = [&](std::size_t o) { return (static_cast<long long>(static_cast<unsigned char>(h[o])) << 24) | (static_cast<unsigned char>(h[o + 1]) << 16) | (static_cast<unsigned char>(h[o + 2]) << 8) | static_cast<unsigned char>(h[o + 3]); };
    return Val::list({Val::integer(be(16)), Val::integer(be(20))});
}
inline std::string sv(const Val& v, const std::string& dflt = "") { return v.is_none() && !dflt.empty() ? dflt : pystr(v); }
inline Val wait(const std::string& base, const std::string& path, double timeout, double poll = 4.0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(timeout * 1000));
    std::string last; bool have_last = false;
    while (std::chrono::steady_clock::now() < end) {
        const Val job = request(base, "GET", path);
        const std::string status = pystr(get(job, "status"));
        if (status == "completed") return job;
        if (status == "failed") die("Sprixen job " + pystr(get(job, "id")) + " failed: " + (truthy(get(job, "error")) ? pystr(get(job, "error")) : "no reason given"));
        const Val prog = truthy(get(job, "progress")) ? get(job, "progress") : Val::dict();
        auto pg = [&](const char* k) { return has(prog, k) ? pystr(get(prog, k)) : std::string(); };
        std::string note = status + " " + pg("phase") + " " + pg("completed") + "/" + pg("total");
        { std::size_t s = 0, e = note.size(); while (s < e && (note[s] == ' ' || note[s] == '/')) ++s; while (e > s && (note[e - 1] == ' ' || note[e - 1] == '/')) --e; note = note.substr(s, e - s); }
        if (!have_last || note != last) {
            const auto parts = split(path, "/"); std::string kind = parts.size() >= 2 ? parts[parts.size() - 2] : ""; if (!kind.empty()) kind.pop_back();
            eprint("um: " + kind + " " + pystr(get(job, "id")) + ": " + note); last = note; have_last = true;
        }
        sleep_s(poll);
    }
    die("Sprixen didn't finish in " + fmt_f(timeout, 0) + " s (" + path + "). It was charged when it started and may still complete: check it at https://sprixen.com before running it again");
    return Val();
}
inline void append_manifest(const std::string& out, const std::string& file, const Val& rec0) {
    Val rec = Val::dict(); rec.d->set("t", Val::str(fal::now_local())); for (const auto& [k, v] : rec0.d->items) rec.d->set(k, v);
    const std::string mf = path_join(out, file);
    FILE* fp = std::fopen(mf.c_str(), "ab");
#if defined(_WIN32)
    if (!fp) fp = _wfopen(P(mf).c_str(), L"ab");
#endif
    if (fp) { const std::string line = dumps(rec) + "\n"; std::fwrite(line.data(), 1, line.size(), fp); std::fclose(fp); }
}
inline std::string name_for(const std::string& name, const std::string& fallback, const char* dflt) {
    if (!name.empty()) return name;
    auto w = split_ws(re::sub("[^a-z0-9 ]", "", lower(fallback))); if (w.size() > 5) w.resize(5);
    const std::string n = join(w, "_"); return n.empty() ? dflt : n;
}
inline Val D(std::initializer_list<std::pair<const char*, Val>> kvs) { Val d = Val::dict(); for (const auto& [k, v] : kvs) d.d->set(k, v); return d; }
inline Val sval(const std::string& s) { return s.empty() ? Val() : Val::str(s); }

// ------------------------------------------------------------------ projects
inline Val project_for(const std::string& base, const std::string& ref, const std::string& style, const std::string& view, const std::string& size, const std::string& palette) {
    const std::string name = !ref.empty() ? ref : "um " + style + " " + view;
    const Val ps = request(base, "GET", "/projects");
    if (ps.t == Val::LIST) for (const auto& p : *ps.l) {
        const bool idm = !ref.empty() && get(p, "id").t == Val::STR && get(p, "id").s == ref;
        if (idm || (get(p, "name").t == Val::STR && get(p, "name").s == name)) {
            if (ref.empty() && !(get(p, "artStyle").t == Val::STR && get(p, "artStyle").s == style && get(p, "viewAngle").t == Val::STR && get(p, "viewAngle").s == view))
                eprint("um: project " + repr(Val::str(name)) + " keeps its own style (" + pystr(get(p, "artStyle")) + ", " + pystr(get(p, "viewAngle")) + ")");
            return p;
        }
    }
    Val body = D({{"name", Val::str(name)}, {"artStyle", Val::str(style)}, {"viewAngle", Val::str(view)}, {"description", Val::str("created by universal-modder")}});
    if (!size.empty()) body.d->set("resolution", Val::str(size));
    if (!palette.empty()) {
        if (palette.find(',') != std::string::npos || starts(palette, "#")) { List l; for (const auto& c : split(palette, ",")) l.push_back(Val::str(strip(c))); body.d->set("palette", Val::list(l)); }
        else body.d->set("palette", Val::str(palette));
    }
    const Val p = request(base, "POST", "/projects", &body);
    eprint("um: created Sprixen project " + repr(Val::str(name)) + " (" + pystr(get(p, "id")) + ")");
    return p;
}

// ------------------------------------------------------------------ commands
inline void cmd_sprite(const Val& a, const std::string& base, const std::string& out) {
    const Val proj = project_for(base, S(a, "project"), S(a, "style"), S(a, "view"), S(a, "size"), S(a, "palette"));
    Val body = D({{"prompt", get(a, "prompt")}, {"projectId", get(proj, "id")}, {"variantCount", get(a, "variants")}, {"pixelPerfect", get(a, "pixel_perfect")}});
    if (!S(a, "size").empty()) body.d->set("resolution", get(a, "size"));
    if (!S(a, "ref").empty()) body.d->set("referenceImageUrl", get(a, "ref"));
    if (truthy(get(a, "no_base"))) body.d->set("noBase", Val::boolean(true));
    Val gen = request(base, "POST", "/generations", &body);
    gen = wait(base, "/generations/" + pystr(get(gen, "id")), to_float(get(a, "timeout")));
    std::vector<std::pair<std::string, Val>> sprites;
    const Val sl = get(gen, "sprites"); if (sl.t == Val::LIST) for (const auto& s : *sl.l) sprites.emplace_back(pystr(get(s, "imageUrl")), s);
    const std::string name = name_for(S(a, "name"), S(a, "prompt"), "sprite");
    List files; const Val urls = get(gen, "resultUrls");
    if (urls.t == Val::LIST) for (std::size_t i = 0; i < urls.size(); ++i) {
        const std::string url = pystr(urls[i]);
        const std::string p = download(base, url, path_join(out, (i == 0 ? name : name + "_" + std::to_string(i + 1)) + ".png"));
        Val sid; for (const auto& [u, s] : sprites) if (u == url) sid = get(s, "id");
        files.push_back(D({{"file", Val::str(p)}, {"sprite_id", sid}, {"size", png_size(p)}}));
        print(p);
        if (truthy(sid)) print("  sprite id " + pystr(sid) + "  (animate: um sprixen animate " + pystr(sid) + " --type walk)");
    }
    if (files.empty()) die("generation " + pystr(get(gen, "id")) + " completed with no images");
    append_manifest(out, "sprixen_manifest.jsonl", D({{"kind", Val::str("sprite")}, {"generation_id", get(gen, "id")}, {"project", get(proj, "name")}, {"project_id", get(proj, "id")},
        {"prompt", get(a, "prompt")}, {"style", get(proj, "artStyle")}, {"view", get(proj, "viewAngle")}, {"size", get(a, "size")}, {"credits", get(gen, "creditsUsed")}, {"files", Val::list(files)}}));
}
inline void cmd_animate(const Val& a, const std::string& base, const std::string& out) {
    Val body = D({{"type", get(a, "type")}, {"quality", Val::str(truthy(get(a, "premium")) ? "premium" : "standard")}});
    const std::pair<const char*, const char*> opt[] = {{"direction", "direction"}, {"frameCount", "frames"}, {"fps", "fps"}, {"customDescription", "custom"},
                                                       {"archetype", "archetype"}, {"weaponHand", "weapon_hand"}, {"weaponType", "weapon_type"}, {"resolution", "size"}};
    for (const auto& [k, d] : opt) if (!get(a, d).is_none()) body.d->set(k, get(a, d));
    if (truthy(get(a, "shield"))) body.d->set("hasShield", Val::boolean(true));
    if (S(a, "type") == "custom" && S(a, "custom").empty()) die("--type custom needs --custom \"what the character does\"");
    const std::string sid = S(a, "sprite_id");
    if (S(a, "size").empty()) {   // frames default to the project's resolution; keep the size this sprite was generated at instead
        const Val sp = request(base, "GET", "/sprites/" + sid);
        if (truthy(get(sp, "width")) && truthy(get(sp, "height"))) body.d->set("resolution", Val::str(pystr(get(sp, "width")) + "x" + pystr(get(sp, "height"))));
    }
    Val anim = request(base, "POST", "/sprites/" + sid + "/animations", &body);
    anim = wait(base, "/animations/" + pystr(get(anim, "id")), to_float(get(a, "timeout")));
    if (!truthy(get(anim, "spriteSheetUrl"))) die("animation " + pystr(get(anim, "id")) + " completed with no sprite sheet");
    std::string name = S(a, "name");
    if (name.empty()) { std::vector<std::string> parts; for (const char* k : {"type", "direction"}) if (!S(a, k).empty()) parts.push_back(S(a, k)); name = join(parts, "_"); }
    const std::string p = download(base, pystr(get(anim, "spriteSheetUrl")), path_join(out, name + ".png"));
    const long long n = truthy(get(anim, "frameCount")) ? static_cast<long long>(get(anim, "frameCount").num()) : 1;
    const Val wh = png_size(p); const long long w = wh.is_none() ? 0 : wh[0].i, h = wh.is_none() ? 0 : wh[1].i;
    const long long fw = n ? floordiv(w, n) : w;
    print(p);
    print("  " + std::to_string(n) + " frames of " + std::to_string(fw) + "x" + std::to_string(h) + " in one row at " + pystr(get(anim, "fps")) + " fps  (split: um sprite slice " + p + " <dir> --frame " + std::to_string(fw) + "x" + std::to_string(h) + ")");
    append_manifest(out, "sprixen_manifest.jsonl", D({{"kind", Val::str("animation")}, {"animation_id", get(anim, "id")}, {"sprite_id", Val::str(sid)}, {"type", get(a, "type")},
        {"direction", get(a, "direction")}, {"quality", get(body, "quality")}, {"frames", Val::integer(n)}, {"fps", get(anim, "fps")},
        {"frame_size", Val::list({Val::integer(fw), Val::integer(h)})}, {"file", Val::str(p)}}));
}
inline void cmd_tile(const Val& a, const std::string& base, const std::string& out) {
    Val body = D({{"prompt", get(a, "prompt")}, {"projectionType", get(a, "projection")}, {"collision", get(a, "collision")}, {"category", get(a, "category")}});
    if (truthy(get(a, "flat"))) body.d->set("flatOnly", Val::boolean(true));
    Val gen = request(base, "POST", "/tile-packs/generate", &body);
    gen = wait(base, "/generations/" + pystr(get(gen, "id")), to_float(get(a, "timeout")));
    const Val urls = get(gen, "resultUrls");
    if (!(urls.t == Val::LIST && urls.size())) die("tile " + pystr(get(gen, "id")) + " completed with no image");
    const std::string p = download(base, pystr(urls[0]), path_join(out, name_for(S(a, "name"), S(a, "prompt"), "sprite") + ".png"));
    print(p);
    if (S(a, "projection") != "isometric") print("  check the seams: um sprite tile-preview " + p + " <out.png>  (um sprite seamless blends the edges)");
    append_manifest(out, "sprixen_manifest.jsonl", D({{"kind", Val::str("tile")}, {"generation_id", get(gen, "id")}, {"prompt", get(a, "prompt")}, {"projection", get(a, "projection")},
        {"collision", get(a, "collision")}, {"size", png_size(p)}, {"file", Val::str(p)}}));
}

inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "key") { cmd_key(a); return 0; }
    const std::string base = base_url(S(a, "url"));
    if (c == "credits") {
        const Val s = request(base, "GET", "/billing/status");
        const Val lim = get(s, "creditsLimit"), used = get(s, "creditsUsed");
        const double l = truthy(lim) ? lim.num() : 0, u = truthy(used) ? used.num() : 0;
        const bool flt = lim.t == Val::FLOAT || used.t == Val::FLOAT;
        const std::string left = flt ? float_repr(l - u) : std::to_string(static_cast<long long>(l - u));
        if (truthy(get(a, "json"))) emit_json(s); else print("plan " + pystr(get(s, "plan")) + ": " + left + " of " + pystr(lim) + " credits left");
        return 0;
    }
    if (c == "projects") {
        const Val ps = request(base, "GET", "/projects");
        if (truthy(get(a, "json"))) { emit_json(ps); return 0; }
        if (ps.t == Val::LIST) for (const auto& p : *ps.l) {
            const Val cnt = truthy(get(p, "_count")) ? get(p, "_count") : Val::dict();
            print(pystr(get(p, "id")) + "  " + repr(get(p, "name")) + "  " + pystr(get(p, "artStyle")) + " " + pystr(get(p, "viewAngle")) + " " + pystr(get(p, "resolution")) + "  " +
                  (has(cnt, "sprites") ? pystr(get(cnt, "sprites")) : "?") + " sprite(s)");
        }
        return 0;
    }
    const std::string out = S(a, "out"); makedirs(out);
    if (c == "sprite") cmd_sprite(a, base, out); else if (c == "animate") cmd_animate(a, base, out); else if (c == "tile") cmd_tile(a, base, out);
    return 0;
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("sprixen", DOC); p.help_line = "2D sprites with Sprixen: one style per project, animation sheets, terrain tiles"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    auto cmd = [&](const char* name, const char* help, bool out = true) -> Parser& {
        Parser& q = p.sub(name); q.help_line = help;
        q.opt({"--url"}).help("API address (default: $SPRIXEN_API_URL or " + DEFAULT_URL + ")");
        if (out) {
            q.opt({"--out"}).def("assets/gen").help("output folder (default assets/gen)"); q.opt({"--name"}).help("output file stem");
            q.opt({"--timeout"}).number().def(900LL).help("seconds to wait for the result");
        }
        return q;
    };
    { Parser& q = cmd("credits", "plan and credits left", false); q.opt({"--json"}).store_true(); }
    { Parser& q = cmd("projects", "list your style projects", false); q.opt({"--json"}).store_true(); }
    { Parser& q = cmd("sprite", "text -> sprite(s) on a transparent background, in a style project (2 credits each)"); q.pos("prompt");
      q.opt({"--project"}).help("project name or id (default: one named after --style and --view)");
      q.opt({"--style"}).def("pixel_art").choices(STYLES).help("art style of a new project");
      q.opt({"--view"}).def("side_scroll").choices(VIEWS).help("view angle of a new project");
      q.opt({"--size"}).choices(SIZES).help("sprite resolution (default: the project's, else 128x128)");
      q.opt({"--palette"}).help("new project palette: a preset (nes, snes, gameboy, pico8) or #hex,#hex,...");
      q.opt({"--variants"}).integer().def(1LL).choices({"1", "2", "3"});
      q.opt({"--ref"}).help("reference image URL to match (style or character)");
      q.opt({"--pixel-perfect"}).def("auto").choices({"auto", "on", "off"}).help("downscale kernel: on = nearest neighbour (crisp pixels), off = smooth");
      q.opt({"--no-base"}).store_true().help("no pedestal or ground under the subject"); }
    { Parser& q = cmd("animate", "sprite id -> animation sheet, one row of frames (4 credits, premium 8)"); q.pos("sprite_id");
      q.opt({"--type"}).def("idle").choices(ANIMATIONS);
      q.opt({"--direction"}).choices(DIRECTIONS).help("default: keep the sprite's facing (left and right are not re-checked, so generate the sprite facing the way the engine wants)");
      q.opt({"--frames"}).integer().help("2-12 (default 8)"); q.opt({"--fps"}).integer().help("4-60 (default 12)");
      q.opt({"--custom"}).help("what the character does, for --type custom");
      q.opt({"--archetype"}).choices({"humanoid_biped", "quadruped", "winged", "amorphous", "serpentine"});
      q.opt({"--weapon-type"}).help("e.g. sword, staff, bow"); q.opt({"--weapon-hand"}).choices({"left", "right", "both"});
      q.opt({"--shield"}).store_true(); q.opt({"--size"}).choices(SIZES).help("frame resolution (default: the size of the sprite)");
      q.opt({"--premium"}).store_true().help("smoother video-based animation, 96 frames at 24 fps (8 credits)"); }
    { Parser& q = cmd("tile", "text -> terrain tile, top-down, side-view or isometric (1 credit)"); q.pos("prompt");
      q.opt({"--projection"}).def("top_down").choices({"top_down", "side_view", "isometric"});
      q.opt({"--collision"}).def("walkable").choices({"walkable", "solid", "water"});
      q.opt({"--category"}).def("terrain").choices({"terrain", "decoration", "entity"});
      q.opt({"--flat"}).store_true().help("isometric only: a flat tile without raised block sides"); }
    Parser& kp = p.sub("key", "`um sprixen` takes the key from SPRIXEN_API_KEY first, then from a file named by UM_SPRIXEN_KEY_FILE, then from the OS keyring "
                              "(libsecret's secret-tool, as `um fal key` uses: Linux), then from ~/.secrets/sprixen-key (owner-only, what `um sprixen key set` "
                              "writes where there's no keyring), then from SPRIXEN_API_KEY=... in a .env. Keys are never printed in full.");
    kp.help_line = "store / inspect the Sprixen API key (OS keyring first)";
    kp.subcommands("action", false); kp.sub_metavar = "<action>";
    { Parser& q = kp.sub("set"); q.help_line = "store a key from a hidden prompt (or --from-file)"; q.opt({"--from-file"}).metavar("PATH");
      q.opt({"--file-only"}).store_true().help("skip the OS keyring; write the owner-only key file"); }
    { Parser& q = kp.sub("status"); q.help_line = "show every source and which one is active (keys are masked)"; }
    { Parser& q = kp.sub("clear"); q.help_line = "delete the stored key (keyring entry and key file)"; }
}
}  // namespace packtools::modder::sprixen
