// =============================================================================================
// Astro AI Workspace IDE features (included after workspace_code.inc, inside the anonymous namespace):
//   * search across project files                      POST /api/ide/search
//   * download a project as a ZIP                      POST /api/ide/zip
//   * checkpoints (version history) per project        POST /api/ide/history/{list,create,restore,delete,changes,read}
//   * interactive terminals (real PTY, bash)           POST /api/ide/term/{open,stream,input,resize,close}
//   * long-running app servers (Node, Flask, ...)      POST /api/ide/proc/{start,stop,status,logs}
//   * live preview of those servers through a proxy    ANY  /app/<preview-key>/<project>/<path>
// Every /api/ide call needs login + the x-astro-client header (checked by the caller / below).
// Terminals and servers run as the server user inside the project folder with a clean environment
// (no API keys). HOME is a shared folder outside the projects so npm / pip caches and `pip --user`
// packages do not clutter projects.
// =============================================================================================

std::string b64enc(std::string_view in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) { const unsigned v = static_cast<unsigned char>(in[i]) << 16; out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += "=="; }
    else if (i + 2 == in.size()) { const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8); out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '='; }
    return out;
}

std::string ide_home() {
    static const std::string home = [] {
        std::error_code ec;
        fs::path p = fs::absolute(fs::path(data_dir()) / ".astro_home", ec);
        fs::create_directories(p, ec);
        return p.string();
    }();
    return home;
}

// Environment for terminals and app servers. Kept small on purpose: no provider keys, no Astro secrets.
std::vector<std::string> ide_env(const fs::path& dir, const std::vector<std::string>& extra) {
    const std::string home = ide_home();
#ifdef _WIN32
    std::vector<std::string> more = {"PIP_DISABLE_PIP_VERSION_CHECK=1"};
    more.insert(more.end(), extra.begin(), extra.end());
    return win_child_env(dir, home, more);
#endif
    std::vector<std::string> env = {
        "PATH=" + dir.string() + "/node_modules/.bin:" + (std::getenv("ASTRO_TOOL_BIN") && *std::getenv("ASTRO_TOOL_BIN") ? std::string(std::getenv("ASTRO_TOOL_BIN")) + ":" : std::string()) + home + "/.local/bin:/usr/local/bin:/usr/bin:/bin",
        "HOME=" + home, "LANG=C.UTF-8", "LC_ALL=C.UTF-8", "PYTHONUNBUFFERED=1", "PYTHONDONTWRITEBYTECODE=1",
        "PIP_USER=1", "PIP_DISABLE_PIP_VERSION_CHECK=1", "PIP_BREAK_SYSTEM_PACKAGES=1", "NPM_CONFIG_UPDATE_NOTIFIER=false",
        "NPM_CONFIG_FUND=false", "NPM_CONFIG_AUDIT=false"};
    for (const auto& e : extra) {   // an extra NAME=value replaces the default for NAME
        const std::string key = e.substr(0, e.find('=') + 1);
        std::erase_if(env, [&](const std::string& x) { return x.rfind(key, 0) == 0; });
        env.push_back(e);
    }
    return env;
}

bool is_binary_blob(const std::string& s) { return s.find('\0', 0) != std::string::npos && s.find('\0') < 8192; }

// Walks a project and calls fn(relative path, absolute path) for each regular file, skipping build / dependency folders.
template <typename Fn>
void walk_project(const fs::path& base, Fn fn, std::size_t max_files = 20000) {
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return;
    std::size_t count = 0;
    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && !ec; it.increment(ec)) {
        const auto& e = *it;
        const std::string name = e.path().filename().string();
        if (e.is_directory(ec)) { if (skip_dir(name) || name == ".astro") it.disable_recursion_pending(); continue; }
        if (e.is_symlink(ec) || !e.is_regular_file(ec)) continue;
        if (++count > max_files) break;
        fn(fs::relative(e.path(), base, ec).generic_string(), e.path());
    }
}

std::optional<std::string> read_small(const fs::path& p, std::size_t limit) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec || size > limit) return std::nullopt;
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ---------- search ----------
void ide_search(int fd, const fs::path& base, const Json& body) {
    const std::string query = get_string(body, "query");
    const bool use_regex = json_bool(body, "regex", false), match_case = json_bool(body, "case", false), whole = json_bool(body, "word", false);
    if (query.empty() || query.size() > 500) { send_json(fd, 400, error_json("Type something to search for.")); return; }
    std::optional<std::regex> re;
    try {
        std::string pattern = use_regex ? query : std::regex_replace(query, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)");
        if (whole) pattern = "\\b(?:" + pattern + ")\\b";
        re.emplace(pattern, match_case ? std::regex::ECMAScript : (std::regex::ECMAScript | std::regex::icase));
    } catch (const std::regex_error&) { send_json(fd, 400, error_json("That regular expression is not valid.")); return; }
    Json::Array results;
    std::size_t total = 0, files_hit = 0; bool truncated = false;
    walk_project(base, [&](const std::string& rel, const fs::path& abs) {
        if (truncated) return;
        const auto text = read_small(abs, 1'000'000);
        if (!text || is_binary_blob(*text)) return;
        std::istringstream in(*text);
        std::string line; int n = 0; Json::Array hits;
        while (std::getline(in, line)) {
            ++n;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::smatch m;
            if (line.size() > 4000 || !std::regex_search(line, m, *re)) continue;
            Json::Object h; h["line"] = Json(static_cast<double>(n)); h["col"] = Json(static_cast<double>(m.position(0) + 1));
            h["len"] = Json(static_cast<double>(m.length(0)));
            h["text"] = Json(line.size() > 300 ? line.substr(0, 300) : line);
            hits.emplace_back(std::move(h));
            if (++total >= 2000 || hits.size() >= 200) { truncated = total >= 2000; break; }
        }
        if (!hits.empty()) { Json::Object f; f["path"] = Json(rel); f["hits"] = Json(std::move(hits)); results.emplace_back(std::move(f)); ++files_hit; }
    });
    Json::Object o; o["files"] = Json(std::move(results)); o["matches"] = Json(static_cast<double>(total));
    o["truncated"] = Json(truncated); send_json(fd, 200, Json(std::move(o)));
}

// ---------- ZIP (stored, no compression; every browser and OS can open it) ----------
std::uint32_t crc32_of(std::string_view data) {
    static std::uint32_t table[256];
    static std::once_flag once;
    std::call_once(once, [] { for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t c = i; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } });
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : data) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
void put16(std::string& s, unsigned v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); }
void put32(std::string& s, std::uint32_t v) { for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF); }

void ide_zip(int fd, const std::string& project, const fs::path& base) {
    std::string zip, central;
    std::size_t count = 0, total = 0;
    bool too_big = false;
    const std::time_t now = std::time(nullptr);
    std::tm tmv{}; localtime_r(&now, &tmv);
    const unsigned dos_time = (tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2);
    const unsigned dos_date = ((tmv.tm_year - 80) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday;
    walk_project(base, [&](const std::string& rel, const fs::path& abs) {
        if (too_big) return;
        const auto data = read_small(abs, 50'000'000);
        if (!data) return;
        total += data->size();
        if (total > 150'000'000 || count >= 20000) { too_big = true; return; }
        const std::string name = project + "/" + rel;
        const std::uint32_t crc = crc32_of(*data);
        const std::uint32_t offset = static_cast<std::uint32_t>(zip.size());
        put32(zip, 0x04034b50); put16(zip, 20); put16(zip, 0x0800); put16(zip, 0); put16(zip, dos_time); put16(zip, dos_date);
        put32(zip, crc); put32(zip, static_cast<std::uint32_t>(data->size())); put32(zip, static_cast<std::uint32_t>(data->size()));
        put16(zip, static_cast<unsigned>(name.size())); put16(zip, 0); zip += name; zip += *data;
        put32(central, 0x02014b50); put16(central, 0x031E); put16(central, 20); put16(central, 0x0800); put16(central, 0); put16(central, dos_time); put16(central, dos_date);
        put32(central, crc); put32(central, static_cast<std::uint32_t>(data->size())); put32(central, static_cast<std::uint32_t>(data->size()));
        put16(central, static_cast<unsigned>(name.size())); put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0);
        put32(central, 0100644u << 16); put32(central, offset); central += name;
        ++count;
    });
    if (too_big) { send_json(fd, 413, error_json("The project is too large to download as one ZIP (over 150 MB).")); return; }
    const std::uint32_t cd_offset = static_cast<std::uint32_t>(zip.size());
    zip += central;
    put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0); put16(zip, static_cast<unsigned>(count)); put16(zip, static_cast<unsigned>(count));
    put32(zip, static_cast<std::uint32_t>(central.size())); put32(zip, cd_offset); put16(zip, 0);
    std::ostringstream h;
    h << "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: " << zip.size()
      << "\r\nContent-Disposition: attachment; filename=\"" << project << ".zip\"\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n";
    send_all(fd, h.str());
    send_all(fd, zip);
}

// ---------- checkpoints ----------
fs::path history_dir(const std::string& project) { return fs::path(data_dir()) / ".astro_history" / project; }

Json history_meta(const fs::path& dir) {
    if (auto txt = read_file_text((dir / "meta.json").string())) { try { return JsonParser(*txt).parse(); } catch (...) {} }
    return Json(Json::Object{});
}

std::vector<fs::path> history_list(const std::string& project) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(history_dir(project), ec)) if (e.is_directory(ec) && fs::exists(e.path() / "meta.json", ec)) out.push_back(e.path());
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) { return a.filename().string() > b.filename().string(); });
    return out;
}

// Copies the project into a new checkpoint. Returns the checkpoint id, or "" with error set.
std::string history_create(const std::string& project, const fs::path& base, const std::string& label, const std::string& kind, std::string& error) {
    std::error_code ec;
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string id = std::to_string(ms) + "-" + random_hex(3);
    const fs::path dir = history_dir(project) / id;
    const fs::path files = dir / "files";
    fs::create_directories(files, ec);
    if (ec) { error = "Could not create the checkpoint folder."; return ""; }
    std::size_t count = 0, bytes = 0; bool too_big = false;
    walk_project(base, [&](const std::string& rel, const fs::path& abs) {
        if (too_big) return;
        std::error_code e2;
        const auto size = fs::file_size(abs, e2);
        if (e2 || size > 10'000'000) return;                // very large files are not versioned
        bytes += size;
        if (bytes > 80'000'000) { too_big = true; return; }
        fs::create_directories((files / rel).parent_path(), e2);
        fs::copy_file(abs, files / rel, fs::copy_options::overwrite_existing, e2);
        if (!e2) ++count;
    });
    if (too_big) { fs::remove_all(dir, ec); error = "The project is too large for a checkpoint (over 80 MB of files)."; return ""; }
    Json::Object meta;
    meta["id"] = Json(id); meta["label"] = Json(label.empty() ? std::string("Checkpoint") : label.substr(0, 160));
    meta["kind"] = Json(kind.empty() ? std::string("manual") : kind); meta["ts"] = Json(static_cast<double>(ms));
    meta["files"] = Json(static_cast<double>(count)); meta["bytes"] = Json(static_cast<double>(bytes));
    write_file_private((dir / "meta.json").string(), dump_json(Json(std::move(meta))));
    // Keep the newest 50 checkpoints per project.
    auto all = history_list(project);
    for (std::size_t i = 50; i < all.size(); ++i) fs::remove_all(all[i], ec);
    return id;
}

bool same_file_content(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::file_size(a, ec) != fs::file_size(b, ec)) return false;
    const auto x = read_small(a, 20'000'000), y = read_small(b, 20'000'000);
    return x && y && *x == *y;
}

void ide_history(int fd, const std::string& action, const std::string& project, const fs::path& base, const Json& body) {
    std::error_code ec;
    if (action == "list") {
        Json::Array list;
        for (const auto& d : history_list(project)) list.emplace_back(history_meta(d));
        Json::Object o; o["checkpoints"] = Json(std::move(list)); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "create") {
        std::string error;
        const std::string id = history_create(project, base, get_string(body, "label"), get_string(body, "kind"), error);
        if (id.empty()) { send_json(fd, 500, error_json(error)); return; }
        Json::Object o; o["ok"] = Json(true); o["id"] = Json(id); send_json(fd, 200, Json(std::move(o))); return;
    }
    const std::string id = get_string(body, "id");
    if (id.empty() || id.find('/') != std::string::npos || id.find("..") != std::string::npos) { send_json(fd, 400, error_json("Unknown checkpoint.")); return; }
    const fs::path dir = history_dir(project) / id;
    if (!fs::exists(dir / "meta.json", ec)) { send_json(fd, 404, error_json("That checkpoint no longer exists.")); return; }
    const fs::path snap = dir / "files";
    if (action == "delete") { fs::remove_all(dir, ec); Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return; }
    if (action == "changes") {
        // What changed between the checkpoint and the project now.
        std::map<std::string, std::string> status;
        walk_project(snap, [&](const std::string& rel, const fs::path&) { status[rel] = "deleted"; });
        walk_project(base, [&](const std::string& rel, const fs::path& abs) {
            auto it = status.find(rel);
            if (it == status.end()) status[rel] = "added";
            else it->second = same_file_content(abs, snap / rel) ? "same" : "modified";
        });
        Json::Array list;
        for (const auto& [rel, st] : status) if (st != "same") { Json::Object f; f["path"] = Json(rel); f["status"] = Json(st); list.emplace_back(std::move(f)); }
        Json::Object o; o["changes"] = Json(std::move(list)); o["meta"] = history_meta(dir); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "read") {
        const std::string rel = get_string(body, "path");
        if (rel.empty() || rel.find("..") != std::string::npos || rel[0] == '/') { send_json(fd, 400, error_json("Invalid path.")); return; }
        Json::Object o;
        const auto text = read_small(snap / rel, 2'000'000);
        if (!text) o["missing"] = Json(true);
        else if (is_binary_blob(*text)) o["binary"] = Json(true);
        else o["content"] = Json(*text);
        send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "restore") {
        std::string error;
        const std::string before = history_create(project, base, "Before restoring " + get_string(history_meta(dir), "label"), "restore", error);
        if (before.empty()) { send_json(fd, 500, error_json("Could not save the current version first: " + error)); return; }
        // Remove current files (not dependency folders), then copy the checkpoint back.
        std::vector<fs::path> current;
        walk_project(base, [&](const std::string&, const fs::path& abs) { std::error_code e2; const auto sz = fs::file_size(abs, e2); if (!e2 && sz <= 10'000'000) current.push_back(abs); });   // files over 10 MB are never in a checkpoint, so never delete them
        for (const auto& p : current) fs::remove(p, ec);
        std::size_t restored = 0;
        walk_project(snap, [&](const std::string& rel, const fs::path& abs) {
            std::error_code e2;
            fs::create_directories((base / rel).parent_path(), e2);
            fs::copy_file(abs, base / rel, fs::copy_options::overwrite_existing, e2);
            if (!e2) ++restored;
        });
        // Drop folders left empty by the restore.
        for (int pass = 0; pass < 6; ++pass) {
            std::vector<fs::path> empty;
            for (fs::recursive_directory_iterator it(base, ec), end; it != end && !ec; it.increment(ec)) {
                if (it->is_directory(ec)) { const std::string n = it->path().filename().string(); if (skip_dir(n) || n == ".astro") { it.disable_recursion_pending(); continue; } if (fs::is_empty(it->path(), ec)) empty.push_back(it->path()); }
            }
            if (empty.empty()) break;
            for (const auto& p : empty) fs::remove(p, ec);
        }
        Json::Object o; o["ok"] = Json(true); o["restored"] = Json(static_cast<double>(restored)); o["before"] = Json(before);
        send_json(fd, 200, Json(std::move(o))); return;
    }
    send_json(fd, 404, error_json("Unknown history action."));
}

// ---------- shared: a child process with output buffered for streaming ----------
struct OutBuffer {
    std::mutex m;
    std::condition_variable cv;
    std::string data;            // last <= cap bytes
    std::size_t base = 0;        // absolute offset of data[0]
    bool closed = false;
    int exit_code = -1;
    std::string exit_note;
    static constexpr std::size_t cap = 400'000;
    void append(const char* p, std::size_t n) {
        std::lock_guard<std::mutex> lock(m);
        data.append(p, n);
        if (data.size() > cap) { const std::size_t drop = data.size() - cap; data.erase(0, drop); base += drop; }
        cv.notify_all();
    }
    std::size_t end() { return base + data.size(); }
};

// Streams an OutBuffer as SSE: {d: base64 chunk, o: offset}... then {exit}. Returns when the client leaves or output ends.
void stream_buffer(int fd, OutBuffer& buf, std::size_t from, std::atomic<long long>* touch = nullptr) {
    send_all(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nCache-Control: no-store\r\n"
                 "X-Accel-Buffering: no\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n");
    std::size_t pos = from;
    auto last_beat = std::chrono::steady_clock::now();
    for (;;) {
        std::string chunk; bool closed = false; int code = -1; std::string note; std::size_t start = 0;
        {
            std::unique_lock<std::mutex> lock(buf.m);
            buf.cv.wait_for(lock, std::chrono::milliseconds(400), [&] { return buf.base + buf.data.size() > pos || buf.closed; });
            if (pos < buf.base) pos = buf.base;   // fell behind the ring buffer
            start = pos;
            if (buf.base + buf.data.size() > pos) { chunk = buf.data.substr(pos - buf.base, 64 * 1024); pos += chunk.size(); }
            closed = buf.closed && pos >= buf.base + buf.data.size();
            code = buf.exit_code; note = buf.exit_note;
        }
        if (touch) *touch = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (!chunk.empty()) {
            Json::Object o; o["d"] = Json(b64enc(chunk)); o["o"] = Json(static_cast<double>(start + chunk.size()));
            if (!send_sse(fd, Json(std::move(o)))) return;
            continue;
        }
        if (closed) { Json::Object o; o["exit"] = Json(static_cast<double>(code)); if (!note.empty()) o["note"] = Json(note); send_sse(fd, Json(std::move(o))); return; }
        if (std::chrono::steady_clock::now() - last_beat > std::chrono::seconds(15)) { if (!send_all(fd, ": ping\n\n")) return; last_beat = std::chrono::steady_clock::now(); }
        if (client_disconnected(fd)) return;
    }
}

long long now_s() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void child_limits(bool interactive) {
#ifndef _WIN32
    rlimit fsz{512ull << 20, 512ull << 20}; setrlimit(RLIMIT_FSIZE, &fsz);
    rlimit nofile{4096, 4096}; setrlimit(RLIMIT_NOFILE, &nofile);
    rlimit core{0, 0}; setrlimit(RLIMIT_CORE, &core);
#endif
    (void)interactive;
}

std::vector<char*> cstrings(std::vector<std::string>& v) { std::vector<char*> out; for (auto& s : v) out.push_back(s.data()); out.push_back(nullptr); return out; }

// ---------- interactive terminals ----------
struct Term {
    std::string id, project;
    int master = -1;
    pid_t pid = -1;
    OutBuffer out;
    std::atomic<long long> touched{0};
    std::atomic<bool> dead{false};
#ifdef _WIN32
    winp::Child child;
    HANDLE in_wr = nullptr;
    void* hpc = nullptr;
#endif
};
std::mutex g_terms_mutex;
std::map<std::string, std::shared_ptr<Term>> g_terms;

void term_kill(const std::shared_ptr<Term>& t) {
#ifdef _WIN32
    if (!t->dead) winp::kill_tree(t->child);
#else
    if (t->pid > 0) { kill(-t->pid, SIGHUP); kill(-t->pid, SIGKILL); }
#endif
}

void term_write(const std::shared_ptr<Term>& t, const std::string& data) {
    if (t->dead || data.empty()) return;
#ifdef _WIN32
    std::size_t off = 0; DWORD w = 0;
    while (off < data.size() && WriteFile(t->in_wr, data.data() + off, static_cast<DWORD>(data.size() - off), &w, nullptr) && w > 0) off += w;
#else
    std::size_t off = 0;
    while (off < data.size()) { const ssize_t w = write(t->master, data.data() + off, data.size() - off); if (w <= 0) { if (errno == EINTR) continue; break; } off += static_cast<std::size_t>(w); }
#endif
}

void term_resize(const std::shared_ptr<Term>& t, int cols, int rows) {
    if (t->dead) return;
#ifdef _WIN32
    if (t->hpc && winp::conpty().resize) winp::conpty().resize(t->hpc, COORD{static_cast<SHORT>(cols), static_cast<SHORT>(rows)});
#else
    winsize wsz{}; wsz.ws_col = static_cast<unsigned short>(cols); wsz.ws_row = static_cast<unsigned short>(rows);
    ioctl(t->master, TIOCSWINSZ, &wsz);
#endif
}

std::shared_ptr<Term> term_open(const std::string& project, const fs::path& dir, int cols, int rows, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(g_terms_mutex);
        if (g_terms.size() >= 8) { error = "Too many terminals are open. Close one first."; return nullptr; }
    }
    auto t = std::make_shared<Term>();
    t->id = random_hex(12); t->project = project; t->touched = now_s();
#ifdef _WIN32
    {
        const auto& api = winp::conpty();
        if (!api.create) { error = "The terminal needs Windows 10 (version 1809) or newer."; return nullptr; }
        HANDLE in_rd = nullptr, in_wr = nullptr, out_rd = nullptr, out_wr = nullptr;
        if (!CreatePipe(&in_rd, &in_wr, nullptr, 0)) { error = "Could not start a terminal."; return nullptr; }
        if (!CreatePipe(&out_rd, &out_wr, nullptr, 0)) { CloseHandle(in_rd); CloseHandle(in_wr); error = "Could not start a terminal."; return nullptr; }
        void* hpc = nullptr;
        const COORD size{static_cast<SHORT>(std::clamp(cols, 20, 400)), static_cast<SHORT>(std::clamp(rows, 5, 200))};
        if (FAILED(api.create(size, in_rd, out_wr, 0, &hpc))) {
            for (HANDLE h : {in_rd, in_wr, out_rd, out_wr}) CloseHandle(h);
            error = "Could not start a terminal."; return nullptr;
        }
        CloseHandle(in_rd); CloseHandle(out_wr);   // the pseudo console keeps its own copies
        const std::string& bash = winp::bash_path();
        const std::string cmdline = bash.empty() ? std::string("powershell.exe -NoLogo") : winp::quote_arg(bash) + " --login -i";
        std::vector<std::string> env = ide_env(dir, {"TERM=xterm-256color", "COLORTERM=truecolor", "ASTRO_PROJECT=" + project});
        if (!winp::spawn(cmdline, dir.string(), env, nullptr, nullptr, nullptr, t->child, error, hpc)) {
            api.close(hpc); CloseHandle(in_wr); CloseHandle(out_rd);
            if (error.empty()) error = "Could not start a terminal.";
            return nullptr;
        }
        t->in_wr = in_wr; t->hpc = hpc; t->pid = static_cast<pid_t>(t->child.pid);
        std::thread([t] {   // the shell ended: closing the pseudo console ends the output pipe
            WaitForSingleObject(t->child.proc, INFINITE);
            if (winp::conpty().close) winp::conpty().close(t->hpc);
        }).detach();
        std::thread([t, out_rd] {
            char buf[16384]; DWORD got = 0;
            while (ReadFile(out_rd, buf, sizeof(buf), &got, nullptr) && got > 0) t->out.append(buf, got);
            CloseHandle(out_rd);
            WaitForSingleObject(t->child.proc, 5000);
            {
                std::lock_guard<std::mutex> lock(t->out.m);
                t->out.closed = true; t->out.exit_code = winp::exit_code(t->child);
                t->out.cv.notify_all();
            }
            t->dead = true;
            CloseHandle(t->in_wr); t->in_wr = nullptr;
            winp::kill_tree(t->child); winp::close_child(t->child);
            std::this_thread::sleep_for(std::chrono::seconds(60));
            std::lock_guard<std::mutex> lock(g_terms_mutex);
            g_terms.erase(t->id);
        }).detach();
        std::lock_guard<std::mutex> lock(g_terms_mutex);
        g_terms[t->id] = t;
        return t;
    }
#else
    winsize wsz{}; wsz.ws_col = static_cast<unsigned short>(std::clamp(cols, 20, 400)); wsz.ws_row = static_cast<unsigned short>(std::clamp(rows, 5, 200));
    std::vector<std::string> env = ide_env(dir, {"TERM=xterm-256color", "COLORTERM=truecolor",
        "PS1=\\[\\e[38;5;141m\\]astro\\[\\e[0m\\]:\\[\\e[38;5;75m\\]\\w\\[\\e[0m\\]$ ", "PROMPT_DIRTRIM=2", "ASTRO_PROJECT=" + project});
    const std::string dir_s = dir.string();
    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &wsz);
    if (pid == 0) {
        if (chdir(dir_s.c_str()) != 0) _exit(127);
        for (int f = 3; f < 4096; ++f) close(f);
        child_limits(true);
        signal(SIGPIPE, SIG_DFL);
        auto envp = cstrings(env);
        const char* argv[] = {"bash", "--noprofile", "--norc", "-i", nullptr};
        execve("/bin/bash", const_cast<char* const*>(argv), envp.data());
        _exit(127);
    }
    if (pid < 0) { error = "Could not start a terminal."; return nullptr; }
    fcntl(master, F_SETFD, FD_CLOEXEC);
    t->master = master; t->pid = pid;
    std::thread([t] {
        char buf[16384];
        for (;;) {
            const ssize_t got = read(t->master, buf, sizeof(buf));
            if (got > 0) { t->out.append(buf, static_cast<std::size_t>(got)); continue; }
            if (got < 0 && errno == EINTR) continue;
            break;
        }
        int status = 0; waitpid(t->pid, &status, 0);
        {
            std::lock_guard<std::mutex> lock(t->out.m);
            t->out.closed = true; t->out.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            t->out.cv.notify_all();
        }
        close(t->master);
        t->dead = true;
        std::this_thread::sleep_for(std::chrono::seconds(60));   // let a reconnecting tab read the last output
        std::lock_guard<std::mutex> lock(g_terms_mutex);
        g_terms.erase(t->id);
    }).detach();
    std::lock_guard<std::mutex> lock(g_terms_mutex);
    g_terms[t->id] = t;
    return t;
#endif
}

std::shared_ptr<Term> term_get(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_terms_mutex);
    auto it = g_terms.find(id);
    return it == g_terms.end() ? nullptr : it->second;
}

// ---------- app servers ----------
struct AppProc {
    std::string project, command;
    pid_t pid = -1;
    int port = 0;                       // PORT given to the app
    std::atomic<int> listen_port{0};    // port the app actually listens on (detected)
    OutBuffer out;
    std::atomic<long long> touched{0};
    std::atomic<bool> dead{false};
    long long started_ms = 0;
#ifdef _WIN32
    winp::Child child;
#endif
};
std::mutex g_procs_mutex;
std::map<std::string, std::shared_ptr<AppProc>> g_procs;

bool port_free(int port) {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(static_cast<uint16_t>(port)); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool ok = bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
    sock_close(s);
    return ok;
}

// True if something accepts TCP connections on 127.0.0.1:port.
[[maybe_unused]] bool port_listening(int port) {
    const int s = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (s < 0) return false;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(static_cast<uint16_t>(port)); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool ok = connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
    sock_close(s);
    return ok;
}

#if !defined(__linux__)
// Windows: ports owned by any process in the app's Job Object. macOS: the PORT we gave the app.
int detect_listen_port(const std::shared_ptr<AppProc>& p) {
#ifdef _WIN32
    return winp::listen_port_of(winp::job_pids(p->child.job));
#else
    return port_listening(p->port) ? p->port : 0;
#endif
}
#else
int detect_listen_port(pid_t pgid);
int detect_listen_port(const std::shared_ptr<AppProc>& p) { return detect_listen_port(p->pid); }
#endif

#if defined(__linux__)
// Finds a TCP port in LISTEN state owned by any process in the group `pgid` (reads /proc).
int detect_listen_port(pid_t pgid) {
    std::map<std::string, int> inode_port;
    for (const char* file : {"/proc/net/tcp", "/proc/net/tcp6"}) {
        std::ifstream in(file);
        std::string line; std::getline(in, line);
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string sl, local, remote, st, txrx, tr, retr, uid, timeout, inode;
            ls >> sl >> local >> remote >> st >> txrx >> tr >> retr >> uid >> timeout >> inode;
            if (st != "0A") continue;
            const auto colon = local.rfind(':');
            if (colon == std::string::npos) continue;
            try { inode_port[inode] = std::stoi(local.substr(colon + 1), nullptr, 16); } catch (...) {}
        }
    }
    if (inode_port.empty()) return 0;
    std::error_code ec;
    int best = 0;
    for (const auto& e : fs::directory_iterator("/proc", ec)) {
        const std::string name = e.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(), ::isdigit)) continue;
        const pid_t p = static_cast<pid_t>(std::stoi(name));
        if (getpgid(p) != pgid) continue;
        for (const auto& f : fs::directory_iterator(e.path() / "fd", ec)) {
            std::error_code e2;
            const std::string target = fs::read_symlink(f.path(), e2).string();
            if (e2 || target.rfind("socket:[", 0) != 0) continue;
            const std::string inode = target.substr(8, target.size() - 9);
            if (auto it = inode_port.find(inode); it != inode_port.end() && (best == 0 || it->second < best)) best = it->second;
        }
    }
    return best;
}
#endif

void proc_stop(const std::string& project) {
    std::shared_ptr<AppProc> p;
    {
        std::lock_guard<std::mutex> lock(g_procs_mutex);
        auto it = g_procs.find(project);
        if (it == g_procs.end()) return;
        p = it->second; g_procs.erase(it);
    }
#ifdef _WIN32
    if (!p->dead) winp::kill_tree(p->child);
#else
    if (p->pid > 0 && !p->dead) { kill(-p->pid, SIGTERM); std::this_thread::sleep_for(std::chrono::milliseconds(300)); kill(-p->pid, SIGKILL); }
#endif
}

std::shared_ptr<AppProc> proc_get(const std::string& project) {
    std::lock_guard<std::mutex> lock(g_procs_mutex);
    auto it = g_procs.find(project);
    return it == g_procs.end() ? nullptr : it->second;
}

std::shared_ptr<AppProc> proc_start(const std::string& project, const fs::path& dir, const std::string& command, std::string& error) {
    proc_stop(project);
    {
        std::lock_guard<std::mutex> lock(g_procs_mutex);
        if (g_procs.size() >= 4) { error = "Four app servers are already running. Stop one first."; return nullptr; }
    }
    int port = 0;
    for (int tries = 0; tries < 200 && !port; ++tries) { const int cand = 5100 + static_cast<int>(std::stoul(random_hex(2), nullptr, 16) % 200); if (port_free(cand)) port = cand; }
    if (!port) { error = "No free port for the app server."; return nullptr; }
    auto p = std::make_shared<AppProc>();
    p->project = project; p->command = command; p->port = port; p->touched = now_s();
    p->started_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<std::string> env = ide_env(dir, {"PORT=" + std::to_string(port), "HOST=127.0.0.1", "FLASK_RUN_PORT=" + std::to_string(port),
        "FLASK_RUN_HOST=127.0.0.1", "NODE_ENV=development", "TERM=dumb", "NO_COLOR=1", "FORCE_COLOR=0", "BROWSER=none"});
    const std::string dir_s = dir.string();
#ifdef _WIN32
    {
        bool have_bash = false;
        const std::string cmdline = winp::shell_cmdline(command, have_bash);
        if (!have_bash) { error = "Starting app servers on Windows needs Git for Windows (it provides bash). Install it from https://git-scm.com."; return nullptr; }
        HANDLE rd = nullptr, wr = nullptr;
        if (!winp::make_pipe(rd, wr, false)) { error = "Could not create pipes."; return nullptr; }
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        const bool ok = winp::spawn(cmdline, dir_s, env, nul, wr, wr, p->child, error);
        CloseHandle(wr);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!ok) { CloseHandle(rd); if (error.empty()) error = "Could not start the app."; return nullptr; }
        p->pid = static_cast<pid_t>(p->child.pid);
        const std::string banner = "$ " + command + "\n";
        p->out.append(banner.data(), banner.size());
        std::thread([p, rd] {
            char buf[8192]; DWORD got = 0;
            while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) p->out.append(buf, got);
            CloseHandle(rd);
            WaitForSingleObject(p->child.proc, INFINITE);
            const int code = winp::exit_code(p->child);
            winp::kill_tree(p->child);   // leftover children
            std::lock_guard<std::mutex> lock(p->out.m);
            p->out.closed = true; p->out.exit_code = code;
            p->dead = true;
            p->out.cv.notify_all();
        }).detach();
    }
#else
    int pipefd[2];
    if (pipe_cloexec(pipefd) != 0) { error = "Could not create pipes."; return nullptr; }
    const pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(pipefd[1], 1); dup2(pipefd[1], 2);
        for (int f = 3; f < 4096; ++f) close(f);
        if (chdir(dir_s.c_str()) != 0) _exit(127);
        child_limits(false);
        signal(SIGPIPE, SIG_DFL);
        auto envp = cstrings(env);
        const char* argv[] = {"bash", "-c", command.c_str(), nullptr};
        execve("/bin/bash", const_cast<char* const*>(argv), envp.data());
        _exit(127);
    }
    close(pipefd[1]);
    if (pid < 0) { close(pipefd[0]); error = "Could not start the app."; return nullptr; }
    p->pid = pid;
    const std::string banner = "$ " + command + "\n";
    p->out.append(banner.data(), banner.size());
    const int rd = pipefd[0];
    std::thread([p, rd] {
        char buf[8192];
        for (;;) {
            const ssize_t got = read(rd, buf, sizeof(buf));
            if (got > 0) { p->out.append(buf, static_cast<std::size_t>(got)); continue; }
            if (got < 0 && errno == EINTR) continue;
            break;
        }
        close(rd);
        int status = 0; waitpid(p->pid, &status, 0);
        kill(-p->pid, SIGKILL);   // leftover children
        std::lock_guard<std::mutex> lock(p->out.m);
        p->out.closed = true; p->out.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (WIFSIGNALED(status)) p->out.exit_note = strsignal(WTERMSIG(status));
        p->dead = true;
        p->out.cv.notify_all();
    }).detach();
#endif
    // Watch for the listening port for up to 3 minutes (npm installs / slow starts).
    std::thread([p] {
        for (int i = 0; i < 360 && !p->dead; ++i) {
            const int found = detect_listen_port(p);
            if (found) { p->listen_port = found; if (found == p->port || i > 40) break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }).detach();
    std::lock_guard<std::mutex> lock(g_procs_mutex);
    g_procs[project] = p;
    return p;
}

Json proc_status_json(const std::shared_ptr<AppProc>& p) {
    Json::Object o;
    if (!p) { o["running"] = Json(false); return Json(std::move(o)); }
    o["running"] = Json(!p->dead.load()); o["command"] = Json(p->command);
    o["port"] = Json(static_cast<double>(p->port)); o["listening"] = Json(static_cast<double>(p->listen_port.load()));
    o["url"] = Json("/app/" + preview_key() + "/" + p->project + "/");
    o["started"] = Json(static_cast<double>(p->started_ms));
    if (p->dead) { o["exit"] = Json(static_cast<double>(p->out.exit_code)); if (!p->out.exit_note.empty()) o["note"] = Json(p->out.exit_note); }
    return Json(std::move(o));
}

// Reaps idle terminals and app servers every minute.
void ide_janitor() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(60));
                const long long now = now_s();
                std::vector<std::shared_ptr<Term>> old_terms;
                {
                    std::lock_guard<std::mutex> lock(g_terms_mutex);
                    for (auto& [id, t] : g_terms) if (!t->dead && now - t->touched > 3 * 3600) old_terms.push_back(t);
                }
                for (auto& t : old_terms) term_kill(t);
                std::vector<std::string> old_procs;
                {
                    std::lock_guard<std::mutex> lock(g_procs_mutex);
                    for (auto& [proj, p] : g_procs) if (now - p->touched > 2 * 3600 || (p->dead && now - p->touched > 3600)) old_procs.push_back(proj);
                }
                for (auto& proj : old_procs) proc_stop(proj);
            }
        }).detach();
    });
}

// ---------- app preview proxy ----------
// Injected into HTML pages served by the preview or the app proxy: working storage (see kStorageShim),
// console capture (shown under Preview), and for proxied apps, URL fixing so "/api/x" reaches the app.
const char* kConsoleShim = R"JS(<script>(function(){if(window.__astroConsole)return;window.__astroConsole=1;var n=0;
function ser(a){try{if(a instanceof Error)return (a.stack||a.message||String(a));if(typeof a==='string')return a;if(a===undefined)return 'undefined';if(typeof a==='function')return a.toString().slice(0,200);var s=JSON.stringify(a,null,1);return s===undefined?String(a):s}catch(e){return String(a)}}
function send(level,args){if(n++>800)return;try{parent.postMessage({astroConsole:{level:level,text:Array.prototype.map.call(args,ser).join(' ').slice(0,4000)}},'*')}catch(e){}}
['log','info','warn','error','debug'].forEach(function(l){var o=console[l];console[l]=function(){send(l,arguments);return o&&o.apply(console,arguments)}});
addEventListener('error',function(e){if(e.target&&e.target!==window&&(e.target.src||e.target.href)){send('error',['Failed to load '+(e.target.src||e.target.href)]);return}send('error',[(e.message||'Error')+(e.filename?' ('+e.filename.split('/').slice(-1)[0]+':'+e.lineno+')':'')])},true);
addEventListener('unhandledrejection',function(e){send('error',['Unhandled promise rejection: '+ser(e.reason)])});
try{parent.postMessage({astroConsole:{level:'clear'}},'*')}catch(e){}})();</script>)JS";

std::string url_fix_shim(const std::string& prefix) {
    return "<script>(function(){var P=" + dump_json(Json(prefix)) + R"JS(;function fix(u){try{var x=new URL(String(u),location.href);
if((x.origin===location.origin||/^(localhost|127\.0\.0\.1|0\.0\.0\.0)$/.test(x.hostname))&&x.pathname.indexOf(P)!==0&&/^https?:$/.test(x.protocol)){return location.origin+P+x.pathname+x.search+x.hash}}catch(e){}return u}
var F=window.fetch;if(F)window.fetch=function(i,o){try{if(i&&typeof i==='object'&&i.url){var f=fix(i.url);if(f!==i.url)i=new Request(f,i)}else i=fix(i)}catch(e){}return F.call(this,i,o)};
var XO=XMLHttpRequest.prototype.open;XMLHttpRequest.prototype.open=function(m,u){arguments[1]=fix(u);return XO.apply(this,arguments)};
if(window.EventSource){var ES=window.EventSource;window.EventSource=function(u,c){return new ES(fix(u),c)};window.EventSource.prototype=ES.prototype}
document.addEventListener('submit',function(e){var f=e.target;if(f&&f.getAttribute){var a=f.getAttribute('action');if(a&&a.charAt(0)==='/'&&a.indexOf(P)!==0)f.setAttribute('action',P+a)}},true);
document.addEventListener('click',function(e){var a=e.target&&e.target.closest&&e.target.closest('a[href]');if(a){var h=a.getAttribute('href');if(h&&h.charAt(0)==='/'&&h.charAt(1)!=='/'&&h.indexOf(P)!==0)a.setAttribute('href',P+h)}},true);})();</script>)JS";
}

std::string inject_after_head(const std::string& html, const std::string& snippet) {
    std::string lower = lower_copy(html.substr(0, std::min<std::size_t>(html.size(), 8000)));
    for (const char* tag : {"<head", "<html", "<!doctype"}) {
        const auto at = lower.find(tag);
        if (at == std::string::npos) continue;
        const auto close = lower.find('>', at);
        if (close != std::string::npos) return html.substr(0, close + 1) + snippet + html.substr(close + 1);
    }
    return snippet + html;
}

std::string rewrite_root_urls(const std::string& text, const std::string& prefix, bool css) {
    static const std::regex attr(R"((\s(?:src|href|action|poster|data-src|formaction)\s*=\s*["'])/(?!/))", std::regex::icase);
    static const std::regex cssurl(R"((url\(\s*["']?)/(?!/))", std::regex::icase);
    std::string out = text;
    if (!css) out = std::regex_replace(out, attr, "$1" + prefix + "/");
    out = std::regex_replace(out, cssurl, "$1" + prefix + "/");
    return out;
}

struct CurlResp { long status = 0; std::vector<std::pair<std::string, std::string>> headers; std::string body; bool too_big = false; };
size_t curl_body_cb(char* p, size_t s, size_t n, void* ud) {
    auto* r = static_cast<CurlResp*>(ud);
    if (r->body.size() + s * n > 60'000'000) { r->too_big = true; return 0; }
    r->body.append(p, s * n); return s * n;
}
size_t curl_head_cb(char* p, size_t s, size_t n, void* ud) {
    auto* r = static_cast<CurlResp*>(ud);
    std::string line(p, s * n);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.rfind("HTTP/", 0) == 0) { r->headers.clear(); return s * n; }   // new response (after 100-continue)
    const auto c = line.find(':');
    if (c != std::string::npos) r->headers.emplace_back(lower_copy(trim(line.substr(0, c))), trim(line.substr(c + 1)));
    return s * n;
}

const char* kSandboxCsp = "Content-Security-Policy: sandbox allow-scripts allow-forms allow-modals allow-popups allow-downloads allow-pointer-lock\r\n";

void app_error_page(int fd, int status, const std::string& title, const std::string& text) {
    const std::string html = "<!doctype html><meta charset=utf-8><body style=\"font:15px system-ui;background:#0b0d14;color:#cdd3e0;display:grid;place-items:center;height:90vh;margin:0\">"
        "<div style=\"max-width:460px;text-align:center\"><h2 style=\"color:#fff;font-weight:600\">" + title + "</h2><p>" + text + "</p></div>";
    std::ostringstream h;
    h << "HTTP/1.1 " << status << " " << reason_phrase(status) << "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " << html.size()
      << "\r\nCache-Control: no-store\r\nConnection: close\r\n" << kSandboxCsp << "\r\n";
    send_all(fd, h.str()); send_all(fd, html);
}

// ANY /app/<key>/<project>/<path>. Returns true when the path was an app path.
bool serve_app_proxy(int fd, const HttpRequest& request) {
    if (request.path.rfind("/app/", 0) != 0) return false;
    std::string rest = request.path.substr(5);
    const auto s1 = rest.find('/');
    const std::string key = url_unescape(rest.substr(0, s1));
    if (s1 == std::string::npos || !same_secret(key, preview_key())) { app_error_page(fd, 404, "Preview link expired", "Reopen the preview from the Workspace."); return true; }
    rest = rest.substr(s1 + 1);
    const auto s2 = rest.find('/');
    const std::string project = url_unescape(rest.substr(0, s2));
    const std::string sub = s2 == std::string::npos ? "/" : rest.substr(s2);   // keep the app's own encoding
    const std::string prefix = "/app/" + key + "/" + project;
    if (request.method == "OPTIONS") {
        send_all(fd, "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, PUT, PATCH, DELETE, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: *\r\nAccess-Control-Max-Age: 600\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return true;
    }
    const auto p = proc_get(project);
    if (!p || p->dead) { app_error_page(fd, 502, "The app server is not running", "Open a server file (for example <b>server.js</b> or <b>app.py</b>) and press <b>Run</b>. Astro AI starts it and shows it here."); return true; }
    p->touched = now_s();
    const int port = p->listen_port ? p->listen_port.load() : p->port;
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + sub + (request.query.empty() ? "" : "?" + request.query);
    CURL* curl = curl_easy_init();
    if (!curl) { app_error_page(fd, 500, "Proxy error", "Could not start the request."); return true; }
    CurlResp r;
    curl_slist* hdrs = nullptr;
    for (const auto& [k, v] : request.headers) {
        if (k == "host" || k == "connection" || k == "content-length" || k == "accept-encoding" || k == "transfer-encoding" || k == "upgrade" ||
            k == "x-astro-token" || k == "authorization" || k == "te" || k == "keep-alive" || k == "expect") continue;
        std::string value = v;
        if (k == "cookie") {   // never pass the Astro session to the app
            std::string kept; std::istringstream cs(v); std::string part;
            while (std::getline(cs, part, ';')) { const std::string t = trim(part); if (t.rfind("astro_session=", 0) == 0 || t.empty()) continue; kept += (kept.empty() ? "" : "; ") + t; }
            if (kept.empty()) continue;
            value = kept;
        }
        if (k == "origin" || k == "referer") continue;
        hdrs = curl_slist_append(hdrs, (k + ": " + value).c_str());
    }
    hdrs = curl_slist_append(hdrs, ("Host: 127.0.0.1:" + std::to_string(port)).c_str());
    hdrs = curl_slist_append(hdrs, "Accept-Encoding: identity");
    hdrs = curl_slist_append(hdrs, "Expect:");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (request.method == "HEAD") curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    if (!request.body.empty() || request.method == "POST" || request.method == "PUT" || request.method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_body_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_head_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &r);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http");
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK || r.status == 0) {
        const bool starting = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - p->started_ms < 60000;
        app_error_page(fd, 502, starting ? "The app is starting…" : "The app did not answer",
                       starting ? "Give it a moment and reload. Its output is shown under <b>Server</b>."
                                : "It is running but not answering on port " + std::to_string(port) + ". Check the <b>Server</b> log for errors.");
        return true;
    }
    std::string ctype;
    for (const auto& [k, v] : r.headers) if (k == "content-type") ctype = lower_copy(v);
    std::string body = std::move(r.body);
    if (ctype.find("text/html") != std::string::npos) {
        body = rewrite_root_urls(body, prefix, false);
        body = inject_after_head(body, std::string(kStorageShim) + kConsoleShim + url_fix_shim(prefix));
    } else if (ctype.find("text/css") != std::string::npos) {
        body = rewrite_root_urls(body, prefix, true);
    }
    std::ostringstream h;
    h << "HTTP/1.1 " << r.status << " " << reason_phrase(static_cast<int>(r.status)) << "\r\n";
    for (const auto& [k, v] : r.headers) {
        if (k == "content-length" || k == "transfer-encoding" || k == "connection" || k == "keep-alive" || k == "content-encoding" ||
            k == "content-security-policy" || k == "x-frame-options" || k == "strict-transport-security" || k == "access-control-allow-origin" ||
            k == "access-control-allow-credentials") continue;
        std::string value = v;
        if (k == "location") {
            static const std::regex local_origin(R"(^https?://(localhost|127\.0\.0\.1|0\.0\.0\.0)(:\d+)?)", std::regex::icase);
            value = std::regex_replace(value, local_origin, "");
            if (!value.empty() && value[0] == '/' && value.rfind(prefix, 0) != 0) value = prefix + value;
        }
        if (k == "set-cookie") value = std::regex_replace(value, std::regex(R"(;\s*Path=/[^;]*)", std::regex::icase), "; Path=" + prefix + "/");
        h << k << ": " << value << "\r\n";
    }
    h << "Content-Length: " << body.size() << "\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\n" << kSandboxCsp << "\r\n";
    send_all(fd, h.str());
    if (request.method != "HEAD") send_all(fd, body);
    return true;
}

// ---------- router ----------
#include "workspace_intel.inc"
#include "workspace_agent.inc"
#include "astro_dev.inc"

bool handle_ide_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path.rfind("/api/ide/", 0) != 0) return false;
    if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
    ide_janitor();
    const auto body_opt = parse_request_json(request, fd);
    if (!body_opt) return true;
    const Json& body = *body_opt;
    const std::string action = path.substr(9);

    // Terminal calls that address an existing session by id.
    if (action == "term/stream" || action == "term/input" || action == "term/resize" || action == "term/close") {
        const auto t = term_get(get_string(body, "id"));
        if (!t) { send_json(fd, 404, error_json("That terminal has closed.")); return true; }
        t->touched = now_s();
        if (action == "term/stream") { stream_buffer(fd, t->out, static_cast<std::size_t>(std::max(0, json_int(body, "from", 0))), &t->touched); return true; }
        if (action == "term/input") {
            const std::string data = get_string(body, "data");
            if (data.size() > 100'000) { send_json(fd, 413, error_json("Too much input at once.")); return true; }
            term_write(t, data);
            Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return true;
        }
        if (action == "term/resize") {
            term_resize(t, std::clamp(json_int(body, "cols", 80), 20, 400), std::clamp(json_int(body, "rows", 24), 5, 200));
            Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return true;
        }
        term_kill(t);
        Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return true;
    }

    const std::string project = get_string(body, "project");
    if (!valid_project_name(project)) { send_json(fd, 400, error_json("Invalid project name.")); return true; }
    const fs::path base = fs::path(projects_root()) / project;
    std::error_code ec;
    if (!fs::is_directory(base, ec)) { send_json(fd, 404, error_json("Project not found.")); return true; }

    if (action == "search") { ide_search(fd, base, body); return true; }
    if (action == "zip") { ide_zip(fd, project, base); return true; }
    if (action == "index") { ide_index(fd, base); return true; }
    if (action == "git") { ide_git(fd, base, body); return true; }
    if (action == "exec") { agent_exec(fd, base, body); return true; }
    if (action == "proc/tail") { agent_proc_tail(fd, project, body); return true; }
    if (action.rfind("agent/", 0) == 0) { agent_sessions(fd, action, project, body); return true; }
    if (action.rfind("history/", 0) == 0) { ide_history(fd, action.substr(8), project, base, body); return true; }
    if (action == "term/open") {
        std::string error;
        const auto t = term_open(project, base, json_int(body, "cols", 100), json_int(body, "rows", 24), error);
        if (!t) { send_json(fd, 429, error_json(error)); return true; }
        Json::Object o; o["id"] = Json(t->id); send_json(fd, 200, Json(std::move(o))); return true;
    }
    if (action == "proc/start") {
        std::string command = get_string(body, "cmd");
        const std::string file = get_string(body, "file");
        if (command.empty() && !file.empty()) {
            if (!project_path(project, file)) { send_json(fd, 400, error_json("Invalid file.")); return true; }
            std::string error;
            command = run_command_for(file, error);
            if (command.empty()) { send_json(fd, 422, error_json(error)); return true; }
        }
        if (command.empty() || command.size() > 4000) { send_json(fd, 400, error_json("Nothing to start.")); return true; }
        std::string error;
        const auto p = proc_start(project, base, command, error);
        if (!p) { send_json(fd, 429, error_json(error)); return true; }
        send_json(fd, 200, proc_status_json(p)); return true;
    }
    if (action == "proc/status") { auto p = proc_get(project); if (p) p->touched = now_s(); send_json(fd, 200, proc_status_json(p)); return true; }
    if (action == "proc/stop") { proc_stop(project); Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return true; }
    if (action == "proc/logs") {
        const auto p = proc_get(project);
        if (!p) { send_json(fd, 404, error_json("No app server is running for this project.")); return true; }
        stream_buffer(fd, p->out, static_cast<std::size_t>(std::max(0, json_int(body, "from", 0))), &p->touched);
        return true;
    }
    send_json(fd, 404, error_json("Unknown workspace action."));
    return true;
}
