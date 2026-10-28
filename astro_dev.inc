// =============================================================================================
// Astro AI developer platform (included from workspace_ide.inc, inside the anonymous namespace).
// Everything a modern AI IDE / coding agent needs on the server side:
//   * open a real folder on this computer as a project      POST /api/dev/fs/{browse,open,info}
//   * MCP servers (Model Context Protocol), stdio + HTTP     POST /api/dev/mcp/{list,call,restart,stop,trust,config_get,config_set}
//   * skills, slash commands, sub-agents, rules, hooks and plugins, read from Astro's own folders AND the
//     folders used by Claude Code, Codex, Cursor, OpenCode and VS Code    POST /api/dev/scan
//   * the extension store (global skills/commands/agents/plugins/themes/snippets)  POST /api/dev/ext/{read,write,delete,list}
//   * downloads from Open VSX and GitHub (themes, snippets, plugin zips)            POST /api/dev/fetch
//   * problems / diagnostics from the real compilers and linters                     POST /api/dev/diagnostics
// Every call needs login + the x-astro-client header (checked in handle_dev_api).
// =============================================================================================

// ---------- paths ----------
fs::path ext_root() {
    const fs::path p = fs::path(data_dir()) / "ext";
    std::error_code ec;
    for (const char* sub : {"skills", "commands", "agents", "plugins", "themes", "snippets"}) fs::create_directories(p / sub, ec);
    return p;
}
fs::path user_home_dir() {
#ifdef _WIN32
    if (const char* h = std::getenv("USERPROFILE"); h && *h) return fs::path(h);
#endif
    if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h);
    return {};
}
// Folders under the user's home that Astro reads (other tools' skills, commands, agents and rules). Read-only.
const std::vector<std::string>& user_dot_dirs() {
    static const std::vector<std::string> v = {".claude", ".codex", ".agents", ".config/opencode", ".cursor", ".gemini"};
    return v;
}
// Resolves `rel` inside `root`, refusing "..", absolute paths and symlinks that leave the root.
std::optional<fs::path> safe_join(const fs::path& root, const std::string& rel) {
    if (rel.empty() || rel.size() > 500 || rel.find('\0') != std::string::npos || rel.find('\\') != std::string::npos) return std::nullopt;
    fs::path r = fs::path(rel).lexically_normal();
    if (r.is_absolute() || r.has_root_name()) return std::nullopt;
    for (const auto& part : r) if (part == "..") return std::nullopt;
    std::error_code ec;
    const fs::path full = root / r;
    const std::string cb = fs::weakly_canonical(root, ec).generic_string(), cf = fs::weakly_canonical(full, ec).generic_string();
    if (cf != cb && cf.rfind(cb + "/", 0) != 0) return std::nullopt;
    return full;
}

// ---------- open a real folder ----------
// A linked project is a symbolic link (a junction on Windows) inside the projects folder that points at the user's
// folder, so the file tree, editor, terminal, git, search and checkpoints work on the real files unchanged.
// Closing it removes only the link (see project_is_link / remove_dir_link in workspace_code.inc).
bool make_dir_link(const fs::path& target, const fs::path& link, std::string& error) {
    std::error_code ec;
    fs::create_directory_symlink(target, link, ec);
    if (!ec) return true;
#ifdef _WIN32
    // Symbolic links need Developer Mode or admin rights on Windows; a directory junction does not.
    const auto r = run_capture({"cmd.exe", "/c", "mklink", "/J", link.string(), target.string()}, fs::path(data_dir()), {}, 20, 20000);
    if (r.code == 0 && fs::exists(link, ec)) return true;
    error = "Windows refused to link the folder: " + trim(r.out + r.err);
    return false;
#else
    error = "Could not link the folder: " + ec.message();
    return false;
#endif
}
std::string unique_project_name(std::string want) {
    std::string n;
    for (char c : want) n += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '-';
    while (!n.empty() && (n[0] == '.' || n[0] == '-')) n.erase(0, 1);
    if (n.size() > 50) n.resize(50);
    if (n.empty()) n = "folder";
    std::error_code ec;
    std::string out = n;
    for (int i = 2; fs::exists(fs::path(projects_root()) / out, ec) || fs::is_symlink(fs::symlink_status(fs::path(projects_root()) / out, ec)); ++i) out = n + "-" + std::to_string(i);
    return out;
}
void dev_browse(int fd, const Json& body) {
    std::error_code ec;
    std::string p = get_string(body, "path");
    const fs::path home = user_home_dir();
    fs::path dir = p.empty() ? (home.empty() ? fs::current_path(ec) : home) : fs::path(p);
    if (!dir.is_absolute() || !fs::is_directory(dir, ec)) { send_json(fd, 404, error_json("That folder does not exist.")); return; }
    dir = fs::weakly_canonical(dir, ec);
    const bool hidden = json_bool(body, "hidden", false);
    Json::Array dirs;
    std::vector<std::pair<std::string, bool>> found;
    for (const auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code e2;
        if (!e.is_directory(e2)) continue;
        const std::string name = e.path().filename().string();
        if (!hidden && (name.empty() || name[0] == '.' || name == "node_modules" || name == "__pycache__" || name == "$RECYCLE.BIN" || name == "System Volume Information")) continue;
        found.emplace_back(name, fs::exists(e.path() / ".git", e2));
        if (found.size() >= 2000) break;
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return lower_copy(a.first) < lower_copy(b.first); });
    for (const auto& [name, git] : found) { Json::Object o; o["name"] = Json(name); if (git) o["git"] = Json(true); dirs.emplace_back(std::move(o)); }
    Json::Object o;
    o["path"] = Json(dir.string());
    o["parent"] = Json(dir.has_parent_path() && dir.parent_path() != dir ? dir.parent_path().string() : std::string());
    o["home"] = Json(home.string());
    o["sep"] = Json(std::string(1, static_cast<char>(fs::path::preferred_separator)));
    o["dirs"] = Json(std::move(dirs));
    o["git"] = Json(fs::exists(dir / ".git", ec));
    Json::Array roots;
#ifdef _WIN32
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) if (mask & (1u << i)) roots.emplace_back(Json(std::string(1, static_cast<char>('A' + i)) + ":\\"));
#else
    roots.emplace_back(Json(std::string("/")));
#endif
    o["roots"] = Json(std::move(roots));
    send_json(fd, 200, Json(std::move(o)));
}
void dev_open_folder(int fd, const Json& body) {
    std::error_code ec;
    const std::string p = get_string(body, "path");
    fs::path dir(p);
    if (p.empty() || !dir.is_absolute() || !fs::is_directory(dir, ec)) { send_json(fd, 400, error_json("Choose an existing folder (a full path).")); return; }
    dir = fs::weakly_canonical(dir, ec);
    const std::string d = dir.generic_string();
    const auto inside = [&](const fs::path& other) {
        std::error_code e2; const std::string o = fs::weakly_canonical(other, e2).generic_string();
        return !o.empty() && (d == o || d.rfind(o + "/", 0) == 0 || o.rfind(d + "/", 0) == 0);
    };
    if (!dir.has_relative_path() || dir == user_home_dir()) { send_json(fd, 400, error_json("Pick a project folder, not the whole drive or your home folder.")); return; }
    if (inside(fs::path(data_dir()))) { send_json(fd, 400, error_json("That folder is Astro AI's own data folder.")); return; }
    // Already open? Reuse the same project.
    for (const auto& e : fs::directory_iterator(projects_root(), ec)) {
        const std::string n = e.path().filename().string();
        const std::string t = link_target_of(n, e.path());
        if (!t.empty()) { std::error_code e2; if (fs::weakly_canonical(fs::path(t), e2) == dir) { Json::Object o; o["ok"] = Json(true); o["name"] = Json(n); o["existing"] = Json(true); send_json(fd, 200, Json(std::move(o))); return; } }
    }
    std::string want = get_string(body, "name");
    if (want.empty()) want = dir.filename().string();
    const std::string name = unique_project_name(want);
    std::string error;
    if (!make_dir_link(dir, fs::path(projects_root()) / name, error)) { send_json(fd, 500, error_json(error)); return; }
    auto links = load_links(); links[name] = Json(dir.string()); save_links(links);
    Json::Object o; o["ok"] = Json(true); o["name"] = Json(name); o["path"] = Json(dir.string());
    o["git"] = Json(fs::exists(dir / ".git", ec));
    send_json(fd, 200, Json(std::move(o)));
}

// ---------- MCP client ----------
// Speaks JSON-RPC 2.0 to MCP servers: newline-delimited over a child process's stdin/stdout ("stdio"), or the
// Streamable HTTP transport (POST, JSON or SSE answers, Mcp-Session-Id). stderr goes to a log file.
struct McpConn {
    std::string key, name, scope, type, command, url;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::string>> env, headers;
    fs::path cwd;
    std::mutex mu;
    bool ready = false;
    std::string error, session, server_name, instructions;
    Json tools = Json(Json::Array{});
    int next_id = 1;
    long long used = 0;
    std::string rbuf;
#ifdef _WIN32
    HANDLE proc = nullptr, in_w = nullptr, out_r = nullptr;
#else
    pid_t pid = -1; int in_w = -1, out_r = -1;
#endif
};
std::mutex g_mcp_mu;
std::map<std::string, std::shared_ptr<McpConn>> g_mcp;

fs::path mcp_log_path(const std::string& key) {
    std::string safe; for (char c : key) safe += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    std::error_code ec; fs::create_directories(fs::path(data_dir()) / ".astro_mcp_logs", ec);
    return fs::path(data_dir()) / ".astro_mcp_logs" / (safe.substr(0, 120) + ".log");
}
std::string mcp_env_path() {
    std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
#ifndef _WIN32
    for (const char* extra : {"/usr/local/bin", "/opt/homebrew/bin", "/usr/bin", "/bin"}) if ((":" + path + ":").find(std::string(":") + extra + ":") == std::string::npos) path += std::string(path.empty() ? "" : ":") + extra;
#endif
    return path;
}
void mcp_kill(McpConn& c) {
#ifdef _WIN32
    if (c.proc) { TerminateProcess(c.proc, 1); CloseHandle(c.proc); c.proc = nullptr; }
    if (c.in_w) { CloseHandle(c.in_w); c.in_w = nullptr; }
    if (c.out_r) { CloseHandle(c.out_r); c.out_r = nullptr; }
#else
    if (c.in_w >= 0) { close(c.in_w); c.in_w = -1; }
    if (c.out_r >= 0) { close(c.out_r); c.out_r = -1; }
    if (c.pid > 0) { kill(-c.pid, SIGTERM); usleep(150000); kill(-c.pid, SIGKILL); int st = 0; waitpid(c.pid, &st, WNOHANG); c.pid = -1; }
#endif
    c.ready = false; c.rbuf.clear(); c.session.clear();
}
bool mcp_spawn(McpConn& c) {
    const fs::path log = mcp_log_path(c.key);
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE in_r = nullptr, out_w = nullptr;
    if (!CreatePipe(&in_r, &c.in_w, &sa, 0) || !CreatePipe(&c.out_r, &out_w, &sa, 0)) { c.error = "Could not create pipes."; return false; }
    SetHandleInformation(c.in_w, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(c.out_r, HANDLE_FLAG_INHERIT, 0);
    HANDLE err = CreateFileW(log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = in_r; si.hStdOutput = out_w; si.hStdError = err;
    PROCESS_INFORMATION pi{};
    // npx / uvx / node etc. are often .cmd shims on Windows, so run through cmd.exe.
    std::vector<std::string> argv = {"cmd.exe", "/d", "/s", "/c", c.command}; argv.insert(argv.end(), c.args.begin(), c.args.end());
    std::wstring cmd = winp::widen(winp::join_args(argv));
    // Environment: the user's environment plus the configured variables.
    std::wstring envblk;
    {
        LPWCH cur = GetEnvironmentStringsW();
        std::map<std::wstring, std::wstring> vars;
        for (LPWCH p = cur; p && *p; p += wcslen(p) + 1) { std::wstring s(p); auto eq = s.find(L'=', 1); if (eq != std::wstring::npos && !astro_secret_env(winp::narrow(s.substr(0, eq)))) vars[s.substr(0, eq)] = s.substr(eq + 1); }
        if (cur) FreeEnvironmentStringsW(cur);
        for (const auto& [k, v] : c.env) vars[winp::widen(k)] = winp::widen(v);
        for (const auto& [k, v] : vars) { envblk += k + L"=" + v; envblk.push_back(L'\0'); }
        envblk.push_back(L'\0');
    }
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, envblk.data(), c.cwd.wstring().c_str(), &si, &pi);
    CloseHandle(in_r); CloseHandle(out_w); if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    if (!ok) { c.error = "Could not start: " + c.command; mcp_kill(c); return false; }
    CloseHandle(pi.hThread); c.proc = pi.hProcess;
    return true;
#else
    int in_p[2], out_p[2];
    if (pipe(in_p) != 0) { c.error = "pipe failed"; return false; }
    if (pipe(out_p) != 0) { close(in_p[0]); close(in_p[1]); c.error = "pipe failed"; return false; }
    std::vector<std::string> envs;
    for (char** e = ::environ; e && *e; ++e) { const std::string kv(*e); if (!astro_secret_env(kv.substr(0, kv.find('=')))) envs.push_back(kv); }
    auto set_env = [&](const std::string& k, const std::string& v) { for (auto& s : envs) if (s.rfind(k + "=", 0) == 0) { s = k + "=" + v; return; } envs.push_back(k + "=" + v); };
    set_env("PATH", mcp_env_path());
    for (const auto& [k, v] : c.env) set_env(k, v);
    std::vector<char*> envp; for (auto& s : envs) envp.push_back(s.data()); envp.push_back(nullptr);
    std::vector<std::string> argv_s = {"/bin/sh", "-c", "exec \"$0\" \"$@\"", c.command}; argv_s.insert(argv_s.end(), c.args.begin(), c.args.end());
    std::vector<char*> argv; for (auto& s : argv_s) argv.push_back(s.data()); argv.push_back(nullptr);
    const std::string logs = log.string(), cwds = c.cwd.string();
    const pid_t pid = fork();
    if (pid < 0) { c.error = "fork failed"; return false; }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(in_p[0], 0); dup2(out_p[1], 1);
        const int lf = open(logs.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600); if (lf >= 0) dup2(lf, 2);
        for (int f = 3; f < 1024; ++f) close(f);
        if (chdir(cwds.c_str()) != 0) { /* stay where we are */ }
        execve("/bin/sh", argv.data(), envp.data());
        _exit(127);
    }
    close(in_p[0]); close(out_p[1]);
    c.pid = pid; c.in_w = in_p[1]; c.out_r = out_p[0];
    fcntl(c.out_r, F_SETFL, fcntl(c.out_r, F_GETFL) | O_NONBLOCK);
    signal(SIGPIPE, SIG_IGN);
    return true;
#endif
}
bool mcp_write(McpConn& c, const std::string& line) {
    const std::string data = line + "\n";
#ifdef _WIN32
    DWORD w = 0; return c.in_w && WriteFile(c.in_w, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
#else
    std::size_t off = 0;
    while (off < data.size()) { const ssize_t n = write(c.in_w, data.data() + off, data.size() - off); if (n <= 0) { if (errno == EINTR) continue; return false; } off += static_cast<std::size_t>(n); }
    return true;
#endif
}
// Reads one line (without the newline) within the deadline. Returns nullopt on timeout or when the server exits.
std::optional<std::string> mcp_read_line(McpConn& c, std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        if (const auto nl = c.rbuf.find('\n'); nl != std::string::npos) { std::string l = c.rbuf.substr(0, nl); c.rbuf.erase(0, nl + 1); if (!l.empty() && l.back() == '\r') l.pop_back(); return l; }
        if (std::chrono::steady_clock::now() > deadline) return std::nullopt;
        char buf[65536];
#ifdef _WIN32
        DWORD avail = 0;
        if (!PeekNamedPipe(c.out_r, nullptr, 0, nullptr, &avail, nullptr)) { c.error = "The server exited."; return std::nullopt; }
        if (avail == 0) { Sleep(15); continue; }
        DWORD got = 0;
        if (!ReadFile(c.out_r, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) { c.error = "The server exited."; return std::nullopt; }
        c.rbuf.append(buf, got);
#else
        pollfd p{c.out_r, POLLIN, 0};
        const int r = poll(&p, 1, 50);
        if (r < 0 && errno != EINTR) return std::nullopt;
        if (r <= 0) continue;
        const ssize_t got = read(c.out_r, buf, sizeof(buf));
        if (got == 0) { c.error = "The server exited."; return std::nullopt; }
        if (got < 0) { if (errno == EAGAIN || errno == EINTR) continue; return std::nullopt; }
        c.rbuf.append(buf, static_cast<std::size_t>(got));
#endif
        if (c.rbuf.size() > 64'000'000) { c.error = "The server sent too much data."; return std::nullopt; }
    }
}
std::size_t mcp_hdr_cb(char* p, std::size_t s, std::size_t n, void* ud) {
    std::string h(p, s * n);
    auto* c = static_cast<McpConn*>(ud);
    if (lower_copy(h).rfind("mcp-session-id:", 0) == 0) c->session = trim(h.substr(15));
    return s * n;
}
// Handles a request the server sends to us (stdio). Returns true if `msg` was such a request.
bool mcp_answer_server_request(McpConn& c, const Json& msg) {
    const std::string method = get_string(msg, "method");
    const Json* id = msg.get("id");
    if (method.empty() || !id) return false;
    Json::Object resp; resp["jsonrpc"] = Json("2.0"); resp["id"] = *id;
    if (method == "ping") resp["result"] = Json(Json::Object{});
    else if (method == "roots/list") {
        Json::Object root; root["uri"] = Json("file://" + c.cwd.generic_string()); root["name"] = Json(c.cwd.filename().string());
        Json::Object r; r["roots"] = Json(Json::Array{Json(std::move(root))}); resp["result"] = Json(std::move(r));
    } else { Json::Object e; e["code"] = Json(-32601); e["message"] = Json("Not supported by Astro AI: " + method); resp["error"] = Json(std::move(e)); }
    if (c.type == "stdio") mcp_write(c, dump_json(Json(std::move(resp))));
    return true;
}
// One JSON-RPC request. Returns the "result" or throws with the error message.
Json mcp_rpc(McpConn& c, const std::string& method, Json params, int timeout_s, bool notify = false) {
    Json::Object req; req["jsonrpc"] = Json("2.0"); req["method"] = Json(method);
    if (params.object_ptr()) req["params"] = std::move(params);
    const int id = c.next_id++;
    if (!notify) req["id"] = Json(id);
    const std::string line = dump_json(Json(std::move(req)));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    auto pick = [&](const Json& msg) -> std::optional<Json> {
        if (mcp_answer_server_request(c, msg)) return std::nullopt;
        const Json* mid = msg.get("id");
        if (!mid) return std::nullopt;
        const auto* d = std::get_if<double>(&mid->value);
        if (!d || static_cast<int>(*d) != id) return std::nullopt;
        if (const Json* err = msg.get("error")) throw std::runtime_error(get_string(*err, "message", "MCP error"));
        const Json* res = msg.get("result");
        return res ? *res : Json(Json::Object{});
    };
    if (c.type == "stdio") {
        if (!mcp_write(c, line)) throw std::runtime_error("The MCP server is not running.");
        if (notify) return Json();
        while (auto l = mcp_read_line(c, deadline)) {
            if (l->empty() || (*l)[0] != '{') continue;
            Json msg; try { msg = JsonParser(*l).parse(); } catch (...) { continue; }
            if (auto r = pick(msg)) return *r;
        }
        throw std::runtime_error(c.error.empty() ? "The MCP server did not answer in time." : c.error);
    }
    // Streamable HTTP
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl init failed");
    std::string resp;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Accept: application/json, text/event-stream");
    h = curl_slist_append(h, "MCP-Protocol-Version: 2025-06-18");
    if (!c.session.empty()) h = curl_slist_append(h, ("Mcp-Session-Id: " + c.session).c_str());
    for (const auto& [k, v] : c.headers) h = curl_slist_append(h, (k + ": " + v).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, c.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, line.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, mcp_hdr_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &c);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_s));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(h); curl_easy_cleanup(curl);
    if (rc != CURLE_OK) throw std::runtime_error(std::string("Could not reach the MCP server: ") + curl_easy_strerror(rc));
    if (status == 401 || status == 403) throw std::runtime_error("The MCP server needs authorization (add an Authorization header in its config).");
    if (status >= 400) throw std::runtime_error("The MCP server answered HTTP " + std::to_string(status) + ": " + resp.substr(0, 300));
    if (notify) return Json();
    // JSON body, or SSE events with "data:" lines.
    if (!resp.empty() && resp.find_first_not_of(" \r\n\t") != std::string::npos && resp[resp.find_first_not_of(" \r\n\t")] == '{') {
        Json msg = JsonParser(resp).parse();
        if (auto r = pick(msg)) return *r;
        throw std::runtime_error("The MCP server sent an unexpected answer.");
    }
    std::istringstream in(resp); std::string l, data;
    auto flush = [&]() -> std::optional<Json> { if (data.empty()) return std::nullopt; Json msg; try { msg = JsonParser(data).parse(); } catch (...) { data.clear(); return std::nullopt; } data.clear(); return pick(msg); };
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.rfind("data:", 0) == 0) { data += trim(l.substr(5)); continue; }
        if (l.empty()) if (auto r = flush()) return *r;
    }
    if (auto r = flush()) return *r;
    throw std::runtime_error("The MCP server sent no answer.");
}
void mcp_start(McpConn& c) {
    mcp_kill(c);
    c.error.clear(); c.tools = Json(Json::Array{}); c.next_id = 1;
    try {
        if (c.type == "stdio" && !mcp_spawn(c)) throw std::runtime_error(c.error);
        Json::Object caps; caps["roots"] = Json(Json::Object{{"listChanged", Json(false)}});
        Json::Object info; info["name"] = Json("Astro AI"); info["version"] = Json("3.0");
        Json::Object p; p["protocolVersion"] = Json("2025-06-18"); p["capabilities"] = Json(std::move(caps)); p["clientInfo"] = Json(std::move(info));
        const Json init = mcp_rpc(c, "initialize", Json(std::move(p)), c.type == "stdio" ? 90 : 30);   // npx may download the server first
        if (const Json* si = init.get("serverInfo")) c.server_name = get_string(*si, "name");
        c.instructions = get_string(init, "instructions").substr(0, 2000);
        mcp_rpc(c, "notifications/initialized", Json(Json::Object{}), 10, true);
        Json::Array all; std::string cursor;
        for (int page = 0; page < 20; ++page) {
            Json::Object lp; if (!cursor.empty()) lp["cursor"] = Json(cursor);
            const Json r = mcp_rpc(c, "tools/list", Json(std::move(lp)), 30);
            if (const Json* t = r.get("tools"); t && t->array_ptr()) for (const auto& x : *t->array_ptr()) all.push_back(x);
            cursor = get_string(r, "nextCursor");
            if (cursor.empty()) break;
        }
        c.tools = Json(std::move(all));
        c.ready = true;
    } catch (const std::exception& e) {
        c.error = e.what();
        if (c.type == "stdio") { if (auto log = read_small(mcp_log_path(c.key), 200000)) { const std::string t = trim(*log); if (!t.empty()) c.error += " — log: " + t.substr(t.size() > 600 ? t.size() - 600 : 0); } }
        mcp_kill(c);
    }
}

// --- configuration (Claude Code / Cursor / VS Code / OpenCode formats) ---
struct McpSpec { std::string name, scope, source, type, command, url; std::vector<std::string> args; std::vector<std::pair<std::string, std::string>> env, headers; bool disabled = false; };
std::vector<std::string> json_str_list(const Json* j) { std::vector<std::string> v; if (j && j->array_ptr()) for (const auto& x : *j->array_ptr()) if (x.string_ptr()) v.push_back(*x.string_ptr()); return v; }
std::vector<std::pair<std::string, std::string>> json_str_map(const Json* j) { std::vector<std::pair<std::string, std::string>> v; if (j && j->object_ptr()) for (const auto& [k, x] : *j->object_ptr()) if (x.string_ptr()) v.emplace_back(k, *x.string_ptr()); return v; }
void mcp_parse_config(const std::string& text, const std::string& scope, const std::string& source, std::vector<McpSpec>& out) {
    Json root; try { root = JsonParser(text).parse(); } catch (...) { return; }
    const Json* servers = root.get("mcpServers"); if (!servers) servers = root.get("servers"); if (!servers) servers = root.get("mcp");
    if (!servers || !servers->object_ptr()) return;
    for (const auto& [name, s] : *servers->object_ptr()) {
        if (!s.object_ptr() || name.empty() || name.size() > 60) continue;
        McpSpec m; m.name = name; m.scope = scope; m.source = source;
        std::string type = lower_copy(get_string(s, "type"));
        const Json* cmd = s.get("command");
        if (cmd && cmd->array_ptr()) { auto v = json_str_list(cmd); if (!v.empty()) { m.command = v[0]; m.args.assign(v.begin() + 1, v.end()); } }   // OpenCode: command is a list
        else m.command = get_string(s, "command");
        if (m.args.empty()) m.args = json_str_list(s.get("args"));
        m.env = json_str_map(s.get("env")); for (auto& e : json_str_map(s.get("environment"))) m.env.push_back(e);
        m.url = get_string(s, "url"); if (m.url.empty()) m.url = get_string(s, "serverUrl");
        m.headers = json_str_map(s.get("headers"));
        m.disabled = json_bool(s, "disabled", false) || (s.get("enabled") && !json_bool(s, "enabled", true));
        m.type = (!m.url.empty() || type == "http" || type == "remote" || type == "sse" || type == "streamable-http") ? "http" : "stdio";
        if (m.type == "stdio" && m.command.empty()) continue;
        if (m.type == "http" && !(m.url.rfind("https://", 0) == 0 || m.url.rfind("http://", 0) == 0)) continue;
        out.push_back(std::move(m));
    }
}
std::vector<McpSpec> mcp_specs(const std::string& project, const fs::path& base) {
    std::vector<McpSpec> out;
    if (auto t = read_small(ext_root() / "mcp.json", 1'000'000)) mcp_parse_config(*t, "global", "Astro (global)", out);
    // Plugins can bundle MCP servers (.mcp.json in the plugin folder).
    std::error_code ec;
    const auto enabled = [&]() { Json::Object o; if (auto t = read_small(ext_root() / "plugins.json", 200000)) { try { Json j = JsonParser(*t).parse(); if (auto* p = j.object_ptr()) o = *p; } catch (...) {} } return o; }();
    for (const auto& e : fs::directory_iterator(ext_root() / "plugins", ec)) {
        const std::string pn = e.path().filename().string();
        if (auto it = enabled.find(pn); it != enabled.end() && std::holds_alternative<bool>(it->second.value) && !std::get<bool>(it->second.value)) continue;
        if (auto t = read_small(e.path() / ".mcp.json", 1'000'000)) mcp_parse_config(*t, "global", "plugin " + pn, out);
    }
    if (!project.empty()) {
        for (const auto& [rel, label] : std::vector<std::pair<std::string, std::string>>{{".mcp.json", ".mcp.json"}, {".astro/mcp.json", ".astro/mcp.json"}, {".cursor/mcp.json", ".cursor/mcp.json"}, {".vscode/mcp.json", ".vscode/mcp.json"}, {"opencode.json", "opencode.json"}})
            if (auto t = read_small(base / rel, 1'000'000)) mcp_parse_config(*t, "project", label, out);
    }
    // Later entries with the same name win (project over global).
    std::vector<McpSpec> uniq;
    for (auto it = out.rbegin(); it != out.rend(); ++it) if (std::none_of(uniq.begin(), uniq.end(), [&](const McpSpec& u) { return u.name == it->name; })) uniq.push_back(*it);
    std::reverse(uniq.begin(), uniq.end());
    return uniq;
}
std::string mcp_fingerprint(const McpSpec& m) {
    std::string s = m.type + "|" + m.command + "|" + m.url;
    for (const auto& a : m.args) s += "|" + a;
    return s;
}
// Project MCP servers run commands from the opened folder, so they start only after the user trusts them (like VS Code's workspace trust).
fs::path mcp_trust_file() { return fs::path(data_dir()) / ".astro_mcp_trust.json"; }
// Trust is remembered per real folder (not per project name), so another folder opened under the same name is not trusted.
std::string mcp_trust_key(const fs::path& base, const std::string& name) { std::error_code ec; return fs::weakly_canonical(base, ec).generic_string() + "|" + name; }
bool mcp_trusted(const fs::path& base, const McpSpec& m) {
    if (m.scope != "project") return true;
    if (auto t = read_file_text(mcp_trust_file().string())) { try { Json j = JsonParser(*t).parse(); return get_string(j, mcp_trust_key(base, m.name)) == mcp_fingerprint(m); } catch (...) {} }
    return false;
}
std::shared_ptr<McpConn> mcp_conn(const std::string& project, const fs::path& base, const McpSpec& m) {
    const std::string key = (m.scope == "project" ? project : std::string("*")) + "/" + m.name;
    std::lock_guard lk(g_mcp_mu);
    auto& c = g_mcp[key];
    const bool changed = c && (c->command != m.command || c->url != m.url || c->args != m.args || (m.scope == "project" && c->cwd != base));
    if (changed) { std::lock_guard l2(c->mu); mcp_kill(*c); c.reset(); }
    if (!c) {
        c = std::make_shared<McpConn>();
        c->key = key; c->name = m.name; c->scope = m.scope; c->type = m.type; c->command = m.command; c->args = m.args; c->url = m.url;
        c->env = m.env; c->headers = m.headers;
        c->cwd = m.scope == "project" ? base : (user_home_dir().empty() ? fs::path(data_dir()) : user_home_dir());
    }
    return c;
}
Json mcp_conn_json(const McpSpec& m, McpConn* c, bool trusted) {
    Json::Object o;
    o["name"] = Json(m.name); o["scope"] = Json(m.scope); o["source"] = Json(m.source); o["type"] = Json(m.type);
    o["command"] = Json(m.type == "stdio" ? m.command + (m.args.empty() ? "" : " " + [&] { std::string s; for (const auto& a : m.args) s += (s.empty() ? "" : " ") + a; return s; }()) : m.url);
    o["disabled"] = Json(m.disabled); o["trusted"] = Json(trusted);
    o["status"] = Json(m.disabled ? "disabled" : !trusted ? "untrusted" : c && c->ready ? "running" : c && !c->error.empty() ? "error" : "stopped");
    if (c) { o["error"] = Json(c->error); o["tools"] = c->tools; o["server"] = Json(c->server_name); if (!c->instructions.empty()) o["instructions"] = Json(c->instructions); }
    else o["tools"] = Json(Json::Array{});
    return Json(std::move(o));
}
void dev_mcp(int fd, const std::string& action, const Json& body) {
    std::error_code ec;
    const std::string project = get_string(body, "project");
    fs::path base;
    if (!project.empty()) { if (!valid_project_name(project)) { send_json(fd, 400, error_json("Invalid project.")); return; } base = fs::path(projects_root()) / project; if (!fs::is_directory(base, ec)) { send_json(fd, 404, error_json("Project not found.")); return; } }
    if (action == "config_get") {
        Json::Object o; o["text"] = Json(read_small(ext_root() / "mcp.json", 1'000'000).value_or("{\n  \"mcpServers\": {}\n}\n")); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "config_set") {
        const std::string text = get_string(body, "text");
        try { Json j = JsonParser(text).parse(); if (!j.object_ptr()) throw std::runtime_error("not an object"); } catch (const std::exception& e) { send_json(fd, 400, error_json(std::string("That is not valid JSON: ") + e.what())); return; }
        if (!write_file_private((ext_root() / "mcp.json").string(), text)) { send_json(fd, 500, error_json("Could not save.")); return; }
        Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return;
    }
    const auto specs = mcp_specs(project, base);
    if (action == "list") {
        const bool start = json_bool(body, "start", false);
        Json::Array arr;
        for (const auto& m : specs) {
            const bool trusted = mcp_trusted(base, m);
            std::shared_ptr<McpConn> c;
            if (!m.disabled && trusted) {
                c = mcp_conn(project, base, m);
                std::unique_lock lk(c->mu, std::defer_lock);
                if (start && !c->ready && c->error.empty() && lk.try_lock()) mcp_start(*c);
            }
            arr.push_back(mcp_conn_json(m, c.get(), trusted));
        }
        Json::Object o; o["servers"] = Json(std::move(arr)); send_json(fd, 200, Json(std::move(o))); return;
    }
    const std::string name = get_string(body, "server");
    const auto it = std::find_if(specs.begin(), specs.end(), [&](const McpSpec& m) { return m.name == name; });
    if (it == specs.end()) { send_json(fd, 404, error_json("No MCP server named \"" + name + "\".")); return; }
    if (action == "trust") {
        Json::Object t; if (auto txt = read_file_text(mcp_trust_file().string())) { try { Json j = JsonParser(*txt).parse(); if (auto* o = j.object_ptr()) t = *o; } catch (...) {} }
        if (json_bool(body, "trust", true)) t[mcp_trust_key(base, it->name)] = Json(mcp_fingerprint(*it)); else t.erase(mcp_trust_key(base, it->name));
        write_file_private(mcp_trust_file().string(), dump_json(Json(std::move(t))));
        Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (it->disabled) { send_json(fd, 400, error_json("That MCP server is disabled in its config.")); return; }
    if (!mcp_trusted(base, *it)) { send_json(fd, 403, error_json("This project's MCP server \"" + name + "\" is not trusted yet. Trust it in Extensions → MCP first.")); return; }
    auto c = mcp_conn(project, base, *it);
    std::lock_guard lk(c->mu);
    if (action == "stop") { mcp_kill(*c); c->error.clear(); send_json(fd, 200, mcp_conn_json(*it, c.get(), true)); return; }
    if (action == "restart") { mcp_start(*c); send_json(fd, 200, mcp_conn_json(*it, c.get(), true)); return; }
    if (action == "call") {
        if (!c->ready) mcp_start(*c);
        if (!c->ready) { send_json(fd, 502, error_json("MCP server \"" + name + "\" is not running: " + c->error)); return; }
        Json::Object p; p["name"] = Json(get_string(body, "tool"));
        const Json* args = body.get("arguments");
        p["arguments"] = args && args->object_ptr() ? *args : Json(Json::Object{});
        try {
            Json r = mcp_rpc(*c, "tools/call", Json(std::move(p)), std::clamp(json_int(body, "timeout", 120), 5, 900));
            c->used++;
            send_json(fd, 200, r);
        } catch (const std::exception& e) {
            if (c->type == "stdio" && !c->error.empty()) mcp_kill(*c);
            send_json(fd, 502, error_json(std::string("MCP call failed: ") + e.what()));
        }
        return;
    }
    send_json(fd, 404, error_json("Unknown MCP action."));
}

// ---------- scan: skills, commands, agents, rules, hooks, plugins, themes, snippets ----------
void dev_scan(int fd, const Json& body) {
    std::error_code ec;
    const std::string project = get_string(body, "project");
    fs::path base;
    if (!project.empty() && valid_project_name(project)) base = fs::path(projects_root()) / project;
    Json::Array files;
    std::size_t total = 0;
    const auto add = [&](const std::string& scope, const std::string& kind, const fs::path& abs, const std::string& rel, const std::string& owner) {
        if (files.size() >= 600 || total > 6'000'000) return;
        auto t = read_small(abs, 300'000);
        if (!t || is_binary_blob(*t)) return;
        total += t->size();
        Json::Object o; o["scope"] = Json(scope); o["kind"] = Json(kind); o["path"] = Json(rel); o["content"] = Json(utf8_clean(*t));
        if (!owner.empty()) o["owner"] = Json(owner);
        files.emplace_back(std::move(o));
    };
    // md files directly in a folder (commands / agents), or nested ("namespace/cmd.md" for commands)
    const auto md_in = [&](const std::string& scope, const std::string& kind, const fs::path& root, const std::string& sub, const std::string& owner, bool nested) {
        const fs::path dir = root / sub;
        if (!fs::is_directory(dir, ec)) return;
        int n = 0;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec && n < 200; it.increment(ec)) {
            if (it->is_directory(ec)) { if (!nested) it.disable_recursion_pending(); continue; }
            const std::string ext = lower_copy(it->path().extension().string());
            if (ext != ".md" && ext != ".mdc" && ext != ".toml") continue;
            add(scope, kind, it->path(), fs::relative(it->path(), root, ec).generic_string(), owner); ++n;
        }
    };
    const auto skills_in = [&](const std::string& scope, const fs::path& root, const std::string& sub, const std::string& owner) {
        const fs::path dir = root / sub;
        if (!fs::is_directory(dir, ec)) return;
        int n = 0;
        for (const auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            if (n++ > 200) break;
            if (!e.is_directory(ec)) continue;
            const fs::path sk = fs::exists(e.path() / "SKILL.md", ec) ? e.path() / "SKILL.md" : e.path() / "skill.md";
            if (fs::exists(sk, ec)) {
                add(scope, "skill", sk, fs::relative(sk, root, ec).generic_string(), owner);
                // list the skill's other files so the agent can open them on demand
                Json::Array extra; int k = 0;
                for (fs::recursive_directory_iterator it(e.path(), fs::directory_options::skip_permission_denied, ec), end; it != end && !ec && k < 60; it.increment(ec))
                    if (it->is_regular_file(ec) && it->path() != sk) { extra.emplace_back(Json(fs::relative(it->path(), root, ec).generic_string())); ++k; }
                if (!files.empty()) if (auto* o = std::get_if<Json::Object>(&files.back().value)) (*o)["files"] = Json(std::move(extra));
            }
        }
    };
    const fs::path ext = ext_root(), home = user_home_dir();
    // Project
    if (!base.empty() && fs::is_directory(base, ec)) {
        for (const char* r : {"AGENTS.md", "CLAUDE.md", "GEMINI.md", ".cursorrules", ".windsurfrules", ".clinerules", ".github/copilot-instructions.md", "AGENT.md", ".astro/rules.md"})
            if (fs::is_regular_file(base / r, ec)) add("project", "rule", base / r, r, "");
        md_in("project", "rule", base, ".cursor/rules", "", true);
        md_in("project", "rule", base, ".astro/rules", "", true);
        for (const char* s : {".astro/skills", ".claude/skills", ".agents/skills", ".codex/skills", ".opencode/skill", ".opencode/skills", ".cursor/skills"}) skills_in("project", base, s, "");
        for (const char* s : {".astro/commands", ".claude/commands", ".opencode/command", ".opencode/commands", ".cursor/commands", ".codex/prompts"}) md_in("project", "command", base, s, "", true);
        for (const char* s : {".astro/agents", ".claude/agents", ".opencode/agent", ".opencode/agents"}) md_in("project", "agent", base, s, "", false);
        for (const char* h : {".astro/hooks.json", ".claude/settings.json", ".claude/settings.local.json"}) if (fs::is_regular_file(base / h, ec)) add("project", "hooks", base / h, h, "");
    }
    // Astro global (Extensions)
    if (fs::is_regular_file(ext / "AGENTS.md", ec)) add("global", "rule", ext / "AGENTS.md", "AGENTS.md", "");
    skills_in("global", ext, "skills", "");
    md_in("global", "command", ext, "commands", "", true);
    md_in("global", "agent", ext, "agents", "", false);
    if (fs::is_regular_file(ext / "hooks.json", ec)) add("global", "hooks", ext / "hooks.json", "hooks.json", "");
    if (fs::is_regular_file(ext / "plugins.json", ec)) add("global", "plugin-state", ext / "plugins.json", "plugins.json", "");
    if (fs::is_regular_file(ext / "settings.json", ec)) add("global", "settings", ext / "settings.json", "settings.json", "");
    for (const auto& e : fs::directory_iterator(ext / "themes", ec)) if (e.path().extension() == ".json") add("global", "theme", e.path(), fs::relative(e.path(), ext, ec).generic_string(), "");
    for (const auto& e : fs::directory_iterator(ext / "snippets", ec)) if (e.path().extension() == ".json") add("global", "snippets", e.path(), fs::relative(e.path(), ext, ec).generic_string(), "");
    // Plugins: ext/plugins/<name>/ with plugin.json (Astro) or .claude-plugin/plugin.json (Claude Code plugins)
    for (const auto& e : fs::directory_iterator(ext / "plugins", ec)) {
        if (!e.is_directory(ec)) continue;
        const std::string pn = e.path().filename().string();
        const std::string pr = "plugins/" + pn;
        for (const char* m : {"plugin.json", ".claude-plugin/plugin.json", "package.json"}) if (fs::is_regular_file(e.path() / m, ec)) { add("global", "plugin", e.path() / m, pr + "/" + m, pn); break; }
        if (fs::is_regular_file(e.path() / "main.js", ec)) add("global", "plugin-script", e.path() / "main.js", pr + "/main.js", pn);
        if (fs::is_regular_file(e.path() / "style.css", ec)) add("global", "plugin-style", e.path() / "style.css", pr + "/style.css", pn);
        skills_in("global", ext, pr + "/skills", pn);
        md_in("global", "command", ext, pr + "/commands", pn, true);
        md_in("global", "agent", ext, pr + "/agents", pn, false);
        if (fs::is_regular_file(e.path() / "hooks" / "hooks.json", ec)) add("global", "hooks", e.path() / "hooks" / "hooks.json", pr + "/hooks/hooks.json", pn);
        if (fs::is_regular_file(e.path() / "hooks.json", ec)) add("global", "hooks", e.path() / "hooks.json", pr + "/hooks.json", pn);
        for (const auto& t : fs::directory_iterator(e.path() / "themes", ec)) if (t.path().extension() == ".json") add("global", "theme", t.path(), fs::relative(t.path(), ext, ec).generic_string(), pn);
        for (const auto& t : fs::directory_iterator(e.path() / "snippets", ec)) if (t.path().extension() == ".json") add("global", "snippets", t.path(), fs::relative(t.path(), ext, ec).generic_string(), pn);
    }
    // Other tools already installed for this user (read-only): Claude Code, Codex, OpenCode, Cursor, Gemini CLI
    if (!home.empty() && !json_bool(body, "no_user", false)) {
        for (const char* r : {".claude/CLAUDE.md", ".codex/AGENTS.md", ".config/opencode/AGENTS.md", ".gemini/GEMINI.md"}) if (fs::is_regular_file(home / r, ec)) add("user", "rule", home / r, r, "");
        for (const char* s : {".claude/skills", ".codex/skills", ".agents/skills", ".config/opencode/skill", ".config/opencode/skills", ".cursor/skills"}) skills_in("user", home, s, "");
        for (const char* s : {".claude/commands", ".codex/prompts", ".config/opencode/command", ".config/opencode/commands", ".cursor/commands"}) md_in("user", "command", home, s, "", true);
        for (const char* s : {".claude/agents", ".config/opencode/agent", ".config/opencode/agents"}) md_in("user", "agent", home, s, "", false);
    }
    Json::Object o; o["files"] = Json(std::move(files));
    o["ext_root"] = Json(ext.string());
    // External coding agents the Astro agent can delegate to (DeepSeek Harness-style "run other agents").
    Json::Array clis;
    for (const auto& [bin, label] : std::vector<std::pair<std::string, std::string>>{{"claude", "Claude Code"}, {"codex", "Codex CLI"}, {"opencode", "OpenCode"}, {"gemini", "Gemini CLI"}, {"dsh", "DeepSeek Harness"}, {"cursor-agent", "Cursor CLI"}, {"qwen", "Qwen Code"}, {"aider", "Aider"}})
        if (have_tool(bin)) { Json::Object c; c["bin"] = Json(bin); c["name"] = Json(label); clis.emplace_back(std::move(c)); }
    o["agents_cli"] = Json(std::move(clis));
    send_json(fd, 200, Json(std::move(o)));
}

// ---------- extension store: read / write / delete under ext/ (and read-only user tool folders) ----------
void dev_ext(int fd, const std::string& action, const Json& body) {
    std::error_code ec;
    const std::string scope = get_string(body, "scope", "global"), rel = get_string(body, "path");
    fs::path root;
    if (scope == "global") root = ext_root();
    else if (scope == "user") {
        root = user_home_dir();
        if (root.empty() || action != "read" || std::none_of(user_dot_dirs().begin(), user_dot_dirs().end(), [&](const std::string& d) { return rel.rfind(d + "/", 0) == 0; })) { send_json(fd, 403, error_json("Only Claude Code / Codex / OpenCode / Cursor / Gemini folders can be read.")); return; }
    } else if (scope == "project") {
        const std::string project = get_string(body, "project");
        if (!valid_project_name(project)) { send_json(fd, 400, error_json("Invalid project.")); return; }
        root = fs::path(projects_root()) / project;
    } else { send_json(fd, 400, error_json("Unknown scope.")); return; }
    if (action == "list") {
        const auto dir = rel.empty() ? std::optional<fs::path>(root) : safe_join(root, rel);
        if (!dir) { send_json(fd, 400, error_json("Bad path.")); return; }
        Json::Array arr;
        for (const auto& e : fs::directory_iterator(*dir, ec)) { Json::Object o; o["name"] = Json(e.path().filename().string()); o["dir"] = Json(e.is_directory(ec)); arr.emplace_back(std::move(o)); }
        Json::Object o; o["entries"] = Json(std::move(arr)); send_json(fd, 200, Json(std::move(o))); return;
    }
    const auto p = safe_join(root, rel);
    if (!p) { send_json(fd, 400, error_json("Bad path.")); return; }
    if (action == "read") {
        auto t = read_small(*p, 2'000'000);
        if (!t) { send_json(fd, 404, error_json("Not found.")); return; }
        Json::Object o; o["content"] = Json(is_binary_blob(*t) ? std::string() : utf8_clean(*t)); o["binary"] = Json(is_binary_blob(*t)); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (scope == "user") { send_json(fd, 403, error_json("Read-only.")); return; }
    if (action == "write") {
        std::string content = get_string(body, "content");
        if (json_bool(body, "base64", false)) content = base64_decode(content);
        if (content.size() > 20'000'000) { send_json(fd, 413, error_json("Too large.")); return; }
        fs::create_directories(p->parent_path(), ec);
        std::ofstream out(*p, std::ios::binary | std::ios::trunc);
        out << content;
        if (!out) { send_json(fd, 500, error_json("Could not write.")); return; }
        Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "delete") {
        if (*p == root) { send_json(fd, 400, error_json("Bad path.")); return; }
        fs::remove_all(*p, ec);
        Json::Object o; o["ok"] = Json(!ec); send_json(fd, 200, Json(std::move(o))); return;
    }
    send_json(fd, 404, error_json("Unknown action."));
}

// ---------- downloads: Open VSX (themes, snippets) and GitHub (plugin zips) only ----------
bool fetch_host_ok(const std::string& url) {
    static const std::regex re(R"(^https://([A-Za-z0-9.-]+)(/|$))");
    std::smatch m;
    if (!std::regex_search(url, m, re)) return false;
    const std::string h = lower_copy(m[1].str());
    for (const char* ok : {"open-vsx.org", "openvsxorg.blob.core.windows.net", "github.com", "codeload.github.com", "api.github.com", "raw.githubusercontent.com", "objects.githubusercontent.com", "registry.npmjs.org", "registry.modelcontextprotocol.io"})
        if (h == ok || (h.size() > std::strlen(ok) && h.ends_with(std::string(".") + ok))) return true;
    return false;
}
void dev_fetch(int fd, const Json& body) {
    const std::string url = get_string(body, "url");
    if (!fetch_host_ok(url)) { send_json(fd, 400, error_json("Downloads are limited to Open VSX, GitHub, npm and the MCP registry.")); return; }
    CURL* curl = curl_easy_init();
    if (!curl) { send_json(fd, 500, error_json("curl init failed")); return; }
    std::string resp;
    struct curl_slist* h = curl_slist_append(nullptr, "User-Agent: AstroAI/3.0 (+https://esoexe.github.io/astro-ai/)");
    h = curl_slist_append(h, "Accept: application/json, application/octet-stream, */*");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 6L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, 40L * 1024 * 1024);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    char* eff = nullptr; curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
    const std::string final_url = eff ? eff : url;
    curl_slist_free_all(h); curl_easy_cleanup(curl);
    if (rc != CURLE_OK) { send_json(fd, 502, error_json(std::string("Download failed: ") + curl_easy_strerror(rc))); return; }
    Json::Object o; o["status"] = Json(static_cast<double>(status)); o["url"] = Json(final_url);
    if (json_bool(body, "binary", false)) o["base64"] = Json(b64enc(resp)); else o["text"] = Json(utf8_clean(resp));
    send_json(fd, 200, Json(std::move(o)));
}

// ---------- diagnostics: real compilers and linters, parsed into file:line:col problems ----------
void dev_diagnostics(int fd, const Json& body) {
    std::error_code ec;
    const std::string project = get_string(body, "project");
    if (!valid_project_name(project)) { send_json(fd, 400, error_json("Invalid project.")); return; }
    const fs::path base = fs::path(projects_root()) / project;
    if (!fs::is_directory(base, ec)) { send_json(fd, 404, error_json("Project not found.")); return; }
    std::set<std::string> only;
    if (const Json* ps = body.get("paths"); ps && ps->array_ptr()) for (const auto& p : *ps->array_ptr()) if (p.string_ptr()) only.insert(*p.string_ptr());
    std::map<std::string, std::vector<std::string>> by_ext;
    walk_project(base, [&](const std::string& rel, const fs::path&) {
        if (!only.empty() && !only.count(rel)) return;
        const std::string ext = lower_copy(fs::path(rel).extension().string());
        auto& v = by_ext[ext]; if (v.size() < 80) v.push_back(rel);
    }, 8000);
    Json::Array probs; Json::Array ran;
    const auto add = [&](const std::string& path, int line, int col, const std::string& sev, std::string msg, const std::string& src) {
        if (probs.size() >= 500) return;
        Json::Object o; o["path"] = Json(path); o["line"] = Json(std::max(1, line)); o["col"] = Json(std::max(1, col));
        o["severity"] = Json(sev); o["message"] = Json(utf8_clean(trim(msg)).substr(0, 600)); o["source"] = Json(src);
        probs.emplace_back(std::move(o));
    };
    // file:line:col: severity: message   (gcc, clang, tsc --pretty false is different, go vet, php -l ...)
    static const std::regex gcc(R"(^(.+?):(\d+):(\d+):\s*(fatal error|error|warning|note):\s*(.*)$)");
    static const std::regex tsc(R"(^(.+?)\((\d+),(\d+)\):\s*(error|warning)\s+TS\d+:\s*(.*)$)");
    static const std::regex simple(R"(^(?:\./)?(.+?):(\d+)(?::(\d+))?:\s*(.*)$)");
    const auto parse_lines = [&](const std::string& out, const std::string& src) {
        std::istringstream in(out); std::string l; std::smatch m;
        while (std::getline(in, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (std::regex_match(l, m, gcc)) { if (m[4] != "note") add(m[1], std::stoi(m[2]), std::stoi(m[3]), m[4] == "warning" ? "warning" : "error", m[5], src); }
            else if (std::regex_match(l, m, tsc)) add(m[1], std::stoi(m[2]), std::stoi(m[3]), m[4], m[5], src);
        }
    };
    const auto has = [&](const char* e) { return by_ext.count(e) && !by_ext[e].empty(); };
    const auto rel_of = [&](std::string p) { const std::string b = base.generic_string() + "/"; if (p.rfind(b, 0) == 0) p = p.substr(b.size()); return p; };
    // JavaScript: node --check
    if (have_tool("node")) for (const char* e : {".js", ".mjs", ".cjs"}) if (has(e)) {
        ran.emplace_back(Json("node --check"));
        int n = 0;
        for (const auto& f : by_ext[e]) {
            if (n++ >= 60) break;
            const auto r = run_capture({"node", "--check", f}, base, {}, 15, 100000);
            if (r.code == 0) continue;
            std::istringstream in(r.err + r.out); std::string l; int line = 1; std::string msg; bool got_line = false;
            static const std::regex at(R"(^(.*):(\d+)$)");
            std::smatch m;
            while (std::getline(in, l)) {
                if (!got_line && std::regex_match(l, m, at) && m[1].str().ends_with(fs::path(f).filename().string())) { line = std::stoi(m[2]); got_line = true; }
                if (msg.empty() && l.find("Error:") != std::string::npos) msg = l;
            }
            add(f, line, 1, "error", msg.empty() ? trim(r.err).substr(0, 300) : msg, "node");
        }
    }
    // TypeScript: the project's own tsc
    if ((has(".ts") || has(".tsx")) && fs::exists(base / "node_modules/.bin/tsc", ec)) {
        ran.emplace_back(Json("tsc --noEmit"));
        const auto r = run_capture({"bash", "-lc", "node_modules/.bin/tsc --noEmit --pretty false" + std::string(fs::exists(base / "tsconfig.json", ec) ? "" : " $(git ls-files '*.ts' '*.tsx' 2>/dev/null || ls *.ts 2>/dev/null)")}, base, {}, 120, 2'000'000);
        parse_lines(r.out + "\n" + r.err, "tsc");
    }
    // Python: compile every file in one interpreter run
    if (has(".py") && (have_tool("python3") || have_tool("python"))) {
        ran.emplace_back(Json("python compile"));
        std::string list; for (const auto& f : by_ext[".py"]) list += f + "\n";
        const std::string script = "import sys\nfor f in open(sys.argv[1],encoding='utf-8').read().split('\\n'):\n  f=f.strip()\n  if not f: continue\n  try:\n    compile(open(f,encoding='utf-8',errors='replace').read(),f,'exec')\n  except SyntaxError as e:\n    print(f'{f}:{e.lineno or 1}:{e.offset or 1}: error: {e.msg}')\n  except Exception as e:\n    print(f'{f}:1:1: error: {e}')\n";
        const fs::path tmp = fs::path(data_dir()) / ".astro_pycheck.txt";
        { std::ofstream(tmp) << list; }
        const auto r = run_capture({have_tool("python3") ? "python3" : "python", "-c", script, tmp.string()}, base, {}, 60, 1'000'000);
        parse_lines(r.out, "python");
        fs::remove(tmp, ec);
    }
    // C / C++: syntax check with the compiler
    for (const auto& [e, cc, std] : std::vector<std::tuple<std::string, std::string, std::string>>{{".c", "gcc", "-std=c17"}, {".cpp", "g++", "-std=c++20"}, {".cc", "g++", "-std=c++20"}, {".cxx", "g++", "-std=c++20"}}) {
        if (!has(e.c_str()) || !have_tool(cc)) continue;
        ran.emplace_back(Json(cc + " -fsyntax-only"));
        int n = 0;
        for (const auto& f : by_ext[e]) { if (n++ >= 30) break; const auto r = run_capture({cc, std, "-fsyntax-only", "-Wall", "-fdiagnostics-plain-output", "-fno-diagnostics-color", f}, base, {}, 30, 500000); parse_lines(r.err + "\n" + r.out, cc); }
    }
    // Shell, PHP, Go, Rust, JSON
    if (has(".sh") && have_tool("bash")) {
        ran.emplace_back(Json("bash -n"));
        static const std::regex bashre(R"(^(.+?): line (\d+): (.*)$)"); std::smatch m;
        for (const auto& f : by_ext[".sh"]) { const auto r = run_capture({"bash", "-n", f}, base, {}, 10, 100000); std::istringstream in(r.err); std::string l; while (std::getline(in, l)) if (std::regex_match(l, m, bashre)) add(f, std::stoi(m[2]), 1, "error", m[3], "bash"); }
    }
    if (has(".php") && have_tool("php")) {
        ran.emplace_back(Json("php -l"));
        static const std::regex phpre(R"(^(?:PHP )?(Parse|Fatal) error:\s*(.*) in (.+) on line (\d+))"); std::smatch m;
        for (const auto& f : by_ext[".php"]) { const auto r = run_capture({"php", "-l", f}, base, {}, 15, 100000); std::istringstream in(r.out + r.err); std::string l; while (std::getline(in, l)) if (std::regex_search(l, m, phpre)) add(f, std::stoi(m[4]), 1, "error", m[2], "php"); }
    }
    if (has(".go") && have_tool("go") && fs::exists(base / "go.mod", ec)) {
        ran.emplace_back(Json("go vet"));
        const auto r = run_capture({"go", "vet", "./..."}, base, {}, 120, 1'000'000);
        std::istringstream in(r.err); std::string l; std::smatch m;
        while (std::getline(in, l)) if (std::regex_match(l, m, simple) && m[2].matched) add(rel_of(m[1]), std::stoi(m[2]), m[3].matched ? std::stoi(m[3]) : 1, "error", m[4], "go vet");
    }
    if (has(".rs") && have_tool("cargo") && fs::exists(base / "Cargo.toml", ec)) {
        ran.emplace_back(Json("cargo check"));
        const auto r = run_capture({"cargo", "check", "--quiet", "--message-format", "short"}, base, {}, 300, 2'000'000);
        parse_lines(r.err + "\n" + r.out, "cargo");
    }
    if (has(".json")) {
        ran.emplace_back(Json("json"));
        for (const auto& f : by_ext[".json"]) {
            if (f.find("tsconfig") != std::string::npos || f.find(".vscode/") != std::string::npos) continue;   // JSON with comments
            if (auto t = read_small(base / f, 2'000'000)) { try { (void)JsonParser(*t).parse(); } catch (const std::exception& e) { add(f, 1, 1, "error", std::string("Invalid JSON: ") + e.what(), "json"); } }
        }
    }
    Json::Object o; o["problems"] = Json(std::move(probs)); o["checkers"] = Json(std::move(ran));
    send_json(fd, 200, Json(std::move(o)));
}

#include "astro_library.inc"

bool handle_dev_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path.rfind("/api/dev/", 0) != 0) return false;
    if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
    const auto body_opt = parse_request_json(request, fd);
    if (!body_opt) return true;
    const Json& body = *body_opt;
    const std::string action = path.substr(9);
    try {
        if (action == "fs/browse") { dev_browse(fd, body); return true; }
        if (action == "fs/open") { dev_open_folder(fd, body); return true; }
        if (action.rfind("mcp/", 0) == 0) { dev_mcp(fd, action.substr(4), body); return true; }
        if (action.rfind("library/", 0) == 0) { dev_library(fd, action.substr(8), body); return true; }
        if (action == "scan") { dev_scan(fd, body); return true; }
        if (action.rfind("ext/", 0) == 0) { dev_ext(fd, action.substr(4), body); return true; }
        if (action == "fetch") { dev_fetch(fd, body); return true; }
        if (action == "diagnostics") { dev_diagnostics(fd, body); return true; }
    } catch (const std::exception& e) { send_json(fd, 500, error_json(e.what())); return true; }
    send_json(fd, 404, error_json("Not found."));
    return true;
}

// Stops a project's MCP servers (the project was closed, deleted or renamed).
void mcp_forget_project(const std::string& project) {
    std::lock_guard lk(g_mcp_mu);
    for (auto it = g_mcp.begin(); it != g_mcp.end();) {
        if (it->first.rfind(project + "/", 0) == 0) { if (it->second) { std::lock_guard l2(it->second->mu); mcp_kill(*it->second); } it = g_mcp.erase(it); } else ++it;
    }
}

// Stops every MCP server (called at shutdown).
void mcp_shutdown_all() {
    std::lock_guard lk(g_mcp_mu);
    for (auto& [k, c] : g_mcp) if (c) { std::lock_guard l2(c->mu); mcp_kill(*c); }
    g_mcp.clear();
}
