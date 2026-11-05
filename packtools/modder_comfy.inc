// modder_comfy.inc - `um comfy`: a C++ port of universal-modder's um/comfy.py (Rehan and universal-modder contributors, MIT).
// Images from a local ComfyUI server over its plain HTTP API: no key, the user's own GPU.
namespace packtools::modder::comfy {
using namespace pyrt;
using namespace packtools::modder;

inline const char* DOC =
    "Images from a local ComfyUI server: no API key, runs on your own GPU (https://www.comfy.org).\n\n"
    "    um comfy status                                   # is ComfyUI up? which checkpoints can it load?\n"
    "    um comfy image \"a rusty scrap drone enemy, side view, 16-bit pixel art\" --name drone\n"
    "    um comfy image \"a red health potion\" --sprite     # plain background, then cut out locally (um sprite cutout)\n"
    "    um comfy image \"mossy dungeon wall\" --checkpoint sd_xl_base_1.0.safetensors --size 1024x1024 --steps 30\n"
    "    um comfy run my_workflow.json --set 6.text=\"a blue mana potion\" --set 3.seed:=42 --name potion\n\n"
    "Start ComfyUI first. It listens on http://127.0.0.1:8188; use --url or COMFYUI_URL for another address.\n"
    "`image` runs ComfyUI's default text-to-image graph (checkpoint, two prompts, KSampler, VAE decode), which\n"
    "fits SD 1.5 and SDXL checkpoints. Without --checkpoint it takes the first one the server lists. For anything\n"
    "else (Flux, img2img, ControlNet, LoRAs), build the graph in ComfyUI, save it with Export (API) and run it\n"
    "with `um comfy run`. --set NODE.INPUT=value changes a node input first: NODE is the node id or its title,\n"
    "and := takes JSON (numbers, true/false). Files land in --out (default assets/gen), and each call appends a\n"
    "line to <out>/comfy_manifest.jsonl (workflow, seed, files) so an image can be traced and made again.\n";

inline const std::string DEFAULT_URL = "http://127.0.0.1:8188";
inline const std::string NEGATIVE = "blurry, low quality, jpeg artifacts, watermark, text, signature";

inline std::string rstrip_slash(std::string s) { while (!s.empty() && s.back() == '/') s.pop_back(); return s; }
inline std::string base_url(const std::string& url) { const std::string e = env("COMFYUI_URL"); return rstrip_slash(!url.empty() ? url : !e.empty() ? e : DEFAULT_URL); }

inline std::string describe(const Val& err) {
    std::vector<std::string> lines;
    const Val e = get(err, "error");
    if (e.t == Val::DICT) {
        std::vector<std::string> parts; for (const char* k : {"message", "details"}) if (truthy(get(e, k))) parts.push_back(pystr(get(e, k)));
        lines.push_back(join(parts, " "));
    } else if (truthy(e)) lines.push_back(pystr(e));
    const Val ne_all = truthy(get(err, "node_errors")) ? get(err, "node_errors") : Val::dict();
    if (ne_all.t == Val::DICT) for (const auto& [node, ne] : ne_all.d->items) {
        const Val errs = get(ne, "errors", Val::list()); if (errs.t != Val::LIST) continue;
        for (const auto& x : *errs.l) {
            std::string l = "node " + node + " (" + pystr(get(ne, "class_type", Val::str("?"))) + "): " + pystr(get(x, "message", Val::str(""))) + ": " + pystr(get(x, "details", Val::str("")));
            while (!l.empty() && (l.back() == ':' || l.back() == ' ')) l.pop_back();
            lines.push_back(l);
        }
    }
    const std::string o = join(lines, "\n  ");
    return !o.empty() ? o : cpslice(dumps(err), 0, 1500);
}
// nullopt only for a 404 with missing_ok
inline std::optional<std::string> req_raw(const std::string& base, const std::string& path, const Val* body = nullptr, double timeout = 60, bool missing_ok = false) {
    std::vector<std::string> h; std::string data; const std::string* dp = nullptr;
    if (body) { data = dumps(*body); dp = &data; h.push_back("Content-Type: application/json"); }
    const UmResp r = um_http(body ? "POST" : "GET", base + path, h, dp, timeout);
    if (r.status >= 200 && r.status < 400) return r.body;
    if (r.status == 0) die("no ComfyUI at " + base + " (" + r.error + "). Start it (python main.py in the ComfyUI folder), or pass --url / set COMFYUI_URL");
    if (missing_ok && r.status == 404) return std::nullopt;
    const std::string detail = decode_utf8_replace(r.body);
    Val err; bool ok = true; try { err = loads(detail); } catch (const PyErr&) { ok = false; }
    if (ok && path == "/prompt" && err.t == Val::DICT && (truthy(get(err, "error")) || truthy(get(err, "node_errors")))) die("ComfyUI rejected the workflow:\n  " + describe(err));
    die("ComfyUI " + path.substr(0, path.find('?')) + " -> HTTP " + std::to_string(r.status) + ": " + cpslice(detail, 0, 1500));
    return std::nullopt;
}
inline Val req(const std::string& base, const std::string& path, const Val* body = nullptr, double timeout = 60, bool missing_ok = false) {
    const auto p = req_raw(base, path, body, timeout, missing_ok);
    if (!p) return Val();
    return p->empty() ? Val::dict() : loads(decode_utf8_replace(*p));
}

inline std::vector<std::string> combo(const Val& spec) {
    std::vector<std::string> o;
    if (spec.t == Val::LIST && spec.size()) {
        if (spec[0].t == Val::LIST) { for (const auto& x : *spec[0].l) o.push_back(pystr(x)); return o; }
        if (spec.size() > 1 && spec[1].t == Val::DICT) { const Val opts = get(spec[1], "options", Val::list()); if (opts.t == Val::LIST) for (const auto& x : *opts.l) o.push_back(pystr(x)); }
    }
    return o;
}
inline std::vector<std::string> checkpoints(const std::string& base) {
    const Val names = req(base, "/models/checkpoints", nullptr, 60, true);   // older servers have no /models route
    std::vector<std::string> o;
    if (names.t == Val::LIST) { for (const auto& x : *names.l) o.push_back(pystr(x)); return o; }
    const Val info = req(base, "/object_info/CheckpointLoaderSimple");
    return combo(get(get(get(get(info, "CheckpointLoaderSimple", Val::dict()), "input", Val::dict()), "required", Val::dict()), "ckpt_name"));
}
inline Val status(const std::string& base) {
    const Val stats = req(base, "/system_stats"); const Val system = get(stats, "system", Val::dict());
    Val o = Val::dict(); o.d->set("url", Val::str(base)); o.d->set("version", get(system, "comfyui_version")); o.d->set("pytorch", get(system, "pytorch_version"));
    List dev; const Val devs = get(stats, "devices", Val::list());
    if (devs.t == Val::LIST) for (const auto& d : *devs.l) dev.push_back(Val::str(pystr(get(d, "name")) + " (" + pystr(get(d, "type")) + ")"));
    o.d->set("devices", Val::list(dev));
    List ck; for (const auto& c : checkpoints(base)) ck.push_back(Val::str(c)); o.d->set("checkpoints", Val::list(ck));
    return o;
}

inline Val node(const char* cls, std::initializer_list<std::pair<const char*, Val>> in) { Val n = Val::dict(); n.d->set("class_type", Val::str(cls)); Val i = Val::dict(); for (const auto& [k, v] : in) i.d->set(k, v); n.d->set("inputs", i); return n; }
inline Val link(const char* id, long long slot) { return Val::list({Val::str(id), Val::integer(slot)}); }
inline Val txt2img(const std::string& prompt, const std::string& ckpt, const std::string& negative, long long w, long long h, long long steps, double cfg,
                   long long seed, const std::string& sampler, const std::string& scheduler, long long n) {
    Val wf = Val::dict();
    wf.d->set("4", node("CheckpointLoaderSimple", {{"ckpt_name", Val::str(ckpt)}}));
    wf.d->set("5", node("EmptyLatentImage", {{"width", Val::integer(w)}, {"height", Val::integer(h)}, {"batch_size", Val::integer(n)}}));
    wf.d->set("6", node("CLIPTextEncode", {{"text", Val::str(prompt)}, {"clip", link("4", 1)}}));
    wf.d->set("7", node("CLIPTextEncode", {{"text", Val::str(negative)}, {"clip", link("4", 1)}}));
    wf.d->set("3", node("KSampler", {{"seed", Val::integer(seed)}, {"steps", Val::integer(steps)}, {"cfg", Val::flt(cfg)}, {"sampler_name", Val::str(sampler)},
                                     {"scheduler", Val::str(scheduler)}, {"denoise", Val::flt(1.0)}, {"model", link("4", 0)}, {"positive", link("6", 0)},
                                     {"negative", link("7", 0)}, {"latent_image", link("5", 0)}}));
    wf.d->set("8", node("VAEDecode", {{"samples", link("3", 0)}, {"vae", link("4", 2)}}));
    wf.d->set("9", node("SaveImage", {{"filename_prefix", Val::str("um")}, {"images", link("8", 0)}}));
    return wf;
}
inline Val load_workflow(const std::string& path) {
    if (!isfile(path)) die("no such workflow: " + path);
    Val wf;
    try { wf = loads(decode_utf8_replace(slurp(path))); } catch (const PyErr& e) { die(path + " is not JSON: " + e.what()); }
    if (wf.t == Val::DICT && has(wf, "nodes") && has(wf, "links")) die(path + " is a UI workflow. In ComfyUI, save it with Export (API) and run that file");
    if (wf.t == Val::DICT && get(wf, "prompt").t == Val::DICT) wf = get(wf, "prompt");
    bool ok = wf.t == Val::DICT && wf.size() > 0;
    if (ok) for (const auto& [k, v] : wf.d->items) if (!(v.t == Val::DICT && has(v, "class_type"))) ok = false;
    if (!ok) die(path + " is not an API-format workflow (a JSON object of nodes, each with class_type and inputs)");
    return wf;
}
inline std::string title_of(const Val& v) { const Val m = get(v, "_meta", Val::dict()); return pystr(get(m, "title", Val::str(""))); }
inline std::string node_ref(const Val& wf, const std::string& ref) {
    if (has(wf, ref)) return ref;
    std::vector<std::string> hits;
    for (const auto& [k, v] : wf.d->items) if (lower(title_of(v)) == lower(ref)) hits.push_back(k);
    if (hits.size() == 1) return hits[0];
    std::vector<std::string> known;
    for (const auto& [k, v] : wf.d->items) { const Val t = get(get(v, "_meta", Val::dict()), "title"); known.push_back(k + " (" + (truthy(t) ? pystr(t) : pystr(get(v, "class_type"))) + ")"); }
    die(std::string(hits.empty() ? "no node is" : "several nodes are") + " called " + repr(Val::str(ref)) + "; nodes: " + join(known, ", "));
    return "";
}
inline Val apply_set(const Val& wf0, const Val& sets) {
    Val wf = loads(dumps(wf0));
    if (sets.t == Val::LIST) for (const auto& sv : *sets.l) {
        const std::string s = sv.s; const auto eqp = s.find('=');
        std::string left = eqp == std::string::npos ? s : s.substr(0, eqp); const std::string raw = eqp == std::string::npos ? "" : s.substr(eqp + 1);
        const bool json_val = ends(left, ":"); if (json_val) left.pop_back();
        const auto dot = left.rfind('.');
        const std::string ref = dot == std::string::npos ? "" : left.substr(0, dot), key = dot == std::string::npos ? left : left.substr(dot + 1);
        if (eqp == std::string::npos || dot == std::string::npos || ref.empty() || key.empty()) die("bad --set " + repr(Val::str(s)) + ": use NODE.INPUT=value or NODE.INPUT:=json");
        Val val = Val::str(raw);
        if (json_val) { try { val = loads(raw); } catch (const PyErr&) { die("--set " + repr(Val::str(s)) + ": " + repr(Val::str(raw)) + " is not JSON"); } }
        Val* n = wf.d->get(node_ref(wf, ref));
        if (!has(*n, "inputs")) n->d->set("inputs", Val::dict());
        n->d->get("inputs")->d->set(key, val);
    }
    return wf;
}
inline std::string uuid_hex() { std::random_device rd; std::mt19937_64 g64((static_cast<uint64_t>(rd()) << 32) ^ rd()); char b[40]; std::snprintf(b, sizeof b, "%016llx%016llx", static_cast<unsigned long long>(g64()), static_cast<unsigned long long>(g64())); return b; }
inline std::string queue(const std::string& base, const Val& wf) {
    Val body = Val::dict(); body.d->set("prompt", wf); body.d->set("client_id", Val::str(uuid_hex()));
    const Val res = req(base, "/prompt", &body);
    if (truthy(get(res, "node_errors"))) die("ComfyUI rejected the workflow:\n  " + describe(res));
    if (!truthy(get(res, "prompt_id"))) die("ComfyUI didn't queue the workflow: " + cpslice(dumps(res), 0, 1500));
    return pystr(get(res, "prompt_id"));
}
inline Val wait(const std::string& base, const std::string& pid, double timeout = 1800, double poll = 1.0) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(timeout * 1000));
    while (std::chrono::steady_clock::now() < end) {
        const Val h = get(req(base, "/history/" + pid), pid);
        if (truthy(h)) {
            const Val st = get(h, "status", Val::dict());
            if (pystr(get(st, "status_str")) == "error") {
                Val e = Val::dict(); const Val msgs = get(st, "messages", Val::list());
                if (msgs.t == Val::LIST) for (const auto& m : *msgs.l) if (m.t == Val::LIST && m.size() == 2 && m[0].t == Val::STR && m[0].s == "execution_error") { e = m[1]; break; }
                die(strip("ComfyUI failed in node " + pystr(get(e, "node_id", Val::str("?"))) + " (" + pystr(get(e, "node_type", Val::str("?"))) + "): " + pystr(get(e, "exception_message", Val::str("see the ComfyUI console")))));
            }
            if (truthy(get(st, "completed", Val::boolean(true)))) return h;
        }
        sleep_s(poll);
    }
    die("ComfyUI didn't finish in " + fmt_f(timeout, 0) + " s (prompt " + pid + "); it is still queued or running");
    return Val();
}
inline std::vector<Val> outputs(const Val& history) {
    std::vector<Val> files; const Val outs = get(history, "outputs", Val::dict()); if (outs.t != Val::DICT) return files;
    std::vector<std::string> keys; for (const auto& [k, v] : outs.d->items) keys.push_back(k);
    std::sort(keys.begin(), keys.end(), [](const std::string& x, const std::string& y) { return cplen(x) != cplen(y) ? cplen(x) < cplen(y) : x < y; });
    for (const auto& k : keys) { const Val nd = get(outs, k); if (nd.t != Val::DICT) continue;
        for (const auto& [kk, items] : nd.d->items) if (items.t == Val::LIST) for (const auto& x : *items.l) if (x.t == Val::DICT && truthy(get(x, "filename"))) files.push_back(x); }
    return files;
}
inline std::string qs(const std::vector<std::pair<std::string, std::string>>& kv) { std::string o; for (const auto& [k, v] : kv) { if (!o.empty()) o += '&'; o += urlencode(k) + "=" + urlencode(v); } return o; }
inline std::vector<std::string> generate(const std::string& base, const Val& wf, const std::string& out, const std::string& name, double timeout, const Val& record) {
    makedirs(out);
    const std::string pid = queue(base, wf);
    std::vector<std::string> files; std::size_t i = 0;
    for (const auto& f : outputs(wait(base, pid, timeout))) {
        const std::string fn = pystr(get(f, "filename"));
        const std::string q = qs({{"filename", fn}, {"subfolder", pystr(get(f, "subfolder", Val::str("")))}, {"type", pystr(get(f, "type", Val::str("output")))}});
        std::string ext = u8native(P(fn).extension()); if (ext.empty()) ext = ".png";
        const std::string path = path_join(out, (i == 0 ? name : name + "_" + std::to_string(i + 1)) + ext);
        spit(path, *req_raw(base, "/view?" + q, nullptr, 600));
        files.push_back(path); ++i;
    }
    if (files.empty()) die("the workflow finished but saved nothing: add a SaveImage node (prompt " + pid + ")");
    Val rec = Val::dict(); rec.d->set("t", Val::str(fal::now_local())); rec.d->set("url", Val::str(base)); rec.d->set("prompt_id", Val::str(pid)); rec.d->set("name", Val::str(name));
    List fl; for (const auto& p : files) fl.push_back(Val::str(p)); rec.d->set("files", Val::list(fl));
    if (record.t == Val::DICT) for (const auto& [k, v] : record.d->items) rec.d->set(k, v);
    rec.d->set("workflow", wf);
    const std::string mf = path_join(out, "comfy_manifest.jsonl");
    std::string prev = isfile(mf) ? slurp(mf) : ""; spit(mf, prev + dumps(rec) + "\n");
    for (const auto& p : files) print(p);
    return files;
}
inline std::string name_for(const Val& a, const std::string& fallback) {
    if (!S(a, "name").empty()) return S(a, "name");
    auto w = split_ws(re::sub("[^a-z0-9 ]", "", lower(fallback))); if (w.size() > 5) w.resize(5);
    const std::string n = join(w, "_"); return n.empty() ? "image" : n;
}

inline int main_(const Val& a) {
    const std::string base = base_url(S(a, "url")), c = S(a, "cmd");
    if (c == "status") {
        const Val s = status(base);
        if (truthy(get(a, "json"))) { emit_json(s); return 0; }
        print("ComfyUI " + (truthy(get(s, "version")) ? pystr(get(s, "version")) : "?") + " at " + pystr(get(s, "url")) + " (PyTorch " + (truthy(get(s, "pytorch")) ? pystr(get(s, "pytorch")) : "?") + ")");
        const Val devs = get(s, "devices"), ck = get(s, "checkpoints");
        for (const auto& d : *devs.l) print("  device: " + d.s);
        std::string line = "  " + std::to_string(ck.size()) + " checkpoint(s)"; for (const auto& x : *ck.l) line += "\n    " + x.s;
        print(line); return 0;
    }
    const double timeout = to_float(get(a, "timeout"));
    if (c == "run") {
        const Val wf = apply_set(load_workflow(S(a, "workflow")), get(a, "set"));
        Val rec = Val::dict(); rec.d->set("source", Val::str(S(a, "workflow"))); rec.d->set("set", truthy(get(a, "set")) ? get(a, "set") : Val::list());
        generate(base, wf, S(a, "out"), !S(a, "name").empty() ? S(a, "name") : u8native(P(S(a, "workflow")).stem()), timeout, rec);
        return 0;
    }
    if (c == "image") {
        std::string ckpt = S(a, "checkpoint");
        if (ckpt.empty()) {
            const auto names = checkpoints(base);
            if (names.empty()) die("ComfyUI lists no checkpoints: put one in ComfyUI/models/checkpoints, or run a workflow with `um comfy run`");
            ckpt = names[0];
        }
        long long w = 512, h = 512;
        if (!S(a, "size").empty()) { const auto wh = parse_size(S(a, "size")); w = wh.first; h = wh.second; }
        else if (re::search("xl|1024", ckpt, 2).ok) { w = 1024; h = 1024; }
        std::string prompt = S(a, "prompt");
        const bool sprite_mode = truthy(get(a, "sprite"));
        if (sprite_mode) prompt = prompt + ". " + fal::SPRITE_STYLE + ", on a plain flat white background";
        long long seed;
        if (!get(a, "seed").is_none()) seed = get(a, "seed").i;
        else { std::random_device rd; seed = static_cast<long long>(rd() & 0xFFFFFFFFu); }
        const Val wf = txt2img(prompt, ckpt, S(a, "negative"), w, h, get(a, "steps").i, to_float(get(a, "cfg")), seed, S(a, "sampler"), S(a, "scheduler"), get(a, "n").i);
        Val rec = Val::dict(); rec.d->set("prompt", Val::str(prompt)); rec.d->set("checkpoint", Val::str(ckpt)); rec.d->set("seed", Val::integer(seed));
        const auto files = generate(base, wf, S(a, "out"), name_for(a, S(a, "prompt")), timeout, rec);
        if (sprite_mode) for (const auto& f : files) {
            const std::string cut = u8native(P(f).parent_path() / P(u8native(P(f).stem()) + "_cut.png"));
            img::save(sprite::cutout(img::load(f), 28, std::nullopt, std::nullopt, 24, false, 1.0), cut);
            print(cut);
        }
    }
    return 0;
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("comfy", DOC); p.help_line = "generate images locally with ComfyUI (no API key)"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    auto cmd = [&](const char* name, const char* help, bool out = true) -> Parser& {
        Parser& q = p.sub(name); q.help_line = help;
        q.opt({"--url"}).help("ComfyUI address (default: $COMFYUI_URL or " + DEFAULT_URL + ")");
        if (out) {
            q.opt({"--out"}).def("assets/gen").help("output folder (default assets/gen)"); q.opt({"--name"}).help("output file stem");
            q.opt({"--timeout"}).number().def(1800LL).help("seconds to wait for the result");
        }
        return q;
    };
    { Parser& q = cmd("status", "is ComfyUI running, and which checkpoints can it load", false); q.opt({"--json"}).store_true(); }
    { Parser& q = cmd("image", "text -> image with ComfyUI's default graph (SD 1.5 / SDXL checkpoints)"); q.pos("prompt");
      q.opt({"--checkpoint"}).help("checkpoint file name (default: the first one the server lists)"); q.opt({"--negative"}).def(Val::str(NEGATIVE));
      q.opt({"--size"}).help("WxH (default 1024x1024 for SDXL-looking checkpoint names, else 512x512)"); q.opt({"--steps"}).integer().def(25LL);
      q.opt({"--cfg"}).number().def_float(7.0); q.opt({"--seed"}).integer().help("default: random (written to the manifest)");
      q.opt({"--sampler"}).def("euler"); q.opt({"--scheduler"}).def("normal"); q.opt({"--n"}).integer().def(1LL).help("images per run (batch size)");
      q.opt({"--sprite"}).store_true().help("a single sprite on a plain background, then cut out locally"); }
    { Parser& q = cmd("run", "run a workflow saved with Export (API), with --set overrides"); q.pos("workflow");
      q.opt({"--set"}).append().metavar("NODE.INPUT=V").help("change a node input (repeatable; := for JSON)"); }
}
}  // namespace packtools::modder::comfy
