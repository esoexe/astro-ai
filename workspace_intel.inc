// =============================================================================================
// Workspace intelligence (included from workspace_ide.inc, just before the /api/ide router):
//   * repository index: symbols (functions, classes, types, routes) + file dependencies
//                                                       POST /api/ide/index   {project}
//   * git pipeline: status, diff, show, commit, log, branches, checkout, init
//                                                       POST /api/ide/git     {project, op, ...}
// Git runs with fork/exec and an argv array (no shell), inside the project folder only. A
// GIT_CEILING_DIRECTORIES guard stops git from ever climbing into a parent repository.
// =============================================================================================

std::string upper_ascii(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }

// ---------- repository index ----------
struct SymRule { const char* kind; const char* hint; std::regex re; };
struct LangRules { const char* lang; std::vector<SymRule> sym; std::vector<std::pair<const char*, std::regex>> dep; };

const LangRules* index_rules_for(const std::string& ext) {
    constexpr auto F = std::regex::ECMAScript | std::regex::optimize;
    static const LangRules js{"javascript", {
        {"function", "function", std::regex(R"~(^\s*(?:export\s+)?(?:default\s+)?(?:async\s+)?function\s*\*?\s*([A-Za-z_$][\w$]*))~", F)},
        {"class", "class", std::regex(R"~(^\s*(?:export\s+)?(?:default\s+)?(?:abstract\s+)?class\s+([A-Za-z_$][\w$]*))~", F)},
        {"function", "=>", std::regex(R"~(^\s*(?:export\s+)?(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*(?::[^=]+)?=\s*(?:async\s*)?(?:\([^)]*\)|[A-Za-z_$][\w$]*)\s*(?::[^=]+)?=>)~", F)},
        {"type", "interface", std::regex(R"~(^\s*(?:export\s+)?(?:declare\s+)?interface\s+([A-Za-z_$][\w$]*))~", F)},
        {"type", "type", std::regex(R"~(^\s*(?:export\s+)?type\s+([A-Za-z_$][\w$]*)\s*(?:<[^>]*>)?\s*=)~", F)},
        {"type", "enum", std::regex(R"~(^\s*(?:export\s+)?(?:const\s+)?enum\s+([A-Za-z_$][\w$]*))~", F)},
        {"route", ".", std::regex(R"~(\b(?:app|router|server|fastify)\.(get|post|put|delete|patch|all)\s*\(\s*['"`]([^'"`]+))~", F)},
    }, {
        {"from", std::regex(R"~(\bfrom\s*['"]([^'"]+)['"])~", F)},
        {"import", std::regex(R"~(^\s*import\s*['"]([^'"]+)['"])~", F)},
        {"require", std::regex(R"~(\brequire\s*\(\s*['"]([^'"]+)['"]\s*\))~", F)},
        {"import(", std::regex(R"~(\bimport\s*\(\s*['"]([^'"]+)['"]\s*\))~", F)},
    }};
    static const LangRules py{"python", {
        {"function", "def", std::regex(R"~(^(\s*)(?:async\s+)?def\s+([A-Za-z_]\w*))~", F)},
        {"class", "class", std::regex(R"~(^\s*class\s+([A-Za-z_]\w*))~", F)},
        {"route", "@", std::regex(R"~(^\s*@\w+\.(route|get|post|put|delete|patch)\(\s*['"]([^'"]+))~", F)},
    }, {
        {"import", std::regex(R"~(^\s*import\s+([\w.]+))~", F)},
        {"from", std::regex(R"~(^\s*from\s+([\w.]+)\s+import\b)~", F)},
    }};
    static const LangRules cpp{"c/c++", {
        {"class", "class", std::regex(R"~(^\s*(?:template\s*<[^>]*>\s*)?class\s+([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^;{]*)?(?:\{.*)?$)~", F)},
        {"class", "struct", std::regex(R"~(^\s*(?:typedef\s+)?struct\s+([A-Za-z_]\w*)\s*(?::[^;{]*)?(?:\{.*)?$)~", F)},
        {"type", "enum", std::regex(R"~(^\s*enum\s+(?:class\s+)?([A-Za-z_]\w*))~", F)},
        {"function", "(", std::regex(R"~(^(?:[A-Za-z_][\w:<>,\s\*&]*?[\s\*&]+)([A-Za-z_~][\w]*(?:::[A-Za-z_~][\w]*)*)\s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:->\s*[\w:<>\s\*&]+)?(?:\{.*)?$)~", F)},
        {"macro", "#", std::regex(R"~(^\s*#\s*define\s+([A-Za-z_]\w*))~", F)},
    }, {
        {"include", std::regex(R"~(^\s*#\s*include\s*"([^"]+)")~", F)},
    }};
    static const LangRules html{"html", {
        {"title", "<title", std::regex(R"~(<title>([^<]{1,80})</title>)~", F)},
        {"canvas", "<canvas", std::regex(R"~(<canvas[^>]*\bid=["']([^"']+))~", F)},
    }, {
        {"script", std::regex(R"~(<script[^>]*\bsrc=["']([^"']+)["'])~", F)},
        {"link", std::regex(R"~(<link[^>]*\bhref=["']([^"']+\.css[^"']*)["'])~", F)},
        {"module", std::regex(R"~(\bfrom\s*['"]([^'"]+)['"])~", F)},
    }};
    static const LangRules css{"css", {
        {"var", "--", std::regex(R"~((?:^|[\s{;])(--[\w-]+)\s*:)~", F)},
        {"keyframes", "@keyframes", std::regex(R"~(@keyframes\s+([\w-]+))~", F)},
    }, {
        {"import", std::regex(R"~(@import\s+(?:url\()?\s*['"]([^'"]+)['"])~", F)},
    }};
    static const LangRules glsl{"glsl", {
        {"function", "(", std::regex(R"~(^\s*(?:void|float|int|bool|vec[234]|mat[234]|ivec[234])\s+([A-Za-z_]\w*)\s*\([^;]*\)\s*(?:\{.*)?$)~", F)},
        {"uniform", "uniform", std::regex(R"~(^\s*uniform\s+\w+\s+([A-Za-z_]\w*))~", F)},
    }, {}};
    static const LangRules java{"java/c#", {
        {"class", "class", std::regex(R"~(^\s*(?:(?:public|private|protected|internal|static|abstract|final|sealed|partial)\s+)*(?:class|interface|record|enum)\s+([A-Za-z_]\w*))~", F)},
        {"function", "(", std::regex(R"~(^\s*(?:(?:public|private|protected|internal|static|final|abstract|override|virtual|async|synchronized)\s+)+[\w<>\[\],\s]+?\s+([A-Za-z_]\w*)\s*\([^;]*\)\s*(?:throws[^{]*)?(?:\{.*)?$)~", F)},
    }, {
        {"import", std::regex(R"~(^\s*(?:import|using)\s+(?:static\s+)?([\w.]+)\s*;)~", F)},
    }};
    static const LangRules go{"go", {
        {"function", "func", std::regex(R"~(^func\s+(?:\([^)]*\)\s*)?([A-Za-z_]\w*))~", F)},
        {"type", "type", std::regex(R"~(^type\s+([A-Za-z_]\w*))~", F)},
    }, {
        {"import", std::regex(R"~(^\s*(?:import\s+)?(?:\w+\s+)?"([^"]+)"\s*$)~", F)},
    }};
    static const LangRules rs{"rust", {
        {"function", "fn", std::regex(R"~(^\s*(?:pub(?:\([^)]*\))?\s+)?(?:async\s+)?(?:unsafe\s+)?fn\s+([A-Za-z_]\w*))~", F)},
        {"type", "struct", std::regex(R"~(^\s*(?:pub\s+)?(?:struct|enum|trait)\s+([A-Za-z_]\w*))~", F)},
    }, {
        {"mod", std::regex(R"~(^\s*(?:pub\s+)?mod\s+([A-Za-z_]\w*)\s*;)~", F)},
        {"use", std::regex(R"~(^\s*use\s+(?:crate::)?([\w:]+))~", F)},
    }};
    static const LangRules sh{"shell", {
        {"function", "(", std::regex(R"~(^\s*(?:function\s+)?([A-Za-z_][\w-]*)\s*\(\)\s*\{?)~", F)},
    }, {
        {"source", std::regex(R"~(^\s*(?:source|\.)\s+["']?([\w./-]+))~", F)},
    }};
    static const LangRules asmr{"assembly", {
        {"label", ":", std::regex(R"~(^([A-Za-z_.$][\w.$]*):)~", F)},
    }, {}};
    static const std::map<std::string, const LangRules*> by_ext = {
        {".js", &js}, {".mjs", &js}, {".cjs", &js}, {".jsx", &js}, {".ts", &js}, {".tsx", &js}, {".mts", &js}, {".vue", &js}, {".svelte", &js},
        {".py", &py}, {".c", &cpp}, {".h", &cpp}, {".cc", &cpp}, {".cpp", &cpp}, {".cxx", &cpp}, {".hpp", &cpp}, {".hh", &cpp}, {".inc", &cpp},
        {".html", &html}, {".htm", &html}, {".css", &css}, {".scss", &css}, {".glsl", &glsl}, {".vert", &glsl}, {".frag", &glsl},
        {".java", &java}, {".cs", &java}, {".kt", &java}, {".go", &go}, {".rs", &rs}, {".sh", &sh}, {".bash", &sh}, {".s", &asmr}, {".asm", &asmr}};
    const auto it = by_ext.find(ext);
    return it == by_ext.end() ? nullptr : it->second;
}

bool cpp_keyword_name(const std::string& n) {
    static const std::set<std::string> kw = {"if", "for", "while", "switch", "return", "sizeof", "catch", "else", "do", "case", "new", "delete", "defined"};
    return kw.count(n) > 0;
}

void ide_index(int fd, const fs::path& base) {
    Json::Array files;
    std::size_t total_lines = 0, total_symbols = 0, total_bytes = 0, budget = 120'000;   // line budget keeps big repos fast
    std::map<std::string, std::size_t> langs;
    walk_project(base, [&](const std::string& rel, const fs::path& abs) {
        std::error_code ec;
        const auto size = fs::file_size(abs, ec);
        if (ec) return;
        const std::string ext = lower_copy(fs::path(rel).extension().string());
        const LangRules* rules = index_rules_for(ext);
        Json::Object f;
        f["path"] = Json(rel);
        f["bytes"] = Json(static_cast<double>(size));
        total_bytes += size;
        if (!rules || size > 768 * 1024 || budget == 0) {
            if (rules) f["lang"] = Json(std::string(rules->lang));
            files.push_back(Json(std::move(f)));
            return;
        }
        const auto text = read_small(abs, 768 * 1024);
        if (!text || is_binary_blob(*text)) { files.push_back(Json(std::move(f))); return; }
        Json::Array syms, deps;
        std::set<std::string> seen_deps;
        std::istringstream in(*text);
        std::string line;
        std::size_t n = 0;
        const bool is_py = std::string(rules->lang) == "python", is_cpp = std::string(rules->lang) == "c/c++";
        while (std::getline(in, line) && budget > 0) {
            ++n; --budget;
            if (line.empty() || line.size() > 8000) continue;   // very long lines are minified bundles
            const bool long_line = line.size() > 600;           // long lines: dependencies only (one-line HTML), no symbols
            std::smatch m;
            for (const auto& r : rules->sym) {
                if (syms.size() >= 300 || long_line) break;
                if (r.hint[0] && line.find(r.hint) == std::string::npos) continue;
                if (!std::regex_search(line, m, r.re)) continue;
                std::string name, kind = r.kind;
                if (std::string(r.kind) == "route") name = upper_ascii(m[1].str()) + " " + m[2].str();
                else if (is_py && kind == "function") { name = m[2].str(); if (!m[1].str().empty()) kind = "method"; }
                else name = m[1].str();
                if (is_cpp && kind == "function") {
                    const auto leaf = name.substr(name.rfind(':') == std::string::npos ? 0 : name.rfind(':') + 1);
                    if (cpp_keyword_name(leaf) || line.find_first_not_of(" \t") != 0) continue;   // only top-level definitions
                }
                Json::Object s;
                s["k"] = Json(kind); s["n"] = Json(name); s["l"] = Json(static_cast<double>(n));
                syms.push_back(Json(std::move(s)));
                break;
            }
            for (const auto& [kind, re] : rules->dep) {   // every match on the line (HTML often has several)
                int per_line = 0;
                for (std::sregex_iterator it(line.begin(), line.end(), re), end; it != end && per_line < 24; ++it, ++per_line) {
                    const std::string d = (*it)[1].str();
                    if (!d.empty() && seen_deps.insert(d).second && deps.size() < 120) deps.push_back(Json(d));
                }
            }
        }
        total_lines += n;
        total_symbols += syms.size();
        langs[rules->lang] += n;
        f["lang"] = Json(std::string(rules->lang));
        f["lines"] = Json(static_cast<double>(n));
        f["symbols"] = Json(std::move(syms));
        f["deps"] = Json(std::move(deps));
        files.push_back(Json(std::move(f)));
    }, 4000);
    Json::Object lang_obj;
    for (const auto& [k, v] : langs) lang_obj[k] = Json(static_cast<double>(v));
    Json::Object o;
    o["files"] = Json(std::move(files));
    o["lines"] = Json(static_cast<double>(total_lines));
    o["symbols"] = Json(static_cast<double>(total_symbols));
    o["bytes"] = Json(static_cast<double>(total_bytes));
    o["languages"] = Json(std::move(lang_obj));
    o["truncated"] = Json(budget == 0);
    send_json(fd, 200, Json(std::move(o)));
}

// ---------- run a program and capture its output (no shell) ----------
struct Captured { int code = -1; std::string out, err; bool timed_out = false; };

Captured run_capture(const std::vector<std::string>& args, const fs::path& cwd, const std::vector<std::string>& extra_env,
                     int timeout_s = 20, std::size_t cap = 2'000'000) {
    Captured r;
#ifdef _WIN32
    {
        if (args.empty()) { r.err = "nothing to run"; return r; }
        std::vector<std::string> a = args;
        if (a[0] == "/bin/bash" || a[0] == "bash") {
            a[0] = winp::bash_path();
            if (a[0].empty()) { r.code = 127; r.err = "This needs Git for Windows (it provides bash). Install it from https://git-scm.com and restart A.S.T.R.O AI."; return r; }
        } else if (fs::path(a[0]).is_relative()) {
            const std::string w = winp::which(a[0]);
            if (w.empty()) { r.code = 127; r.err = a[0] + ": command not found"; return r; }
            a[0] = w;
        }
        const WinRunResult w = win_run(winp::join_args(a), cwd.string(), ide_env(cwd, extra_env), "", timeout_s,
            [&](int i, const std::string& data) {
                std::string& dst = i == 0 ? r.out : r.err;
                if (dst.size() < cap) dst.append(data, 0, std::min(data.size(), cap - dst.size()));
                return true;
            },
            [] { return false; }, 400);
        if (!w.started) { r.code = 127; r.err = w.error; return r; }
        r.timed_out = w.timed_out;
        r.code = w.code;
        return r;
    }
#else
    int out_pipe[2], err_pipe[2];
    if (pipe_cloexec(out_pipe) != 0) { r.err = "pipe failed"; return r; }
    if (pipe_cloexec(err_pipe) != 0) { close(out_pipe[0]); close(out_pipe[1]); r.err = "pipe failed"; return r; }
    const std::vector<std::string> env = ide_env(cwd, extra_env);
    std::vector<char*> argv, envp;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    for (const auto& e : env) envp.push_back(const_cast<char*>(e.c_str()));
    envp.push_back(nullptr);
    const std::string cwd_s = cwd.string();
    const pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(out_pipe[1], 1); dup2(err_pipe[1], 2);
        for (int f = 3; f < 4096; ++f) close(f);
        if (chdir(cwd_s.c_str()) != 0) _exit(127);
#if defined(__APPLE__)
        ::environ = envp.data();   // the global one declared in platform.h (a local extern here would get internal linkage)
        execvp(argv[0], argv.data());
#else
        execvpe(argv[0], argv.data(), envp.data());
#endif
        _exit(127);
    }
    close(out_pipe[1]); close(err_pipe[1]);
    if (pid < 0) { close(out_pipe[0]); close(err_pipe[0]); r.err = "fork failed"; return r; }
    pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
    int open_pipes = 2;
    const auto started = std::chrono::steady_clock::now();
    char buf[16384];
    int status = 0; bool reaped = false;
    std::chrono::steady_clock::time_point leader_done{};
    while (open_pipes > 0) {
        const int ready = poll(fds, 2, 200);
        if (ready < 0 && errno != EINTR) break;
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            const ssize_t got = read(fds[i].fd, buf, sizeof(buf));
            if (got <= 0) { close(fds[i].fd); fds[i].fd = -1; --open_pipes; continue; }
            std::string& dst = i == 0 ? r.out : r.err;
            if (dst.size() < cap) dst.append(buf, static_cast<std::size_t>(std::min<ssize_t>(got, static_cast<ssize_t>(cap - dst.size()))));
        }
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(timeout_s)) { r.timed_out = true; break; }
        // The shell exited but a background child still holds the pipe: drain briefly, then stop waiting.
        if (!reaped && waitpid(pid, &status, WNOHANG) == pid) { reaped = true; leader_done = std::chrono::steady_clock::now(); }
        if (reaped && std::chrono::steady_clock::now() - leader_done > std::chrono::milliseconds(400)) break;
    }
    // Output closed: wait for the real exit (bounded by the timeout) before cleaning up the process group,
    // otherwise a shell that is still exiting gets SIGKILLed and reports -1.
    while (!r.timed_out && !reaped) {
        const pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) { reaped = true; break; }
        if (w < 0 && errno != EINTR) break;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(timeout_s)) { r.timed_out = true; break; }
        usleep(3000);
    }
    kill(-pid, SIGKILL);  // background stragglers in the group
    for (auto& f : fds) if (f.fd >= 0) close(f.fd);
    if (!reaped) waitpid(pid, &status, 0);
    r.code = r.timed_out ? -1 : (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return r;
#endif
}

// ---------- git pipeline ----------
bool valid_branch_name(const std::string& b) {
    static const std::regex re(R"~(^[A-Za-z0-9_][A-Za-z0-9_./-]{0,99}$)~");
    return std::regex_match(b, re) && b.find("..") == std::string::npos && b.find("//") == std::string::npos &&
           b.back() != '/' && b.back() != '.' && !(b.size() >= 5 && b.substr(b.size() - 5) == ".lock");
}

std::string clean_identity(std::string s, const std::string& fallback) {
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == '<' || c == '>'; }), s.end());
    s = trim(s);
    return s.empty() ? fallback : s.substr(0, 80);
}

void ide_git(int fd, const fs::path& base, const Json& body) {
    if (!have_tool("git")) { send_json(fd, 501, error_json("git is not installed on this server.")); return; }
    const std::string op = get_string(body, "op");
    std::error_code ec;
    const bool repo = fs::exists(base / ".git", ec);
    const std::vector<std::string> env = {
        "GIT_CEILING_DIRECTORIES=" + fs::path(projects_root()).string(), "GIT_TERMINAL_PROMPT=0", "GIT_PAGER=cat", "PAGER=cat",
        "GIT_OPTIONAL_LOCKS=0", "GIT_CONFIG_NOSYSTEM=1"};
    const std::string name = clean_identity(get_string(body, "author"), "A.S.T.R.O AI Workspace");
    const std::string email = clean_identity(get_string(body, "email"), "workspace@astro.local");
    auto git = [&](std::vector<std::string> args, int timeout_s = 20) {
        std::vector<std::string> full = {"git", "-c", "user.name=" + name, "-c", "user.email=" + email, "-c", "core.quotepath=off",
                                         "-c", "color.ui=never", "-c", "init.defaultBranch=main", "-c", "advice.detachedHead=false"};
        full.insert(full.end(), args.begin(), args.end());
        return run_capture(full, base, env, timeout_s);
    };
    auto fail = [&](const Captured& c, const std::string& what) {
        std::string msg = trim(c.err.empty() ? c.out : c.err);
        if (c.timed_out) msg = "git timed out.";
        send_json(fd, 422, error_json(what + (msg.empty() ? "" : ": " + msg.substr(0, 2000))));
    };

    if (op == "init") {
        if (repo) { send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}, {"already", Json(true)}})); return; }
        const auto c = git({"init", "-q"});
        if (c.code != 0) { fail(c, "git init failed"); return; }
        if (!fs::exists(base / ".gitignore", ec)) {
            std::ofstream gi(base / ".gitignore");
            gi << "node_modules/\n.build/\n__pycache__/\n.venv/\n*.o\n*.pyc\n.DS_Store\n.env\n";
        }
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}}));
        return;
    }
    if (!repo) {
        if (op == "status") { send_json(fd, 200, Json(Json::Object{{"repo", Json(false)}})); return; }
        send_json(fd, 409, error_json("This project is not a git repository yet. Use “Initialize git” first."));
        return;
    }
    const bool has_head = git({"rev-parse", "--verify", "-q", "HEAD"}).code == 0;

    if (op == "status") {
        const auto c = git({"status", "--porcelain=v1", "-b", "-uall"});
        if (c.code != 0) { fail(c, "git status failed"); return; }
        Json::Array files;
        std::string branch = "main", upstream;
        long ahead = 0, behind = 0;
        std::istringstream in(c.out);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("## ", 0) == 0) {
                std::string b = line.substr(3);
                if (b.rfind("No commits yet on ", 0) == 0) b = b.substr(18);
                const auto dots = b.find("...");
                const auto sp = b.find(' ');
                branch = b.substr(0, std::min(dots, sp));
                std::smatch m;
                static const std::regex a(R"~(ahead (\d+))~"), be(R"~(behind (\d+))~");
                if (std::regex_search(b, m, a)) ahead = std::stol(m[1].str());
                if (std::regex_search(b, m, be)) behind = std::stol(m[1].str());
                continue;
            }
            if (line.size() < 4) continue;
            std::string path = line.substr(3);
            const auto arrow = path.find(" -> ");
            if (arrow != std::string::npos) path = path.substr(arrow + 4);
            if (path.size() >= 2 && path.front() == '"' && path.back() == '"') path = path.substr(1, path.size() - 2);
            Json::Object f;
            f["path"] = Json(path); f["x"] = Json(line.substr(0, 1)); f["y"] = Json(line.substr(1, 1));
            files.push_back(Json(std::move(f)));
            if (files.size() >= 2000) break;
        }
        const auto last = has_head ? git({"log", "-1", "--pretty=format:%h %s"}) : Captured{};
        Json::Object o;
        o["repo"] = Json(true); o["branch"] = Json(branch); o["head"] = Json(has_head);
        o["ahead"] = Json(static_cast<double>(ahead)); o["behind"] = Json(static_cast<double>(behind));
        o["last"] = Json(trim(last.out)); o["files"] = Json(std::move(files));
        send_json(fd, 200, Json(std::move(o)));
        return;
    }
    if (op == "diff") {
        // Whole-tree (or one path) diff against HEAD, including untracked files, so reviews see everything.
        const std::string path = get_string(body, "path");
        if (!path.empty() && !project_path(base.filename().string(), path)) { send_json(fd, 400, error_json("Invalid path.")); return; }
        git({"add", "-A", "-N", "--", path.empty() ? "." : path});   // intent-to-add makes new files show up
        std::vector<std::string> args = {"diff", "--no-ext-diff", "-M"};
        if (body.is_object() && json_bool(body, "stat", false)) args.push_back("--stat");
        if (has_head) args.push_back("HEAD");
        args.push_back("--");
        if (!path.empty()) args.push_back(path);
        const auto c = git(args);
        if (c.code != 0) { fail(c, "git diff failed"); return; }
        Json::Object o;
        o["diff"] = Json(c.out.substr(0, 400'000)); o["truncated"] = Json(c.out.size() > 400'000);
        send_json(fd, 200, Json(std::move(o)));
        return;
    }
    if (op == "show") {
        const std::string path = get_string(body, "path");
        std::string ref = get_string(body, "ref");
        if (ref.empty()) ref = "HEAD";
        static const std::regex ref_re(R"~(^[A-Za-z0-9_./~^-]{1,100}$)~");
        if (path.empty() || !project_path(base.filename().string(), path) || !std::regex_match(ref, ref_re) || ref[0] == '-') {
            send_json(fd, 400, error_json("Invalid path or ref.")); return;
        }
        if (!has_head) { send_json(fd, 200, Json(Json::Object{{"missing", Json(true)}})); return; }
        const auto c = git({"show", ref + ":" + path});
        if (c.code != 0) { send_json(fd, 200, Json(Json::Object{{"missing", Json(true)}})); return; }
        send_json(fd, 200, Json(Json::Object{{"content", Json(c.out)}}));
        return;
    }
    if (op == "commit") {
        std::string message = get_string(body, "message");
        message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
        message = trim(message);
        if (message.empty()) { send_json(fd, 400, error_json("Write a commit message first.")); return; }
        if (message.size() > 8000) message.resize(8000);
        auto c = git({"add", "-A"});
        if (c.code != 0) { fail(c, "git add failed"); return; }
        c = git({"commit", "-q", "-m", message});
        if (c.code != 0) {
            if (c.out.find("nothing to commit") != std::string::npos || c.err.find("nothing to commit") != std::string::npos) {
                send_json(fd, 409, error_json("Nothing to commit — the working tree is clean.")); return;
            }
            fail(c, "git commit failed"); return;
        }
        const auto h = git({"log", "-1", "--pretty=format:%h"});
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}, {"hash", Json(trim(h.out))}}));
        return;
    }
    if (op == "log") {
        if (!has_head) { send_json(fd, 200, Json(Json::Object{{"commits", Json(Json::Array{})}})); return; }
        const long n = std::clamp<long>(json_int(body, "limit", 40), 1, 200);
        const auto c = git({"log", "-n", std::to_string(n), "--pretty=format:%H%x1f%h%x1f%an%x1f%at%x1f%s%x1f%D%x1e", "--shortstat"});
        if (c.code != 0) { fail(c, "git log failed"); return; }
        Json::Array commits;
        std::size_t pos = 0;
        while (pos < c.out.size()) {
            const auto end = c.out.find('\x1e', pos);
            std::string rec = c.out.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? c.out.size() : end + 1;
            // The --shortstat of the previous record lands at the start of this one; attach it to the last commit.
            const auto first_field = rec.find('\x1f');
            if (first_field == std::string::npos) continue;
            std::string head_part = rec;
            const auto nl = rec.find_last_of('\n', first_field);
            if (nl != std::string::npos) {
                const std::string stat = trim(rec.substr(0, nl));
                head_part = rec.substr(nl + 1);
                if (!commits.empty() && !stat.empty()) std::get<Json::Object>(commits.back().value)["stat"] = Json(stat);
            }
            std::vector<std::string> parts;
            std::size_t s = 0;
            for (;;) { const auto e = head_part.find('\x1f', s); parts.push_back(head_part.substr(s, e == std::string::npos ? std::string::npos : e - s)); if (e == std::string::npos) break; s = e + 1; }
            if (parts.size() < 5) continue;
            Json::Object o;
            o["hash"] = Json(trim(parts[0])); o["short"] = Json(parts[1]); o["author"] = Json(parts[2]);
            o["time"] = Json(static_cast<double>(std::atoll(parts[3].c_str()))); o["subject"] = Json(parts[4]);
            if (parts.size() > 5) o["refs"] = Json(trim(parts[5]));
            commits.push_back(Json(std::move(o)));
        }
        // Trailing stat after the final record separator.
        send_json(fd, 200, Json(Json::Object{{"commits", Json(std::move(commits))}}));
        return;
    }
    if (op == "branches") {
        const auto c = git({"branch", "--format=%(HEAD)%(refname:short)"});
        Json::Array list;
        std::string current;
        std::istringstream in(c.out);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            const bool cur = line[0] == '*';
            const std::string b = trim(line.substr(1));
            if (cur) current = b;
            list.push_back(Json(b));
        }
        if (current.empty()) current = trim(git({"symbolic-ref", "--short", "-q", "HEAD"}).out);
        send_json(fd, 200, Json(Json::Object{{"branches", Json(std::move(list))}, {"current", Json(current)}}));
        return;
    }
    if (op == "checkout") {
        const std::string branch = get_string(body, "branch");
        if (!valid_branch_name(branch)) { send_json(fd, 400, error_json("Invalid branch name (letters, digits, _ . / - only).")); return; }
        const bool create = body.is_object() && json_bool(body, "create", false);
        Captured c;
        if (create) c = git({"checkout", "-q", "-b", branch});
        else if (!has_head) c = git({"symbolic-ref", "HEAD", "refs/heads/" + branch});
        else c = git({"checkout", "-q", branch});
        if (c.code != 0) { fail(c, create ? "Could not create branch" : "Could not switch branch"); return; }
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}, {"branch", Json(branch)}}));
        return;
    }
    send_json(fd, 404, error_json("Unknown git operation."));
}
