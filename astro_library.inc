// =============================================================================================
// A.S.T.R.O AI Library: install MCP servers, skills and plugins from the official catalog
// (included from astro_dev.inc, inside the anonymous namespace).
//
//   POST /api/dev/library/index       the catalog + what is installed here
//   POST /api/dev/library/install     { id, inputs: { NAME: value } }
//   POST /api/dev/library/uninstall   { id }
//
// Safety model (the browser is never trusted with what gets written):
//   * the server downloads the catalog itself, from ASTRO_LIBRARY_URL or the official site, over https
//   * every file is downloaded from the catalog's own folder and must match the SHA-256 in the catalog
//   * skills and plugins are text only (.md .json .txt); scripts, hooks and styles are refused
//   * a plugin's .mcp.json may only list remote https servers
//   * MCP packages must be pinned (`npx -y <pkg>@<x.y.z>`); argument values cannot carry shell syntax
//   * secrets the user types (tokens) are written only to the local, private mcp.json
//   * installs go to a staging folder first and replace the old version only when complete
//   * a receipt (ext/library.json) lists every installed file, so uninstall removes exactly those
// =============================================================================================

// sha256_hex() comes from astro_ext.inc (libcrypto / CommonCrypto through platform.h).

// ---------- catalog location and download ----------
std::string library_index_url() {
    if (const char* u = std::getenv("ASTRO_LIBRARY_URL"); u && *u) return u;
    return "https://esoexe.github.io/astro-ai/library/index.json";
}
// https anywhere; plain http only for a catalog on this computer (development and tests).
bool library_url_ok(const std::string& url, bool& local) {
    static const std::regex re(R"(^(https?)://([A-Za-z0-9.-]+)(:\d+)?/[A-Za-z0-9._~%/-]*$)");
    std::smatch m;
    if (!std::regex_match(url, m, re)) return false;
    const std::string host = lower_copy(m[2].str());
    local = host == "127.0.0.1" || host == "localhost";
    return m[1] == "https" || local;
}
std::string library_get(const std::string& url, std::size_t cap, std::string& error) {
    bool local = false;
    if (!library_url_ok(url, local)) { error = "The library address is not allowed: " + truncate(url, 120); return {}; }
    const FetchResult r = http_fetch(url, {"Accept: application/json, text/plain, */*", "Cache-Control: no-cache"}, nullptr, 40, cap, !local);
    if (!r.error.empty()) { error = "Could not reach the A.S.T.R.O AI Library: " + r.error; return {}; }
    if (r.truncated) { error = "A library file is larger than allowed."; return {}; }
    if (r.status != 200) { error = "The A.S.T.R.O AI Library answered HTTP " + std::to_string(r.status) + " for " + truncate(url, 120); return {}; }
    return r.body;
}
std::string library_base_url() { const std::string u = library_index_url(); return u.substr(0, u.rfind('/') + 1); }

std::mutex g_lib_mu;
std::string g_lib_cache;
long long g_lib_cache_at = 0;
Json library_index(bool force, std::string& error) {
    std::lock_guard lk(g_lib_mu);
    if (force || g_lib_cache.empty() || now_ms() - g_lib_cache_at > 5LL * 60 * 1000) {
        std::string body = library_get(library_index_url(), 4'000'000, error);
        if (body.empty()) { if (g_lib_cache.empty()) return Json(); error.clear(); }
        else { g_lib_cache = std::move(body); g_lib_cache_at = now_ms(); }
    }
    try {
        Json j = JsonParser(g_lib_cache).parse();
        if (json_int(j, "schema", 0) != 1 || !j.get("items") || !j.get("items")->array_ptr()) { error = "The library catalog has an unknown format. Update A.S.T.R.O AI."; return Json(); }
        return j;
    } catch (const std::exception& e) { error = std::string("The library catalog is not valid: ") + e.what(); return Json(); }
}

// ---------- validation shared by install ----------
bool lib_id_ok(const std::string& id) { static const std::regex re(R"(^[a-z0-9][a-z0-9-]{0,47}$)"); return std::regex_match(id, re); }
bool lib_file_ok(const std::string& rel) {
    if (rel == ".claude-plugin/plugin.json" || rel == ".mcp.json") return true;
    static const std::regex re(R"(^(?:[A-Za-z0-9_][A-Za-z0-9_.-]*/)*[A-Za-z0-9_][A-Za-z0-9_.-]*\.(?:md|json|txt)$)");
    if (!std::regex_match(rel, re) || rel.find("..") != std::string::npos || rel.size() > 200) return false;
    static const std::regex hooks(R"((^|/)hooks(/|\.json$))");
    return !std::regex_search(rel, hooks);
}
bool lib_https_url(const std::string& u) { static const std::regex re(R"(^https://[A-Za-z0-9.-]+(:\d+)?/[^\s"'<>]*$)"); return std::regex_match(u, re); }
bool lib_pkg_ok(const std::string& p) { static const std::regex re(R"(^(?:@[a-z0-9][a-z0-9._-]*/)?[a-z0-9][a-z0-9._-]*@\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$)"); return std::regex_match(p, re); }
bool lib_flag_ok(const std::string& a) { static const std::regex re(R"(^(?:--?[A-Za-z0-9][A-Za-z0-9-]*(?:=[A-Za-z0-9._:/-]*)?|\{[A-Z][A-Z0-9_]*\})$)"); return std::regex_match(a, re); }
// A value the user typed for an argument, environment variable or header. Arguments reach cmd.exe on Windows,
// so shell syntax is refused everywhere to keep the behaviour the same on every OS.
bool lib_value_ok(const std::string& v, bool arg) {
    if (v.size() > 4096) return false;
    for (unsigned char c : v) { if (c < 0x20 || c == 0x7f) return false; if (arg && std::strchr("&|<>^%\"`$;", c)) return false; }
    return true;
}

fs::path lib_receipt_path() { return ext_root() / "library.json"; }
Json::Object lib_receipts() {
    if (auto t = read_small(lib_receipt_path(), 2'000'000)) { try { Json j = JsonParser(*t).parse(); if (const Json* it = j.get("items"); it && it->object_ptr()) return *it->object_ptr(); } catch (...) {} }
    return {};
}
bool lib_save_receipts(const Json::Object& items) {
    Json::Object root; root["schema"] = Json(1); root["items"] = Json(items);
    return write_file_private(lib_receipt_path().string(), dump_json(Json(std::move(root))));
}
const Json* lib_find(const Json& index, const std::string& id) {
    for (const auto& it : *index.get("items")->array_ptr()) if (get_string(it, "id") == id) return &it;
    return nullptr;
}

// Loads ext/mcp.json as an object (refuses to touch a file the user broke by hand).
std::optional<Json::Object> lib_mcp_load(std::string& error) {
    const auto t = read_small(ext_root() / "mcp.json", 1'000'000);
    if (!t || trim(*t).empty()) { Json::Object root; root["mcpServers"] = Json(Json::Object{}); return root; }
    try {
        Json j = JsonParser(*t).parse();
        if (!j.object_ptr()) throw std::runtime_error("not an object");
        Json::Object root = *j.object_ptr();
        if (!root.count("mcpServers") || !root["mcpServers"].object_ptr()) root["mcpServers"] = Json(Json::Object{});
        return root;
    } catch (const std::exception& e) { error = std::string("Your mcp.json has an error (") + e.what() + "). Fix it in Extensions → MCP → Edit mcp.json first."; return std::nullopt; }
}

// ---------- install / uninstall ----------
// Stops a global MCP connection so it restarts with the new settings (or stays gone) the next time it is used.
void lib_mcp_forget(const std::string& id) {
    std::lock_guard lk(g_mcp_mu);
    const auto it = g_mcp.find("*/" + id);
    if (it == g_mcp.end()) return;
    if (it->second) { std::lock_guard l2(it->second->mu); mcp_kill(*it->second); }
    g_mcp.erase(it);
}
// ---------- community catalog (schema 2) ----------
// The website also publishes every MCP server in the official MCP Registry, Agent Skills and Claude Code plugins from
// public GitHub repos, AI command-line tools and an AI app directory (library/import/index_all.js). Rules here:
//   * MCP packages run pinned (npx pkg@x.y.z, uvx pkg==x.y.z, docker image:tag); remote servers must be https
//   * skills and plugins are downloaded from GitHub at the commit that was indexed, and every file must match its SHA-256
//   * text files only; no hooks, no A.S.T.R.O plugin UI scripts (main.js / style.css); plugin MCP servers must be remote
//   * command-line tools install into ext/tools/<id> (never system-wide) and are added to PATH for the agent and terminal
std::string library_v2_base() { return library_base_url() + "v2/"; }
// Where skill and plugin files are downloaded from (pinned commits on GitHub). Tests point this at 127.0.0.1.
std::string lib_raw_base() {
    if (const char* u = std::getenv("ASTRO_LIBRARY_RAW_BASE"); u && *u) { bool local = false; if (library_url_ok(std::string(u) + "x", local)) return u; }
    return "https://raw.githubusercontent.com/";
}
fs::path lib_v2_cache_dir() { return fs::path(data_dir()) / ".astro_library_cache"; }
bool lib_v2_file_ok(const std::string& f) {
    static const std::regex re(R"(^(?:manifest\.json|list/(?:mcp|skill|plugin|tool)-\d{1,3}\.json|detail/[0-9a-f]{2}\.json)$)");
    return std::regex_match(f, re);
}
// Downloads a catalog file, keeping a copy on disk so browsing works offline and repeat visits are instant.
std::string lib_v2_get(const std::string& file, bool force, std::string& error) {
    if (!lib_v2_file_ok(file)) { error = "Unknown catalog file."; return {}; }
    std::string flat = file; std::replace(flat.begin(), flat.end(), '/', '_');
    const fs::path cached = lib_v2_cache_dir() / flat;
    std::error_code ec;
    const long long max_age = file == "manifest.json" ? 60LL * 60 : 6LL * 60 * 60;
    if (!force && fs::exists(cached, ec)) {
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - fs::last_write_time(cached, ec)).count();
        if (age >= 0 && age < max_age) if (auto t = read_small(cached, 40'000'000)) return *t;
    }
    std::string body = library_get(library_v2_base() + file, 40'000'000, error);
    if (body.empty()) {
        if (auto t = read_small(cached, 40'000'000)) { error.clear(); return *t; }   // offline: use the last copy
        if (error.empty()) error = "The catalog file is empty.";
        return {};
    }
    fs::create_directories(lib_v2_cache_dir(), ec);
    write_file_private(cached.string(), body);
    return body;
}
std::optional<Json> lib_v2_detail(const std::string& id, std::string& error) {
    const std::string file = "detail/" + sha256_hex(id).substr(0, 2) + ".json";
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::string body = lib_v2_get(file, attempt == 1, error);
        if (body.empty()) return std::nullopt;
        try {
            Json j = JsonParser(body).parse();
            if (const Json* it = j.get(id); it && it->object_ptr()) return *it;
        } catch (const std::exception& e) { error = std::string("The catalog is not valid: ") + e.what(); }
    }
    if (error.empty()) error = "That item is not in the A.S.T.R.O AI Library (it may have been removed).";
    return std::nullopt;
}

bool lib_repo_ok(const std::string& r) { static const std::regex re(R"(^[A-Za-z0-9_.-]{1,100}/[A-Za-z0-9_.-]{1,100}$)"); return std::regex_match(r, re) && r.find("..") == std::string::npos; }
bool lib_commit_ok(const std::string& c) { static const std::regex re(R"(^[0-9a-f]{40}$)"); return std::regex_match(c, re); }
bool lib_reldir_ok(const std::string& d) {
    if (d.empty()) return true;
    if (d.size() > 300 || d.front() == '/' || d.back() == '/' || d.find('\\') != std::string::npos || d.find('\0') != std::string::npos) return false;
    std::stringstream ss(d); std::string part;
    while (std::getline(ss, part, '/')) if (part.empty() || part == "." || part == "..") return false;
    return true;
}
// Same list as the indexer: text formats only.
bool lib_community_file_ok(const std::string& rel, const std::string& kind) {
    if (!lib_reldir_ok(rel) || rel.empty() || rel.size() > 240) return false;
    for (unsigned char c : rel) if (c < 0x20 || std::strchr("\\:*?\"<>|", c)) return false;
    static const std::regex skip(R"((^|/)(\.git|node_modules|__pycache__|\.venv|venv|dist|build|target|\.next|coverage|\.cache|hooks)(/|$))");
    if (std::regex_search(rel, skip)) return false;
    const std::string base = lower_copy(rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1));
    if (base == "hooks.json") return false;
    if (kind == "plugin" && (rel == "main.js" || rel == "style.css")) return false;
    static const std::set<std::string> exts = {"md","mdx","markdown","txt","rst","json","jsonc","json5","yaml","yml","toml","ini","cfg","conf","csv","tsv","xml","html","htm","css","scss","js","mjs","cjs","ts","tsx","jsx","mts","cts","py","pyi","sh","bash","zsh","fish","ps1","psm1","bat","cmd","rb","go","rs","java","kt","kts","swift","c","h","cc","cpp","hpp","cs","php","sql","r","lua","pl","pm","ex","exs","erl","hs","ml","scala","clj","dart","vue","svelte","astro","graphql","gql","proto","tf","hcl","nix","dockerfile","makefile","mk","gradle","properties","env-example","template","tpl","j2","jinja","hbs","mustache","ejs","liquid","tex","bib","sty","cls","typ","ipynb"};
    static const std::set<std::string> names = {"license","licence","notice","makefile","dockerfile","readme","changelog","authors","contributing",".gitignore",".env.example","license.md","license.txt","readme.md","readme.txt","notice.md","notice.txt","changelog.md","authors.md","contributing.md"};
    const auto dot = base.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : base.substr(dot + 1);
    return exts.count(ext) || names.count(base);
}
std::string lib_path_escape(const std::string& rel) {
    std::string out; std::stringstream ss(rel); std::string part; bool first = true;
    while (std::getline(ss, part, '/')) { if (!first) out += '/'; out += url_escape(part); first = false; }
    return out;
}

// The PATH Astro hands to the agent, the terminal and MCP servers: command-line tools from the library first.
std::mutex g_tool_path_mu;
std::optional<std::string> g_base_path;
// Only the commands each tool declares are exposed (through ext/tools/.bin), so a tool's own dependencies
// (tsc, semver, ...) never shadow programs you already have.
std::string tools_path_prefix(char sep);   /* astro_tools.inc: bin folders of installed compilers/runtimes/servers */
void lib_tools_path_refresh() {
    std::lock_guard lk(g_tool_path_mu);
#ifdef _WIN32
    if (!g_base_path) g_base_path = winp::getenv_s8("PATH");
    const char sep = ';';
#else
    if (!g_base_path) { const char* p = std::getenv("PATH"); g_base_path = p ? p : "/usr/local/bin:/usr/bin:/bin"; }
    const char sep = ':';
#endif
    std::error_code ec;
    const fs::path tools = ext_root() / "tools", bin = tools / ".bin";
    fs::remove_all(bin, ec);
    int linked = 0;
    static const std::regex bin_re(R"(^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$)");
    for (const auto& [id, r] : lib_receipts()) {
        if (get_string(r, "kind") != "tool" || !lib_id_ok(id)) continue;
        const bool py = get_string(r, "package").find("==") != std::string::npos;
        for (const auto& b : json_str_list(r.get("bins"))) {
            if (!std::regex_match(b, bin_re)) continue;
#ifdef _WIN32
            const fs::path target = py ? tools / id / "venv" / "Scripts" / (b + ".exe") : tools / id / "node_modules" / ".bin" / (b + ".cmd");
            if (!fs::exists(target, ec)) continue;
            fs::create_directories(bin, ec);
            std::ofstream out(bin / (b + ".cmd"), std::ios::binary | std::ios::trunc);
            out << "@echo off\r\n\"" << target.string() << "\" %*\r\n";
#else
            const fs::path target = py ? tools / id / "venv" / "bin" / b : tools / id / "node_modules" / ".bin" / b;
            if (!fs::exists(target, ec)) continue;
            fs::create_directories(bin, ec);
            fs::create_symlink(target, bin / b, ec);
#endif
            ++linked;
        }
    }
    const std::string extra = tools_path_prefix(sep);   /* "dir1<sep>dir2<sep>" or "" */
    const std::string path = (linked ? bin.string() + sep : std::string()) + extra + *g_base_path;
#ifdef _WIN32
    SetEnvironmentVariableW(L"PATH", winp::widen(path).c_str());
#else
    setenv("PATH", path.c_str(), 1);
    {   /* sandboxed runners use a fixed PATH plus these folders */
        std::string tb = linked ? bin.string() : std::string();
        if (!extra.empty()) tb += (tb.empty() ? "" : ":") + extra.substr(0, extra.size() - 1);
        if (!tb.empty()) setenv("ASTRO_TOOL_BIN", tb.c_str(), 1); else unsetenv("ASTRO_TOOL_BIN");
    }
#endif
}

// Downloads files in parallel. Returns false (and sets error) if any download fails or doesn't match its checksum.
struct LibFile { std::string rel, sha; std::size_t size = 0; std::string data; };
bool lib_download_files(std::vector<LibFile>& files, const std::string& base_url, std::string& error) {
    std::atomic<std::size_t> next{0};
    std::mutex emu; std::string first_error;
    auto worker = [&]() {
        for (;;) {
            const std::size_t i = next++;
            if (i >= files.size()) return;
            { std::lock_guard l(emu); if (!first_error.empty()) return; }
            LibFile& f = files[i];
            std::string err;
            std::string data;
            for (int attempt = 0; attempt < 3 && data.empty(); ++attempt) {
                err.clear();
                bool local = false; library_url_ok(base_url + "x", local);
                const FetchResult r = http_fetch(base_url + lib_path_escape(f.rel), {"Accept: */*"}, nullptr, 60, 1'200'000, !local);
                if (!r.error.empty()) err = r.error;
                else if (r.truncated) err = "too large";
                else if (r.status != 200) err = "HTTP " + std::to_string(r.status);
                else { data = r.body; if (data.empty() && f.size == 0) break; }
            }
            std::lock_guard l(emu);
            if (!err.empty()) { if (first_error.empty()) first_error = "Could not download " + truncate(f.rel, 80) + " (" + err + ")."; return; }
            if (sha256_hex(data) != f.sha) { if (first_error.empty()) first_error = "Checksum mismatch for " + truncate(f.rel, 80) + ". Nothing was installed."; return; }
            if (data.find('\0') != std::string::npos) { if (first_error.empty()) first_error = "Binary content is not allowed: " + truncate(f.rel, 80); return; }
            f.data = std::move(data);
        }
    };
    std::vector<std::thread> pool;
    const std::size_t n = std::min<std::size_t>(8, files.size());
    for (std::size_t i = 0; i < n; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
    if (!first_error.empty()) { error = first_error; return false; }
    return true;
}

// Writes the MCP server into ext/mcp.json under the item id. Returns false after sending an error.
bool lib_save_mcp(int fd, const std::string& id, Json server, bool reinstall, const Json& body) {
    std::string error;
    auto root = lib_mcp_load(error);
    if (!root) { send_json(fd, 409, error_json(error)); return false; }
    Json::Object servers = *(*root)["mcpServers"].object_ptr();
    if (servers.count(id) && !reinstall && !json_bool(body, "replace", false)) {
        Json::Object o; o["error"] = Json("You already have an MCP server named \"" + id + "\" that did not come from the library."); o["conflict"] = Json(true);
        send_json(fd, 409, Json(std::move(o))); return false;
    }
    servers[id] = std::move(server);
    (*root)["mcpServers"] = Json(std::move(servers));
    if (!write_file_private((ext_root() / "mcp.json").string(), dump_json(Json(std::move(*root))))) { send_json(fd, 500, error_json("Could not save mcp.json.")); return false; }
    lib_mcp_forget(id);
    return true;
}

void lib_install_community(int fd, const Json& body, const std::string& id, const Json& item, Json::Object& receipts, bool reinstall) {
    const std::string k = get_string(item, "k"), name = get_string(item, "n", id);
    Json::Object receipt; receipt["kind"] = Json(k); receipt["name"] = Json(name); receipt["source"] = Json("community");
    receipt["version"] = Json(get_string(item, "v")); receipt["installed_at"] = Json(static_cast<double>(now_ms()));
    const Json* given = body.get("inputs");
    auto input = [&](const std::string& key) { return given ? trim(get_string(*given, key)) : std::string(); };
    std::error_code ec;
    Json::Array notes;

    if (k == "mcp") {
        const Json* spec = item.get("i");
        if (!spec || !spec->object_ptr()) { send_json(fd, 422, error_json("This server has no one-click setup (its package type isn't supported yet). Use the links on its page to set it up by hand.")); return; }
        const std::string t = get_string(*spec, "t");
        // Declared placeholders ({NAME}) for arguments and the URL.
        std::map<std::string, std::string> values;
        if (const Json* decl = item.get("in"); decl && decl->array_ptr()) for (const auto& in : *decl->array_ptr()) {
            const std::string iname = get_string(in, "name");
            std::string v = input(iname);
            if (v.empty()) v = get_string(in, "def");
            if (v.empty()) { if (json_bool(in, "req", false) || json_int(in, "req", 0)) { send_json(fd, 400, error_json(get_string(in, "label", iname) + " is required.")); return; } continue; }
            if (json_int(in, "url", 0)) { static const std::regex ure(R"(^[A-Za-z0-9._~-]{1,200}$)"); if (!std::regex_match(v, ure)) { send_json(fd, 400, error_json(get_string(in, "label", iname) + ": use letters, numbers, dots, dashes or underscores.")); return; } }
            else if (!lib_value_ok(v, true)) { send_json(fd, 400, error_json(get_string(in, "label", iname) + ": that value has characters that are not allowed.")); return; }
            values[iname] = v;
        }
        auto fill = [&](std::string s, std::string& missing) {
            std::string out; std::size_t i = 0;
            while (i < s.size()) {
                const auto o = s.find('{', i);
                if (o == std::string::npos) { out += s.substr(i); break; }
                const auto c = s.find('}', o);
                if (c == std::string::npos) { out += s.substr(i); break; }
                const std::string key = s.substr(o + 1, c - o - 1);
                out += s.substr(i, o - i);
                if (auto it = values.find(key); it != values.end()) out += it->second; else { missing = key; return std::string(); }
                i = c + 1;
            }
            return out;
        };
        Json::Object server;
        if (t == "http" || t == "sse") {
            std::string missing;
            const std::string url = fill(get_string(*spec, "url"), missing);
            if (!missing.empty()) { send_json(fd, 400, error_json("Missing value for " + missing + ".")); return; }
            if (!lib_https_url(url)) { send_json(fd, 422, error_json("The catalog entry has an unsafe address.")); return; }
            Json::Object headers;
            if (const Json* hs = spec->get("headers"); hs && hs->array_ptr()) for (const auto& h : *hs->array_ptr()) {
                const std::string hn = get_string(h, "h");
                static const std::regex hre(R"(^[A-Za-z0-9_-]{1,64}$)");
                if (!std::regex_match(hn, hre)) continue;
                std::string v = get_string(h, "value");
                if (v.empty()) { v = input(hn); if (!v.empty()) { const std::string pre = get_string(h, "prefix"); if (!pre.empty() && v.rfind(pre, 0) != 0) v = pre + v; } }
                if (v.empty()) { if (json_int(h, "req", 0)) { send_json(fd, 400, error_json(get_string(h, "label", hn) + " is required.")); return; } continue; }
                if (!lib_value_ok(v, false)) { send_json(fd, 400, error_json(hn + ": that value has characters that are not allowed.")); return; }
                headers[hn] = Json(v);
            }
            server["type"] = Json(t == "sse" ? "sse" : "http"); server["url"] = Json(url);
            if (!headers.empty()) server["headers"] = Json(std::move(headers));
            if (t == "sse") notes.emplace_back(Json("This server uses the older SSE transport; if it doesn't connect, ask its publisher for a Streamable HTTP address."));
        } else if (t == "stdio") {
            const std::string cmd = get_string(*spec, "cmd");
            const auto args = json_str_list(spec->get("args"));
            static const std::regex pypi(R"(^[A-Za-z0-9][A-Za-z0-9._-]*==[0-9][0-9A-Za-z.+!-]*$)");
            static const std::regex image(R"(^[a-z0-9][a-z0-9._/-]*(?::[A-Za-z0-9._-]{1,128}|@sha256:[a-f0-9]{64})$)");
            bool pinned = false;
            if (cmd == "npx") pinned = args.size() >= 2 && args[0] == "-y" && lib_pkg_ok(args[1]);
            else if (cmd == "uvx") pinned = !args.empty() && std::regex_match(args[0], pypi);
            else if (cmd == "docker") {
                pinned = args.size() >= 4 && args[0] == "run" && args[1] == "-i" && args[2] == "--rm" &&
                         std::any_of(args.begin() + 3, args.end(), [](const std::string& a) { return std::regex_match(a, image) && a.find(":latest") == std::string::npos; });
                for (const auto& a : args) { const std::string l = lower_copy(a); for (const char* bad : {"--privileged", "--cap-add", "--pid", "--network=host", "--net=host", "--device", "--security-opt", "--ipc", "--userns", "--uts"}) if (l.rfind(bad, 0) == 0) pinned = false; }
            }
            if (!pinned) { send_json(fd, 422, error_json("The catalog entry is not pinned to an exact package version, so it was not installed.")); return; }
            Json::Array out_args;
            for (const auto& a0 : args) {
                std::string missing;
                const std::string a = fill(a0, missing);
                if (!missing.empty()) { send_json(fd, 400, error_json("Missing value for " + missing + ".")); return; }
                if (a.size() > 600 || !lib_value_ok(a, true)) { send_json(fd, 422, error_json("The catalog entry has an unsafe argument.")); return; }
                out_args.emplace_back(Json(a));
            }
            Json::Object env;
            if (const Json* es = spec->get("env"); es && es->array_ptr()) for (const auto& e : *es->array_ptr()) {
                const std::string en = get_string(e, "name");
                static const std::regex ere(R"(^[A-Za-z_][A-Za-z0-9_]{0,63}$)");
                if (!std::regex_match(en, ere)) continue;
                std::string v = input(en);
                if (v.empty()) v = get_string(e, "def");
                if (v.empty()) { if (json_int(e, "req", 0)) { send_json(fd, 400, error_json(en + " is required (" + truncate(get_string(e, "label"), 120) + ").")); return; } continue; }
                if (!lib_value_ok(v, false)) { send_json(fd, 400, error_json(en + ": that value has characters that are not allowed.")); return; }
                env[en] = Json(v);
            }
            server["command"] = Json(cmd); server["args"] = Json(std::move(out_args));
            if (!env.empty()) server["env"] = Json(std::move(env));
            if (cmd == "uvx" && !have_tool("uvx")) notes.emplace_back(Json("This server runs with uv. Install uv (https://docs.astral.sh/uv/) if it doesn't start."));
            if (cmd == "docker" && !have_tool("docker")) notes.emplace_back(Json("This server runs in Docker. Install Docker Desktop if it doesn't start."));
            if (cmd == "npx" && !have_tool("npx")) notes.emplace_back(Json("This server needs Node.js (npx). Install Node.js if it doesn't start."));
        } else { send_json(fd, 422, error_json("Unknown server type in the catalog.")); return; }
        if (!lib_save_mcp(fd, id, Json(std::move(server)), reinstall, body)) return;
        receipt["mcp"] = Json(id);
        receipt["registry"] = Json(get_string(item, "reg"));
    } else if (k == "skill" || k == "plugin") {
        const std::string repo = get_string(item, "repo"), commit = get_string(item, "c"), dir = get_string(item, "dir");
        if (!lib_repo_ok(repo) || !lib_commit_ok(commit) || !lib_reldir_ok(dir)) { send_json(fd, 422, error_json("The catalog entry has an invalid source.")); return; }
        const Json* fl = item.get("f");
        if (!fl || !fl->array_ptr() || fl->array_ptr()->empty() || fl->array_ptr()->size() > 400) { send_json(fd, 422, error_json("The catalog entry has no files.")); return; }
        std::vector<LibFile> files; std::size_t total = 0; bool has_skill = false;
        for (const auto& f : *fl->array_ptr()) {
            const Json::Array* a = f.array_ptr();
            if (!a || a->size() < 2 || !(*a)[0].string_ptr() || !(*a)[1].string_ptr()) { send_json(fd, 422, error_json("The catalog entry is damaged.")); return; }
            LibFile lf; lf.rel = *(*a)[0].string_ptr(); lf.sha = lower_copy(*(*a)[1].string_ptr());
            if (a->size() > 2) if (const double* d = std::get_if<double>(&(*a)[2].value)) lf.size = static_cast<std::size_t>(*d);
            if (!lib_community_file_ok(lf.rel, k)) { send_json(fd, 422, error_json("The catalog lists a file that is not allowed: " + truncate(lf.rel, 80))); return; }
            if (lf.sha.size() != 64) { send_json(fd, 422, error_json("The catalog entry is missing a checksum.")); return; }
            total += lf.size;
            if (lower_copy(lf.rel) == "skill.md") has_skill = true;
            files.push_back(std::move(lf));
        }
        if (total > 8'500'000) { send_json(fd, 422, error_json("The item is larger than allowed.")); return; }
        if (k == "skill" && !has_skill) { send_json(fd, 422, error_json("The skill has no SKILL.md.")); return; }
        const fs::path dest_root = ext_root() / (k + "s");
        const fs::path dest = dest_root / id;
        if (fs::exists(dest, ec) && !reinstall && !json_bool(body, "replace", false)) {
            Json::Object o; o["error"] = Json("You already have a " + k + " named \"" + id + "\" that did not come from the library."); o["conflict"] = Json(true);
            send_json(fd, 409, Json(std::move(o))); return;
        }
        std::string error;
        const std::string base = lib_raw_base() + repo + "/" + commit + "/" + (dir.empty() ? std::string() : lib_path_escape(dir) + "/");
        if (!lib_download_files(files, base, error)) { send_json(fd, 502, error_json(error)); return; }
        const fs::path stage = ext_root() / ".library-staging" / (id + "-" + random_hex(6));
        fs::create_directories(stage, ec);
        auto fail = [&](int code, const std::string& why) { fs::remove_all(stage, ec); send_json(fd, code, error_json(why)); };
        Json::Array written;
        for (auto& f : files) {
            std::string data = std::move(f.data);
            // Plugins: keep only remote MCP servers and never register hooks.
            if (k == "plugin" && f.rel == ".mcp.json") {
                try {
                    Json j = JsonParser(data).parse();
                    Json::Object root = j.object_ptr() ? *j.object_ptr() : Json::Object{};
                    Json::Object kept;
                    const Json* sv = j.get("mcpServers");
                    if (sv && sv->object_ptr()) for (const auto& [n, s] : *sv->object_ptr()) {
                        const std::string ty = lower_copy(get_string(s, "type"));
                        if (!s.get("command") && lib_https_url(get_string(s, "url"))) kept[n] = s;
                        else notes.emplace_back(Json("Skipped the plugin's local MCP server \"" + n + "\" (only remote https servers are added from community plugins). You can add it yourself in Extensions → MCP."));
                        (void)ty;
                    }
                    root["mcpServers"] = Json(std::move(kept));
                    data = dump_json(Json(std::move(root)));
                } catch (const std::exception&) { fail(422, ".mcp.json is not valid JSON."); return; }
            }
            if (k == "plugin" && f.rel == ".claude-plugin/plugin.json") {
                try {
                    Json j = JsonParser(data).parse();
                    if (const auto* o = j.object_ptr(); o && o->count("hooks")) { Json::Object copy = *o; copy.erase("hooks"); data = dump_json(Json(std::move(copy))); notes.emplace_back(Json("The plugin's hooks were not installed.")); }
                } catch (const std::exception&) { fail(422, "plugin.json is not valid JSON."); return; }
            }
            const auto target = safe_join(stage, f.rel);
            if (!target) { fail(422, "Unsafe file path: " + f.rel); return; }
            fs::create_directories(target->parent_path(), ec);
            std::ofstream out(*target, std::ios::binary | std::ios::trunc);
            out << data;
            if (!out) { fail(500, "Could not write " + f.rel); return; }
            written.emplace_back(Json(f.rel));
        }
        if (k == "skill" && !fs::is_regular_file(stage / "SKILL.md", ec) && fs::is_regular_file(stage / "skill.md", ec)) fs::rename(stage / "skill.md", stage / "SKILL.md", ec);
        fs::remove_all(dest, ec);
        fs::create_directories(dest_root, ec);
        fs::rename(stage, dest, ec);
        if (ec) { fail(500, "Could not move the files into place: " + ec.message()); return; }
        receipt["path"] = Json(k + "s/" + id);
        receipt["files"] = Json(std::move(written));
        receipt["repo"] = Json(repo); receipt["commit"] = Json(commit);
        if (k == "plugin") {
            Json::Object st; if (auto t = read_small(ext_root() / "plugins.json", 200000)) { try { Json j = JsonParser(*t).parse(); if (auto* p = j.object_ptr()) st = *p; } catch (...) {} }
            st[id] = Json(true);
            write_file_private((ext_root() / "plugins.json").string(), dump_json(Json(std::move(st))));
        }
    } else if (k == "tool") {
        const std::string t = get_string(item, "t"), pkg = get_string(item, "pkg");
        if (t == "link") { send_json(fd, 422, error_json("This is an app or website, not something to install. Open it from its page.")); return; }
        const fs::path dir = ext_root() / "tools" / id;
        static const std::regex pypi(R"(^[A-Za-z0-9][A-Za-z0-9._-]*==[0-9][0-9A-Za-z.+!-]*$)");
        if (t == "npm") {
            if (!lib_pkg_ok(pkg)) { send_json(fd, 422, error_json("The catalog entry is not pinned to an exact version.")); return; }
            if (!have_tool("npm")) { send_json(fd, 424, error_json("This tool needs Node.js (npm). Install Node.js from https://nodejs.org and try again.")); return; }
            fs::create_directories(dir, ec);
            const Captured r = run_capture({"npm", "install", "--prefix", dir.string(), "--no-audit", "--no-fund", "--loglevel=error", "--omit=dev", pkg}, dir, {}, 900, 400000);
            if (r.code != 0) { fs::remove_all(dir, ec); send_json(fd, 502, error_json("npm could not install " + pkg + ": " + truncate(trim(r.err.empty() ? r.out : r.err), 600))); return; }
        } else if (t == "pypi") {
            if (!std::regex_match(pkg, pypi)) { send_json(fd, 422, error_json("The catalog entry is not pinned to an exact version.")); return; }
            std::string py;
            for (const char* c : {"python3", "python"}) if (have_tool(c)) { py = c; break; }
            if (py.empty()) { send_json(fd, 424, error_json("This tool needs Python 3. Install it from https://www.python.org and try again.")); return; }
            fs::create_directories(dir, ec);
            Captured r = run_capture({py, "-m", "venv", (dir / "venv").string()}, dir, {"PIP_USER=0"}, 300, 200000);
            if (r.code != 0) { fs::remove_all(dir, ec); send_json(fd, 502, error_json("Could not create a Python environment: " + truncate(trim(r.err), 400))); return; }
#ifdef _WIN32
            const fs::path vpy = dir / "venv" / "Scripts" / "python.exe";
#else
            const fs::path vpy = dir / "venv" / "bin" / "python";
#endif
            r = run_capture({vpy.string(), "-m", "pip", "install", "--disable-pip-version-check", "--no-input", "-q", pkg}, dir, {"PIP_USER=0"}, 900, 400000);   // a venv install, never --user
            if (r.code != 0) { fs::remove_all(dir, ec); send_json(fd, 502, error_json("pip could not install " + pkg + ": " + truncate(trim(r.err.empty() ? r.out : r.err), 600))); return; }
        } else { send_json(fd, 422, error_json("Unknown tool type in the catalog.")); return; }
        receipt["path"] = Json("tools/" + id);
        receipt["package"] = Json(pkg);
        receipt["bins"] = Json(item.get("bins") && item.get("bins")->array_ptr() ? *item.get("bins") : Json(Json::Array{}));
        notes.emplace_back(Json("Added to PATH for the agent and terminal: " + [&]() { std::string s; for (const auto& b : json_str_list(item.get("bins"))) s += (s.empty() ? "" : ", ") + b; return s.empty() ? pkg : s; }()));
    } else { send_json(fd, 422, error_json("Unknown item type in the catalog.")); return; }

    receipts[id] = Json(std::move(receipt));
    lib_save_receipts(receipts);
    if (k == "tool") lib_tools_path_refresh();
    fs::remove_all(ext_root() / ".library-staging", ec);
    Json::Object o; o["ok"] = Json(true); o["id"] = Json(id); o["kind"] = Json(k); o["version"] = Json(get_string(item, "v")); o["updated"] = Json(reinstall);
    o["community"] = Json(true);
    if (!notes.empty()) o["notes"] = Json(std::move(notes));
    send_json(fd, 200, Json(std::move(o)));
}

void lib_install(int fd, const Json& body) {
    const std::string id = get_string(body, "id");
    if (!lib_id_ok(id)) { send_json(fd, 400, error_json("Unknown library item.")); return; }
    std::string error;
    const Json index = library_index(true, error);
    const Json* item = index.object_ptr() ? lib_find(index, id) : nullptr;
    if (!item) {   // not one of the reviewed items: look in the community catalog
        std::string e2;
        const auto community = lib_v2_detail(id, e2);
        if (!community) {
            if (!index.object_ptr()) { send_json(fd, 502, error_json(error)); return; }
            const bool offline = e2.find("Could not reach") != std::string::npos;
            send_json(fd, offline ? 502 : 404, error_json(offline ? e2 : std::string("That item is not in the A.S.T.R.O AI Library (it may have been removed).")));
            return;
        }
        auto receipts = lib_receipts();
        const bool reinstall = receipts.count(id) > 0;
        lib_install_community(fd, body, id, *community, receipts, reinstall);
        return;
    }
    const std::string kind = get_string(*item, "kind"), version = get_string(*item, "version"), name = get_string(*item, "name", id);
    auto receipts = lib_receipts();
    const bool reinstall = receipts.count(id) > 0;
    Json::Object receipt; receipt["kind"] = Json(kind); receipt["name"] = Json(name); receipt["version"] = Json(version);
    receipt["installed_at"] = Json(static_cast<double>(now_ms()));
    std::error_code ec;

    if (kind == "mcp") {
        const Json* spec = item->get("mcp");
        if (!spec || !spec->object_ptr()) { send_json(fd, 422, error_json("The catalog entry has no server details.")); return; }
        const std::string type = get_string(*spec, "type");
        const Json* inputs_given = body.get("inputs");
        std::map<std::string, std::string> values;
        Json::Object env, headers;
        // Inputs: only the names the catalog declares are accepted.
        if (const Json* decl = item->get("inputs"); decl && decl->array_ptr()) for (const auto& in : *decl->array_ptr()) {
            const std::string iname = get_string(in, "name"), ikind = get_string(in, "kind");
            std::string v = inputs_given ? trim(get_string(*inputs_given, iname)) : std::string();
            if (v.empty()) { if (json_bool(in, "required", false)) { send_json(fd, 400, error_json(get_string(in, "label", iname) + " is required.")); return; } continue; }
            if (!lib_value_ok(v, ikind == "arg")) { send_json(fd, 400, error_json(get_string(in, "label", iname) + ": that value has characters that are not allowed.")); return; }
            if (ikind == "arg" && iname == "FOLDER") {
                const fs::path p(v);
                if (!p.is_absolute() || !fs::is_directory(p, ec)) { send_json(fd, 400, error_json("Choose an existing folder (a full path).")); return; }
                if (p == user_home_dir() || !p.has_relative_path()) { send_json(fd, 400, error_json("Pick a specific folder, not your whole home folder or drive.")); return; }
            }
            if (ikind == "env") env[iname] = Json(v);
            else if (ikind == "header") { const std::string h = get_string(in, "header"); if (!h.empty()) headers[h] = Json(get_string(in, "prefix") + v); }
            else values[iname] = v;
        }
        Json::Object server;
        if (type == "http") {
            const std::string url = get_string(*spec, "url");
            if (!lib_https_url(url)) { send_json(fd, 422, error_json("The catalog entry has an unsafe address.")); return; }
            server["type"] = Json("http"); server["url"] = Json(url);
            if (!headers.empty()) server["headers"] = Json(std::move(headers));
        } else if (type == "stdio") {
            const std::string cmd = get_string(*spec, "command");
            const auto args = json_str_list(spec->get("args"));
            if (cmd != "npx" || args.size() < 2 || args[0] != "-y" || !lib_pkg_ok(args[1])) { send_json(fd, 422, error_json("The catalog entry is not pinned to an exact package version, so it was not installed.")); return; }
            Json::Array out_args;
            for (std::size_t i = 0; i < args.size(); ++i) {
                std::string a = args[i];
                if (i >= 2 && !lib_flag_ok(a)) { send_json(fd, 422, error_json("The catalog entry has an unsafe argument.")); return; }
                if (a.size() > 2 && a.front() == '{' && a.back() == '}') {
                    const auto vit = values.find(a.substr(1, a.size() - 2));
                    if (vit == values.end()) { send_json(fd, 400, error_json("Missing value for " + a + ".")); return; }
                    a = vit->second;
                }
                out_args.emplace_back(Json(a));
            }
            server["command"] = Json(cmd); server["args"] = Json(std::move(out_args));
            if (!env.empty()) server["env"] = Json(std::move(env));
        } else { send_json(fd, 422, error_json("Unknown server type in the catalog.")); return; }

        auto root = lib_mcp_load(error);
        if (!root) { send_json(fd, 409, error_json(error)); return; }
        Json::Object servers = *(*root)["mcpServers"].object_ptr();
        if (servers.count(id) && !reinstall && !json_bool(body, "replace", false)) {
            Json::Object o; o["error"] = Json("You already have an MCP server named \"" + id + "\" that did not come from the library."); o["conflict"] = Json(true);
            send_json(fd, 409, Json(std::move(o))); return;
        }
        servers[id] = Json(std::move(server));
        (*root)["mcpServers"] = Json(std::move(servers));
        if (!write_file_private((ext_root() / "mcp.json").string(), dump_json(Json(std::move(*root))))) { send_json(fd, 500, error_json("Could not save mcp.json.")); return; }
        receipt["mcp"] = Json(id);
        // The running connection (if any) restarts with the new settings the next time it is used.
        lib_mcp_forget(id);
    } else if (kind == "skill" || kind == "plugin") {
        const Json* files = item->get("files");
        if (!files || !files->array_ptr() || files->array_ptr()->empty() || files->array_ptr()->size() > 80) { send_json(fd, 422, error_json("The catalog entry has no files.")); return; }
        const fs::path dest_root = ext_root() / (kind + "s");
        const fs::path dest = dest_root / id;
        if (fs::exists(dest, ec) && !reinstall && !json_bool(body, "replace", false)) {
            Json::Object o; o["error"] = Json("You already have a " + kind + " named \"" + id + "\" that did not come from the library."); o["conflict"] = Json(true);
            send_json(fd, 409, Json(std::move(o))); return;
        }
        const fs::path stage = ext_root() / ".library-staging" / (id + "-" + random_hex(6));
        fs::create_directories(stage, ec);
        auto fail = [&](int code, const std::string& why) { fs::remove_all(stage, ec); send_json(fd, code, error_json(why)); };
        const std::string base = library_base_url() + get_string(index, "files_base", "files/") + kind + "s/" + id + "/";
        Json::Array written;
        std::size_t total = 0;
        for (const auto& f : *files->array_ptr()) {
            const std::string rel = get_string(f, "path"), want = lower_copy(get_string(f, "sha256"));
            if (!lib_file_ok(rel)) { fail(422, "The catalog lists a file type that is not allowed: " + truncate(rel, 80)); return; }
            if (want.size() != 64) { fail(422, "The catalog entry is missing a checksum."); return; }
            if (rel == ".mcp.json" && kind != "plugin") { fail(422, "Only plugins can bundle MCP servers."); return; }
            std::string data = library_get(base + rel, 300'000, error);
            if (!error.empty()) { fail(502, error); return; }
            if (sha256_hex(data) != want) { fail(422, "Checksum mismatch for " + rel + ". Nothing was installed. (The download was changed or corrupted.)"); return; }
            if (data.find('\0') != std::string::npos) { fail(422, "Binary content is not allowed: " + rel); return; }
            total += data.size();
            if (total > 2'000'000) { fail(422, "The item is larger than allowed."); return; }
            if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".json") == 0) {
                try {
                    Json j = JsonParser(data).parse();
                    if (rel == ".mcp.json") {
                        const Json* sv = j.get("mcpServers");
                        if (sv && sv->object_ptr()) for (const auto& [n, s] : *sv->object_ptr())
                            if (get_string(s, "type") != "http" || !lib_https_url(get_string(s, "url")) || s.get("command")) { fail(422, "Plugin MCP servers must be remote (https). \"" + n + "\" is not."); return; }
                    }
                } catch (const std::exception&) { fail(422, rel + " is not valid JSON."); return; }
            }
            const auto target = safe_join(stage, rel);
            if (!target) { fail(422, "Unsafe file path: " + rel); return; }
            fs::create_directories(target->parent_path(), ec);
            std::ofstream out(*target, std::ios::binary | std::ios::trunc);
            out << data;
            if (!out) { fail(500, "Could not write " + rel); return; }
            written.emplace_back(Json(rel));
        }
        if (kind == "skill" && !fs::is_regular_file(stage / "SKILL.md", ec)) { fail(422, "The skill has no SKILL.md."); return; }
        fs::remove_all(dest, ec);
        fs::create_directories(dest_root, ec);
        fs::rename(stage, dest, ec);
        if (ec) { fail(500, "Could not move the files into place: " + ec.message()); return; }
        receipt["path"] = Json(kind + "s/" + id);
        receipt["files"] = Json(std::move(written));
        if (kind == "plugin") {   // enabled by default; the user can switch it off in Extensions → Plugins
            Json::Object st; if (auto t = read_small(ext_root() / "plugins.json", 200000)) { try { Json j = JsonParser(*t).parse(); if (auto* p = j.object_ptr()) st = *p; } catch (...) {} }
            st[id] = Json(true);
            write_file_private((ext_root() / "plugins.json").string(), dump_json(Json(std::move(st))));
        }
    } else { send_json(fd, 422, error_json("Unknown item type in the catalog.")); return; }

    receipts[id] = Json(std::move(receipt));
    lib_save_receipts(receipts);
    fs::remove_all(ext_root() / ".library-staging", ec);
    Json::Object o; o["ok"] = Json(true); o["id"] = Json(id); o["kind"] = Json(kind); o["version"] = Json(version); o["updated"] = Json(reinstall);
    send_json(fd, 200, Json(std::move(o)));
}

void lib_uninstall(int fd, const Json& body) {
    const std::string id = get_string(body, "id");
    if (!lib_id_ok(id)) { send_json(fd, 400, error_json("Unknown library item.")); return; }
    auto receipts = lib_receipts();
    const auto it = receipts.find(id);
    if (it == receipts.end()) { send_json(fd, 404, error_json("That item was not installed from the library.")); return; }
    std::error_code ec;
    const std::string kind = get_string(it->second, "kind");
    if (kind == "mcp") {
        std::string error;
        auto root = lib_mcp_load(error);
        if (!root) { send_json(fd, 409, error_json(error)); return; }
        Json::Object servers = *(*root)["mcpServers"].object_ptr();
        servers.erase(id);
        (*root)["mcpServers"] = Json(std::move(servers));
        write_file_private((ext_root() / "mcp.json").string(), dump_json(Json(std::move(*root))));
        lib_mcp_forget(id);
    } else if (kind == "tool") {
        if (const auto dir = safe_join(ext_root(), "tools/" + id); dir && fs::exists(*dir, ec)) fs::remove_all(*dir, ec);
    } else if (kind == "skill" || kind == "plugin") {
        const auto dir = safe_join(ext_root(), kind + "s/" + id);
        if (dir && fs::exists(*dir, ec)) {
            // Remove the files we installed; keep anything the user added to the folder.
            for (const auto& f : json_str_list(it->second.get("files"))) if (auto p = safe_join(*dir, f)) fs::remove(*p, ec);
            std::vector<fs::path> dirs;
            for (auto r = fs::recursive_directory_iterator(*dir, ec); r != fs::recursive_directory_iterator(); r.increment(ec)) if (r->is_directory(ec)) dirs.push_back(r->path());
            std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.string().size() > b.string().size(); });
            for (const auto& d : dirs) if (fs::is_empty(d, ec)) fs::remove(d, ec);
            if (fs::is_empty(*dir, ec)) fs::remove(*dir, ec);
        }
        if (kind == "plugin") {
            Json::Object st; if (auto t = read_small(ext_root() / "plugins.json", 200000)) { try { Json j = JsonParser(*t).parse(); if (auto* p = j.object_ptr()) st = *p; } catch (...) {} }
            st.erase(id);
            write_file_private((ext_root() / "plugins.json").string(), dump_json(Json(std::move(st))));
        }
    }
    receipts.erase(it);
    lib_save_receipts(receipts);
    if (kind == "tool") lib_tools_path_refresh();
    Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o)));
}

void dev_library(int fd, const std::string& action, const Json& body) {
    if (action == "index") {
        std::string error;
        Json index = library_index(json_bool(body, "refresh", false), error);
        Json::Object o;
        o["url"] = Json(library_index_url());
        if (index.object_ptr()) { o["items"] = *index.get("items"); o["generated"] = Json(get_string(index, "generated")); }
        else { o["items"] = Json(Json::Array{}); o["error"] = Json(error); }
        o["installed"] = Json(lib_receipts());
        o["node"] = Json(have_tool("npx") || have_tool("node"));
        send_json(fd, 200, Json(std::move(o)));
        return;
    }
    if (action == "v2") {   // community catalog files, cached on this computer
        std::string error;
        const std::string file = get_string(body, "file");
        std::string data = lib_v2_get(file, json_bool(body, "refresh", false), error);
        if (data.empty()) { send_json(fd, 502, error_json(error)); return; }
        send_response(fd, 200, "application/json; charset=utf-8", data);
        return;
    }
    if (action == "detail") {
        std::string error;
        const std::string id = get_string(body, "id");
        if (!lib_id_ok(id)) { send_json(fd, 400, error_json("Unknown library item.")); return; }
        auto d = lib_v2_detail(id, error);
        if (!d) { send_json(fd, 404, error_json(error)); return; }
        Json::Object o; o["item"] = std::move(*d); o["installed"] = Json(lib_receipts().count(id) > 0);
        send_json(fd, 200, Json(std::move(o)));
        return;
    }
    if (action == "installed") { Json::Object o; o["installed"] = Json(lib_receipts()); send_json(fd, 200, Json(std::move(o))); return; }
    if (action == "install") { lib_install(fd, body); return; }
    if (action == "uninstall") { lib_uninstall(fd, body); return; }
    send_json(fd, 404, error_json("Not found."));
}
