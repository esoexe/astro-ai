// Agent mode backend: a shell tool for the agent, the app-server log tail, and saved agent sessions.
// Included from workspace_ide.inc (after workspace_intel.inc, which provides run_capture).

// Replace invalid UTF-8 (cut sequences, binary output) with '?', so JSON stays valid for the browser.
std::string utf8_clean(const std::string& s) {
    std::string out; out.reserve(s.size());
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = p[i];
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = len > 0 && i + len <= n;
        for (std::size_t k = 1; ok && k < len; ++k) ok = (p[i + k] & 0xC0) == 0x80;
        if (ok && len == 1 && c < 0x20 && c != '\n' && c != '\t') { out += '?'; ++i; continue; }
        if (ok) { out.append(s, i, len); i += len; } else { out += '?'; ++i; }
    }
    return out;
}

// Keep the first `head` and last `tail` bytes of long output.
std::string clip_middle(const std::string& s, std::size_t head, std::size_t tail, bool& clipped) {
    clipped = s.size() > head + tail + 200;
    if (!clipped) return s;
    return s.substr(0, head) + "\n\n… [" + std::to_string(s.size() - head - tail) + " bytes of output trimmed] …\n\n" + s.substr(s.size() - tail);
}

// Remove ANSI colour/cursor escape sequences (npm, compilers and test runners print them).
std::string strip_ansi(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            std::size_t j = i + 2;
            while (j < s.size() && !(s[j] >= '@' && s[j] <= '~')) ++j;
            i = j; continue;
        }
        if (s[i] == '\r') { if (i + 1 < s.size() && s[i + 1] == '\n') continue; out += '\n'; continue; }
        out += s[i];
    }
    return out;
}

bool valid_agent_id(const std::string& id) {
    if (id.empty() || id.size() > 48) return false;
    for (unsigned char c : id) if (!(std::isalnum(c) || c == '-' || c == '_')) return false;
    return true;
}

fs::path agent_dir(const std::string& project) { return fs::path(data_dir()) / ".astro_agent" / project; }

// POST /api/ide/exec {project, command, timeout?, cwd?} -> {code, out, ms, timed_out, truncated}
void agent_exec(int fd, const fs::path& base, const Json& body) {
    const std::string command = get_string(body, "command");
    if (trim(command).empty()) { send_json(fd, 400, error_json("Give a command to run.")); return; }
    if (command.size() > 20'000) { send_json(fd, 413, error_json("That command is too long.")); return; }
    const int timeout = std::clamp(json_int(body, "timeout", 120), 1, 900);
    fs::path cwd = base;
    const std::string rel = get_string(body, "cwd");
    if (!rel.empty() && rel != ".") {
        if (rel.find("..") != std::string::npos || rel[0] == '/') { send_json(fd, 400, error_json("cwd must be a folder inside the project.")); return; }
        cwd = base / rel;
        std::error_code ec;
        if (!fs::is_directory(cwd, ec)) { send_json(fd, 404, error_json("No folder " + rel + " in this project.")); return; }
    }
    const auto t0 = std::chrono::steady_clock::now();
    // exec 2>&1 merges stderr into stdout, so the output keeps its real order.
    const Captured r = run_capture({"/bin/bash", "-c", "exec 2>&1\n" + command}, cwd, {"CI=1", "NO_COLOR=1", "FORCE_COLOR=0", "npm_config_yes=true", "PIP_NO_INPUT=1", "DEBIAN_FRONTEND=noninteractive"}, timeout, 6'000'000);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    bool clipped = false;
    const std::string out = utf8_clean(clip_middle(strip_ansi(r.out + r.err), 8'000, 32'000, clipped));
    Json::Object o;
    o["code"] = Json(static_cast<double>(r.code)); o["out"] = Json(out); o["ms"] = Json(static_cast<double>(ms));
    o["timed_out"] = Json(r.timed_out); o["truncated"] = Json(clipped); o["timeout"] = Json(static_cast<double>(timeout));
    send_json(fd, 200, Json(std::move(o)));
}

// POST /api/ide/proc/tail {project, bytes?} -> proc status + the last bytes of its log (no streaming).
void agent_proc_tail(int fd, const std::string& project, const Json& body) {
    const auto p = proc_get(project);
    Json st = proc_status_json(p);
    Json::Object o = std::get<Json::Object>(st.value);
    std::string data;
    if (p) {
        p->touched = now_s();
        const std::size_t want = static_cast<std::size_t>(std::clamp(json_int(body, "bytes", 12'000), 500, 200'000));
        std::lock_guard<std::mutex> lock(p->out.m);
        data = p->out.data.size() > want ? p->out.data.substr(p->out.data.size() - want) : p->out.data;
    }
    o["log"] = Json(utf8_clean(strip_ansi(data)));
    send_json(fd, 200, Json(std::move(o)));
}

// Saved agent sessions: <data>/.astro_agent/<project>/<id>.json (+ <id>.meta.json for the list), next to the
// checkpoints and outside the project folder, so they never show up as project files or changes.
void agent_sessions(int fd, const std::string& action, const std::string& project, const Json& body) {
    std::error_code ec;
    const fs::path dir = agent_dir(project);
    if (action == "agent/list") {
        std::vector<std::pair<double, Json>> metas;
        if (fs::is_directory(dir, ec))
            for (const auto& e : fs::directory_iterator(dir, ec)) {
                const std::string name = e.path().filename().string();
                if (!name.ends_with(".meta.json")) continue;
                const auto text = read_small(e.path(), 64'000);
                if (!text) continue;
                try {
                    Json m = JsonParser(*text).parse();
                    double updated = 0;
                    if (const auto* obj = std::get_if<Json::Object>(&m.value)) {
                        auto it = obj->find("updated");
                        if (it != obj->end()) if (const auto* d = std::get_if<double>(&it->second.value)) updated = *d;
                    }
                    metas.emplace_back(updated, std::move(m));
                } catch (...) { /* skip a broken file */ }
            }
        std::sort(metas.begin(), metas.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        Json::Array list;
        for (std::size_t i = 0; i < metas.size() && i < 200; ++i) list.push_back(std::move(metas[i].second));
        Json::Object o; o["sessions"] = Json(std::move(list)); send_json(fd, 200, Json(std::move(o))); return;
    }
    const std::string id = get_string(body, "id");
    if (!valid_agent_id(id)) { send_json(fd, 400, error_json("Invalid session id.")); return; }
    const fs::path data_file = dir / (id + ".json");
    const fs::path meta_file = dir / (id + ".meta.json");
    if (action == "agent/save") {
        const std::string data = get_string(body, "data");
        if (data.empty() || data.size() > 6'000'000) { send_json(fd, 413, error_json("The session is too large to save.")); return; }
        const Json* meta = body.get("meta");
        if (!meta || !std::get_if<Json::Object>(&meta->value)) { send_json(fd, 400, error_json("Missing session meta.")); return; }
        const std::string meta_text = dump_json(*meta);
        if (meta_text.size() > 60'000) { send_json(fd, 413, error_json("Session meta too large.")); return; }
        fs::create_directories(dir, ec);
        if (!write_file_private(data_file.string(), data) || !write_file_private(meta_file.string(), meta_text)) { send_json(fd, 500, error_json("Could not save the session.")); return; }
        // Keep the newest 100 sessions.
        std::vector<std::pair<fs::file_time_type, fs::path>> all;
        for (const auto& e : fs::directory_iterator(dir, ec)) if (e.path().filename().string().ends_with(".meta.json")) all.emplace_back(fs::last_write_time(e.path(), ec), e.path());
        if (all.size() > 100) {
            std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (std::size_t i = 100; i < all.size(); ++i) {
                std::string n = all[i].second.filename().string(); n = n.substr(0, n.size() - 10);
                fs::remove(all[i].second, ec); fs::remove(dir / (n + ".json"), ec);
            }
        }
        Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "agent/load") {
        const auto text = read_small(data_file, 6'500'000);
        if (!text) { send_json(fd, 404, error_json("That session no longer exists.")); return; }
        Json::Object o; o["data"] = Json(*text); send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "agent/delete") {
        fs::remove(data_file, ec); fs::remove(meta_file, ec);
        Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return;
    }
    send_json(fd, 404, error_json("Unknown agent action."));
}
