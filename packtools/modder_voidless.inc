// modder_voidless.inc - `um voidless`: a C++ port of universal-modder's um/voidless.py (Rehan and universal-modder contributors, MIT).
// Voidless (https://voidless.dev) is a separate paid service: a plan with API access, an API key and credits are needed.
namespace packtools::modder::voidless {
using namespace pyrt;
using namespace packtools::modder;
using sprixen::D;
using sprixen::name_for;

inline const char* DOC =
    "2D game art from Voidless (https://voidless.dev): sprites, 8-way turns, and animation sets that keep one size.\n\n"
    "    um voidless key set                                      # hidden prompt; or set VOIDLESS_API_KEY\n"
    "    um voidless credits                                      # plan and credits left\n"
    "    um voidless sprite \"a rusty scrap drone enemy\" --facing right --name drone\n"
    "    um voidless sprite \"a red health potion\" --type object --size 64 --style pixel-art\n"
    "    um voidless rotate <asset_id>                            # one sprite -> 8 facings (10 credits)\n"
    "    um voidless animate <asset_id> --preset platformer-character   # several clips in one call\n"
    "    um voidless animate <asset_id> --prompt \"walk: walks left; hit: recoils\" --hold-facing\n"
    "    um voidless presets                                      # named animation sets you can ask for\n"
    "    um voidless asset <asset_id>                             # frames, image keys and download urls\n"
    "    um voidless fetch <asset_id> --out assets/gen            # download every frame again, free\n\n"
    "Voidless keeps a generated sprite as an asset, and later calls add frames to it: `rotate` adds the eight facings,\n"
    "`animate` adds clips at the asset's base size. `--hold-facing` ends each clip on the frame it began with, so a\n"
    "directional loop doesn't drift. On an 8-way rotation the left-side clips are mirrored from the right-side ones\n"
    "by default (half the price); `--no-flip-facings` animates them too.\n\n"
    "Voidless is a separate paid service: it needs an account on a plan with API access (Voidless+ in October 2026),\n"
    "an API key (https://voidless.dev/api-docs) and credits. An image costs 1 credit on the default model and up to\n"
    "15 on others (`--model` lists them), an 8-way rotation 10, and an animation set 100 a pass (200 for 1024 px\n"
    "sprites). `--type tileset` is always drawn by gpt-image-2.5 at 5 credits whatever --model says, because the\n"
    "server uses that model for tilesets. `um voidless credits` shows what is left. The key\n"
    "is read from VOIDLESS_API_KEY, else from where `um voidless key set` stored it (the desktop keyring on Linux, an\n"
    "owner-only ~/.secrets/voidless-key elsewhere), else from VOIDLESS_API_KEY=... in a .env.\n"
    "Files land in --out (default assets/gen), and each call appends a line to <out>/voidless_manifest.jsonl (asset\n"
    "and frame ids, prompt, settings, files) so an asset can be traced back to the call that made it.\n";

inline const std::string DEFAULT_URL = "https://api.voidless.dev", DEFAULT_CDN = "https://cdn.voidless.dev";
inline const std::vector<std::string> TYPES = {"object", "character", "character portrait", "artwork", "tileset", "ui"};
inline const std::vector<std::string> MODELS = {"flux2-klein", "gpt-image-2.5", "seedream-5-flash", "nano-banana-2", "retro-diffusion", "flux-juggernaut"};
inline const std::string TILESET_MODEL = "gpt-image-2.5";
inline const std::vector<std::string> DIMENSIONS = {"2d", "top down", "isometric"};
inline const std::vector<std::string> SIZES = {"32", "64", "128", "512", "1024"};
inline const std::vector<std::string> FACINGS = {"forward", "right"}, POSES = {"idle", "tpose"}, QUALITIES = {"high", "ultra"}, BODY_TYPES = {"humanoid", "quadruped"};
inline const std::vector<std::string> KEYRING_ATTRS = {"service", "voidless.dev", "key", "api"};

// ------------------------------------------------------------------ key
inline std::string key_file() { const std::string n = env("UM_VOIDLESS_KEY_FILE"); return expanduser(!n.empty() ? n : "~/.secrets/voidless-key"); }
inline std::optional<std::string> read_dotenv(const std::string& p) {
    if (!isfile(p)) return std::nullopt;
    const auto m = re::search(R"(^\s*VOIDLESS_API_KEY\s*=\s*['"]?([^'"\s]+))", slurp(p), 8);
    if (!m.ok) return std::nullopt; return m.group(1);
}
inline std::vector<std::pair<std::string, std::function<std::optional<std::string>()>>> key_candidates() {
    std::vector<std::pair<std::string, std::function<std::optional<std::string>()>>> c;
    const std::string named = env("UM_VOIDLESS_KEY_FILE"), kf = key_file();
    c.emplace_back("VOIDLESS_API_KEY environment variable", [] { const std::string v = strip(env("VOIDLESS_API_KEY")); return v.empty() ? std::nullopt : std::optional<std::string>(v); });
    if (!named.empty()) c.emplace_back("key file " + kf + " (UM_VOIDLESS_KEY_FILE)", [kf] { return fal::read_file_key(kf); });
    c.emplace_back("OS keyring (" + join(KEYRING_ATTRS, " ") + ")", [] { return fal::secret_lookup(KEYRING_ATTRS); });
    if (named.empty()) c.emplace_back("key file " + kf, [kf] { return fal::read_file_key(kf); });
    std::error_code ec; const std::string dot = u8native(fs::current_path(ec) / ".env");
    c.emplace_back(".env " + dot, [dot] { return read_dotenv(dot); });
    return c;
}
inline std::string& key_cache() { static std::string k; return k; }
inline std::string api_key() {
    if (key_cache().empty()) for (const auto& [label, read] : key_candidates()) if (const auto k = read()) { key_cache() = *k; break; }
    if (key_cache().empty()) die("no Voidless key. Create one at https://voidless.dev/api-docs, then `um voidless key set` (or set VOIDLESS_API_KEY)");
    return key_cache();
}
inline void cmd_key(const Val& a) {
    const std::string action = S(a, "action");
    if (action == "set") {
        const std::string value = !S(a, "from_file").empty() ? strip(slurp(expanduser(S(a, "from_file")))) : strip(getpass_line("Voidless API key (hidden): "));
        if (value.empty()) die("empty key, nothing stored");
        if (!starts(value, "void_")) eprint("note: Voidless keys start with void_; storing it anyway");
        if (truthy(get(a, "file_only"))) print("stored in " + fal::write_key_file(value, key_file()) + " as " + fal::mask(value) + " (plaintext, owner-only)");
        else if (fal::secret_store("Voidless API key (universal-modder)", KEYRING_ATTRS, value) && fal::secret_lookup(KEYRING_ATTRS) == value)
            print("stored in the OS keyring as " + fal::mask(value) + " (" + join(KEYRING_ATTRS, " ") + ")");
        else if (fal::keyring_available()) die("could not write to the OS keyring (is it unlocked?); `um voidless key set --file-only` stores the key in a plaintext file instead");
        else print("no OS keyring (no secret-tool); stored in " + fal::write_key_file(value, key_file()) + " as " + fal::mask(value) + " (plaintext, owner-only)");
        if (!env("VOIDLESS_API_KEY").empty()) print("note: VOIDLESS_API_KEY is set in this environment and takes precedence over the stored key");
    } else if (action == "clear") {
        std::vector<std::string> removed;
        if (fal::secret_lookup(KEYRING_ATTRS) && fal::secret_clear(KEYRING_ATTRS)) removed.push_back("OS keyring");
        const std::string kf = key_file();
        if (pexists(kf)) { std::error_code ec; fs::remove(P(kf), ec); removed.push_back(kf); }
        print(removed.empty() ? "nothing to remove" : "removed: " + join(removed, ", "));
    } else {
        std::vector<std::pair<std::string, std::string>> src;
        for (const auto& [label, read] : key_candidates()) if (const auto k = read()) src.emplace_back(label, *k);
        if (src.empty()) print("no Voidless key configured; run `um voidless key set`");
        for (std::size_t i = 0; i < src.size(); ++i) print(std::string(i == 0 ? "active  " : "shadowed") + "  " + src[i].first + "  ->  " + fal::mask(src[i].second));
        const std::string kf = key_file();
        print(std::string("keyring     ") + (fal::keyring_available() ? "available" : "not available (no secret-tool)"));
        print("key file    " + kf + " " + (pexists(kf) ? "exists (mode " + fal::file_mode(kf) + ")" : std::string("absent")));
    }
}

// ------------------------------------------------------------------ http
inline std::string base_url(const std::string& url) { const std::string e = env("VOIDLESS_API_URL"); return comfy::rstrip_slash(!url.empty() ? url : !e.empty() ? e : DEFAULT_URL); }
inline std::optional<std::string> cdn_url(const Val& key) {
    if (key.t != Val::STR || key.s.empty() || starts(key.s, "gdrive:")) return std::nullopt;
    if (re::search("^https?://", key.s).ok) return key.s;
    const std::string e = env("VOIDLESS_CDN_URL"); const std::string cdn = comfy::rstrip_slash(!e.empty() ? e : DEFAULT_CDN);
    std::string k = key.s; while (!k.empty() && k[0] == '/') k.erase(0, 1);
    return cdn + "/" + k;
}
inline std::string error_line(long code, const std::string& body) {
    Val err; try { err = loads(decode_utf8_replace(body)); } catch (const PyErr&) { return "HTTP " + std::to_string(code) + ": " + decode_utf8_replace(body.substr(0, 500)); }
    if (err.t != Val::DICT) err = Val::dict();
    const Val e = get(err, "error"); const Val msg = e.t == Val::DICT ? get(e, "message") : Val();
    std::string m = truthy(msg) ? pystr(msg) : truthy(get(err, "message")) ? pystr(get(err, "message")) : truthy(e) ? pystr(e) : decode_utf8_replace(body.substr(0, 200));
    return "HTTP " + std::to_string(code) + ": " + m;
}
inline Val request(const std::string& base, const std::string& method, const std::string& path, const Val* body = nullptr, int retries = 3, double timeout = 900) {
    std::string data; const std::string* dp = nullptr; if (body) { data = dumps(*body); dp = &data; }
    const bool post = method == "POST";
    for (int attempt = 0; attempt <= retries; ++attempt) {
        std::vector<std::string> h = {"x-api-key: " + api_key(), "Accept: application/json", "User-Agent: universal-modder"};
        if (dp) h.push_back("Content-Type: application/json");
        const UmResp r = um_http(method, base + "/api" + path, h, dp, timeout);
        if (r.status == 0) {
            if (r.error.find("imeout") != std::string::npos) die("Voidless " + method + " " + path + " failed: " + r.error);
            die("no Voidless API at " + base + " (" + r.error + ")");
        }
        if (r.status >= 200 && r.status < 400) {
            if (r.body.empty()) return Val::dict();
            try { return loads(decode_utf8_replace(r.body)); } catch (const PyErr&) { die("Voidless " + method + " " + path + " answered with something that isn't JSON"); }
        }
        const std::string line = error_line(r.status, r.body);
        // a POST is retried only on 429 (refused before any work): after a gateway 502/503/504 the job may still finish and be charged
        const bool retry = r.status == 429 || (!post && (r.status == 502 || r.status == 503 || r.status == 504));
        if (attempt < retries && retry) {
            const double w = 15.0 * (attempt + 1);
            eprint("um: " + line + "; retrying in " + fmt_f(w, 0) + " s"); sleep_s(w); continue;
        }
        if (post && (r.status == 502 || r.status == 503 || r.status == 504))
            die("Voidless " + method + " " + path + " -> " + line + ". The job may still finish and be charged: check `um voidless credits` and your assets on voidless.dev before running it again");
        die("Voidless " + method + " " + path + " -> " + line);
    }
    return Val();
}
inline std::string download(const std::string& url, const std::string& path) {
    const UmResp r = um_http("GET", url, {"User-Agent: universal-modder"}, nullptr, 300);
    if (r.status < 200 || r.status >= 300) die("could not download " + url + ": " + (r.status ? "HTTP Error " + std::to_string(r.status) : r.error));
    spit(path, r.body); return path;
}
inline std::vector<std::string> save_keys(const std::string& out, const std::string& stem, const Val& keys, const std::string& label) {
    std::vector<std::string> saved, urls;
    if (keys.t == Val::LIST) for (const auto& k : *keys.l) if (const auto u = cdn_url(k)) urls.push_back(*u);
    for (std::size_t i = 0; i < urls.size(); ++i) {
        std::string ext; { const std::string b = urls[i].substr(0, urls[i].find('?')); const std::string last = b.substr(b.rfind('/') + 1); const auto dot = last.rfind('.'); if (dot != std::string::npos && dot > 0 && dot + 1 < last.size()) ext = last.substr(dot); }
        if (ext.empty()) ext = ".png";
        char suf[32] = ""; if (urls.size() > 1) std::snprintf(suf, sizeof suf, "_%02zu", i);
        const std::string p = download(urls[i], path_join(out, stem + suf + ext));
        saved.push_back(p); eprint("um: " + label + " -> " + p);
    }
    return saved;
}
inline std::string cost(const Val& d) { const Val c = truthy(get(d, "cost")) ? get(d, "cost") : get(d, "credits"); return truthy(c) ? " (" + pystr(c) + " credits)" : ""; }
inline Val strs(const std::vector<std::string>& v) { List l; for (const auto& s : v) l.push_back(Val::str(s)); return Val::list(l); }
inline void record(const std::string& out, const Val& rec) { sprixen::append_manifest(out, "voidless_manifest.jsonl", rec); }
inline Val frames_of(const Val& asset) { const Val md = truthy(get(asset, "meta_data")) ? get(asset, "meta_data") : Val::dict(); const Val f = get(md, "frames"); return f.t == Val::LIST ? f : Val::list(); }
inline std::string clip_name(const Val& f, const std::string& dflt) { const Val n = truthy(get(f, "name")) ? get(f, "name") : truthy(get(f, "type")) ? get(f, "type") : Val::str(dflt); return name_for("", pystr(n), "sprite"); }

// ------------------------------------------------------------------ commands
inline void cmd_credits(const Val& a, const std::string& base) {
    const Val me = request(base, "GET", "/users/me");
    if (truthy(get(a, "json"))) { emit_json(me); return; }
    for (const char* f : {"username", "email", "tier", "subscribed", "credits"}) if (!get(me, f).is_none()) print(ljust(f, 12) + " " + pystr(get(me, f)));
}
inline void cmd_presets(const Val& a, const std::string& base) {
    const Val data = request(base, "GET", "/generate/multi-animation/presets");
    if (truthy(get(a, "json"))) { emit_json(data); return; }
    const Val presets = data.t == Val::LIST ? data : (get(data, "presets").t == Val::LIST ? get(data, "presets") : Val::list());
    for (const auto& p : *presets.l) {
        if (p.t == Val::DICT) {
            std::vector<std::string> acts; const Val al = get(p, "actions"); if (al.t == Val::LIST) for (const auto& x : *al.l) acts.push_back(pystr(x));
            print(ljust(pystr(get(p, "key", Val::str("?"))), 24) + " " + (truthy(get(p, "label")) ? pystr(get(p, "label")) : ""));
            if (!acts.empty()) print(std::string(24, ' ') + "   " + join(acts, ", "));
        } else print(pystr(p));
    }
}
inline void cmd_asset(const Val& a, const std::string& base) {
    const Val asset = request(base, "GET", "/assets/" + pystr(get(a, "asset_id")));
    if (truthy(get(a, "json"))) { emit_json(asset); return; }
    print("asset   " + pystr(get(asset, "id")) + "  " + (truthy(get(asset, "name")) ? pystr(get(asset, "name")) : ""));
    if (const auto u = cdn_url(get(asset, "file"))) print("base    " + *u);
    const Val fr = frames_of(asset);
    for (std::size_t i = 0; i < fr.size(); ++i) {
        const Val& f = fr[i]; const Val imgs = get(f, "images").t == Val::LIST ? get(f, "images") : Val::list();
        const Val nm = truthy(get(f, "name")) ? get(f, "name") : truthy(get(f, "type")) ? get(f, "type") : Val::str("?");
        print("frame   id=" + pystr(get(f, "id", Val::integer(static_cast<long long>(i)))) + " " + pystr(nm) + "  " + std::to_string(imgs.size()) + " image(s)");
        if (truthy(get(a, "urls"))) for (const auto& k : *imgs.l) if (const auto u = cdn_url(k)) print("          " + *u);
    }
}
inline void cmd_sprite(const Val& a, const std::string& base, const std::string& out) {
    std::string model = S(a, "model");
    if (S(a, "type") == "tileset" && model != TILESET_MODEL) { eprint("um: tilesets are always generated with " + TILESET_MODEL + " (5 credits); ignoring --model " + model); model = TILESET_MODEL; }
    // every one of prompt/colours/dimension/size/type/model/seed is required: the endpoint answers 422 rather than filling anything in
    const Val body = D({{"prompt", get(a, "prompt")}, {"type", get(a, "type")}, {"model", Val::str(model)}, {"seed", get(a, "seed")}, {"dimension", get(a, "dimension")},
                        {"size", get(a, "size")}, {"colours", get(a, "colours")}, {"palette", Val::list()}, {"style", get(a, "style")},
                        {"referenceImage", Val::str("")}, {"styleImage", Val::str("")},
                        {"options", D({{"facing", get(a, "facing")}, {"pose", get(a, "pose")}, {"bodyType", get(a, "body_type")}, {"aspect", get(a, "aspect")}, {"mode", Val::str("")}})}});
    const Val data = request(base, "POST", "/generate/image", &body, 3, to_float(get(a, "timeout")));
    const auto saved = save_keys(out, name_for(S(a, "name"), S(a, "prompt"), "sprite"), Val::list({get(data, "file")}), "sprite");
    const Val asset_id = get(data, "assetId");
    eprint("um: asset " + pystr(asset_id) + cost(data) + "  (pass it to `um voidless rotate` or `animate`)");
    record(out, D({{"cmd", Val::str("sprite")}, {"asset", asset_id}, {"prompt", get(a, "prompt")}, {"type", get(a, "type")}, {"model", Val::str(model)},
                   {"dimension", get(a, "dimension")}, {"size", get(a, "size")}, {"style", get(a, "style")}, {"seed", get(data, "seed")}, {"files", strs(saved)}}));
}
inline void cmd_rotate(const Val& a, const std::string& base, const std::string& out) {
    const Val body = D({{"id", get(a, "asset_id")}, {"referenceFile", Val::str("")}});
    const Val data = request(base, "POST", "/generate/image-rotations", &body, 3, to_float(get(a, "timeout")));
    std::vector<std::string> saved; const Val fr = get(data, "frames");
    if (fr.t == Val::LIST) for (const auto& f : *fr.l) { const auto s = save_keys(out, name_for(S(a, "name"), pystr(get(a, "asset_id")) + " rotations", "sprite"), get(f, "images"), "rotation"); saved.insert(saved.end(), s.begin(), s.end()); }
    eprint("um: 8-way rotation on asset " + pystr(get(a, "asset_id")) + cost(data));
    record(out, D({{"cmd", Val::str("rotate")}, {"asset", get(a, "asset_id")}, {"files", strs(saved)}}));
}
inline void cmd_animate(const Val& a, const std::string& base, const std::string& out) {
    if (S(a, "preset").empty() && S(a, "prompt").empty()) die("give --preset (see `um voidless presets`) or --prompt \"name: what happens; name: what happens\"");
    // the endpoint animates a list of images: items = [{assetId, frameIndex?, imageIndex?}]
    Val item = D({{"assetId", get(a, "asset_id")}});
    if (!get(a, "frame_id").is_none()) item.d->set("frameIndex", get(a, "frame_id"));
    if (!get(a, "image_index").is_none()) item.d->set("imageIndex", get(a, "image_index"));
    Val body = D({{"items", Val::list({item})}, {"preset", Val::str(S(a, "preset"))}, {"prompt", Val::str(S(a, "prompt"))}, {"holdFacing", get(a, "hold_facing")},
                  {"flipFacings", Val::boolean(!truthy(get(a, "no_flip_facings")))}, {"autoFlip", get(a, "auto_flip")}});
    if (!S(a, "quality").empty()) body.d->set("quality", get(a, "quality"));
    const Val data = request(base, "POST", "/generate/multi-animation", &body, 3, to_float(get(a, "timeout")));
    std::vector<std::string> saved; List skipped;
    const Val results = get(data, "results").t == Val::LIST ? get(data, "results") : Val::list();
    for (const auto& r : *results.l) {
        const Val fr = get(r, "frames");
        if (fr.t == Val::LIST) for (const auto& f : *fr.l) {
            const std::string clip = clip_name(f, "clip");
            const auto s = save_keys(out, name_for(S(a, "name"), pystr(get(a, "asset_id")), "sprite") + "_" + clip, get(f, "images"), "clip " + clip);
            saved.insert(saved.end(), s.begin(), s.end());
        }
    }
    for (const auto& r : *results.l) { const Val sk = get(r, "skipped"); if (sk.t == Val::LIST) for (const auto& s : *sk.l) skipped.push_back(s); }
    if (!skipped.empty()) eprint("um: skipped " + dumps(Val::list(skipped)));
    eprint("um: animation set on asset " + pystr(get(a, "asset_id")) + cost(data) + (truthy(get(data, "quality")) ? "  quality=" + pystr(get(data, "quality")) : ""));
    record(out, D({{"cmd", Val::str("animate")}, {"asset", get(a, "asset_id")}, {"preset", get(a, "preset")}, {"prompt", get(a, "prompt")},
                   {"quality", get(a, "quality")}, {"hold_facing", get(a, "hold_facing")}, {"files", strs(saved)}}));
}
inline void cmd_fetch(const Val& a, const std::string& base, const std::string& out) {
    const Val asset = request(base, "GET", "/assets/" + pystr(get(a, "asset_id")));
    const std::string stem = name_for(S(a, "name"), truthy(get(asset, "name")) ? pystr(get(asset, "name")) : pystr(get(a, "asset_id")), "sprite");
    std::vector<std::string> saved;
    if (!truthy(get(a, "frames_only"))) saved = save_keys(out, stem, Val::list({get(asset, "file")}), "base");
    const Val fr = frames_of(asset);
    for (std::size_t i = 0; i < fr.size(); ++i) {
        const Val& f = fr[i];
        if (!get(a, "frame_id").is_none()) { const Val id = get(f, "id"); if (!(id.is_real() && id.num() == get(a, "frame_id").num() && id.t != Val::BOOL)) continue; }
        const std::string clip = clip_name(f, "frame" + std::to_string(i));
        const auto s = save_keys(out, stem + "_" + clip, get(f, "images"), "frame " + clip); saved.insert(saved.end(), s.begin(), s.end());
    }
    eprint("um: " + std::to_string(saved.size()) + " file(s) from asset " + pystr(get(a, "asset_id")) + " (no credits spent)");
    record(out, D({{"cmd", Val::str("fetch")}, {"asset", get(a, "asset_id")}, {"files", strs(saved)}}));
}

inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "key") { cmd_key(a); return 0; }
    const std::string base = base_url(S(a, "url"));
    const std::string out = has(a, "out") ? S(a, "out") : "assets/gen";
    if (has(a, "out")) makedirs(out);   // credits/presets/asset only read: no folder for them
    if (c == "credits") cmd_credits(a, base);
    else if (c == "presets") cmd_presets(a, base);
    else if (c == "asset") cmd_asset(a, base);
    else if (c == "fetch") cmd_fetch(a, base, out);
    else if (c == "sprite") cmd_sprite(a, base, out);
    else if (c == "rotate") cmd_rotate(a, base, out);
    else if (c == "animate") cmd_animate(a, base, out);
    return 0;
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("voidless", DOC); p.help_line = "2D sprites with Voidless: 8-way turns and animation sets that keep one size"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    auto cmd = [&](const char* name, const char* help, bool out = true) -> Parser& {
        Parser& q = p.sub(name); q.help_line = help;
        q.opt({"--url"}).help("API address (default: $VOIDLESS_API_URL or " + DEFAULT_URL + ")");
        if (out) {
            q.opt({"--out"}).def("assets/gen").help("output folder (default assets/gen)"); q.opt({"--name"}).help("output file stem");
            q.opt({"--timeout"}).number().def(900LL).help("seconds to wait for the result");
        }
        return q;
    };
    { Parser& q = cmd("credits", "plan and credits left", false); q.opt({"--json"}).store_true(); }
    { Parser& q = cmd("presets", "named animation sets `animate --preset` accepts", false); q.opt({"--json"}).store_true(); }
    { Parser& q = cmd("asset", "frames, image keys and download urls for one asset", false); q.pos("asset_id").integer();
      q.opt({"--urls"}).store_true().help("print every image url"); q.opt({"--json"}).store_true(); }
    { Parser& q = cmd("fetch", "download an asset's frames again without generating (free)"); q.pos("asset_id").integer();
      q.opt({"--frame-id"}).integer().help("only this frame"); q.opt({"--frames-only"}).store_true().help("skip the base image"); }
    { Parser& q = cmd("sprite", "text -> sprite on a transparent background (1 credit on the default model)"); q.pos("prompt");
      q.opt({"--type"}).def("character").choices(TYPES).help("'character' is a full-body sprite; 'object' props and items; 'artwork' scenes");
      q.opt({"--model"}).def("flux2-klein").choices(MODELS).help("flux2-klein (1 credit) unless you need another: flux-juggernaut 1, seedream-5-flash 2, gpt-image-2.5 5, "
                                                                "retro-diffusion 10, nano-banana-2 15. --type tileset always uses gpt-image-2.5 whatever you pass");
      q.opt({"--size"}).integer().def(512LL).choices(SIZES).help("output px: 32/64/128 read as pixel art, 512 otherwise, 1024 high-res");
      q.opt({"--dimension"}).def("2d").choices(DIMENSIONS).help("'2d' side view, 'top down' overhead, 'isometric' 2.5D");
      q.opt({"--colours"}).integer().def(8LL).help("colour count for pixel-art reduction (4-12)");
      q.opt({"--style"}).def("").help("style preset, e.g. pixel-art, cartoon, anime, hand-drawn");
      q.opt({"--facing"}).def("forward").choices(FACINGS).help("characters: front view or side profile. Side profiles rotate best, and a left-facing sprite is a mirrored right one, so generate 'right' and flip");
      q.opt({"--pose"}).def("idle").choices(POSES).help("'idle' for animation sets and rotations; 'tpose' only for rigging");
      q.opt({"--body-type"}).def("humanoid").choices(BODY_TYPES).help("characters: body plan");
      q.opt({"--aspect"}).def("1:1").choices({"1:1", "16:9"}).help("16:9 only applies to --type artwork");
      q.opt({"--seed"}).integer().def(0LL).help("0 = random"); }
    { Parser& q = cmd("rotate", "asset id -> the eight facings as one frame (10 credits)"); q.pos("asset_id").integer(); }
    { Parser& q = cmd("animate", "asset id -> an animation set, every clip at the asset's base size (100 credits a pass)"); q.pos("asset_id").integer();
      q.opt({"--preset"}).help("a named set from `um voidless presets`");
      q.opt({"--prompt"}).help("without a preset: 'name: what happens; name: what happens'");
      q.opt({"--hold-facing"}).store_true().help("end each clip on the frame it started from, so a directional loop does not drift");
      q.opt({"--no-flip-facings"}).store_true().help("animate the left-side clips too instead of mirroring them from the right (costs double)");
      q.opt({"--auto-flip"}).store_true().help("afterwards, mirror every right-side clip that has no left twin (free)");
      q.opt({"--quality"}).choices(QUALITIES).help("force a tier: high (100 credits per pass) or ultra (200). Default: the server picks it from the sprite size (200 credits a pass for 1024 px sprites)");
      q.opt({"--frame-id"}).integer().help("animate one frame of the asset (e.g. its 8-way rotation, id from `asset`) instead of its base image");
      q.opt({"--image-index"}).integer().help("with --frame-id: which image of that frame (on a rotation, the facing's position)"); }
    Parser& kp = p.sub("key", "`um voidless` takes the key from VOIDLESS_API_KEY first, then from a file named by UM_VOIDLESS_KEY_FILE, then from the OS keyring "
                              "(libsecret's secret-tool, as `um fal key` uses: Linux), then from ~/.secrets/voidless-key (owner-only, what `um voidless key set` "
                              "writes where there's no keyring), then from VOIDLESS_API_KEY=... in a .env. Keys are never printed in full.");
    kp.help_line = "store / inspect the Voidless API key (OS keyring first)";
    kp.subcommands("action", false); kp.sub_metavar = "<action>";
    { Parser& q = kp.sub("set"); q.help_line = "store a key from a hidden prompt (or --from-file)"; q.opt({"--from-file"}).metavar("PATH");
      q.opt({"--file-only"}).store_true().help("skip the OS keyring; write the owner-only key file"); }
    { Parser& q = kp.sub("status"); q.help_line = "show every source and which one is active (keys are masked)"; }
    { Parser& q = kp.sub("clear"); q.help_line = "delete the stored key (keyring entry and key file)"; }
}
}  // namespace packtools::modder::voidless
