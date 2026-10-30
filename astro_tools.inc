// A.S.T.R.O AI Tools manager + language-server bridge (included from astro_dev.inc).
//
//   Tools:  portable installs of compilers, runtimes, build tools and language servers into <data>/tools/<id>/.
//           Downloads come only from the projects' official hosts, are checked against the SHA-256 the publisher
//           lists (GitHub release digests, Node SHASUMS256, go.dev, Adoptium, Eclipse .sha256), and are extracted
//           by a small zip / tar.gz / gz reader that refuses paths outside the tool folder. npm packages are
//           verified by npm itself. Nothing is installed system-wide and no admin rights are needed.
//           POST /api/dev/tools/list {detect?}   /install {id}   /remove {id}
//   LSP:    one language server per (project, language), spoken to over stdio with Content-Length framing.
//           The browser does the LSP client work; the engine relays messages.
//           POST /api/dev/lsp/status {project}   /start {project, lang}   /send {id, messages}   /recv {id, after, wait}   /stop {id}


// ------------------------------------------------------------------ catalog
// asset regexes per platform key: linux-x64, mac-arm64, mac-x64, win-x64
const char* TOOLS_CATALOG = R"JSON([
 {"id":"piper","name":"Natural voice (Piper)","group":"Voice","kind":"voice","bins":["piper"],"desc":"Offline neural text-to-speech: replies are read in a natural voice generated on your computer (about 90 MB with the English voice). Piper is MIT-licensed; the English voice (LJSpeech) is public domain. Checksums pinned by A.S.T.R.O AI (the release lists none).","src":{"type":"pinned","version":"2023.11.14-2","files":{"linux-x64":{"url":"https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz","sha256":"a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992"},"linux-arm64":{"url":"https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_aarch64.tar.gz","sha256":"fea0fd2d87c54dbc7078d0f878289f404bd4d6eea6e7444a77835d1537ab88eb"},"mac-arm64":{"url":"https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_macos_aarch64.tar.gz","sha256":"6b1eb03b3735946cb35216e063e7eebcc33a6bbf5dd96ec0217959bf1cdcb0cc"},"mac-x64":{"url":"https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_macos_x64.tar.gz","sha256":"ced85c0a3df13945b1e623b878a48fdc2854d5c485b4b67f62857cf551deaf8b"},"win-x64":{"url":"https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip","sha256":"f3c58906402b24f3a96d92145f58acba6d86c9b5db896d207f78dc80811efcea"}}},"extras":[{"url":"https://huggingface.co/rhasspy/piper-voices/resolve/b4e358ffcb232591bf73a540e70f7de528e4174a/en/en_US/ljspeech/medium/en_US-ljspeech-medium.onnx","sha256":"6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22","path":"voices/en_US-ljspeech-medium.onnx"},{"url":"https://huggingface.co/rhasspy/piper-voices/resolve/b4e358ffcb232591bf73a540e70f7de528e4174a/en/en_US/ljspeech/medium/en_US-ljspeech-medium.onnx.json","sha256":"141d612cc0a95ed7efc1ca936b845c2364967f2e9217c5dbfcf69fc4d6c65860","path":"voices/en_US-ljspeech-medium.onnx.json"}],"bindirs":["piper","*"]},
 {"id":"piper-voice-fr","name":"French voice (Piper)","group":"Voice","kind":"voice","bins":[],"desc":"French voice for the natural voice (about 63 MB). Needs \"Natural voice (Piper)\". Voice: SIWIS database, CC BY 4.0.","src":{"type":"files","version":"siwis-medium"},"extras":[{"url":"https://huggingface.co/rhasspy/piper-voices/resolve/b4e358ffcb232591bf73a540e70f7de528e4174a/fr/fr_FR/siwis/medium/fr_FR-siwis-medium.onnx","sha256":"641d1ab097da2b81128c076810edb052b385decc8be3381814802a64a73baf99","path":"voices/fr_FR-siwis-medium.onnx"},{"url":"https://huggingface.co/rhasspy/piper-voices/resolve/b4e358ffcb232591bf73a540e70f7de528e4174a/fr/fr_FR/siwis/medium/fr_FR-siwis-medium.onnx.json","sha256":"39479916c2db192b5ac9764daddd0c744d83e023ad890c6976c0633ae4df8959","path":"voices/fr_FR-siwis-medium.onnx.json"}]},
 {"id":"piper-voice-ar","name":"Arabic voice (Piper)","group":"Voice","kind":"voice","bins":[],"desc":"Arabic (Jordan) voice for the natural voice (about 63 MB). Needs \"Natural voice (Piper)\". The dataset does not state a license: personal use only.","src":{"type":"files","version":"kareem-medium"},"extras":[{"url":"https://huggingface.co/rhasspy/piper-voices/resolve/b4e358ffcb232591bf73a540e70f7de528e4174a/ar/ar_JO/kareem/medium/ar_JO-kareem-medium.onnx","sha256":"9e95cab07b679da603bba17c4dec7ab3111320571964ee95c0379603c086491e","path":"voices/ar_JO-kareem-medium.onnx"},{"url":"https://huggingface.co/rhasspy/piper-voices/resolve/b4e358ffcb232591bf73a540e70f7de528e4174a/ar/ar_JO/kareem/medium/ar_JO-kareem-medium.onnx.json","sha256":"ea6d9b9d9076dbdb6bf5c98c6a141ef154959d2359709b37855727964e7d6c4d","path":"voices/ar_JO-kareem-medium.onnx.json"}]},
 {"id":"clangd","name":"clangd","group":"C / C++","kind":"lsp","bins":["clangd"],
  "desc":"Completion, errors, hover, go-to-definition and formatting for C and C++ (LLVM).",
  "src":{"type":"github","repo":"clangd/clangd","asset":{"linux-x64":"^clangd-linux-[0-9.]+\\.zip$","mac-arm64":"^clangd-mac-[0-9.]+\\.zip$","mac-x64":"^clangd-mac-[0-9.]+\\.zip$","win-x64":"^clangd-windows-[0-9.]+\\.zip$"}},"bindirs":["*/bin"]},
 {"id":"cpp-compiler","name":"C / C++ compiler","group":"C / C++","kind":"compiler","bins":["clang++","g++","c++","cl"],
  "desc":"Builds and runs C and C++. On Windows: LLVM-MinGW (clang, clang++, gcc and g++ names). On Linux and macOS the system compiler is used.",
  "src":{"type":"github","repo":"mstorsjo/llvm-mingw","asset":{"win-x64":"^llvm-mingw-[0-9]+-ucrt-x86_64\\.zip$"}},"bindirs":["*/bin"],
  "system":{"linux-x64":"sudo apt install build-essential    (Fedora: sudo dnf install gcc-c++ make)","mac-arm64":"xcode-select --install","mac-x64":"xcode-select --install"}},
 {"id":"cmake","name":"CMake","group":"C / C++","kind":"build","bins":["cmake"],"desc":"Cross-platform build system generator.",
  "src":{"type":"github","repo":"Kitware/CMake","asset":{"linux-x64":"^cmake-[0-9.]+-linux-x86_64\\.tar\\.gz$","mac-arm64":"^cmake-[0-9.]+-macos-universal\\.tar\\.gz$","mac-x64":"^cmake-[0-9.]+-macos-universal\\.tar\\.gz$","win-x64":"^cmake-[0-9.]+-windows-x86_64\\.zip$"}},"bindirs":["*/bin","*/CMake.app/Contents/bin"]},
 {"id":"ninja","name":"Ninja","group":"C / C++","kind":"build","bins":["ninja"],"desc":"Small, fast build tool (used by CMake).",
  "src":{"type":"github","repo":"ninja-build/ninja","asset":{"linux-x64":"^ninja-linux\\.zip$","mac-arm64":"^ninja-mac\\.zip$","mac-x64":"^ninja-mac\\.zip$","win-x64":"^ninja-win\\.zip$"}},"bindirs":["."]},
 {"id":"node","name":"Node.js (LTS)","group":"JavaScript / TypeScript","kind":"runtime","bins":["node","npm","npx"],"desc":"JavaScript runtime with npm. Also runs the npm-based language servers and the extension host.",
  "src":{"type":"node"},"bindirs":["*/bin","*"]},
 {"id":"typescript","name":"TypeScript 7 (native)","group":"JavaScript / TypeScript","kind":"lsp","bins":["tsc"],
  "desc":"The TypeScript compiler and its built-in language server: completion, errors, hover and go-to-definition for .js .jsx .ts .tsx.","src":{"type":"npm","packages":["typescript"]}},
 {"id":"vscode-langservers","name":"HTML, CSS & JSON servers","group":"Web","kind":"lsp","bins":["vscode-html-language-server","vscode-css-language-server","vscode-json-language-server"],
  "desc":"The language servers VS Code uses for HTML, CSS/SCSS/Less and JSON.","src":{"type":"npm","packages":["vscode-langservers-extracted"]}},
 {"id":"python","name":"Python 3.13 (portable)","group":"Python","kind":"runtime","bins":["python3","python"],"desc":"A standalone CPython build (python-build-standalone) for running your Python projects.",
  "src":{"type":"github","repo":"astral-sh/python-build-standalone","asset":{"linux-x64":"^cpython-3\\.13\\.[0-9]+\\+[0-9]+-x86_64-unknown-linux-gnu-install_only\\.tar\\.gz$","mac-arm64":"^cpython-3\\.13\\.[0-9]+\\+[0-9]+-aarch64-apple-darwin-install_only\\.tar\\.gz$","mac-x64":"^cpython-3\\.13\\.[0-9]+\\+[0-9]+-x86_64-apple-darwin-install_only\\.tar\\.gz$","win-x64":"^cpython-3\\.13\\.[0-9]+\\+[0-9]+-x86_64-pc-windows-msvc-install_only\\.tar\\.gz$"}},"bindirs":["python/bin","python"]},
 {"id":"pyright","name":"Pyright","group":"Python","kind":"lsp","bins":["pyright-langserver","pyright"],"desc":"Fast type checker and language server for Python.","src":{"type":"npm","packages":["pyright"]}},
 {"id":"rust-analyzer","name":"rust-analyzer","group":"Rust","kind":"lsp","bins":["rust-analyzer"],"desc":"The Rust language server.",
  "src":{"type":"github","repo":"rust-lang/rust-analyzer","asset":{"linux-x64":"^rust-analyzer-x86_64-unknown-linux-gnu\\.gz$","mac-arm64":"^rust-analyzer-aarch64-apple-darwin\\.gz$","mac-x64":"^rust-analyzer-x86_64-apple-darwin\\.gz$","win-x64":"^rust-analyzer-x86_64-pc-windows-msvc\\.zip$"}},"bindirs":["."]},
 {"id":"rust","name":"Rust toolchain","group":"Rust","kind":"compiler","bins":["cargo","rustc"],"desc":"rustc and cargo. Rust's own installer (rustup) is the supported way to get them.",
  "src":{"type":"system"},"system":{"*":"Install with rustup: https://rustup.rs  (Linux/macOS: curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh)"}},
 {"id":"go","name":"Go","group":"Go","kind":"runtime","bins":["go","gofmt"],"desc":"The Go toolchain.","src":{"type":"go"},"bindirs":["go/bin"]},
 {"id":"gopls","name":"gopls","group":"Go","kind":"lsp","bins":["gopls"],"desc":"The Go language server.","src":{"type":"goinstall","package":"golang.org/x/tools/gopls@latest"}},
 {"id":"jdk","name":"Java 21 JDK (Temurin)","group":"Java","kind":"runtime","bins":["java","javac"],"desc":"Eclipse Temurin JDK from Adoptium.","src":{"type":"adoptium","feature":21},"bindirs":["*/bin","*/Contents/Home/bin"]},
 {"id":"jdtls","name":"Eclipse JDT language server","group":"Java","kind":"lsp","bins":[],"desc":"The Java language server (needs a JDK 21+).",
  "src":{"type":"url","url":"https://download.eclipse.org/jdtls/snapshots/jdt-language-server-latest.tar.gz","sha256_url":"https://download.eclipse.org/jdtls/snapshots/jdt-language-server-latest.tar.gz.sha256"},"bindirs":[]},
 {"id":"bash-language-server","name":"Bash language server","group":"Shell","kind":"lsp","bins":["bash-language-server"],"desc":"Completion and errors for shell scripts.","src":{"type":"npm","packages":["bash-language-server"]}},
 {"id":"yaml-language-server","name":"YAML language server","group":"YAML","kind":"lsp","bins":["yaml-language-server"],"desc":"Validation and completion for YAML (with JSON schemas).","src":{"type":"npm","packages":["yaml-language-server"]}},
 {"id":"lua-language-server","name":"Lua language server","group":"Lua","kind":"lsp","bins":["lua-language-server"],"desc":"The Lua language server (LuaLS).",
  "src":{"type":"github","repo":"LuaLS/lua-language-server","asset":{"linux-x64":"^lua-language-server-[0-9.]+-linux-x64\\.tar\\.gz$","mac-arm64":"^lua-language-server-[0-9.]+-darwin-arm64\\.tar\\.gz$","mac-x64":"^lua-language-server-[0-9.]+-darwin-x64\\.tar\\.gz$","win-x64":"^lua-language-server-[0-9.]+-win32-x64\\.zip$"}},"bindirs":["bin"]},
 {"id":"intelephense","name":"Intelephense (PHP)","group":"PHP","kind":"lsp","bins":["intelephense"],"desc":"PHP language server (the free features).","src":{"type":"npm","packages":["intelephense"]}}
])JSON";

// language -> server. tool = catalog id that provides it, bin = executable, exts = file extensions it handles.
struct LspLang { const char* lang; const char* exts; const char* tool; const char* bin; std::vector<std::string> args; const char* lang_id; };
const std::vector<LspLang>& lsp_langs() {
    static const std::vector<LspLang> v = {
        {"cpp", "c h cpp cc cxx hpp hh hxx ino", "clangd", "clangd", {"--background-index", "--header-insertion=never", "--clang-tidy=false"}, "cpp"},
        {"typescript", "js mjs cjs jsx ts mts cts tsx", "typescript", "tsc", {"--lsp", "--stdio"}, "typescript"},
        {"python", "py pyi", "pyright", "pyright-langserver", {"--stdio"}, "python"},
        {"html", "html htm", "vscode-langservers", "vscode-html-language-server", {"--stdio"}, "html"},
        {"css", "css scss less", "vscode-langservers", "vscode-css-language-server", {"--stdio"}, "css"},
        {"json", "json jsonc", "vscode-langservers", "vscode-json-language-server", {"--stdio"}, "json"},
        {"rust", "rs", "rust-analyzer", "rust-analyzer", {}, "rust"},
        {"go", "go", "gopls", "gopls", {}, "go"},
        {"java", "java", "jdtls", "java", {}, "java"},
        {"bash", "sh bash zsh", "bash-language-server", "bash-language-server", {"start"}, "shellscript"},
        {"yaml", "yaml yml", "yaml-language-server", "yaml-language-server", {"--stdio"}, "yaml"},
        {"lua", "lua", "lua-language-server", "lua-language-server", {}, "lua"},
        {"php", "php", "intelephense", "intelephense", {"--stdio"}, "php"},
    };
    return v;
}

const Json& tools_catalog() { static const Json j = JsonParser(TOOLS_CATALOG).parse(); return j; }
const Json* tool_spec(const std::string& id) {
    if (const auto* a = tools_catalog().array_ptr()) for (const auto& t : *a) if (t.get("id") && t.get("id")->string_or() == id) return &t;
    return nullptr;
}
// UTF-8 string -> path (wide on Windows)
fs::path tpath(const std::string& s) {
#ifdef _WIN32
    return fs::path(winp::widen(s));
#else
    return fs::path(s);
#endif
}

std::string tools_platform() {
#if defined(_WIN32)
#  if defined(_M_ARM64) || defined(__aarch64__)
    return "win-arm64";
#  else
    return "win-x64";
#  endif
#elif defined(__APPLE__)
#  if defined(__aarch64__) || defined(__arm64__)
    return "mac-arm64";
#  else
    return "mac-x64";
#  endif
#else
#  if defined(__aarch64__)
    return "linux-arm64";
#  else
    return "linux-x64";
#  endif
#endif
}
// Windows on ARM runs x64 programs through emulation, so x64 downloads are a fallback there.
const Json* tools_plat_entry(const Json* map, const std::string& plat) {
    if (!map) return nullptr;
    if (const Json* e = map->get(plat)) return e;
    if (plat == "win-arm64") return map->get("win-x64");
    return nullptr;
}
fs::path tools_root() { std::error_code ec; const fs::path p = fs::path(data_dir()) / "tools"; fs::create_directories(p, ec); return p; }
fs::path tools_manifest_path() { return tools_root() / "installed.json"; }

std::mutex g_tools_mu;                    // guards the manifest file and g_tool_jobs
struct ToolJob { std::string state = "running", step, message, version; double progress = 0; long long bytes = 0, total = 0; long long started = 0; };
std::map<std::string, ToolJob> g_tool_jobs;

Json tools_manifest_locked() {
    const auto text = read_small(tools_manifest_path(), 4'000'000);
    if (!text) return Json(Json::Object{});
    try { Json j = JsonParser(*text).parse(); if (j.is_object()) return j; } catch (...) {}
    return Json(Json::Object{});
}
void tools_manifest_save_locked(const Json& m) {
    const fs::path p = tools_manifest_path(), tmp = p.string() + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << dump_json(m); }
    std::error_code ec; fs::rename(tmp, p, ec);
}

// ------------------------------------------------------------------ PATH: installed tools come first
std::vector<std::string> tools_bin_dirs_locked() {
    std::vector<std::string> out; std::error_code ec;
    const Json m = tools_manifest_locked();
    if (const auto* o = m.object_ptr()) for (const auto& [id, t] : *o) for (const auto& d : json_strings(t.get("bindirs"))) if (fs::is_directory(tpath(d), ec)) out.push_back(d);
    return out;
}
std::string tools_path_prefix(char sep) {
    std::lock_guard<std::mutex> lk(g_tools_mu);
    std::string p; for (const auto& d : tools_bin_dirs_locked()) { p += d; p += sep; }
    return p;
}
// Re-applies PATH (Library tools, then installed tools, then the original PATH). Never call with g_tools_mu held.
void tools_refresh_path() { lib_tools_path_refresh(); }

// Finds an executable on the (refreshed) PATH. Returns the full path or "".
std::string tools_which(const std::string& name) {
#ifdef _WIN32
    const std::string path = winp::getenv_s8("PATH"); const char sep = ';';
    const std::vector<std::string> exts = {".exe", ".cmd", ".bat", ""};
#else
    const std::string path = std::getenv("PATH") ? std::getenv("PATH") : ""; const char sep = ':';
    const std::vector<std::string> exts = {""};
#endif
    std::size_t s = 0;
    while (s <= path.size()) {
        std::size_t e = path.find(sep, s); if (e == std::string::npos) e = path.size();
        const std::string dir = path.substr(s, e - s); s = e + 1;
        if (dir.empty()) continue;
        for (const auto& x : exts) {
            std::error_code ec; const fs::path f = fs::path(dir) / (name + x);
            if (fs::is_regular_file(f, ec)) {
#ifndef _WIN32
                if (access(f.c_str(), X_OK) != 0) continue;
#endif
                return f.string();
            }
        }
    }
    return "";
}

// Executable 'bin' inside an installed tool's own folders, or "".
std::string tools_installed_bin(const std::string& tool, const std::string& bin) {
    std::vector<std::string> dirs;
    { std::lock_guard<std::mutex> lk(g_tools_mu); const Json m = tools_manifest_locked(); if (const Json* t = m.get(tool)) dirs = json_strings(t->get("bindirs")); }
    std::error_code ec;
    for (const auto& d : dirs) for (const char* x : {"", ".exe", ".cmd"}) { const fs::path f = tpath(d) / (bin + x); if (fs::is_regular_file(f, ec)) return f.string(); }
    return "";
}

// ------------------------------------------------------------------ downloads
bool tools_host_ok(const std::string& url) {
    if (url.rfind("https://", 0) != 0) return false;
    const std::string rest = url.substr(8); const std::string host = rest.substr(0, rest.find_first_of("/:?#"));
    static const std::set<std::string> ok = {"api.github.com", "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com",
                                             "nodejs.org", "go.dev", "dl.google.com", "api.adoptium.net", "download.eclipse.org",
                                             "open-vsx.org", "openvsxorg.blob.core.windows.net", "huggingface.co"};
    return ok.count(host) > 0;
}
struct Sha256Stream {
#if defined(__APPLE__)
    CC_SHA256_CTX c; Sha256Stream() { CC_SHA256_Init(&c); } void update(const void* p, std::size_t n) { CC_SHA256_Update(&c, p, static_cast<CC_LONG>(n)); }
    std::string hex() { unsigned char o[32]; CC_SHA256_Final(o, &c); return to_hex(o); }
#else
    EVP_MD_CTX* c; Sha256Stream() : c(EVP_MD_CTX_new()) { EVP_DigestInit_ex(c, EVP_sha256(), nullptr); } ~Sha256Stream() { EVP_MD_CTX_free(c); }
    void update(const void* p, std::size_t n) { EVP_DigestUpdate(c, p, n); }
    std::string hex() { unsigned char o[32]; unsigned int l = 0; EVP_DigestFinal_ex(c, o, &l); return to_hex(o); }
#endif
    static std::string to_hex(const unsigned char* o) { static const char* h = "0123456789abcdef"; std::string s; for (int i = 0; i < 32; ++i) { s += h[o[i] >> 4]; s += h[o[i] & 15]; } return s; }
};
curl_slist* tools_headers(bool github_api) {
    curl_slist* h = curl_slist_append(nullptr, "User-Agent: AstroAI-Tools/1.6 (+https://esoexe.github.io/astro-ai/)");
    if (github_api) h = curl_slist_append(h, "Accept: application/vnd.github+json");
    return h;
}
void tools_curl_common(CURL* c) {
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L); curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
}
std::string tools_get_text(const std::string& url) {
    if (!tools_host_ok(url)) throw std::runtime_error("Blocked download host: " + url);
    CURL* c = curl_easy_init(); if (!c) throw std::runtime_error("curl init failed");
    std::string out; curl_slist* h = tools_headers(url.find("api.github.com") != std::string::npos);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str()); curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write); curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L); curl_easy_setopt(c, CURLOPT_MAXFILESIZE, 30L * 1024 * 1024); tools_curl_common(c);
    const CURLcode rc = curl_easy_perform(c); long st = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &st);
    curl_slist_free_all(h); curl_easy_cleanup(c);
    if (rc != CURLE_OK) throw std::runtime_error(std::string("Download failed: ") + curl_easy_strerror(rc));
    if (st >= 400) throw std::runtime_error("HTTP " + std::to_string(st) + " from " + url);
    return out;
}
struct DlCtx { std::FILE* f = nullptr; Sha256Stream sha; ToolJob* job = nullptr; std::string id; };
std::size_t tools_dl_write(char* p, std::size_t s, std::size_t n, void* ud) {
    auto* d = static_cast<DlCtx*>(ud); const std::size_t len = s * n;
    if (std::fwrite(p, 1, len, d->f) != len) return 0;
    d->sha.update(p, len);
    return len;
}
int tools_dl_progress(void* ud, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* d = static_cast<DlCtx*>(ud);
    std::lock_guard<std::mutex> lk(g_tools_mu);
    auto it = g_tool_jobs.find(d->id);
    if (it != g_tool_jobs.end()) { it->second.bytes = now; it->second.total = total; it->second.progress = total > 0 ? 0.05 + 0.75 * double(now) / double(total) : 0.1; if (it->second.state == "cancel") return 1; }
    return 0;
}
// Downloads url to file, returns its sha256 (lower-case hex).
std::string tools_download(const std::string& id, const std::string& url, const fs::path& file) {
    if (!tools_host_ok(url)) throw std::runtime_error("Blocked download host: " + url);
    CURL* c = curl_easy_init(); if (!c) throw std::runtime_error("curl init failed");
    DlCtx d; d.id = id;
#ifdef _WIN32
    d.f = _wfopen(file.wstring().c_str(), L"wb");
#else
    d.f = std::fopen(file.c_str(), "wb");
#endif
    if (!d.f) { curl_easy_cleanup(c); throw std::runtime_error("Could not write " + file.string()); }
    curl_slist* h = tools_headers(false); h = curl_slist_append(h, "Accept: application/octet-stream");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str()); curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, tools_dl_write); curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, tools_dl_progress); curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d); curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(900) * 1024 * 1024);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L); curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L); tools_curl_common(c);
    const CURLcode rc = curl_easy_perform(c); long st = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &st);
    curl_slist_free_all(h); curl_easy_cleanup(c); std::fclose(d.f);
    if (rc == CURLE_ABORTED_BY_CALLBACK) throw std::runtime_error("Cancelled.");
    if (rc != CURLE_OK) throw std::runtime_error(std::string("Download failed: ") + curl_easy_strerror(rc));
    if (st >= 400) throw std::runtime_error("HTTP " + std::to_string(st) + " for " + url);
    return d.sha.hex();
}

// ------------------------------------------------------------------ extraction (zip, tar.gz, gz)
// Joins an archive member name to root; returns empty if it would escape root (zip-slip) or is absolute.
fs::path tools_safe_join(const fs::path& root, std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name[0] == '/' || (name.size() > 1 && name[1] == ':')) return {};
    fs::path out = root;
    std::stringstream ss(name); std::string part;
    while (std::getline(ss, part, '/')) { if (part.empty() || part == ".") continue; if (part == "..") return {}; out /= tpath(part); }
    return out;
}
void tools_set_mode(const fs::path& p, unsigned mode) {
#ifndef _WIN32
    if (mode & 0111) chmod(p.c_str(), 0755); else chmod(p.c_str(), 0644);
#else
    (void)p; (void)mode;
#endif
}
bool tools_symlink(const fs::path& root, const fs::path& at, const std::string& target) {
#ifndef _WIN32
    if (target.empty() || target[0] == '/') return false;
    const fs::path resolved = (at.parent_path() / target).lexically_normal();
    const std::string r = resolved.string(), base = root.lexically_normal().string();
    if (r.rfind(base, 0) != 0) return false;           // must stay inside the tool folder
    std::error_code ec; fs::remove(at, ec); fs::create_directories(at.parent_path(), ec);
    return symlink(target.c_str(), at.c_str()) == 0;
#else
    (void)root; (void)at; (void)target; return false;
#endif
}

// Streaming tar reader fed with decompressed bytes.
struct TarReader {
    fs::path root; std::string buf; std::string long_name, pax_path, long_link;
    long long remaining = 0, pad = 0; std::ofstream out; fs::path cur; unsigned cur_mode = 0; bool skipping = false; bool done = false;
    int kind = 0;           // 0 file, 1 longname, 2 pax, 3 longlink
    std::string meta;
    static long long octal(const char* p, int n) { long long v = 0; for (int i = 0; i < n && p[i]; ++i) { if (p[i] == ' ') continue; if (p[i] < '0' || p[i] > '7') break; v = v * 8 + (p[i] - '0'); } return v; }
    void feed(const char* data, std::size_t n) {
        buf.append(data, n);
        std::size_t off = 0;
        while (!done) {
            if (remaining > 0) {
                const std::size_t take = static_cast<std::size_t>(std::min<long long>(remaining, static_cast<long long>(buf.size() - off)));
                if (!take) break;
                if (kind == 0) { if (!skipping && out.is_open()) out.write(buf.data() + off, static_cast<std::streamsize>(take)); }
                else meta.append(buf, off, take);
                off += take; remaining -= static_cast<long long>(take);
                if (remaining == 0) finish_entry();
                continue;
            }
            if (pad > 0) { const std::size_t take = static_cast<std::size_t>(std::min<long long>(pad, static_cast<long long>(buf.size() - off))); if (!take) break; off += take; pad -= static_cast<long long>(take); continue; }
            if (buf.size() - off < 512) break;
            const char* h = buf.data() + off; off += 512;
            bool zero = true; for (int i = 0; i < 512; ++i) if (h[i]) { zero = false; break; }
            if (zero) continue;
            std::string name(h, strnlen(h, 100)); const std::string prefix(h + 345, strnlen(h + 345, 155));
            if (!prefix.empty() && std::string(h + 257, 5) == "ustar") name = prefix + "/" + name;
            if (!long_name.empty()) { name = long_name; long_name.clear(); }
            if (!pax_path.empty()) { name = pax_path; pax_path.clear(); }
            const long long size = octal(h + 124, 12); const unsigned mode = static_cast<unsigned>(octal(h + 100, 8)); const char type = h[156];
            std::string link(h + 157, strnlen(h + 157, 100)); if (!long_link.empty()) { link = long_link; long_link.clear(); }
            pad = (512 - size % 512) % 512; remaining = size; meta.clear(); skipping = false; cur.clear();
            if (type == 'L') { kind = 1; if (!remaining) finish_entry(); continue; }
            if (type == 'K') { kind = 3; if (!remaining) finish_entry(); continue; }
            if (type == 'x') { kind = 2; if (!remaining) finish_entry(); continue; }
            kind = 0;
            const fs::path dest = tools_safe_join(root, name);
            if (dest.empty()) { skipping = true; if (!remaining) finish_entry(); continue; }
            std::error_code ec;
            if (type == '5') { fs::create_directories(dest, ec); skipping = true; }
            else if (type == '2') { tools_symlink(root, dest, link); skipping = true; }
            else if (type == '0' || type == '\0' || type == '7') { fs::create_directories(dest.parent_path(), ec); out.open(dest, std::ios::binary | std::ios::trunc); cur = dest; cur_mode = mode; if (!out) skipping = true; }
            else skipping = true;     // hard links, devices, global pax headers
            if (!remaining) finish_entry();
        }
        buf.erase(0, off);
    }
    void finish_entry() {
        if (kind == 1) long_name = meta.c_str();
        else if (kind == 3) long_link = meta.c_str();
        else if (kind == 2) {   // pax: "len key=value\n"
            std::size_t p = 0;
            while (p < meta.size()) {
                const std::size_t sp = meta.find(' ', p); if (sp == std::string::npos) break;
                const long long len = std::atoll(meta.substr(p, sp - p).c_str()); if (len <= 0) break;
                const std::string rec = meta.substr(sp + 1, static_cast<std::size_t>(len) - (sp - p) - 2);
                const auto eq = rec.find('=');
                if (eq != std::string::npos) { const std::string k = rec.substr(0, eq), v = rec.substr(eq + 1); if (k == "path") pax_path = v; else if (k == "linkpath") long_link = v; }
                p += static_cast<std::size_t>(len);
            }
        } else if (out.is_open()) { out.close(); tools_set_mode(cur, cur_mode); }
        kind = 0; meta.clear();
    }
};
// gzip file -> sink(bytes)
void tools_gunzip(const fs::path& file, const std::function<void(const char*, std::size_t)>& sink) {
    std::ifstream in(file, std::ios::binary); if (!in) throw std::runtime_error("Cannot open the download.");
    z_stream z{}; if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) throw std::runtime_error("zlib init failed");
    std::vector<char> ibuf(1 << 16), obuf(1 << 18); int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        in.read(ibuf.data(), static_cast<std::streamsize>(ibuf.size())); const auto got = in.gcount();
        if (got <= 0) break;
        z.next_in = reinterpret_cast<Bytef*>(ibuf.data()); z.avail_in = static_cast<uInt>(got);
        while (z.avail_in > 0) {
            z.next_out = reinterpret_cast<Bytef*>(obuf.data()); z.avail_out = static_cast<uInt>(obuf.size());
            rc = inflate(&z, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&z); throw std::runtime_error("The archive is damaged (gzip)."); }
            sink(obuf.data(), obuf.size() - z.avail_out);
            if (rc == Z_STREAM_END) { if (z.avail_in > 0) { inflateReset(&z); continue; } break; }   // concatenated members
        }
    }
    inflateEnd(&z);
}
void tools_extract_targz(const fs::path& file, const fs::path& root) { TarReader t; t.root = root; tools_gunzip(file, [&](const char* p, std::size_t n) { t.feed(p, n); }); }
void tools_extract_gz_single(const fs::path& file, const fs::path& dest) {
    std::error_code ec; fs::create_directories(dest.parent_path(), ec);
    std::ofstream o(dest, std::ios::binary | std::ios::trunc); tools_gunzip(file, [&](const char* p, std::size_t n) { o.write(p, static_cast<std::streamsize>(n)); }); o.close();
    tools_set_mode(dest, 0755);
}
void tools_extract_zip(const fs::path& file, const fs::path& root) {
    std::ifstream in(file, std::ios::binary); if (!in) throw std::runtime_error("Cannot open the download.");
    in.seekg(0, std::ios::end); const long long fsize = in.tellg();
    auto rd = [&](long long at, std::size_t n) { std::string b(n, '\0'); in.seekg(at); in.read(b.data(), static_cast<std::streamsize>(n)); if (in.gcount() != static_cast<std::streamsize>(n)) throw std::runtime_error("The archive is damaged (zip)."); return b; };
    auto u16 = [](const std::string& b, std::size_t o) { return static_cast<unsigned>(static_cast<unsigned char>(b[o]) | (static_cast<unsigned char>(b[o + 1]) << 8)); };
    auto u32 = [&](const std::string& b, std::size_t o) { return static_cast<unsigned long long>(u16(b, o) | (static_cast<unsigned long long>(u16(b, o + 2)) << 16)); };
    auto u64 = [&](const std::string& b, std::size_t o) { return u32(b, o) | (u32(b, o + 4) << 32); };
    const long long tail = std::min<long long>(fsize, 65557); const std::string t = rd(fsize - tail, static_cast<std::size_t>(tail));
    long long eocd = -1; for (long long i = tail - 22; i >= 0; --i) if (t.compare(static_cast<std::size_t>(i), 4, "PK\x05\x06") == 0) { eocd = i; break; }
    if (eocd < 0) throw std::runtime_error("Not a zip file.");
    unsigned long long count = u16(t, static_cast<std::size_t>(eocd) + 10), cd_off = u32(t, static_cast<std::size_t>(eocd) + 16);
    if (count == 0xFFFF || cd_off == 0xFFFFFFFFull) {      // zip64
        if (eocd >= 20 && t.compare(static_cast<std::size_t>(eocd) - 20, 4, "PK\x06\x07") == 0) {
            const unsigned long long z64 = u64(t, static_cast<std::size_t>(eocd) - 20 + 8); const std::string r = rd(static_cast<long long>(z64), 56);
            count = u64(r, 32); cd_off = u64(r, 48);
        }
    }
    long long p = static_cast<long long>(cd_off);
    std::vector<char> obuf(1 << 18), ibuf(1 << 16);
    for (unsigned long long i = 0; i < count; ++i) {
        const std::string h = rd(p, 46); if (h.compare(0, 4, "PK\x01\x02") != 0) throw std::runtime_error("The archive is damaged (zip directory).");
        const unsigned method = u16(h, 10); unsigned long long csize = u32(h, 20), usize = u32(h, 24);
        const unsigned nlen = u16(h, 28), xlen = u16(h, 30), clen = u16(h, 32); const unsigned long long attr = u32(h, 38); unsigned long long loff = u32(h, 42);
        const std::string name = rd(p + 46, nlen); const std::string extra = xlen ? rd(p + 46 + nlen, xlen) : std::string();
        for (std::size_t x = 0; x + 4 <= extra.size();) {        // zip64 sizes
            const unsigned id = u16(extra, x), sz = u16(extra, x + 2); std::size_t q = x + 4;
            if (id == 1) { if (usize == 0xFFFFFFFFull && q + 8 <= extra.size()) { usize = u64(extra, q); q += 8; } if (csize == 0xFFFFFFFFull && q + 8 <= extra.size()) { csize = u64(extra, q); q += 8; } if (loff == 0xFFFFFFFFull && q + 8 <= extra.size()) { loff = u64(extra, q); } }
            x += 4 + sz;
        }
        p += 46 + nlen + xlen + clen;
        const fs::path dest = tools_safe_join(root, name); if (dest.empty()) continue;
        std::error_code ec;
        if (!name.empty() && (name.back() == '/' || name.back() == '\\')) { fs::create_directories(dest, ec); continue; }
        const unsigned mode = static_cast<unsigned>(attr >> 16);
        const std::string lh = rd(static_cast<long long>(loff), 30); const long long data = static_cast<long long>(loff) + 30 + u16(lh, 26) + u16(lh, 28);
        fs::create_directories(dest.parent_path(), ec);
        if ((mode & 0170000) == 0120000 && method == 0) { tools_symlink(root, dest, rd(data, static_cast<std::size_t>(csize))); continue; }
        std::ofstream o(dest, std::ios::binary | std::ios::trunc); if (!o) continue;
        in.seekg(data);
        if (method == 0) {
            unsigned long long left = csize;
            while (left) { const std::size_t n = static_cast<std::size_t>(std::min<unsigned long long>(left, ibuf.size())); in.read(ibuf.data(), static_cast<std::streamsize>(n)); o.write(ibuf.data(), static_cast<std::streamsize>(n)); left -= n; }
        } else if (method == 8) {
            z_stream z{}; inflateInit2(&z, -MAX_WBITS); unsigned long long left = csize; int rc = Z_OK;
            while (rc != Z_STREAM_END) {
                if (z.avail_in == 0) { if (!left) break; const std::size_t n = static_cast<std::size_t>(std::min<unsigned long long>(left, ibuf.size())); in.read(ibuf.data(), static_cast<std::streamsize>(n)); left -= n; z.next_in = reinterpret_cast<Bytef*>(ibuf.data()); z.avail_in = static_cast<uInt>(n); }
                z.next_out = reinterpret_cast<Bytef*>(obuf.data()); z.avail_out = static_cast<uInt>(obuf.size());
                rc = inflate(&z, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) { inflateEnd(&z); throw std::runtime_error("The archive is damaged (deflate) at " + name); }
                o.write(obuf.data(), static_cast<std::streamsize>(obuf.size() - z.avail_out));
            }
            inflateEnd(&z);
        } else throw std::runtime_error("Unsupported zip compression in " + name);
        o.close();
        tools_set_mode(dest, mode ? mode : 0644);
#ifndef _WIN32
        if (!mode) { const std::string fn = dest.filename().string(); if (dest.parent_path().filename() == "bin" || fn.find('.') == std::string::npos) chmod(dest.c_str(), 0755); }
#endif
    }
}

// Expands "*/bin" style patterns (one '*' = any single folder) under root to existing folders.
std::vector<fs::path> tools_glob_dirs(const fs::path& root, const std::string& pat) {
    std::vector<fs::path> cur = {root};
    std::stringstream ss(pat); std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty() || part == ".") continue;
        std::vector<fs::path> next; std::error_code ec;
        for (const auto& c : cur) {
            if (part == "*") { for (fs::directory_iterator it(c, ec), e; !ec && it != e; it.increment(ec)) if (it->is_directory(ec)) next.push_back(it->path()); }
            else if (fs::is_directory(c / part, ec)) next.push_back(c / part);
        }
        cur.swap(next);
    }
    return cur;
}
bool tools_dir_has_bin(const fs::path& dir, const std::vector<std::string>& bins) {
    std::error_code ec;
    if (bins.empty()) return true;
    for (const auto& b : bins) for (const char* x : {"", ".exe", ".cmd", ".bat"}) if (fs::exists(dir / (b + x), ec)) return true;
    return false;
}

void tool_job_step(const std::string& id, const std::string& step, double progress) {
    std::lock_guard<std::mutex> lk(g_tools_mu); auto& j = g_tool_jobs[id]; j.step = step; if (progress >= 0) j.progress = progress;
}
std::string tools_find_npm_cli(std::vector<std::string>& prefix_args) {
    // Portable Node first (node + npm-cli.js), else the system npm.
    std::lock_guard<std::mutex> lk(g_tools_mu);
    const Json m = tools_manifest_locked();
    if (const Json* n = m.get("node")) {
        const fs::path dir = n->get("dir") ? tpath(n->get("dir")->string_or()) : fs::path();
        for (const auto& d : tools_glob_dirs(dir, "*")) {
            std::error_code ec;
#ifdef _WIN32
            const fs::path node = d / "node.exe", cli = d / "node_modules" / "npm" / "bin" / "npm-cli.js";
#else
            const fs::path node = d / "bin" / "node", cli = d / "lib" / "node_modules" / "npm" / "bin" / "npm-cli.js";
#endif
            if (fs::exists(node, ec) && fs::exists(cli, ec)) { prefix_args = {node.string(), cli.string()}; return node.parent_path().string(); }
        }
    }
    return "";
}

// Installs one tool. Runs on its own thread; progress goes to g_tool_jobs[id].
void tools_install_run(const std::string id) {
    const Json* spec = tool_spec(id);
    const std::string plat = tools_platform();
    const fs::path base = tools_root() / id; const fs::path stage = tools_root() / (".stage-" + id); const fs::path dl = tools_root() / (".dl-" + id);
    std::error_code ec; fs::remove_all(stage, ec); fs::create_directories(stage, ec);
    std::string version, verified = "sha256", source_url;
    auto fail = [&](const std::string& msg) { std::lock_guard<std::mutex> lk(g_tools_mu); auto& j = g_tool_jobs[id]; j.state = "error"; j.message = msg; };
    try {
        if (!spec) throw std::runtime_error("Unknown tool.");
        const Json& src = *spec->get("src");
        const std::string type = src.get("type") ? src.get("type")->string_or() : "";
        std::string url, want_sha, fname;
        if (type == "github") {
            const Json* rx = tools_plat_entry(src.get("asset"), plat);
            if (!rx) throw std::runtime_error("Not available for this system.");
            tool_job_step(id, "Finding the latest release", 0.02);
            const Json rel = JsonParser(tools_get_text("https://api.github.com/repos/" + src.get("repo")->string_or() + "/releases/latest")).parse();
            version = rel.get("tag_name") ? rel.get("tag_name")->string_or() : "";
            const std::regex re(rx->string_or());
            if (const auto* as = rel.get("assets") ? rel.get("assets")->array_ptr() : nullptr)
                for (const auto& a : *as) {
                    const std::string n = a.get("name") ? a.get("name")->string_or() : "";
                    if (!std::regex_search(n, re)) continue;
                    fname = n; url = a.get("browser_download_url") ? a.get("browser_download_url")->string_or() : "";
                    const std::string dg = a.get("digest") ? a.get("digest")->string_or() : "";
                    if (dg.rfind("sha256:", 0) == 0) want_sha = dg.substr(7);
                    break;
                }
            if (url.empty()) throw std::runtime_error("No download for this system in release " + version + ".");
        } else if (type == "node") {
            tool_job_step(id, "Finding the latest LTS", 0.02);
            const Json idx = JsonParser(tools_get_text("https://nodejs.org/dist/index.json")).parse();
            if (const auto* a = idx.array_ptr()) for (const auto& r : *a) { const Json* lts = r.get("lts"); if (lts && lts->string_ptr()) { version = r.get("version")->string_or(); break; } }
            if (version.empty()) throw std::runtime_error("Could not find a Node.js LTS release.");
            const std::string p = plat == "win-x64" ? "win-x64" : plat == "win-arm64" ? "win-arm64" : plat == "mac-arm64" ? "darwin-arm64" : plat == "mac-x64" ? "darwin-x64" : plat == "linux-arm64" ? "linux-arm64" : "linux-x64";
            fname = "node-" + version + "-" + p + (plat.rfind("win", 0) == 0 ? ".zip" : ".tar.gz");
            url = "https://nodejs.org/dist/" + version + "/" + fname;
            const std::string sums = tools_get_text("https://nodejs.org/dist/" + version + "/SHASUMS256.txt");
            std::stringstream ss(sums); std::string line;
            while (std::getline(ss, line)) { const auto sp = line.find("  "); if (sp != std::string::npos && line.substr(sp + 2) == fname) { want_sha = line.substr(0, sp); break; } }
        } else if (type == "go") {
            tool_job_step(id, "Finding the latest Go", 0.02);
            const Json rel = JsonParser(tools_get_text("https://go.dev/dl/?mode=json")).parse();
            const std::string os = plat.rfind("win", 0) == 0 ? "windows" : plat.rfind("mac", 0) == 0 ? "darwin" : "linux", arch = plat.find("arm64") != std::string::npos ? "arm64" : "amd64";
            if (const auto* a = rel.array_ptr(); a && !a->empty())
                if (const auto* files = (*a)[0].get("files") ? (*a)[0].get("files")->array_ptr() : nullptr)
                    for (const auto& f : *files)
                        if (f.get("os")->string_or() == os && f.get("arch")->string_or() == arch && f.get("kind")->string_or() == "archive") {
                            fname = f.get("filename")->string_or(); want_sha = f.get("sha256")->string_or(); version = f.get("version")->string_or(); url = "https://go.dev/dl/" + fname; break;
                        }
            if (url.empty()) throw std::runtime_error("No Go download for this system.");
        } else if (type == "adoptium") {
            tool_job_step(id, "Finding the latest JDK", 0.02);
            const std::string os = plat.rfind("win", 0) == 0 ? "windows" : plat.rfind("mac", 0) == 0 ? "mac" : "linux", arch = plat.find("arm64") != std::string::npos ? "aarch64" : "x64";
            const int feat = src.get("feature") ? static_cast<int>(std::get<double>(src.get("feature")->value)) : 21;
            const Json r = JsonParser(tools_get_text("https://api.adoptium.net/v3/assets/latest/" + std::to_string(feat) + "/hotspot?architecture=" + arch + "&image_type=jdk&os=" + os + "&vendor=eclipse")).parse();
            if (const auto* a = r.array_ptr(); a && !a->empty()) {
                const Json* pkg = (*a)[0].get("binary") ? (*a)[0].get("binary")->get("package") : nullptr;
                if (pkg) { url = pkg->get("link")->string_or(); want_sha = pkg->get("checksum")->string_or(); fname = pkg->get("name")->string_or(); }
                if (const Json* v = (*a)[0].get("version")) version = v->get("openjdk_version") ? v->get("openjdk_version")->string_or() : "";
            }
            if (url.empty()) throw std::runtime_error("No JDK download for this system.");
        } else if (type == "url") {
            url = src.get("url")->string_or(); fname = url.substr(url.rfind('/') + 1);
            tool_job_step(id, "Reading the checksum", 0.02);
            const std::string s = tools_get_text(src.get("sha256_url")->string_or());
            want_sha = s.substr(0, s.find_first_of(" \t\r\n")); version = "latest";
        } else if (type == "npm") {
            std::vector<std::string> cmd; const std::string node_dir = tools_find_npm_cli(cmd);
            if (cmd.empty()) {
                const std::string npm = tools_which("npm");
                if (npm.empty()) throw std::runtime_error("Needs Node.js: install Node.js (LTS) first.");
#ifdef _WIN32
                cmd = {"cmd.exe", "/d", "/c", npm};
#else
                cmd = {npm};
#endif
            }
            tool_job_step(id, "Installing with npm", 0.2);
            fs::create_directories(stage, ec);
            for (const char* a : {"install", "--no-audit", "--no-fund", "--loglevel=error", "--prefix"}) cmd.push_back(a);
            cmd.push_back(stage.string());
            for (const auto& p : json_strings(src.get("packages"))) cmd.push_back(p + "@latest");
            std::vector<std::string> env;
            if (!node_dir.empty()) {
#ifdef _WIN32
                env.push_back("PATH=" + node_dir + ";" + winp::getenv_s8("PATH"));
#else
                env.push_back("PATH=" + node_dir + ":" + (std::getenv("PATH") ? std::getenv("PATH") : ""));
#endif
            }
            const Captured r = run_capture(cmd, stage, env, 600, 400'000);
            if (r.code != 0) throw std::runtime_error("npm failed: " + (r.err.empty() ? r.out : r.err).substr(0, 600));
            const auto pk = json_strings(src.get("packages"));
            if (!pk.empty()) if (auto t = read_small(stage / "node_modules" / pk[0] / "package.json", 2'000'000)) { try { const Json j = JsonParser(*t).parse(); version = j.get("version") ? j.get("version")->string_or() : ""; } catch (...) {} }
            verified = "npm";
        } else if (type == "goinstall") {
            const std::string go = tools_which("go");
            if (go.empty()) throw std::runtime_error("Needs Go: install Go first.");
            tool_job_step(id, "Building with go install", 0.2);
            fs::create_directories(stage / "bin", ec);
            const std::vector<std::string> env = {"GOBIN=" + (stage / "bin").string(), "GOPATH=" + (tools_root() / ".gopath").string(), "GOFLAGS=-modcacherw", "CGO_ENABLED=0"};
            const Captured r = run_capture({go, "install", src.get("package")->string_or()}, stage, env, 900, 400'000);
            if (r.code != 0) throw std::runtime_error("go install failed: " + (r.err.empty() ? r.out : r.err).substr(0, 600));
            version = "latest"; verified = "go";
        } else if (type == "pinned") {
            const Json* f = tools_plat_entry(src.get("files"), plat);
            if (!f) throw std::runtime_error("Not available for this system.");
            url = f->get("url")->string_or(); want_sha = f->get("sha256")->string_or(); fname = url.substr(url.rfind('/') + 1);
            version = src.get("version") ? src.get("version")->string_or() : "pinned";
        } else if (type == "files") {
            version = src.get("version") ? src.get("version")->string_or() : "1";
        } else if (type == "system") {
            throw std::runtime_error("Install this one with its official installer (see the instructions).");
        } else throw std::runtime_error("Unknown source type.");

        if (!url.empty()) {
            tool_job_step(id, "Downloading " + fname, 0.05);
            source_url = url;
            const std::string got = tools_download(id, url, dl);
            std::string want = want_sha; std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (want.empty()) verified = "https-only";
            else if (got != want) { fs::remove(dl, ec); throw std::runtime_error("Checksum mismatch: the download was not what the publisher listed. Nothing was installed."); }
            tool_job_step(id, "Unpacking", 0.82);
            const std::string low = [&] { std::string s = fname; std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; }();
            if (low.size() > 4 && low.compare(low.size() - 4, 4, ".zip") == 0) tools_extract_zip(dl, stage);
            else if ((low.size() > 7 && low.compare(low.size() - 7, 7, ".tar.gz") == 0) || (low.size() > 4 && low.compare(low.size() - 4, 4, ".tgz") == 0)) tools_extract_targz(dl, stage);
            else if (low.size() > 3 && low.compare(low.size() - 3, 3, ".gz") == 0) {
                const auto bins = json_strings(spec->get("bins")); tools_extract_gz_single(dl, stage / (bins.empty() ? id : bins[0]));
            } else throw std::runtime_error("Unsupported archive: " + fname);
            fs::remove(dl, ec);
        }
        // Extra files (voice models): each one checked against its pinned SHA-256 before it is kept.
        if (const auto* ex = spec->get("extras") ? spec->get("extras")->array_ptr() : nullptr) {
            fs::create_directories(stage, ec);
            std::size_t k = 0;
            for (const auto& e : *ex) {
                const std::string eu = e.get("url")->string_or(), es = e.get("sha256")->string_or(), ep = e.get("path")->string_or();
                if (ep.empty() || ep[0] == '/' || ep.find("..") != std::string::npos || es.size() != 64) throw std::runtime_error("Bad extra file in the catalog.");
                tool_job_step(id, "Downloading " + fs::path(ep).filename().string(), 0.05 + 0.75 * static_cast<double>(k) / static_cast<double>(ex->size()));
                const fs::path dest = stage / ep, part = stage / (ep + ".part");
                fs::create_directories(dest.parent_path(), ec);
                const std::string got = tools_download(id, eu, part);
                if (got != es) { fs::remove(part, ec); throw std::runtime_error("Checksum mismatch for " + fs::path(ep).filename().string() + ". Nothing was installed."); }
                fs::rename(part, dest, ec);
                if (ec) throw std::runtime_error("Could not save " + ep + ": " + ec.message());
                if (source_url.empty()) source_url = eu;
                ++k;
            }
        }
        // Folders that hold the executables
        std::vector<std::string> bindirs;
        const auto bins = json_strings(spec->get("bins"));
        std::vector<std::string> pats = json_strings(spec->get("bindirs"));
        const std::string type2 = src.get("type")->string_or();
        if (type2 == "npm") pats = {"node_modules/.bin"};
        if (type2 == "goinstall") pats = {"bin"};
        tool_job_step(id, "Finishing", 0.95);
        fs::remove_all(base, ec);
        fs::rename(stage, base, ec);
        if (ec) { std::error_code e2; fs::copy(stage, base, fs::copy_options::recursive, e2); fs::remove_all(stage, e2); if (e2) throw std::runtime_error("Could not move the tool into place: " + e2.message()); }
        for (const auto& p : pats) for (const auto& d : tools_glob_dirs(base, p)) if (tools_dir_has_bin(d, bins)) { const std::string s = d.string(); if (std::find(bindirs.begin(), bindirs.end(), s) == bindirs.end()) bindirs.push_back(s); }
        if (!bins.empty() && bindirs.empty() && type2 != "url") throw std::runtime_error("Installed, but its programs were not found in the archive.");
        {
            std::lock_guard<std::mutex> lk(g_tools_mu);
            Json m = tools_manifest_locked(); auto* o = std::get_if<Json::Object>(&m.value);
            Json::Array bd; for (const auto& d : bindirs) bd.push_back(Json(d));
            Json::Object e; e["version"] = Json(version); e["dir"] = Json(base.string()); e["bindirs"] = Json(std::move(bd)); e["verified"] = Json(verified);
            e["source"] = Json(source_url.empty() ? std::string(src.get("type")->string_or()) : source_url); e["at"] = Json(static_cast<double>(std::time(nullptr)));
            (*o)[id] = Json(std::move(e)); tools_manifest_save_locked(m);
            auto& j = g_tool_jobs[id]; j.state = "done"; j.progress = 1; j.step = "Installed"; j.version = version;
            j.message = verified == "sha256" ? "Checksum verified (SHA-256)." : verified == "npm" ? "Installed with npm (npm checks package integrity)." : verified == "go" ? "Built with go install (checked against the Go checksum database)." : "Downloaded over HTTPS from the official host (no checksum published).";
        }
        tools_refresh_path();
    } catch (const std::exception& e) {
        fs::remove_all(stage, ec); fs::remove(dl, ec);
        fail(e.what());
    }
}

// Version line of a program (first non-empty line of --version), with a small cache.
std::string tools_version_of(const std::string& exe, const std::string& tool_id) {
    static std::mutex mu; static std::map<std::string, std::pair<long long, std::string>> cache;
    const long long now = static_cast<long long>(std::time(nullptr));
    { std::lock_guard<std::mutex> lk(mu); auto it = cache.find(exe); if (it != cache.end() && now - it->second.first < 120) return it->second.second; }
    std::vector<std::string> args = {exe, tool_id == "go" ? "version" : "--version"};
#ifdef _WIN32
    if (exe.size() > 4 && (exe.substr(exe.size() - 4) == ".cmd" || exe.substr(exe.size() - 4) == ".bat")) args.insert(args.begin(), {"cmd.exe", "/d", "/c"});
#endif
    const Captured r = run_capture(args, fs::temp_directory_path(), {}, 6, 20'000);
    std::string out = r.out.empty() ? r.err : r.out, line;
    std::stringstream ss(out); while (std::getline(ss, line)) { while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back(); if (!line.empty()) break; }
    if (line.size() > 120) line = line.substr(0, 120);
    std::lock_guard<std::mutex> lk(mu); cache[exe] = {now, line};
    return line;
}

Json tools_list_json(bool detect) {
    const std::string plat = tools_platform();
    Json m; std::map<std::string, ToolJob> jobs;
    { std::lock_guard<std::mutex> lk(g_tools_mu); m = tools_manifest_locked(); jobs = g_tool_jobs; }
    Json::Array arr;
    for (const auto& t : *tools_catalog().array_ptr()) {
        const std::string id = t.get("id")->string_or();
        Json::Object o;
        for (const char* k : {"id", "name", "group", "kind", "desc"}) if (t.get(k)) o[k] = *t.get(k);
        const Json& src = *t.get("src"); const std::string type = src.get("type")->string_or();
        bool available = type != "system";
        if (type == "github") available = tools_plat_entry(src.get("asset"), plat) != nullptr;
        if (type == "pinned") available = tools_plat_entry(src.get("files"), plat) != nullptr;
        if ((type == "node" || type == "go" || type == "adoptium") && plat == "linux-arm64") available = true;
        o["installable"] = Json(available);
        o["source"] = Json(type == "github" ? "GitHub · " + src.get("repo")->string_or() : type == "node" ? std::string("nodejs.org") : type == "go" ? std::string("go.dev") : type == "adoptium" ? std::string("Adoptium") : type == "url" ? std::string("download.eclipse.org") : type == "npm" ? std::string("npm") : type == "goinstall" ? std::string("go install") : type == "pinned" || type == "files" ? std::string("GitHub / Hugging Face (pinned SHA-256)") : std::string("official installer"));
        if (const Json* sys = t.get("system")) { const Json* s = sys->get(plat); if (!s) s = sys->get("*"); if (s) o["instructions"] = *s; }
        if (const Json* in = m.get(id)) o["installed"] = *in;
        if (auto it = jobs.find(id); it != jobs.end()) {
            Json::Object j; j["state"] = Json(it->second.state); j["step"] = Json(it->second.step); j["message"] = Json(it->second.message);
            j["progress"] = Json(it->second.progress); j["bytes"] = Json(static_cast<double>(it->second.bytes)); j["total"] = Json(static_cast<double>(it->second.total));
            o["job"] = Json(std::move(j));
        }
        if (detect) {
            for (const auto& b : json_strings(t.get("bins"))) {
                const std::string w = tools_which(b); if (w.empty()) continue;
                const Json* in = m.get(id); bool ours = false;
                if (in) for (const auto& d : json_strings(in->get("bindirs"))) if (w.rfind(d, 0) == 0) ours = true;
                Json::Object s; s["path"] = Json(w); s["version"] = Json(tools_version_of(w, id)); s["ours"] = Json(ours);
                o["found"] = Json(std::move(s)); break;
            }
        }
        arr.push_back(Json(std::move(o)));
    }
    Json::Object out; out["tools"] = Json(std::move(arr)); out["platform"] = Json(plat); out["root"] = Json(tools_root().string());
    return Json(std::move(out));
}

void dev_tools(int fd, const std::string& action, const Json& body) {
    const std::string id = get_string(body, "id");
    if (action == "list") { send_json(fd, 200, tools_list_json(body.get("detect") && std::holds_alternative<bool>(body.get("detect")->value) && std::get<bool>(body.get("detect")->value))); return; }
    if (action == "install") {
        if (!tool_spec(id)) { send_json(fd, 404, error_json("Unknown tool.")); return; }
        {
            std::lock_guard<std::mutex> lk(g_tools_mu);
            auto it = g_tool_jobs.find(id);
            if (it != g_tool_jobs.end() && it->second.state == "running") { send_json(fd, 200, Json(Json::Object{{"started", Json(false)}, {"running", Json(true)}})); return; }
            ToolJob j; j.state = "running"; j.step = "Starting"; j.started = static_cast<long long>(std::time(nullptr)); g_tool_jobs[id] = j;
        }
        std::thread(tools_install_run, id).detach();
        send_json(fd, 200, Json(Json::Object{{"started", Json(true)}})); return;
    }
    if (action == "cancel") { std::lock_guard<std::mutex> lk(g_tools_mu); auto it = g_tool_jobs.find(id); if (it != g_tool_jobs.end() && it->second.state == "running") it->second.state = "cancel"; send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}})); return; }
    if (action == "remove") {
        std::error_code ec;
        {
            std::lock_guard<std::mutex> lk(g_tools_mu);
            Json m = tools_manifest_locked(); auto* o = std::get_if<Json::Object>(&m.value);
            if (o && o->count(id)) { const std::string dir = (*o)[id].get("dir") ? (*o)[id].get("dir")->string_or() : ""; const fs::path d = tpath(dir);
                if (!dir.empty() && d.parent_path() == tools_root()) fs::remove_all(d, ec);
                o->erase(id); tools_manifest_save_locked(m); }
            g_tool_jobs.erase(id);
        }
        tools_refresh_path();
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}})); return;
    }
    send_json(fd, 404, error_json("Unknown tools action."));
}

// ------------------------------------------------------------------ language servers
struct LspConn {
    std::string id, project, lang; McpConn proc; fs::path root;
    std::mutex mu; std::condition_variable cv; std::deque<std::pair<long long, std::string>> inbox; long long seq = 0;
    std::atomic<bool> alive{false}; std::thread reader; std::string exit_reason; long long last_used = 0;
};
std::mutex g_lsp_mu;
std::map<std::string, std::shared_ptr<LspConn>> g_lsp;

std::string file_uri(const fs::path& p) {
    std::string s = fs::absolute(p).generic_string(), out = "file://";
#ifdef _WIN32
    out += "/";
#endif
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) { if (std::isalnum(c) || std::strchr("/-_.~:", c)) out += static_cast<char>(c); else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; } }
    return out;
}
bool lsp_write(LspConn& c, const std::string& body) {
    const std::string data = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    std::lock_guard<std::mutex> lk(c.proc.mu);
#ifdef _WIN32
    DWORD w = 0; return c.proc.in_w && WriteFile(c.proc.in_w, data.data(), static_cast<DWORD>(data.size()), &w, nullptr) && w == data.size();
#else
    std::size_t off = 0;
    while (off < data.size()) { const ssize_t n = write(c.proc.in_w, data.data() + off, data.size() - off); if (n <= 0) { if (errno == EINTR) continue; if (errno == EAGAIN) { usleep(2000); continue; } return false; } off += static_cast<std::size_t>(n); }
    return true;
#endif
}
void lsp_reader(std::shared_ptr<LspConn> c) {
    std::string buf; char tmp[65536];
    while (c->alive) {
#ifdef _WIN32
        DWORD avail = 0;
        if (!PeekNamedPipe(c->proc.out_r, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail == 0) { Sleep(10); continue; }
        DWORD got = 0; if (!ReadFile(c->proc.out_r, tmp, std::min<DWORD>(avail, sizeof(tmp)), &got, nullptr) || got == 0) break;
        buf.append(tmp, got);
#else
        pollfd p{c->proc.out_r, POLLIN, 0}; const int r = poll(&p, 1, 100);
        if (r < 0 && errno != EINTR) break;
        if (r <= 0) continue;
        const ssize_t got = read(c->proc.out_r, tmp, sizeof(tmp));
        if (got == 0) break;
        if (got < 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
        buf.append(tmp, static_cast<std::size_t>(got));
#endif
        for (;;) {
            const auto he = buf.find("\r\n\r\n"); if (he == std::string::npos) break;
            std::size_t len = 0; bool has = false;
            std::string hdr = buf.substr(0, he); std::transform(hdr.begin(), hdr.end(), hdr.begin(), [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
            if (auto cl = hdr.find("content-length:"); cl != std::string::npos) { len = static_cast<std::size_t>(std::strtoull(hdr.c_str() + cl + 15, nullptr, 10)); has = true; }
            if (!has) { buf.erase(0, he + 4); continue; }
            if (buf.size() < he + 4 + len) break;
            std::string body = buf.substr(he + 4, len); buf.erase(0, he + 4 + len);
            std::lock_guard<std::mutex> lk(c->mu);
            c->inbox.emplace_back(++c->seq, std::move(body));
            while (c->inbox.size() > 4000) c->inbox.pop_front();
            c->cv.notify_all();
        }
        if (buf.size() > 64'000'000) break;
    }
    c->alive = false;
    std::lock_guard<std::mutex> lk(c->mu); if (c->exit_reason.empty()) c->exit_reason = "The language server stopped."; c->cv.notify_all();
}
void lsp_stop(const std::shared_ptr<LspConn>& c) {
    c->alive = false;
    { std::lock_guard<std::mutex> lk(c->proc.mu); mcp_kill(c->proc); }
    if (c->reader.joinable()) c->reader.join();
}
fs::path vscode_ext_root();
// Command line for a language's server, or "" when it is not installed. java (jdtls) is assembled from its folder.
std::vector<std::string> lsp_command(const LspLang& L, const std::string& project, std::string& why) {
    if (std::string(L.lang) == "exthost") {   /* VS Code extension host (preview): node web/exthost-node.js */
        std::string node = tools_installed_bin("node", "node"); if (node.empty()) node = tools_which("node");
        if (node.empty()) { why = "The extension host needs Node.js: install Node.js (LTS) in Extensions › Tools."; return {}; }
        const std::string v = tools_version_of(node, "node"); const auto d = v.find_first_of("0123456789");
        if (d != std::string::npos && std::atoi(v.c_str() + d) < 18) { why = "The extension host needs Node.js 18 or newer (found " + v + "). Install Node.js (LTS) in Extensions › Tools."; return {}; }
        std::error_code ec; const fs::path host = fs::absolute("web/exthost-node.js", ec);
        if (!fs::exists(host, ec)) { why = "web/exthost-node.js is missing from this install."; return {}; }
        return {node, host.string(), "--extensions", vscode_ext_root().string(), "--storage", (fs::path(data_dir()) / "ext" / "vscode-storage").string()};
    }
    if (std::string(L.lang) == "java") {
        const std::string java = tools_which("java"); if (java.empty()) { why = "Needs a JDK 21+ (install Java 21 JDK)."; return {}; }
        fs::path jd; { std::lock_guard<std::mutex> lk(g_tools_mu); const Json m = tools_manifest_locked(); if (const Json* j = m.get("jdtls")) jd = tpath(j->get("dir")->string_or()); }
        if (jd.empty()) { why = "Install the Eclipse JDT language server."; return {}; }
        std::string launcher; std::error_code ec;
        for (fs::directory_iterator it(jd / "plugins", ec), e; !ec && it != e; it.increment(ec)) { const std::string n = it->path().filename().string(); if (n.rfind("org.eclipse.equinox.launcher_", 0) == 0 && n.size() > 4 && n.substr(n.size() - 4) == ".jar") launcher = it->path().string(); }
        if (launcher.empty()) { why = "The JDT language server folder looks incomplete."; return {}; }
        const std::string cfg = tools_platform() == "win-x64" ? "config_win" : tools_platform().rfind("mac", 0) == 0 ? "config_mac" : "config_linux";
        const fs::path ws = tools_root() / ".jdtls-data" / project; fs::create_directories(ws, ec);
        return {java, "-Declipse.application=org.eclipse.jdt.ls.core.id1", "-Dosgi.bundles.defaultStartLevel=4", "-Declipse.product=org.eclipse.jdt.ls.core.product",
                "-Xmx1G", "--add-modules=ALL-SYSTEM", "--add-opens", "java.base/java.util=ALL-UNNAMED", "--add-opens", "java.base/java.lang=ALL-UNNAMED",
                "-jar", launcher, "-configuration", (jd / cfg).string(), "-data", ws.string()};
    }
    std::string bin = tools_installed_bin(L.tool, L.bin);
    if (bin.empty()) {
        bin = tools_which(L.bin);
        if (!bin.empty() && std::string(L.bin) == "tsc") {   /* only TypeScript 7+ has the built-in server */
            const std::string v = tools_version_of(bin, "typescript"); const auto sp = v.find_first_of("0123456789");
            if (sp == std::string::npos || std::atoi(v.c_str() + sp) < 7) bin.clear();
        }
    }
    if (bin.empty()) { why = std::string("Install ") + (tool_spec(L.tool) ? tool_spec(L.tool)->get("name")->string_or() : L.tool) + " in Extensions › Tools."; return {}; }
    std::vector<std::string> cmd = {bin}; cmd.insert(cmd.end(), L.args.begin(), L.args.end());
    return cmd;
}
const LspLang* lsp_lang(const std::string& lang) {
    static const LspLang exthost = {"exthost", "", "node", "node", {}, "exthost"};
    if (lang == "exthost") return &exthost;
    for (const auto& L : lsp_langs()) if (lang == L.lang) return &L;
    return nullptr;
}
fs::path vscode_ext_root() { std::error_code ec; const fs::path p = fs::path(data_dir()) / "ext" / "vscode"; fs::create_directories(p, ec); return p; }

void dev_lsp(int fd, const std::string& action, const Json& body) {
    const std::string project = get_string(body, "project");
    if (action == "status") {
        Json::Array arr;
        for (const auto& L : lsp_langs()) {
            std::string why; const auto cmd = lsp_command(L, project.empty() ? "_" : project, why);
            Json::Object o; o["lang"] = Json(L.lang); o["exts"] = Json(L.exts); o["tool"] = Json(L.tool); o["languageId"] = Json(L.lang_id);
            o["available"] = Json(!cmd.empty()); if (!cmd.empty()) o["command"] = Json(cmd[0]); else o["why"] = Json(why);
            arr.push_back(Json(std::move(o)));
        }
        Json::Array running;
        { std::lock_guard<std::mutex> lk(g_lsp_mu); for (const auto& [k, c] : g_lsp) if (c->alive) running.push_back(Json(Json::Object{{"id", Json(c->id)}, {"project", Json(c->project)}, {"lang", Json(c->lang)}})); }
        send_json(fd, 200, Json(Json::Object{{"servers", Json(std::move(arr))}, {"running", Json(std::move(running))}})); return;
    }
    if (action == "start") {
        const std::string lang = get_string(body, "lang");
        if (!valid_project_name(project)) { send_json(fd, 400, error_json("Invalid project.")); return; }
        const LspLang* L = lsp_lang(lang); if (!L) { send_json(fd, 404, error_json("No language server for " + lang)); return; }
        const fs::path root = fs::path(projects_root()) / project; std::error_code ec;
        if (!fs::is_directory(root, ec)) { send_json(fd, 404, error_json("Project not found.")); return; }
        const std::string key = project + "|" + lang;
        const bool fresh = body.get("restart") && std::holds_alternative<bool>(body.get("restart")->value) && std::get<bool>(body.get("restart")->value);
        std::shared_ptr<LspConn> c;
        {
            std::lock_guard<std::mutex> lk(g_lsp_mu);
            auto it = g_lsp.find(key);
            if (it != g_lsp.end() && it->second->alive && !fresh) c = it->second;
            else if (it != g_lsp.end()) { lsp_stop(it->second); g_lsp.erase(it); }
        }
        bool reused = true;
        if (!c) {
            std::string why; const auto cmd = lsp_command(*L, project, why);
            if (cmd.empty()) { send_json(fd, 409, error_json(why)); return; }
            c = std::make_shared<LspConn>(); c->id = key; c->project = project; c->lang = lang; c->root = fs::weakly_canonical(root, ec);
            c->proc.key = "lsp-" + project + "-" + lang; c->proc.command = cmd[0]; c->proc.args.assign(cmd.begin() + 1, cmd.end()); c->proc.cwd = c->root; c->proc.name = lang;
            if (!mcp_spawn(c->proc)) { send_json(fd, 500, error_json(c->proc.error.empty() ? "Could not start the language server." : c->proc.error)); return; }
            c->alive = true; c->reader = std::thread(lsp_reader, c);
            std::shared_ptr<LspConn> old;   /* a concurrent start for the same key: keep ours, stop theirs (never leak a process) */
            { std::lock_guard<std::mutex> lk(g_lsp_mu); auto it = g_lsp.find(key); if (it != g_lsp.end() && it->second != c) old = it->second; g_lsp[key] = c; }
            if (old) lsp_stop(old);
            reused = false;
        }
        c->last_used = static_cast<long long>(std::time(nullptr));
        long long seq; { std::lock_guard<std::mutex> lk(c->mu); seq = c->seq; }
        Json::Object o; o["id"] = Json(c->id); o["rootUri"] = Json(file_uri(c->root)); o["rootPath"] = Json(c->root.string()); o["reused"] = Json(reused); o["seq"] = Json(static_cast<double>(seq));
        send_json(fd, 200, Json(std::move(o))); return;
    }
    std::shared_ptr<LspConn> c;
    { std::lock_guard<std::mutex> lk(g_lsp_mu); auto it = g_lsp.find(get_string(body, "id")); if (it != g_lsp.end()) c = it->second; }
    if (!c) { send_json(fd, 404, error_json("Language server not running.")); return; }
    c->last_used = static_cast<long long>(std::time(nullptr));
    if (action == "send") {
        const Json* msgs = body.get("messages"); int n = 0;
        if (msgs && msgs->array_ptr()) for (const auto& m : *msgs->array_ptr()) { if (!c->alive || !lsp_write(*c, dump_json(m))) { send_json(fd, 410, error_json("The language server stopped.")); return; } ++n; }
        send_json(fd, 200, Json(Json::Object{{"sent", Json(n)}})); return;
    }
    if (action == "recv") {
        long long after = 0; if (const Json* a = body.get("after")) if (std::holds_alternative<double>(a->value)) after = static_cast<long long>(std::get<double>(a->value));
        int wait = 20000; if (const Json* w = body.get("wait")) if (std::holds_alternative<double>(w->value)) wait = std::clamp(static_cast<int>(std::get<double>(w->value)), 0, 25000);
        std::unique_lock<std::mutex> lk(c->mu);
        c->cv.wait_for(lk, std::chrono::milliseconds(wait), [&] { return c->seq > after || !c->alive; });
        std::string out = "{\"messages\":[";
        bool first = true; long long last = after;
        for (const auto& [s, m] : c->inbox) if (s > after) { if (!first) out += ','; out += m; first = false; last = s; }
        out += "],\"seq\":" + std::to_string(last) + ",\"alive\":" + (c->alive ? "true" : "false");
        if (!c->alive) out += ",\"error\":" + dump_json(Json(c->exit_reason));
        out += "}";
        lk.unlock();
        send_response(fd, 200, "application/json; charset=utf-8", out); return;
    }
    if (action == "stop") {   /* only remove our own entry: a restart may already have put a new server under the same id */
        lsp_stop(c);
        { std::lock_guard<std::mutex> lk(g_lsp_mu); auto it = g_lsp.find(c->id); if (it != g_lsp.end() && it->second == c) g_lsp.erase(it); }
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}})); return;
    }
    send_json(fd, 404, error_json("Unknown lsp action."));
}
// Stops a project's language servers (the project was closed, deleted or renamed).
void lsp_forget_project(const std::string& project) {
    std::vector<std::shared_ptr<LspConn>> all;
    { std::lock_guard<std::mutex> lk(g_lsp_mu); for (auto it = g_lsp.begin(); it != g_lsp.end();) { if (it->second->project == project) { all.push_back(it->second); it = g_lsp.erase(it); } else ++it; } }
    for (auto& c : all) lsp_stop(c);
}


// ------------------------------------------------------------------ VS Code extensions (for the extension host)
Json vscode_manifest_locked() {
    const auto text = read_small(vscode_ext_root() / "installed.json", 4'000'000);
    if (text) { try { Json j = JsonParser(*text).parse(); if (j.is_object()) return j; } catch (...) {} }
    return Json(Json::Object{});
}
void vscode_manifest_save_locked(const Json& m) {
    const fs::path p = vscode_ext_root() / "installed.json", tmp = p.string() + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << dump_json(m); }
    std::error_code ec; fs::rename(tmp, p, ec);
}
bool vsx_name_ok(const std::string& s) { static const std::regex re(R"(^[A-Za-z0-9][A-Za-z0-9._-]{0,80}$)"); return std::regex_match(s, re); }
void dev_exthost(int fd, const std::string& action, const Json& body) {
    if (action == "list") {
        Json m; { std::lock_guard<std::mutex> lk(g_tools_mu); m = vscode_manifest_locked(); }
        std::string why; const auto cmd = lsp_command(*lsp_lang("exthost"), "_", why);
        Json::Object o; o["extensions"] = m; o["ready"] = Json(!cmd.empty()); if (cmd.empty()) o["why"] = Json(why); else o["node"] = Json(cmd[0]);
        send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action == "install") {
        const std::string nsp = get_string(body, "namespace"), name = get_string(body, "name");
        if (!vsx_name_ok(nsp) || !vsx_name_ok(name)) { send_json(fd, 400, error_json("Invalid extension id.")); return; }
        const Json meta = JsonParser(tools_get_text("https://open-vsx.org/api/" + nsp + "/" + name)).parse();
        const Json* files = meta.get("files");
        const std::string url = files && files->get("download") ? files->get("download")->string_or() : "";
        const std::string sha_url = files && files->get("sha256") ? files->get("sha256")->string_or() : "";
        const std::string version = meta.get("version") ? meta.get("version")->string_or() : "";
        if (url.empty()) { send_json(fd, 404, error_json("Open VSX has no download for this extension.")); return; }
        const std::string id = nsp + "." + name; const std::string key = "vsx-" + id;
        const fs::path stage = tools_root() / (".stage-" + key), dl = tools_root() / (".dl-" + key), dest = vscode_ext_root() / id;
        std::error_code ec; fs::remove_all(stage, ec); fs::create_directories(stage, ec);
        try {
            { std::lock_guard<std::mutex> lk(g_tools_mu); g_tool_jobs[key] = ToolJob{}; }
            const std::string got = tools_download(key, url, dl);
            std::string want; if (!sha_url.empty()) { const std::string s = tools_get_text(sha_url); want = s.substr(0, s.find_first_of(" \t\r\n")); }
            std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!want.empty() && want != got) throw std::runtime_error("Checksum mismatch: the download was not what Open VSX lists. Nothing was installed.");
            tools_extract_zip(dl, stage); fs::remove(dl, ec);
            const fs::path ext = stage / "extension";
            const auto pkgt = read_small(ext / "package.json", 4'000'000);
            if (!pkgt) throw std::runtime_error("This file is not a VS Code extension (no extension/package.json).");
            const Json pkg = JsonParser(*pkgt).parse();
            const std::string main = pkg.get("main") ? pkg.get("main")->string_or() : "";
            if (main.empty()) throw std::runtime_error(pkg.get("browser") ? "This is a web-only extension; the extension host runs desktop (Node) extensions." : "This extension has no code to run. Themes and snippets install from the Themes and Snippets tabs.");
            fs::remove_all(dest, ec); fs::rename(ext, dest, ec);
            if (ec) { std::error_code e2; fs::copy(ext, dest, fs::copy_options::recursive, e2); if (e2) throw std::runtime_error("Could not move the extension into place: " + e2.message()); }
            fs::remove_all(stage, ec);
            Json::Object e; e["id"] = Json(id); e["version"] = Json(version); e["dir"] = Json(dest.string()); e["enabled"] = Json(true); e["main"] = Json(main);
            e["displayName"] = Json(meta.get("displayName") ? meta.get("displayName")->string_or(name) : name); e["description"] = Json(meta.get("description") ? meta.get("description")->string_or() : "");
            e["verified"] = Json(want.empty() ? "https-only" : "sha256"); e["at"] = Json(static_cast<double>(std::time(nullptr)));
            if (const Json* en = pkg.get("engines")) if (const Json* v = en->get("vscode")) e["engine"] = *v;
            Json out;
            { std::lock_guard<std::mutex> lk(g_tools_mu); Json m = vscode_manifest_locked(); std::get<Json::Object>(m.value)[id] = Json(e); vscode_manifest_save_locked(m); g_tool_jobs.erase(key); out = Json(e); }
            send_json(fd, 200, out); return;
        } catch (const std::exception& ex) {
            fs::remove_all(stage, ec); fs::remove(dl, ec); { std::lock_guard<std::mutex> lk(g_tools_mu); g_tool_jobs.erase(key); }
            send_json(fd, 422, error_json(ex.what())); return;
        }
    }
    const std::string id = get_string(body, "id");
    if (action == "remove" || action == "toggle") {
        std::lock_guard<std::mutex> lk(g_tools_mu);
        Json m = vscode_manifest_locked(); auto& o = std::get<Json::Object>(m.value);
        auto it = o.find(id); if (it == o.end()) { send_json(fd, 404, error_json("Not installed.")); return; }
        if (action == "remove") { std::error_code ec; const fs::path d = tpath(it->second.get("dir") ? it->second.get("dir")->string_or() : ""); if (d.parent_path() == vscode_ext_root()) fs::remove_all(d, ec); o.erase(it); }
        else { auto& e = std::get<Json::Object>(it->second.value); e["enabled"] = Json(body.get("on") && std::holds_alternative<bool>(body.get("on")->value) && std::get<bool>(body.get("on")->value)); }
        vscode_manifest_save_locked(m);
        send_json(fd, 200, Json(Json::Object{{"ok", Json(true)}})); return;
    }
    send_json(fd, 404, error_json("Unknown exthost action."));
}

// ------------------------------------------------------------------ natural voice (Piper, offline neural TTS)
// POST /api/dev/tts/status {} -> {available, voices:[{lang, name}]}
// POST /api/dev/tts/speak {text, lang, rate?} -> audio/wav (generated on this computer; nothing is sent anywhere)
struct PiperVoice { std::string lang, name; fs::path model; };
std::string piper_bin() {
    Json m; { std::lock_guard<std::mutex> lk(g_tools_mu); m = tools_manifest_locked(); }
    std::error_code ec;
    if (const Json* e = m.get("piper"))
        for (const auto& d : json_strings(e->get("bindirs")))
            for (const char* n : {"piper", "piper.exe"}) { const fs::path p = fs::path(d) / n; if (fs::is_regular_file(p, ec)) return p.string(); }
    return tools_which("piper");
}
std::vector<PiperVoice> piper_voices() {
    std::vector<PiperVoice> out; std::error_code ec;
    for (const auto& t : fs::directory_iterator(tools_root(), ec)) {
        const fs::path vd = t.path() / "voices";
        if (!fs::is_directory(vd, ec)) continue;
        for (const auto& f : fs::directory_iterator(vd, ec)) {
            const std::string n = f.path().filename().string();
            if (n.size() < 6 || n.compare(n.size() - 5, 5, ".onnx") != 0) continue;
            if (!fs::exists(fs::path(f.path().string() + ".json"), ec)) continue;
            PiperVoice v; v.model = f.path(); v.lang = n.substr(0, n.find('-')); v.name = n.substr(0, n.size() - 5);
            out.push_back(std::move(v));
        }
    }
    std::sort(out.begin(), out.end(), [](const PiperVoice& a, const PiperVoice& b) { return a.name < b.name; });
    return out;
}
void dev_tts(int fd, const std::string& action, const Json& body) {
    const std::string bin = piper_bin();
    const auto voices = piper_voices();
    if (action == "status") {
        Json::Array vs; for (const auto& v : voices) { Json::Object o; o["lang"] = Json(v.lang); o["name"] = Json(v.name); vs.push_back(Json(std::move(o))); }
        Json::Object o; o["available"] = Json(!bin.empty() && !voices.empty()); o["installed"] = Json(!bin.empty()); o["voices"] = Json(std::move(vs));
        send_json(fd, 200, Json(std::move(o))); return;
    }
    if (action != "speak") { send_json(fd, 404, error_json("Unknown voice action.")); return; }
    if (bin.empty()) { send_json(fd, 404, error_json("The natural voice is not installed (Settings > Voice).")); return; }
    std::string text = get_string(body, "text");
    if (trim(text).empty()) { send_json(fd, 400, error_json("Nothing to say.")); return; }
    if (text.size() > 3000) text = text.substr(0, 3000);
    for (char& c : text) if (c == '\r' || c == '\n') c = ' ';
    std::string lang = get_string(body, "lang", "en"); for (char& c : lang) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string want = lang.substr(0, 2), voice_name = get_string(body, "voice");
    const PiperVoice* pick = nullptr;
    for (const auto& v : voices) if (!voice_name.empty() && v.name == voice_name) pick = &v;
    if (!pick) for (const auto& v : voices) if (v.lang.size() >= 2 && std::tolower(static_cast<unsigned char>(v.lang[0])) == want[0] && std::tolower(static_cast<unsigned char>(v.lang[1])) == (want.size() > 1 ? want[1] : ' ')) { pick = &v; break; }
    if (!pick) { send_json(fd, 404, error_json("No natural voice for this language.")); return; }
    double rate = 1.0; if (const Json* r = body.get("rate")) if (const auto* d = std::get_if<double>(&r->value)) rate = std::clamp(*d, 0.5, 2.0);
    std::error_code ec;
    const fs::path dir = fs::path(data_dir()) / ".astro_tts"; fs::create_directories(dir, ec);
    const fs::path out = dir / (random_hex(8) + ".wav");
    char ls[32]; std::snprintf(ls, sizeof ls, "%.3f", 1.0 / rate);
    const std::string input = text + "\n";
    const Captured r = run_capture({bin, "-m", pick->model.string(), "-f", out.string(), "--length_scale", ls}, fs::path(bin).parent_path(), {}, 90, 200'000, &input);
    std::string wav;
    { std::ifstream in(out, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); wav = ss.str(); }
    fs::remove(out, ec);
    if (r.code != 0 || wav.size() < 100) { send_json(fd, 500, error_json("The natural voice failed: " + trim((r.err.empty() ? r.out : r.err).substr(0, 300)))); return; }
    send_response(fd, 200, "audio/wav", wav);
}
