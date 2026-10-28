// =============================================================================================
// Astro AI code workspace (included inside workspace_server.cpp's anonymous namespace):
//   * projects and files on the server        /api/fs/*          (JSON, owner only)
//   * run a file or a shell command            /api/run           (SSE: out / err / exit)
//   * live preview of web projects             /preview/<key>/<project>/<path>
// Files live in $ASTRO_DATA_DIR/projects/<project>/. Code runs as the server user inside the
// project folder with a clean environment (no API keys), CPU / memory / file-size limits and a
// wall-clock timeout; the whole process group is killed on timeout, on Stop, or when the tab closes.
// Previews are served with a CSP sandbox (opaque origin), so project code cannot call /api/*.
// =============================================================================================
// (sys/resource.h comes from platform.h on Linux/macOS)

namespace fs = std::filesystem;

std::string projects_root() {
    const std::string root = data_dir() + "/projects";
    std::error_code ec; fs::create_directories(root, ec);
    return root;
}

bool valid_project_name(const std::string& n) {
    return !n.empty() && n.size() <= 64 && n[0] != '.' &&
           std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
}

// Resolves a project-relative path. Rejects absolute paths, "..", and symlinks that leave the project.
std::optional<fs::path> project_path(const std::string& project, const std::string& rel, bool allow_root = false) {
    if (!valid_project_name(project)) return std::nullopt;
    if (rel.size() > 400 || rel.find('\0') != std::string::npos || rel.find('\\') != std::string::npos) return std::nullopt;
#ifdef _WIN32
    if (rel.find(':') != std::string::npos) return std::nullopt;   // drive letters / alternate data streams
#endif
    const fs::path base = fs::path(projects_root()) / project;
    fs::path r = fs::path(rel).lexically_normal();
    if (r.is_absolute()) return std::nullopt;
    for (const auto& part : r) if (part == "..") return std::nullopt;
    std::string rs = r.generic_string();
    while (!rs.empty() && rs.back() == '/') rs.pop_back();
    if (rs == ".") rs.clear();
    if (rs.empty() && !allow_root) return std::nullopt;
    const fs::path full = rs.empty() ? base : base / rs;
    std::error_code ec;
    const fs::path canon_base = fs::weakly_canonical(base, ec);
    const fs::path canon = fs::weakly_canonical(full, ec);
    const std::string cb = canon_base.generic_string(), cf = canon.generic_string();
    if (cf != cb && cf.rfind(cb + "/", 0) != 0) return std::nullopt;
    return full;
}

bool skip_dir(const std::string& name) {
    return name == ".git" || name == "node_modules" || name == ".build" || name == "__pycache__" || name == ".venv" || name == ".cache";
}

// Linked projects ("Open folder"): a symlink, or a directory junction on Windows, pointing at a real folder.
// Closing / deleting such a project removes ONLY the link, never the user's files.
void mcp_forget_project(const std::string& project);   // astro_dev.inc
bool project_is_link(const fs::path& p) {
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(p, ec))) return true;
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(p.wstring().c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return false;
#endif
}
bool remove_dir_link(const fs::path& p) {
#ifdef _WIN32
    if (RemoveDirectoryW(p.wstring().c_str())) return true;
    return DeleteFileW(p.wstring().c_str()) != 0;
#else
    std::error_code ec; return fs::remove(p, ec);
#endif
}
fs::path links_file() { return fs::path(data_dir()) / ".astro_linked.json"; }
Json::Object load_links() {
    if (auto t = read_file_text(links_file().string())) { try { Json j = JsonParser(*t).parse(); if (auto* o = j.object_ptr()) return *o; } catch (...) {} }
    return {};
}
void save_links(const Json::Object& o) { write_file_private(links_file().string(), dump_json(Json(o))); }
std::string link_target_of(const std::string& name, const fs::path& link) {
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(link, ec))) { auto t = fs::read_symlink(link, ec); if (!ec) return t.string(); }
    const auto links = load_links();
    if (auto it = links.find(name); it != links.end() && project_is_link(link)) return it->second.string_or();
    return "";
}

void write_starter_project(const fs::path& dir) {
    std::error_code ec; fs::create_directories(dir, ec);
    auto put = [&](const std::string& name, const std::string& text) { std::ofstream(dir / name, std::ios::binary) << text; };
    put("index.html", "<!doctype html>\n<html lang=\"en\">\n<head>\n  <meta charset=\"utf-8\">\n  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n  <title>Hello from Astro AI</title>\n  <link rel=\"stylesheet\" href=\"style.css\">\n</head>\n<body>\n  <main>\n    <h1>Hello, world</h1>\n    <p>Edit <code>index.html</code>, <code>style.css</code> or <code>script.js</code>, or ask the team to build something.</p>\n    <button id=\"count\">Clicked 0 times</button>\n  </main>\n  <script src=\"script.js\"></script>\n</body>\n</html>\n");
    put("style.css", "body {\n  margin: 0; min-height: 100vh; display: grid; place-items: center;\n  font-family: system-ui, sans-serif; background: #0b1020; color: #e8ecff;\n}\nmain { text-align: center; }\nbutton {\n  font: inherit; padding: 10px 18px; border-radius: 999px; border: 0;\n  background: linear-gradient(135deg, #6d8bff, #b06dff); color: white; cursor: pointer;\n}\n");
    put("script.js", "let clicks = 0;\nconst button = document.getElementById('count');\nbutton.addEventListener('click', () => {\n  clicks += 1;\n  button.textContent = `Clicked ${clicks} time${clicks === 1 ? '' : 's'}`;\n});\nconsole.log('Page ready');\n");
    put("main.cpp", "#include <iostream>\n\nint main() {\n    std::cout << \"Hello from C++20!\" << std::endl;\n    for (int i = 1; i <= 5; ++i) std::cout << i << \" squared is \" << i * i << '\\n';\n}\n");
    put("README.md", "# My project\n\nA starter project. Open a file to edit it, press **Run** (Ctrl+Enter) to run it, and see the result on the right.\n\n- `index.html`, `style.css`, `script.js`: a small web page (shown in **Preview**)\n- `main.cpp`: a C++ program (output in **Output**)\n");
}

Json tree_json(const fs::path& base) {
    Json::Array files, dirs;
    std::error_code ec;
    std::size_t count = 0;
    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && count < 3000; it.increment(ec)) {
        if (ec) break;
        const fs::path& p = it->path();
        const std::string name = p.filename().string();
        const std::string rel = fs::relative(p, base, ec).generic_string();
        if (it->is_directory(ec)) {
            if (skip_dir(name)) { it.disable_recursion_pending(); continue; }
            dirs.emplace_back(Json(rel));
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        Json::Object f; f["path"] = Json(rel);
        f["size"] = Json(static_cast<double>(it->file_size(ec)));
        const auto t = fs::last_write_time(p, ec);
        f["mtime"] = Json(static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::file_clock::to_sys(t).time_since_epoch()).count()));
        files.emplace_back(std::move(f));
        ++count;
    }
    Json::Object o; o["files"] = Json(std::move(files)); o["dirs"] = Json(std::move(dirs)); o["truncated"] = Json(count >= 3000);
    return Json(std::move(o));
}

// ---------- running code ----------
std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}

bool have_tool(const std::string& name) {
#ifdef _WIN32
    return !winp::which(name).empty();
#else
    for (const char* dir : {"/usr/local/bin/", "/usr/bin/", "/bin/", "/opt/homebrew/bin/"}) if (access((std::string(dir) + name).c_str(), X_OK) == 0) return true;
    if (const char* path = std::getenv("PATH")) {   // desktop app: tools installed anywhere on PATH
        std::stringstream ss(path); std::string dir;
        while (std::getline(ss, dir, ':')) if (!dir.empty() && access((dir + "/" + name).c_str(), X_OK) == 0) return true;
    }
    return false;
#endif
}
// Python's command name: python3 on Linux/macOS; on Windows usually "python" (or the "py" launcher).
const std::string& python_cmd() {
    static const std::string cmd = [] {
#ifdef _WIN32
        for (const char* c : {"python", "python3", "py"}) if (have_tool(c)) return std::string(c);
#endif
        return std::string("python3");
    }();
    return cmd;
}

// Profiler wrapper (tools/astro_measure.cpp, built by make). Wraps only the program itself, not the compiler,
// so CPU time / peak memory describe the user's code. Empty when the helper is missing (runs still work).
const std::string& measure_prefix() {
    static const std::string prefix = [] {
        std::error_code ec;
#ifdef _WIN32
        (void)ec;
        return std::string();   // CPU/memory profiler is POSIX-only; runs still work without it
#else
        const fs::path p = fs::absolute("astro_measure", ec);
        return (!ec && access(p.c_str(), X_OK) == 0) ? shell_quote(p.string()) + " " : std::string();
#endif
    }();
    return prefix;
}

// Assembly listing for C / C++ (Intel syntax, demangled, noise directives removed).
std::string asm_command_for(const std::string& rel, std::string& error) {
    const std::string ext = lower_copy(fs::path(rel).extension().string());
    const std::string q = shell_quote(rel);
    const std::string clean = " | c++filt | grep -vE '^\\s*\\.(file|ident|section\\s+\\.note|cfi_|p2align|type\\s|size\\s|addrsig)'";
    if (ext == ".c" && have_tool("gcc")) return "gcc -O2 -S -masm=intel -fno-asynchronous-unwind-tables -fno-stack-protector -o - " + q + clean;
    if ((ext == ".cpp" || ext == ".cc" || ext == ".cxx") && have_tool("g++"))
        return "g++ -std=c++20 -O2 -S -masm=intel -fno-asynchronous-unwind-tables -fno-stack-protector -o - " + q + clean;
    error = "Assembly view works for .c and .cpp files.";
    return "";
}

// Command that runs one file, chosen by its extension. Empty + error when the language is unknown.
std::string run_command_for(const std::string& rel, std::string& error) {
    const std::string& M = measure_prefix();
    const fs::path p(rel);
    const std::string ext = lower_copy(p.extension().string());
    const std::string q = shell_quote(rel);
    const std::string out = shell_quote(".build/" + p.stem().string());
    auto need = [&](const std::string& tool, const std::string& lang) { if (have_tool(tool)) return true; error = lang + " is not installed on this server (" + tool + " missing)."; return false; };
    if (ext == ".js" || ext == ".mjs" || ext == ".cjs") return need("node", "Node.js") ? M + "node " + q : "";
    if (ext == ".py") return need(python_cmd(), "Python") ? M + python_cmd() + " -u " + q : "";
    if (ext == ".sh" || ext == ".bash") return M + "bash " + q;
    if (ext == ".c") return need("gcc", "C (gcc)") ? "mkdir -p .build && gcc -O2 -Wall -o " + out + " " + q + " -lm && " + M + out : "";
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") return need("g++", "C++ (g++)") ? "mkdir -p .build && g++ -std=c++20 -O2 -Wall -o " + out + " " + q + " && " + M + out : "";
    if (ext == ".java") return need("java", "Java") ? M + "java " + q : "";
    if (ext == ".go") return need("go", "Go") ? "go run " + q : "";
    if (ext == ".rs") return need("rustc", "Rust") ? "mkdir -p .build && rustc -O -o " + out + " " + q + " && " + M + out : "";
    if (ext == ".rb") return need("ruby", "Ruby") ? M + "ruby " + q : "";
    if (ext == ".php") return need("php", "PHP") ? M + "php " + q : "";
    if (ext == ".lua") return need("lua", "Lua") ? M + "lua " + q : "";
    // TypeScript: tsx (esbuild-based) strips types and runs on Node; fetched once into the shared npm cache.
    if (ext == ".ts" || ext == ".mts" || ext == ".cts" || ext == ".tsx") return need("npx", "TypeScript (Node.js / npx)") ? M + "npx --yes tsx " + q : "";
    if (ext == ".cs") { error = "C# needs the .NET SDK, which is not installed on this server. The team can still write C# and explain it, or port it to C++."; return ""; }
    error = "Astro AI does not know how to run " + (ext.empty() ? std::string("this file") : ext + " files") + ".";
    return "";
}

#ifdef _WIN32
// Environment overrides for programs started from a project on Windows (the rest is inherited).
std::vector<std::string> win_child_env(const fs::path& dir, const std::string& home, const std::vector<std::string>& extra) {
    const std::string path = (dir / "node_modules" / ".bin").string() + ";" + (fs::path(home) / ".local" / "bin").string() + ";" + winp::getenv_s8("PATH");
    std::vector<std::string> env = {"PATH=" + path, "HOME=" + home, "PYTHONUNBUFFERED=1", "PYTHONDONTWRITEBYTECODE=1", "PYTHONIOENCODING=utf-8",
                                    "PYTHONUTF8=1", "NPM_CONFIG_UPDATE_NOTIFIER=false", "NPM_CONFIG_FUND=false", "NPM_CONFIG_AUDIT=false", "CHERE_INVOKING=1",
                                    "MSYS2_ARG_CONV_EXCL=*"};
    env.insert(env.end(), extra.begin(), extra.end());
    return env;
}

struct WinRunResult { int code = -1; bool started = false, timed_out = false, stopped = false; std::string error; };

// Runs a Windows command line with stdin/stdout/stderr pipes inside a Job Object. on_out(0|1, chunk) receives
// stdout/stderr (return false to stop); should_stop() is polled; leader_grace_ms >= 0 stops waiting that long
// after the main process exits (background children may keep the pipes open).
template <typename OnOut, typename ShouldStop>
WinRunResult win_run(const std::string& cmdline, const std::string& cwd, const std::vector<std::string>& env, const std::string& input,
                     int timeout_s, OnOut on_out, ShouldStop should_stop, int leader_grace_ms = -1) {
    WinRunResult r;
    HANDLE out_rd = nullptr, out_wr = nullptr, err_rd = nullptr, err_wr = nullptr, in_rd = nullptr, in_wr = nullptr;
    if (!winp::make_pipe(out_rd, out_wr, false) || !winp::make_pipe(err_rd, err_wr, false) || !winp::make_pipe(in_rd, in_wr, true)) {
        for (HANDLE h : {out_rd, out_wr, err_rd, err_wr, in_rd, in_wr}) if (h) CloseHandle(h);
        r.error = "Could not create pipes."; return r;
    }
    winp::Child child;
    const bool ok = winp::spawn(cmdline, cwd, env, in_rd, out_wr, err_wr, child, r.error);
    CloseHandle(out_wr); CloseHandle(err_wr); CloseHandle(in_rd);
    if (!ok) { CloseHandle(out_rd); CloseHandle(err_rd); CloseHandle(in_wr); return r; }
    r.started = true;
    std::thread([in_wr, input] {
        std::size_t off = 0; DWORD w = 0;
        while (off < input.size() && WriteFile(in_wr, input.data() + off, static_cast<DWORD>(std::min<std::size_t>(input.size() - off, 65536)), &w, nullptr) && w > 0) off += w;
        CloseHandle(in_wr);
    }).detach();
    struct Queue { std::mutex m; std::condition_variable cv; std::deque<std::pair<int, std::string>> items; int open = 2; };
    auto q = std::make_shared<Queue>();
    for (int i = 0; i < 2; ++i)
        winp::drain_async(i == 0 ? out_rd : err_rd, [q, i](const char* d, std::size_t n) {
            std::lock_guard<std::mutex> lock(q->m);
            if (!d) --q->open; else q->items.emplace_back(i, std::string(d, n));
            q->cv.notify_all();
        });
    const auto started = std::chrono::steady_clock::now();
    bool leader_done = false; auto leader_at = started;
    for (;;) {
        std::deque<std::pair<int, std::string>> batch; bool closed = false;
        {
            std::unique_lock<std::mutex> lock(q->m);
            q->cv.wait_for(lock, std::chrono::milliseconds(200), [&] { return !q->items.empty() || q->open == 0; });
            batch.swap(q->items); closed = q->open == 0;
        }
        for (auto& [i, data] : batch) if (!on_out(i, data)) { r.stopped = true; break; }
        if (r.stopped || closed) break;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(timeout_s)) { r.timed_out = true; break; }
        if (should_stop()) { r.stopped = true; break; }
        if (leader_grace_ms >= 0) {
            if (!leader_done && WaitForSingleObject(child.proc, 0) == WAIT_OBJECT_0) { leader_done = true; leader_at = std::chrono::steady_clock::now(); }
            if (leader_done && std::chrono::steady_clock::now() - leader_at > std::chrono::milliseconds(leader_grace_ms)) break;
        }
    }
    if (!r.timed_out && !r.stopped) {
        const auto left = std::chrono::seconds(timeout_s) - (std::chrono::steady_clock::now() - started);
        const long long ms = std::max<long long>(50, std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
        if (WaitForSingleObject(child.proc, static_cast<DWORD>(ms)) == WAIT_TIMEOUT) r.timed_out = true;
    }
    r.code = r.timed_out || r.stopped ? -1 : winp::exit_code(child);
    winp::kill_tree(child);
    winp::close_child(child);
    return r;
}
#endif

void run_stream(int fd, const fs::path& dir, const std::string& command, const std::string& input, int timeout_s, bool big_memory) {
    send_all(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream; charset=utf-8\r\nCache-Control: no-store\r\n"
                 "X-Accel-Buffering: no\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n");
    {
        std::string shown = command;   // hide the profiler wrapper path from the user
        const std::string& mp = measure_prefix();
        for (std::size_t at; !mp.empty() && (at = shown.find(mp)) != std::string::npos;) shown.erase(at, mp.size());
        Json::Object o; o["start"] = Json(shown); send_sse(fd, Json(std::move(o)));
    }
#ifdef _WIN32
    {
        (void)big_memory;
        std::error_code home_ec;
        const fs::path home_dir = fs::absolute(fs::path(data_dir()) / ".astro_home", home_ec);
        fs::create_directories(home_dir, home_ec);
        bool have_bash = false;
        const std::string cmdline = winp::shell_cmdline(command, have_bash);
        if (!have_bash) {
            Json::Object o; o["exit"] = Json(-1.0);
            o["error"] = Json(std::string("Running code on Windows needs Git for Windows (it provides bash). Install it from https://git-scm.com and restart Astro AI."));
            send_sse(fd, Json(std::move(o))); return;
        }
        const auto started = std::chrono::steady_clock::now();
        std::size_t total = 0; bool capped = false;
        const WinRunResult r = win_run(cmdline, dir.string(), win_child_env(dir, home_dir.string(), {"TERM=dumb", "NO_COLOR=1", "FORCE_COLOR=0"}), input, timeout_s,
            [&](int i, const std::string& data) {
                total += data.size();
                if (total > 2'000'000) { capped = true; return false; }
                Json::Object o; o[i == 0 ? "out" : "err"] = Json(data);
                return send_sse(fd, Json(std::move(o)));
            },
            [&] { return client_disconnected(fd); });
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        Json::Object o;
        o["exit"] = Json(static_cast<double>(r.started && !capped ? r.code : -1));
        o["ms"] = Json(static_cast<double>(ms));
        if (!r.started) o["error"] = Json(r.error.empty() ? std::string("Could not start the process.") : r.error);
        if (r.timed_out) o["error"] = Json("Stopped after " + std::to_string(timeout_s) + " s (time limit).");
        if (capped) o["error"] = Json(std::string("Stopped: more than 2 MB of output."));
        send_sse(fd, Json(std::move(o)));
        return;
    }
#else
    int out_pipe[2], err_pipe[2], in_pipe[2];
    if (pipe_cloexec(out_pipe) != 0 || pipe_cloexec(err_pipe) != 0 || pipe_cloexec(in_pipe) != 0) {
        Json::Object o; o["exit"] = Json(-1.0); o["error"] = Json(std::string("Could not create pipes.")); send_sse(fd, Json(std::move(o))); return;
    }
    const std::string dir_s = dir.string();
    std::error_code home_ec;
    const fs::path home_dir = fs::absolute(fs::path(data_dir()) / ".astro_home", home_ec);
    fs::create_directories(home_dir, home_ec);
    const std::string home = "HOME=" + home_dir.string();
    const fs::path measure_file = home_dir / (".measure-" + random_hex(8) + ".json");
    const std::string measure_env = "ASTRO_MEASURE_OUT=" + measure_file.string();
    const char* tool_bin = std::getenv("ASTRO_TOOL_BIN");   // AI tools installed from the Library
    const std::string path_env = "PATH=" + dir_s + "/node_modules/.bin:" + (tool_bin && *tool_bin ? std::string(tool_bin) + ":" : std::string()) + home_dir.string() + "/.local/bin:/usr/local/bin:/usr/bin:/bin";
    const auto started = std::chrono::steady_clock::now();
    const pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        dup2(in_pipe[0], 0); dup2(out_pipe[1], 1); dup2(err_pipe[1], 2);
        for (int f = 3; f < 4096; ++f) close(f);
        if (chdir(dir_s.c_str()) != 0) _exit(127);
        rlimit cpu{static_cast<rlim_t>(timeout_s + 5), static_cast<rlim_t>(timeout_s + 10)}; setrlimit(RLIMIT_CPU, &cpu);
        rlimit fsz{64ull << 20, 64ull << 20}; setrlimit(RLIMIT_FSIZE, &fsz);
        if (!big_memory) { rlimit as{2ull << 30, 2ull << 30}; setrlimit(RLIMIT_AS, &as); }
        rlimit nofile{256, 256}; setrlimit(RLIMIT_NOFILE, &nofile);
        rlimit core{0, 0}; setrlimit(RLIMIT_CORE, &core);
        const char* argv[] = {"bash", "-c", command.c_str(), nullptr};
        const char* envp[] = {path_env.c_str(), home.c_str(), "LANG=C.UTF-8", "TERM=dumb", "PYTHONUNBUFFERED=1",
                              "PYTHONDONTWRITEBYTECODE=1", "NO_COLOR=1", "FORCE_COLOR=0", measure_env.c_str(), "NPM_CONFIG_UPDATE_NOTIFIER=false", nullptr};
        execve("/bin/bash", const_cast<char* const*>(argv), const_cast<char* const*>(envp));
        _exit(127);
    }
    close(out_pipe[1]); close(err_pipe[1]); close(in_pipe[0]);
    if (pid < 0) {
        close(out_pipe[0]); close(err_pipe[0]); close(in_pipe[1]);
        Json::Object o; o["exit"] = Json(-1.0); o["error"] = Json(std::string("Could not start the process.")); send_sse(fd, Json(std::move(o))); return;
    }
    if (!input.empty()) { const ssize_t ignored = write(in_pipe[1], input.data(), input.size()); (void)ignored; }
    close(in_pipe[1]);
    pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
    std::size_t total = 0; bool timed_out = false, stopped = false, capped = false;
    int open_pipes = 2; char buf[8192];
    while (open_pipes > 0) {
        const int ready = poll(fds, 2, 250);
        if (ready > 0) {
            for (int i = 0; i < 2; ++i) {
                if (fds[i].fd < 0 || !(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
                const ssize_t got = read(fds[i].fd, buf, sizeof(buf));
                if (got <= 0) { close(fds[i].fd); fds[i].fd = -1; --open_pipes; continue; }
                total += static_cast<std::size_t>(got);
                if (total > 2'000'000) { capped = true; break; }
                Json::Object o; o[i == 0 ? "out" : "err"] = Json(std::string(buf, static_cast<std::size_t>(got)));
                if (!send_sse(fd, Json(std::move(o)))) { stopped = true; break; }
            }
        }
        if (capped || stopped) break;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(timeout_s)) { timed_out = true; break; }
        if (client_disconnected(fd)) { stopped = true; break; }
    }
    kill(-pid, SIGKILL);
    for (auto& f : fds) if (f.fd >= 0) close(f.fd);
    int status = 0; waitpid(pid, &status, 0);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    Json::Object o;
    const bool natural = !timed_out && !stopped && !capped;
    o["exit"] = Json(static_cast<double>(natural && WIFEXITED(status) ? WEXITSTATUS(status) : -1));
    if (natural && WIFSIGNALED(status)) o["signal"] = Json(std::string(strsignal(WTERMSIG(status))));
    o["ms"] = Json(static_cast<double>(ms));
    if (std::ifstream mf(measure_file); mf) {   // written by astro_measure when the program ended
        const std::string stats((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        try { const Json parsed = JsonParser(stats).parse(); if (parsed.is_object()) o["stats"] = parsed; } catch (...) {}
    }
    { std::error_code rm_ec; fs::remove(measure_file, rm_ec); }
    if (timed_out) o["error"] = Json("Stopped after " + std::to_string(timeout_s) + " s (time limit).");
    if (capped) o["error"] = Json(std::string("Stopped: more than 2 MB of output."));
    send_sse(fd, Json(std::move(o)));
#endif
}

// ---------- preview ----------
const std::string& preview_key() { static const std::string key = random_hex(16); return key; }

std::string mime_for(const std::string& ext) {
    static const std::map<std::string, std::string> m = {
        {".html", "text/html; charset=utf-8"}, {".htm", "text/html; charset=utf-8"}, {".css", "text/css; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"}, {".mjs", "text/javascript; charset=utf-8"}, {".json", "application/json; charset=utf-8"},
        {".svg", "image/svg+xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
        {".webp", "image/webp"}, {".ico", "image/x-icon"}, {".txt", "text/plain; charset=utf-8"}, {".md", "text/plain; charset=utf-8"},
        {".csv", "text/plain; charset=utf-8"}, {".xml", "application/xml"}, {".wasm", "application/wasm"}, {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"}, {".ogg", "audio/ogg"}, {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".woff2", "font/woff2"},
        {".woff", "font/woff"}, {".ttf", "font/ttf"}, {".pdf", "application/pdf"}};
    const auto it = m.find(lower_copy(ext));
    return it == m.end() ? "application/octet-stream" : it->second;
}

// Previews run in an opaque origin, where localStorage/sessionStorage throw. This shim gives pages working
// storage: it starts from data passed in the URL hash and reports every change to the Workspace (parent),
// which keeps it per project, so saved games, settings and to-do lists survive reloads.
const char* kStorageShim = R"JS(<script>(function(){try{window.localStorage.getItem('x');return}catch(e){}
var seed={};try{var h=location.hash.match(/astro-storage=([^&]*)/);if(h){seed=JSON.parse(decodeURIComponent(h[1]))||{};var rest=location.hash.replace(/#?astro-storage=[^&]*&?/,'');history.replaceState(null,'',location.pathname+location.search+(rest&&rest!=='#'?rest:''))}}catch(e){}
function mk(name,persist){var d={},init=seed[name]||{};for(var k in init)d[k]=String(init[k]);
function save(){if(persist)try{parent.postMessage({astroStorage:name,data:d},'*')}catch(e){}}
var s={getItem:function(k){k=String(k);return Object.prototype.hasOwnProperty.call(d,k)?d[k]:null},setItem:function(k,v){d[String(k)]=String(v);save()},
removeItem:function(k){delete d[String(k)];save()},clear:function(){for(var k in d)delete d[k];save()},key:function(i){return Object.keys(d)[i]||null}};
Object.defineProperty(s,'length',{get:function(){return Object.keys(d).length}});
try{Object.defineProperty(window,name,{value:s,configurable:true})}catch(e){}}
mk('localStorage',true);mk('sessionStorage',false);})();</script>)JS";

extern const char* kConsoleShim;   // defined in workspace_ide.inc; forwards console output to the editor

std::string with_storage_shim(const std::string& html) {
    const std::string kShims = std::string(kStorageShim) + kConsoleShim;
    std::string lower = lower_copy(html.substr(0, std::min<std::size_t>(html.size(), 4000)));
    std::size_t at = lower.find("<head");
    if (at != std::string::npos) { const auto close = lower.find('>', at); if (close != std::string::npos) return html.substr(0, close + 1) + kShims + html.substr(close + 1); }
    at = lower.find("<html");
    if (at != std::string::npos) { const auto close = lower.find('>', at); if (close != std::string::npos) return html.substr(0, close + 1) + kShims + html.substr(close + 1); }
    const auto doctype = lower.find("<!doctype");
    if (doctype != std::string::npos) { const auto close = lower.find('>', doctype); if (close != std::string::npos) return html.substr(0, close + 1) + kShims + html.substr(close + 1); }
    return kShims + html;
}

// GET /preview/<key>/<project>/<path>. Returns true when the path was a preview path.
bool serve_preview(int fd, const HttpRequest& request) {
    if (request.path.rfind("/preview/", 0) != 0) return false;
    std::string rest = url_unescape(request.path.substr(9));
    const auto s1 = rest.find('/');
    const std::string key = rest.substr(0, s1);
    if (!same_secret(key, preview_key()) || s1 == std::string::npos) { send_response(fd, 404, "text/plain; charset=utf-8", "Preview link expired. Reopen the preview."); return true; }
    rest = rest.substr(s1 + 1);
    const auto s2 = rest.find('/');
    const std::string project = rest.substr(0, s2);
    std::string rel = s2 == std::string::npos ? "" : rest.substr(s2 + 1);
    if (rel.empty() || rel.back() == '/') rel += "index.html";
    const auto full = project_path(project, rel);
    std::error_code ec;
    if (!full || !fs::is_regular_file(*full, ec)) { send_response(fd, 404, "text/plain; charset=utf-8", "Not found in the project: " + rel); return true; }
    std::ifstream in(*full, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string ext = lower_copy(full->extension().string());
    if (ext == ".html" || ext == ".htm") content = with_storage_shim(content);
    if (request.method == "HEAD") content.clear();
    std::ostringstream h;
    h << "HTTP/1.1 200 OK\r\nContent-Type: " << mime_for(full->extension().string()) << "\r\nContent-Length: " << content.size()
      << "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
      // Previews are sandboxed (opaque origin "null"): ES-module scripts and fetch() of the project's own files need CORS.
      << "Access-Control-Allow-Origin: null\r\nVary: Origin\r\n"
      << "Content-Security-Policy: sandbox allow-scripts allow-forms allow-modals allow-popups allow-downloads allow-pointer-lock\r\n\r\n";
    send_all(fd, h.str());
    send_all(fd, content);
    return true;
}

// ---------- API ----------
bool handle_workspace_api(int fd, const HttpRequest& request) {
    const std::string& path = request.path;
    if (path.rfind("/api/fs/", 0) != 0 && path != "/api/run") return false;
    // Every workspace call must come from the app itself (custom header => browsers enforce CORS),
    // so a page opened in a preview cannot write files or run commands.
    if (request.headers.find("x-astro-client") == request.headers.end()) { send_json(fd, 403, error_json("Missing client header.")); return true; }
    if (path == "/api/fs/projects" && request.method == "GET") {
        const fs::path root = projects_root();
        std::error_code ec;
        bool any = false;
        for (const auto& e : fs::directory_iterator(root, ec)) if (e.is_directory(ec) && valid_project_name(e.path().filename().string())) { any = true; break; }
        if (!any) write_starter_project(root / "my-project");
        Json::Array list;
        for (const auto& e : fs::directory_iterator(root, ec)) {
            const std::string name = e.path().filename().string();
            if (!e.is_directory(ec) || !valid_project_name(name)) continue;
            Json::Object o; o["name"] = Json(name);
            if (const std::string lt = link_target_of(name, e.path()); !lt.empty()) o["linked"] = Json(lt);
            const auto t = fs::last_write_time(e.path(), ec);
            o["mtime"] = Json(static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::file_clock::to_sys(t).time_since_epoch()).count()));
            list.emplace_back(std::move(o));
        }
        Json::Object o; o["projects"] = Json(std::move(list)); o["preview_key"] = Json(preview_key());
        Json::Array langs;
        for (const auto& [tool, lang] : std::vector<std::pair<std::string, std::string>>{{"node", "JavaScript"}, {"python3", "Python"}, {"g++", "C++"}, {"gcc", "C"}, {"java", "Java"}, {"bash", "Bash"}, {"go", "Go"}, {"rustc", "Rust"}, {"ruby", "Ruby"}, {"php", "PHP"}})
            if (have_tool(tool)) langs.emplace_back(Json(lang));
        o["languages"] = Json(std::move(langs));
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (request.method != "POST") { send_json(fd, 405, error_json("Use POST.")); return true; }
    const auto body_opt = parse_request_json(request, fd);
    if (!body_opt) return true;
    const Json& body = *body_opt;
    const std::string project = get_string(body, "project");
    if (!valid_project_name(project)) { send_json(fd, 400, error_json("Invalid project name (letters, numbers, - _ . only).")); return true; }
    const fs::path base = fs::path(projects_root()) / project;
    std::error_code ec;
    auto ok = [&](Json::Object extra = {}) { extra["ok"] = Json(true); send_json(fd, 200, Json(std::move(extra))); };

    if (path == "/api/fs/project/create") {
        if (fs::exists(base, ec)) { send_json(fd, 409, error_json("A project with that name already exists.")); return true; }
        if (json_bool(body, "starter", false)) write_starter_project(base); else fs::create_directories(base, ec);
        ok(); return true;
    }
    if (!fs::is_directory(base, ec)) { send_json(fd, 404, error_json("Project not found.")); return true; }
    // Checkpoints and agent sessions live outside the project folder; they follow the project on delete and rename.
    const auto side_dirs = [](const std::string& name) { return std::vector<fs::path>{fs::path(data_dir()) / ".astro_history" / name, fs::path(data_dir()) / ".astro_agent" / name}; };
    if (path == "/api/fs/project/delete") { mcp_forget_project(project); if (project_is_link(base)) { remove_dir_link(base); auto links = load_links(); links.erase(project); save_links(links); } else fs::remove_all(base, ec); for (const auto& d : side_dirs(project)) fs::remove_all(d, ec); ok(); return true; }
    if (path == "/api/fs/project/rename") {
        const std::string to = get_string(body, "to");
        if (!valid_project_name(to)) { send_json(fd, 400, error_json("Invalid new name.")); return true; }
        const fs::path dest = fs::path(projects_root()) / to;
        if (fs::exists(dest, ec)) { send_json(fd, 409, error_json("That name is taken.")); return true; }
        mcp_forget_project(project);
        fs::rename(base, dest, ec);
        if (ec) { send_json(fd, 500, error_json(ec.message())); return true; }
        { auto links = load_links(); if (auto it = links.find(project); it != links.end()) { links[to] = it->second; links.erase(project); save_links(links); } }
        { const auto from = side_dirs(project), to_dirs = side_dirs(to); for (std::size_t i = 0; i < from.size(); ++i) { std::error_code e2; if (fs::exists(from[i], e2)) { fs::remove_all(to_dirs[i], e2); fs::rename(from[i], to_dirs[i], e2); } } }
        ok(); return true;
    }
    if (path == "/api/fs/tree") { send_json(fd, 200, tree_json(base)); return true; }

    if (path == "/api/run") {
        const std::string file = get_string(body, "file");
        std::string command = get_string(body, "cmd");
        const int timeout_s = std::clamp(json_int(body, "timeout", 30), 2, 300);
        if (command.empty()) {
            if (!project_path(project, file)) { send_json(fd, 400, error_json("Invalid file path.")); return true; }
            std::string error;
            command = get_string(body, "mode") == "asm" ? asm_command_for(file, error) : run_command_for(file, error);
            if (command.empty()) { send_json(fd, 422, error_json(error)); return true; }
        }
        if (command.size() > 4000) { send_json(fd, 400, error_json("Command too long.")); return true; }
        // JVM and WebAssembly (tsx/esbuild) reserve large virtual address ranges, so they run without the 2 GB RLIMIT_AS cap.
        const bool java = command.rfind("java ", 0) == 0 || command.find(" java ") != std::string::npos || command.find(" tsx ") != std::string::npos;
        run_stream(fd, base, command, get_string(body, "stdin").substr(0, 1'000'000), timeout_s, java);
        return true;
    }

    const std::string rel = get_string(body, "path");
    const auto full = project_path(project, rel);
    if (!full) { send_json(fd, 400, error_json("Invalid path: " + truncate(rel, 120))); return true; }
    if (path == "/api/fs/read") {
        if (!fs::is_regular_file(*full, ec)) { send_json(fd, 404, error_json("File not found: " + rel)); return true; }
        const auto size = fs::file_size(*full, ec);
        Json::Object o; o["path"] = Json(rel); o["size"] = Json(static_cast<double>(size));
        if (size > 3'000'000) { o["binary"] = Json(true); o["too_big"] = Json(true); send_json(fd, 200, Json(std::move(o))); return true; }
        std::ifstream in(*full, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const bool binary = content.find('\0') != std::string::npos;
        o["binary"] = Json(binary);
        if (!binary) o["content"] = Json(content);
        send_json(fd, 200, Json(std::move(o)));
        return true;
    }
    if (path == "/api/fs/write") {
        const Json* content = body.get("content");
        const Json* b64 = body.get("base64");
        if (!(content && content->string_ptr()) && !(b64 && b64->string_ptr())) { send_json(fd, 400, error_json("Missing content.")); return true; }
        const std::string data = content && content->string_ptr() ? *content->string_ptr() : base64_decode(*b64->string_ptr());
        if (data.size() > 8'000'000) { send_json(fd, 413, error_json("File too large (8 MB max).")); return true; }
        if (fs::is_directory(*full, ec)) { send_json(fd, 409, error_json("A folder has that name.")); return true; }
        fs::create_directories(full->parent_path(), ec);
        const bool existed = fs::exists(*full, ec);
        std::ofstream out(*full, std::ios::binary | std::ios::trunc);
        out << data;
        if (!out) { send_json(fd, 500, error_json("Could not write the file.")); return true; }
        Json::Object o; o["created"] = Json(!existed); o["size"] = Json(static_cast<double>(data.size()));
        ok(std::move(o)); return true;
    }
    if (path == "/api/fs/mkdir") { fs::create_directories(*full, ec); if (ec) { send_json(fd, 500, error_json(ec.message())); return true; } ok(); return true; }
    if (path == "/api/fs/delete") { fs::remove_all(*full, ec); if (ec) { send_json(fd, 500, error_json(ec.message())); return true; } ok(); return true; }
    if (path == "/api/fs/rename") {
        const auto dest = project_path(project, get_string(body, "to"));
        if (!dest) { send_json(fd, 400, error_json("Invalid new path.")); return true; }
        if (fs::exists(*dest, ec)) { send_json(fd, 409, error_json("Something already has that name.")); return true; }
        fs::create_directories(dest->parent_path(), ec);
        fs::rename(*full, *dest, ec);
        if (ec) { send_json(fd, 500, error_json(ec.message())); return true; }
        ok(); return true;
    }
    send_json(fd, 404, error_json("Not found."));
    return true;
}
