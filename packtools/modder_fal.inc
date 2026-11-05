// modder_fal.inc - `um fal`: a C++ port of universal-modder's um/fal.py (Rehan and universal-modder contributors, MIT).
// Game assets from fal (https://fal.ai) over plain REST. Paid service: every call spends the user's fal credits.
#if !defined(_WIN32)
#include <termios.h>
#else
#include <conio.h>
#endif
namespace packtools::modder {
// ------------------------------------------------------------------ HTTP for um (the engine's libcurl via http_ex_hook)
struct UmResp { long status = 0; std::string body, error; std::vector<std::pair<std::string, std::string>> headers; };
inline UmResp um_http(const std::string& method, const std::string& url, const std::vector<std::string>& headers, const std::string* body = nullptr, double timeout_s = 120) {
    UmResp r;
    HttpExFn f = http_ex_hook();
    if (!f) { r.error = "this build of A.S.T.R.O AI has no network client for pack tools"; return r; }
    HttpExReq q; q.url = url; q.method = method; q.headers = headers; q.timeout_s = timeout_s; q.follow_redirects = true;
    if (body) { q.body = *body; q.has_body = true; }
    const HttpExResp x = f(q);
    r.status = x.status; r.body = x.body; r.error = x.error; r.headers = x.headers;
    return r;
}
inline void sleep_s(double s) { std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(s * 1000))); }
inline std::string urlencode(const std::string& s) {   // urllib.parse.quote_plus
    static const char* hx = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') o += static_cast<char>(c);
        else if (c == ' ') o += '+';
        else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
    }
    return o;
}
inline std::string b64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) { const uint32_t v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]); o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63]; }
    if (i + 1 == in.size()) { const uint32_t v = static_cast<unsigned char>(in[i]) << 16; o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == in.size()) { const uint32_t v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8); o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '='; }
    return o;
}
inline std::string guess_mime(const std::string& name) {   // mimetypes.guess_type for the files games use
    static const std::map<std::string, std::string> m = {
        {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".webp", "image/webp"}, {".gif", "image/gif"}, {".bmp", "image/bmp"},
        {".tif", "image/tiff"}, {".tiff", "image/tiff"}, {".svg", "image/svg+xml"}, {".mp3", "audio/mpeg"}, {".wav", "audio/x-wav"}, {".ogg", "audio/ogg"},
        {".flac", "audio/flac"}, {".m4a", "audio/mp4"}, {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".mov", "video/quicktime"}, {".mkv", "video/x-matroska"},
        {".json", "application/json"}, {".txt", "text/plain"}, {".zip", "application/zip"}, {".obj", "model/obj"}, {".gltf", "model/gltf+json"},
        {".fbx", "application/octet-stream"}};
    const auto it = m.find(lower(u8native(P(name).extension())));
    return it == m.end() ? "" : it->second;
}
// a line from the terminal without echo (getpass)
inline std::string getpass_line(const std::string& prompt) {
    std::fputs(prompt.c_str(), stderr); std::fflush(stderr); std::string line;
#if defined(_WIN32)
    for (;;) { const int c = _getwch(); if (c == '\r' || c == '\n' || c == WEOF) break; if (c == 8) { if (!line.empty()) line.pop_back(); continue; } if (c < 128) line += static_cast<char>(c); }
#else
    termios old{}; const bool tty = tcgetattr(0, &old) == 0;
    if (tty) { termios t = old; t.c_lflag &= ~static_cast<tcflag_t>(ECHO); tcsetattr(0, TCSANOW, &t); }
    std::getline(std::cin, line);
    if (tty) tcsetattr(0, TCSANOW, &old);
#endif
    std::fputs("\n", stderr);
    return line;
}
}  // namespace packtools::modder

namespace packtools::modder::fal {
using namespace pyrt;
using namespace packtools::modder;

inline const char* DOC =
    "Game assets from fal (https://fal.ai): plain REST, no SDK needed.\n\n"
    "The key comes from FAL_KEY, else from where `um fal key set` stored it (the desktop keyring on Linux,\n"
    "~/.secrets/fal-key elsewhere), else from FAL_KEY=... in a .env. `um fal key status` shows which one is used.\n\n"
    "    um fal sprite \"a rusty scrap drone enemy, side view facing left, 16-bit pixel art\" --out assets/gen --name drone\n"
    "    um fal image \"key art: ...\" --aspect 16:9\n"
    "    um fal edit \"same drone, rotors blurred, second animation frame\" --ref assets/gen/drone.png\n"
    "    um fal rmbg in.png                       # background removal (BiRefNet v2)\n"
    "    um fal pixelate in.png --colors 24       # image -> clean pixel art (grid-snapped, palette-limited)\n"
    "    um fal texture \"mossy cobblestone\"       # seamless tiling texture\n"
    "    um fal pbr \"rusted sheet metal\"          # basecolor/normal/roughness/metalness/height maps\n"
    "    um fal model3d concept.png               # image -> textured GLB (Trellis 2; --engine hunyuan|tripo|meshy)\n"
    "    um fal rig character.glb --animate       # auto-rig a humanoid (Meshy) -> rigged GLB/FBX + animations\n"
    "    um fal sfx \"laser rifle shot, sci-fi, punchy\" --seconds 1.5\n"
    "    um fal music \"tense boss battle, chiptune, 140 bpm\" --seconds 60\n"
    "    um fal voice \"You dare challenge the Mothership?\" --voice-id Adam\n"
    "    um fal video still.png \"camera orbits the boss as it powers up\"\n"
    "    um fal run <endpoint> key=value key:=json image_url=@local.png   # anything else\n"
    "    um fal search \"image to 3d\" | um fal schema fal-ai/trellis-2 | um fal price fal-ai/trellis-2\n\n"
    "Every call appends a line to <out>/fal_manifest.jsonl (endpoint, inputs, request id, seed, files) so an\n"
    "asset can be traced and regenerated. Local files passed as inputs are uploaded to fal storage first.\n"
    "Model ids move fast: `um fal search` / the fal MCP's search_models find the current best.\n\n"
    "fal is a paid service: every generation spends credits on your fal account (check `um fal price <endpoint>`).\n";

inline const std::string QUEUE = "https://queue.fal.run", REST = "https://rest.fal.ai", CDN = "https://v3.fal.media", PLATFORM = "https://api.fal.ai/v1",
                         OPENAPI = "https://fal.ai/api/openapi/queue/openapi.json";
inline const std::map<std::string, std::string> MODELS = {
    {"image", "fal-ai/nano-banana-2"}, {"sprite", "openai/gpt-image-2"}, {"edit", "fal-ai/nano-banana-2/edit"}, {"rmbg", "fal-ai/birefnet/v2"},
    {"pixelate", "fal-ai/image2pixel"}, {"upscale", "fal-ai/seedvr/upscale/image"}, {"texture", "fal-ai/z-image/turbo/tiling"}, {"pbr", "fal-ai/patina/material"},
    {"model3d", "fal-ai/trellis-2"}, {"rig", "fal-ai/meshy/rigging"}, {"sfx", "fal-ai/elevenlabs/sound-effects/v2"}, {"music", "elevenlabs/music/v2.5"},
    {"voice", "fal-ai/elevenlabs/tts/eleven-v3"}, {"video", "bytedance/seedance-2.5/image-to-video"}};
inline const std::vector<std::array<std::string, 3>> MODEL3D = {
    {"trellis2", "fal-ai/trellis-2", "image_url"}, {"hunyuan", "fal-ai/hunyuan-3d/v3.1/pro/image-to-3d", "input_image_url"},
    {"tripo", "tripo3d/h3.1/image-to-3d", "image_url"}, {"meshy", "meshy/v7.1/image-to-3d", "image_url"}};
inline const std::string SPRITE_STYLE = "a single game sprite, the whole subject in frame and centered, clean readable silhouette, "
                                        "no text, no watermark, no ground shadow, no background scenery";

// ------------------------------------------------------------------ key
inline const std::vector<std::string> KEYRING_ATTRS = {"service", "fal.ai", "key", "api"};
inline std::string named_key_file() { const std::string a = env("UM_FAL_KEY_FILE"); return !a.empty() ? a : env("FAL_KEY_FILE"); }
inline std::string key_file() { const std::string n = named_key_file(); return expanduser(!n.empty() ? n : "~/.secrets/fal-key"); }
inline std::string mask(const std::string& k) { return cplen(k) > 14 ? cpslice(k, 0, 6) + "\xE2\x80\xA6" + cpslice(k, static_cast<long long>(cplen(k)) - 4, static_cast<long long>(cplen(k))) : "\xE2\x80\xA6"; }
inline bool keyring_available() {
#if defined(_WIN32)
    return false;
#else
    return !which_tool("secret-tool").empty();
#endif
}
inline std::string shq(const std::string& s) { std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
inline std::optional<std::string> secret_lookup(const std::vector<std::string>& attrs) {
    if (!keyring_available()) return std::nullopt;
    std::vector<std::string> cmd{which_tool("secret-tool"), "lookup"}; cmd.insert(cmd.end(), attrs.begin(), attrs.end());
    const auto r = mk::run_proc(cmd, "", {}, 20);
    if (r.code != 0) return std::nullopt;
    const std::string v = strip(decode_utf8_replace(r.out)); if (v.empty()) return std::nullopt; return v;
}
inline bool secret_store(const std::string& label, const std::vector<std::string>& attrs, const std::string& value) {
#if defined(_WIN32)
    (void)label; (void)attrs; (void)value; return false;
#else
    if (!keyring_available()) return false;
    std::string cmd = shq(which_tool("secret-tool")) + " store --label " + shq(label);
    for (const auto& a : attrs) cmd += " " + shq(a);
    cmd += " >/dev/null 2>&1";
    FILE* p = ::popen(cmd.c_str(), "w"); if (!p) return false;
    std::fwrite(value.data(), 1, value.size(), p);
    return ::pclose(p) == 0;
#endif
}
inline bool secret_clear(const std::vector<std::string>& attrs) {
    if (!keyring_available()) return false;
    std::vector<std::string> cmd{which_tool("secret-tool"), "clear"}; cmd.insert(cmd.end(), attrs.begin(), attrs.end());
    return mk::run_proc(cmd, "", {}, 20).code == 0;
}
inline std::optional<std::string> keyring_get() {
    if (!keyring_available()) return std::nullopt;
    std::vector<std::string> cmd{which_tool("secret-tool"), "lookup"}; cmd.insert(cmd.end(), KEYRING_ATTRS.begin(), KEYRING_ATTRS.end());
    const auto r = mk::run_proc(cmd, "", {}, 20);
    if (r.code != 0) return std::nullopt;
    const std::string v = strip(decode_utf8_replace(r.out)); if (v.empty()) return std::nullopt; return v;
}
inline bool keyring_set(const std::string& value) {   // the secret goes in on stdin, never on the command line
#if defined(_WIN32)
    (void)value; return false;
#else
    if (!keyring_available()) return false;
    std::string cmd = shq(which_tool("secret-tool")) + " store --label " + shq("fal.ai API key (universal-modder)");
    for (const auto& a : KEYRING_ATTRS) cmd += " " + shq(a);
    cmd += " >/dev/null 2>&1";
    FILE* p = ::popen(cmd.c_str(), "w"); if (!p) return false;
    std::fwrite(value.data(), 1, value.size(), p);
    return ::pclose(p) == 0;
#endif
}
inline bool keyring_clear() {
    if (!keyring_available()) return false;
    std::vector<std::string> cmd{which_tool("secret-tool"), "clear"}; cmd.insert(cmd.end(), KEYRING_ATTRS.begin(), KEYRING_ATTRS.end());
    return mk::run_proc(cmd, "", {}, 20).code == 0;
}
inline std::string write_key_file(const std::string& value, std::string p = "") {
    if (p.empty()) p = key_file();
    const fs::path dir = P(p).parent_path();
    std::error_code ec;
    if (!dir.empty() && !fs::exists(dir, ec)) { fs::create_directories(dir, ec); fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec); }
#if !defined(_WIN32)
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) throw OSError(errno, p);
    ::fchmod(fd, 0600);
    const std::string line = strip(value) + "\n"; const ssize_t w = ::write(fd, line.data(), line.size()); (void)w; ::close(fd);
#else
    spit(p, strip(value) + "\n");
#endif
    return p;
}
inline std::string file_mode(const std::string& p) {
    std::error_code ec; const auto st = fs::status(P(p), ec); if (ec || !fs::exists(st)) return "?";
    const unsigned m = static_cast<unsigned>(st.permissions()) & 0777u; char b[16]; std::snprintf(b, sizeof b, "0o%o", m); return b;
}
inline std::optional<std::string> read_file_key(const std::string& p) { if (!isfile(p)) return std::nullopt; const std::string v = strip(slurp(p)); if (v.empty()) return std::nullopt; return v; }
inline std::optional<std::string> read_dotenv(const std::string& p) {
    if (!isfile(p)) return std::nullopt;
    const auto m = re::search(R"(^\s*FAL_KEY\s*=\s*['"]?([^'"\s]+))", slurp(p), 8);
    if (!m.ok) return std::nullopt; return m.group(1);
}
struct Cand { std::string label; std::function<std::optional<std::string>()> read; bool plaintext; };
inline std::vector<Cand> key_candidates() {
    std::vector<Cand> c;
    c.push_back({"FAL_KEY environment variable", [] { const std::string v = strip(env("FAL_KEY")); return v.empty() ? std::nullopt : std::optional<std::string>(v); }, false});
    const std::string named = named_key_file(), p = key_file();
    if (!named.empty()) c.push_back({"key file " + p + " (UM_FAL_KEY_FILE / FAL_KEY_FILE)", [p] { return read_file_key(p); }, false});
    c.push_back({"OS keyring (" + join(KEYRING_ATTRS, " ") + ")", [] { return keyring_get(); }, false});
    if (named.empty()) c.push_back({"plaintext file " + p + " (mode " + file_mode(p) + ")", [p] { return read_file_key(p); }, true});
    std::error_code ec; const std::string dot = u8native(fs::current_path(ec) / ".env");
    c.push_back({".env file " + dot, [dot] { return read_dotenv(dot); }, true});
    return c;
}
inline std::string& key_cache() { static std::string k; return k; }
inline std::string fal_key() {
    if (!key_cache().empty()) return key_cache();
    for (const auto& c : key_candidates()) if (const auto k = c.read()) {
        if (c.plaintext && keyring_available()) eprint("um: fal key from " + c.label + "; `um fal key set` keeps it in the OS keyring");
        key_cache() = *k; return *k;
    }
    die("no fal key. Create one at https://fal.ai/dashboard/keys, then `um fal key set` (or set FAL_KEY)");
    return "";
}
inline void cmd_key(const Val& a) {
    const std::string action = S(a, "action"), kf = key_file();
    if (action == "set") {
        std::string value = !S(a, "from_file").empty() ? strip(slurp(expanduser(S(a, "from_file")))) : strip(getpass_line("fal API key (hidden): "));
        if (value.empty()) die("empty key, nothing stored");
        if (truthy(get(a, "file_only"))) print("stored in " + write_key_file(value) + " (plaintext, owner-only)");
        else if (keyring_set(value) && keyring_get() == value) {
            print("stored in the OS keyring as " + mask(value) + " (" + join(KEYRING_ATTRS, " ") + ")");
            if (pexists(kf)) print("note: " + kf + " still holds a key in plaintext; `um fal key clear --file` deletes it");
        } else if (keyring_available()) die("could not write to the OS keyring (is it unlocked?); `um fal key set --file-only` stores the key in a plaintext file instead");
        else print("no OS keyring (no secret-tool); stored in " + write_key_file(value) + " (plaintext, owner-only)");
        if (!env("FAL_KEY").empty()) print("note: FAL_KEY is set in this environment and takes precedence over the stored key");
    } else if (action == "status") {
        std::vector<std::pair<std::string, std::string>> src;
        for (const auto& c : key_candidates()) if (const auto k = c.read()) src.emplace_back(c.label, *k);
        if (src.empty()) print("no fal key configured; run `um fal key set`");
        bool in_keyring = false;
        for (std::size_t i = 0; i < src.size(); ++i) { print(std::string(i == 0 ? "active  " : "shadowed") + "  " + src[i].first + "  ->  " + mask(src[i].second)); if (starts(src[i].first, "OS keyring")) in_keyring = true; }
        print();
        print(std::string("keyring     ") + (keyring_available() ? "available" : "not available (no secret-tool)") + (in_keyring ? ", has the key" : ""));
        print("key file    " + kf + " " + (pexists(kf) ? "exists (mode " + file_mode(kf) + ")" : std::string("absent")));
    } else if (action == "clear") {
        std::vector<std::string> removed;
        if (keyring_get() && keyring_clear()) removed.push_back("OS keyring");
        if (truthy(get(a, "file")) && pexists(kf)) { std::error_code ec; fs::remove(P(kf), ec); removed.push_back(kf); }
        print(removed.empty() ? "nothing to remove" : "removed: " + join(removed, ", "));
    } else if (action == "migrate") {
        const auto value = read_file_key(kf);
        if (!value) die("nothing to migrate: " + kf + " does not exist or is empty");
        if (!keyring_available()) die("no OS keyring here (no secret-tool); keeping the file");
        if (!(keyring_set(*value) && keyring_get() == *value)) die("could not write to the OS keyring (is it unlocked?); keeping the file");
        std::error_code ec; fs::remove(P(kf), ec);
        print("moved " + mask(*value) + " into the OS keyring and deleted " + kf);
    } else die("usage: um fal key {set|status|clear|migrate}");
}

// ------------------------------------------------------------------ requests
inline std::string req_raw(const std::string& method, const std::string& url, const Val* json_body = nullptr, const std::string* raw_body = nullptr,
                           std::vector<std::string> headers = {}, bool auth = true, double timeout = 120) {
    headers.insert(headers.begin(), "Accept: application/json");
    if (auth) headers.push_back("Authorization: Key " + fal_key());
    std::string body; const std::string* bp = nullptr;
    if (raw_body) { body = *raw_body; bp = &body; }
    else if (json_body) { body = dumps(*json_body); bp = &body; bool has_ct = false; for (const auto& h : headers) if (starts(lower(h), "content-type:")) has_ct = true; if (!has_ct) headers.push_back("Content-Type: application/json"); }
    const std::string bare = url.substr(0, url.find('?'));
    for (int attempt = 0; attempt < 4; ++attempt) {
        const UmResp r = um_http(method, url, headers, bp, timeout);
        if (r.status >= 200 && r.status < 400) return r.body;
        if (r.status == 0) { if (attempt < 3) { sleep_s(2.0 * (attempt + 1)); continue; } die("fal " + method + " " + url + ": " + r.error); }
        if ((r.status == 429 || r.status == 500 || r.status == 502 || r.status == 503 || r.status == 504) && attempt < 3) { sleep_s(2.0 * (attempt + 1)); continue; }
        std::string detail = decode_utf8_replace(r.body); if (cplen(detail) > 1500) detail = cpslice(detail, 0, 1500);
        die("fal " + method + " " + bare + " -> HTTP " + std::to_string(r.status) + ": " + detail);
    }
    return "";
}
inline Val req(const std::string& method, const std::string& url, const Val* body = nullptr) { const std::string p = req_raw(method, url, body); return p.empty() ? Val::dict() : loads(decode_utf8_replace(p)); }

inline std::string upload_cdn(const std::string& name, const std::string& ctype, const std::string& data) {
    const Val empty = Val::dict();
    const Val tok = req("POST", REST + "/storage/auth/token?storage_type=fal-cdn-v3", &empty);
    const UmResp r = um_http("POST", CDN + "/files/upload", {"Authorization: " + pystr(get(tok, "token_type")) + " " + pystr(get(tok, "token")), "Content-Type: " + ctype,
                                                            "X-Fal-File-Name: " + name, "Accept: application/json", "User-Agent: universal-modder"}, &data, 600);
    if (r.status < 200 || r.status >= 300) throw PyErr("URLError", r.status ? "HTTP Error " + std::to_string(r.status) : r.error);
    const Val j = loads(decode_utf8_replace(r.body));
    if (!has(j, "access_url")) throw PyErr("KeyError", "'access_url'");
    return pystr(get(j, "access_url"));
}
inline std::string upload(const std::string& path) {
    if (!isfile(path)) die("no such file: " + path);
    const std::string name = name_of_path(path);
    std::string ctype = guess_mime(name); if (ctype.empty()) ctype = "application/octet-stream";
    if (lower(u8native(P(path).extension())) == ".glb") ctype = "model/gltf-binary";
    const std::string data = slurp(path); std::string err;
    try { return upload_cdn(name, ctype, data); }
    catch (const Exit&) { err = "see above"; }
    catch (const PyErr& e) { err = e.what(); }
    if (data.size() < (8u << 20)) { eprint("fal upload failed (" + err + "); sending " + name + " inline as a data URI"); return "data:" + ctype + ";base64," + b64(data); }
    die("could not upload " + name + " (" + commas(std::to_string(data.size())) + " bytes) to fal storage (" + err + "); the data-URI fallback only covers files under 8 MiB");
    return "";
}
inline Val as_url(const Val& v) {
    if (v.t != Val::STR) return v;
    if (starts(v.s, "@")) return Val::str(upload(v.s.substr(1)));
    if (!re::search(R"(^(https?|data):)", v.s).ok && isfile(v.s)) return Val::str(upload(v.s));
    return v;
}
inline void urls_in(const Val& o, const std::string& trail, std::vector<std::array<std::string, 3>>& out) {
    if (o.t == Val::DICT) {
        const Val u = get(o, "url");
        if (u.t == Val::STR && starts(u.s, "http")) out.push_back({trail, u.s, truthy(get(o, "content_type")) ? pystr(get(o, "content_type")) : ""});
        for (const auto& [k, v] : o.d->items) if (k != "url") urls_in(v, trail.empty() ? k : trail + "." + k, out);
    } else if (o.t == Val::LIST) for (std::size_t i = 0; i < o.l->size(); ++i) urls_in((*o.l)[i], trail + "[" + std::to_string(i) + "]", out);
}
inline std::vector<std::string> download_outputs(const Val& result, const std::string& out, const std::string& name) {
    makedirs(out);
    static const std::map<std::string, std::string> EXT = {{"image/png", ".png"}, {"image/jpeg", ".jpg"}, {"image/webp", ".webp"}, {"image/gif", ".gif"}, {"audio/mpeg", ".mp3"},
                                                           {"audio/wav", ".wav"}, {"audio/x-wav", ".wav"}, {"video/mp4", ".mp4"}, {"model/gltf-binary", ".glb"}, {"application/octet-stream", ""}};
    std::vector<std::array<std::string, 3>> urls; urls_in(result, "", urls);
    std::vector<std::string> files, failed; std::set<std::string> used; std::string first_key;
    for (const auto& [trail, url, ctype] : urls) {
        std::string upath = url; { const auto se = upath.find("://"); const auto sl = upath.find('/', se == std::string::npos ? 0 : se + 3); upath = sl == std::string::npos ? "" : upath.substr(sl); upath = upath.substr(0, upath.find_first_of("?#")); }
        std::string ext; { const std::string last = upath.substr(upath.rfind('/') + 1); const auto dot = last.rfind('.'); if (dot != std::string::npos && dot > 0 && dot + 1 < last.size()) ext = last.substr(dot); }
        if (ext.empty()) { const auto it = EXT.find(ctype.substr(0, ctype.find(';'))); if (it != EXT.end()) ext = it->second; }
        std::string key = re::sub(R"(\[\d+\])", "", trail); key = key.substr(key.rfind('.') == std::string::npos ? 0 : key.rfind('.') + 1); if (key.empty()) key = "file";
        const auto idx = re::findall(R"(\[(\d+)\])", trail);
        if (first_key.empty()) first_key = key;
        std::string stem;
        const bool zero = idx.empty() || idx.back() == "0";
        if (key == first_key) stem = zero ? name : name + "_" + std::to_string(std::stoll(idx.back()) + 1);
        else stem = name + "_" + key + (zero ? "" : "_" + std::to_string(std::stoll(idx.back()) + 1));
        std::string fname = stem + ext; int n = 1;
        while (used.count(fname)) { ++n; fname = stem + "_" + std::to_string(n) + ext; }
        used.insert(fname);
        const std::string path = path_join(out, fname);
        const UmResp r = um_http("GET", url, {"User-Agent: universal-modder"}, nullptr, 600);
        if (r.status < 200 || r.status >= 300) { failed.push_back(url + " (" + (r.status ? "HTTP Error " + std::to_string(r.status) : r.error) + ")"); continue; }
        spit(path, r.body); files.push_back(path);
    }
    if (!failed.empty()) {
        const Val rid = get(result, "_request_id"); const std::string ep = truthy(get(result, "_endpoint")) ? pystr(get(result, "_endpoint")) : "<endpoint>";
        die("the job finished but these outputs did not download:\n  " + join(failed, "\n  ") + (truthy(rid) ? "\nrequest " + pystr(rid) + ": try again later with `um fal result " + ep + " " + pystr(rid) + "`" : ""));
    }
    return files;
}
inline Val run(const std::string& endpoint, const Val& inp, double timeout = 1800, bool quiet = false) {
    const Val job = req("POST", QUEUE + "/" + endpoint, &inp);
    const std::string rid = pystr(get(job, "request_id"));
    const std::string status_url = truthy(get(job, "status_url")) ? pystr(get(job, "status_url")) : QUEUE + "/" + endpoint + "/requests/" + rid + "/status";
    const std::string response_url = truthy(get(job, "response_url")) ? pystr(get(job, "response_url")) : QUEUE + "/" + endpoint + "/requests/" + rid;
    const auto t0 = std::chrono::steady_clock::now(); std::size_t seen = 0; std::string last;
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    for (;;) {
        const Val st = req("GET", status_url + (status_url.find('?') != std::string::npos ? "&" : "?") + "logs=1");
        const std::string s = pystr(get(st, "status")); const Val logs = truthy(get(st, "logs")) ? get(st, "logs") : Val::list();
        if (!quiet) {
            for (std::size_t i = seen; i < logs.size(); ++i) { const Val& line = (*logs.l)[i]; const std::string msg = truthy(get(line, "message")) ? pystr(get(line, "message")) : ""; if (!msg.empty()) eprint("  [" + endpoint + "] " + msg); }
            if (s != last) { const std::string pos = (s == "IN_QUEUE" && !get(st, "queue_position").is_none()) ? " (queue position " + pystr(get(st, "queue_position")) + ")" : ""; eprint("  [" + endpoint + "] " + s + pos); }
        }
        seen = logs.size(); last = s;
        if (s == "COMPLETED") {
            if (truthy(get(st, "error"))) die(endpoint + " failed: " + pystr(get(st, "error")));
            Val res = req("GET", response_url);
            if (res.t != Val::DICT) { Val w = Val::dict(); w.d->set("result", res); res = w; }
            res.d->set("_request_id", Val::str(rid)); res.d->set("_endpoint", Val::str(endpoint));
            return res;
        }
        if (elapsed() > timeout) die(endpoint + ": still " + s + " after " + fmt_f(timeout, 0) + "s (request " + rid + "); check later with `um fal result " + endpoint + " " + rid + "`");
        sleep_s(elapsed() < 30 ? 1.0 : 3.0);
    }
}
inline std::string now_local() { const std::time_t t = std::time(nullptr); std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32]; std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &tmv); return b; }
inline Val generate(const std::string& endpoint, const Val& inp0, const std::string& out, const std::string& name, bool quiet = false) {
    Val inp = Val::dict();
    for (const auto& [k, v] : inp0.d->items) {
        if (v.is_none()) continue;
        if (v.t == Val::LIST && ends(k, "urls")) { List l; for (const auto& x : *v.l) l.push_back(as_url(x)); inp.d->set(k, Val::list(l)); }
        else if (ends(k, "url")) inp.d->set(k, as_url(v));
        else inp.d->set(k, v);
    }
    const Val res = run(endpoint, inp, 1800, quiet);
    const auto files = download_outputs(res, out, name);
    Val rec = Val::dict(); rec.d->set("t", Val::str(now_local())); rec.d->set("endpoint", Val::str(endpoint)); rec.d->set("name", Val::str(name));
    rec.d->set("request_id", get(res, "_request_id")); rec.d->set("seed", get(res, "seed"));
    List fl; for (const auto& f : files) fl.push_back(Val::str(f)); rec.d->set("files", Val::list(fl));
    Val ri = Val::dict(); for (const auto& [k, v] : inp.d->items) ri.d->set(k, (v.t == Val::STR && starts(v.s, "data:")) ? Val::str(cpslice(v.s, 0, 120) + "...") : v);
    rec.d->set("input", ri);
    const std::string mf = path_join(out, "fal_manifest.jsonl");
    FILE* fp = std::fopen(mf.c_str(), "ab");
#if defined(_WIN32)
    if (!fp) fp = _wfopen(P(mf).c_str(), L"ab");
#endif
    if (fp) { const std::string line = dumps(rec) + "\n"; std::fwrite(line.data(), 1, line.size(), fp); std::fclose(fp); }
    for (const auto& f : files) print(f);
    Val o = Val::dict(); o.d->set("files", Val::list(fl)); o.d->set("result", res); return o;
}

// ------------------------------------------------------------------ catalog
inline Val search(const std::string& q, const std::string& category, long long limit) {
    std::string qs = "q=" + urlencode(q) + "&status=active&limit=" + std::to_string(limit); if (!category.empty()) qs += "&category=" + urlencode(category);
    const Val r = req("GET", PLATFORM + "/models?" + qs);
    List out;
    const Val models = get(r, "models").t == Val::LIST ? get(r, "models") : Val::list();
    for (const auto& m : *models.l) {
        const Val md = truthy(get(m, "metadata")) ? get(m, "metadata") : Val::dict();
        Val d = Val::dict(); d.d->set("endpoint", get(m, "endpoint_id")); d.d->set("name", get(md, "display_name")); d.d->set("category", get(md, "category"));
        d.d->set("date", Val::str(cpslice(truthy(get(md, "date")) ? pystr(get(md, "date")) : "", 0, 10))); out.push_back(d);
    }
    return Val::list(out);
}
inline Val schema(const std::string& endpoint) {
    const Val api = loads(decode_utf8_replace(req_raw("GET", OPENAPI + "?endpoint_id=" + urlencode(endpoint), nullptr, nullptr, {}, false)));
    const Val comps = get(get(api, "components", Val::dict()), "schemas", Val::dict());
    auto resolve = [&](const Val& ref) -> Val { if (!truthy(ref)) return Val::dict(); const std::string r = pystr(ref); return get(comps, r.substr(r.rfind('/') + 1), Val::dict()); };
    Val inp = Val::dict(), outp = Val::dict(); bool have_in = false;
    const Val paths = get(api, "paths").t == Val::DICT ? get(api, "paths") : Val::dict();
    for (const auto& [path, ops] : paths.d->items) {
        const Val post = get(ops, "post");
        if (truthy(post) && has(post, "requestBody") && !have_in) {
            inp = resolve(get(get(get(get(get(post, "requestBody"), "content"), "application/json"), "schema"), "$ref")); have_in = inp.size() > 0;
        }
        if (ends(path, "/requests/{request_id}") && has(ops, "get")) {
            const Val ref = get(get(get(get(get(get(get(ops, "get"), "responses"), "200"), "content"), "application/json"), "schema"), "$ref");
            if (truthy(ref)) outp = resolve(ref);
        }
    }
    auto fields = [](const Val& s) {
        Val res = Val::dict();
        const Val props = get(s, "properties", Val::dict()); if (props.t != Val::DICT) return res;
        for (const auto& [k, v] : props.d->items) {
            Val d = Val::dict();
            for (const char* x : {"type", "default", "enum", "description"}) if (has(v, x)) d.d->set(x, get(v, x));
            if (get(v, "anyOf").t == Val::LIST) { std::vector<std::string> ts; const Val any = get(v, "anyOf"); for (const auto& a : *any.l) { if (truthy(get(a, "type"))) ts.push_back(pystr(get(a, "type"))); else { const std::string r = truthy(get(a, "$ref")) ? pystr(get(a, "$ref")) : ""; ts.push_back(r.substr(r.rfind('/') == std::string::npos ? 0 : r.rfind('/') + 1)); } } d.d->set("type", Val::str(join(ts, " | "))); }
            if (has(v, "$ref")) { const std::string r = pystr(get(v, "$ref")); d.d->set("type", Val::str(r.substr(r.rfind('/') + 1))); }
            if (d.d->has("description")) d.d->set("description", Val::str(cpslice(pystr(get(d, "description")), 0, 160)));
            res.d->set(k, d);
        }
        return res;
    };
    Val o = Val::dict(); o.d->set("endpoint", Val::str(endpoint)); o.d->set("required", get(inp, "required", Val::list())); o.d->set("input", fields(inp)); o.d->set("output", fields(outp));
    return o;
}
inline Val price(const std::string& endpoint) { return req("GET", PLATFORM + "/models/pricing?endpoint_id=" + urlencode(endpoint)); }

// ------------------------------------------------------------------ recipes
inline Val kv(const Val& pairs) {
    Val out = Val::dict(); if (pairs.t != Val::LIST) return out;
    for (const auto& pv : *pairs.l) {
        const std::string p = pv.s; const auto ce = p.find(":="), eq = p.find('=');
        if (ce != std::string::npos) out.d->set(p.substr(0, ce), loads(p.substr(ce + 2)));
        else if (eq != std::string::npos) out.d->set(p.substr(0, eq), Val::str(p.substr(eq + 1)));
        else die("bad argument " + repr(Val::str(p)) + ": use key=value or key:=json");
    }
    return out;
}
inline std::string name_for(const Val& a, const std::string& fallback) {
    if (!S(a, "name").empty()) return S(a, "name");
    const std::string t = re::sub("[^a-z0-9 ]", "", lower(fallback)); auto w = split_ws(t); if (w.size() > 5) w.resize(5);
    const std::string n = join(w, "_"); return n.empty() ? "asset" : n;
}
inline std::string stem_of(const std::string& p) { return u8native(P(p).stem()); }
inline Val merged(Val base, const Val& extra) { for (const auto& [k, v] : extra.d->items) base.d->set(k, v); return base; }
inline Val D(std::initializer_list<std::pair<const char*, Val>> kvs) { Val d = Val::dict(); for (const auto& [k, v] : kvs) d.d->set(k, v); return d; }

inline void cmd(const Val& a) {
    const std::string r = S(a, "recipe"); const std::string out = has(a, "out") && !S(a, "out").empty() ? S(a, "out") : "assets/gen";
    const Val extra = kv(get(a, "set")); const std::string model = S(a, "model");
    auto M = [&](const char* k) { return !model.empty() ? model : MODELS.at(k); };
    DumpOpts ind; ind.indent = 2;
    auto num_or_none = [&](const char* k) { const Val v = get(a, k); return v; };
    if (r == "key") { cmd_key(a); return; }
    if (r == "search") {
        const Val found = search(S(a, "query"), S(a, "category"), get(a, "limit").i);
        for (const auto& m : *found.l)
            print(ljust(pystr(get(m, "endpoint")), 55) + " " + ljust(truthy(get(m, "category")) ? pystr(get(m, "category")) : "", 18) + " " + pystr(get(m, "date")) + "  " + pystr(get(m, "name")));
        return;
    }
    if (r == "schema") { print(dumps(schema(S(a, "endpoint")), ind)); return; }
    if (r == "price") { print(dumps(price(S(a, "endpoint")), ind)); return; }
    if (r == "result") {
        Val res = req("GET", QUEUE + "/" + S(a, "endpoint") + "/requests/" + S(a, "request_id"));
        download_outputs(res, out, !S(a, "name").empty() ? S(a, "name") : S(a, "request_id"));
        print(dumps(res, ind)); return;
    }
    if (r == "upload") { print(upload(S(a, "file"))); return; }
    if (r == "run") { const std::string ep = S(a, "endpoint"); generate(ep, kv(get(a, "params")), out, !S(a, "name").empty() ? S(a, "name") : ep.substr(ep.rfind('/') + 1)); return; }
    const std::string prompt = S(a, "prompt");
    if (r == "image") generate(M("image"), merged(D({{"prompt", Val::str(prompt)}, {"aspect_ratio", get(a, "aspect")}, {"resolution", get(a, "res")}, {"num_images", get(a, "n")}, {"seed", num_or_none("seed")}}), extra), out, name_for(a, prompt));
    else if (r == "sprite") {
        const std::string full = prompt + ". " + SPRITE_STYLE; const std::string ep = M("sprite"); const bool gpt = starts(ep, "openai/gpt-image");
        const Val inp = gpt ? D({{"prompt", Val::str(full)}, {"background", Val::str("transparent")}, {"quality", get(a, "quality")}, {"image_size", get(a, "size")}, {"num_images", get(a, "n")}, {"output_format", Val::str("png")}})
                            : D({{"prompt", Val::str(full + ", on a plain flat white background")}, {"num_images", get(a, "n")}});
        const Val res = generate(ep, merged(inp, extra), out, name_for(a, prompt));
        const Val files = get(res, "files"); if (!gpt) for (const auto& f : *files.l) generate(MODELS.at("rmbg"), D({{"image_url", f}, {"output_format", Val::str("png")}}), out, stem_of(f.s) + "_cut");
    }
    else if (r == "edit") generate(M("edit"), merged(D({{"prompt", Val::str(prompt)}, {"image_urls", get(a, "ref")}, {"num_images", get(a, "n")}, {"aspect_ratio", get(a, "aspect")}, {"resolution", get(a, "res")}, {"seed", num_or_none("seed")}}), extra), out, name_for(a, prompt));
    else if (r == "rmbg") generate(M("rmbg"), merged(D({{"image_url", get(a, "image")}, {"output_format", Val::str("png")}}), extra), out, !S(a, "name").empty() ? S(a, "name") : stem_of(S(a, "image")) + "_cut");
    else if (r == "pixelate") generate(M("pixelate"), merged(D({{"image_url", get(a, "image")}, {"max_colors", get(a, "colors")}, {"transparent_background", Val::boolean(true)}, {"trim_borders", Val::boolean(true)}, {"scale", num_or_none("scale")}}), extra), out, !S(a, "name").empty() ? S(a, "name") : stem_of(S(a, "image")) + "_px");
    else if (r == "upscale") generate(M("upscale"), merged(D({{"image_url", get(a, "image")}, {"upscale_factor", get(a, "factor")}, {"output_format", Val::str("png")}}), extra), out, !S(a, "name").empty() ? S(a, "name") : stem_of(S(a, "image")) + "_up");
    else if (r == "texture") generate(M("texture"), merged(D({{"prompt", Val::str(prompt + ", seamless tileable texture, top-down, even lighting")}, {"image_size", get(a, "size")}, {"tiling_mode", get(a, "tiling")}}), extra), out, name_for(a, prompt));
    else if (r == "pbr") { List maps; for (const auto& m : split(S(a, "maps"), ",")) maps.push_back(Val::str(m));
        generate(M("pbr"), merged(D({{"prompt", Val::str(prompt)}, {"maps", Val::list(maps)}, {"image_size", get(a, "size")}, {"tiling_mode", Val::str("both")}}), extra), out, name_for(a, prompt)); }
    else if (r == "model3d") {
        const std::string eng = S(a, "engine"); std::string ep, field; for (const auto& m : MODEL3D) if (m[0] == eng) { ep = m[1]; field = m[2]; }
        Val inp = Val::dict(); inp.d->set(field, get(a, "image"));
        if (eng == "trellis2") { inp.d->set("texture_size", get(a, "texture")); inp.d->set("decimation_target", get(a, "faces")); }
        else if (eng == "hunyuan") { inp.d->set("face_count", get(a, "faces")); inp.d->set("enable_pbr", Val::boolean(true)); }
        generate(!model.empty() ? model : ep, merged(inp, extra), out, !S(a, "name").empty() ? S(a, "name") : stem_of(S(a, "image")));
    }
    else if (r == "rig") generate(M("rig"), merged(D({{"model_url", get(a, "model_file")}, {"enable_animation", get(a, "animate")}, {"height_meters", get(a, "height")}}), extra), out, !S(a, "name").empty() ? S(a, "name") : stem_of(S(a, "model_file")) + "_rigged");
    else if (r == "sfx") generate(M("sfx"), merged(D({{"text", Val::str(prompt)}, {"duration_seconds", get(a, "seconds")}, {"loop", get(a, "loop")}, {"output_format", Val::str("mp3_44100_192")}}), extra), out, name_for(a, prompt));
    else if (r == "music") generate(M("music"), merged(D({{"prompt", Val::str(prompt)}, {"music_length_ms", Val::integer(static_cast<long long>(get(a, "seconds").num() * 1000))}, {"force_instrumental", Val::boolean(!truthy(get(a, "vocals")))}, {"output_format", Val::str("mp3_44100_192")}}), extra), out, name_for(a, prompt));
    else if (r == "voice") generate(M("voice"), merged(D({{"text", get(a, "text")}, {"voice", get(a, "voice_id")}, {"stability", get(a, "stability")}}), extra), out, name_for(a, S(a, "text")));
    else if (r == "video") generate(M("video"), merged(D({{"image_url", get(a, "image")}, {"prompt", Val::str(prompt)}, {"duration", Val::str(pystr(get(a, "seconds")))}, {"resolution", get(a, "res")}, {"generate_audio", get(a, "audio")}}), extra), out, !S(a, "name").empty() ? S(a, "name") : stem_of(S(a, "image")) + "_video");
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("fal", DOC); p.help_line = "generate assets with fal (sprites, textures, 3D, rigs, audio, video)"; p.raw_description = true;
    p.subcommands("recipe", false); p.sub_metavar = "<recipe>";
    auto recipe = [&](const char* name, const char* help, std::vector<const char*> pos, bool out = true) -> Parser& {
        Parser& q = p.sub(name); q.help_line = help;
        for (const char* x : pos) q.pos(x);
        if (out) {
            q.opt({"--out"}).def("assets/gen").help("output folder (default assets/gen)"); q.opt({"--name"}).help("output file stem");
            q.opt({"--model"}).help("override the endpoint"); q.opt({"--set"}).append().metavar("K=V").help("extra model input (repeatable; K:=json for numbers)");
        }
        return q;
    };
    const std::vector<std::string> RES = {"0.5K", "1K", "2K", "4K"};
    { Parser& q = recipe("image", "text -> image (Nano Banana 2)", {"prompt"}); q.opt({"--aspect"}).def("1:1").help("e.g. 1:1, 16:9, 3:4, 21:9");
      q.opt({"--res"}).def("1K").choices(RES); q.opt({"--n"}).integer().def(1LL); q.opt({"--seed"}).integer(); }
    { Parser& q = recipe("sprite", "text -> sprite on a transparent background (GPT Image 2)", {"prompt"}); q.opt({"--quality"}).def("high").choices({"low", "medium", "high"});
      q.opt({"--size"}).def("square_hd").help("square_hd, portrait_4_3, landscape_4_3, ..."); q.opt({"--n"}).integer().def(1LL); }
    { Parser& q = recipe("edit", "edit / make variants of reference images (consistent sets, animation frames)", {"prompt"});
      q.opt({"--ref"}).append().required().help("reference image (path or URL), repeatable"); q.opt({"--aspect"}).def("auto"); q.opt({"--res"}).def("1K").choices(RES);
      q.opt({"--n"}).integer().def(1LL); q.opt({"--seed"}).integer(); }
    recipe("rmbg", "remove the background (BiRefNet v2)", {"image"});
    { Parser& q = recipe("pixelate", "image -> grid-snapped pixel art (fal image2pixel)", {"image"}); q.opt({"--colors"}).integer().def(32LL); q.opt({"--scale"}).integer().help("force the pixel scale"); }
    { Parser& q = recipe("upscale", "upscale an image (SeedVR2)", {"image"}); q.opt({"--factor"}).number().def(2LL); }
    { Parser& q = recipe("texture", "seamless tiling texture (Z-Image Turbo tiling)", {"prompt"}); q.opt({"--size"}).def("square_hd"); q.opt({"--tiling"}).def("both").choices({"both", "horizontal", "vertical"}); }
    { Parser& q = recipe("pbr", "PBR material maps (PATINA)", {"prompt"}); q.opt({"--maps"}).def("basecolor,normal,roughness,metalness,height"); q.opt({"--size"}).def("square_hd"); }
    { Parser& q = recipe("model3d", "image -> textured 3D model (GLB)", {"image"}); q.opt({"--engine"}).def("trellis2").choices({"trellis2", "hunyuan", "tripo", "meshy"});
      q.opt({"--texture"}).integer().def(2048LL).choices({"1024", "2048", "4096"}); q.opt({"--faces"}).integer().def(100000LL).help("target face count (game-ready: 5k-50k)"); }
    { Parser& q = recipe("rig", "auto-rig a humanoid model (Meshy), optionally with animations", {"model_file"}); q.opt({"--animate"}).store_true();
      q.opt({"--height"}).number().def_float(1.7).help("character height in meters"); }
    { Parser& q = recipe("sfx", "sound effect (ElevenLabs SFX v2)", {"prompt"}); q.opt({"--seconds"}).number(); q.opt({"--loop"}).store_true(); }
    { Parser& q = recipe("music", "music track (ElevenLabs Music)", {"prompt"}); q.opt({"--seconds"}).number().def(60LL); q.opt({"--vocals"}).store_true(); }
    { Parser& q = recipe("voice", "voice line / narration (ElevenLabs v3)", {"text"}); q.opt({"--voice-id"}).def("Rachel"); q.opt({"--stability"}).number().def_float(0.5); }
    { Parser& q = recipe("video", "image -> video clip (Seedance 2.5): trailers, cutscenes", {"image", "prompt"}); q.opt({"--seconds"}).integer().def(5LL);
      q.opt({"--res"}).def("720p").choices({"480p", "720p", "1080p"}); q.opt({"--audio"}).store_true(); }
    { Parser& q = recipe("run", "any endpoint: key=value / key:=json / key=@file", {"endpoint"}); q.pos("params").nargs("*"); }
    { Parser& q = recipe("search", "search the fal catalog", {"query"}, false); q.opt({"--category"}).help("text-to-image, image-to-3d, text-to-audio, ..."); q.opt({"--limit"}).integer().def(20LL); }
    recipe("schema", "input/output fields of an endpoint", {"endpoint"}, false);
    recipe("price", "pricing of an endpoint", {"endpoint"}, false);
    recipe("result", "fetch (and download) a finished request", {"endpoint", "request_id"});
    recipe("upload", "upload a local file, print its URL", {"file"}, false);
    Parser& kp = p.sub("key", "Every `um fal` recipe takes the key from FAL_KEY first, then from a file named by UM_FAL_KEY_FILE (or FAL_KEY_FILE), then from the OS keyring, "
                              "then from ~/.secrets/fal-key, then from FAL_KEY=... in a .env. `um fal key set` stores it in the desktop keyring through libsecret's secret-tool "
                              "(GNOME Keyring, KWallet: Linux). Where secret-tool is missing (Windows, macOS, WSL, servers) it writes the plaintext file instead, readable only by you. "
                              "Keys are never printed in full.");
    kp.help_line = "store / inspect the fal API key (OS keyring first)";
    kp.subcommands("action", false); kp.sub_metavar = "<action>";
    { Parser& q = kp.sub("set"); q.help_line = "store a key from a hidden prompt (or --from-file)"; q.opt({"--from-file"}).metavar("PATH").help("read the key from PATH instead of prompting");
      q.opt({"--file-only"}).store_true().help("skip the keyring and write the plaintext file"); }
    { Parser& q = kp.sub("status"); q.help_line = "show every source and which one is active (keys are masked)"; }
    { Parser& q = kp.sub("clear"); q.help_line = "remove the key from the OS keyring"; q.opt({"--file"}).store_true().help("also delete the plaintext key file"); }
    { Parser& q = kp.sub("migrate"); q.help_line = "move the plaintext key file into the OS keyring and delete it"; }
}
inline int main_(const Val& a) { cmd(a); return 0; }
}  // namespace packtools::modder::fal
