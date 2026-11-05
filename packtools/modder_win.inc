// modder_win.inc - `um win`: a C++ port of universal-modder's um/win.py (Rehan and universal-modder contributors, MIT).
// Windows games from an agent: native Windows, or WSL driving Windows through powershell.exe / cmd.exe / reg.exe.
// Input goes only to the game window while it is in the foreground; processes are killed by exact PID only.
namespace packtools::modder::win {
using namespace pyrt;
using namespace packtools::modder;

inline const char* DOC =
    "Windows games from an agent (native Windows, or WSL where most coding agents live).\n\n"
    "    um win setup                                  # copy the PowerShell tools + fetch an ffmpeg with gfxcapture (SHA-256 checked)\n"
    "    um win ps [name]                              # processes with windows: pid, name, title\n"
    "    um win kill <pid>                             # by exact PID only (never by pattern)\n"
    "    um win launch --steam 105600 [-- args]        # or: um win launch \"C:\\\\Games\\\\Foo\\\\foo.exe\" -- -windowed\n"
    "    um win shot --exe AoE2DE_s.exe out.png [--scale 0.33]    # one frame of the game window (GPU-safe capture)\n"
    "    um win drive --proc AoE2DE_s \"focus\" \"click 640 360\" \"key 0x1B\"   # input (WinDrive protocol, see um/ps1)\n"
    "    um win record --exe Game.exe --out C:\\\\caps\\\\take1 --seconds 30  # video (gfxcapture) + game-only audio\n"
    "    um video mux C:\\\\caps\\\\take1.mkv C:\\\\caps\\\\take1.audio.raw C:\\\\caps\\\\take1.json out.mp4\n"
    "    um win reg get \"HKCU\\\\Software\\\\Foo\" [value] | um win reg set KEY VALUE DATA [--type REG_DWORD]\n\n"
    "Why these tools: games render with the GPU, so GDI screen grabs are black - Windows.Graphics.Capture\n"
    "(ffmpeg's gfxcapture) gets the real frames of one window even when covered. Many games talk to WASAPI\n"
    "directly, so the only clean way to get just the game's sound is a process-loopback capture. Input goes\n"
    "through SendInput only while the game is the foreground window, so nothing leaks into other apps.\n"
    "Lessons: never block the game's main thread when stopping a recording; write .mkv (survives a kill);\n"
    "NVENC H.264 tops out at 4096 px wide (ultrawide: crop or use HEVC); a crashed game's crash reporter\n"
    "(BugSplat etc.) can make Steam refuse to relaunch - list and kill it by PID.\n";

inline const std::string FFMPEG_URL = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";
inline const std::string FFMPEG_SUMS = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/checksums.sha256";
inline const std::string GPU_PREFS = "Software\\Microsoft\\DirectX\\UserGpuPreferences";

// ------------------------------------------------------------------ an interactive child (subprocess.Popen with pipes)
enum class ErrTo { INHERIT, DEVNULL, FILE };
struct Child {
    bool ok = false;
#if !defined(_WIN32)
    pid_t pid = -1; int in_fd = -1, out_fd = -1;
#else
    HANDLE proc = nullptr, in_h = nullptr, out_h = nullptr;
#endif
    std::string buf;
    bool start(const std::vector<std::string>& args, const std::string& cwd, bool pipe_out, ErrTo err, const std::string& err_file = "") {
#if !defined(_WIN32)
        int pi[2], po[2] = {-1, -1};
        if (pipe(pi) != 0) return false;
        if (pipe_out && pipe(po) != 0) { close(pi[0]); close(pi[1]); return false; }
        std::vector<char*> argv; for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str())); argv.push_back(nullptr);
        pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            dup2(pi[0], 0); close(pi[0]); close(pi[1]);
            if (pipe_out) { dup2(po[1], 1); close(po[0]); close(po[1]); }
            if (err == ErrTo::DEVNULL) { const int n = open("/dev/null", O_WRONLY); if (n >= 0) { dup2(n, 2); close(n); } }
            else if (err == ErrTo::FILE) { const int n = open(err_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); if (n >= 0) { dup2(n, 2); close(n); } }
            signal(SIGPIPE, SIG_DFL);
            if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        close(pi[0]); in_fd = pi[1];
        if (pipe_out) { close(po[1]); out_fd = po[0]; }
        ok = true; return true;
#else
        SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
        HANDLE ir = nullptr, iw = nullptr, orr = nullptr, ow = nullptr, eh = nullptr;
        if (!CreatePipe(&ir, &iw, &sa, 0)) return false;
        SetHandleInformation(iw, HANDLE_FLAG_INHERIT, 0);
        if (pipe_out) { if (!CreatePipe(&orr, &ow, &sa, 0)) return false; SetHandleInformation(orr, HANDLE_FLAG_INHERIT, 0); }
        if (err == ErrTo::DEVNULL) eh = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        else if (err == ErrTo::FILE) eh = CreateFileW(mk::widen(err_file).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        else eh = GetStdHandle(STD_ERROR_HANDLE);
        STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = ir;
        si.hStdOutput = pipe_out ? ow : GetStdHandle(STD_OUTPUT_HANDLE); si.hStdError = eh;
        std::wstring cmd; for (const auto& a : args) { if (!cmd.empty()) cmd += L' '; cmd += mk::win_quote(mk::widen(a)); }
        const std::wstring wcwd = mk::widen(cwd);
        PROCESS_INFORMATION pinfo{};
        const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                            cwd.empty() ? nullptr : wcwd.c_str(), &si, &pinfo);
        CloseHandle(ir); if (ow) CloseHandle(ow); if (err != ErrTo::INHERIT && eh && eh != INVALID_HANDLE_VALUE) CloseHandle(eh);
        if (!started) { CloseHandle(iw); if (orr) CloseHandle(orr); return false; }
        CloseHandle(pinfo.hThread); proc = pinfo.hProcess; in_h = iw; out_h = orr;
        ok = true; return true;
#endif
    }
    bool write(const std::string& s) {
#if !defined(_WIN32)
        if (in_fd < 0) return false;
        std::size_t off = 0; while (off < s.size()) { const ssize_t n = ::write(in_fd, s.data() + off, s.size() - off); if (n < 0) { if (errno == EINTR) continue; return false; } off += static_cast<std::size_t>(n); }
        return true;
#else
        if (!in_h) return false; DWORD n = 0; return WriteFile(in_h, s.data(), static_cast<DWORD>(s.size()), &n, nullptr) && n == s.size();
#endif
    }
    std::string readline() {   // text mode: "\r\n" -> "\n"; "" at end of file
        for (;;) {
            const auto nl = buf.find('\n');
            if (nl != std::string::npos) { std::string l = buf.substr(0, nl + 1); buf.erase(0, nl + 1); if (l.size() > 1 && l[l.size() - 2] == '\r') l.erase(l.size() - 2, 1); return l; }
            char b[4096];
#if !defined(_WIN32)
            if (out_fd < 0) break;
            const ssize_t n = ::read(out_fd, b, sizeof b);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            buf.append(b, static_cast<std::size_t>(n));
#else
            if (!out_h) break; DWORD n = 0;
            if (!ReadFile(out_h, b, sizeof b, &n, nullptr) || n == 0) break;
            buf.append(b, n);
#endif
        }
        std::string l; l.swap(buf); return l;
    }
    void close_stdin() {
#if !defined(_WIN32)
        if (in_fd >= 0) { ::close(in_fd); in_fd = -1; }
#else
        if (in_h) { CloseHandle(in_h); in_h = nullptr; }
#endif
    }
    bool wait(double timeout_s) {   // false on timeout
#if !defined(_WIN32)
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(timeout_s * 1000));
        for (;;) {
            int st = 0; const pid_t r = waitpid(pid, &st, WNOHANG);
            if (r == pid || r < 0) { pid = -1; return true; }
            if (std::chrono::steady_clock::now() >= end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#else
        const DWORD r = WaitForSingleObject(proc, static_cast<DWORD>(timeout_s * 1000)); return r != WAIT_TIMEOUT;
#endif
    }
    void kill_() {
#if !defined(_WIN32)
        if (pid > 0) { ::kill(pid, SIGKILL); int st = 0; waitpid(pid, &st, 0); pid = -1; }
#else
        if (proc) TerminateProcess(proc, 1);
#endif
    }
    ~Child() {
        close_stdin();
#if !defined(_WIN32)
        if (out_fd >= 0) ::close(out_fd);
#else
        if (out_h) CloseHandle(out_h);
        if (proc) CloseHandle(proc);
#endif
    }
};
inline std::string wsl_cwd() { return is_wsl() && isdir("/mnt/c") ? "/mnt/c" : ""; }   // upstream: cwd="/mnt/c" under WSL (always there in real WSL)
inline std::string text_of(const std::string& b) { return decode_utf8_replace(b); }

// ------------------------------------------------------------------ platform
inline void check_platform() {
    if (!(is_windows() || is_wsl()))
        die("`um win` drives Windows games (native Windows or WSL). On Linux use xdotool/ydotool + ffmpeg x11grab/pipewire; "
            "on macOS use screencapture + ffmpeg avfoundation (see skills/game-automation).");
}
inline std::string powershell(const std::string& script, int timeout = 60) {
    check_platform();
    const auto r = mk::run_proc({ps_exe(), "-NoProfile", "-NonInteractive", "-Command", PS_UTF8 + script}, wsl_cwd(), {}, timeout);
    if (r.code != 0) { const std::string e = strip(text_of(r.err)); die("powershell failed: " + cpslice(e, -1500)); }
    return replace_all(text_of(r.out), "\r", "");
}
inline std::string local_appdata() {
    std::string base;
    if (is_windows()) base = env("LOCALAPPDATA");
    else base = to_posix(strip(powershell("[Environment]::GetFolderPath('LocalApplicationData')")));
    const std::string d = path_join(base, "universal-modder");
    makedirs(d);
    return d;
}
inline std::string tool_path(const std::string& name) {
    const std::string& src = name == "WinDrive.ps1" ? windrive_ps1() : procloopback_ps1();
    const std::string dst = path_join(path_join(local_appdata(), "tools"), name);
    makedirs(u8native(P(dst).parent_path()));
    if (!isfile(dst) || sha256_hex(slurp(dst)) != sha256_hex(src)) spit(dst, src);
    return to_win(dst);
}
inline std::optional<std::string> ffmpeg_win(bool required = true) {
    std::vector<std::string> cands;
    if (!env("UM_FFMPEG_WIN").empty()) cands.push_back(env("UM_FFMPEG_WIN"));
    try { cands.push_back(path_join(path_join(path_join(local_appdata(), "ffmpeg"), "bin"), "ffmpeg.exe")); } catch (const Exit&) {}
    if (is_windows()) { const std::string w = which_tool("ffmpeg"); if (!w.empty()) cands.push_back(w); }
    for (const auto& c : cands) if (pexists(to_posix(c))) return is_wsl() ? to_posix(c) : c;
    if (required) die("no Windows ffmpeg with gfxcapture yet: run `um win setup`");
    return std::nullopt;
}
inline std::string ffmpeg_sha256() {
    const std::string pin = lower(strip(env("UM_FFMPEG_SHA256")));
    if (!pin.empty()) return pin;
    const std::string name = FFMPEG_URL.substr(FFMPEG_URL.rfind('/') + 1);
    const UmResp r = um_http("GET", FFMPEG_SUMS, {"User-Agent: universal-modder"}, nullptr, 60);
    if (r.status < 200 || r.status >= 300) throw PyErr("urllib.error.URLError", r.status ? "HTTP Error " + std::to_string(r.status) : r.error);
    for (const auto& line : split(text_of(r.body), "\n")) {
        const auto parts = split_ws(line);
        if (parts.size() == 2) { std::string f = parts[1]; while (!f.empty() && f[0] == '*') f.erase(0, 1); if (f == name) return lower(parts[0]); }
    }
    die(name + " isn't listed in " + FFMPEG_SUMS + "; set UM_FFMPEG_SHA256, or point UM_FFMPEG_WIN at an ffmpeg you trust");
    return "";
}
inline std::string download_ffmpeg(const std::string& z) {   // returns the zip bytes, checked before anything is extracted
    const std::string want = ffmpeg_sha256();
    print("downloading " + FFMPEG_URL);
    const UmResp r = um_http("GET", FFMPEG_URL, {"User-Agent: universal-modder"}, nullptr, 3600);
    if (r.status < 200 || r.status >= 300) throw PyErr("urllib.error.URLError", r.status ? "HTTP Error " + std::to_string(r.status) : r.error);
    spit(z, r.body);
    const std::string got = sha256_hex(r.body);
    if (got != want) {
        std::error_code ec; fs::remove(P(z), ec);
        die("ffmpeg download failed its checksum (got " + got + ", expected " + want + "); deleted it. The daily build may have been replaced mid-download: "
            "run `um win setup` again. If it keeps failing, don't use this build.");
    }
    print("sha256 ok " + want);
    return r.body;
}
inline std::string pick_encoder(const std::string& ff) {
    for (const char* enc : {"h264_nvenc", "h264_amf", "h264_qsv"})
        if (mk::run_proc({ff, "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "color=black:s=256x256:d=0.1", "-c:v", enc, "-f", "null", "-"}).code == 0) return enc;
    return "libx264";
}
inline std::string encoder() {
    try { const Val c = loads(slurp(path_join(local_appdata(), "config.json"))); if (get(c, "encoder").t == Val::STR) return get(c, "encoder").s; } catch (const PyErr&) {}
    return "libx264";
}
inline void setup(const Val& a) {
    check_platform();
    const std::string d = local_appdata();
    for (const char* t : {"WinDrive.ps1", "ProcLoopback.ps1"}) print(std::string("tool ") + tool_path(t));
    if (!ffmpeg_win(false) || truthy(get(a, "force"))) {
        const std::string z = path_join(d, "ffmpeg.zip");
        const std::string data = download_ffmpeg(z);
        const auto mem = pyrt::zip::members(data);
        if (mem.empty()) throw pyrt::zip::BadZipFile("File is not a zip file");
        const std::string root = split(mem[0].name, "/")[0];
        const std::string dst = path_join(d, "ffmpeg");
        std::error_code ec; if (pexists(dst)) fs::remove_all(P(dst), ec);
        for (const auto& m : mem) {
            const std::string& n = m.name;
            if (!starts(n, root + "/") || ends(n, "/")) continue;
            const std::string rel = n.substr(root.size() + 1);
            bool bad = rel.empty() || rel[0] == '/' || rel.find(':') != std::string::npos;   // extractall drops absolute and drive parts; skip them
            for (const auto& part : split(replace_all(rel, "\\", "/"), "/")) if (part == "..") bad = true;
            if (bad) continue;
            spit(path_join(dst, rel), pyrt::zip::read(data, n));
        }
        fs::remove(P(z), ec);
    }
    const std::string ff = *ffmpeg_win();
    // ask for the filter list and match the name as a word: `-h filter=gfxcapture` echoes the name even when it is missing
    const std::string out = text_of(mk::run_proc({ff, "-hide_banner", "-filters"}).out);
    print("ffmpeg " + ff + " " + (out.find(" gfxcapture ") != std::string::npos ? "(gfxcapture ok)" : "(WARNING: no gfxcapture in this build)"));
    const std::string enc = pick_encoder(ff);
    Val cfg = Val::dict(); cfg.d->set("encoder", Val::str(enc)); spit(path_join(d, "config.json"), dumps(cfg));
    print("encoder " + enc);
}

// ------------------------------------------------------------------ processes
inline Val processes(const std::string& name) {
    const std::string flt = name.empty() ? "" : "-Name '" + name + "*'";
    const std::string out = powershell("Get-Process " + flt + " -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | "
                                       "Select-Object Id,ProcessName,MainWindowTitle,@{n='Hwnd';e={[int64]$_.MainWindowHandle}} | ConvertTo-Json -Compress");
    if (strip(out).empty()) return Val::list();
    const Val d = loads(out);
    return d.t == Val::LIST ? d : Val::list({d});
}
inline std::optional<long long> pid_of(const std::string& name) {
    const std::string n = ends(lower(name), ".exe") ? name.substr(0, name.size() - 4) : name;
    const std::string out = strip(powershell("(Get-Process -Name '" + n + "' -ErrorAction SilentlyContinue | Select-Object -First 1).Id"));
    if (out.empty() || out.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
    return std::stoll(out);
}
inline void kill_pid(long long pid) {   // by exact PID: pattern kills can hit the agent's own shell or other apps
    const auto r = mk::run_proc({is_wsl() ? "taskkill.exe" : "taskkill", "/PID", std::to_string(pid), "/F"});
    print(strip(text_of(!r.out.empty() ? r.out : r.err)));
}
inline std::string nt_basename(const std::string& p) { const auto k = p.find_last_of("/\\"); std::string s = k == std::string::npos ? p : p.substr(k + 1); if (k == std::string::npos && s.size() >= 2 && s[1] == ':') s = s.substr(2); return s; }
inline std::string nt_dirname(const std::string& p) {
    std::string drive, rest = p;
    if (p.size() >= 2 && p[1] == ':') { drive = p.substr(0, 2); rest = p.substr(2); }
    const auto k = rest.find_last_of("/\\");
    if (k == std::string::npos) return drive;
    std::string head = rest.substr(0, k + 1);
    if (head.find_first_not_of("/\\") != std::string::npos) while (!head.empty() && (head.back() == '/' || head.back() == '\\')) head.pop_back();
    return drive + head;
}
inline void launch(const std::string& target, const std::vector<std::string>& args, bool steam) {
    const std::string cmdexe = is_wsl() ? "cmd.exe" : "cmd";
    if (steam) {
        const std::string url = args.empty() ? "steam://rungameid/" + target : "steam://run/" + target + "//" + join(args, " ") + "/";
        mk::run_proc({cmdexe, "/c", "start", "", url}, wsl_cwd(), {}, 30);
        return;
    }
    const std::string exe = to_win(target);
    // start in the exe's own folder: games open their data files relative to the working directory
    const std::string exe_dir = nt_dirname(exe);
    std::vector<std::string> c = {cmdexe, "/c", "start", ""};
    if (!exe_dir.empty()) { c.push_back("/D"); c.push_back(exe_dir); }
    c.push_back(exe); c.insert(c.end(), args.begin(), args.end());
    mk::run_proc(c, wsl_cwd(), {}, 30);
}

// ------------------------------------------------------------------ capture
inline std::string ff_value(const std::string& v) {   // escaped for the filter's option list, then for the filtergraph
    std::string a; for (char c : v) { if (c == '\\' || c == '\'' || c == ':') a += '\\'; a += c; }
    std::string b; for (char c : a) { if (c == '\\' || c == '\'' || c == '[' || c == ']' || c == ',' || c == ';') b += '\\'; b += c; }
    return b;
}
inline std::string exe_name(const std::string& exe) { return re::match(R"([A-Za-z]:[\\/]|\\\\|/)", exe).ok ? nt_basename(exe) : exe; }
inline std::string source(const std::string& exe0, long long hwnd, const std::string& title, bool cursor = false, const std::vector<long long>& crop = {}) {
    std::string sel;
    if (hwnd) sel = "hwnd=" + std::to_string(hwnd);
    else if (!exe0.empty()) { const std::string exe = exe_name(exe0); sel = "window_exe=" + ff_value(ends(lower(exe), ".exe") ? exe : exe + ".exe"); }
    else if (!title.empty()) sel = "window_title=" + ff_value(title);
    else die("give --exe, --hwnd or --title");
    std::string c;
    if (crop.size() == 4) c = ":crop_left=" + std::to_string(crop[0]) + ":crop_top=" + std::to_string(crop[1]) + ":crop_right=" + std::to_string(crop[2]) + ":crop_bottom=" + std::to_string(crop[3]);
    return "gfxcapture=" + sel + ":capture_cursor=" + (cursor ? "1" : "0") + ":max_framerate=60" + c + ",hwdownload,format=bgra";
}
inline std::vector<std::pair<std::string, std::string>> gpu_prefs() {
    std::vector<std::pair<std::string, std::string>> out;
#if defined(_WIN32)
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, mk::widen(GPU_PREFS).c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        for (DWORD i = 0;; ++i) {
            wchar_t name[2048]; DWORD nlen = 2048; BYTE data[8192]; DWORD dlen = sizeof data, type = 0;
            if (RegEnumValueW(k, i, name, &nlen, nullptr, &type, data, &dlen) != ERROR_SUCCESS) break;
            auto narrow = [](const wchar_t* w, int n) { const int m = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr); std::string s(static_cast<std::size_t>(m), '\0'); WideCharToMultiByte(CP_UTF8, 0, w, n, s.data(), m, nullptr, nullptr); return s; };
            std::string val;
            if (type == REG_SZ || type == REG_EXPAND_SZ) { int n = static_cast<int>(dlen / sizeof(wchar_t)); const wchar_t* w = reinterpret_cast<const wchar_t*>(data); while (n > 0 && w[n - 1] == 0) --n; val = narrow(w, n); }
            else if (type == REG_DWORD && dlen >= 4) { DWORD v; std::memcpy(&v, data, 4); val = std::to_string(v); }
            out.emplace_back(narrow(name, static_cast<int>(nlen)), val);
        }
        RegCloseKey(k);
    }
#else
    if (is_wsl()) {
        const auto r = mk::run_proc({"reg.exe", "query", "HKCU\\" + GPU_PREFS}, "/mnt/c", {}, 15);
        for (const auto& line0 : split(text_of(r.out), "\n")) {
            const std::string line = strip(line0); const auto k = line.find("    REG_SZ    ");
            if (k != std::string::npos) out.emplace_back(line.substr(0, k), line.substr(k + 14));
        }
    }
#endif
    return out;
}
inline std::optional<bool> hdr_flag(const std::string& s) {
    std::string v; for (const auto& kv : split(s, ";")) { const auto e = kv.find('='); if (e != std::string::npos && kv.substr(0, e) == "AutoHDREnable") v = kv.substr(e + 1); }
    if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
    return (v.back() - '0') % 2 == 1;
}
inline bool auto_hdr_on(const std::string& exe) {
    const auto prefs = gpu_prefs();
    if (!exe.empty()) {
        const std::string name = lower(ends(lower(exe), ".exe") ? exe : exe + ".exe");
        for (const auto& [path, data] : prefs) {
            std::string p = lower(replace_all(path, "/", "\\")); p = p.substr(p.rfind('\\') == std::string::npos ? 0 : p.rfind('\\') + 1);
            if (p == name && hdr_flag(data)) return *hdr_flag(data);
        }
    }
    for (const auto& [path, data] : prefs) if (path == "DirectXUserGlobalSettings") return hdr_flag(data).value_or(false);
    return false;
}
inline void warn_auto_hdr(const std::string& exe) {
    if (auto_hdr_on(exe))
        eprint("WARNING: Windows Auto HDR is on" + (exe.empty() ? std::string() : " for " + exe) + ". On an HDR display the capture of an SDR game comes out washed out "
               "(brighter, shifted colours). Turn Auto HDR off for the game while capturing: Settings > System > Display > Graphics > (the game) > Auto HDR.");
}
inline std::string resolve(const std::string& p) { std::error_code ec; fs::path a = fs::absolute(P(p), ec); fs::path w = fs::weakly_canonical(a, ec); return u8native(ec ? a : w); }
inline std::string shot(const std::string& out, const std::string& exe0, long long hwnd, const std::string& title, double scale, int timeout = 20) {
    const std::string exe = exe0.empty() ? exe0 : exe_name(exe0);
    const std::string ff = *ffmpeg_win();
    warn_auto_hdr(exe);
    const std::string dst = resolve(out);
    makedirs(u8native(P(dst).parent_path()));
    const std::string target = is_wsl() ? to_win(dst) : dst;
    const auto r = mk::run_proc({ff, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", source(exe, hwnd, title), "-frames:v", "1", target}, "", {}, timeout);
    if (r.code != 0 || !pexists(dst)) die("capture failed (is the window open and not minimized?): " + cpslice(strip(text_of(r.err)), -800));
    if (scale > 0) {
        const img::Image im = img::load(dst);
        const std::string small = u8native(P(dst).parent_path() / P(u8native(P(dst).stem()) + "_small.png"));
        img::save(img::resize(im, std::max(1LL, static_cast<long long>(im.w * scale)), std::max(1LL, static_cast<long long>(im.h * scale)), img::Filter::LANCZOS), small);
        print(small);
    }
    return dst;
}

struct Recorder {
    static constexpr double STARTUP = 0.3;   // ffmpeg spawn + capture + encoder init before the first frame (measured ~0.24-0.3 s)
    std::string exe, title, base, logp; long long hwnd = 0, fps = 30, pid = 0, max_width = 0, cq = 19; bool audio_on = true; std::vector<long long> crop;
    std::unique_ptr<Child> video, audio; Val header = Val::dict();
    std::chrono::steady_clock::time_point t_video, t_audio;
    Recorder(const std::string& exe0, const std::string& out, long long hwnd_, const std::string& title_, long long fps_, bool audio_, std::vector<long long> crop_, long long pid_)
        : exe(exe0.empty() ? exe0 : exe_name(exe0)), title(title_), hwnd(hwnd_), fps(fps_), pid(pid_), audio_on(audio_), crop(std::move(crop_)) {
        base = (is_wsl() && !(out.size() > 1 && out[1] == ':')) ? to_win(resolve(out)) : out;
    }
    void start() {
        const std::string ff = *ffmpeg_win();
        warn_auto_hdr(exe);
        const std::string enc = encoder();
        std::vector<std::string> vf = {"fps=" + std::to_string(fps)};
        const long long mw = max_width ? max_width : (enc == "h264_nvenc" ? 4096 : 0);
        if (mw) vf.push_back("scale='min(iw," + std::to_string(mw) + ")':-2");
        vf.push_back("crop=trunc(iw/2)*2:trunc(ih/2)*2");
        // RGB -> YUV with the BT.709 matrix, and say so: an untagged file gets BT.601 here but is read as BT.709 by browsers
        vf.push_back("scale=out_color_matrix=bt709:out_range=tv,format=yuv420p");
        const std::string q = std::to_string(cq);
        std::vector<std::string> codec;
        if (enc == "h264_nvenc") codec = {"-c:v", "h264_nvenc", "-preset", "p4", "-cq", q};
        else if (enc == "h264_amf") codec = {"-c:v", "h264_amf", "-quality", "quality", "-qp_i", q, "-qp_p", q};
        else if (enc == "h264_qsv") codec = {"-c:v", "h264_qsv", "-global_quality", q};
        else codec = {"-c:v", "libx264", "-preset", "veryfast", "-crf", "18"};
        t_audio = std::chrono::steady_clock::now();
        if (audio_on) {
            long long p = pid; if (!p && !exe.empty()) p = pid_of(exe).value_or(0);
            if (!p) eprint("no pid for audio capture (give --exe or --pid); recording video only");
            else {
                audio = std::make_unique<Child>();
                if (!audio->start({ps_exe(), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", tool_path("ProcLoopback.ps1"), "-TargetPid", std::to_string(p), "-Out", base + ".audio.raw"},
                                  wsl_cwd(), true, ErrTo::DEVNULL)) die("could not start PowerShell for the audio capture");
                const std::string l = audio->readline();
                header = loads(!l.empty() ? l : "{}");
                t_audio = std::chrono::steady_clock::now();
            }
        }
        t_video = std::chrono::steady_clock::now();
        logp = to_posix(base + ".ffmpeg.log");
        std::vector<std::string> args = {ff, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", source(exe, hwnd, title, false, crop), "-vf", join(vf, ",")};
        args.insert(args.end(), codec.begin(), codec.end());
        for (const char* x : {"-pix_fmt", "yuv420p", "-colorspace", "bt709", "-color_range", "tv", "-flush_packets", "1"}) args.push_back(x);
        args.push_back(base + ".mkv");
        video = std::make_unique<Child>();
        if (!video->start(args, "", false, ErrTo::FILE, logp)) die("could not start ffmpeg: " + ff);
    }
    Val stop() {   // 'q' to ffmpeg, then wait: never from a thread the game needs (gfxcapture stalls if the window freezes)
        if (video) {
            video->write("q");
            if (!video->wait(20)) { eprint("ffmpeg ignored q; killing it"); video->kill_(); }
        }
        Val meta = Val::dict(); meta.d->set("video", Val::str(base + ".mkv"));
        if (audio) {
            if (!(audio->write("\n") && audio->wait(10))) audio->kill_();
            if (header.t == Val::DICT) for (const auto& [k, v] : header.d->items) meta.d->set(k, v);
            meta.d->set("audio", Val::str(base + ".audio.raw"));
            meta.d->set("audio_offset_s", Val::flt(round_n(std::chrono::duration<double>(t_video - t_audio).count() + STARTUP, 3)));
        }
        DumpOpts o; o.indent = 1; spit(to_posix(base + ".json"), dumps(meta, o));
        const std::string mkv = to_posix(base + ".mkv");
        if (!pexists(mkv) || fsize(mkv) == 0) {
            const std::string err = isfile(logp) ? cpslice(strip(text_of(slurp(logp))), -600) : "";
            eprint("WARNING: no video frames were captured. gfxcapture only delivers a frame when the window redraws: a game that is paused, minimized or "
                   "showing a static menu produces nothing (and ffmpeg can't react to 'q' until a frame arrives). Record while the game is actively rendering." +
                   (err.empty() ? std::string() : "\nffmpeg: " + err));
        }
        print("recorded " + base + ".mkv " + (audio ? "(+ audio)" : ""));
        return meta;
    }
};

struct Drive {
    Child p; std::string ready;
    explicit Drive(const std::string& proc) {
        check_platform();
        if (!p.start({ps_exe(), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", tool_path("WinDrive.ps1"), "-Proc", proc}, wsl_cwd(), true, ErrTo::INHERIT))
            die("could not start PowerShell for WinDrive");
        ready = strip(p.readline());
    }
    std::string cmd(const std::string& line, bool retry = true) {
        p.write(line + "\n");
        const std::string out = strip(p.readline());
        if (starts(out, "error") && out.find("foreground") != std::string::npos && retry) {
            cmd("focus", false);   // nobody at the PC: the foreground drifts; take it back once
            sleep_s(0.3);
            return cmd(line, false);
        }
        if (starts(out, "error")) throw PyErr("RuntimeError", line + ": " + out);
        return out;
    }
    void close() { p.close_stdin(); if (!p.wait(5)) p.kill_(); }
};

// ------------------------------------------------------------------ registry
inline std::string now_stamp() { const std::time_t t = std::time(nullptr); std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32]; std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tmv); return b; }
inline void reg_cmd(const std::string& action, const std::string& key, const std::string& value, const std::string& data, const std::string& typ) {
    const std::string exe = is_wsl() ? "reg.exe" : "reg";
    std::vector<std::string> c;
    if (action == "get") { c = {exe, "query", key}; if (!value.empty()) { c.push_back("/v"); c.push_back(value); } }
    else {
        const std::string backup = path_join(local_appdata(), "reg-backups"); makedirs(backup);
        const std::string bfile = path_join(backup, replace_all(replace_all(key, "\\", "_"), ":", "") + "-" + now_stamp() + ".reg");
        mk::run_proc({exe, "export", key, is_wsl() ? to_win(bfile) : bfile, "/y"});
        print("backup: " + bfile);
        c = {exe, "add", key, "/v", value, "/t", typ, "/d", data, "/f"};
    }
    const auto r = mk::run_proc(c);
    print(strip(replace_all(text_of(!r.out.empty() ? r.out : r.err), "\r", "")));
}

// ------------------------------------------------------------------ CLI
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    auto L = [&](const char* k) { const Val v = get(a, k); return v.is_none() ? 0LL : v.i; };
    if (c == "setup") setup(a);
    else if (c == "ps") {
        const Val ps = processes(S(a, "name"));
        for (const auto& p : *ps.l) print(rjust(pystr(get(p, "Id")), 7) + "  " + ljust(pystr(get(p, "ProcessName")), 28) + " " + pystr(get(p, "MainWindowTitle")));
    } else if (c == "kill") kill_pid(L("pid"));
    else if (c == "launch") {
        std::vector<std::string> args; const Val al = get(a, "args"); if (al.t == Val::LIST) for (const auto& x : *al.l) args.push_back(x.s);
        launch(S(a, "target"), args, truthy(get(a, "steam")));
    } else if (c == "shot") print(shot(S(a, "output"), S(a, "exe"), L("hwnd"), S(a, "title"), get(a, "scale").is_none() ? 0.0 : to_float(get(a, "scale"))));
    else if (c == "drive") {
        Drive d(S(a, "proc"));
        print(d.ready);
        try {
            const Val cmds = get(a, "commands");
            for (const auto& line : *cmds.l) print(line.s + " -> " + d.cmd(line.s, !truthy(get(a, "no_retry"))));
        } catch (...) { d.close(); throw; }
        d.close();
    } else if (c == "record") {
        std::vector<long long> crop; if (!S(a, "crop").empty()) for (const auto& v : split(S(a, "crop"), ":")) crop.push_back(std::stoll(strip(v)));
        Recorder rec(S(a, "exe"), S(a, "out"), L("hwnd"), S(a, "title"), L("fps"), !truthy(get(a, "no_audio")), crop, L("pid"));
        rec.start();
        DumpOpts o; o.indent = 1;
        try {
            if (truthy(get(a, "seconds"))) sleep_s(to_float(get(a, "seconds")));
            else { std::fputs("recording - press Enter to stop ", stdout); std::fflush(stdout); std::string l; std::getline(std::cin, l); }
        } catch (...) { print(dumps(rec.stop(), o)); throw; }
        print(dumps(rec.stop(), o));
    } else if (c == "reg") reg_cmd(S(a, "action"), S(a, "key"), S(a, "value"), S(a, "data"), S(a, "type"));
    return 0;
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("win", DOC); p.help_line = "Windows/WSL: screenshots, recording with game-only audio, input, processes"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("setup"); q.help_line = "install the PowerShell tools and a gfxcapture-capable ffmpeg"; q.opt({"--force"}).store_true().help("re-download ffmpeg"); }
    { Parser& q = p.sub("ps"); q.help_line = "processes that have a window"; q.pos("name").nargs("?"); }
    { Parser& q = p.sub("kill"); q.help_line = "kill by exact PID"; q.pos("pid").integer(); }
    { Parser& q = p.sub("launch"); q.help_line = "start a game (exe path or --steam APPID)"; q.pos("target"); q.pos("args").nargs("*"); q.opt({"--steam"}).store_true(); }
    for (const char* name : {"shot", "record"}) {
        Parser& q = p.sub(name); q.help_line = std::string(name) == "shot" ? "screenshot of a game window" : "record a game window + its audio";
        if (std::string(name) == "shot") { q.pos("output"); q.opt({"--scale"}).number().help("also write a scaled copy (e.g. 0.33) that is cheap to look at"); }
        else {
            q.opt({"--out"}).required().help("base path (Windows or WSL path); writes .mkv/.audio.raw/.json");
            q.opt({"--seconds"}).number(); q.opt({"--fps"}).integer().def(30LL); q.opt({"--no-audio"}).store_true();
            q.opt({"--crop"}).help("left:top:right:bottom px to cut (e.g. ultrawide -> centre 16:9)");
            q.opt({"--pid"}).integer().help("process to capture audio from, when --exe is ambiguous (e.g. two java.exe)");
        }
        q.opt({"--exe"}).help("window's executable name, e.g. AoE2DE_s.exe (regex; a full path is cut to its file name)");
        q.opt({"--hwnd"}).integer(); q.opt({"--title"}).help("window title regex");
    }
    { Parser& q = p.sub("drive"); q.help_line = "send WinDrive commands to a game window"; q.opt({"--proc"}).required().help("process name without .exe");
      q.pos("commands").nargs("+"); q.opt({"--no-retry"}).store_true(); }
    { Parser& q = p.sub("reg"); q.help_line = "read / set registry values (set backs the key up first)"; q.pos("action").choices({"get", "set"}); q.pos("key");
      q.pos("value").nargs("?"); q.pos("data").nargs("?"); q.opt({"--type"}).def("REG_DWORD"); }
}
}  // namespace packtools::modder::win
