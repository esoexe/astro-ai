// =============================================================================================
// Astro AI Library: install MCP servers, skills and plugins from the official catalog
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
    if (!r.error.empty()) { error = "Could not reach the Astro AI Library: " + r.error; return {}; }
    if (r.truncated) { error = "A library file is larger than allowed."; return {}; }
    if (r.status != 200) { error = "The Astro AI Library answered HTTP " + std::to_string(r.status) + " for " + truncate(url, 120); return {}; }
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
        if (json_int(j, "schema", 0) != 1 || !j.get("items") || !j.get("items")->array_ptr()) { error = "The library catalog has an unknown format. Update Astro AI."; return Json(); }
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
void lib_install(int fd, const Json& body) {
    const std::string id = get_string(body, "id");
    if (!lib_id_ok(id)) { send_json(fd, 400, error_json("Unknown library item.")); return; }
    std::string error;
    const Json index = library_index(true, error);
    if (!index.object_ptr()) { send_json(fd, 502, error_json(error)); return; }
    const Json* item = lib_find(index, id);
    if (!item) { send_json(fd, 404, error_json("That item is not in the Astro AI Library (it may have been removed).")); return; }
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
    if (action == "installed") { Json::Object o; o["installed"] = Json(lib_receipts()); send_json(fd, 200, Json(std::move(o))); return; }
    if (action == "install") { lib_install(fd, body); return; }
    if (action == "uninstall") { lib_uninstall(fd, body); return; }
    send_json(fd, 404, error_json("Not found."));
}
