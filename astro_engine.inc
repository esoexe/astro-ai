// ---------- Astro agent engine ----------
// The agent loop of the Agent page, run on the server, so it works without a browser:
//   - the terminal app (`astro`), print mode for scripts, background agents in the web app
//   - parallel agents, each in an isolated copy of the project (a git worktree, or a plain copy), merged back on request
// Same behaviour as web/app.js agRun + web/dev.js: tools in ```tool blocks, verify-before-done guard, checkpoints
// for undo, Plan / Ask / Auto permission modes, project rules, skills, subagents and MCP tools.
// Endpoints: POST /api/agent/{start,send,events,list,get,stop,answer,diff,changes,merge,discard,undo,models,delete}

void handle_api(int fd, const HttpRequest& request);   // workspace_server.cpp (the normal router)

namespace eng {

// ---------- small helpers ----------
std::string clip(const std::string& t, std::size_t n) {
    if (t.size() <= n) return t;
    std::size_t cut = n;
    while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) --cut;   // never split a UTF-8 character
    return t.substr(0, cut) + "\xE2\x80\xA6";
}
std::string head_tail(const std::string& t, std::size_t head, std::size_t tail) {
    if (t.size() <= head + tail + 80) return t;
    std::size_t a = head, b = t.size() - tail;
    while (a > 0 && (static_cast<unsigned char>(t[a]) & 0xC0) == 0x80) --a;
    while (b < t.size() && (static_cast<unsigned char>(t[b]) & 0xC0) == 0x80) ++b;
    return t.substr(0, a) + "\n\xE2\x80\xA6 [" + std::to_string(b - a) + " characters trimmed] \xE2\x80\xA6\n" + t.substr(b);
}
std::string one_line(std::string s) {
    for (char& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    std::string out; bool sp = false;
    for (char c : s) { if (c == ' ') { if (!sp) out += c; sp = true; } else { out += c; sp = false; } }
    return trim(out);
}
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
Json num(double v) { return Json(v); }
Json num(long long v) { return Json(static_cast<double>(v)); }
double jnum(const Json* v, double fallback = 0) {
    if (!v) return fallback;
    if (const double* d = std::get_if<double>(&v->value)) return *d;
    if (const auto* s = v->string_ptr()) { try { return std::stod(*s); } catch (...) {} }
    return fallback;
}
bool jtrue(const Json* v) { if (!v) return false; if (const bool* b = std::get_if<bool>(&v->value)) return *b; return false; }
// A string field, accepting numbers too (models sometimes send "timeout": "60").
std::string sfield(const Json::Object& o, const char* key) {
    auto it = o.find(key);
    if (it == o.end()) return {};
    if (const auto* s = it->second.string_ptr()) return *s;
    if (const double* d = std::get_if<double>(&it->second.value)) { std::ostringstream os; os << *d; return os.str(); }
    if (const bool* b = std::get_if<bool>(&it->second.value)) return *b ? "true" : "false";
    return {};
}
std::string first_of(const Json::Object& o, std::initializer_list<const char*> keys) {
    for (const char* k : keys) { std::string v = sfield(o, k); if (!v.empty()) return v; }
    return {};
}
std::string clean_path(std::string p) {
    p = trim(p);
    while (!p.empty() && (p.front() == '"' || p.front() == '\'' || p.front() == '`')) p.erase(0, 1);
    while (!p.empty() && (p.back() == '"' || p.back() == '\'' || p.back() == '`')) p.pop_back();
    if (p.rfind("./", 0) == 0) p.erase(0, 2);
    while (!p.empty() && p.front() == '/') p.erase(0, 1);
    std::string out; for (char c : p) { if (c == '/' && !out.empty() && out.back() == '/') continue; out += c; }
    return out;
}
bool ok_path(const std::string& p) {
    if (p.empty() || p.size() >= 300 || p.find('\0') != std::string::npos || p.find('\\') != std::string::npos) return false;
    std::stringstream ss(p); std::string seg;
    while (std::getline(ss, seg, '/')) if (seg.empty() || seg == "..") return false;
    return p.back() != '/';
}
std::string fmt_bytes(double b) {
    std::ostringstream os;
    if (b < 1024) os << static_cast<long long>(b) << " B";
    else if (b < 1024 * 1024) os << std::fixed << std::setprecision(1) << b / 1024 << " KB";
    else os << std::fixed << std::setprecision(1) << b / 1024 / 1024 << " MB";
    return os.str();
}
std::string today_iso() {
    const std::time_t t = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16]; std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm); return buf;
}
std::string read_all(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }
bool write_all(const fs::path& p, const std::string& data) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc); out << data; return static_cast<bool>(out);
}

// ---------- in-process API calls (the same handlers the browser uses) ----------
struct CallResult {
    int status = 0; std::string body; Json json;
    bool ok() const { return status >= 200 && status < 300; }
    std::string error() const {
        const std::string e = get_string(json, "error");
        return !e.empty() ? e : status == 0 ? "no response" : "HTTP " + std::to_string(status);
    }
};
// on_body receives the response body as it arrives (for SSE). cancel and timeout_ms end the wait early.
CallResult call(const std::string& path, const Json& body, const std::function<void(const std::string&)>& on_body = {},
                const std::atomic<bool>* cancel = nullptr, int timeout_ms = 0) {
    CallResult res;
    int sv[2];
    if (astro_socketpair(sv) != 0) { res.body = "socketpair failed"; return res; }
    HttpRequest req; req.method = "POST"; req.path = path; req.body = dump_json(body);
    req.headers["x-astro-client"] = "1"; req.headers["content-type"] = "application/json";
    const int wr = sv[1];
    std::thread([req, wr]() {
        t_internal_call = true;
        try { handle_api(wr, req); } catch (...) { /* the reader sees a closed socket */ }
        t_internal_call = false;
        shutdown(wr, SHUT_RDWR);
        sock_close(wr);
    }).detach();
    const int rd = sv[0];
    std::string buf; bool headers = false;
    const auto start = std::chrono::steady_clock::now();
    char chunk[16384];
    while (true) {
        if (cancel && cancel->load()) break;
        if (timeout_ms > 0 && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms)) break;
        pollfd p{static_cast<decltype(pollfd::fd)>(rd), POLLIN, 0};
        const int pr = poll(&p, 1, 300);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) continue;
        const ssize_t n = recv(rd, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        buf.append(chunk, static_cast<std::size_t>(n));
        if (!headers) {
            const auto e = buf.find("\r\n\r\n");
            if (e == std::string::npos) { if (buf.size() > 65536) break; continue; }
            headers = true;
            const auto sp = buf.find(' ');
            if (sp != std::string::npos) res.status = std::atoi(buf.c_str() + sp + 1);
            buf.erase(0, e + 4);
        }
        if (on_body && !buf.empty()) { on_body(buf); buf.clear(); }
    }
    shutdown(rd, SHUT_RDWR);
    sock_close(rd);
    if (!on_body) {
        res.body = std::move(buf);
        try { res.json = JsonParser(res.body).parse(); } catch (...) { /* not JSON */ }
    }
    return res;
}
Json obj(std::initializer_list<std::pair<const std::string, Json>> items) { return Json(Json::Object(items)); }

// ---------- models ----------
const std::vector<std::regex>& code_rank() {
    static const std::vector<std::regex> r = [] {
        std::vector<std::regex> v;
        for (const char* p : {"deepseek-v4[^ ]*pro", "qwen3-coder-plus", "glm-5\\.3(?!-flash)", "deepseek-v4", "qwen3\\.8-max", "kimi-k2",
                              "devstral", "minimax-m3(?!\\.1-flash)", "qwen3\\.5-397b", "codestral", "nemotron-3-ultra", "qwen3-coder",
                              "gpt-oss-120b", "glm-5\\.3-flash", "minimax-m3", "qwen3\\.8", "llama-3[._]3-70b"})
            v.emplace_back(p, std::regex::icase | std::regex::ECMAScript);
        return v;
    }();
    return r;
}
// The best free coding models first (the Agent page's CODE_RANK), then the live health order.
std::vector<Route> candidates(const std::string& preferred) {
    std::vector<Route> out;
    if (!preferred.empty() && preferred != "auto") {
        for (const auto& r : g_routes) if (r.id == preferred) { out.push_back(r); break; }
    }
    std::vector<std::pair<int, Route>> ranked;
    int order = 0;
    for (const auto& r : g_routes) {
        if (r.local || (r.paid && !r.in_auto) || route_blocked(r)) continue;
        if (!out.empty() && r.id == out.front().id) continue;
        const std::string s = r.id + " " + r.name;
        int score = 999;
        const auto& rank = code_rank();
        for (std::size_t i = 0; i < rank.size(); ++i) if (std::regex_search(s, rank[i])) { score = static_cast<int>(i) * 10; break; }
        if (score == 999 && !r.in_auto) score = 2000;   // models outside Auto only after every Auto model
        ranked.emplace_back(score * 1000 + order++, r);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Route> rest;
    for (auto& [_, r] : ranked) rest.push_back(r);
    model_stats_reorder(rest, 0);
    for (auto& r : rest) { if (out.size() >= 6) break; out.push_back(r); }
    return out;
}

struct Turn { std::string text, model, error; bool ok = false; };
std::string strip_reasoning(std::string t) {
    for (std::size_t a; (a = t.find("<think>")) != std::string::npos;) {
        const std::size_t b = t.find("</think>", a);
        if (b == std::string::npos) { t.erase(a); break; }
        t.erase(a, b + 8 - a);
    }
    if (const auto b = t.find("</think>"); b != std::string::npos) t.erase(0, b + 8);
    return t;
}
// One streamed model turn through /api/chat/stream (server-side failover included).
Turn model_turn(const std::vector<std::pair<std::string, std::string>>& messages, const std::string& model, bool agentloop, int max_tokens,
                const std::atomic<bool>* cancel, const std::function<void(const std::string&)>& on_text = {}, bool strict = false) {
    Turn t;
    Json::Array msgs;
    for (const auto& [role, content] : messages) msgs.push_back(obj({{"role", Json(role)}, {"content", Json(content)}}));
    Json::Object b; b["messages"] = Json(std::move(msgs)); b["mode"] = Json("coding"); b["agentloop"] = Json(agentloop);
    b["max_tokens"] = num(static_cast<double>(max_tokens)); b["model"] = Json(model.empty() ? std::string("auto") : model);
    if (strict) b["strict"] = Json(true);
    std::string pending;
    auto last_emit = std::chrono::steady_clock::now();
    const auto r = call("/api/chat/stream", Json(std::move(b)), [&](const std::string& chunk) {
        pending += chunk;
        for (std::size_t e; (e = pending.find("\n\n")) != std::string::npos;) {
            std::string ev = pending.substr(0, e); pending.erase(0, e + 2);
            if (ev.rfind("data: ", 0) != 0) continue;
            try {
                const Json j = JsonParser(ev.substr(6)).parse();
                if (const Json* d = j.get("delta")) t.text += d->string_or();
                if (jtrue(j.get("done"))) { t.ok = true; t.model = get_string(j, "model"); }
                if (const Json* er = j.get("error")) t.error = er->string_or("error");
            } catch (...) { /* keep-alive */ }
        }
        if (on_text && std::chrono::steady_clock::now() - last_emit > std::chrono::milliseconds(250)) { last_emit = std::chrono::steady_clock::now(); on_text(t.text); }
    }, cancel);
    if (r.status && r.status != 200 && t.error.empty()) t.error = "HTTP " + std::to_string(r.status);
    if (!t.ok && t.error.empty()) t.error = cancel && cancel->load() ? "stopped" : "the answer was interrupted";
    t.text = strip_reasoning(t.text);
    // trailing confidence tag some prompts add
    if (const auto c = t.text.rfind("[[confidence"); c != std::string::npos && t.text.size() - c < 80) t.text.erase(c);
    return t;
}

// ---------- parsing the model's reply (port of agParse / agJson / agNorm) ----------
std::optional<Json> try_parse(const std::string& s) { try { return JsonParser(s).parse(); } catch (...) { return std::nullopt; } }
std::optional<Json> loose_json(std::string src) {
    src = trim(src);
    if (lower(src.substr(0, 5)) == "json\n") src.erase(0, 5);
    if (auto j = try_parse(src)) return j;
    std::string out; bool in = false, esc = false;
    for (char ch : src) {
        if (in) {
            if (esc) { out += ch; esc = false; continue; }
            if (ch == '\\') { out += ch; esc = true; continue; }
            if (ch == '"') { in = false; out += ch; continue; }
            if (ch == '\n') { out += "\\n"; continue; }
            if (ch == '\r') continue;
            if (ch == '\t') { out += "\\t"; continue; }
            out += ch;
        } else { if (ch == '"') in = true; out += ch; }
    }
    // trailing commas
    std::string fixed; in = false; esc = false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char ch = out[i];
        if (in) { fixed += ch; if (esc) esc = false; else if (ch == '\\') esc = true; else if (ch == '"') in = false; continue; }
        if (ch == '"') { in = true; fixed += ch; continue; }
        if (ch == ',') { std::size_t j = i + 1; while (j < out.size() && std::isspace(static_cast<unsigned char>(out[j]))) ++j; if (j < out.size() && (out[j] == '}' || out[j] == ']')) continue; }
        fixed += ch;
    }
    if (auto j = try_parse(fixed)) return j;
    const auto a = fixed.find('{'), b = fixed.rfind('}');
    if (a != std::string::npos && b != std::string::npos && b > a && (a > 0 || b + 1 < fixed.size())) if (auto j = try_parse(fixed.substr(a, b - a + 1))) return j;
    return std::nullopt;
}
// Balanced {...} objects in a string (string-aware).
std::vector<std::pair<std::size_t, std::size_t>> object_spans(const std::string& src) {
    std::vector<std::pair<std::size_t, std::size_t>> parts; int depth = 0; std::size_t start = 0; bool in = false, esc = false;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char ch = src[i];
        if (in) { if (esc) esc = false; else if (ch == '\\') esc = true; else if (ch == '"') in = false; continue; }
        if (ch == '"') in = true;
        else if (ch == '{') { if (depth == 0) start = i; ++depth; }
        else if (ch == '}' && depth > 0) { if (--depth == 0) parts.emplace_back(start, i + 1); }
    }
    return parts;
}
std::vector<Json> json_all(const std::string& body) {
    std::string src = trim(body);
    if (lower(src.substr(0, 5)) == "json\n") src.erase(0, 5);
    if (auto one = loose_json(src)) {
        if (const auto* arr = one->array_ptr()) { std::vector<Json> v; for (const auto& x : *arr) if (x.is_object()) v.push_back(x); return v; }
        if (one->is_object()) return {*one};
    }
    const auto spans = object_spans(src);
    if (spans.size() < 2) return {};
    std::vector<Json> v;
    for (const auto& [a, b] : spans) { auto j = loose_json(src.substr(a, b - a)); if (!j || !j->is_object()) return {}; v.push_back(*j); }
    return v;
}
const std::map<std::string, std::string>& aliases() {
    static const std::map<std::string, std::string> m = {
        {"shell", "bash"}, {"run", "bash"}, {"exec", "bash"}, {"terminal", "bash"}, {"command", "bash"}, {"sh", "bash"}, {"run_command", "bash"}, {"execute", "bash"},
        {"create_file", "write_file"}, {"write", "write_file"}, {"save_file", "write_file"}, {"read", "read_file"}, {"cat", "read_file"}, {"open_file", "read_file"},
        {"ls", "list_files"}, {"list", "list_files"}, {"list_dir", "list_files"}, {"grep", "search"}, {"search_code", "search"}, {"search_files", "search"},
        {"replace", "edit_file"}, {"str_replace", "edit_file"}, {"edit", "edit_file"}, {"patch", "edit_file"}, {"remove_file", "delete_file"}, {"rm", "delete_file"},
        {"read_url", "fetch_page"}, {"open_url", "fetch_page"}, {"fetch", "fetch_page"}, {"browse", "fetch_page"}, {"search_web", "web_search"}, {"google", "web_search"},
        {"start_process", "start_server"}, {"serve", "start_server"}, {"stop_process", "stop_server"}, {"logs", "server_logs"}, {"process_output", "server_logs"},
        {"ask", "ask_user"}, {"question", "ask_user"}, {"memory", "remember"}, {"show", "present"}, {"present_file", "present"}, {"open", "present"},
        {"find_files", "glob"}, {"lint", "diagnostics"}, {"problems", "diagnostics"}, {"check", "diagnostics"}, {"todowrite", "todo"}, {"todo_write", "todo"}, {"todos", "todo"},
        {"use_skill", "skill"}, {"load_skill", "skill"}, {"mcp_call", "mcp"}, {"call_mcp", "mcp"}, {"subagent", "task"}, {"agent", "task"}, {"delegate", "task"},
    };
    return m;
}
Json::Object norm_call(const Json& c) {
    Json::Object o = c.object_ptr() ? *c.object_ptr() : Json::Object{};
    std::string t = lower(trim(first_of(o, {"tool", "name", "action"})));
    std::string tt; bool sep = false;
    for (char ch : t) { if (ch == ' ' || ch == '-' || ch == '\t') { if (!sep) tt += '_'; sep = true; } else { tt += ch; sep = false; } }
    t = tt;
    for (const char* k : {"arguments", "args"}) {
        auto it = o.find(k);
        if (it != o.end() && it->second.is_object()) { const Json::Object inner = *it->second.object_ptr(); for (const auto& [ik, iv] : inner) o[ik] = iv; }
    }
    if (auto a = aliases().find(t); a != aliases().end()) t = a->second;
    o["tool"] = Json(t);
    auto set = [&](const char* key, std::initializer_list<const char*> keys) { o[key] = Json(first_of(o, keys)); };
    if (t == "bash") set("command", {"command", "cmd", "script", "code"});
    if (t == "read_file" || t == "write_file" || t == "edit_file" || t == "delete_file" || t == "present" || t == "list_files")
        o["path"] = Json(clean_path(first_of(o, {"path", "file", "filename", "file_path", "target"})));
    if (t == "write_file") set("content", {"content", "text", "data", "code"});
    if (t == "edit_file") { set("old_text", {"old_text", "old_str", "old", "search", "find"}); set("new_text", {"new_text", "new_str", "new", "replace", "replacement"}); }
    if (t == "search" || t == "web_search") set("query", {"query", "q", "pattern", "text"});
    if (t == "fetch_page") set("url", {"url", "link", "href"});
    if (t == "start_server") set("command", {"command", "cmd"});
    if (t == "ask_user") set("question", {"question", "text", "prompt"});
    if (t == "remember") set("fact", {"fact", "text", "memory"});
    return o;
}
// edit_file's "new_text" may legitimately be empty, and write_file's content may contain only whitespace: keep raw values.
std::string raw_field(const Json::Object& o, const char* key) { auto it = o.find(key); return it != o.end() ? it->second.string_or() : std::string(); }

struct Parsed { std::vector<Json::Object> calls; std::optional<Json> done; std::string text; };
Parsed parse_reply(const std::string& raw) {
    Parsed P;
    const std::string text = strip_reasoning(raw);
    std::string narrative;
    std::size_t pos = 0, last = 0;
    static const std::regex path_re(R"((?:path|file|filename)\s*[=:]\s*["']?([^\s"']+))", std::regex::icase);
    while (true) {
        const std::size_t p = text.find("```", pos);
        if (p == std::string::npos) break;
        const std::size_t nl = text.find('\n', p + 3);
        if (nl == std::string::npos) break;
        const std::string info_raw = text.substr(p + 3, nl - p - 3);
        if (info_raw.find('`') != std::string::npos) { pos = p + 3; continue; }
        const std::size_t close = text.find("```", nl + 1);
        const bool closed = close != std::string::npos;
        const std::string body = text.substr(nl + 1, (closed ? close : text.size()) - nl - 1);
        narrative += text.substr(last, p - last);
        last = closed ? close + 3 : text.size();
        const std::string info = trim(info_raw), low = lower(info);
        if (low == "tool" || low == "tool_call" || low == "json tool" || low.rfind("tool ", 0) == 0) {
            auto all = json_all(body);
            std::vector<Json::Object> got;
            for (const auto& c : all) if (c.get("tool") || c.get("name")) got.push_back(norm_call(c));
            if (!got.empty()) for (auto& g : got) P.calls.push_back(std::move(g));
            else P.calls.push_back(Json::Object{{"tool", Json("_bad")}, {"raw", Json(clip(body, 600))}, {"cut", Json(!closed)}});
        } else if (low == "done" || low.rfind("done ", 0) == 0) {
            auto j = loose_json(body);
            if (j && j->is_object()) P.done = *j; else P.done = obj({{"summary", Json(clip(trim(body), 300))}});
        } else {
            std::smatch m;
            if (std::regex_search(info, m, path_re)) {
                const std::string path = clean_path(m[1].str());
                if (closed) P.calls.push_back(Json::Object{{"tool", Json("write_file")}, {"path", Json(path)}, {"content", Json(body)}});
                else P.calls.push_back(Json::Object{{"tool", Json("_bad")}, {"raw", Json("file block for " + path)}, {"cut", Json(true)}});
            } else if (trim(body).empty()) {
                // an empty fence (models sometimes open a block and put the call elsewhere): not worth showing
            } else if ((low == "json" || low.empty() || ((low == "bash" || low == "sh" || low == "shell" || low == "jsonc") && trim(body).rfind("{", 0) == 0)) && std::regex_search(body, std::regex("\"tool\"\\s*:"))) {
                auto j = loose_json(body);
                if (j && j->get("tool")) P.calls.push_back(norm_call(*j));
                else P.calls.push_back(Json::Object{{"tool", Json("_bad")}, {"raw", Json(clip(body, 600))}, {"cut", Json(!closed)}});
            } else narrative += text.substr(p, last - p);
        }
        if (!closed) break;
        pos = last;
    }
    if (last < text.size()) narrative += text.substr(last);
    if (P.calls.empty() && !P.done) {
        // a bare {"tool": ...} object outside a block
        for (const auto& [a, b] : object_spans(text)) {
            const std::string s = text.substr(a, b - a);
            static const std::regex tool_head("^\\{\\s*\"tool\"\\s*:\\s*\"[a-z_]+\"");
            if (!std::regex_search(s, tool_head)) continue;
            auto j = loose_json(s);
            if (j && j->get("tool")) {
                P.calls.push_back(norm_call(*j));
                if (const auto at = narrative.find(s); at != std::string::npos) narrative.erase(at, s.size());
                break;
            }
        }
    }
    std::string squeezed; int nls = 0;
    for (char c : narrative) { if (c == '\n') { if (++nls > 2) continue; } else nls = 0; squeezed += c; }
    P.text = trim(squeezed);
    return P;
}
std::string call_short(const Json::Object& c) {
    const std::string t = sfield(c, "tool");
    if (t == "bash") { std::string cmd = sfield(c, "command"); cmd = cmd.substr(0, cmd.find('\n')); return "bash `" + clip(cmd, 80) + "`"; }
    if (t == "write_file" || t == "edit_file" || t == "read_file" || t == "delete_file" || t == "present") return t + " " + sfield(c, "path");
    if (t == "search" || t == "web_search") return t + " \"" + clip(sfield(c, "query"), 60) + "\"";
    if (t == "fetch_page") return "fetch " + clip(sfield(c, "url"), 80);
    if (t == "start_server") return "start_server `" + clip(sfield(c, "command"), 60) + "`";
    if (t == "glob") return "glob " + first_of(c, {"pattern", "glob", "query", "path"});
    if (t == "mcp") return "mcp " + sfield(c, "server") + " \xC2\xB7 " + sfield(c, "name");
    if (t == "skill") return "skill " + first_of(c, {"name", "skill"});
    if (t == "task") return "task" + (sfield(c, "agent").empty() ? std::string() : " " + sfield(c, "agent"));
    return t;
}

// ---------- custom tools: <project>/.astro/tools/*.json ----------
// {"name":"run_tests","description":"...","command":"npm test -- {{file}}","args":{"file":"test file (optional)"},"readonly":false,"timeout":300}
// The agent calls them like built-in tools ({"tool":"run_tests","file":"a.test.js"}); arguments are shell-quoted into the command.
struct CustomTool { std::string name, description, command; std::vector<std::pair<std::string, std::string>> args; bool readonly = false; int timeout = 120; };
bool builtin_tool(const std::string& n) {
    static const std::set<std::string> b = {"bash", "read_file", "write_file", "edit_file", "delete_file", "list_files", "glob", "search", "web_search", "fetch_page",
        "start_server", "server_logs", "stop_server", "ask_user", "remember", "present", "diagnostics", "todo", "skill", "mcp", "task"};
    return b.count(n) > 0 || aliases().count(n) > 0;
}
std::vector<CustomTool> custom_tools(const std::string& project) {
    std::vector<CustomTool> out;
    std::error_code ec;
    const fs::path dir = fs::path(projects_root()) / project / ".astro" / "tools";
    if (!fs::is_directory(dir, ec)) return out;
    static const std::regex name_re("^[a-z][a-z0-9_]{0,39}$");
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (out.size() >= 30) break;
        if (e.path().extension() != ".json") continue;
        std::error_code e2;
        if (fs::file_size(e.path(), e2) > 64 * 1024) continue;
        std::ifstream in(e.path(), std::ios::binary); std::stringstream ss; ss << in.rdbuf();
        Json j; try { j = JsonParser(ss.str()).parse(); } catch (...) { continue; }
        CustomTool t;
        t.name = lower(trim(get_string(j, "name", e.path().stem().string())));
        t.command = get_string(j, "command");
        if (!std::regex_match(t.name, name_re) || builtin_tool(t.name) || trim(t.command).empty()) continue;
        t.description = clip(get_string(j, "description"), 300);
        t.readonly = json_bool(j, "readonly", false);
        t.timeout = std::clamp(json_int(j, "timeout", 120), 1, 1800);
        if (const Json* as = j.get("args"); as && as->object_ptr()) for (const auto& [k, v] : *as->object_ptr()) if (std::regex_match(k, name_re)) t.args.push_back({k, v.is_string() ? clip(*v.string_ptr(), 160) : std::string()});
        out.push_back(std::move(t));
    }
    std::sort(out.begin(), out.end(), [](const CustomTool& x, const CustomTool& y) { return x.name < y.name; });
    return out;
}
std::optional<CustomTool> find_custom(const std::string& project, const std::string& name) {
    if (name.empty() || builtin_tool(name)) return std::nullopt;
    for (auto& t : custom_tools(project)) if (t.name == name) return t;
    return std::nullopt;
}
std::string shell_quote(const std::string& v) { std::string o = "'"; for (char ch : v) { if (ch == '\'') o += "'\\''"; else o += ch; } return o + "'"; }
std::string custom_command(const CustomTool& t, const Json::Object& c) {
    std::string cmd = t.command;
    for (const auto& [k, d] : t.args) {
        const std::string val = sfield(c, k.c_str());
        const std::string key = "{{" + k + "}}";
        for (std::size_t p = cmd.find(key); p != std::string::npos; p = cmd.find(key, p)) { const std::string q = val.empty() ? std::string() : shell_quote(val); cmd.replace(p, key.size(), q); p += q.size(); }
    }
    return cmd;
}
thread_local std::string t_custom_project;   // the project of the call being checked (permission helpers have no session)

// ---------- permission modes (port of web/dev.js) ----------
bool readonly_bash(const std::string& cmd) {
    static const std::regex re(R"(^\s*(ls|cat|head|tail|wc|pwd|echo|find|grep|rg|tree|file|stat|du|which|type|env|printenv|node\s+(-v|--version)|python3?\s+(-V|--version)|git\s+(status|log|diff|show|branch|remote|blame|ls-files|rev-parse))\b[^;&|><`$]*$)");
    return std::regex_search(cmd, re);
}
bool cli_known(const std::string& name);
bool is_mutating(const Json::Object& c) {
    const std::string t = sfield(c, "tool");
    if (t == "task") return !sfield(c, "agent").empty() && cli_known(sfield(c, "agent"));
    if (t == "bash") return !readonly_bash(sfield(c, "command"));
    if (auto ct = find_custom(t_custom_project, t)) return !ct->readonly;
    static const std::set<std::string> m = {"write_file", "edit_file", "delete_file", "bash", "start_server", "stop_server", "remember", "mcp"};
    return m.count(t) > 0;
}
std::string approval_text(const Json::Object& c) {
    const std::string t = sfield(c, "tool");
    if (t == "bash") return "Run this command?";
    if (t == "write_file") return "Write " + sfield(c, "path") + "?";
    if (t == "edit_file") return "Edit " + sfield(c, "path") + "?";
    if (t == "delete_file") return "Delete " + sfield(c, "path") + "?";
    if (t == "start_server") return "Start this server?";
    if (t == "stop_server") return "Stop the app server?";
    if (t == "mcp") return "Call the MCP tool " + sfield(c, "server") + " \xC2\xB7 " + sfield(c, "name") + "?";
    if (t == "task") return "Let " + sfield(c, "agent") + " work on the project?";
    if (t == "remember") return "Save this to the project memory?";
    if (find_custom(t_custom_project, t)) return "Run the project tool " + t + "?";
    return "Allow " + t + "?";
}
std::string prefix_lines(const std::string& s, const std::string& pre) {
    std::string out = pre; for (char ch : s) { out += ch; if (ch == '\n') out += pre; } return out;
}
std::string approval_preview(const Json::Object& c) {
    const std::string t = sfield(c, "tool");
    if (t == "bash" || t == "start_server") return "$ " + sfield(c, "command");
    if (t == "write_file") return clip(raw_field(c, "content"), 3000);
    if (t == "edit_file") return clip(prefix_lines(raw_field(c, "old_text"), "- ") + "\n" + prefix_lines(raw_field(c, "new_text"), "+ "), 3000);
    if (t == "mcp") { auto it = c.find("arguments"); return it != c.end() ? clip(dump_json(it->second), 3000) : "{}"; }
    if (t == "task") return clip(sfield(c, "prompt"), 3000);
    if (t == "remember") return sfield(c, "fact");
    if (auto ct = find_custom(t_custom_project, t)) return "$ " + custom_command(*ct, c);
    return {};
}

// ---------- project scan: rules, skills, agents, installed coding CLIs ----------
struct Doc { std::string scope, path, name, description, body, owner; std::vector<std::string> files; };
struct Scan { std::vector<Doc> rules, skills, agents; std::vector<std::pair<std::string, std::string>> cli; };
std::pair<std::map<std::string, std::string>, std::string> frontmatter(const std::string& t) {
    std::map<std::string, std::string> meta;
    if (t.rfind("---", 0) != 0) return {meta, t};
    const std::size_t end = t.find("\n---", 3);
    if (end == std::string::npos) return {meta, t};
    std::stringstream ss(t.substr(3, end - 3)); std::string line;
    static const std::regex kv(R"(^([A-Za-z0-9_-]+)\s*:\s*(.*)$)");
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::smatch m;
        if (std::regex_match(line, m, kv)) {
            std::string v = trim(m[2].str());
            if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
            meta[lower(m[1].str())] = v;
        }
    }
    std::size_t body = t.find('\n', end + 4);
    return {meta, body == std::string::npos ? std::string() : t.substr(body + 1)};
}
std::string stem_of(const std::string& p) {
    std::string b = p.substr(p.find_last_of('/') == std::string::npos ? 0 : p.find_last_of('/') + 1);
    for (const char* ext : {".md", ".mdc", ".toml"}) { const std::string e = ext; if (b.size() > e.size() && lower(b.substr(b.size() - e.size())) == e) return b.substr(0, b.size() - e.size()); }
    return b;
}
Scan scan_project(const std::string& project) {
    Scan S;
    const auto r = call("/api/dev/scan", obj({{"project", Json(project)}}), {}, nullptr, 20000);
    if (!r.ok()) return S;
    std::map<std::string, bool> enabled;
    const Json* files = r.json.get("files");
    if (files && files->array_ptr()) {
        for (const auto& f : *files->array_ptr())
            if (get_string(f, "kind") == "plugin-state") if (auto j = try_parse(get_string(f, "content")); j && j->object_ptr()) for (const auto& [k, v] : *j->object_ptr()) if (const bool* b = std::get_if<bool>(&v.value)) enabled[k] = *b;
        std::map<std::string, int> rank = {{"project", 0}, {"global", 1}, {"user", 2}};
        for (const auto& f : *files->array_ptr()) {
            Doc d; d.scope = get_string(f, "scope"); d.path = get_string(f, "path"); d.owner = get_string(f, "owner");
            const std::string kind = get_string(f, "kind"), content = get_string(f, "content");
            if (!d.owner.empty() && enabled.count(d.owner) && !enabled[d.owner]) continue;
            if (kind == "rule") { d.body = frontmatter(content).second; S.rules.push_back(d); continue; }
            if (kind != "skill" && kind != "agent") continue;
            auto [meta, body] = frontmatter(content);
            d.body = body;
            if (kind == "skill") {
                std::string dir = d.path; const auto slash = dir.find_last_of('/');
                dir = slash == std::string::npos ? stem_of(d.path) : dir.substr(0, slash);
                if (const auto s2 = dir.find_last_of('/'); s2 != std::string::npos) dir = dir.substr(s2 + 1);
                d.name = meta.count("name") ? meta["name"] : dir;
                d.description = meta.count("description") ? meta["description"] : clip(one_line(body), 160);
                for (const auto& x : json_strings(f.get("files"))) d.files.push_back(x);
                S.skills.push_back(d);
            } else {
                d.name = meta.count("name") ? meta["name"] : stem_of(d.path);
                d.description = meta.count("description") ? meta["description"] : clip(one_line(body), 140);
                S.agents.push_back(d);
            }
        }
        auto uniq = [&](std::vector<Doc>& v) {
            std::stable_sort(v.begin(), v.end(), [&](const Doc& a, const Doc& b) { return rank[a.scope] < rank[b.scope]; });
            std::set<std::string> seen; std::vector<Doc> out;
            for (auto& d : v) if (seen.insert(lower(d.name)).second) out.push_back(std::move(d));
            v = std::move(out);
        };
        uniq(S.skills); uniq(S.agents);
    }
    if (const Json* cl = r.json.get("agents_cli"); cl && cl->array_ptr())
        for (const auto& c : *cl->array_ptr()) S.cli.emplace_back(get_string(c, "bin"), get_string(c, "name"));
    return S;
}
std::string cli_bin_for(const std::string& name) {
    static const std::map<std::string, std::string> map = {{"claude code", "claude"}, {"claude", "claude"}, {"codex", "codex"}, {"codex cli", "codex"}, {"opencode", "opencode"},
        {"gemini", "gemini"}, {"gemini cli", "gemini"}, {"dsh", "dsh"}, {"deepseek harness", "dsh"}, {"deepseek", "dsh"}, {"cursor-agent", "cursor-agent"}, {"cursor", "cursor-agent"},
        {"qwen", "qwen"}, {"aider", "aider"}};
    const std::string n = lower(trim(name));
    auto it = map.find(n);
    return it != map.end() ? it->second : n;
}
bool cli_known(const std::string& name) {
    static const std::set<std::string> bins = {"claude", "codex", "opencode", "gemini", "dsh", "cursor-agent", "qwen", "aider"};
    return bins.count(cli_bin_for(name)) > 0;
}
std::string shq(const std::string& s) { std::string o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
std::string cli_command(const std::string& bin, const std::string& prompt, const std::string& mode) {
    const std::string p = shq(prompt); const bool autom = mode != "plan";
    if (bin == "claude") return "claude -p " + p + " --output-format text" + (autom ? " --permission-mode acceptEdits" : " --permission-mode plan");
    if (bin == "codex") return std::string("codex exec ") + (autom ? "--full-auto" : "--sandbox read-only") + " " + p;
    if (bin == "opencode") return "opencode run " + p;
    if (bin == "gemini") return "gemini -p " + p + (autom ? " --yolo" : "");
    if (bin == "cursor-agent") return "cursor-agent -p " + p + " --output-format text";
    if (bin == "qwen") return "qwen -p " + p + (autom ? " --yolo" : "");
    if (bin == "aider") return "aider --message " + p + (autom ? " --yes-always" : " --dry-run") + " --no-pretty";
    if (bin == "dsh") return "dsh run " + p;
    return {};
}

// ---------- sessions ----------
struct Session {
    std::string id, project, base, title = "New session", mode = "auto", model = "auto", status = "idle", checkpoint, merge_checkpoint, last_model;
    bool isolated = false, git_fork = false, review = false, interactive = true, merged = false, discarded = false;
    long long created = 0, updated = 0, ms = 0;
    int steps = 0, tools = 0, files = 0, max_steps = 40, pref = 0;
    std::vector<std::pair<std::string, std::string>> convo;
    std::vector<Json> events;
    long long next_seq = 1;
    std::vector<Json> todos;
    // runtime only
    std::mutex mu;
    std::condition_variable cv;
    std::atomic<bool> cancel{false};
    bool running = false, allow_all = false;
    std::string waiting_id, waiting_kind, live;
    std::optional<std::string> answer;
    std::deque<std::string> queue;
};
std::mutex g_mu;
std::map<std::string, std::shared_ptr<Session>> g_sessions;
std::mutex g_slot_mu;
std::condition_variable g_slot_cv;
int g_running = 0;
int max_parallel() { return env_int("ASTRO_MAX_AGENTS", 8, 1, 64); }

fs::path engine_dir() { return fs::path(data_dir()) / ".astro_engine"; }
fs::path session_file(const std::string& id) { return engine_dir() / "runs" / (id + ".json"); }
fs::path manifest_file(const std::string& id) { return engine_dir() / "runs" / (id + ".manifest.json"); }

Json meta_json(const Session& s) {
    Json::Object o;
    o["id"] = Json(s.id); o["project"] = Json(s.project); o["base"] = Json(s.base); o["title"] = Json(s.title); o["mode"] = Json(s.mode);
    o["model"] = Json(s.model); o["status"] = Json(s.status); o["isolated"] = Json(s.isolated); o["git"] = Json(s.git_fork);
    o["merged"] = Json(s.merged); o["discarded"] = Json(s.discarded); o["created"] = num(s.created); o["updated"] = num(s.updated);
    o["steps"] = num(static_cast<double>(s.steps)); o["tools"] = num(static_cast<double>(s.tools)); o["files"] = num(static_cast<double>(s.files));
    o["ms"] = num(s.ms); o["last_model"] = Json(s.last_model); o["review"] = Json(s.review); o["checkpoint"] = Json(s.checkpoint);
    o["waiting"] = Json(s.waiting_id); o["queued"] = num(static_cast<double>(s.queue.size()));
    return Json(std::move(o));
}
// caller holds s.mu
void save_locked(Session& s) {
    s.updated = now_ms();
    Json::Object o = *meta_json(s).object_ptr();
    o["max_steps"] = num(static_cast<double>(s.max_steps)); o["pref"] = num(static_cast<double>(s.pref)); o["interactive"] = Json(s.interactive);
    o["merge_checkpoint"] = Json(s.merge_checkpoint); o["next_seq"] = num(s.next_seq);
    Json::Array convo; for (const auto& [r, c] : s.convo) convo.push_back(obj({{"role", Json(r)}, {"content", Json(c)}}));
    o["convo"] = Json(std::move(convo));
    Json::Array ev; const std::size_t from = s.events.size() > 3000 ? s.events.size() - 3000 : 0;
    for (std::size_t i = from; i < s.events.size(); ++i) ev.push_back(s.events[i]);
    o["events"] = Json(std::move(ev));
    o["todos"] = Json(Json::Array(s.todos.begin(), s.todos.end()));
    write_file_private(session_file(s.id).string(), dump_json(Json(std::move(o))));
}
void save(Session& s) { std::lock_guard<std::mutex> lk(s.mu); save_locked(s); }
std::shared_ptr<Session> load_session(const std::string& id) {
    const auto text = read_file_text(session_file(id).string());
    if (!text) return nullptr;
    auto j = try_parse(*text);
    if (!j || !j->is_object()) return nullptr;
    auto s = std::make_shared<Session>();
    s->id = id; s->project = get_string(*j, "project"); s->base = get_string(*j, "base"); s->title = get_string(*j, "title", "Session");
    s->mode = get_string(*j, "mode", "auto"); s->model = get_string(*j, "model", "auto"); s->status = get_string(*j, "status", "idle");
    s->checkpoint = get_string(*j, "checkpoint"); s->merge_checkpoint = get_string(*j, "merge_checkpoint"); s->last_model = get_string(*j, "last_model");
    s->isolated = json_bool(*j, "isolated", false); s->git_fork = json_bool(*j, "git", false); s->review = json_bool(*j, "review", false);
    s->interactive = json_bool(*j, "interactive", true); s->merged = json_bool(*j, "merged", false); s->discarded = json_bool(*j, "discarded", false);
    s->created = static_cast<long long>(jnum(j->get("created"))); s->updated = static_cast<long long>(jnum(j->get("updated"))); s->ms = static_cast<long long>(jnum(j->get("ms")));
    s->steps = json_int(*j, "steps", 0); s->tools = json_int(*j, "tools", 0); s->files = json_int(*j, "files", 0);
    s->max_steps = json_int(*j, "max_steps", 40); s->pref = json_int(*j, "pref", 0); s->next_seq = static_cast<long long>(jnum(j->get("next_seq"), 1));
    if (const Json* c = j->get("convo"); c && c->array_ptr()) for (const auto& m : *c->array_ptr()) s->convo.emplace_back(get_string(m, "role"), get_string(m, "content"));
    if (const Json* e = j->get("events"); e && e->array_ptr()) s->events = *e->array_ptr();
    if (const Json* t = j->get("todos"); t && t->array_ptr()) s->todos = *t->array_ptr();
    if (s->status == "running" || s->status == "waiting" || s->status == "queued") s->status = "interrupted";
    return s;
}
std::shared_ptr<Session> get_session(const std::string& id) {
    if (id.empty() || id.size() > 40 || !std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; })) return nullptr;
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_sessions.find(id);
    if (it != g_sessions.end()) return it->second;
    auto s = load_session(id);
    if (s) g_sessions[id] = s;
    return s;
}
// Adds an event (thread-safe) and wakes every long-poll waiter.
Json push(Session& s, Json::Object ev) {
    std::lock_guard<std::mutex> lk(s.mu);
    ev["seq"] = num(s.next_seq++); ev["t"] = num(now_ms());
    Json j(std::move(ev));
    s.events.push_back(j);
    if (s.events.size() > 4000) s.events.erase(s.events.begin(), s.events.begin() + 500);
    s.updated = now_ms();
    s.cv.notify_all();
    return j;
}
void note(Session& s, const std::string& text) { push(s, Json::Object{{"type", Json("note")}, {"text", Json(text)}}); }
void set_status(Session& s, const std::string& st) {
    { std::lock_guard<std::mutex> lk(s.mu); if (s.status == st) return; s.status = st; }
    push(s, Json::Object{{"type", Json("status")}, {"status", Json(st)}});
}
Json P(Session& s, Json::Object extra = {}) { extra["project"] = Json(s.project); return Json(std::move(extra)); }

// Waits for /api/agent/answer. Returns nullopt when the run is stopped.
std::optional<std::string> wait_answer(Session& s, const std::string& id, const std::string& kind) {
    std::unique_lock<std::mutex> lk(s.mu);
    s.waiting_id = id; s.waiting_kind = kind; s.answer.reset();
    const std::string prev = s.status; s.status = "waiting";
    s.cv.notify_all();
    s.cv.wait(lk, [&] { return s.answer.has_value() || s.cancel.load(); });
    s.waiting_id.clear(); s.waiting_kind.clear(); s.status = "running";
    s.cv.notify_all();
    (void)prev;
    if (s.cancel.load()) return std::nullopt;
    return s.answer;
}

struct Stopped {};

// ---------- tools ----------
struct ToolOut { std::string result, out, error; bool ok = true; Json::Object meta; };
std::optional<std::string> read_text(Session& s, const std::string& path, bool* binary = nullptr) {
    const auto r = call("/api/fs/read", P(s, {{"path", Json(path)}}));
    if (!r.ok()) return std::nullopt;
    if (jtrue(r.json.get("binary"))) { if (binary) *binary = true; return std::nullopt; }
    return get_string(r.json, "content");
}
std::string must_write(Session& s, const std::string& path, const std::string& content) {
    const auto r = call("/api/fs/write", P(s, {{"path", Json(path)}, {"content", Json(content)}}));
    if (!r.ok()) throw std::runtime_error("Could not write " + path + ": " + r.error());
    return {};
}
std::pair<int, int> line_stats(const std::string& a, const std::string& b) {
    auto lines = [](const std::string& t) { std::multiset<std::string> m; std::stringstream ss(t); std::string l; while (std::getline(ss, l)) m.insert(l); return m; };
    auto A = lines(a), B = lines(b);
    int added = 0, removed = 0;
    for (auto it = B.begin(); it != B.end(); ++it) { auto f = A.find(*it); if (f != A.end()) A.erase(f); else ++added; }
    removed = static_cast<int>(A.size());
    return {added, removed};
}
std::optional<std::pair<std::size_t, std::size_t>> find_loose(const std::string& text, const std::string& needle) {
    if (const auto i = text.find(needle); i != std::string::npos) return std::make_pair(i, needle.size());
    const std::string t = trim(needle);
    if (t.empty()) return std::nullopt;
    // whitespace-insensitive match: compare token sequences
    std::vector<std::string> parts; { std::stringstream ss(t); std::string w; while (ss >> w) parts.push_back(w); }
    if (parts.empty()) return std::nullopt;
    for (std::size_t start = text.find(parts[0]); start != std::string::npos; start = text.find(parts[0], start + 1)) {
        std::size_t p = start + parts[0].size(); bool ok = true;
        for (std::size_t k = 1; k < parts.size(); ++k) {
            const std::size_t ws = p;
            while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
            if (p == ws || text.compare(p, parts[k].size(), parts[k]) != 0) { ok = false; break; }
            p += parts[k].size();
        }
        if (ok) return std::make_pair(start, p - start);
    }
    return std::nullopt;
}
std::regex glob_re(std::string p) {
    p = trim(p); if (p.rfind("./", 0) == 0) p.erase(0, 2);
    std::string re;
    auto esc = [](char c) { return std::string(std::strchr(".+^$()|[]\\", c) ? "\\" : "") + c; };
    for (std::size_t i = 0; i < p.size();) {
        const char c = p[i];
        if (c == '*' && i + 1 < p.size() && p[i + 1] == '*') { if (i + 2 < p.size() && p[i + 2] == '/') { re += "(?:.*/)?"; i += 3; } else { re += ".*"; i += 2; } continue; }
        if (c == '*') { re += "[^/]*"; ++i; continue; }
        if (c == '?') { re += "[^/]"; ++i; continue; }
        if (c == '{') {
            const auto j = p.find('}', i);
            if (j != std::string::npos) {
                re += "(?:"; std::stringstream ss(p.substr(i + 1, j - i - 1)); std::string alt; bool first = true;
                while (std::getline(ss, alt, ',')) { if (!first) re += "|"; first = false; for (char a : alt) re += a == '*' ? std::string("[^/]*") : esc(a); }
                re += ")"; i = j + 1; continue;
            }
        }
        re += esc(c); ++i;
    }
    return std::regex("^" + re + "$");
}
struct TreeFile { std::string path; double size = 0; };
std::vector<TreeFile> tree(Session& s) {
    std::vector<TreeFile> out;
    const auto r = call("/api/fs/tree", P(s));
    if (const Json* f = r.json.get("files"); f && f->array_ptr()) for (const auto& x : *f->array_ptr()) out.push_back({get_string(x, "path"), jnum(x.get("size"))});
    return out;
}
std::string base_name(const std::string& p) { const auto s = p.find_last_of('/'); return s == std::string::npos ? p : p.substr(s + 1); }

ToolOut exec_tool(Session& s, Json::Object& c) {
    ToolOut o;
    const std::string t = sfield(c, "tool");
    auto need = [&](const std::string& key) { std::string v = sfield(c, key.c_str()); if (trim(v).empty()) throw std::runtime_error(t + " needs \"" + key + "\"."); return v; };
    auto path_ok = [&]() { std::string p = need("path"); if (!ok_path(p)) throw std::runtime_error("Invalid path \"" + p + "\" (use a relative path inside the project)."); return p; };
    if (t == "bash") {
        const std::string cmd = need("command");
        const int timeout = std::clamp(static_cast<int>(jnum(c.count("timeout") ? &c["timeout"] : nullptr, 120)), 1, 1800);
        const auto r = call("/api/ide/exec", P(s, {{"command", Json(cmd)}, {"timeout", num(static_cast<double>(timeout))}, {"cwd", Json(sfield(c, "cwd"))}}), {}, &s.cancel);
        if (s.cancel) throw Stopped{};
        if (!r.ok()) throw std::runtime_error(r.error());
        const int code = static_cast<int>(jnum(r.json.get("code"), -1));
        const std::string out = get_string(r.json, "out");
        o.meta["code"] = num(static_cast<double>(code));
        o.out = clip(out, 40000); o.ok = code == 0;
        o.result = "exit code " + std::to_string(code) + (jtrue(r.json.get("timed_out")) ? " (stopped: timed out after " + std::to_string(timeout) + "s \xE2\x80\x94 use start_server for long-running processes)" : "") +
                   " \xC2\xB7 " + std::to_string(static_cast<long long>(jnum(r.json.get("ms")))) + " ms\n" + head_tail(out.empty() ? "(no output)" : out, 1500, 5000);
        return o;
    }
    if (t == "read_file") {
        const std::string p = path_ok(); bool bin = false;
        const auto text = read_text(s, p, &bin);
        if (bin) throw std::runtime_error(p + " is a binary file.");
        if (!text) throw std::runtime_error(p + " does not exist.");
        std::vector<std::string> lines; { std::stringstream ss(*text); std::string l; while (std::getline(ss, l)) lines.push_back(l); if (!text->empty() && text->back() == '\n') lines.push_back(""); }
        const int off = std::max(1, static_cast<int>(jnum(c.count("offset") ? &c["offset"] : nullptr, 1)));
        const int lim = std::clamp(static_cast<int>(jnum(c.count("limit") ? &c["limit"] : nullptr, 400)), 1, 2000);
        std::string body;
        const int n = static_cast<int>(lines.size());
        for (int i = off - 1; i < std::min(n, off - 1 + lim); ++i) { std::ostringstream ln; ln << std::setw(4) << (i + 1) << "| " << lines[static_cast<std::size_t>(i)] << "\n"; body += ln.str(); }
        o.meta["path"] = Json(p); o.meta["lines"] = num(static_cast<double>(n));
        o.out = clip(*text, 3000);
        o.result = p + " (" + std::to_string(n) + " lines" + (off > 1 || n > off - 1 + lim ? ", showing " + std::to_string(off) + "-" + std::to_string(std::min(n, off - 1 + lim)) : "") + ")\n" + clip(body, 14000);
        return o;
    }
    if (t == "write_file") {
        const std::string p = path_ok();
        std::string content = raw_field(c, "content");
        if (trim(content).empty()) throw std::runtime_error("write_file for " + p + " has no \"content\". Put the whole new file in \"content\" (use edit_file for small changes).");
        if (content.back() != '\n') content += '\n';
        const auto old = read_text(s, p);
        must_write(s, p, content);
        const auto [a, r] = line_stats(old.value_or(""), content);
        const long long lines = std::count(content.begin(), content.end(), '\n');
        o.meta["path"] = Json(p); o.meta["added"] = num(static_cast<double>(a)); o.meta["removed"] = num(static_cast<double>(r)); o.meta["created"] = Json(!old.has_value());
        o.out = clip(content, 3000);
        o.result = std::string(old ? "rewrote " : "created ") + p + " (" + std::to_string(lines) + " lines, +" + std::to_string(a) + " \xE2\x88\x92" + std::to_string(r) + ")";
        return o;
    }
    if (t == "edit_file") {
        const std::string p = path_ok();
        const std::string old_text = raw_field(c, "old_text"), new_text = raw_field(c, "new_text");
        if (old_text.empty()) throw std::runtime_error("edit_file needs \"old_text\".");
        bool bin = false;
        const auto text = read_text(s, p, &bin);
        if (!text) throw std::runtime_error(bin ? p + " is a binary file." : p + " does not exist. Use write_file to create it.");
        const auto hit = find_loose(*text, old_text);
        if (!hit) throw std::runtime_error("old_text was not found in " + p + ". Use read_file to see the current text and copy it exactly (or rewrite the whole file).");
        const std::string next = text->substr(0, hit->first) + new_text + text->substr(hit->first + hit->second);
        must_write(s, p, next);
        const auto [a, r] = line_stats(*text, next);
        const long long line = std::count(text->begin(), text->begin() + static_cast<long>(hit->first), '\n') + 1;
        o.meta["path"] = Json(p); o.meta["added"] = num(static_cast<double>(a)); o.meta["removed"] = num(static_cast<double>(r));
        o.out = clip(prefix_lines(old_text, "- ") + "\n" + prefix_lines(new_text, "+ "), 3000);
        o.result = "edited " + p + " at line " + std::to_string(line) + " (+" + std::to_string(a) + " \xE2\x88\x92" + std::to_string(r) + ")";
        return o;
    }
    if (t == "delete_file") {
        const std::string p = path_ok();
        const auto r = call("/api/fs/delete", P(s, {{"path", Json(p)}}));
        if (!r.ok()) throw std::runtime_error(r.error());
        o.meta["path"] = Json(p); o.result = "deleted " + p; return o;
    }
    if (t == "list_files") {
        std::string pre = sfield(c, "path");
        while (!pre.empty() && pre.back() == '/') pre.pop_back();
        if (pre == ".") pre.clear();
        if (!pre.empty()) pre += "/";
        std::vector<TreeFile> files;
        for (auto& f : tree(s)) if (pre.empty() || f.path.rfind(pre, 0) == 0) files.push_back(f);
        o.meta["count"] = num(static_cast<double>(files.size()));
        std::string list;
        for (std::size_t i = 0; i < files.size() && i < 400; ++i) list += files[i].path + " (" + fmt_bytes(files[i].size) + ")\n";
        o.out = clip(list, 3000);
        o.result = files.empty() ? (pre.empty() ? "The project is empty." : "No files under " + pre) : std::to_string(files.size()) + " files" + (files.size() > 400 ? " (first 400)" : "") + ":\n" + list;
        return o;
    }
    if (t == "glob") {
        const std::string pattern = trim(first_of(c, {"pattern", "glob", "query", "path"}));
        if (pattern.empty()) throw std::runtime_error("glob needs \"pattern\".");
        std::regex re; try { re = glob_re(pattern); } catch (...) { throw std::runtime_error("Could not read the glob pattern " + pattern); }
        const bool bare = pattern.find('/') == std::string::npos;
        std::vector<std::string> hits;
        for (auto& f : tree(s)) if (std::regex_match(f.path, re) || (bare && std::regex_match(base_name(f.path), re))) hits.push_back(f.path);
        std::string list; for (std::size_t i = 0; i < hits.size() && i < 300; ++i) list += hits[i] + "\n";
        o.meta["count"] = num(static_cast<double>(hits.size())); o.out = clip(list, 3000);
        o.result = hits.empty() ? "No files match " + pattern + "." : std::to_string(hits.size()) + " files match " + pattern + (hits.size() > 300 ? " (first 300)" : "") + ":\n" + list;
        return o;
    }
    if (t == "search") {
        const std::string q = need("query");
        const auto r = call("/api/ide/search", P(s, {{"query", Json(q)}, {"regex", Json(jtrue(c.count("regex") ? &c["regex"] : nullptr))}}));
        if (!r.ok()) throw std::runtime_error(r.error());
        std::vector<std::string> lines;
        if (const Json* fl = r.json.get("files"); fl && fl->array_ptr())
            for (const auto& f : *fl->array_ptr()) if (const Json* h = f.get("hits"); h && h->array_ptr())
                for (const auto& x : *h->array_ptr()) lines.push_back(get_string(f, "path") + ":" + std::to_string(static_cast<long long>(jnum(x.get("line")))) + ": " + clip(trim(get_string(x, "text")), 200));
        const double count = jnum(r.json.get("matches"), static_cast<double>(lines.size()));
        std::string body; for (std::size_t i = 0; i < lines.size() && i < 80; ++i) body += lines[i] + "\n";
        o.meta["count"] = num(count); o.out = clip(body, 3000);
        o.result = lines.empty() ? "No matches." : std::to_string(static_cast<long long>(count)) + " matches" + (jtrue(r.json.get("truncated")) ? " (truncated)" : "") + ":\n" + body;
        return o;
    }
    if (t == "web_search") {
        const std::string q = need("query");
        const auto r = call("/api/web/search", obj({{"q", Json(q)}}), {}, &s.cancel, 60000);
        if (!r.ok()) throw std::runtime_error(r.error());
        std::string body; int n = 0;
        if (const Json* res = r.json.get("results"); res && res->array_ptr())
            for (const auto& x : *res->array_ptr()) { if (++n > 8) break; body += std::to_string(n) + ". " + get_string(x, "title") + " \xE2\x80\x94 " + get_string(x, "url") + "\n   " + clip(get_string(x, "snippet"), 300) + "\n"; }
        o.meta["count"] = num(static_cast<double>(std::min(n, 8))); o.out = clip(body, 3000);
        o.result = body.empty() ? "No results." : body;
        return o;
    }
    if (t == "fetch_page") {
        const std::string url = need("url");
        const auto r = call("/api/web/read", obj({{"url", Json(url)}}), {}, &s.cancel, 60000);
        if (!r.ok()) throw std::runtime_error(r.error());
        o.meta["url"] = Json(get_string(r.json, "url", url)); o.out = clip(get_string(r.json, "text"), 3000);
        o.result = get_string(r.json, "title") + " \xE2\x80\x94 " + get_string(r.json, "url", url) + "\n" + clip(get_string(r.json, "text"), 9000);
        return o;
    }
    if (t == "start_server") {
        const std::string cmd = need("command");
        const auto st0 = call("/api/ide/proc/start", P(s, {{"cmd", Json(cmd)}}));
        if (!st0.ok()) throw std::runtime_error(st0.error());
        Json st;
        for (int i = 0; i < 24 && !s.cancel; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const auto r = call("/api/ide/proc/tail", P(s, {{"bytes", num(4000.0)}}));
            if (r.ok()) st = r.json;
            if (i >= 3 && (!jtrue(st.get("running")) || jnum(st.get("listening")) > 0)) break;
        }
        const bool running = jtrue(st.get("running"));
        const long long listening = static_cast<long long>(jnum(st.get("listening")));
        o.meta["port"] = num(static_cast<double>(listening)); o.meta["url"] = Json(get_string(st, "url"));
        o.out = clip(get_string(st, "log"), 3000); o.ok = running;
        if (!running) { o.result = "The server exited (code " + std::to_string(static_cast<long long>(jnum(st.get("exit"), -1))) + ").\nLog:\n" + head_tail(get_string(st, "log", "(no output)"), 1000, 3000); return o; }
        o.result = "running: " + cmd + "\n" + (listening ? "listening on port " + std::to_string(listening) + " \xE2\x80\x94 verify with: curl -s http://127.0.0.1:" + std::to_string(listening) + "/"
                                                      : "not listening yet (expected port " + std::to_string(static_cast<long long>(jnum(st.get("port")))) + "); check server_logs") +
                   "\nLog:\n" + head_tail(get_string(st, "log", "(no output yet)"), 800, 2500);
        return o;
    }
    if (t == "server_logs") {
        const auto r = call("/api/ide/proc/tail", P(s, {{"bytes", num(8000.0)}}));
        const bool running = jtrue(r.json.get("running"));
        o.out = clip(get_string(r.json, "log"), 3000);
        o.result = (running ? "running (port " + std::to_string(static_cast<long long>(jnum(r.json.get("listening"), jnum(r.json.get("port"))))) + ")" : std::string("no server running")) + "\n" + head_tail(get_string(r.json, "log", "(no output)"), 1000, 5000);
        return o;
    }
    if (t == "stop_server") { call("/api/ide/proc/stop", P(s)); o.result = "server stopped"; return o; }
    if (t == "ask_user") {
        const std::string q = need("question");
        Json::Array opts; if (auto it = c.find("options"); it != c.end() && it->second.array_ptr()) for (const auto& x : *it->second.array_ptr()) if (opts.size() < 6) opts.push_back(Json(x.string_or(dump_json(x))));
        if (!s.interactive) { o.result = "The user is not available to answer (non-interactive run). Choose the most sensible option yourself, continue, and mention the choice in the report."; return o; }
        const std::string qid = random_hex(6);
        push(s, Json::Object{{"type", Json("question")}, {"id", Json(qid)}, {"question", Json(q)}, {"options", Json(opts)}});
        save(s);
        const auto ans = wait_answer(s, qid, "question");
        if (!ans) throw Stopped{};
        push(s, Json::Object{{"type", Json("answered")}, {"id", Json(qid)}, {"answer", Json(*ans)}});
        o.result = "The user answered: " + *ans;
        return o;
    }
    if (t == "remember") {
        std::string fact = one_line(need("fact")); fact = fact.substr(0, 300);
        std::string mem = read_text(s, ".workspace/memory.md").value_or("# Project memory\n\nArchitecture decisions, conventions and preferences for this project. The AI team reads this before every task and adds lines it learns.\n");
        while (!mem.empty() && mem.back() == '\n') mem.pop_back();
        must_write(s, ".workspace/memory.md", mem + "\n- " + fact + " (" + today_iso() + ")\n");
        o.meta["fact"] = Json(fact); o.result = "saved to project memory: " + fact; return o;
    }
    if (t == "present") {
        const std::string p = path_ok();
        const auto r = call("/api/fs/read", P(s, {{"path", Json(p)}}));
        if (!r.ok()) throw std::runtime_error(p + " does not exist.");
        o.meta["path"] = Json(p); o.result = "shown to the user: " + p; return o;
    }
    if (t == "diagnostics") {
        Json::Array paths;
        if (auto it = c.find("paths"); it != c.end() && it->second.array_ptr()) for (const auto& x : *it->second.array_ptr()) paths.push_back(Json(x.string_or()));
        else if (!sfield(c, "path").empty()) paths.push_back(Json(sfield(c, "path")));
        const auto r = call("/api/dev/diagnostics", P(s, {{"paths", Json(paths)}}), {}, &s.cancel, 180000);
        if (!r.ok()) throw std::runtime_error(r.error());
        std::string lines; int n = 0, total = 0;
        if (const Json* pr = r.json.get("problems"); pr && pr->array_ptr()) {
            total = static_cast<int>(pr->array_ptr()->size());
            for (const auto& x : *pr->array_ptr()) { if (++n > 80) break; lines += get_string(x, "path") + ":" + std::to_string(static_cast<long long>(jnum(x.get("line")))) + ":" + std::to_string(static_cast<long long>(jnum(x.get("col")))) + " " + get_string(x, "severity") + ": " + get_string(x, "message") + " (" + get_string(x, "source") + ")\n"; }
        }
        std::string checkers; for (const auto& x : json_strings(r.json.get("checkers"))) checkers += (checkers.empty() ? "" : ", ") + x;
        o.meta["count"] = num(static_cast<double>(total)); o.out = clip(lines, 3000);
        o.result = "Checked with: " + (checkers.empty() ? std::string("no checker found for these files") : checkers) + ".\n" + (total ? std::to_string(total) + " problem(s):\n" + lines : std::string("No problems found."));
        return o;
    }
    if (t == "todo") {
        Json::Array items;
        const Json* src = nullptr;
        for (const char* k : {"items", "todos", "list"}) if (auto it = c.find(k); it != c.end()) { src = &it->second; break; }
        std::vector<std::string> raw;
        if (src && src->string_ptr()) { std::stringstream ss(*src->string_ptr()); std::string l; while (std::getline(ss, l)) if (!trim(l).empty()) raw.push_back(l); }
        auto status_of = [](const std::string& st) { const std::string l = lower(st); return l == "done" || l == "completed" || l == "complete" ? "done" : l == "doing" || l == "in_progress" || l == "active" ? "doing" : "todo"; };
        if (src && src->array_ptr()) for (const auto& x : *src->array_ptr()) {
            if (items.size() >= 30) break;
            if (x.string_ptr()) { raw.push_back(*x.string_ptr()); continue; }
            const std::string text = x.object_ptr() ? first_of(*x.object_ptr(), {"text", "content", "task", "title"}) : std::string();
            if (!text.empty()) items.push_back(obj({{"text", Json(text)}, {"status", Json(status_of(get_string(x, "status")))}}));
        }
        for (const auto& r0 : raw) {
            if (items.size() >= 30) break;
            const bool done = lower(r0).find("[x]") != std::string::npos;
            std::size_t k = 0; while (k < r0.size() && std::strchr("-*0123456789. []xX", r0[k])) ++k;
            if (k < r0.size()) items.push_back(obj({{"text", Json(trim(r0.substr(k)))}, {"status", Json(done ? "done" : "todo")}}));
        }
        if (items.empty()) throw std::runtime_error("todo needs \"items\": [{\"text\":\"...\",\"status\":\"todo|doing|done\"}].");
        int done = 0; for (const auto& x : items) if (get_string(x, "status") == "done") ++done;
        { std::lock_guard<std::mutex> lk(s.mu); s.todos.assign(items.begin(), items.end()); }
        push(s, Json::Object{{"type", Json("todo")}, {"items", Json(items)}});
        o.result = "To-do list updated (" + std::to_string(done) + "/" + std::to_string(items.size()) + " done).";
        return o;
    }
    if (t == "skill") {
        const std::string name = trim(first_of(c, {"name", "skill"}));
        const Scan sc = scan_project(s.project);
        const Doc* k = nullptr;
        for (const auto& d : sc.skills) if (lower(d.name) == lower(name)) { k = &d; break; }
        if (!k) for (const auto& d : sc.skills) if (lower(d.path).find("/" + lower(name) + "/") != std::string::npos) { k = &d; break; }
        if (!k) { std::string names; for (const auto& d : sc.skills) names += (names.empty() ? "" : ", ") + d.name; throw std::runtime_error("No skill named \"" + name + "\". Skills: " + (names.empty() ? "none installed" : names) + "."); }
        o.meta["path"] = Json(k->path);
        if (!sfield(c, "file").empty()) {
            const auto r = call("/api/dev/ext/read", P(s, {{"scope", Json(k->scope)}, {"path", Json(sfield(c, "file"))}}));
            if (!r.ok()) throw std::runtime_error(r.error());
            o.result = sfield(c, "file") + ":\n" + clip(get_string(r.json, "content"), 14000); return o;
        }
        std::string extra;
        if (!k->files.empty()) { extra = "\nThe skill also has these files (read one with {\"tool\":\"skill\",\"name\":\"" + k->name + "\",\"file\":\"<path>\"}):\n"; for (const auto& f : k->files) extra += f + "\n"; }
        o.out = clip(k->body, 3000);
        o.result = "SKILL \"" + k->name + "\" (" + k->scope + ") \xE2\x80\x94 follow these instructions:\n" + clip(k->body, 14000) + extra;
        return o;
    }
    if (t == "mcp") {
        const std::string server = trim(sfield(c, "server")), name = trim(first_of(c, {"name", "tool_name", "method"}));
        if (server.empty() || name.empty()) throw std::runtime_error("mcp needs \"server\" and \"name\" (see MCP TOOLS in the job).");
        Json args = Json(Json::Object{});
        if (auto it = c.find("arguments"); it != c.end() && it->second.is_object()) args = it->second;
        else if (auto it2 = c.find("input"); it2 != c.end() && it2->second.is_object()) args = it2->second;
        const auto r = call("/api/dev/mcp/call", P(s, {{"server", Json(server)}, {"tool", Json(name)}, {"arguments", args}}), {}, &s.cancel, 300000);
        if (!r.ok()) throw std::runtime_error(r.error());
        std::string text;
        if (const Json* ct = r.json.get("content"); ct && ct->array_ptr())
            for (const auto& x : *ct->array_ptr()) {
                const std::string ty = get_string(x, "type");
                if (ty == "text") text += get_string(x, "text") + "\n";
                else if (ty == "image") text += "[image " + get_string(x, "mimeType") + "]\n";
                else if (ty == "resource") { const Json* rs = x.get("resource"); text += (rs ? get_string(*rs, "text", "[resource " + get_string(*rs, "uri") + "]") : std::string("[resource]")) + "\n"; }
                else text += dump_json(x) + "\n";
            }
        if (text.empty()) if (const Json* sc2 = r.json.get("structuredContent")) text = dump_json(*sc2);
        if (jtrue(r.json.get("isError"))) throw std::runtime_error(clip(text.empty() ? "the MCP tool reported an error" : text, 2000));
        o.out = clip(text, 3000); o.result = clip(text.empty() ? "(empty result)" : text, 20000);
        return o;
    }
    if (t == "task") {
        const std::string prompt = trim(first_of(c, {"prompt", "description", "task"}));
        if (prompt.empty()) throw std::runtime_error("task needs \"prompt\".");
        const std::string agent = trim(first_of(c, {"agent", "subagent_type"}));
        const Scan sc = scan_project(s.project);
        if (!agent.empty()) {
            const std::string bin = cli_bin_for(agent);
            for (const auto& [b, label] : sc.cli) if (b == bin) {
                const auto r = call("/api/ide/exec", P(s, {{"command", Json(cli_command(bin, prompt, s.mode))}, {"timeout", num(900.0)}}), {}, &s.cancel);
                if (s.cancel) throw Stopped{};
                const int code = static_cast<int>(jnum(r.json.get("code"), -1));
                o.meta["code"] = num(static_cast<double>(code)); o.meta["agent"] = Json(label); o.out = clip(get_string(r.json, "out"), 3000); o.ok = code == 0;
                o.result = label + " finished with exit code " + std::to_string(code) + (jtrue(r.json.get("timed_out")) ? " (timed out)" : "") + ":\n" + clip(get_string(r.json, "out", "(no output)"), 12000);
                return o;
            }
        }
        const Doc* custom = nullptr;
        if (!agent.empty()) {
            for (const auto& d : sc.agents) if (lower(d.name) == lower(agent)) { custom = &d; break; }
            if (!custom) { std::string names; for (const auto& d : sc.agents) names += (names.empty() ? "" : ", ") + d.name; for (const auto& [b, _] : sc.cli) names += (names.empty() ? "" : ", ") + b; throw std::runtime_error("No agent named \"" + agent + "\". Agents: " + (names.empty() ? "none (omit \"agent\" for a general subagent)" : names) + "."); }
        }
        const auto files = tree(s);
        std::string ctx, list; int budget = 30000, shown = 0;
        for (const auto& f : files) {
            if (shown < 200) { list += f.path + "\n"; ++shown; }
            if (budget > 0 && (prompt.find(f.path) != std::string::npos || prompt.find(base_name(f.path)) != std::string::npos)) {
                if (auto txt = read_text(s, f.path)) { const std::string cut = clip(*txt, static_cast<std::size_t>(std::min(9000, budget))); budget -= static_cast<int>(cut.size()); ctx += "\n--- " + f.path + " ---\n" + cut + "\n"; }
            }
        }
        const std::string head = custom ? trim(custom->body) + "\n\n" : "You are a focused subagent of Astro AI's coding agent. ";
        const std::string q = head + "Do the sub-task below and reply with a concise, factual report the main agent can act on (findings, exact file paths and line numbers, recommended changes as code snippets). You cannot run tools; say what should be verified.\n\nSUB-TASK: " + prompt +
                              "\n\nPROJECT FILES (" + std::to_string(files.size()) + "):\n" + list + (ctx.empty() ? "" : "\n\nFILE CONTENTS:" + ctx);
        const auto cands = candidates(s.model);
        const Turn tr = model_turn({{"user", q}}, cands.empty() ? "auto" : cands.front().id, false, 3000, &s.cancel);
        if (s.cancel) throw Stopped{};
        if (trim(tr.text).empty()) throw std::runtime_error("The subagent got no answer (" + tr.error + ").");
        o.meta["agent"] = Json(custom ? custom->name : std::string("subagent")); o.out = clip(tr.text, 3000);
        o.result = (custom ? custom->name : std::string("Subagent")) + " report:\n" + clip(trim(tr.text), 12000);
        return o;
    }
    if (t == "_bad") {
        if (jtrue(c.count("cut") ? &c["cut"] : nullptr)) throw std::runtime_error("Your reply was cut off in the middle of a tool call or file (too long). Write big files in smaller parts (several files, or edit_file), then continue.");
        throw std::runtime_error("Could not read this tool call as JSON: " + clip(sfield(c, "raw"), 200) + ". Put exactly one JSON object in each ```tool block.");
    }
    if (auto ct = find_custom(s.project, t)) {
        Json::Object bc{{"tool", Json(std::string("bash"))}, {"command", Json(custom_command(*ct, c))}, {"timeout", num(static_cast<double>(ct->timeout))}};
        ToolOut r = exec_tool(s, bc);
        r.meta["custom"] = Json(true);
        return r;
    }
    throw std::runtime_error("Unknown tool \"" + t + "\". Tools: bash, read_file, write_file, edit_file, delete_file, list_files, glob, search, web_search, fetch_page, start_server, server_logs, stop_server, ask_user, remember, present, diagnostics, todo, skill, mcp, task.");
}

// ---------- context for the agent ----------
std::string project_brief(Session& s) {
    const auto files = tree(s);
    std::string list;
    for (std::size_t i = 0; i < files.size() && i < 120; ++i) list += files[i].path + " (" + fmt_bytes(files[i].size) + ")\n";
    const auto mem = read_text(s, ".workspace/memory.md");
    std::string out = "PROJECT \"" + (s.isolated ? s.base : s.project) + "\" \xE2\x80\x94 " +
                      (files.empty() ? std::string("empty folder") : std::to_string(files.size()) + " files" + (files.size() > 120 ? " (first 120)" : "") + ":\n" + list);
    if (mem) out += "\n\nPROJECT MEMORY (obey):\n" + clip(*mem, 3000);
    std::vector<std::string> parts;
    const std::string mode = s.mode;
    std::string mline = "MODE: " + std::string(mode == "plan" ? "PLAN" : mode == "ask" ? "ASK" : "AUTO") + " \xE2\x80\x94 " +
        (mode == "plan" ? "read-only: inspect, then reply with a numbered implementation plan and the done block. Do not change files or run commands that change anything."
         : mode == "ask" ? "Astro automatically shows the user an approval prompt before each change or command runs, so just call the tools \xE2\x80\x94 do not use ask_user to request permission. If a call is denied, choose another approach."
                         : "you act on your own; a checkpoint was saved so the user can undo everything.");
    std::error_code ec;
    if (project_is_link(fs::path(projects_root()) / s.project)) mline += " This project is a real folder on the user's computer: be careful, never delete or rewrite files you were not asked to touch.";
    if (s.isolated) mline += " You work in an isolated copy of the project (a parallel agent); the user reviews and merges your changes afterwards, so finish the job completely here.";
    if (!s.interactive) mline += " The user is not watching this run: never wait for answers; make sensible choices and report them.";
    parts.push_back(mline);
    const Scan sc = scan_project(s.project);
    std::string rules; int budget = 12000;
    for (const char* scope : {"project", "global", "user"})
        for (const auto& r : sc.rules) {
            if (r.scope != scope || budget <= 0) continue;
            const std::string body = clip(trim(r.body), static_cast<std::size_t>(std::min(5000, budget)));
            budget -= static_cast<int>(body.size());
            if (!body.empty()) rules += "--- " + (r.scope == "project" ? r.path : r.scope + ": " + r.path) + " ---\n" + body + "\n";
        }
    if (!rules.empty()) parts.push_back("PROJECT RULES (obey them; project rules win over global ones):\n" + rules);
    if (!sc.skills.empty()) {
        std::string k; int n = 0;
        for (const auto& d : sc.skills) { if (++n > 60) break; k += "- " + d.name + ": " + clip(d.description, 200) + "\n"; }
        parts.push_back("SKILLS (load one with {\"tool\":\"skill\",\"name\":\"...\"} when the job matches its description):\n" + k);
    }
    std::string agents;
    for (const auto& a : sc.agents) agents += "- " + a.name + ": " + clip(a.description, 160) + " (custom subagent)\n";
    for (const auto& [bin, label] : sc.cli) agents += "- " + bin + ": " + label + " installed on this computer (runs headless in the project folder)\n";
    if (!agents.empty()) parts.push_back("AGENTS for the task tool:\n" + agents);
    if (const auto cts = custom_tools(s.project); !cts.empty()) {
        std::string k;
        for (const auto& t : cts) {
            std::string as; for (const auto& [n, d] : t.args) as += (as.empty() ? "" : ", ") + n;
            k += "- " + t.name + "(" + as + "): " + t.description + (t.readonly ? " [read-only]" : "") + "\n";
            for (const auto& [n, d] : t.args) if (!d.empty()) k += "    " + n + ": " + d + "\n";
        }
        parts.push_back("PROJECT TOOLS (defined in .astro/tools; call with {\"tool\":\"<name>\",\"<arg>\":\"...\"}; they run a fixed command in the project folder):\n" + k);
    }
    // MCP tools of the project's servers (started on demand; capped wait)
    const auto mr = call("/api/dev/mcp/list", P(s, {{"start", Json(true)}}), {}, &s.cancel, 25000);
    if (const Json* svs = mr.json.get("servers"); svs && svs->array_ptr()) {
        std::string lines; int tb = 6000; int total = 0, shown = 0;
        for (const auto& sv : *svs->array_ptr()) {
            if (get_string(sv, "status") != "running") continue;
            if (const Json* tl = sv.get("tools"); tl && tl->array_ptr()) for (const auto& tool : *tl->array_ptr()) {
                ++total;
                if (tb < 0) continue;
                std::string props;
                if (const Json* sch = tool.get("inputSchema")) if (const Json* pr = sch->get("properties"); pr && pr->object_ptr()) {
                    const auto req = json_strings(sch->get("required"));
                    for (const auto& [k, v] : *pr->object_ptr()) props += (props.empty() ? "" : ", ") + k + (std::find(req.begin(), req.end(), k) != req.end() ? "" : "?") + ":" + get_string(v, "type", "any");
                }
                const std::string line = "- " + get_string(sv, "name") + " \xC2\xB7 " + get_string(tool, "name") + "(" + props + "): " + clip(get_string(tool, "description"), 160) + "\n";
                tb -= static_cast<int>(line.size());
                if (tb >= 0) { lines += line; ++shown; }
            }
        }
        if (total > shown) lines += "\xE2\x80\xA6 and " + std::to_string(total - shown) + " more\n";
        if (total) parts.push_back("MCP TOOLS (call with {\"tool\":\"mcp\",\"server\":\"...\",\"name\":\"...\",\"arguments\":{...}}):\n" + lines);
    }
    for (const auto& p : parts) out += "\n\n" + p;
    return out;
}
// The conversation sent to the model: the job, a summary of older steps, and the last 6 exchanges in full.
std::vector<std::pair<std::string, std::string>> compact(const Session& s) {
    const auto& c = s.convo;
    std::vector<std::pair<std::string, std::string>> out;
    if (c.empty()) return out;
    const std::size_t KEEP = 12;
    std::size_t tail = c.size() > KEEP + 1 ? c.size() - KEEP : 1;
    if (tail < c.size() && c[tail].first != "assistant") tail = std::min(c.size() - 1, tail + 1);
    std::string head = c[0].second;
    if (tail > 1) {
        std::string lines;
        for (std::size_t i = 1; i < tail; ++i) {
            if (c[i].first == "assistant") {
                const Parsed p = parse_reply(c[i].second);
                std::string calls; for (const auto& x : p.calls) calls += (calls.empty() ? "" : "; ") + call_short(x);
                lines += "- You: " + clip(one_line(p.text), 220) + (calls.empty() ? "" : " \xE2\x86\x92 " + calls) + "\n";
            } else {
                std::string r = c[i].second; if (r.rfind("TOOL RESULTS:", 0) == 0) r.erase(0, 13);
                lines += "  Results: " + clip(one_line(r), 260) + "\n";
            }
        }
        head += "\n\nEARLIER STEPS IN THIS JOB (summary; the files on disk are the source of truth):\n" + clip(lines, 9000);
    }
    out.emplace_back("user", clip(head, 22000));
    for (std::size_t i = tail; i < c.size(); ++i) out.emplace_back(c[i].first, clip(c[i].second, 20000));
    // validate_messages needs user/assistant alternation ending with the user
    std::vector<std::pair<std::string, std::string>> merged;
    for (auto& m : out) { if (!merged.empty() && merged.back().first == m.first) merged.back().second += "\n\n" + m.second; else merged.push_back(std::move(m)); }
    return merged;
}
struct Change { std::string path, status; };
std::vector<Change> fork_changes(Session& s);
std::vector<Change> change_list(Session& s) {
    if (s.isolated) return fork_changes(s);
    std::vector<Change> out;
    if (s.checkpoint.empty()) return out;
    const auto r = call("/api/ide/history/changes", P(s, {{"id", Json(s.checkpoint)}}));
    if (const Json* ch = r.json.get("changes"); ch && ch->array_ptr()) for (const auto& x : *ch->array_ptr()) out.push_back({get_string(x, "path"), get_string(x, "status")});
    return out;
}

// ---------- the agent loop ----------
const char* VERIFY_MSG = "VERIFY FIRST: you changed files but have not run anything since, so nothing is checked yet. Run the tests or the program now (for example the project's test command) with bash and read the output. Fix what fails. Only send the ```done block after a real run shows it works, and report the real result.";

void acquire_slot(Session& s) {
    std::unique_lock<std::mutex> lk(g_slot_mu);
    if (g_running >= max_parallel()) {
        set_status(s, "queued");
        note(s, "Waiting for a free agent slot (" + std::to_string(max_parallel()) + " agents run at once).");
    }
    g_slot_cv.wait(lk, [&] { return g_running < max_parallel() || s.cancel.load(); });
    ++g_running;
}
void release_slot() { { std::lock_guard<std::mutex> lk(g_slot_mu); --g_running; } g_slot_cv.notify_all(); }

std::string review_prompt(Session& s, const std::string& job, const std::string& report, const std::vector<Change>& changes) {
    std::string actions;
    {
        std::lock_guard<std::mutex> lk(s.mu);
        std::vector<std::string> acts;
        for (const auto& e : s.events) if (get_string(e, "type") == "tool_done") acts.push_back("- " + get_string(e, "label") + " \xE2\x86\x92 " + (jtrue(e.get("ok")) ? "ok" : "FAILED: " + clip(get_string(e, "summary"), 120)));
        for (std::size_t i = acts.size() > 40 ? acts.size() - 40 : 0; i < acts.size(); ++i) actions += acts[i] + "\n";
    }
    std::string contents; int budget = 12000;
    static const std::regex code_ext(R"(\.(c|cc|cpp|h|hpp|js|mjs|cjs|ts|py|java|go|rs|rb|php|sh|html|css|json|md|txt|yml|yaml|toml|sql)$)", std::regex::icase);
    std::string names;
    for (const auto& c : changes) {
        names += (names.empty() ? "" : ", ") + c.path + " [" + c.status + "]";
        if (c.status == "deleted" || budget <= 0 || !std::regex_search(c.path, code_ext) || c.path.rfind(".workspace/", 0) == 0) continue;
        const auto t = read_text(s, c.path); if (!t) continue;
        const std::string chunk = clip(*t, static_cast<std::size_t>(std::min(3500, budget))); budget -= static_cast<int>(chunk.size());
        contents += "\n--- " + c.path + " (" + c.status + ") ---\n" + chunk + "\n";
    }
    return "Review the work of Astro AI's autonomous agent and decide whether the job is fully and correctly done.\n\nJOB: " + job + "\n\nAGENT REPORT:\n" + clip(report.empty() ? "(none)" : report, 2500) +
           "\n\nACTIONS (latest last):\n" + actions + "\nFILES CHANGED (" + std::to_string(changes.size()) + "): " + names + "\n\nCURRENT CONTENT OF CHANGED FILES:" + (contents.empty() ? " (none readable)" : contents) +
           "\n\nReply with the first line exactly \"VERDICT: PASS\" or \"VERDICT: FIX\". Then at most 8 short bullets: real problems (bugs, missing requirements, claims that were not verified, broken run commands) and how to fix each. Use FIX only for problems that matter; for PASS you may add brief suggestions.";
}

// One run of the loop. Returns true when the job finished (done block accepted).
bool loop(Session& s, int limit, std::string& report, bool& unverified) {
    int no_call = 0, tools_used = 0, verify_nudges = 0;
    std::map<std::string, int> fails;
    unverified = false;
    for (int step = 0; step < limit; ++step) {
        if (s.cancel) throw Stopped{};
        const auto cands = candidates(s.model);
        std::vector<std::string> order;
        if (cands.empty()) order.push_back("auto");
        else { const std::size_t k = static_cast<std::size_t>(s.pref) % cands.size(); for (std::size_t i = 0; i < cands.size() && order.size() < 4; ++i) order.push_back(cands[(k + i) % cands.size()].id); }
        Turn turn; std::string last_err;
        for (std::size_t k = 0; k < order.size(); ++k) {
            if (s.cancel) throw Stopped{};
            turn = model_turn(compact(s), order[k], true, 8000, &s.cancel, [&](const std::string& text) {
                const Parsed p = parse_reply(text);
                std::lock_guard<std::mutex> lk(s.mu);
                std::string live = p.text; if (const auto f = live.find("```"); f != std::string::npos) live.erase(f);
                s.live = trim(live); s.cv.notify_all();
            });
            if (s.cancel) throw Stopped{};
            if (trim(turn.text).size() > 2 && (turn.ok || turn.text.size() > 40)) { if (k > 0 && !cands.empty()) s.pref = static_cast<int>((static_cast<std::size_t>(s.pref) + k) % cands.size()); break; }
            last_err = turn.error.empty() ? "empty answer" : turn.error;
            turn.text.clear();
        }
        { std::lock_guard<std::mutex> lk(s.mu); s.live.clear(); }
        if (trim(turn.text).empty()) throw std::runtime_error("No model could answer (" + last_err + ").");
        ++s.steps;
        if (!turn.model.empty()) s.last_model = turn.model;
        const Parsed parsed = parse_reply(turn.text);
        if (!parsed.text.empty()) push(s, Json::Object{{"type", Json("say")}, {"text", Json(parsed.text)}, {"model", Json(turn.model)}, {"step", num(static_cast<double>(s.steps))}, {"final", Json(parsed.calls.empty() && parsed.done.has_value())}});
        s.convo.emplace_back("assistant", turn.text);
        if (parsed.calls.empty()) {
            if (parsed.done && unverified && verify_nudges < 2 && s.mode != "plan") { ++verify_nudges; s.convo.emplace_back("user", VERIFY_MSG); note(s, "Checking the work before finishing."); continue; }
            if (parsed.done || ++no_call >= (tools_used ? 2 : 3)) {
                report = !parsed.text.empty() ? parsed.text : parsed.done ? get_string(*parsed.done, "summary") : std::string();
                return true;
            }
            s.convo.emplace_back("user", "TOOL RESULTS: (none \xE2\x80\x94 your reply had no tool call.) Continue the job with tool calls in ```tool blocks, or, if the job is completely done and verified, reply with the report and the ```done block.");
            if (!tools_used && !cands.empty()) s.pref = (s.pref + 1) % static_cast<int>(cands.size());
            save(s);
            continue;
        }
        no_call = 0; ++tools_used;
        std::string results; bool batch_err = false; int idx = 0;
        for (auto call0 : parsed.calls) {
            if (idx >= 6 || s.cancel) break;
            ++idx;
            Json::Object& c = call0;
            const std::string tool = sfield(c, "tool");
            t_custom_project = s.project;
            const std::string tid = random_hex(5);
            const std::string label = call_short(c);
            Json::Object ev{{"type", Json("tool")}, {"id", Json(tid)}, {"tool", Json(tool)}, {"label", Json(label)}};
            if (tool == "bash" || tool == "start_server") ev["command"] = Json(clip(sfield(c, "command"), 4000));
            push(s, std::move(ev));
            const auto t0 = std::chrono::steady_clock::now();
            ToolOut out; bool blocked = false;
            try {
                if (s.mode == "plan" && is_mutating(c)) {
                    blocked = true;
                    throw std::runtime_error("BLOCKED: Plan mode is read-only: inspect with read_file, list_files, glob, search, diagnostics or read-only bash (ls, cat, grep, git diff...), then write the plan and finish with the done block. Do not change anything.");
                }
                if (s.mode == "ask" && is_mutating(c) && !s.allow_all) {
                    if (!s.interactive) { blocked = true; throw std::runtime_error("BLOCKED: Ask mode needs the user's approval, and nobody is watching this run. Explain in the report what you would run or change."); }
                    push(s, Json::Object{{"type", Json("approval")}, {"id", Json(tid)}, {"text", Json(approval_text(c))}, {"preview", Json(approval_preview(c))}, {"tool", Json(tool)}});
                    save(s);
                    const auto ans = wait_answer(s, tid, "approval");
                    if (!ans) throw Stopped{};
                    push(s, Json::Object{{"type", Json("answered")}, {"id", Json(tid)}, {"answer", Json(*ans)}});
                    const std::string a = lower(trim(*ans));
                    if (a == "allow all" || a == "allow all in this run" || a == "a") s.allow_all = true;
                    else if (a == "deny" || a == "n" || a == "no") { blocked = true; throw std::runtime_error("BLOCKED: The user denied this action. Choose another approach, or ask_user what they want."); }
                    else if (!(a == "allow" || a == "y" || a == "yes")) { blocked = true; throw std::runtime_error("BLOCKED: The user denied this action and said: " + *ans); }
                }
                out = exec_tool(s, c);
            } catch (const Stopped&) {
                push(s, Json::Object{{"type", Json("tool_done")}, {"id", Json(tid)}, {"tool", Json(tool)}, {"label", Json(label)}, {"ok", Json(false)}, {"summary", Json("Stopped")}});
                throw;
            } catch (const std::exception& e) {
                out.ok = false; out.error = e.what();
                out.result = std::string(blocked ? "" : "ERROR: ") + e.what();
            }
            const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            ++s.tools;
            if (!out.ok) batch_err = true;
            if ((tool == "write_file" || tool == "edit_file" || tool == "delete_file") && out.ok) unverified = true;
            if (tool == "bash" && out.ok && !blocked) unverified = false;
            if (tool == "bash" && !out.ok && out.error.empty()) unverified = false;   // a command ran (non-zero exit): the agent saw real output
            std::string res = out.result;
            const std::string sig = tool + "|" + sfield(c, "command") + sfield(c, "path");
            if (!out.ok && ++fails[sig] >= 3) res += "\nNOTE: this exact action has now failed 3 times. Stop repeating it: read the error, change the approach.";
            // automatic problem check after code edits (like the Agent page)
            static const std::regex code_ext(R"(\.(js|mjs|cjs|jsx|ts|tsx|mts|py|c|cc|cpp|cxx|h|hpp|sh|bash|php|go|rs|json)$)", std::regex::icase);
            if ((tool == "write_file" || tool == "edit_file") && out.ok && std::regex_search(sfield(c, "path"), code_ext) && !s.cancel) {
                const auto r = call("/api/dev/diagnostics", P(s, {{"paths", Json(Json::Array{Json(sfield(c, "path"))})}}), {}, &s.cancel, 60000);
                std::string errs; int n = 0;
                if (const Json* pr = r.json.get("problems"); pr && pr->array_ptr())
                    for (const auto& x : *pr->array_ptr()) if (get_string(x, "severity") == "error" && ++n <= 12) errs += get_string(x, "path") + ":" + std::to_string(static_cast<long long>(jnum(x.get("line")))) + ":" + std::to_string(static_cast<long long>(jnum(x.get("col")))) + " " + get_string(x, "message") + "\n";
                if (n) res += "\nPROBLEMS in " + sfield(c, "path") + " after this change (fix them):\n" + errs;
            }
            std::string summary = out.ok ? out.result.substr(0, out.result.find('\n')) : out.error;
            Json::Object done{{"type", Json("tool_done")}, {"id", Json(tid)}, {"tool", Json(tool)}, {"label", Json(label)}, {"ok", Json(out.ok)}, {"summary", Json(clip(summary, 300))},
                              {"out", Json(clip(out.out, 4000))}, {"ms", num(ms)}, {"blocked", Json(blocked)}};
            for (auto& [k, v] : out.meta) done[k] = v;
            push(s, std::move(done));
            results += "### [" + std::to_string(idx) + "] " + label + "\n" + res + "\n\n";
        }
        if (parsed.calls.size() > 6) results += "(Only the first 6 tool calls ran; send the rest in the next reply.)";
        s.convo.emplace_back("user", "TOOL RESULTS:\n" + trim(results));
        save(s);
        if (parsed.done && !s.cancel) {
            if (verify_nudges < 2) {
                ++verify_nudges;
                s.convo.emplace_back("user", batch_err ? "NOT DONE YET: some of the actions above failed (see TOOL RESULTS). Fix them, run the tests or the program, and only then send the ```done block."
                                             : unverified && s.mode != "plan" ? std::string(VERIFY_MSG)
                                             : "NOT DONE YET: you sent the ```done block in the same reply as tool calls, before you could see their results, so your report is a guess. Read the TOOL RESULTS above, check the work for real, and send the report with the ```done block in a reply of its own.");
                note(s, "Checking the work before finishing.");
                continue;
            }
            report = parsed.text;
            return true;
        }
    }
    return false;
}

void run(std::shared_ptr<Session> sp, std::string prompt) {
    Session& s = *sp;
    acquire_slot(s);
    const auto started = std::chrono::steady_clock::now();
    set_status(s, "running");
    push(s, Json::Object{{"type", Json("user")}, {"text", Json(prompt)}});
    try {
        while (true) {
            if (s.cancel) throw Stopped{};
            if (s.checkpoint.empty() && !s.isolated) {
                const auto r = call("/api/ide/history/create", P(s, {{"label", Json("Before agent: " + clip(one_line(prompt), 90))}, {"kind", Json("ai")}}));
                s.checkpoint = get_string(r.json, "id");
            }
            const std::string brief = project_brief(s);
            const std::string prior = s.convo.empty() ? "" : "\n\nThis continues an earlier job in the same session; the earlier steps are summarised below.";
            const std::string job = "JOB: " + prompt + prior + "\n\n" + brief + "\n\nStart now: act with tools, verify, and finish with the done block.";
            s.convo.emplace_back("user", job);
            save(s);
            std::string report; bool unverified = false;
            bool finished = loop(s, s.max_steps, report, unverified);
            if (!finished) {
                note(s, "Reached the limit of " + std::to_string(s.max_steps) + " steps for one run. Send \"continue\" to let the agent keep going.");
                set_status(s, "paused");
            }
            if (finished && s.review && s.mode != "plan" && !s.cancel) {
                const auto changes = change_list(s);
                const auto cands = candidates("auto");
                std::string reviewer;
                for (const auto& r : cands) if (r.label != s.last_model && r.name != s.last_model) { reviewer = r.id; break; }
                if (!changes.empty() && !reviewer.empty()) {
                    note(s, "A second model is reviewing the work.");
                    const Turn rv = model_turn({{"user", review_prompt(s, prompt, report, changes)}}, reviewer, false, 1500, &s.cancel);
                    static const std::regex vre(R"(VERDICT\s*:\s*\**\s*(PASS|FIX))", std::regex::icase);
                    std::smatch m; std::string verdict = "PASS";
                    if (std::regex_search(rv.text, m, vre)) verdict = lower(m[1].str()) == "fix" ? "FIX" : "PASS";
                    std::string text = std::regex_replace(rv.text, std::regex(R"(^.*VERDICT\s*:\s*\**\s*(PASS|FIX)\**\s*\n?)", std::regex::icase), "");
                    push(s, Json::Object{{"type", Json("review")}, {"verdict", Json(rv.text.empty() ? std::string("SKIPPED") : verdict)}, {"text", Json(trim(text))}, {"model", Json(rv.model)}});
                    if (verdict == "FIX" && !rv.text.empty() && !s.cancel) {
                        s.convo.emplace_back("user", "REVIEW \xE2\x80\x94 a second model reviewed your work and found problems:\n" + clip(trim(text), 4000) + "\n\nFix every real problem (verify each fix by running or testing), then finish again with the report and the done block. If a point is wrong, say why in the report.");
                        std::string r2; bool u2 = false;
                        if (loop(s, 16, r2, u2)) { report = r2; unverified = u2; }
                    }
                }
            }
            if (finished) {
                const auto changes = change_list(s);
                s.files = static_cast<int>(changes.size());
                Json::Array fl; for (const auto& c : changes) fl.push_back(obj({{"path", Json(c.path)}, {"status", Json(c.status)}}));
                const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                push(s, Json::Object{{"type", Json("done")}, {"text", Json(report)}, {"files", Json(fl)}, {"steps", num(static_cast<double>(s.steps))}, {"ms", num(s.ms + ms)}, {"model", Json(s.last_model)}});
                static const std::regex claims(R"(\b(tests?|specs?)\b[^.\n]{0,40}\b(pass|passed|passing|green|succeed)|\bverified\b|\bworks\b)", std::regex::icase);
                if (s.mode == "plan") note(s, "Plan mode: nothing was changed. Review the plan, then run it in Ask or Auto mode.");
                else if (unverified && !changes.empty()) note(s, std::regex_search(report, claims) ? "Heads up: the report says the work was checked, but nothing was run after the last change. Treat it as untested." : "Nothing was run after the last change, so this result is not tested yet.");
                if (s.isolated && !changes.empty()) note(s, "This agent worked in an isolated copy. Review with `astro diff " + s.id + "`, then merge or discard.");
                set_status(s, "done");
            }
            // follow-up prompts sent while this run was busy
            std::lock_guard<std::mutex> lk(s.mu);
            if (s.queue.empty() || s.cancel) break;
            prompt = s.queue.front(); s.queue.pop_front();
            Json::Object u{{"type", Json("user")}, {"text", Json(prompt)}}; u["seq"] = num(s.next_seq++); u["t"] = num(now_ms());
            s.events.push_back(Json(std::move(u)));
            s.status = "running";
        }
    } catch (const Stopped&) {
        note(s, "Stopped by you.");
        set_status(s, "stopped");
    } catch (const std::exception& e) {
        push(s, Json::Object{{"type", Json("error")}, {"text", Json(std::string(e.what()))}});
        set_status(s, "error");
    }
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.ms += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        s.running = false; s.allow_all = false; s.live.clear(); s.queue.clear();
        save_locked(s);
        s.cv.notify_all();
    }
    release_slot();
}

// ---------- isolation: a private copy of the project for each parallel agent ----------
fs::path project_dir(const std::string& name) {
    std::error_code ec;
    const fs::path p = fs::path(projects_root()) / name;
    const fs::path c = fs::canonical(p, ec);
    return ec ? p : c;
}
void walk_files(const fs::path& root, const std::function<void(const std::string& rel, const fs::path& full)>& fn, std::size_t cap = 60000) {
    std::error_code ec;
    std::size_t n = 0;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto name = it->path().filename().string();
        std::error_code e2;
        if (it->is_symlink(e2)) { if (it->is_directory(e2)) it.disable_recursion_pending(); continue; }
        if (it->is_directory(e2)) { if (skip_dir(name)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(e2)) continue;
        if (++n > cap) break;
        fn(fs::relative(it->path(), root, e2).generic_string(), it->path());
    }
}
std::string file_hash(const fs::path& p) { return sha256_hex(read_all(p)); }
std::map<std::string, std::string> load_manifest(const std::string& id) {
    std::map<std::string, std::string> m;
    const auto t = read_file_text(manifest_file(id).string());
    if (auto j = t ? try_parse(*t) : std::nullopt; j && j->object_ptr()) for (const auto& [k, v] : *j->object_ptr()) m[k] = v.string_or();
    return m;
}
bool git_ok(const fs::path& dir, const std::vector<std::string>& args, std::string* out = nullptr, int timeout = 120) {
    std::vector<std::string> a = {"git", "-C", dir.string()};
    a.insert(a.end(), args.begin(), args.end());
    const auto r = run_capture(a, dir, {}, timeout, 50'000'000);
    if (out) *out = r.out;
    return r.code == 0;
}
// Creates the isolated copy and returns its project name.
std::string fork_project(Session& s, std::string& error) {
    const fs::path base = project_dir(s.base);
    std::error_code ec;
    if (!fs::is_directory(base, ec)) { error = "Project not found: " + s.base; return {}; }
    const std::string name = unique_project_name(s.base + ".agent-" + s.id.substr(0, 6));
    const fs::path work = fs::path(projects_root()) / name;
    bool git = false;
    if (fs::exists(base / ".git", ec) && have_tool("git") && git_ok(base, {"rev-parse", "--verify", "HEAD"})) {
        if (git_ok(base, {"worktree", "add", "--detach", work.string(), "HEAD"}, nullptr, 300)) {
            git = true;
            std::string patch;
            if (git_ok(base, {"diff", "--binary", "HEAD"}, &patch) && !patch.empty()) {
                const fs::path pf = engine_dir() / "tmp" / (s.id + ".patch");
                write_all(pf, patch);
                if (!git_ok(work, {"apply", "--binary", "--whitespace=nowarn", pf.string()})) note(s, "Some uncommitted changes could not be copied into the isolated worktree.");
                fs::remove(pf, ec);
            }
            std::string untracked;
            if (git_ok(base, {"ls-files", "--others", "--exclude-standard", "-z"}, &untracked)) {
                std::size_t a = 0;
                while (a < untracked.size()) {
                    const std::size_t b = untracked.find('\0', a);
                    const std::string rel = untracked.substr(a, (b == std::string::npos ? untracked.size() : b) - a);
                    a = b == std::string::npos ? untracked.size() : b + 1;
                    if (rel.empty()) continue;
                    fs::create_directories((work / rel).parent_path(), ec);
                    fs::copy_file(base / rel, work / rel, fs::copy_options::overwrite_existing, ec);
                }
            }
            // local environment files are ignored by git but needed to run the project (never merged back: not in the manifest)
            for (const char* envf : {".env", ".env.local", ".env.development"}) if (fs::exists(base / envf, ec) && !fs::exists(work / envf, ec)) fs::copy_file(base / envf, work / envf, ec);
        }
    }
    if (!git) {
        fs::create_directories(work, ec);
        if (ec) { error = "Could not create the isolated copy: " + ec.message(); return {}; }
        std::size_t count = 0; bool too_big = false;
        walk_files(base, [&](const std::string& rel, const fs::path& full) {
            if (too_big) return;
            if (++count > 30000) { too_big = true; return; }
            std::error_code e2;
            fs::create_directories((work / rel).parent_path(), e2);
            fs::copy_file(full, work / rel, fs::copy_options::overwrite_existing, e2);
        });
        if (too_big) { fs::remove_all(work, ec); error = "The project is too large to copy (over 30,000 files). Put it in a git repository to use isolated agents."; return {}; }
    }
    // dependencies are shared, not copied
    for (const char* dep : {"node_modules"})
        if (fs::is_directory(base / dep, ec) && !fs::exists(work / dep, ec)) { std::error_code e3; fs::create_directory_symlink(base / dep, work / dep, e3); }
    // manifest of the starting point, for merging back safely
    Json::Object man;
    walk_files(work, [&](const std::string& rel, const fs::path& full) { man[rel] = Json(file_hash(full)); });
    write_file_private(manifest_file(s.id).string(), dump_json(Json(std::move(man))));
    s.git_fork = git;
    return name;
}
std::vector<Change> fork_changes(Session& s) {
    std::vector<Change> out;
    if (!s.isolated || s.discarded) return out;
    const auto man = load_manifest(s.id);
    const fs::path work = project_dir(s.project);
    std::set<std::string> seen;
    walk_files(work, [&](const std::string& rel, const fs::path& full) {
        seen.insert(rel);
        auto it = man.find(rel);
        if (it == man.end()) out.push_back({rel, "added"});
        else if (it->second != file_hash(full)) out.push_back({rel, "modified"});
    });
    for (const auto& [rel, _] : man) if (!seen.count(rel)) out.push_back({rel, "deleted"});
    std::sort(out.begin(), out.end(), [](const Change& a, const Change& b) { return a.path < b.path; });
    return out;
}
std::string unified_diff(const fs::path& a, const fs::path& b, const std::string& rel) {
    std::error_code ec;
    const bool ha = fs::exists(a, ec), hb = fs::exists(b, ec);
    if (have_tool("git")) {
        std::vector<std::string> args = {"git", "diff", "--no-index", "--no-color", "-U3", "--", ha ? a.string() : std::string("/dev/null"), hb ? b.string() : std::string("/dev/null")};
        const auto r = run_capture(args, b.parent_path().empty() ? fs::path(".") : (hb ? b.parent_path() : a.parent_path()), {}, 30, 2'000'000);
        std::string out; std::stringstream ss(r.out); std::string line; bool body = false;
        while (std::getline(ss, line)) {
            if (!body) {
                if (line.rfind("--- ", 0) == 0) { out += ha ? "--- a/" + rel + "\n" : "--- /dev/null\n"; continue; }
                if (line.rfind("+++ ", 0) == 0) { out += hb ? "+++ b/" + rel + "\n" : "+++ /dev/null\n"; body = true; continue; }
                if (line.rfind("Binary files", 0) == 0) { out += "Binary file " + rel + " changed\n"; continue; }
                continue;
            }
            out += line + "\n";
        }
        if (!out.empty()) return "diff --astro a/" + rel + " b/" + rel + "\n" + out;
    }
    return "diff --astro a/" + rel + " b/" + rel + "\n(" + std::string(!ha ? "added" : !hb ? "deleted" : "changed") + ")\n";
}

// ---------- HTTP ----------
std::string new_id() { return "a" + random_hex(5); }
Json session_view(Session& s, long long since, std::size_t max_events = 400) {
    std::lock_guard<std::mutex> lk(s.mu);
    Json::Object o = *meta_json(s).object_ptr();
    Json::Array ev;
    for (const auto& e : s.events) if (static_cast<long long>(jnum(e.get("seq"))) > since) { ev.push_back(e); if (ev.size() >= max_events) break; }
    o["events"] = Json(std::move(ev)); o["live"] = Json(s.live); o["running"] = Json(s.running); o["waiting_kind"] = Json(s.waiting_kind);
    o["seq"] = num(s.next_seq - 1);
    o["todos"] = Json(Json::Array(s.todos.begin(), s.todos.end()));
    return Json(std::move(o));
}
bool start_run(const std::shared_ptr<Session>& sp, const std::string& prompt) {
    std::lock_guard<std::mutex> lk(sp->mu);
    if (sp->running) { sp->queue.push_back(prompt); sp->cv.notify_all(); return false; }
    sp->running = true; sp->cancel = false; sp->status = "running";
    if (sp->title == "New session") sp->title = clip(one_line(prompt), 70);
    std::thread(run, sp, prompt).detach();
    return true;
}
Json models_json() {
    Json::Array list;
    for (const auto& r : candidates("auto")) list.push_back(obj({{"id", Json(r.id)}, {"label", Json(r.label)}, {"provider", Json(r.provider_name)}}));
    Json::Array all;
    for (const auto& r : g_routes) {
        if (r.local) continue;
        all.push_back(obj({{"id", Json(r.id)}, {"label", Json(r.label)}, {"paid", Json(r.paid)}, {"blocked", Json(route_blocked(r).has_value())}}));
    }
    return obj({{"ranked", Json(std::move(list))}, {"all", Json(std::move(all))}});
}

}  // namespace eng

bool handle_engine_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path.rfind("/api/agent/", 0) != 0) return false;
    if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
    const auto body_opt = parse_request_json(request, fd);
    if (!body_opt) return true;
    const Json& body = *body_opt;
    const std::string action = path.substr(11);
    using namespace eng;
    try {
        if (action == "models") { send_json(fd, 200, models_json()); return true; }
        if (action == "server-stop") {
            // Only an engine started by the terminal app stops this way; the desktop app owns its own server.
            if (std::string(std::getenv("ASTRO_CLI_SERVER") ? std::getenv("ASTRO_CLI_SERVER") : "") != "1") { send_json(fd, 403, error_json("This engine belongs to the Astro AI app or a service; close it there.")); return true; }
            send_json(fd, 200, obj({{"ok", Json(true)}}));
            std::thread([] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); std::exit(0); }).detach();
            return true;
        }
        if (action == "start") {
            const std::string project = get_string(body, "project");
            const std::string prompt = trim(get_string(body, "prompt"));
            if (!valid_project_name(project)) { send_json(fd, 400, error_json("Choose a project.")); return true; }
            std::error_code ec;
            if (!fs::is_directory(fs::path(projects_root()) / project, ec)) { send_json(fd, 404, error_json("Project not found: " + project)); return true; }
            auto s = std::make_shared<Session>();
            s->id = new_id(); s->created = now_ms(); s->updated = s->created;
            s->mode = get_string(body, "mode", "auto"); if (s->mode != "plan" && s->mode != "ask") s->mode = "auto";
            s->model = truncate(get_string(body, "model", "auto"), 120); if (s->model.empty()) s->model = "auto";
            s->max_steps = std::clamp(json_int(body, "max_steps", 40), 1, 200);
            s->review = json_bool(body, "review", false);
            s->interactive = json_bool(body, "interactive", true);
            s->project = project;
            if (const std::string title = get_string(body, "title"); !title.empty()) s->title = clip(one_line(title), 70);
            if (json_bool(body, "isolate", false)) {
                s->base = project; s->isolated = true;
                std::string error;
                const std::string work = fork_project(*s, error);
                if (work.empty()) { send_json(fd, 500, error_json(error)); return true; }
                s->project = work;
            }
            { std::lock_guard<std::mutex> lk(g_mu); g_sessions[s->id] = s; }
            save(*s);
            if (!prompt.empty()) start_run(s, prompt);
            send_json(fd, 200, session_view(*s, 0));
            return true;
        }
        if (action == "list") {
            const std::string project = get_string(body, "project");
            std::vector<Json> metas;
            std::set<std::string> seen;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (auto& [id, s] : g_sessions) { std::lock_guard<std::mutex> l2(s->mu); seen.insert(id); metas.push_back(meta_json(*s)); }
            }
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(engine_dir() / "runs", ec)) {
                const std::string fn = e.path().filename().string();
                if (fn.size() < 6 || fn.substr(fn.size() - 5) != ".json" || fn.find(".manifest.") != std::string::npos) continue;
                const std::string id = fn.substr(0, fn.size() - 5);
                if (seen.count(id)) continue;
                if (auto s = load_session(id)) metas.push_back(meta_json(*s));
            }
            Json::Array out;
            std::sort(metas.begin(), metas.end(), [](const Json& a, const Json& b) { return jnum(a.get("updated")) > jnum(b.get("updated")); });
            const bool all = json_bool(body, "all", false);
            for (auto& m : metas) {
                if (!project.empty() && get_string(m, "project") != project && get_string(m, "base") != project) continue;
                if (!all && jtrue(m.get("discarded")) && !jtrue(m.get("merged"))) continue;  // merged ones stay (they can be undone)
                if (out.size() >= 200) break;
                out.push_back(m);
            }
            send_json(fd, 200, obj({{"sessions", Json(std::move(out))}, {"running", num(static_cast<double>(g_running))}, {"max", num(static_cast<double>(max_parallel()))}}));
            return true;
        }
        const auto s = get_session(get_string(body, "id"));
        if (!s) { send_json(fd, 404, error_json("No agent session with that id.")); return true; }
        if (action == "get") { send_json(fd, 200, session_view(*s, static_cast<long long>(jnum(body.get("since"), 0)), 4000)); return true; }
        if (action == "events") {
            const long long since = static_cast<long long>(jnum(body.get("since"), 0));
            const int wait = std::clamp(json_int(body, "wait", 20), 0, 25);
            const std::string live_seen = get_string(body, "live");
            {
                std::unique_lock<std::mutex> lk(s->mu);
                s->cv.wait_for(lk, std::chrono::seconds(wait), [&] { return s->next_seq - 1 > since || (!s->live.empty() && s->live != live_seen) || !s->running; });
            }
            send_json(fd, 200, session_view(*s, since));
            return true;
        }
        if (action == "send") {
            const std::string prompt = trim(get_string(body, "prompt"));
            if (prompt.empty()) { send_json(fd, 400, error_json("Write a message.")); return true; }
            if (s->discarded) { send_json(fd, 409, error_json("This agent's copy was discarded.")); return true; }
            { std::lock_guard<std::mutex> lk(s->mu);
              if (const std::string m = get_string(body, "mode"); m == "plan" || m == "ask" || m == "auto") s->mode = m;
              if (const std::string m = get_string(body, "model"); !m.empty()) s->model = truncate(m, 120); }
            const bool started = start_run(s, prompt);
            send_json(fd, 200, obj({{"ok", Json(true)}, {"queued", Json(!started)}, {"seq", num(s->next_seq - 1)}}));
            return true;
        }
        if (action == "stop") {
            { std::lock_guard<std::mutex> lk(s->mu); s->cancel = true; s->queue.clear(); s->cv.notify_all(); }
            eng::g_slot_cv.notify_all();
            send_json(fd, 200, obj({{"ok", Json(true)}}));
            return true;
        }
        if (action == "answer") {
            std::lock_guard<std::mutex> lk(s->mu);
            const std::string qid = get_string(body, "qid");
            if (s->waiting_id.empty() || (!qid.empty() && qid != s->waiting_id)) { send_json(fd, 409, error_json("The agent is not waiting for that answer.")); return true; }
            s->answer = truncate(get_string(body, "answer"), 4000);
            s->cv.notify_all();
            send_json(fd, 200, obj({{"ok", Json(true)}}));
            return true;
        }
        if (action == "mode") {
            std::lock_guard<std::mutex> lk(s->mu);
            const std::string m = get_string(body, "mode");
            if (m == "plan" || m == "ask" || m == "auto") s->mode = m;
            if (const std::string mm = get_string(body, "model"); !mm.empty()) s->model = truncate(mm, 120);
            if (!s->running) save_locked(*s);   // a running agent saves on its own (it owns the conversation)
            send_json(fd, 200, meta_json(*s));
            return true;
        }
        if (action == "changes") {
            Json::Array fl; for (const auto& c : change_list(*s)) fl.push_back(obj({{"path", Json(c.path)}, {"status", Json(c.status)}}));
            send_json(fd, 200, obj({{"changes", Json(std::move(fl))}, {"isolated", Json(s->isolated)}}));
            return true;
        }
        if (action == "diff") {
            std::string diff; Json::Array fl;
            if (s->isolated) {
                const fs::path base = project_dir(s->base), work = project_dir(s->project);
                for (const auto& c : fork_changes(*s)) {
                    fl.push_back(obj({{"path", Json(c.path)}, {"status", Json(c.status)}}));
                    if (diff.size() < 3'000'000) diff += unified_diff(base / c.path, work / c.path, c.path);
                }
            } else {
                const fs::path dir = project_dir(s->project);
                std::error_code ec;
                for (const auto& c : change_list(*s)) fl.push_back(obj({{"path", Json(c.path)}, {"status", Json(c.status)}}));
                if (fs::exists(dir / ".git", ec) && have_tool("git")) { std::string out; git_ok(dir, {"diff", "--no-color", "HEAD"}, &out); diff = clip(out, 3'000'000); }
            }
            send_json(fd, 200, obj({{"changes", Json(std::move(fl))}, {"diff", Json(diff)}}));
            return true;
        }
        if (action == "merge") {
            if (!s->isolated) { send_json(fd, 400, error_json("Only agents that ran in an isolated copy can be merged.")); return true; }
            if (s->discarded) { send_json(fd, 409, error_json("This copy was discarded.")); return true; }
            { std::lock_guard<std::mutex> lk(s->mu); if (s->running) { send_json(fd, 409, error_json("The agent is still working. Stop it or wait until it finishes.")); return true; } }
            const auto man = load_manifest(s->id);
            const fs::path base = project_dir(s->base), work = project_dir(s->project);
            const auto changes = fork_changes(*s);
            const bool force = json_bool(body, "force", false);
            std::set<std::string> only; for (const auto& p : json_strings(body.get("paths"))) only.insert(p);
            std::vector<Change> apply; Json::Array conflicts, skipped;
            std::error_code ec;
            for (const auto& c : changes) {
                if (!only.empty() && !only.count(c.path)) continue;
                const bool base_has = fs::exists(base / c.path, ec);
                const std::string base_now = base_has ? file_hash(base / c.path) : std::string();
                auto it = man.find(c.path);
                const std::string at_fork = it == man.end() ? std::string() : it->second;
                const bool work_has = fs::exists(work / c.path, ec);
                if (work_has && base_has && base_now == file_hash(work / c.path)) { skipped.push_back(Json(c.path)); continue; }   // already identical
                if (base_now != at_fork && !force) { conflicts.push_back(obj({{"path", Json(c.path)}, {"status", Json(c.status)}})); continue; }
                apply.push_back(c);
            }
            if (!conflicts.empty() && !json_bool(body, "partial", false)) {
                send_json(fd, 409, obj({{"error", Json("These files also changed in the project since the agent started. Merge with partial=true to apply the rest, or force=true to overwrite.")}, {"conflicts", Json(conflicts)}}));
                return true;
            }
            std::string cp;
            if (!apply.empty()) {
                const auto r = call("/api/ide/history/create", obj({{"project", Json(s->base)}, {"label", Json("Before merging agent " + s->id + ": " + clip(s->title, 60))}, {"kind", Json("ai")}}));
                cp = get_string(r.json, "id");
            }
            Json::Array merged;
            for (const auto& c : apply) {
                std::error_code e2;
                if (c.status == "deleted") fs::remove(base / c.path, e2);
                else { fs::create_directories((base / c.path).parent_path(), e2); fs::copy_file(work / c.path, base / c.path, fs::copy_options::overwrite_existing, e2); }
                if (!e2) merged.push_back(obj({{"path", Json(c.path)}, {"status", Json(c.status)}}));
                else conflicts.push_back(obj({{"path", Json(c.path)}, {"status", Json("error: " + e2.message())}}));
            }
            {
                std::lock_guard<std::mutex> lk(s->mu);
                s->merged = true; s->merge_checkpoint = cp;
                save_locked(*s);
            }
            push(*s, Json::Object{{"type", Json("note")}, {"text", Json("Merged " + std::to_string(merged.size()) + " file(s) into " + s->base + (conflicts.empty() ? "." : "; " + std::to_string(conflicts.size()) + " conflict(s) left."))}});
            bool discarded = false;
            if (json_bool(body, "discard", false) && conflicts.empty()) {
                if (s->git_fork) git_ok(project_dir(s->base), {"worktree", "remove", "--force", work.string()});
                fs::remove_all(fs::path(projects_root()) / s->project, ec);
                std::lock_guard<std::mutex> lk(s->mu); s->discarded = true; save_locked(*s); discarded = true;
            }
            send_json(fd, 200, obj({{"ok", Json(true)}, {"merged", Json(merged)}, {"conflicts", Json(conflicts)}, {"skipped", Json(skipped)}, {"checkpoint", Json(cp)}, {"discarded", Json(discarded)}}));
            return true;
        }
        if (action == "discard") {
            if (!s->isolated) { send_json(fd, 400, error_json("Only isolated agents have a copy to discard.")); return true; }
            { std::lock_guard<std::mutex> lk(s->mu); s->cancel = true; s->cv.notify_all(); }
            for (int i = 0; i < 100; ++i) { { std::lock_guard<std::mutex> lk(s->mu); if (!s->running) break; } std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
            std::error_code ec;
            const fs::path work = fs::path(projects_root()) / s->project;
            if (s->git_fork) { git_ok(project_dir(s->base), {"worktree", "remove", "--force", fs::weakly_canonical(work, ec).string()}); git_ok(project_dir(s->base), {"worktree", "prune"}); }
            if (fs::exists(work, ec) && s->project.find(".agent-") != std::string::npos) fs::remove_all(work, ec);
            { std::lock_guard<std::mutex> lk(s->mu); s->discarded = true; if (s->status == "running") s->status = "stopped"; save_locked(*s); }
            push(*s, Json::Object{{"type", Json("note")}, {"text", Json("The isolated copy was discarded.")}});
            send_json(fd, 200, obj({{"ok", Json(true)}}));
            return true;
        }
        if (action == "undo") {
            const std::string target = s->isolated ? s->base : s->project;
            const std::string cp = s->isolated ? s->merge_checkpoint : s->checkpoint;
            if (cp.empty()) { send_json(fd, 400, error_json(s->isolated ? "Nothing was merged yet." : "This session has no checkpoint.")); return true; }
            const auto r = call("/api/ide/history/restore", obj({{"project", Json(target)}, {"id", Json(cp)}}));
            if (!r.ok()) { send_json(fd, 500, error_json(r.error())); return true; }
            push(*s, Json::Object{{"type", Json("note")}, {"text", Json("Undone: the project was restored to the checkpoint before " + std::string(s->isolated ? "the merge." : "this session."))}});
            send_json(fd, 200, obj({{"ok", Json(true)}, {"project", Json(target)}}));
            return true;
        }
        if (action == "delete") {
            { std::lock_guard<std::mutex> lk(s->mu); if (s->running) { send_json(fd, 409, error_json("Stop the agent first.")); return true; } }
            std::error_code ec;
            if (s->isolated && !s->discarded) {
                const fs::path work = fs::path(projects_root()) / s->project;
                if (s->git_fork) git_ok(project_dir(s->base), {"worktree", "remove", "--force", fs::weakly_canonical(work, ec).string()});
                if (s->project.find(".agent-") != std::string::npos) fs::remove_all(work, ec);
            }
            fs::remove(session_file(s->id), ec); fs::remove(manifest_file(s->id), ec);
            { std::lock_guard<std::mutex> lk(g_mu); g_sessions.erase(s->id); }
            send_json(fd, 200, obj({{"ok", Json(true)}}));
            return true;
        }
    } catch (const std::exception& e) {
        send_json(fd, 500, error_json(e.what()));
        return true;
    }
    send_json(fd, 404, error_json("Unknown agent action."));
    return true;
}
