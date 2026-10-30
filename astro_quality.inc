// =============================================================================================
// Answer quality (included from workspace_server.cpp after workspace_ide.inc, inside the anonymous namespace)
//
//   * live model health: every model call records success and speed (EWMA) -> model_stats.json
//       GET  /api/models/health       per-model success rate, failures in a row, typical speed
//       Auto routing moves models that keep failing to the back of the line.
//   * background code check (runs AFTER the answer is shown, so answers stay fast)
//       POST /api/check/code          { lang, code, run } -> syntax check, and a short sandboxed run of pure code
//   * "this answer didn't work" feedback
//       POST /api/feedback            saved locally; shared with the A.S.T.R.O AI team only when the user ticks "share"
// =============================================================================================

// ---------- model health ----------
struct ModelStat { int n = 0, ok = 0, fails_in_row = 0; double ewma_ms = 0; long long last_at = 0; long long last_ok_at = 0; };
std::mutex g_mstat_mu;
std::map<std::string, ModelStat> g_mstat;
bool g_mstat_loaded = false;
long long g_mstat_saved = 0;

void model_stats_load_locked() {
    if (g_mstat_loaded) return;
    g_mstat_loaded = true;
    const auto t = read_small(fs::path(data_path("model_stats.json")), 4'000'000);
    if (!t) return;
    try {
        const Json j = JsonParser(*t).parse();
        const Json* m = j.get("models");
        if (!m || !m->object_ptr()) return;
        for (const auto& [id, v] : *m->object_ptr()) {
            ModelStat s;
            s.n = json_int(v, "n", 0); s.ok = json_int(v, "ok", 0); s.fails_in_row = json_int(v, "fails_in_row", 0);
            if (const Json* e = v.get("ewma_ms"); e && std::holds_alternative<double>(e->value)) s.ewma_ms = std::get<double>(e->value);
            if (const Json* e = v.get("last_at"); e && std::holds_alternative<double>(e->value)) s.last_at = static_cast<long long>(std::get<double>(e->value));
            if (const Json* e = v.get("last_ok_at"); e && std::holds_alternative<double>(e->value)) s.last_ok_at = static_cast<long long>(std::get<double>(e->value));
            g_mstat[id] = s;
        }
    } catch (...) {}
}
Json model_stats_json_locked() {
    Json::Object models;
    for (const auto& [id, s] : g_mstat) {
        Json::Object o;
        o["n"] = Json(s.n); o["ok"] = Json(s.ok); o["fails_in_row"] = Json(s.fails_in_row);
        o["ok_rate"] = Json(s.n ? static_cast<double>(s.ok) / s.n : 0.0);
        o["ewma_ms"] = Json(std::round(s.ewma_ms)); o["last_at"] = Json(static_cast<double>(s.last_at)); o["last_ok_at"] = Json(static_cast<double>(s.last_ok_at));
        models[id] = Json(std::move(o));
    }
    Json::Object root; root["schema"] = Json(1); root["updated"] = Json(static_cast<double>(now_ms())); root["models"] = Json(std::move(models));
    return Json(std::move(root));
}
// Called after every model attempt (chat, brain minds, team). Counts are windowed (they decay) so old history fades.
void model_stats_record(const std::string& id, bool ok, long long ms) {
    if (id.empty()) return;
    std::lock_guard lk(g_mstat_mu);
    model_stats_load_locked();
    ModelStat& s = g_mstat[id];
    if (s.n >= 40) { s.ok = static_cast<int>(std::lround(s.ok * 0.75)); s.n = 30; }   // keep roughly the last 30-40 calls
    ++s.n; s.last_at = now_ms();
    if (ok) {
        ++s.ok; s.fails_in_row = 0; s.last_ok_at = s.last_at;
        const double x = static_cast<double>(std::max<long long>(ms, 1));
        s.ewma_ms = s.ewma_ms <= 0 ? x : s.ewma_ms * 0.7 + x * 0.3;
    } else ++s.fails_in_row;
    if (now_ms() - g_mstat_saved > 15000) {
        g_mstat_saved = now_ms();
        write_file_private(data_path("model_stats.json"), dump_json(model_stats_json_locked()));
    }
}
// Penalty used to reorder Auto routes: models failing right now go last; slow ones drift back a little.
int model_stats_penalty_locked(const std::string& id) {
    const auto it = g_mstat.find(id);
    if (it == g_mstat.end()) return 0;
    const ModelStat& s = it->second;
    const bool recent = now_ms() - s.last_at < 30LL * 60 * 1000;
    int p = 0;
    if (recent && s.fails_in_row >= 2) p += 40;
    if (s.n >= 4 && static_cast<double>(s.ok) / s.n < 0.5) p += 25;
    if (s.ewma_ms > 0 && s.ewma_ms < 8000) p -= 2;
    if (s.ewma_ms > 40000) p += 6;
    return p;
}
void model_stats_reorder(std::vector<Route>& routes, std::size_t keep_first) {
    if (routes.size() <= keep_first + 1) return;
    std::lock_guard lk(g_mstat_mu);
    model_stats_load_locked();
    std::vector<std::pair<int, std::size_t>> keyed;
    for (std::size_t i = keep_first; i < routes.size(); ++i) {
        int p = model_stats_penalty_locked(routes[i].id);
        if (p < 10) p = 0;   // only real trouble changes the configured order
        keyed.emplace_back(p, i);
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Route> out(routes.begin(), routes.begin() + static_cast<std::ptrdiff_t>(keep_first));
    for (const auto& [p, i] : keyed) out.push_back(routes[i]);
    routes.swap(out);
}

// ---------- background code check ----------
// Code is only RUN when it is self-contained: no files, network, processes, environment or input. Anything else
// gets a syntax check only. Runs happen in a fresh temp folder with a 6 second limit and a minimal environment.
struct CheckLang { std::string id, ext; };
std::optional<CheckLang> check_lang(std::string l) {
    l = lower_copy(trim(l));
    if (l == "js" || l == "javascript" || l == "node" || l == "mjs") return CheckLang{"js", ".js"};
    if (l == "py" || l == "python" || l == "python3") return CheckLang{"py", ".py"};
    if (l == "c") return CheckLang{"c", ".c"};
    if (l == "cpp" || l == "c++" || l == "cc" || l == "cxx") return CheckLang{"cpp", ".cpp"};
    if (l == "sh" || l == "bash" || l == "shell" || l == "zsh") return CheckLang{"bash", ".sh"};
    if (l == "json") return CheckLang{"json", ".json"};
    if (l == "php") return CheckLang{"php", ".php"};
    if (l == "rb" || l == "ruby") return CheckLang{"ruby", ".rb"};
    if (l == "ts" || l == "typescript") return CheckLang{"ts", ".ts"};
    return std::nullopt;
}
// Returns why the code must not be run (empty = pure enough to run).
std::string check_impure(const std::string& lang, const std::string& code) {
    static const std::regex js(R"((require\s*\(|\bimport\s*\(|^\s*import\s|^\s*export\s|\b(document|window|localStorage|sessionStorage|navigator|location|alert|React|ReactDOM|\$)\s*[.(]|from\s+['"](node:)?(fs|child_process|net|http|https|os|dgram|cluster|worker_threads|vm|tls|dns|process)['"]|\bprocess\.(env|exit|stdin|kill|binding|chdir)|\bfetch\s*\(|XMLHttpRequest|WebSocket|\bDeno\.|\bBun\.|\beval\s*\(|new\s+Function|readline|prompt\s*\())");
    static const std::regex py(R"((\bimport\s+(os|sys|subprocess|socket|shutil|requests|urllib|http|ctypes|multiprocessing|pathlib|glob|signal|asyncio|threading|pickle|importlib|builtins)\b|\bfrom\s+(os|sys|subprocess|socket|shutil|requests|urllib|http|ctypes|multiprocessing|pathlib|glob|signal|asyncio|threading|pickle|importlib|builtins)\b|\bopen\s*\(|__import__|\beval\s*\(|\bexec\s*\(|\binput\s*\(|\bcompile\s*\())");
    static const std::regex cc(R"((\bsystem\s*\(|\bpopen\s*\(|\bfork\s*\(|\bexec[lv]p?e?\s*\(|\bfopen\s*\(|\bfreopen\s*\(|<fstream>|<filesystem>|<sys/socket\.h>|<unistd\.h>|<windows\.h>|\bremove\s*\(|\brename\s*\(|\bscanf\s*\(|\bgets\s*\(|\bstd::cin\b|\bcin\s*>>|\bgetline\s*\(\s*(std::)?cin|\bgetchar\s*\(|\bfgets\s*\(|asm\s*\(|__asm))");
    if (code.size() > 60000) return "too long to run";
    if (lang == "js" && std::regex_search(code, js)) return "uses files, network, processes or input";
    if (lang == "py" && std::regex_search(code, py)) return "uses files, the system, network or input";
    if (lang == "py") {
        // Line by line (std::regex::multiline is missing from older libc++ on macOS).
        static const std::regex imp(R"(^\s*(?:from\s+([A-Za-z_][\w.]*)\s+import|import\s+([A-Za-z_][\w., ]*)))");
        static const std::set<std::string> safe = {"math", "re", "json", "collections", "itertools", "functools", "typing", "dataclasses", "heapq", "bisect", "string",
            "random", "datetime", "decimal", "fractions", "statistics", "unittest", "enum", "abc", "copy", "operator", "textwrap", "time", "__future__", "array", "cmath", "doctest"};
        std::stringstream lines(code); std::string line; std::smatch mt;
        while (std::getline(lines, line)) {
            if (!std::regex_search(line, mt, imp)) continue;
            std::string mods = mt[1].matched ? mt[1].str() : mt[2].str();
            std::stringstream ss(mods); std::string m;
            while (std::getline(ss, m, ',')) { m = trim(m); m = m.substr(0, m.find_first_of(". ")); if (!m.empty() && !safe.count(m)) return "uses the package " + m; }
        }
    }
    if ((lang == "c" || lang == "cpp") && std::regex_search(code, cc)) return "uses files, the system or input";
    if (lang == "bash" || lang == "php" || lang == "ruby" || lang == "ts" || lang == "json") return "only checked, not run";
    return "";
}
void handle_check_code(int fd, const Json& body) {
    const std::string code = get_string(body, "code");
    const auto lang = check_lang(get_string(body, "lang"));
    Json::Object o;
    if (code.empty() || code.size() > 120000) { send_json(fd, 400, error_json("Send 1 to 120000 characters of code.")); return; }
    if (!lang) { o["checked"] = Json(false); o["reason"] = Json("This language is not checked yet."); send_json(fd, 200, Json(std::move(o))); return; }
    o["lang"] = Json(lang->id);
    if (lang->id == "json") {
        try { (void)JsonParser(code).parse(); o["checked"] = Json(true); o["ok"] = Json(true); }
        catch (const std::exception& e) { o["checked"] = Json(true); o["ok"] = Json(false); o["stage"] = Json("syntax"); o["errors"] = Json(std::string(e.what())); }
        send_json(fd, 200, Json(std::move(o))); return;
    }
    std::error_code ec;
    const fs::path dir = fs::path(data_dir()) / ".astro_checks" / random_hex(8);
    fs::create_directories(dir, ec);
    struct Cleanup { fs::path d; ~Cleanup() { std::error_code e; fs::remove_all(d, e); } } cleanup{dir};
    static const std::regex esm(R"(^\s*(import|export)\s)");
    bool is_esm = false;
    if (lang->id == "js") { std::stringstream ls(code); std::string ln; while (!is_esm && std::getline(ls, ln)) is_esm = std::regex_search(ln, esm); }
    const std::string file = "main" + (is_esm ? std::string(".mjs") : lang->ext);
    { std::ofstream out(dir / file, std::ios::binary); out << code; }
#ifdef _WIN32
    const std::string exe = "prog.exe";
#else
    const std::string exe = "./prog";
#endif
    std::vector<std::string> syntax, build, runv;
    std::string tool;
    const std::string& py = python_cmd();
    if (lang->id == "js") { tool = "node"; syntax = {"node", "--check", file}; runv = {"node", file}; }
    else if (lang->id == "py") { tool = py; syntax = {py, "-c", "import ast,sys; ast.parse(open(sys.argv[1],encoding='utf-8').read(), sys.argv[1])", file}; runv = {py, "-I", "-S", file}; }
    else if (lang->id == "c") { tool = "gcc"; syntax = {"gcc", "-std=c17", "-fsyntax-only", file}; build = {"gcc", "-std=c17", "-O1", "-o", exe, file, "-lm"}; runv = {exe}; }
    else if (lang->id == "cpp") { tool = "g++"; syntax = {"g++", "-std=c++20", "-fsyntax-only", file}; build = {"g++", "-std=c++20", "-O1", "-o", exe, file}; runv = {exe}; }
    else if (lang->id == "bash") { tool = "bash"; syntax = {"bash", "-n", file}; }
    else if (lang->id == "php") { tool = "php"; syntax = {"php", "-l", file}; }
    else if (lang->id == "ruby") { tool = "ruby"; syntax = {"ruby", "-c", file}; }
    else if (lang->id == "ts") { tool = "tsc"; syntax = {"tsc", "--noEmit", "--target", "es2022", "--skipLibCheck", file}; }
    if (!have_tool(tool)) { o["checked"] = Json(false); o["reason"] = Json(tool + " is not installed on this computer."); send_json(fd, 200, Json(std::move(o))); return; }
    o["tool"] = Json(tool);
    // Error messages mention the temp folder; show them as plain "main.js:3".
    auto clean = [&](std::string t) {
        for (const std::string& d : {dir.string() + "/", dir.string() + "\\", dir.string()}) for (std::size_t p; (p = t.find(d)) != std::string::npos;) t.erase(p, d.size());
        return t;
    };
    Captured s = run_capture(syntax, dir, {}, 25, 200000);
    s.out = clean(s.out); s.err = clean(s.err);
    o["checked"] = Json(true);
    if (s.timed_out || s.code != 0) {
        std::string err = trim(s.err + (s.err.empty() ? "" : "\n") + s.out);
        o["ok"] = Json(false); o["stage"] = Json(lang->id == "c" || lang->id == "cpp" ? "compile" : "syntax");
        o["errors"] = Json(truncate(s.timed_out ? "The check took too long." : err, 6000));
        send_json(fd, 200, Json(std::move(o))); return;
    }
    o["ok"] = Json(true);
    const std::string why = check_impure(lang->id, code);
    if (!json_bool(body, "run", true) || runv.empty() || !why.empty()) {
        o["ran"] = Json(false); if (!why.empty()) o["not_run"] = Json(why);
        send_json(fd, 200, Json(std::move(o))); return;
    }
    static const std::regex has_main(R"(\bint\s+main\s*\()");
    if (!build.empty()) {
        if (!std::regex_search(code, has_main)) { o["ran"] = Json(false); o["not_run"] = Json("no main() to run"); send_json(fd, 200, Json(std::move(o))); return; }
        Captured b = run_capture(build, dir, {}, 40, 200000);
        if (b.timed_out || b.code != 0) {   // compiles but does not link: a missing library or a snippet, not a bug we can judge
            o["ran"] = Json(false); o["not_run"] = Json("could not be built here (" + truncate(trim(clean(b.err)), 200) + ")");
            send_json(fd, 200, Json(std::move(o))); return;
        }
    }
    Captured r = run_capture(runv, dir, {"ASTRO_CHECK=1"}, 6, 100000);
    r.out = clean(r.out); r.err = clean(r.err);
    o["ran"] = Json(true); o["exit"] = Json(r.code); o["timed_out"] = Json(r.timed_out);
    o["stdout"] = Json(truncate(r.out, 4000)); o["stderr"] = Json(truncate(r.err, 4000));
    if (r.timed_out) { o["ok"] = Json(false); o["stage"] = Json("run"); o["errors"] = Json("It ran for more than 6 seconds (endless loop, or waiting for input)."); }
    else if (r.code != 0) {
        // Missing packages or browser-only globals mean "can't judge here", not "the code is wrong".
        static const std::regex benign(R"(ModuleNotFoundError|No module named|Cannot find module|ERR_MODULE_NOT_FOUND|Cannot find package|ReferenceError: (document|window|localStorage|navigator|fetch|alert|require|React|\$) is not defined)");
        if (std::regex_search(r.err, benign)) { o["ran"] = Json(false); o["not_run"] = Json("needs a browser or packages that are not installed here"); o.erase("exit"); }
        else { o["ok"] = Json(false); o["stage"] = Json("run"); o["errors"] = Json(truncate(trim(r.err.empty() ? r.out : r.err), 6000)); }
    }
    send_json(fd, 200, Json(std::move(o)));
}

// ---------- feedback ----------
std::mutex g_feedback_mu;
void handle_feedback(int fd, const Json& body) {
    const std::string kind = get_string(body, "kind", "bad_answer");
    if (kind != "bad_answer" && kind != "good_answer" && kind != "fixed") { send_json(fd, 400, error_json("Unknown feedback type.")); return; }
    Json::Object e;
    e["at"] = Json(static_cast<double>(now_ms()));
    e["kind"] = Json(kind);
    e["question"] = Json(truncate(get_string(body, "question"), 8000));
    e["answer"] = Json(truncate(get_string(body, "answer"), 16000));
    e["model"] = Json(truncate(get_string(body, "model"), 200));
    e["mode"] = Json(truncate(get_string(body, "mode"), 40));
    e["note"] = Json(truncate(get_string(body, "note"), 2000));
    e["lang"] = Json(truncate(get_string(body, "lang"), 40));
    const bool share = json_bool(body, "share", false);
    e["shared"] = Json(share);
    const std::string line = dump_json(Json(e)) + "\n";
    {
        std::lock_guard lk(g_feedback_mu);
        const fs::path p = data_path(".astro_feedback.jsonl");
        std::error_code ec;
        if (fs::exists(p, ec) && fs::file_size(p, ec) > 5'000'000) fs::rename(p, data_path(".astro_feedback.old.jsonl"), ec);
        std::ofstream out(p, std::ios::app | std::ios::binary);
        out << line;
    }
    Json::Object o; o["ok"] = Json(true); o["saved"] = Json(true); o["shared"] = Json(false);
    if (share) {
        // Opt-in only: an insert-only table (no one can read it back with the public key).
        const char* off = std::getenv("ASTRO_FEEDBACK_SHARE");
        if (!(off && std::string(off) == "0")) {
            const char* u = std::getenv("ASTRO_FEEDBACK_URL");
            const std::string url = u && *u ? u : "https://vgtucdfretcvojcfvbzk.supabase.co/rest/v1/astro_feedback";
            Json::Object row;
            row["kind"] = e["kind"]; row["question"] = e["question"]; row["answer"] = e["answer"]; row["model"] = e["model"];
            row["mode"] = e["mode"]; row["note"] = e["note"]; row["lang"] = e["lang"]; row["app_version"] = Json(truncate(get_string(body, "app_version", ASTRO_APP_VERSION), 20));
            const std::string post = dump_json(Json(std::move(row)));
            const FetchResult r = http_fetch(url, {"Content-Type: application/json", "Prefer: return=minimal",
                                                   "apikey: sb_publishable_E2ZGJpsa3Bqbunemq7qUPw_W3g5LSol"}, &post, 15, 100000, true);
            o["shared"] = Json(r.error.empty() && r.status >= 200 && r.status < 300);
            if (!(r.error.empty() && r.status >= 200 && r.status < 300)) o["share_error"] = Json(r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        }
    }
    send_json(fd, 200, Json(std::move(o)));
}

// ---------- opt-in reports: errors, crashes, "Send feedback" (off by default) ----------
// The engine never sends anything on its own. The app calls POST /api/report only when the user turned on
// "Send anonymous error reports" (Settings -> Privacy) or pressed Send in the feedback form. Text is scrubbed of
// keys, tokens, e-mail addresses and personal paths before it leaves the computer. Rows go to an insert-only table.
// ASTRO_REPORTS=off disables sending entirely (for example in managed installs); ASTRO_REPORTS_URL overrides the target.
std::string scrub_report(std::string t) {
    static const std::regex keys(R"RX((sk|pk|rk|sb_secret|sb_publishable|ghp|gho|ghu|ghs|ghr|github_pat|xox[abprs]|AIza|hf|gsk|csk|nvapi|or-v1)[-_][-_A-Za-z0-9]{10,})RX");
    static const std::regex jwt(R"RX(eyJ[-_A-Za-z0-9]{10,}\.[-_A-Za-z0-9]{10,}\.[-_A-Za-z0-9]{10,})RX");
    static const std::regex bearer(R"RX((authorization|bearer|api[-_ ]?key|token|password|secret)(["'\s:=]+)(bearer\s+|basic\s+)?[^\s"',;]{6,})RX", std::regex::icase);
    static const std::regex longtok(R"RX([A-Za-z0-9_\-]{40,})RX");
    static const std::regex email(R"RX([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})RX");
    static const std::regex home_nix(R"RX(/(home|Users)/[^/\s"']+)RX");
    static const std::regex home_win(R"RX([A-Za-z]:\\(Users|Documents and Settings)\\[^\\\s"']+)RX", std::regex::icase);
    t = std::regex_replace(t, jwt, "[redacted-token]");
    t = std::regex_replace(t, keys, "[redacted-key]");
    t = std::regex_replace(t, bearer, "$1$2[redacted]");
    t = std::regex_replace(t, longtok, "[redacted]");
    t = std::regex_replace(t, email, "[email]");
    t = std::regex_replace(t, home_nix, "~");
    t = std::regex_replace(t, home_win, "~");
    return t;
}
std::string report_platform() {
#if defined(_WIN32)
    std::string os = "windows";
#elif defined(__APPLE__)
    std::string os = "macos";
#elif defined(__ANDROID__)
    std::string os = "android";
#else
    std::string os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    return os + "-arm64";
#else
    return os + "-x64";
#endif
}
bool reports_disabled() { const char* v = std::getenv("ASTRO_REPORTS"); return v && (std::string(v) == "off" || std::string(v) == "0"); }

// Crash marker: written by a fatal-signal handler, picked up at the next start. Sent only if the user opted in.
char g_crash_path[1024] = {0};
void astro_crash_handler(int sig) {
    if (g_crash_path[0]) {
        if (std::FILE* f = std::fopen(g_crash_path, "ab")) {
            std::fprintf(f, "{\"signal\":%d,\"version\":\"%s\",\"at\":%lld}\n", sig, ASTRO_APP_VERSION, static_cast<long long>(std::time(nullptr)));
            std::fclose(f);
        }
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
void install_crash_marker() {
    const std::string p = data_path(".astro_crash.jsonl");
    if (p.size() < sizeof(g_crash_path)) std::memcpy(g_crash_path, p.c_str(), p.size() + 1);
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(s, astro_crash_handler);
#ifndef _WIN32
    std::signal(SIGBUS, astro_crash_handler);
#endif
}
const char* signal_name(int s) {
    switch (s) { case SIGSEGV: return "segmentation fault"; case SIGABRT: return "abort"; case SIGFPE: return "arithmetic error"; case SIGILL: return "illegal instruction";
#ifndef _WIN32
    case SIGBUS: return "bus error";
#endif
    default: return "fatal signal"; }
}

double rep_num(const Json& j, std::string_view k) { const Json* v = j.get(k); const double* d = v ? std::get_if<double>(&v->value) : nullptr; return d ? *d : 0; }
void handle_report(int fd, const Json& body) {
    const std::string kind = get_string(body, "kind");
    if (kind != "error" && kind != "crash" && kind != "feedback") { send_json(fd, 400, error_json("Unknown report type.")); return; }
    if (reports_disabled()) { Json::Object o; o["ok"] = Json(false); o["error"] = Json("Reports are turned off on this computer (ASTRO_REPORTS=off)."); send_json(fd, 200, Json(std::move(o))); return; }
    Json::Object row;
    row["kind"] = Json(kind);
    row["category"] = Json(truncate(scrub_report(get_string(body, "category")), 40));
    const std::string message = truncate(scrub_report(get_string(body, "message")), 4000);
    row["message"] = Json(message);
    row["detail"] = Json(truncate(scrub_report(get_string(body, "detail")), 8000));
    // A contact address is only kept when the user typed it into the feedback form themselves.
    row["contact"] = Json(kind == "feedback" ? truncate(get_string(body, "contact"), 200) : std::string());
    row["platform"] = Json(truncate(get_string(body, "platform", report_platform()), 80));
    row["app_version"] = Json(std::string(ASTRO_APP_VERSION));
    row["lang"] = Json(truncate(get_string(body, "lang"), 40));
    const std::string src = get_string(body, "source", "app");
    row["source"] = Json(src == "desktop" || src == "android" || src == "cli" ? src : std::string("app"));
    if (trim(message).empty()) { send_json(fd, 400, error_json("The report is empty.")); return; }
    if (json_bool(body, "dry_run", false)) { Json::Object o; o["ok"] = Json(true); o["dry_run"] = Json(true); o["sent"] = Json(row); send_json(fd, 200, Json(std::move(o))); return; }
    const char* u = std::getenv("ASTRO_REPORTS_URL");
    const std::string url = u && *u ? u : "https://vgtucdfretcvojcfvbzk.supabase.co/rest/v1/astro_reports";
    const std::string post = dump_json(Json(row));
    const FetchResult r = http_fetch(url, {"Content-Type: application/json", "Prefer: return=minimal",
                                           "apikey: sb_publishable_E2ZGJpsa3Bqbunemq7qUPw_W3g5LSol"}, &post, 15, 100000, true);
    const bool ok = r.error.empty() && r.status >= 200 && r.status < 300;
    Json::Object o; o["ok"] = Json(ok);
    if (!ok) o["error"] = Json(r.error.empty() ? (r.status == 400 && r.body.find("Too many") != std::string::npos ? std::string("Too many reports right now, please try again later.") : "HTTP " + std::to_string(r.status)) : r.error);
    send_json(fd, 200, Json(std::move(o)));
}
// GET: crash markers from earlier runs (the app decides whether to send them); POST /api/report/clear removes them.
void handle_report_pending(int fd) {
    Json::Array items;
    std::ifstream in(data_path(".astro_crash.jsonl"), std::ios::binary);
    std::string line; int n = 0;
    while (std::getline(in, line) && n < 5) {
        Json j; try { j = JsonParser(line).parse(); } catch (...) { continue; }
        Json::Object it;
        const int sig = static_cast<int>(rep_num(j, "signal"));
        it["message"] = Json(std::string("The A.S.T.R.O AI engine stopped unexpectedly (") + signal_name(sig) + ")");
        it["version"] = Json(get_string(j, "version"));
        it["at"] = Json(rep_num(j, "at"));
        items.push_back(Json(std::move(it))); ++n;
    }
    Json::Object o; o["crashes"] = Json(std::move(items)); o["disabled"] = Json(reports_disabled());
    send_json(fd, 200, Json(std::move(o)));
}

// ---------------------------------------------------------------- Pro license (scaffolding)
// Pro is not on sale yet, and nothing in the app is locked behind it. This is only the plumbing so a license bought later
// (Lemon Squeezy or Paddle webhook -> astro_licenses table) can be entered and checked. Only the key's SHA-256 hash is
// stored server side; the key itself stays in <data>/.astro_license.json. Offline grace: a valid check counts for 30 days.
std::string license_mask(const std::string& k) { return k.size() <= 8 ? std::string("****") : k.substr(0, 4) + "…" + k.substr(k.size() - 4); }
Json license_remote_check(const std::string& key, std::string& err) {
    const char* u = std::getenv("ASTRO_LICENSE_URL");
    const std::string url = u && *u ? u : "https://vgtucdfretcvojcfvbzk.supabase.co/rest/v1/rpc/astro_license_check";
    Json::Object b; b["p_key"] = Json(key);
    const std::string post = dump_json(Json(std::move(b)));
    const FetchResult r = http_fetch(url, {"Content-Type: application/json", "apikey: sb_publishable_E2ZGJpsa3Bqbunemq7qUPw_W3g5LSol"}, &post, 15, 100000, true);
    if (!r.error.empty()) { err = r.error; return Json(); }
    if (r.status < 200 || r.status >= 300) { err = "HTTP " + std::to_string(r.status); return Json(); }
    try { return JsonParser(r.body).parse(); } catch (...) { err = "Bad reply from the license server."; return Json(); }
}
Json license_state(bool recheck) {
    Json saved; { std::ifstream in(data_path(".astro_license.json"), std::ios::binary); std::stringstream ss; ss << in.rdbuf(); try { saved = JsonParser(ss.str()).parse(); } catch (...) {} }
    Json::Object o; o["available"] = Json(false); o["note"] = Json(std::string("Pro is not on sale yet. Everything in the app is free."));
    const std::string key = get_string(saved, "key");
    if (key.empty()) { o["active"] = Json(false); return Json(std::move(o)); }
    const double now = static_cast<double>(std::time(nullptr));
    double checked = rep_num(saved, "checked_at");
    bool valid = json_bool(saved, "valid", false);
    std::string plan = get_string(saved, "plan"), reason = get_string(saved, "reason");
    if (recheck && now - checked > 7 * 86400) {
        std::string err; Json j = license_remote_check(key, err);
        if (err.empty()) {
            valid = json_bool(j, "valid", false); plan = get_string(j, "plan", plan); reason = get_string(j, "reason"); checked = now;
            Json::Object w; w["key"] = Json(key); w["valid"] = Json(valid); w["plan"] = Json(plan); w["reason"] = Json(reason); w["checked_at"] = Json(checked);
            std::ofstream(data_path(".astro_license.json"), std::ios::binary | std::ios::trunc) << dump_json(Json(std::move(w)));
        }
    }
    o["active"] = Json(valid && now - checked < 30 * 86400);
    o["plan"] = Json(plan); o["key"] = Json(license_mask(key)); o["checked_at"] = Json(checked);
    if (!reason.empty()) o["reason"] = Json(reason);
    return Json(std::move(o));
}
void handle_license(int fd, const HttpRequest& request) {
    if (request.method == "GET") { send_json(fd, 200, license_state(true)); return; }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use GET or POST.")); return; }
    if (request.path == "/api/license/remove") { std::error_code ec; fs::remove(data_path(".astro_license.json"), ec); send_json(fd, 200, license_state(false)); return; }
    const auto body = parse_request_json(request, fd);
    if (!body) return;
    const std::string key = trim(get_string(*body, "key"));
    if (key.size() < 16 || key.size() > 200) { send_json(fd, 400, error_json("That doesn't look like a license key.")); return; }
    std::string err; Json j = license_remote_check(key, err);
    if (!err.empty()) { send_json(fd, 502, error_json("Couldn't reach the license server: " + err)); return; }
    const bool valid = json_bool(j, "valid", false);
    if (!valid) {
        const std::string why = get_string(j, "reason");
        send_json(fd, 200, error_json(why == "unknown" || why == "malformed" ? "This license key wasn't found." : "This license is " + why + ".")); return;
    }
    Json::Object w; w["key"] = Json(key); w["valid"] = Json(true); w["plan"] = Json(get_string(j, "plan", "pro")); w["checked_at"] = Json(static_cast<double>(std::time(nullptr)));
    std::ofstream(data_path(".astro_license.json"), std::ios::binary | std::ios::trunc) << dump_json(Json(std::move(w)));
    send_json(fd, 200, license_state(false));
}

bool handle_quality_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path == "/api/models/health" && request.method == "GET") {
        Json j;
        { std::lock_guard lk(g_mstat_mu); model_stats_load_locked(); j = model_stats_json_locked(); }
        if (auto* o = std::get_if<Json::Object>(&j.value)) (*o)["skipped"] = dead_json();
        send_json(fd, 200, j);
        return true;
    }
    if (path == "/api/sandbox") {
        if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
        if (request.method == "POST") {
            const auto body = parse_request_json(request, fd);
            if (!body) return true;
            SandboxCfg c = sandbox_cfg();
            if (body->get("on")) c.on = json_bool(*body, "on", c.on);
            if (body->get("net")) c.net = json_bool(*body, "net", c.net);
            Json::Object w; w["on"] = Json(c.on); w["net"] = Json(c.net);
            { std::lock_guard lk(g_sandbox_mu); write_file_private((fs::path(data_dir()) / ".astro_sandbox.json").string(), dump_json(Json(std::move(w)))); }
        }
        send_json(fd, 200, sandbox_status_json());
        return true;
    }
    if (path == "/api/license" || path == "/api/license/remove") {
        if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
        try { handle_license(fd, request); } catch (const std::exception& e) { send_json(fd, 500, error_json(e.what())); }
        return true;
    }
    if (path == "/api/report/pending" || path == "/api/report/clear" || path == "/api/report") {
        if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
        if (path == "/api/report/pending") { handle_report_pending(fd); return true; }
        if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
        if (path == "/api/report/clear") { std::error_code ec; fs::remove(data_path(".astro_crash.jsonl"), ec); Json::Object o; o["ok"] = Json(true); send_json(fd, 200, Json(std::move(o))); return true; }
        const auto body = parse_request_json(request, fd);
        if (!body) return true;
        try { handle_report(fd, *body); } catch (const std::exception& e) { send_json(fd, 500, error_json(e.what())); }
        return true;
    }
    if (path != "/api/check/code" && path != "/api/feedback") return false;
    if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
    const auto body = parse_request_json(request, fd);
    if (!body) return true;
    try {
        if (path == "/api/check/code") handle_check_code(fd, *body);
        else handle_feedback(fd, *body);
    } catch (const std::exception& e) { send_json(fd, 500, error_json(e.what())); }
    return true;
}
