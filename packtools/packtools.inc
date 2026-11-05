// packtools.inc - A.S.T.R.O AI's built-in pack tools: the helper programs of the Astro Packs, compiled into the
// engine so a Library pack ships only text (SKILL.md files and data), never something to execute.
//   astro-server tool <skill>/<helper> [args...]      e.g.  astro-server tool trader-screener/screen metrics.json
// In a skill, ${ASTRO_TOOL} expands to the quoted engine path plus "tool". Each helper reproduces the output of the
// original (Jake Schincariol's MIT-licensed skills) and is registered with PACKTOOL(name, function).
#if defined(_WIN32)   // the same Windows setup as platform.h (this file is its own translation unit)
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#endif
#include "packtools_api.h"
#include "pyrt.h"
#include "common.h"
#include "xml.h"
#include "zip.h"
#include "unicode.h"
#include "pyre.h"
#include "htmlparser.h"
#include "urlparse.h"
#include <map>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#endif

// Groups: the engine compiles the tools in several translation units (packtools.cpp = group 1, packtools_<group>.cpp),
// which keeps the compiler's memory in bounds on small machines; the linker puts them back together. Without
// PACKTOOLS_GROUP (the test harness, a quick single-file build) every group is compiled at once.
//   1 = the dispatcher + trader, day-trader, hunter    2 = master-skills virality/build    3 = master-skills edit
//   4 = console    5 = social and founder packs (duel, founder, replica)    6 = arena
//   7 = closer (sales)    8 = seed (startup fundraising)    9 = design (UI/UX Pro Max search)
//   10 = modder (universal-modder's um CLI: scan, kb, backup, publish, sprites, video, fal, CoD formats)
#ifndef PACKTOOLS_GROUP
#define PACKTOOLS_GROUP 0
#endif
#define PACKTOOLS_IN(g) (PACKTOOLS_GROUP == 0 || PACKTOOLS_GROUP == (g))

namespace packtools {
using Fn = int (*)(const std::vector<std::string>& argv);
inline std::map<std::string, Fn>& registry() { static std::map<std::string, Fn> r; return r; }
struct Reg { Reg(const char* name, Fn f) { registry()[name] = f; } };
#define PACKTOOL_CAT2(a, b) a##b
#define PACKTOOL_CAT(a, b) PACKTOOL_CAT2(a, b)
#define PACKTOOL(name, fn) static packtools::Reg PACKTOOL_CAT(packtool_reg_, __LINE__)(name, fn)
// The skill's folder (for the data files a helper ships with): --skill-dir before the tool name, else $ASTRO_SKILL_DIR.
inline std::string& skill_dir_ref() { static std::string d; return d; }
inline std::string skill_file(const std::string& name) { return skill_dir_ref().empty() ? name : pyrt::path_join(skill_dir_ref(), name); }
// Network: HttpResp and the hook are declared in packtools_api.h (the engine sets the hook)
#if PACKTOOLS_IN(1)   /* defined once: the other groups see the declarations in packtools_api.h */
HttpFn& http_hook() { static HttpFn f = nullptr; return f; }
HttpExFn& http_ex_hook() { static HttpExFn f = nullptr; return f; }
ExecFn& exec_hook() { static ExecFn f = nullptr; return f; }
#endif
// ASTRO_TOOL_OFFLINE=1 turns the network off for pack tools (the test suites set it, so they never call live services)
inline bool net_off(HttpResp& r) { const char* e = std::getenv("ASTRO_TOOL_OFFLINE"); if (!e || std::string(e) != "1") return false; r.error = "network is off (ASTRO_TOOL_OFFLINE=1)"; return true; }
inline HttpResp http_get(const std::string& url, const std::vector<std::string>& headers = {}, long timeout_s = 30) {
    if (HttpResp r; net_off(r)) return r;
    if (!http_hook()) { HttpResp r; r.error = "this build has no network client for pack tools (run it through A.S.T.R.O AI)"; return r; }
    return http_hook()(url, headers, timeout_s, nullptr);
}
inline HttpResp http_post(const std::string& url, const std::string& body, const std::vector<std::string>& headers = {}, long timeout_s = 30) {
    if (HttpResp r; net_off(r)) return r;
    if (!http_hook()) { HttpResp r; r.error = "this build has no network client for pack tools (run it through A.S.T.R.O AI)"; return r; }
    return http_hook()(url, headers, timeout_s, &body);
}
// run a program and capture its stdout (subprocess.run(args, capture_output=True, timeout=...)): {exit code, stdout}.
// The engine sets exec_hook on Windows (CreateProcess); elsewhere fork/exec with stderr discarded and a timeout.
inline ExecResult run_cmd(const std::vector<std::string>& args, int timeout_s = 60) {
    if (exec_hook()) return exec_hook()(args);
    ExecResult r;
#ifndef _WIN32
    int fds[2]; if (args.empty() || pipe(fds) != 0) return r;
    const pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return r; }
    if (pid == 0) {
        close(fds[0]); dup2(fds[1], 1); close(fds[1]);
        const int devnull = open("/dev/null", O_RDWR); if (devnull >= 0) { dup2(devnull, 2); dup2(devnull, 0); close(devnull); }
        std::vector<char*> argv; for (const auto& x : args) argv.push_back(const_cast<char*>(x.c_str())); argv.push_back(nullptr);
        execvp(argv[0], argv.data()); _exit(127);
    }
    close(fds[1]);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s); bool timed_out = false; char buf[8192];
    for (;;) {
        const long long left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) { timed_out = true; break; }
        pollfd pf{fds[0], POLLIN, 0}; const int pr = poll(&pf, 1, static_cast<int>(std::min<long long>(left, 1000)));
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) continue;
        const ssize_t got = read(fds[0], buf, sizeof buf);
        if (got < 0) { if (errno == EINTR) continue; break; }
        if (got == 0) break;
        r.out.append(buf, static_cast<std::size_t>(got));
    }
    close(fds[0]);
    if (timed_out) kill(pid, SIGKILL);
    int st = 0; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (timed_out) { r.code = -1; r.out.clear(); return r; }   // subprocess.TimeoutExpired
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#else
    (void)timeout_s;
#endif
    return r;
}
}  // namespace packtools
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif

#if PACKTOOLS_IN(1)
#include "trader_screener.inc"
#include "trader_rebalance.inc"
#include "trader_risk.inc"
#include "trader_budget.inc"
#include "trader_broker.inc"
#include "trader_tax.inc"
#include "trader_news.inc"
#include "trader_analyst.inc"
#include "trader_alerts.inc"
#include "trader_pelosi.inc"
#include "trader_copycat.inc"
#include "daytrade_news.inc"
#include "daytrade_risk.inc"
#include "daytrade_broker.inc"
#include "daytrade_alerts.inc"
#include "daytrade_budget.inc"
#include "daytrade_yahoo.inc"
#include "daytrade_exit.inc"
#include "daytrade_entry.inc"
#include "daytrade_scanner.inc"
#include "daytrade_timemachine.inc"
#include "hunter_emails.inc"
#include "hunter_outreach.inc"
#include "hunter_cover.inc"
#include "hunter_linkedin.inc"
#include "hunter_interview.inc"
#include "hunter_apply.inc"
#include "hunter_research.inc"
#include "hunter_salary.inc"
#include "hunter_resume.inc"
#include "hunter_followup.inc"
#include "hunter_tracker.inc"
#include "hunter_scout.inc"
#endif
#if PACKTOOLS_IN(2) || PACKTOOLS_IN(3) || PACKTOOLS_IN(5) || PACKTOOLS_IN(6)   /* master_common: shared by the master groups and groups 5 and 6 */
#include "master_common.inc"
#endif
#if PACKTOOLS_IN(2)
#include "master_title.inc"
#include "master_swipe.inc"
#include "master_speech.inc"
#include "master_hookscore.inc"
#include "master_detect.inc"
#include "master_humanize.inc"
#include "master_collect.inc"
#include "master_contrast.inc"
#include "master_serve_net.inc"
#include "master_serve.inc"
#include "master_sitecheck_net.inc"
#include "master_sitecheck_page.inc"
#include "master_sitecheck.inc"
#include "social_youtube.inc"
#include "social_instagram.inc"
#include "social_x.inc"
#include "social_x2.inc"
#endif
#if PACKTOOLS_IN(3) || PACKTOOLS_IN(10)   /* subprocess layer: shared by the edit tools and the modder tools */
#include "master_mediakit_sys.inc"
#endif
#if PACKTOOLS_IN(3)
#include "master_mediakit.inc"
#include "master_probe.inc"
#include "master_deadspace.inc"
#include "master_captions.inc"
#include "master_stopmotion.inc"
#endif
#if PACKTOOLS_IN(4)
#include "console_common.inc"
#include "console_elf.inc"
#include "console_iso.inc"
#include "console_state.inc"
#include "console_triage.inc"
#include "console_run.inc"
#include "console_abdiff.inc"
#include "console_override.inc"
#include "console_pnach.inc"
#include "console_tune.inc"
#include "console_gspacket.inc"
#include "console_widescreen.inc"
#include "console_tim2.inc"
#include "console_stubs.inc"
#include "console_vag.inc"
#include "console_memcard.inc"
#include "console_datascan.inc"
#include "console_targets.inc"
#include "console_padmap.inc"
#endif
#if PACKTOOLS_IN(5)
#include "social_duel.inc"
#include "social_founder.inc"
#include "social_replica.inc"
#include "social_replica_img.inc"
#endif
#if PACKTOOLS_IN(6)
#include "social_arena.inc"
#endif
#if PACKTOOLS_IN(7)
#include "closer_common.inc"
#include "closer_forecast.inc"
#include "closer_followup.inc"
#include "closer_docs.inc"
#include "closer_prospect.inc"
#include "closer_log.inc"
#include "closer_brief.inc"
#include "closer_coach.inc"
#include "closer_slots.inc"
#include "closer_winback.inc"
#endif
#if PACKTOOLS_IN(9)
#include "design_uipro.inc"
#endif
#if PACKTOOLS_IN(10)
#include "modder_common.inc"
#include "modder_scan.inc"
#include "modder_publish.inc"
#include "modder_kb.inc"
#include "modder_backup.inc"
#include "modder_cod.inc"
#include "modder_img.inc"
#include "modder_sprite.inc"
#include "modder_render3d.inc"
#include "modder_b3d.inc"
#include "modder_upk.inc"
#include "modder_formats.inc"
#include "modder_font.inc"
#include "modder_video_fonts.inc"
#include "modder_video.inc"
#include "modder_fal.inc"
#include "modder_comfy.inc"
#include "modder_sprixen.inc"
#include "modder_voidless.inc"
#include "modder_win_ps1.inc"
#include "modder_win.inc"
#include "modder_um.inc"
#endif
#if PACKTOOLS_IN(8)
#include "bigdec.h"
#include "seed_common.inc"
#include "seed_dataroom.inc"
#include "seed_random.inc"
#include "seed_market.inc"
#include "seed_updates.inc"
#include "seed_coach.inc"
#include "seed_model.inc"
#include "seed_scout.inc"
#include "seed_terms.inc"
#include "seed_deck.inc"
#include "seed_plan.inc"
#include "seed_investors.inc"
#include "seed_closer.inc"
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if PACKTOOLS_IN(1)   /* the dispatcher: once, in packtools.cpp */
namespace packtools {
// Absolute path of this executable (UTF-8).
inline std::string self_exe() {
#ifdef _WIN32
    wchar_t b[32768]; const DWORD n = GetModuleFileNameW(nullptr, b, 32768);
    if (n == 0) return "astro-server.exe";
    const int len = WideCharToMultiByte(CP_UTF8, 0, b, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
    std::string o(static_cast<std::size_t>(len), '\0'); WideCharToMultiByte(CP_UTF8, 0, b, static_cast<int>(n), o.data(), len, nullptr, nullptr); return o;
#elif defined(__APPLE__)
    char b[4096]; uint32_t sz = sizeof b; if (_NSGetExecutablePath(b, &sz) == 0) { char r[4096]; if (realpath(b, r)) return r; return b; } return "astro-server";
#else
    char b[4096]; const ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1); if (n > 0) return std::string(b, static_cast<std::size_t>(n)); return "astro-server";
#endif
}
// What ${ASTRO_TOOL} expands to in a skill: the quoted engine path plus "tool".
std::string tool_command() {
    std::string p = self_exe();
#ifdef _WIN32
    for (auto& c : p) if (c == '\\') c = '/';   // the agent's shell is bash-like; forward slashes work for both
#endif
    std::string q = "\""; for (char c : p) { if (c == '"' || c == '\\' || c == '$' || c == '`') q += '\\'; q += c; } q += "\"";
    return q + " tool";
}
inline std::string list_text() {
    std::string o = "A.S.T.R.O AI pack tools (" + std::to_string(registry().size()) + "):\n";
    for (const auto& [k, v] : registry()) o += "  " + k + "\n";
    return o + "Run: astro-server tool <name> --help\n";
}
#ifdef _WIN32
// GetCommandLineW split with the MSVC rules (2n backslashes + quote -> n backslashes, toggles quoting), as UTF-8.
inline std::vector<std::string> win_args() {
    const wchar_t* c = GetCommandLineW(); std::vector<std::wstring> out; std::wstring cur; bool inq = false, any = false;
    // argv[0]: up to the next space, or quoted, without backslash rules
    if (*c == L'"') { ++c; while (*c && *c != L'"') cur += *c++; if (*c) ++c; } else { while (*c && *c != L' ' && *c != L'\t') cur += *c++; }
    out.push_back(cur); cur.clear();
    for (;;) {
        while (*c == L' ' || *c == L'\t') ++c;
        if (!*c) break;
        any = false; inq = false;
        while (*c && (inq || (*c != L' ' && *c != L'\t'))) {
            std::size_t bs = 0; while (*c == L'\\') { ++bs; ++c; }
            if (*c == L'"') { cur.append(bs / 2, L'\\'); if (bs % 2) { cur += L'"'; ++c; } else { if (inq && c[1] == L'"') { cur += L'"'; c += 2; } else { inq = !inq; ++c; } } any = true; }
            else { cur.append(bs, L'\\'); if (*c) cur += *c++; any = true; }
        }
        if (any) out.push_back(cur); cur.clear();
    }
    std::vector<std::string> r;
    for (const auto& w : out) { const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr); std::string s(static_cast<std::size_t>(len), '\0'); if (len) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), len, nullptr, nullptr); r.push_back(s); }
    return r;
}
#endif
// argv here is everything after "tool".
inline int tool_main(std::vector<std::string> argv) {
    if (argv.empty() || argv[0] == "--list" || argv[0] == "-h" || argv[0] == "--help") { std::fputs(list_text().c_str(), stdout); return argv.empty() ? 2 : 0; }
    if (argv.size() >= 2 && argv[0] == "--skill-dir") { skill_dir_ref() = argv[1]; argv.erase(argv.begin(), argv.begin() + 2); }
    else if (const char* d = std::getenv("ASTRO_SKILL_DIR"); d && *d) skill_dir_ref() = d;
    if (argv.empty()) { std::fputs(list_text().c_str(), stdout); return 2; }
    if (argv[0] == "--path") { std::printf("%s\n", tool_command().c_str()); return 0; }
    const auto it = registry().find(argv[0]);
    if (it == registry().end()) { std::fprintf(stderr, "astro-server tool: unknown tool '%s'. Run 'astro-server tool --list'.\n", argv[0].c_str()); return 2; }
    const std::string tool_name = argv[0];
    argv.erase(argv.begin());
    int code = 0;
    try { code = it->second(argv); }
    catch (const pyrt::Exit& e) { code = e.code; }
    catch (const pyrt::PyErr& e) { pyrt::flush(); std::fprintf(stderr, "%s: %s: %s\n", tool_name.c_str(), e.type.c_str(), e.what()); code = 1; }
    catch (const std::exception& e) { pyrt::flush(); std::fprintf(stderr, "%s: error: %s\n", tool_name.c_str(), e.what()); code = 1; }
    pyrt::flush();
    return code;
}
// Called first thing in main(): returns -1 when this is not a tool run.
int maybe_tool(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "tool") != 0) return -1;
#ifdef _WIN32
    auto a = win_args(); if (a.size() >= 2) a.erase(a.begin(), a.begin() + 2); else a.clear();
    SetConsoleOutputCP(CP_UTF8);
    return tool_main(a);
#else
    std::vector<std::string> a; for (int k = 2; k < argc; ++k) a.emplace_back(argv[k]);
    return tool_main(a);
#endif
}
}  // namespace packtools
#endif
