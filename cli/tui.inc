// cli/tui.inc — the full-screen terminal app (the default interactive mode of `astro`).
// A Claude Code-style conversation (prompt box, streaming transcript, approvals, slash commands)
// with an OpenCode-style side panel (Ctrl+B): session, changes, to-do and parallel agents.
// POSIX: raw termios + poll().  Windows: console input records + VT output.  No extra libraries.
// Included by astro.cpp after the agent API helpers; `astro --classic` keeps the line-by-line REPL.
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#ifndef _WIN32
#include <poll.h>
#include <termios.h>
#endif

namespace tui {

// ===================================================================== styles
struct St { uint32_t fg = 0, bg = 0; uint8_t a = 0; bool operator==(const St& o) const { return fg == o.fg && bg == o.bg && a == o.a; } };
enum : uint8_t { BOLD = 1, DIM = 2, ITAL = 4, UNDER = 8, REV = 16 };
constexpr uint32_t rgb(uint32_t c) { return 0x1000000u | c; }
static uint32_t PEACH = rgb(0xf3a679), PEACH_D = rgb(0x9a6a50), MUTED = rgb(0x8f8a86), FAINT = rgb(0x5f5a56), GREEN = rgb(0x4ade80),
                RED = rgb(0xf87171), AMBER = rgb(0xfbbf24), BLUE = rgb(0x7aa2f7), VIOLET = rgb(0xb4a0f8), SELBG = rgb(0x33241b),
                BORDER = rgb(0x4a4541), CODE = rgb(0xf0b48f), WHITE = rgb(0xfff1e6), CORE_T = rgb(0xffd3b4), CORE_B = rgb(0xe98a55);
static bool g_truecolor = true, g_light_bg = false;
struct Seg { std::string t; St s; };
using Line = std::vector<Seg>;
static Seg sg(std::string t, uint32_t fg = 0, uint8_t a = 0, uint32_t bg = 0) { return Seg{std::move(t), St{fg, bg, a}}; }

static void detect_colors() {
    const std::string ct = env("COLORTERM"), tp = env("TERM_PROGRAM"), term = env("TERM");
    g_truecolor = ct.find("truecolor") != std::string::npos || ct.find("24bit") != std::string::npos || !env("WT_SESSION").empty() ||
                  tp == "iTerm.app" || tp == "vscode" || tp == "WezTerm" || tp == "ghostty" || term.find("kitty") != std::string::npos ||
                  term.find("direct") != std::string::npos;
#ifdef _WIN32
    g_truecolor = true;   // Windows 10+ consoles understand 24-bit colour
#endif
    if (env("ASTRO_COLOR") == "256") g_truecolor = false;
    bool light = false;
    const std::string fgbg = env("COLORFGBG");
    if (const auto p = fgbg.rfind(';'); p != std::string::npos) { const int bg = std::atoi(fgbg.c_str() + p + 1); light = bg == 7 || bg == 15; }
    if (env("ASTRO_THEME") == "light") light = true;
    if (env("ASTRO_THEME") == "dark") light = false;
    g_light_bg = light;
    if (light) {
        PEACH = rgb(0xc8622b); PEACH_D = rgb(0xe3b293); MUTED = rgb(0x78716c); FAINT = rgb(0xa8a29e); GREEN = rgb(0x15803d); RED = rgb(0xdc2626);
        AMBER = rgb(0xb45309); BLUE = rgb(0x2563eb); VIOLET = rgb(0x7c3aed); SELBG = rgb(0xfbe3d3); BORDER = rgb(0xc9c2bc); CODE = rgb(0xa94a17);
        WHITE = rgb(0xc8622b); CORE_T = rgb(0xf6b48c); CORE_B = rgb(0xd0652c);
    }
}
static int to256(uint32_t c) {
    const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    auto q = [](int v) { return v < 48 ? 0 : v < 115 ? 1 : (v - 35) / 40; };
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    const int ri = q(r), gi = q(g), bi = q(b);
    const int avg = (r + g + b) / 3; int gr = avg > 238 ? 23 : (avg - 3) / 10; if (gr < 0) gr = 0;
    const int gv = 8 + gr * 10;
    auto sq = [](int v) { return v * v; };
    const long dc = sq(lv[ri] - r) + sq(lv[gi] - g) + sq(lv[bi] - b), dg = sq(gv - r) + sq(gv - g) + sq(gv - b);
    return dg < dc ? 232 + gr : 16 + 36 * ri + 6 * gi + bi;
}
static std::string sgr(const St& s) {
    std::string o = "\x1b[0";
    if (s.a & BOLD) o += ";1";
    if (s.a & DIM) o += ";2";
    if (s.a & ITAL) o += ";3";
    if (s.a & UNDER) o += ";4";
    if (s.a & REV) o += ";7";
    auto colr = [&](uint32_t c, bool fg) {
        c &= 0xffffff;
        if (g_truecolor) { o += fg ? ";38;2;" : ";48;2;"; o += std::to_string(c >> 16) + ";" + std::to_string((c >> 8) & 255) + ";" + std::to_string(c & 255); }
        else { o += fg ? ";38;5;" : ";48;5;"; o += std::to_string(to256(c)); }
    };
    if (s.fg) colr(s.fg, true);
    if (s.bg) colr(s.bg, false);
    return o + "m";
}
static uint32_t mix(uint32_t a, uint32_t b, double t) {
    auto ch = [&](int sh) { const int x = (a >> sh) & 255, y = (b >> sh) & 255; return static_cast<uint32_t>(x + (y - x) * t) & 255; };
    return 0x1000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// ===================================================================== UTF-8 and widths
static uint32_t dec(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp; int n;
    if (c < 0x80) { ++i; return c; }
    if ((c >> 5) == 6) { cp = c & 31; n = 2; } else if ((c >> 4) == 14) { cp = c & 15; n = 3; } else if ((c >> 3) == 30) { cp = c & 7; n = 4; } else { ++i; return 0xFFFD; }
    if (i + n > s.size()) { i = s.size(); return 0xFFFD; }
    for (int k = 1; k < n; ++k) { const unsigned char d = static_cast<unsigned char>(s[i + k]); if ((d & 0xC0) != 0x80) { i += k; return 0xFFFD; } cp = (cp << 6) | (d & 63); }
    i += n;
    return cp;
}
[[maybe_unused]] static std::string enc(uint32_t cp) {
    std::string o;
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
    else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 63)); o += static_cast<char>(0x80 | ((cp >> 6) & 63)); o += static_cast<char>(0x80 | (cp & 63)); }
    return o;
}
static int cpw(uint32_t cp) {
    if (cp == 0) return 0;
    if (cp < 32 || (cp >= 0x7f && cp < 0xa0)) return -1;
    if ((cp >= 0x300 && cp <= 0x36F) || (cp >= 0x200B && cp <= 0x200F) || cp == 0xFE0E || cp == 0xFE0F || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x20D0 && cp <= 0x20FF)) return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) || (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F680 && cp <= 0x1F6FF) || (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD)) return 2;
    return 1;
}
static int swidth(const std::string& s) { int w = 0; for (size_t i = 0; i < s.size();) { const int x = cpw(dec(s, i)); if (x > 0) w += x; } return w; }
static std::string strip_ansi(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b' && i + 1 < s.size()) {
            if (s[i + 1] == '[') { size_t j = i + 2; while (j < s.size() && (s[j] < 0x40 || s[j] > 0x7e)) ++j; i = j; continue; }
            if (s[i + 1] == ']') { size_t j = i + 2; while (j < s.size() && s[j] != '\x07' && !(s[j] == '\x1b' && j + 1 < s.size() && s[j + 1] == '\\')) ++j; i = (j < s.size() && s[j] == '\x1b') ? j + 1 : j; continue; }
            ++i; continue;
        }
        if (s[i] == '\r') continue;
        o += s[i];
    }
    return o;
}

struct Cell { std::string ch; St s; int w; };
static void cells_of(const Line& l, std::vector<Cell>& out) {
    for (const auto& sgm : l) {
        const std::string& t = sgm.t;
        for (size_t i = 0; i < t.size();) {
            const size_t st = i;
            const uint32_t cp = dec(t, i);
            if (cp == '\t') { for (int k = 0; k < 4; ++k) out.push_back({" ", sgm.s, 1}); continue; }
            const int w = cpw(cp);
            if (w < 0) continue;
            if (w == 0) { if (!out.empty()) out.back().ch += t.substr(st, i - st); continue; }
            out.push_back({t.substr(st, i - st), sgm.s, w});
        }
    }
}
static Line join_cells(const std::vector<Cell>& c, size_t a, size_t b) {
    Line l;
    for (size_t i = a; i < b && i < c.size(); ++i) { if (!l.empty() && l.back().s == c[i].s) l.back().t += c[i].ch; else l.push_back({c[i].ch, c[i].s}); }
    return l;
}
static int lwidth(const Line& l) { int w = 0; for (const auto& s : l) w += swidth(s.t); return w; }
static Line fit(const Line& l, int width, bool ellipsis = false) {
    std::vector<Cell> c; cells_of(l, c);
    int w = 0; size_t i = 0;
    for (; i < c.size(); ++i) { if (w + c[i].w > width) break; w += c[i].w; }
    if (i == c.size()) return join_cells(c, 0, c.size());
    if (ellipsis && width > 1) {
        while (i > 0 && w + 1 > width) { --i; w -= c[i].w; }
        Line r = join_cells(c, 0, i); St s = i < c.size() ? c[i].s : St{}; r.push_back({"\xE2\x80\xA6", s}); return r;
    }
    return join_cells(c, 0, i);
}
static Line cat(Line a, const Line& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static Line pad(Line l, int width) { const int w = lwidth(l); if (w < width) l.push_back(sg(std::string(static_cast<size_t>(width - w), ' '))); return l; }
// Two-sided line: left content, right content right-aligned, clipped to width.
static Line lr(const Line& left, const Line& right, int width) {
    const int rw = lwidth(right);
    if (rw + 2 > width) return fit(left, width, true);
    Line l = fit(left, width - rw - 1, true);
    return cat(pad(l, width - rw), right);
}
static void wrap(const Line& content, int width, const Line& p1, const Line& p2, std::vector<Line>& out) {
    const int w1 = std::max(4, width - lwidth(p1)), w2 = std::max(4, width - lwidth(p2));
    std::vector<Cell> c; cells_of(content, c);
    if (c.empty()) { out.push_back(p1); return; }
    size_t i = 0; bool first = true;
    while (i < c.size()) {
        const int avail = first ? w1 : w2;
        int used = 0; size_t j = i, brk = SIZE_MAX;
        while (j < c.size() && used + c[j].w <= avail) { if (c[j].ch == " ") brk = j; used += c[j].w; ++j; }
        size_t end = j, next = j;
        if (j < c.size()) {
            if (c[j].ch == " ") { end = j; next = j + 1; }
            else if (brk != SIZE_MAX && brk > i) { end = brk; next = brk + 1; }
            if (end == i) { end = i + 1; next = i + 1; }
        }
        out.push_back(cat(first ? p1 : p2, join_cells(c, i, end)));
        i = next; first = false;
    }
}
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> v; std::string cur;
    for (char ch : s) { if (ch == '\n') { v.push_back(cur); cur.clear(); } else if (ch != '\r') cur += ch; }
    v.push_back(cur);
    return v;
}

// ===================================================================== markdown-lite
struct Para { Line c, p1, p2; };
static Line inline_md(const std::string& s, St base) {
    Line l; std::string cur; bool b = false, code = false;
    auto flush = [&] {
        if (cur.empty()) return;
        St st = base;
        if (code) { st.fg = CODE; } else if (b) st.a |= BOLD;
        l.push_back({cur, st}); cur.clear();
    };
    for (size_t i = 0; i < s.size();) {
        if (!code && s.compare(i, 2, "**") == 0) { flush(); b = !b; i += 2; continue; }
        if (s[i] == '`') { flush(); code = !code; ++i; continue; }
        cur += s[i++];
    }
    flush();
    return l;
}
static void md_paras(const std::string& text, const Line& first, const Line& rest, std::vector<Para>& out) {
    bool in_code = false, used_first = false, last_blank = false;
    auto P = [&]() -> const Line& { if (!used_first) { used_first = true; return first; } return rest; };
    for (std::string ln : split_lines(text)) {
        std::string t = trim(ln);
        if (t.rfind("```", 0) == 0) {
            const std::string lang = trim(t.substr(3));
            if (!in_code) { out.push_back({Line{sg(lang.empty() ? "code" : lang, FAINT, ITAL)}, cat(P(), Line{sg("\xE2\x95\xAD ", FAINT)}), rest}); }
            else out.push_back({Line{}, cat(P(), Line{sg("\xE2\x95\xB0", FAINT)}), rest});
            in_code = !in_code; last_blank = false; continue;
        }
        if (in_code) { out.push_back({Line{sg(ln)}, cat(P(), Line{sg("\xE2\x94\x82 ", FAINT)}), cat(rest, Line{sg("\xE2\x94\x82 ", FAINT)})}); continue; }
        if (t.empty()) { if (!last_blank && used_first) out.push_back({Line{}, rest, rest}); last_blank = true; continue; }
        last_blank = false;
        const size_t ind = ln.find_first_not_of(' ');
        const std::string sp(std::min<size_t>(ind == std::string::npos ? 0 : ind, 8) / 2 * 2, ' ');
        if (t[0] == '#') { size_t k = t.find_first_not_of('#'); std::string h = trim(t.substr(k == std::string::npos ? t.size() : k)); out.push_back({inline_md(h, St{0, 0, BOLD}), P(), rest}); continue; }
        if (t == "---" || t == "***" || t == "___") { out.push_back({Line{sg(std::string("\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80"), FAINT)}, P(), rest}); continue; }
        if ((t[0] == '-' || t[0] == '*' || t[0] == '+') && t.size() > 1 && t[1] == ' ') {
            const Line& p = P();
            out.push_back({inline_md(t.substr(2), St{}), cat(p, Line{sg(sp + "\xE2\x80\xA2 ", PEACH_D)}), cat(rest, Line{sg(sp + "  ")})}); continue;
        }
        if (std::isdigit(static_cast<unsigned char>(t[0]))) {
            size_t k = 0; while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
            if (k < t.size() - 1 && (t[k] == '.' || t[k] == ')') && t[k + 1] == ' ') {
                const std::string num = t.substr(0, k + 1) + " ";
                const Line& p = P();
                out.push_back({inline_md(t.substr(k + 2), St{}), cat(p, Line{sg(sp + num, MUTED)}), cat(rest, Line{sg(sp + std::string(num.size(), ' '))})}); continue;
            }
        }
        if (t[0] == '>') { const Line& p = P(); out.push_back({inline_md(trim(t.substr(1)), St{MUTED, 0, ITAL}), cat(p, Line{sg("\xE2\x94\x83 ", FAINT)}), cat(rest, Line{sg("\xE2\x94\x83 ", FAINT)})}); continue; }
        if (t[0] == '|') { out.push_back({Line{sg(t, MUTED)}, P(), rest}); continue; }
        out.push_back({inline_md(t, St{}), P(), rest});
    }
}

// ===================================================================== the app icon (half-blocks, true colour)
#include "logo.h"
static std::vector<Line> logo_art() {
    std::vector<Line> out;
    const uint32_t bg = g_light_bg ? 0xffffffu : 0x0d0c0eu;
    auto px = [&](int x, int y, bool& on) -> uint32_t {
        const uint32_t p = LOGO[y * LOGO_N + x]; const uint32_t a = p >> 24;
        on = a >= 24;
        return on ? mix(0x1000000u | bg, 0x1000000u | (p & 0xffffff), a / 255.0) : 0;
    };
    for (int r = 0; r + 1 < LOGO_N; r += 2) {
        Line l;
        for (int x = 0; x < LOGO_N; ++x) {
            bool t = false, b = false;
            const uint32_t ct = px(x, r, t), cb = px(x, r + 1, b);
            if (!t && !b) l.push_back(sg(" "));
            else if (t && b) l.push_back(sg("\xE2\x96\x80", ct, 0, cb));
            else if (t) l.push_back(sg("\xE2\x96\x80", ct));
            else l.push_back(sg("\xE2\x96\x84", cb));
        }
        out.push_back(l);
    }
    return out;
}

// ===================================================================== terminal
enum K { K_NONE, K_TEXT, K_ENTER, K_NL, K_TAB, K_BTAB, K_BS, K_DEL, K_LEFT, K_RIGHT, K_UP, K_DOWN, K_HOME, K_END, K_PGUP, K_PGDN,
         K_ESC, K_PASTE, K_CTRL, K_WUP, K_WDN, K_WLEFT, K_WRIGHT, K_WBS, K_RESIZE };
struct Key { K k = K_NONE; std::string s; char c = 0; };

static std::atomic<bool> g_active{false};
static bool g_mouse = false;
static const char* LEAVE_SEQ = "\x1b[?1000l\x1b[?1006l\x1b[?2004l\x1b[?7h\x1b[?25h\x1b[0m\x1b[?1049l";
#ifndef _WIN32
static termios g_orig{};
static void raw_on() {
    termios t = g_orig;
    t.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_cflag |= CS8;
    t.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
}
static void on_fatal(int sig) {
    if (g_active.load()) { const ssize_t n = ::write(STDOUT_FILENO, LEAVE_SEQ, std::strlen(LEAVE_SEQ)); (void)n; tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig); }
    signal(sig, SIG_DFL); raise(sig);
}
#else
static HANDLE g_hin = nullptr, g_hout = nullptr;
static DWORD g_in_mode = 0, g_out_mode = 0;
static void win_input_mode() { SetConsoleMode(g_hin, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS | (g_mouse ? ENABLE_MOUSE_INPUT : ENABLE_QUICK_EDIT_MODE)); }
#endif
static void twrite(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); std::fflush(stdout); }
static std::string mouse_seq() { return g_mouse ? "\x1b[?1000h\x1b[?1006h" : "\x1b[?1000l\x1b[?1006l"; }
static void term_enter() {
#ifndef _WIN32
    tcgetattr(STDIN_FILENO, &g_orig);
    raw_on();
    signal(SIGTERM, on_fatal); signal(SIGHUP, on_fatal); signal(SIGQUIT, on_fatal); signal(SIGSEGV, on_fatal); signal(SIGABRT, on_fatal);
#else
    g_hin = GetStdHandle(STD_INPUT_HANDLE); g_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(g_hin, &g_in_mode); GetConsoleMode(g_hout, &g_out_mode);
    win_input_mode();
    SetConsoleMode(g_hout, g_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
#endif
    g_active = true;
    twrite("\x1b[?1049h\x1b[?7l\x1b[?2004h\x1b[?25l\x1b[H\x1b[2J" + mouse_seq());
}
static void term_leave() {
    if (!g_active.exchange(false)) return;
    twrite(LEAVE_SEQ);
#ifndef _WIN32
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig);
#else
    SetConsoleMode(g_hin, g_in_mode); SetConsoleMode(g_hout, g_out_mode);
#endif
}
static void term_size(int& w, int& h) {
    w = 80; h = 24;
#ifndef _WIN32
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) { w = ws.ws_col; h = ws.ws_row; }
#else
    CONSOLE_SCREEN_BUFFER_INFO bi;
    if (GetConsoleScreenBufferInfo(g_hout, &bi)) { w = bi.srWindow.Right - bi.srWindow.Left + 1; h = bi.srWindow.Bottom - bi.srWindow.Top + 1; }
#endif
}

#ifndef _WIN32
// Parses one key from b at i. Returns bytes used, or 0 when more bytes are needed.
static size_t parse_key(const std::string& b, size_t i, Key& k, bool final) {
    const unsigned char c = static_cast<unsigned char>(b[i]);
    if (c == 0x1b) {
        if (i + 1 >= b.size()) { if (!final) return 0; k.k = K_ESC; return 1; }
        const char n = b[i + 1];
        if (n == '[') {
            if (b.compare(i, 6, "\x1b[200~") == 0) {
                const size_t e = b.find("\x1b[201~", i + 6);
                if (e == std::string::npos) { if (!final) return 0; k.k = K_PASTE; k.s = b.substr(i + 6); return b.size() - i; }
                k.k = K_PASTE; k.s = b.substr(i + 6, e - i - 6); return e + 6 - i;
            }
            size_t j = i + 2;
            while (j < b.size() && (static_cast<unsigned char>(b[j]) < 0x40 || static_cast<unsigned char>(b[j]) > 0x7e)) ++j;
            if (j >= b.size()) { if (!final) return 0; k.k = K_ESC; return 1; }
            const std::string p = b.substr(i + 2, j - i - 2);
            const char f = b[j];
            int mod = 1;
            if (const auto sc = p.find(';'); sc != std::string::npos) mod = std::atoi(p.c_str() + sc + 1);
            const bool word = mod > 1 && (((mod - 1) & 4) || ((mod - 1) & 2)), shift = mod > 1 && ((mod - 1) & 1);
            switch (f) {
                case 'A': k.k = K_UP; break;
                case 'B': k.k = K_DOWN; break;
                case 'C': k.k = word ? K_WRIGHT : K_RIGHT; break;
                case 'D': k.k = word ? K_WLEFT : K_LEFT; break;
                case 'H': k.k = K_HOME; break;
                case 'F': k.k = K_END; break;
                case 'Z': k.k = K_BTAB; break;
                case '~': {
                    const int n0 = std::atoi(p.c_str());
                    if (n0 == 1 || n0 == 7) k.k = K_HOME; else if (n0 == 4 || n0 == 8) k.k = K_END; else if (n0 == 3) k.k = K_DEL;
                    else if (n0 == 5) k.k = K_PGUP; else if (n0 == 6) k.k = K_PGDN;
                    else if (n0 == 27) { const auto l2 = p.rfind(';'); if (l2 != std::string::npos && std::atoi(p.c_str() + l2 + 1) == 13) k.k = K_NL; }
                    break;
                }
                case 'u': {
                    const int code = std::atoi(p.c_str());
                    if (code == 13) k.k = mod > 1 ? K_NL : K_ENTER; else if (code == 27) k.k = K_ESC; else if (code == 9) k.k = shift ? K_BTAB : K_TAB;
                    else if (code == 127) k.k = word ? K_WBS : K_BS;
                    break;
                }
                case 'M': case 'm':
                    if (!p.empty() && p[0] == '<' && f == 'M') { const int bt = std::atoi(p.c_str() + 1); if (bt == 64) k.k = K_WUP; else if (bt == 65) k.k = K_WDN; }
                    break;
                default: break;
            }
            return j - i + 1;
        }
        if (n == 'O') {
            if (i + 2 >= b.size()) { if (!final) return 0; k.k = K_ESC; return 1; }
            switch (b[i + 2]) { case 'A': k.k = K_UP; break; case 'B': k.k = K_DOWN; break; case 'C': k.k = K_RIGHT; break; case 'D': k.k = K_LEFT; break;
                                case 'H': k.k = K_HOME; break; case 'F': k.k = K_END; break; default: break; }
            return 3;
        }
        if (n == '\r' || n == '\n') { k.k = K_NL; return 2; }
        if (n == 0x7f || n == 8) { k.k = K_WBS; return 2; }
        if (n == 'b') { k.k = K_WLEFT; return 2; }
        if (n == 'f') { k.k = K_WRIGHT; return 2; }
        if (n == 0x1b) { k.k = K_ESC; return 1; }
        return 2;   // other Alt+key: ignored
    }
    if (c == '\r') { k.k = K_ENTER; return 1; }
    if (c == '\n') { k.k = K_NL; return 1; }
    if (c == '\t') { k.k = K_TAB; return 1; }
    if (c == 0x7f || c == 8) { k.k = K_BS; return 1; }
    if (c < 0x20) { if (c == 0) return 1; k.k = K_CTRL; k.c = static_cast<char>('a' + c - 1); return 1; }
    size_t j = i;
    while (j < b.size() && static_cast<unsigned char>(b[j]) >= 0x20 && b[j] != 0x7f) ++j;
    if (!final) {   // do not split a UTF-8 sequence that is still arriving
        size_t s = j; int back = 0;
        while (s > i && back < 4 && (static_cast<unsigned char>(b[s - 1]) & 0xC0) == 0x80) { --s; ++back; }
        if (s > i) {
            const unsigned char lead = static_cast<unsigned char>(b[s - 1]);
            const int need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
            if (need > back) j = s - 1;
        }
        if (j == i) return 0;
    }
    k.k = K_TEXT; k.s = b.substr(i, j - i);
    return j - i;
}
static std::string g_inbuf;
static void read_keys(int timeout_ms, std::vector<Key>& out) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    if (g_inbuf.empty() && poll(&p, 1, timeout_ms) <= 0) return;
    char buf[8192];
    const long long t0 = now_ms();
    while (true) {
        const ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
        if (n > 0) g_inbuf.append(buf, static_cast<size_t>(n));
        bool need_more = false;
        size_t i = 0;
        std::vector<Key> got;
        while (i < g_inbuf.size()) {
            Key k;
            const size_t used = parse_key(g_inbuf, i, k, false);
            if (!used) { need_more = true; break; }
            if (k.k == K_ENTER && i + used < g_inbuf.size() && g_inbuf[i + used] != '\x1b') k.k = K_NL;   // pasted text without bracketed paste
            if (k.k != K_NONE) got.push_back(k);
            i += used;
        }
        g_inbuf.erase(0, i);
        out.insert(out.end(), got.begin(), got.end());
        if (!need_more) return;
        const bool in_paste = g_inbuf.rfind("\x1b[200~", 0) == 0;
        if (poll(&p, 1, in_paste ? 200 : 30) <= 0 || now_ms() - t0 > (in_paste ? 3000 : 120)) {
            size_t j = 0;
            while (j < g_inbuf.size()) { Key k; const size_t used = parse_key(g_inbuf, j, k, true); if (k.k != K_NONE) out.push_back(k); j += used ? used : 1; }
            g_inbuf.clear();
            return;
        }
    }
}
#else
static void read_keys(int timeout_ms, std::vector<Key>& out) {
    if (WaitForSingleObject(g_hin, static_cast<DWORD>(timeout_ms)) != WAIT_OBJECT_0) return;
    INPUT_RECORD recs[256]; DWORD n = 0;
    if (!ReadConsoleInputW(g_hin, recs, 256, &n)) return;
    static wchar_t hi = 0;
    auto text = [&](const std::string& s) { if (!out.empty() && out.back().k == K_TEXT) out.back().s += s; else { Key k; k.k = K_TEXT; k.s = s; out.push_back(k); } };
    auto push = [&](K kk, char c = 0) { Key k; k.k = kk; k.c = c; out.push_back(k); };
    for (DWORD i = 0; i < n; ++i) {
        const INPUT_RECORD& r = recs[i];
        if (r.EventType == WINDOW_BUFFER_SIZE_EVENT) { push(K_RESIZE); continue; }
        if (r.EventType == MOUSE_EVENT) {
            if (r.Event.MouseEvent.dwEventFlags & MOUSE_WHEELED) push(static_cast<short>(HIWORD(r.Event.MouseEvent.dwButtonState)) > 0 ? K_WUP : K_WDN);
            continue;
        }
        if (r.EventType != KEY_EVENT || !r.Event.KeyEvent.bKeyDown) continue;
        const KEY_EVENT_RECORD& e = r.Event.KeyEvent;
        const DWORD st = e.dwControlKeyState;
        const bool ctrl = st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED), alt = st & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED), shift = st & SHIFT_PRESSED;
        const bool altgr = ctrl && alt;
        const wchar_t ch = e.uChar.UnicodeChar;
        for (WORD rep = 0; rep < std::max<WORD>(1, e.wRepeatCount); ++rep) {
            switch (e.wVirtualKeyCode) {
                case VK_RETURN: {
                    bool more = false;
                    for (DWORD j = i + 1; j < n; ++j) if (recs[j].EventType == KEY_EVENT && recs[j].Event.KeyEvent.bKeyDown) { more = true; break; }
                    if (!more) { DWORD pend = 0; if (GetNumberOfConsoleInputEvents(g_hin, &pend) && pend > 0) more = true; }
                    push(more || shift || alt || (ctrl && !altgr) ? K_NL : K_ENTER); continue;
                }
                case VK_BACK: push(ctrl || alt ? K_WBS : K_BS); continue;
                case VK_TAB: push(shift ? K_BTAB : K_TAB); continue;
                case VK_ESCAPE: push(K_ESC); continue;
                case VK_LEFT: push(ctrl || alt ? K_WLEFT : K_LEFT); continue;
                case VK_RIGHT: push(ctrl || alt ? K_WRIGHT : K_RIGHT); continue;
                case VK_UP: push(K_UP); continue;
                case VK_DOWN: push(K_DOWN); continue;
                case VK_PRIOR: push(K_PGUP); continue;
                case VK_NEXT: push(K_PGDN); continue;
                case VK_HOME: push(K_HOME); continue;
                case VK_END: push(K_END); continue;
                case VK_DELETE: push(K_DEL); continue;
                default: break;
            }
            if (ctrl && !altgr && e.wVirtualKeyCode >= 'A' && e.wVirtualKeyCode <= 'Z') {
                const char c = static_cast<char>('a' + (e.wVirtualKeyCode - 'A'));
                if (c == 'j') push(K_NL); else push(K_CTRL, c);
                continue;
            }
            if (!ch) continue;
            if (ch >= 0xD800 && ch <= 0xDBFF) { hi = ch; continue; }
            uint32_t cp = ch;
            if (ch >= 0xDC00 && ch <= 0xDFFF) { if (!hi) continue; cp = 0x10000u + ((static_cast<uint32_t>(hi) - 0xD800u) << 10) + (static_cast<uint32_t>(ch) - 0xDC00u); hi = 0; }
            if (cp < 0x20) continue;
            text(enc(cp));
        }
    }
}
#endif

static std::string b64(const std::string& s) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; size_t i = 0;
    for (; i + 2 < s.size(); i += 3) { const uint32_t v = (static_cast<unsigned char>(s[i]) << 16) | (static_cast<unsigned char>(s[i + 1]) << 8) | static_cast<unsigned char>(s[i + 2]); o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63]; }
    if (i + 1 == s.size()) { const uint32_t v = static_cast<unsigned char>(s[i]) << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == s.size()) { const uint32_t v = (static_cast<unsigned char>(s[i]) << 16) | (static_cast<unsigned char>(s[i + 1]) << 8); o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
    return o;
}
static std::string ascii_lower(std::string s) { return lower(std::move(s)); }
static std::string mmss(double ms) { const long s = static_cast<long>(ms / 1000); char b[32]; if (s >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60); else std::snprintf(b, sizeof b, "%ld:%02ld", s / 60, s % 60); return b; }

// ===================================================================== app state
struct Block {
    std::vector<Para> paras;
    std::function<void(int, std::vector<Line>&)> gen;
    bool dynamic = false;
    int gap = 1, cw = -1;
    std::vector<Line> cache;
    std::string plain;   // for /export
    const std::vector<Line>& lines(int w) {
        if (gen) { if (dynamic || cw != w) { cache.clear(); gen(w, cache); cw = w; } return cache; }
        if (cw != w) { cache.clear(); for (const auto& p : paras) wrap(p.c, w, p.p1, p.p2, cache); cw = w; }
        return cache;
    }
};
using BlockP = std::shared_ptr<Block>;
struct Item { Line label; std::string id, search; Line right; };
struct App;
struct ListOv {
    bool on = false, filterable = true, loading = false;
    std::string kind, title, hint, empty = "Nothing here yet.", filter;
    std::vector<Item> items;
    int sel = 0, top = 0;
    std::function<void(App&, const Item&)> pick;
    std::function<bool(App&, const Item&, const Key&)> key;
};
struct TextOv { bool on = false; std::string title, hint; std::vector<Line> lines; int top = 0; };
struct Dialog { bool on = false; std::string kind, qid, title; std::vector<Line> body; std::vector<std::string> opts; int sel = 0; std::function<void(App&, int)> done; };
struct PA { std::string id, task, status = "starting", last, report, failing; int steps = 0, files = 0; double ms = 0; };
struct PGroup { std::vector<PA> ags; bool merge = false, finished = false; std::vector<Line> result; long long t0 = 0; std::atomic<bool> stop{false}; };
struct ShellRun { std::string cmd; std::vector<std::string> lines; size_t dropped = 0; bool done = false; int code = 0; long long t0 = 0, ms = 0; };
struct Cmd { const char* name; const char* args; const char* desc; };
static const Cmd CMDS[] = {
    {"help", "", "Commands and keyboard shortcuts"},
    {"model", "[id]", "Choose the model (auto = best first, automatic fallback)"},
    {"models", "", "List the coding models, best first"},
    {"mode", "plan|ask|auto", "How much the agent may do on its own"},
    {"new", "", "Start a new session (keeps the screen)"},
    {"clear", "", "Clear the screen and start a new session"},
    {"resume", "[id]", "Resume an earlier session of this project"},
    {"sessions", "", "Browse this project's sessions"},
    {"diff", "", "Show what this session changed"},
    {"undo", "", "Restore the project to before this session"},
    {"stop", "", "Stop the agent (also Esc)"},
    {"parallel", "\"task\" \"task\" [--merge]", "Several agents at once, each in an isolated copy (or: task | task)"},
    {"agents", "", "Manage running and finished agents"},
    {"merge", "<id>", "Merge an isolated agent's changes into the project"},
    {"discard", "<id>", "Throw away an isolated agent's copy"},
    {"review", "on|off", "A second model reviews the work before it finishes"},
    {"init", "", "Write AGENTS.md, a guide to this project for agents"},
    {"export", "[file]", "Save this conversation as Markdown"},
    {"copy", "", "Copy the last answer to the clipboard"},
    {"panel", "", "Show or hide the side panel (Ctrl+B)"},
    {"mouse", "", "Turn mouse-wheel scrolling on or off"},
    {"status", "", "Engine, project and session details"},
    {"quit", "", "Exit (also Ctrl+D)"},
};
static const char* SPIN[] = {"\xC2\xB7", "\xE2\x9C\xA2", "\xE2\x9C\xB3", "\xE2\x9C\xB6", "\xE2\x9C\xBB", "\xE2\x9C\xBD", "\xE2\x9C\xBB", "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2"};
static const char* BRAILLE_SPIN[] = {"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9", "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4", "\xE2\xA0\xA6", "\xE2\xA0\xA7", "\xE2\xA0\x87", "\xE2\xA0\x8F"};

struct App {
    Opts o; std::string project; fs::path root;
    // session
    std::string sid, title, status = "idle", waiting, live, last_tool, model_used, skip_user, final_text, answer_qid;
    long long since = 0, run_t0 = 0; int steps = 0;
    std::atomic<uint64_t> gen{0};
    bool polling = false, running = false, stopping = false;
    std::map<std::string, Value> pending; std::set<std::string> asked;
    std::vector<std::pair<std::string, std::string>> changes;
    std::vector<std::pair<std::string, std::string>> todo;   // status, text
    std::vector<BlockP> blocks;
    std::vector<std::shared_ptr<PGroup>> groups;
    // input
    std::string in; size_t cur = 0; std::vector<std::string> hist; int hidx = -1; std::string hsave;
    int pop_sel = 0, in_top = 0; bool pop_dismissed = false;
    // view
    int W = 80, H = 24, scroll = 0, last_th = 10; bool panel_auto = true, panel_on = false, quit = false, dirty = true, full = true, welcome_visible = true;
    std::vector<std::string> prev;
    long long last_ctrl_c = 0, last_esc = 0, toast_t = 0, last_frame = 0, last_agents = 0; std::string toast; uint32_t toast_c = 0;
    ListOv lo; TextOv to; Dialog dg;
    // data
    Value models; bool models_loading = false; int nmodels = 0; std::string best_model;
    std::vector<std::string> files; bool files_loaded = false;
    // cross-thread queue
    std::mutex mu; std::deque<std::function<void()>> q;

    void post(std::function<void()> f) { std::lock_guard<std::mutex> lk(mu); q.push_back(std::move(f)); }
    void drain() {
        std::deque<std::function<void()>> d;
        { std::lock_guard<std::mutex> lk(mu); d.swap(q); }
        for (auto& f : d) { f(); dirty = true; }
    }
    void async(std::function<Value()> work, std::function<void(const Value&, const std::string&)> then) {
        std::thread([this, work, then] {
            Value r; std::string err;
            try { r = work(); } catch (const std::exception& e) { err = e.what(); }
            post([r, err, then] { if (then) then(r, err); });
        }).detach();
    }
    void say_toast(const std::string& t, uint32_t c = 0) { toast = t; toast_c = c ? c : MUTED; toast_t = now_ms(); dirty = true; }

    // ---------------------------------------------------------------- blocks
    BlockP add(std::vector<Para> paras, const std::string& plain, int gap = 1) {
        auto b = std::make_shared<Block>(); b->paras = std::move(paras); b->plain = plain; b->gap = gap; blocks.push_back(b);
        if (scroll > 0) scroll += 0;   // keep the reader's place
        dirty = true;
        return b;
    }
    void note(const std::string& text, uint32_t c = 0, int gap = 1) {
        std::vector<Para> ps; bool first = true;
        for (const auto& l : split_lines(text)) { ps.push_back({Line{sg(l, c ? c : MUTED)}, Line{sg(first ? "  " : "  ")}, Line{sg("  ")}}); first = false; }
        add(ps, text, gap);
    }
    void error_block(const std::string& text) { add({Para{Line{sg(text, RED)}, Line{sg("\xE2\x9C\x97 ", RED, BOLD)}, Line{sg("  ")}}}, "Error: " + text); }
    void user_block(const std::string& text) {
        std::vector<Para> ps; bool first = true;
        for (const auto& l : split_lines(text)) { ps.push_back({Line{sg(l, 0, BOLD)}, first ? Line{sg("\xE2\x80\xBA ", PEACH, BOLD)} : Line{sg("  ")}, Line{sg("  ")}}); first = false; }
        add(ps, "> " + text);
    }
    void md_block(const std::string& text, const Line& first, int gap = 1) {
        std::vector<Para> ps; md_paras(text, first, Line{sg("  ")}, ps); add(ps, text, gap);
    }

    // ---------------------------------------------------------------- events
    void apply(const Value& e) {
        const std::string t = e["type"].str();
        if (t == "user") {
            const std::string tx = e["text"].str();
            if (!skip_user.empty() && tx == skip_user) { skip_user.clear(); return; }
            if (title.empty()) title = one_line(tx);
            user_block(tx); return;
        }
        if (t == "say") { if (e["final"].truthy()) return; md_block(e["text"].str(), Line{sg("\xE2\x97\x8F ", PEACH)}); return; }
        if (t == "tool") { last_tool = e["label"].str(); return; }
        if (t == "tool_done") {
            const bool ok = e["ok"].truthy(), blocked = e["blocked"].truthy();
            const std::string tool = e["tool"].str();
            Line l{ok ? sg("\xE2\x9C\x93 ", GREEN) : blocked ? sg("\xE2\x8A\x98 ", AMBER) : sg("\xE2\x9C\x97 ", RED), sg(e["label"].str())};
            if (!e["added"].is_null() || !e["removed"].is_null()) { l.push_back(sg("  +" + e["added"].str("0"), GREEN)); l.push_back(sg(" \xE2\x88\x92" + e["removed"].str("0"), RED)); }
            else if (tool == "bash" && !e["code"].is_null()) l.push_back(sg("  exit " + e["code"].str(), MUTED));
            if (e["ms"].num() > 0) l.push_back(sg("  \xC2\xB7 " + fmt_ms(e["ms"].num()), FAINT));
            std::vector<Para> ps{{l, Line{sg("  ")}, Line{sg("    ")}}};
            std::string plain = std::string(ok ? "- ok: " : "- failed: ") + e["label"].str();
            if (!ok) {
                std::string why = e["summary"].str();
                if (why.rfind("BLOCKED: ", 0) == 0) why = why.substr(9);
                if (!why.empty() && !(tool == "bash" && why.rfind("exit code", 0) == 0)) ps.push_back({Line{sg(clip(one_line(why), 400), blocked ? AMBER : RED)}, Line{sg("    ")}, Line{sg("    ")}});
                if (tool == "bash" || tool == "start_server") {
                    auto ls = split_lines(strip_ansi(e["out"].str()));
                    while (!ls.empty() && trim(ls.back()).empty()) ls.pop_back();
                    for (size_t i = ls.size() > 8 ? ls.size() - 8 : 0; i < ls.size(); ++i) ps.push_back({Line{sg(ls[i], MUTED)}, Line{sg("    \xE2\x94\x82 ", FAINT)}, Line{sg("    \xE2\x94\x82 ", FAINT)}});
                }
            }
            add(ps, plain, 0);
            if (!e["added"].is_null() || !e["removed"].is_null()) refresh_changes();
            return;
        }
        if (t == "approval" || t == "question") { pending[e["id"].str()] = e; return; }
        if (t == "answered") { add({Para{Line{sg(e["answer"].str(), MUTED)}, Line{sg("    \xE2\x86\xB3 ", MUTED)}, Line{sg("      ")}}}, "  -> " + e["answer"].str(), 0); return; }
        if (t == "note") { add({Para{Line{sg(e["text"].str(), MUTED)}, Line{sg("  \xC2\xB7 ", FAINT)}, Line{sg("    ")}}}, "  " + e["text"].str(), 0); return; }
        if (t == "todo") {
            todo.clear();
            std::vector<Para> ps{{Line{sg("To-do", MUTED, BOLD)}, Line{sg("  ")}, Line{sg("  ")}}};
            std::string plain = "To-do:";
            if (auto a = e["items"].arr()) for (const auto& it : *a) {
                const std::string st = it["status"].str(), tx = it["text"].str();
                todo.push_back({st, tx});
                const Seg box = st == "done" ? sg("\xE2\x98\x91 ", GREEN) : st == "doing" ? sg("\xE2\x97\x90 ", AMBER) : sg("\xE2\x98\x90 ", FAINT);
                ps.push_back({Line{st == "done" ? sg(tx, MUTED) : st == "doing" ? sg(tx, 0, BOLD) : sg(tx)}, Line{sg("   "), box}, Line{sg("     ")}});
                plain += "\n- [" + std::string(st == "done" ? "x" : " ") + "] " + tx;
            }
            add(ps, plain, 1);
            return;
        }
        if (t == "review") {
            const std::string v = e["verdict"].str();
            Line h{sg("\xE2\x97\x86 Review ", PEACH, BOLD), sg(v, v == "PASS" ? GREEN : v == "FIX" ? AMBER : MUTED, BOLD)};
            if (!e["model"].str().empty()) h.push_back(sg("  \xC2\xB7 " + e["model"].str(), FAINT));
            std::vector<Para> ps{{h, Line{sg("  ")}, Line{sg("  ")}}};
            md_paras(e["text"].str(), Line{sg("    ")}, Line{sg("    ")}, ps);
            add(ps, "Review " + v + ": " + e["text"].str());
            return;
        }
        if (t == "done") {
            final_text = e["text"].str();
            if (!e["model"].str().empty()) model_used = e["model"].str();
            const size_t nf = e["files"].arr() ? e["files"].arr()->size() : 0;
            Line h{sg("\xE2\x9C\x94 Done", GREEN, BOLD), sg("  \xC2\xB7 " + std::to_string(nf) + (nf == 1 ? " file" : " files") + " changed \xC2\xB7 " + e["steps"].str("0") + (e["steps"].num() == 1 ? " step" : " steps") + " \xC2\xB7 " + fmt_ms(e["ms"].num()) + (e["model"].str().empty() ? "" : " \xC2\xB7 " + e["model"].str()), MUTED)};
            std::vector<Para> ps{{h, Line{}, Line{sg("  ")}}};
            if (!final_text.empty()) { ps.push_back({Line{}, Line{}, Line{}}); md_paras(final_text, Line{sg("  ")}, Line{sg("  ")}, ps); }
            if (auto a = e["files"].arr()) {
                if (!a->empty()) ps.push_back({Line{}, Line{}, Line{}});
                size_t n = 0;
                for (const auto& f : *a) {
                    if (++n > 30) { ps.push_back({Line{sg("\xE2\x80\xA6 and " + std::to_string(a->size() - 30) + " more", MUTED)}, Line{sg("  ")}, Line{sg("  ")}}); break; }
                    const std::string st = f["status"].str();
                    ps.push_back({Line{sg(f["path"].str())}, Line{sg("  "), st == "added" ? sg("+ ", GREEN) : st == "deleted" ? sg("- ", RED) : sg("~ ", AMBER)}, Line{sg("    ")}});
                }
            }
            if (!e["failing"].str().empty()) ps.push_back({Line{sg("! the last check still fails: " + e["failing"].str(), AMBER)}, Line{sg("  ")}, Line{sg("    ")}});
            add(ps, "Done: " + final_text);
            refresh_changes();
            return;
        }
        if (t == "error") { error_block(e["text"].str()); return; }
    }
    void on_poll(const Value& j, bool idle) {
        if (auto a = j["events"].arr()) for (const auto& e : *a) { since = std::max(since, static_cast<long long>(e["seq"].num())); apply(e); }
        status = j["status"].str(); live = j["live"].str(); waiting = j["waiting"].str();
        if (!j["steps"].is_null()) steps = static_cast<int>(j["steps"].num());
        running = j["running"].truthy() || !waiting.empty();
        if (dg.on && (dg.kind == "approval" || dg.kind == "question") && dg.qid != waiting) dg.on = false;
        if (!answer_qid.empty() && answer_qid != waiting) answer_qid.clear();
        if (!waiting.empty() && !asked.count(waiting)) {
            auto it = pending.find(waiting);
            if (it != pending.end()) { asked.insert(waiting); open_question(it->second); }
        }
        if (idle) {
            polling = false; running = false; stopping = false; live.clear();
            if (status == "stopped") note("Stopped. Send a message to continue, or /undo to restore the project.");
            else if (status == "paused") note("Step limit reached. Send \"continue\" to keep going.");
            refresh_changes();
            twrite("\x07");   // a soft bell: the agent is done (terminals may show it as a badge)
        }
    }
    void start_poll() {
        if (sid.empty()) return;
        const uint64_t g = ++gen;
        polling = true;
        const std::string id = sid; const long long s0 = since;
        std::thread([this, g, id, s0] {
            long long s = s0; std::string live_seen; int fails = 0;
            while (gen.load() == g) {
                Value j;
                try { j = api("/api/agent/events", Object{{"id", id}, {"since", static_cast<double>(s)}, {"wait", 1}, {"live", live_seen}}, 40); fails = 0; }
                catch (const ApiError& e) {
                    if (e.status == 0 && ++fails < 60) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }
                    const std::string m = e.what();
                    post([this, g, m] { if (gen.load() != g) return; polling = false; running = false; error_block(m); });
                    return;
                } catch (const std::exception& e) {
                    const std::string m = e.what();
                    post([this, g, m] { if (gen.load() != g) return; polling = false; running = false; error_block(m); });
                    return;
                }
                live_seen = j["live"].str();
                bool any = false;
                if (auto a = j["events"].arr()) for (const auto& e : *a) { s = std::max(s, static_cast<long long>(e["seq"].num())); any = true; }
                const bool idle = !j["running"].truthy() && j["waiting"].str().empty() && !any;
                post([this, g, j, idle] { if (gen.load() == g) on_poll(j, idle); });
                if (idle) return;
            }
        }).detach();
    }
    void refresh_changes() {
        if (sid.empty()) return;
        const std::string id = sid;
        async([id] { return api("/api/agent/changes", Object{{"id", id}}, 20); }, [this, id](const Value& r, const std::string& err) {
            if (!err.empty() || id != sid) return;
            changes.clear();
            if (auto a = r["changes"].arr()) for (const auto& c : *a) changes.push_back({c["path"].str(), c["status"].str()});
        });
    }

    // ---------------------------------------------------------------- actions
    void send_prompt(const std::string& text, const std::string& shown = "") {
        user_block(shown.empty() ? text : shown);
        if (title.empty()) title = one_line(shown.empty() ? text : shown);
        skip_user = text; running = true; stopping = false; run_t0 = now_ms(); status = "starting"; last_tool.clear(); live.clear(); scroll = 0;
        const bool fresh = sid.empty();
        const Opts oo = o; const std::string proj = project, id = sid;
        async([fresh, oo, proj, id, text]() -> Value {
            if (fresh) return api("/api/agent/start", start_body(oo, proj, text, true), 600);
            api("/api/agent/send", Object{{"id", id}, {"prompt", text}, {"mode", oo.mode}, {"model", oo.model}});
            return Value(Object{{"id", id}});
        }, [this, fresh](const Value& r, const std::string& err) {
            if (!err.empty()) { running = false; error_block(err); return; }
            if (fresh) {
                sid = r["id"].str(); since = 0;
                if (r["isolated"].truthy()) note("Working in an isolated copy: " + r["project"].str());
            }
            if (!polling) start_poll();
        });
    }
    void stop_agent() {
        bool any = false;
        if (running && !sid.empty()) { const std::string id = sid; stopping = true; any = true; async([id] { return api("/api/agent/stop", Object{{"id", id}}, 10); }, nullptr); }
        for (auto& g : groups) if (!g->finished) { g->stop = true; any = true; }
        say_toast(any ? "Stopping\xE2\x80\xA6" : "Nothing is running.");
    }
    void answer(const std::string& qid, const std::string& text) {
        const std::string id = sid;
        async([id, qid, text] { return api("/api/agent/answer", Object{{"id", id}, {"qid", qid}, {"answer", text}}, 10); },
              [this](const Value&, const std::string& err) { if (!err.empty()) error_block(err); });
    }
    void set_mode(const std::string& m) {
        o.mode = m;
        if (!sid.empty()) { const std::string id = sid; async([id, m] { return api("/api/agent/mode", Object{{"id", id}, {"mode", m}}, 10); }, nullptr); }
        say_toast(m == "plan" ? "Plan mode: the agent reads and plans, but changes nothing" : m == "ask" ? "Ask mode: you approve each change and command" : "Auto mode: the agent works on its own (undo with /undo)", m == "plan" ? BLUE : m == "ask" ? PEACH : GREEN);
    }
    void set_model(const std::string& m) {
        o.model = m.empty() ? "auto" : m;
        if (!sid.empty()) { const std::string id = sid, mm = o.model; async([id, mm] { return api("/api/agent/mode", Object{{"id", id}, {"model", mm}}, 10); }, nullptr); }
        say_toast("Model: " + model_label(o.model), PEACH);
    }
    std::string model_label(const std::string& id) {
        if (id == "auto" || id.empty()) return "auto";
        for (const char* k : {"ranked", "all"}) if (auto a = models[k].arr()) for (const auto& m : *a) if (m["id"].str() == id) return m["label"].str();
        return id;
    }
    void new_session(bool clear_screen) {
        ++gen; sid.clear(); since = 0; title.clear(); running = false; polling = false; status = "idle"; changes.clear(); todo.clear();
        pending.clear(); asked.clear(); answer_qid.clear(); steps = 0; final_text.clear(); scroll = 0;
        if (clear_screen) { blocks.resize(std::min<size_t>(blocks.size(), 1)); full = true; }
        else add({Para{Line{sg("\xE2\x94\x80\xE2\x94\x80 new session \xE2\x94\x80\xE2\x94\x80", FAINT)}, Line{sg("  ")}, Line{sg("  ")}}}, "---");
    }
    void resume(const std::string& id) {
        say_toast("Opening " + id + "\xE2\x80\xA6");
        async([id] { return api("/api/agent/get", Object{{"id", id}}, 30); }, [this, id](const Value& r, const std::string& err) {
            if (!err.empty()) { error_block(err); return; }
            new_session(true);
            sid = id; title = r["title"].str();
            if (auto a = r["events"].arr()) for (const auto& e : *a) {
                since = std::max(since, static_cast<long long>(e["seq"].num()));
                const std::string t = e["type"].str();
                if ((t == "approval" || t == "question") && e["id"].str() != r["waiting"].str()) asked.insert(e["id"].str());
                apply(e);
            }
            const std::string m = r["mode"].str(); if (m == "plan" || m == "ask" || m == "auto") o.mode = m;
            status = r["status"].str();
            note("Resumed " + id + " (" + status + "). Your next message continues it.");
            refresh_changes();
            if (r["running"].truthy() || !r["waiting"].str().empty()) { running = true; run_t0 = now_ms(); start_poll(); }
        });
    }
    void load_models(std::function<void()> then = nullptr) {
        if (models_loading) return;
        models_loading = true;
        async([] { return api("/api/agent/models", {}, 30); }, [this, then](const Value& r, const std::string& err) {
            models_loading = false;
            if (!err.empty()) { if (then) error_block(err); return; }
            models = r;
            if (!blocks.empty()) blocks[0]->cw = -1;
            nmodels = r["all"].arr() ? static_cast<int>(r["all"].arr()->size()) : 0;
            if (auto a = r["ranked"].arr(); a && !a->empty()) best_model = (*a)[0]["label"].str();
            if (then) then();
        });
    }
    void open_models() {
        lo = ListOv{}; lo.on = true; lo.kind = "models"; lo.title = "Choose a model"; lo.hint = "type to filter \xC2\xB7 enter choose \xC2\xB7 esc close";
        auto fill = [this] {
            if (!lo.on || lo.kind != "models") return;
            lo.items.clear(); lo.loading = false;
            lo.items.push_back({Line{sg("Auto", 0, BOLD)}, "auto", "auto best", Line{sg("best first, automatic fallback", MUTED)}});
            std::set<std::string> seen{"auto"};
            if (auto a = models["ranked"].arr()) for (const auto& m : *a) { const std::string id = m["id"].str(); if (!seen.insert(id).second) continue; lo.items.push_back({Line{sg(m["label"].str())}, id, ascii_lower(m["label"].str() + " " + id + " " + m["provider"].str()), Line{sg(m["provider"].str(), MUTED)}}); }
            if (auto a = models["all"].arr()) for (const auto& m : *a) {
                const std::string id = m["id"].str(); if (!seen.insert(id).second) continue;
                const bool paid = m["paid"].truthy(), blocked = m["blocked"].truthy();
                lo.items.push_back({Line{sg(m["label"].str(), blocked ? FAINT : 0)}, id, ascii_lower(m["label"].str() + " " + id), Line{sg(blocked ? "unavailable" : paid ? "paid \xC2\xB7 opt-in" : "free", blocked ? FAINT : paid ? AMBER : MUTED)}});
            }
            for (size_t i = 0; i < lo.items.size(); ++i) if (lo.items[i].id == o.model) lo.sel = static_cast<int>(i);
        };
        lo.pick = [](App& a, const Item& it) { a.set_model(it.id); a.lo.on = false; };
        if (models.is_null()) { lo.loading = true; load_models(fill); } else fill();
    }
    void open_sessions() {
        lo = ListOv{}; lo.on = true; lo.kind = "sessions"; lo.title = "Sessions in " + project; lo.hint = "enter resume \xC2\xB7 esc close"; lo.loading = true; lo.empty = "No sessions in this project yet.";
        const std::string proj = project;
        async([proj] { return api("/api/agent/list", Object{{"project", proj}}, 20); }, [this](const Value& r, const std::string& err) {
            if (!lo.on || lo.kind != "sessions") return;
            lo.loading = false;
            if (!err.empty()) { lo.empty = err; return; }
            if (auto a = r["sessions"].arr()) for (const auto& s : *a) {
                const std::string st = s["status"].str();
                lo.items.push_back({Line{sg(s["title"].str().empty() ? "(untitled)" : one_line(s["title"].str()))}, s["id"].str(), ascii_lower(s["title"].str() + " " + s["id"].str()),
                                    Line{sg(st, st == "done" ? GREEN : st == "error" ? RED : st == "running" || st == "waiting" ? AMBER : MUTED), sg("  " + ago(s["updated"].num()), FAINT)}});
            }
        });
        lo.pick = [](App& a, const Item& it) { a.lo.on = false; a.resume(it.id); };
    }
    void refresh_agents() {
        if (lo.loading) return;
        lo.loading = lo.items.empty();
        last_agents = now_ms();
        async([] { return api("/api/agent/list", Object{{"all", true}}, 20); }, [this](const Value& r, const std::string& err) {
            if (!lo.on || lo.kind != "agents") return;
            lo.loading = false;
            if (!err.empty()) { lo.empty = err; return; }
            const std::string keep = lo.sel < static_cast<int>(lo.items.size()) ? lo.items[static_cast<size_t>(lo.sel)].id : "";
            lo.items.clear();
            lo.title = "Agents \xC2\xB7 " + r["running"].str("0") + " / " + r["max"].str("8") + " running";
            if (auto a = r["sessions"].arr()) for (const auto& s : *a) {
                const std::string st = s["status"].str();
                const std::string where = s["isolated"].truthy() ? s["base"].str() + (s["merged"].truthy() ? " \xC2\xB7 merged" : s["discarded"].truthy() ? " \xC2\xB7 discarded" : " \xC2\xB7 isolated") : s["project"].str();
                lo.items.push_back({Line{sg(s["id"].str() + "  ", MUTED), sg(one_line(s["title"].str()))}, s["id"].str(), "",
                                    Line{sg(st, st == "done" ? GREEN : st == "error" ? RED : st == "running" || st == "waiting" || st == "queued" ? AMBER : MUTED), sg("  " + s["files"].str("0") + (s["files"].num() == 1 ? " file" : " files") + " \xC2\xB7 " + where, FAINT)}});
            }
            for (size_t i = 0; i < lo.items.size(); ++i) if (lo.items[i].id == keep) lo.sel = static_cast<int>(i);
        });
    }
    void open_agents() {
        lo = ListOv{}; lo.on = true; lo.kind = "agents"; lo.title = "Agents"; lo.filterable = false; lo.empty = "No agents yet. Start some with /parallel \"task\" \"task\".";
        lo.hint = "enter open \xC2\xB7 d diff \xC2\xB7 m merge \xC2\xB7 x discard \xC2\xB7 s stop \xC2\xB7 esc close";
        lo.pick = [](App& a, const Item& it) { a.lo.on = false; a.resume(it.id); };
        lo.key = [](App& a, const Item& it, const Key& k) {
            if (k.k != K_TEXT || k.s.size() != 1) return false;
            const char c = k.s[0]; const std::string id = it.id;
            if (c == 'd') { a.show_diff(id); return true; }
            if (c == 'm') { a.merge(id); return true; }
            if (c == 'x') { a.async([id] { return api("/api/agent/discard", Object{{"id", id}}); }, [&a, id](const Value&, const std::string& err) { if (!err.empty()) a.say_toast(err, RED); else a.say_toast("Discarded " + id); a.refresh_agents(); }); return true; }
            if (c == 's') { a.async([id] { return api("/api/agent/stop", Object{{"id", id}}, 10); }, [&a, id](const Value&, const std::string& err) { a.say_toast(err.empty() ? "Stopping " + id : err, err.empty() ? 0 : RED); }); return true; }
            return false;
        };
        refresh_agents();
    }
    void show_diff(const std::string& id) {
        to = TextOv{}; to.on = true; to.title = "Changes \xC2\xB7 " + id; to.hint = "\xE2\x86\x91\xE2\x86\x93 pgup pgdn scroll \xC2\xB7 esc close"; to.lines.push_back(Line{sg("Loading\xE2\x80\xA6", MUTED)});
        async([id] { return api("/api/agent/diff", Object{{"id", id}}, 60); }, [this](const Value& r, const std::string& err) {
            if (!to.on) return;
            to.lines.clear();
            if (!err.empty()) { to.lines.push_back(Line{sg(err, RED)}); return; }
            const std::string diff = r["diff"].str();
            if (auto a = r["changes"].arr()) {
                to.lines.push_back(Line{sg(std::to_string(a->size()) + (a->size() == 1 ? " file changed" : " files changed"), 0, BOLD)});
                for (const auto& f : *a) { const std::string st = f["status"].str(); to.lines.push_back(Line{sg("  "), st == "added" ? sg("+ ", GREEN) : st == "deleted" ? sg("- ", RED) : sg("~ ", AMBER), sg(f["path"].str())}); }
                to.lines.push_back(Line{});
            }
            if (diff.empty()) { to.lines.push_back(Line{sg("No line diff available (the project is not a git repository). The files above changed.", MUTED)}); return; }
            for (const auto& l : split_lines(strip_ansi(diff))) {
                uint32_t c = 0; uint8_t a = 0;
                if (l.rfind("+++", 0) == 0 || l.rfind("---", 0) == 0 || l.rfind("diff ", 0) == 0 || l.rfind("index ", 0) == 0) { a = BOLD; c = MUTED; }
                else if (!l.empty() && l[0] == '+') c = GREEN; else if (!l.empty() && l[0] == '-') c = RED; else if (l.rfind("@@", 0) == 0) c = VIOLET; else if (l.rfind("  ", 0) == 0) c = MUTED;
                to.lines.push_back(Line{sg(l, c, a)});
            }
        });
    }
    void merge(const std::string& id) {
        say_toast("Merging " + id + "\xE2\x80\xA6");
        async([id] { return api("/api/agent/merge", Object{{"id", id}, {"discard", true}}, 120); }, [this, id](const Value& r, const std::string& err) {
            if (!err.empty()) { error_block("Merge " + id + ": " + err + "  (open /agents, press d to review)"); return; }
            const size_t n = r["merged"].arr() ? r["merged"].arr()->size() : 0;
            std::string t = "Merged " + std::to_string(n) + (n == 1 ? " file" : " files") + " from " + id + (r["checkpoint"].str().empty() ? "" : " \xC2\xB7 undo: astro undo " + id);
            if (auto a = r["merged"].arr()) for (const auto& f : *a) t += "\n  \xE2\x86\x92 " + f["path"].str();
            if (auto a = r["conflicts"].arr(); a && !a->empty()) { t += "\nNot merged (changed in the project meanwhile):"; for (const auto& f : *a) t += "\n  ! " + f["path"].str(); }
            note(t, GREEN);
            if (lo.on && lo.kind == "agents") refresh_agents();
        });
    }
    void open_question(const Value& e) {
        dg = Dialog{}; dg.on = true; dg.qid = e["id"].str();
        if (e["type"].str() == "approval") {
            dg.kind = "approval"; dg.title = "Allow this?";
            dg.body.push_back(Line{sg(e["text"].str(), 0, BOLD)});
            int n = 0;
            for (const auto& l : split_lines(e["preview"].str())) {
                if (++n > 400) break;
                uint32_t c = 0; if (!l.empty() && l[0] == '+') c = GREEN; else if (!l.empty() && l[0] == '-') c = RED; else if (l.rfind("@@", 0) == 0) c = VIOLET;
                dg.body.push_back(Line{sg("\xE2\x94\x82 ", FAINT), sg(l, c)});
            }
            dg.opts = {"Yes", "Yes, and don't ask again during this run", "No, and tell the agent what to do instead"};
            dg.done = [](App& a, int i) {
                const std::string q = a.dg.qid; a.dg.on = false;
                if (i == 0) a.answer(q, "Allow");
                else if (i == 1) a.answer(q, "Allow all in this run");
                else { a.answer_qid = q; a.in.clear(); a.cur = 0; }
            };
        } else {
            dg.kind = "question"; dg.title = "The agent asks";
            for (const auto& l : split_lines(e["question"].str())) dg.body.push_back(Line{sg(l, 0, BOLD)});
            if (auto a = e["options"].arr()) for (const auto& op : *a) dg.opts.push_back(op.str());
            dg.opts.push_back("Type an answer\xE2\x80\xA6");
            dg.done = [](App& a, int i) {
                const std::string q = a.dg.qid; a.dg.on = false;
                if (i + 1 < static_cast<int>(a.dg.opts.size())) a.answer(q, a.dg.opts[static_cast<size_t>(i)]);
                else { a.answer_qid = q; a.in.clear(); a.cur = 0; }
            };
        }
        dirty = true;
    }
    void confirm(const std::string& q, const std::string& yes, std::function<void(App&)> fn) {
        dg = Dialog{}; dg.on = true; dg.kind = "confirm"; dg.title = "Confirm"; dg.body.push_back(Line{sg(q, 0, BOLD)}); dg.opts = {yes, "Cancel"}; dg.sel = 1;
        dg.done = [fn](App& a, int i) { a.dg.on = false; if (i == 0) fn(a); };
    }
    void run_parallel(std::vector<std::string> tasks, bool merge_after) {
        if (tasks.empty()) { note("Give the tasks: /parallel \"add tests\" \"write the README\" [--merge]", AMBER); return; }
        auto g = std::make_shared<PGroup>(); g->merge = merge_after; g->t0 = now_ms();
        for (const auto& t : tasks) { PA a; a.task = t; g->ags.push_back(a); }
        groups.push_back(g);
        auto b = std::make_shared<Block>(); b->dynamic = true;
        b->plain = "Parallel: " + std::to_string(tasks.size()) + " agents";
        b->gen = [this, g](int w, std::vector<Line>& out) {
            out.push_back(Line{sg("\xE2\x97\x86 ", PEACH), sg(std::to_string(g->ags.size()) + (g->ags.size() == 1 ? " agent" : " agents"), 0, BOLD), sg(g->finished ? "  finished" : "  working in parallel, each in an isolated copy of " + project, MUTED)});
            int k = 0;
            for (const auto& a : g->ags) {
                const bool run = a.status == "running" || a.status == "waiting" || a.status == "queued" || a.status == "starting";
                const Seg icon = run ? sg(BRAILLE_SPIN[(now_ms() / 90 + k++) % 10], PEACH) : a.status == "done" ? sg("\xE2\x9C\x94", GREEN) : a.status == "error" ? sg("\xE2\x9C\x97", RED) : sg("\xE2\x96\xA0", AMBER);
                const std::string tail = a.status + " \xC2\xB7 " + std::to_string(a.steps) + (a.steps == 1 ? " step" : " steps") + (a.status == "done" ? " \xC2\xB7 " + std::to_string(a.files) + (a.files == 1 ? " file" : " files") : "") + (a.last.empty() || !run ? "" : " \xC2\xB7 " + a.last);
                out.push_back(fit(Line{sg("  "), icon, sg(" " + (a.id.empty() ? std::string("\xE2\x80\xA6") : a.id) + "  ", MUTED), sg(clip(a.task, 40), 0, BOLD), sg("  " + one_line(tail), FAINT)}, w, true));
                if (g->finished && !a.report.empty()) { int n = 0; for (const auto& l : split_lines(a.report)) { if (trim(l).empty()) continue; if (++n > 3) break; out.push_back(fit(Line{sg("      " + l, MUTED)}, w, true)); } }
                if (g->finished && !a.failing.empty()) out.push_back(fit(Line{sg("      ! the last check still fails: " + a.failing, AMBER)}, w, true));
            }
            for (const auto& l : g->result) wrap(l, w, Line{sg("  ")}, Line{sg("    ")}, out);
        };
        blocks.push_back(b);
        Opts oo = o; if (oo.mode == "ask") oo.mode = "auto";   // nobody approves inside parallel runs
        oo.isolate = true;
        const std::string proj = project;
        std::thread([this, g, tasks, oo, proj] {
            std::vector<PA> ags;
            for (const auto& t : tasks) {
                PA a; a.task = t;
                try { const Value r = api("/api/agent/start", start_body(oo, proj, t, false), 600); a.id = r["id"].str(); a.status = "queued"; }
                catch (const std::exception& e) { a.status = "error"; a.last = e.what(); a.report = std::string("Could not start: ") + e.what(); }
                ags.push_back(a);
            }
            auto snap = [&](bool fin, std::vector<Line> res = {}) { post([g, ags, fin, res] { g->ags = ags; if (fin) { g->finished = true; g->result = res; } }); };
            snap(false);
            std::map<std::string, long long> since_of;
            bool stop_sent = false;
            while (true) {
                if (g->stop && !stop_sent) { stop_sent = true; for (auto& a : ags) if (!a.id.empty()) try { api("/api/agent/stop", Object{{"id", a.id}}, 10); } catch (...) {} }
                bool live_any = false;
                for (auto& a : ags) {
                    if (a.id.empty()) continue;
                    Value j;
                    try { j = api("/api/agent/events", Object{{"id", a.id}, {"since", static_cast<double>(since_of[a.id])}, {"wait", 0}}, 30); } catch (...) { live_any = true; continue; }
                    if (auto ev = j["events"].arr()) for (const auto& e : *ev) {
                        since_of[a.id] = std::max(since_of[a.id], static_cast<long long>(e["seq"].num()));
                        const std::string t = e["type"].str();
                        if (t == "tool_done") a.last = (e["ok"].truthy() ? "\xE2\x9C\x93 " : "\xE2\x9C\x97 ") + e["label"].str();
                        else if (t == "say") a.last = one_line(e["text"].str());
                        else if (t == "done") { a.failing = e["failing"].str(); a.report = e["text"].str(); a.files = e["files"].arr() ? static_cast<int>(e["files"].arr()->size()) : 0; a.ms = e["ms"].num(); }
                        else if (t == "error") a.last = "error: " + e["text"].str();
                    }
                    a.status = j["status"].str(); a.steps = static_cast<int>(j["steps"].num());
                    if (j["running"].truthy() || a.status == "running" || a.status == "queued" || a.status == "waiting") live_any = true;
                }
                snap(false);
                if (!live_any) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
            }
            std::vector<Line> res;
            if (g->merge) {
                for (auto& a : ags) {
                    if (a.status != "done") { res.push_back(Line{sg("skipped " + a.id + " (" + a.status + ")", AMBER)}); continue; }
                    if (!a.failing.empty()) { res.push_back(Line{sg("skipped " + a.id + ": its last check failed. Review it in /agents.", AMBER)}); continue; }
                    if (a.files == 0) { try { api("/api/agent/discard", Object{{"id", a.id}}); } catch (...) {} res.push_back(Line{sg(a.id + ": no changes", MUTED)}); continue; }
                    try {
                        const Value r = api("/api/agent/merge", Object{{"id", a.id}, {"discard", true}}, 120);
                        const size_t n = r["merged"].arr() ? r["merged"].arr()->size() : 0;
                        res.push_back(Line{sg("\xE2\x9C\x94 merged " + a.id, GREEN), sg("  " + std::to_string(n) + (n == 1 ? " file" : " files"), MUTED)});
                        if (auto c = r["conflicts"].arr(); c && !c->empty()) for (const auto& f : *c) res.push_back(Line{sg("  ! not merged (changed meanwhile): " + f["path"].str(), AMBER)});
                    } catch (const std::exception& e) { res.push_back(Line{sg("\xE2\x9C\x97 " + a.id + ": " + e.what(), RED)}); }
                }
            } else {
                bool any = false;
                for (auto& a : ags) if (a.status == "done" && a.files > 0) any = true;
                if (any) res.push_back(Line{sg("Review and merge in /agents (d = diff, m = merge, x = discard), or /merge <id>.", MUTED)});
            }
            snap(true, res);
        }).detach();
    }
    void run_shell(const std::string& cmd) {
        auto r = std::make_shared<ShellRun>(); r->cmd = cmd; r->t0 = now_ms();
        auto b = std::make_shared<Block>(); b->dynamic = true; b->plain = "$ " + cmd;
        b->gen = [r](int w, std::vector<Line>& out) {
            out.push_back(fit(Line{sg("! ", AMBER, BOLD), sg(r->cmd, 0, BOLD)}, w, true));
            const size_t show = r->done ? 40 : 12;
            const size_t from = r->lines.size() > show ? r->lines.size() - show : 0;
            if (from + r->dropped > 0) out.push_back(Line{sg("  \xE2\x94\x82 ", FAINT), sg("\xE2\x80\xA6 " + std::to_string(from + r->dropped) + " earlier lines", FAINT)});
            for (size_t i = from; i < r->lines.size(); ++i) out.push_back(fit(Line{sg("  \xE2\x94\x82 ", FAINT), sg(r->lines[i], MUTED)}, w));
            if (r->done) out.push_back(Line{sg("  \xE2\x8E\xBF ", FAINT), sg("exit " + std::to_string(r->code), r->code == 0 ? GREEN : RED), sg(" \xC2\xB7 " + fmt_ms(static_cast<double>(r->ms)), FAINT)});
            else out.push_back(Line{sg("  \xE2\x8E\xBF ", FAINT), sg(std::string(BRAILLE_SPIN[(now_ms() / 90) % 10]) + " running", PEACH)});
        };
        blocks.push_back(b); scroll = 0;
        const std::string dir = to_utf8(root);
        std::thread([this, r, cmd, dir] {
#ifdef _WIN32
            const std::string full = "cd /d \"" + dir + "\" && (" + cmd + ") < NUL 2>&1";
            FILE* p = _popen(full.c_str(), "r");
#else
            std::string qd = "'"; for (char ch : dir) { if (ch == '\'') qd += "'\\''"; else qd += ch; } qd += "'";
            const std::string full = "cd " + qd + " && (" + cmd + ") < /dev/null 2>&1";
            FILE* p = popen(full.c_str(), "r");
#endif
            if (!p) { post([r] { r->done = true; r->code = -1; r->lines.push_back("could not start the shell"); }); return; }
            char buf[4096]; std::vector<std::string> batch; long long last = now_ms(); std::string part;
            auto flush = [&] { if (batch.empty()) return; post([r, batch] { for (const auto& l : batch) { r->lines.push_back(l); if (r->lines.size() > 4000) { r->lines.erase(r->lines.begin(), r->lines.begin() + 1000); r->dropped += 1000; } } }); batch.clear(); last = now_ms(); };
            while (std::fgets(buf, sizeof buf, p)) {
                part += buf;
                if (!part.empty() && part.back() == '\n') { part.pop_back(); batch.push_back(strip_ansi(part)); part.clear(); }
                if (batch.size() >= 50 || now_ms() - last > 100) flush();
            }
            if (!part.empty()) batch.push_back(strip_ansi(part));
            flush();
#ifdef _WIN32
            const int code = _pclose(p);
#else
            const int st = pclose(p); const int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
            const long long ms = now_ms() - r->t0;
            post([r, code, ms] { r->done = true; r->code = code; r->ms = ms; });
        }).detach();
    }
    void load_files() {
        if (files_loaded) return;
        files_loaded = true;
        static const std::set<std::string> skip = {".git", "node_modules", "dist", "build", "target", ".venv", "venv", "__pycache__", ".next", ".cache", "out", "coverage", ".idea", ".vscode", "vendor"};
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const std::string name = to_utf8(it->path().filename());
            if (it->is_directory(ec)) { if (skip.count(name) || (name.size() > 1 && name[0] == '.')) it.disable_recursion_pending(); continue; }
            files.push_back(to_utf8(fs::relative(it->path(), root, ec)));
            if (files.size() >= 20000) break;
        }
        for (auto& f : files) std::replace(f.begin(), f.end(), '\\', '/');
        std::sort(files.begin(), files.end());
    }
    void export_md(const std::string& arg) {
        std::string name = arg;
        if (name.empty()) { name = "astro-session-" + (sid.empty() ? std::to_string(std::time(nullptr)) : sid) + ".md"; }
        fs::path p = from_utf8(name); if (p.is_relative()) p = root / p;
        std::ofstream f(p, std::ios::binary);
        if (!f) { error_block("Cannot write " + to_utf8(p)); return; }
        f << "# A.S.T.R.O AI session" << (sid.empty() ? "" : " " + sid) << "\n\nProject: " << project << "\n\n";
        for (size_t i = 1; i < blocks.size(); ++i) if (!blocks[i]->plain.empty()) f << blocks[i]->plain << "\n\n";
        note("Saved " + to_utf8(p), GREEN);
    }
    void status_block() {
        std::string t = "Engine   " + g_url + (nmodels ? "  \xC2\xB7  " + std::to_string(nmodels) + " models" : "") +
                        "\nProject  " + project + "  (" + to_utf8(root) + ")" +
                        "\nSession  " + (sid.empty() ? std::string("none yet") : sid + "  \xC2\xB7  " + status + "  \xC2\xB7  " + std::to_string(steps) + (steps == 1 ? " step" : " steps")) +
                        "\nMode     " + o.mode + "    Model  " + model_label(o.model) + (model_used.empty() ? "" : "  (last used: " + model_used + ")") +
                        "\nReview   " + (o.review ? "on" : "off") + "    Version  astro " + VERSION;
        note(t);
    }

    // ---------------------------------------------------------------- commands
    void run_cmd(const std::string& text) {
        std::string cmd = text.substr(1), arg;
        if (const auto sp = cmd.find_first_of(" \n"); sp != std::string::npos) { arg = trim(cmd.substr(sp + 1)); cmd = cmd.substr(0, sp); }
        cmd = lower(cmd);
        if (cmd == "quit" || cmd == "exit" || cmd == "q") { quit = true; return; }
        if (cmd == "help" || cmd == "?") { open_help(); return; }
        if (cmd == "mode") { if (arg == "plan" || arg == "ask" || arg == "auto") set_mode(arg); else note("Mode: " + o.mode + "   (/mode plan | ask | auto, or Shift+Tab)"); return; }
        if (cmd == "model") { if (arg.empty()) open_models(); else set_model(arg); return; }
        if (cmd == "models") { open_models(); return; }
        if (cmd == "review") { o.review = arg != "off"; say_toast(std::string("Review ") + (o.review ? "on: a second model checks the work" : "off"), PEACH); return; }
        if (cmd == "new") { new_session(false); return; }
        if (cmd == "clear") { new_session(true); return; }
        if (cmd == "sessions" || (cmd == "resume" && arg.empty())) { open_sessions(); return; }
        if (cmd == "resume") { resume(arg); return; }
        if (cmd == "agents") { open_agents(); return; }
        if (cmd == "diff") { if (sid.empty() && arg.empty()) note("Nothing changed yet."); else show_diff(arg.empty() ? sid : arg); return; }
        if (cmd == "undo") {
            if (sid.empty()) { note("Nothing to undo yet."); return; }
            confirm("Restore the project to how it was before this session?", "Yes, restore it", [](App& a) {
                const std::string id = a.sid;
                a.async([id] { return api("/api/agent/undo", Object{{"id", id}}, 60); }, [&a](const Value& r, const std::string& err) { if (!err.empty()) a.error_block(err); else { a.note("\xE2\x9C\x94 Restored " + r["project"].str() + ".", GREEN); a.refresh_changes(); } });
            });
            return;
        }
        if (cmd == "stop") { stop_agent(); return; }
        if (cmd == "parallel") {
            bool m = false;
            if (const auto p = arg.find("--merge"); p != std::string::npos) { m = true; arg.erase(p, 7); }
            run_parallel(split_tasks(arg), m); return;
        }
        if (cmd == "merge") { if (arg.empty()) open_agents(); else merge(arg); return; }
        if (cmd == "discard") {
            if (arg.empty()) { open_agents(); return; }
            async([arg] { return api("/api/agent/discard", Object{{"id", arg}}); }, [this, arg](const Value&, const std::string& err) { if (!err.empty()) error_block(err); else note("Discarded " + arg + "."); });
            return;
        }
        if (cmd == "init") {
            send_prompt("Create or update AGENTS.md at the project root: a short guide for AI coding agents working in this project. Read the project first. Cover what it is, the layout, how to install, build, run and test it, the code style, and any gotchas. Keep it concise and accurate; do not invent commands.", "/init");
            return;
        }
        if (cmd == "export") { export_md(arg); return; }
        if (cmd == "copy") {
            if (final_text.empty()) { note("No answer to copy yet."); return; }
            twrite("\x1b]52;c;" + b64(final_text) + "\x07");
            say_toast("Copied the last answer (if your terminal allows clipboard access)", GREEN); return;
        }
        if (cmd == "panel") { if (W < 96) { say_toast("The side panel needs a window at least 96 columns wide", AMBER); return; } panel_auto = false; panel_on = !panel_visible(); return; }
        if (cmd == "mouse") {
            g_mouse = !g_mouse;
#ifdef _WIN32
            win_input_mode();
#endif
            twrite(mouse_seq());
            say_toast(g_mouse ? "Mouse wheel scrolls the conversation (hold Shift to select text)" : "Mouse off: select text freely; scroll with PgUp/PgDn", PEACH); return;
        }
        if (cmd == "status") { status_block(); return; }
        note("Unknown command /" + cmd + ". Type / to see them all.", AMBER);
    }
    void submit() {
        std::string text = trim(in);
        in.clear(); cur = 0; hidx = -1; pop_dismissed = false; in_top = 0;
        if (text.empty()) return;
        if (hist.empty() || hist.back() != text) hist.push_back(text);
        if (!answer_qid.empty()) { const std::string q = answer_qid; answer_qid.clear(); answer(q, text); return; }
        scroll = 0;
        if (text[0] == '/' && text.size() > 1 && text[1] != '/') { run_cmd(text); return; }
        if (text[0] == '!' && text.size() > 1) { run_shell(trim(text.substr(1))); return; }
        if (text == "?") { open_help(); return; }
        send_prompt(text);
    }
    void open_help() {
        to = TextOv{}; to.on = true; to.title = "A.S.T.R.O AI \xC2\xB7 help"; to.hint = "esc close";
        auto H2 = [&](const std::string& t) { to.lines.push_back(Line{}); to.lines.push_back(Line{sg(t, PEACH, BOLD)}); };
        auto row = [&](const std::string& k, const std::string& d) { const int kw = swidth(k); to.lines.push_back(Line{sg("  " + k + std::string(static_cast<size_t>(kw < 22 ? 22 - kw : 1), ' '), 0, BOLD), sg(d, MUTED)}); };
        to.lines.push_back(Line{sg("Type a task and press Enter. The agent reads, edits and runs things in this folder; you stay in control.", MUTED)});
        H2("Keys");
        row("Enter", "send");
        row("Alt+Enter, Ctrl+J, \\", "new line (Shift+Enter where the terminal supports it)");
        row("Shift+Tab", "cycle mode: auto \xE2\x86\x92 plan \xE2\x86\x92 ask");
        row("Esc", "stop the agent \xC2\xB7 close a menu \xC2\xB7 twice clears the prompt");
        row("Ctrl+C", "clear the prompt \xC2\xB7 stop \xC2\xB7 twice quits");
        row("Ctrl+D", "quit (empty prompt)");
        row("Ctrl+B", "side panel");
        row("Ctrl+O", "choose the model");
        row("Ctrl+R", "resume a session");
        row("Ctrl+G", "agents");
        row("PgUp / PgDn", "scroll (Home / End with an empty prompt)");
        row("\xE2\x86\x91 / \xE2\x86\x93", "prompt history");
        row("Ctrl+A E U K W", "line editing, like a shell");
        row("Ctrl+L", "redraw");
        H2("Prompt");
        row("/", "commands");
        row("@path", "mention a file (Tab completes)");
        row("!command", "run a shell command here (non-interactive)");
        H2("Commands");
        for (const auto& c : CMDS) row(std::string("/") + c.name + (c.args[0] ? std::string(" ") + c.args : ""), c.desc);
        H2("Modes");
        row("auto", "edits and commands run on their own; /undo restores the project");
        row("plan", "read-only: the agent looks around and proposes a plan");
        row("ask", "you approve every edit and command");
    }

    // ---------------------------------------------------------------- input editing
    size_t prev_cp(size_t i) const { if (i == 0) return 0; --i; while (i > 0 && (static_cast<unsigned char>(in[i]) & 0xC0) == 0x80) --i; return i; }
    size_t next_cp(size_t i) const { if (i >= in.size()) return in.size(); ++i; while (i < in.size() && (static_cast<unsigned char>(in[i]) & 0xC0) == 0x80) ++i; return i; }
    size_t word_left(size_t i) const { while (i > 0 && std::isspace(static_cast<unsigned char>(in[i - 1]))) --i; while (i > 0 && !std::isspace(static_cast<unsigned char>(in[i - 1]))) i = prev_cp(i); return i; }
    size_t word_right(size_t i) const { while (i < in.size() && std::isspace(static_cast<unsigned char>(in[i]))) ++i; while (i < in.size() && !std::isspace(static_cast<unsigned char>(in[i]))) i = next_cp(i); return i; }
    size_t line_start(size_t i) const { while (i > 0 && in[i - 1] != '\n') --i; return i; }
    size_t line_end(size_t i) const { while (i < in.size() && in[i] != '\n') ++i; return i; }
    void insert(const std::string& s) { in.insert(cur, s); cur += s.size(); pop_dismissed = false; pop_sel = 0; hidx = -1; }

    // popup: slash commands or @file
    struct Pop { std::string kind; std::vector<std::pair<std::string, std::string>> items; size_t from = 0; };   // items: (insert, description)
    Pop popup() {
        Pop p;
        if (pop_dismissed || dg.on || lo.on || to.on) return p;
        if (!in.empty() && in[0] == '/' && in.find_first_of(" \n") == std::string::npos && answer_qid.empty()) {
            const std::string typed = lower(in.substr(1));
            for (const auto& c : CMDS) if (std::string(c.name).rfind(typed, 0) == 0) p.items.push_back({c.name, c.desc});
            for (const auto& c : CMDS) if (std::string(c.name).rfind(typed, 0) != 0 && std::string(c.name).find(typed) != std::string::npos) p.items.push_back({c.name, c.desc});
            if (!p.items.empty()) p.kind = "cmd";
            return p;
        }
        size_t s = cur;
        while (s > 0 && !std::isspace(static_cast<unsigned char>(in[s - 1]))) --s;
        if (s < in.size() && in[s] == '@' && cur > s) {
            load_files();
            const std::string q = ascii_lower(in.substr(s + 1, cur - s - 1));
            std::vector<std::pair<int, std::string>> scored;
            for (const auto& f : files) {
                const std::string lf = ascii_lower(f);
                const auto pos = lf.find(q);
                if (pos == std::string::npos) continue;
                const auto slash = lf.rfind('/');
                const int score = (pos == (slash == std::string::npos ? 0 : slash + 1) ? 0 : 1000) + static_cast<int>(f.size());
                scored.push_back({score, f});
                if (scored.size() > 4000) break;
            }
            std::sort(scored.begin(), scored.end());
            for (size_t i = 0; i < scored.size() && i < 50; ++i) p.items.push_back({scored[i].second, ""});
            if (!p.items.empty()) { p.kind = "file"; p.from = s; }
        }
        return p;
    }
    void accept_popup(const Pop& p, bool run) {
        if (p.items.empty()) return;
        const auto& it = p.items[static_cast<size_t>(std::clamp(pop_sel, 0, static_cast<int>(p.items.size()) - 1))];
        if (p.kind == "cmd") {
            const Cmd* c = nullptr; for (const auto& x : CMDS) if (it.first == x.name) c = &x;
            const bool needs = c && (c->args[0] == '<' || c->args[0] == '"' || std::string(c->args).find('|') != std::string::npos);
            if (run && !needs) { in = "/" + it.first; cur = in.size(); submit(); return; }
            in = "/" + it.first + " "; cur = in.size(); pop_sel = 0; return;
        }
        const std::string ins = "@" + it.first + " ";
        in.replace(p.from, cur - p.from, ins); cur = p.from + ins.size(); pop_sel = 0;
    }

    // ---------------------------------------------------------------- keys
    void key(const Key& k) {
        dirty = true;
        if (k.k == K_RESIZE) { full = true; return; }
        if (k.k == K_WUP || k.k == K_WDN) {
            if (to.on) { to.top = std::max(0, to.top + (k.k == K_WUP ? -3 : 3)); return; }
            if (lo.on) { lo.sel = std::clamp(lo.sel + (k.k == K_WUP ? -1 : 1), 0, std::max(0, static_cast<int>(lo.items.size()) - 1)); return; }
            scroll = std::max(0, scroll + (k.k == K_WUP ? 3 : -3)); return;
        }
        if (k.k == K_CTRL && k.c == 'l') { full = true; return; }
#ifndef _WIN32
        if (k.k == K_CTRL && k.c == 'z') { term_leave(); raise(SIGTSTP); term_enter(); full = true; return; }
#endif
        if (to.on) {
            const int page = std::max(1, H - 6);
            if (k.k == K_ESC || (k.k == K_TEXT && (k.s == "q" || k.s == "?")) || (k.k == K_CTRL && (k.c == 'c' || k.c == 'd'))) to.on = false;
            else if (k.k == K_UP || (k.k == K_TEXT && k.s == "k")) to.top = std::max(0, to.top - 1);
            else if (k.k == K_DOWN || (k.k == K_TEXT && k.s == "j") || k.k == K_ENTER) ++to.top;
            else if (k.k == K_PGUP) to.top = std::max(0, to.top - page);
            else if (k.k == K_PGDN || (k.k == K_TEXT && k.s == " ")) to.top += page;
            else if (k.k == K_HOME) to.top = 0;
            else if (k.k == K_END) to.top = 1 << 28;
            return;
        }
        if (lo.on) {
            std::vector<int> vis = visible_items();
            const int n = static_cast<int>(vis.size());
            int pos = 0; for (int i = 0; i < n; ++i) if (vis[static_cast<size_t>(i)] == lo.sel) pos = i;
            if (k.k == K_ESC || (k.k == K_CTRL && (k.c == 'c' || k.c == 'd'))) { lo.on = false; return; }
            if (n && lo.key && lo.key(*this, lo.items[static_cast<size_t>(vis[static_cast<size_t>(pos)])], k)) return;
            if (k.k == K_UP && n) lo.sel = vis[static_cast<size_t>((pos - 1 + n) % n)];
            else if ((k.k == K_DOWN || k.k == K_TAB) && n) lo.sel = vis[static_cast<size_t>((pos + 1) % n)];
            else if (k.k == K_PGUP && n) lo.sel = vis[static_cast<size_t>(std::max(0, pos - 10))];
            else if (k.k == K_PGDN && n) lo.sel = vis[static_cast<size_t>(std::min(n - 1, pos + 10))];
            else if (k.k == K_ENTER && n) { const Item it = lo.items[static_cast<size_t>(vis[static_cast<size_t>(pos)])]; if (lo.pick) lo.pick(*this, it); }
            else if (lo.filterable && (k.k == K_TEXT || k.k == K_PASTE)) { lo.filter += k.s; auto v = visible_items(); if (!v.empty()) lo.sel = v[0]; }
            else if (lo.filterable && k.k == K_BS && !lo.filter.empty()) { lo.filter.erase(prev_in(lo.filter, lo.filter.size())); auto v = visible_items(); if (!v.empty()) lo.sel = v[0]; }
            return;
        }
        if (dg.on) {
            const int n = static_cast<int>(dg.opts.size());
            if (k.k == K_UP) dg.sel = (dg.sel - 1 + n) % n;
            else if (k.k == K_DOWN || k.k == K_TAB) dg.sel = (dg.sel + 1) % n;
            else if (k.k == K_ENTER) { auto f = dg.done; if (f) f(*this, dg.sel); }
            else if (k.k == K_TEXT && k.s.size() == 1 && k.s[0] >= '1' && k.s[0] < '1' + n) { auto f = dg.done; if (f) f(*this, k.s[0] - '1'); }
            else if (k.k == K_TEXT && dg.kind != "question" && (k.s == "y" || k.s == "Y")) { auto f = dg.done; if (f) f(*this, 0); }
            else if (k.k == K_TEXT && dg.kind == "approval" && (k.s == "a" || k.s == "A")) { auto f = dg.done; if (f) f(*this, 1); }
            else if (k.k == K_TEXT && dg.kind == "approval" && (k.s == "n" || k.s == "N")) { auto f = dg.done; if (f) f(*this, 2); }
            else if (k.k == K_ESC) { if (dg.kind == "confirm") dg.on = false; else { auto f = dg.done; if (f) f(*this, n - 1); } }
            else if (k.k == K_CTRL && k.c == 'c') { if (dg.kind == "confirm") dg.on = false; else { const std::string q = dg.qid; dg.on = false; answer(q, "Deny"); stop_agent(); } }
            else if (k.k == K_PGUP) dg_scroll = std::max(0, dg_scroll - 5);
            else if (k.k == K_PGDN) dg_scroll += 5;
            return;
        }
        // global shortcuts
        if (k.k == K_CTRL) {
            switch (k.c) {
                case 'c':
                    if (!in.empty()) { in.clear(); cur = 0; return; }
                    if (!answer_qid.empty()) { const std::string q = answer_qid; answer_qid.clear(); answer(q, "Deny"); return; }
                    if (running) { stop_agent(); return; }
                    if (now_ms() - last_ctrl_c < 1500) { quit = true; return; }
                    last_ctrl_c = now_ms(); say_toast("Press Ctrl+C again to exit"); return;
                case 'd': if (in.empty()) quit = true; else if (cur < in.size()) in.erase(cur, next_cp(cur) - cur); return;
                case 'b': if (W < 96) { say_toast("The side panel needs a window at least 96 columns wide", AMBER); return; } panel_auto = false; panel_on = !panel_visible(); return;
                case 'o': open_models(); return;
                case 'r': open_sessions(); return;
                case 'g': open_agents(); return;
                case 'a': cur = line_start(cur); return;
                case 'e': cur = line_end(cur); return;
                case 'u': { const size_t s = line_start(cur); in.erase(s, cur - s); cur = s; return; }
                case 'k': { const size_t e = line_end(cur); in.erase(cur, (e == cur && e < in.size()) ? 1 : e - cur); return; }
                case 'w': { const size_t s = word_left(cur); in.erase(s, cur - s); cur = s; return; }
                case 'p': history(-1); return;
                case 'n': history(1); return;
                case 'f': cur = next_cp(cur); return;
                default: return;
            }
        }
        if (k.k == K_BTAB) { set_mode(o.mode == "auto" ? "plan" : o.mode == "plan" ? "ask" : "auto"); return; }
        if (k.k == K_PGUP) { scroll += std::max(1, last_th - 2); return; }
        if (k.k == K_PGDN) { scroll = std::max(0, scroll - std::max(1, last_th - 2)); return; }
        const Pop p = popup();
        const int np = static_cast<int>(p.items.size());
        if (np) {
            if (k.k == K_UP) { pop_sel = (pop_sel - 1 + np) % np; return; }
            if (k.k == K_DOWN) { pop_sel = (pop_sel + 1) % np; return; }
            if (k.k == K_TAB) { accept_popup(p, false); return; }
            if (k.k == K_ENTER) { const bool exact = p.kind == "cmd" && std::any_of(std::begin(CMDS), std::end(CMDS), [&](const Cmd& c) { return in == std::string("/") + c.name; }); if (exact && pop_sel == 0) { submit(); return; } accept_popup(p, true); return; }
            if (k.k == K_ESC) { pop_dismissed = true; return; }
        }
        if (k.k == K_ESC) {
            if (!answer_qid.empty()) { const std::string q = answer_qid; answer_qid.clear(); answer(q, "Deny"); in.clear(); cur = 0; return; }
            if (running) { stop_agent(); return; }
            if (scroll > 0) { scroll = 0; return; }
            if (!in.empty()) { if (now_ms() - last_esc < 800) { in.clear(); cur = 0; } else { last_esc = now_ms(); say_toast("Esc again to clear the prompt"); } }
            return;
        }
        switch (k.k) {
            case K_TEXT:
                if (k.s == "?" && in.empty()) { open_help(); return; }
                insert(k.s); return;
            case K_PASTE: { std::string s = k.s; std::string t; for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\r') { t += '\n'; if (i + 1 < s.size() && s[i + 1] == '\n') ++i; } else t += s[i]; } insert(t); return; }
            case K_NL: insert("\n"); return;
            case K_TAB: insert("  "); return;
            case K_ENTER:
                if (!in.empty() && cur == in.size() && in.back() == '\\') { in.back() = '\n'; return; }
                submit(); return;
            case K_BS: if (cur > 0) { const size_t pc = prev_cp(cur); in.erase(pc, cur - pc); cur = pc; pop_dismissed = false; } return;
            case K_WBS: { const size_t s = word_left(cur); in.erase(s, cur - s); cur = s; return; }
            case K_DEL: if (cur < in.size()) in.erase(cur, next_cp(cur) - cur); return;
            case K_LEFT: cur = prev_cp(cur); return;
            case K_RIGHT: cur = next_cp(cur); return;
            case K_WLEFT: cur = word_left(cur); return;
            case K_WRIGHT: cur = word_right(cur); return;
            case K_HOME: if (in.empty()) scroll = 1 << 28; else cur = line_start(cur); return;
            case K_END: if (in.empty()) scroll = 0; else cur = line_end(cur); return;
            case K_UP: case K_DOWN: {
                std::vector<VRow> rows; int cr = 0, cc = 0; layout_input(input_width(), rows, cr, cc);
                const bool up = k.k == K_UP;
                if ((up && cr == 0) || (!up && cr + 1 >= static_cast<int>(rows.size()))) { history(up ? -1 : 1); return; }
                const VRow& t = rows[static_cast<size_t>(cr + (up ? -1 : 1))];
                size_t i = t.a; int col = 0;
                while (i < t.b) { size_t j = i; const int w = std::max(0, cpw(dec(in, j))); if (col + w > cc) break; col += w; i = j; }
                cur = i; return;
            }
            default: return;
        }
    }
    static size_t prev_in(const std::string& s, size_t i) { if (i == 0) return 0; --i; while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i; return i; }
    void history(int d) {
        if (hist.empty()) return;
        if (hidx == -1) { if (d > 0) return; hsave = in; hidx = static_cast<int>(hist.size()); }
        hidx += d;
        if (hidx < 0) hidx = 0;
        if (hidx >= static_cast<int>(hist.size())) { hidx = -1; in = hsave; cur = in.size(); return; }
        in = hist[static_cast<size_t>(hidx)]; cur = in.size(); pop_dismissed = true;
    }
    std::vector<int> visible_items() const {
        std::vector<int> v;
        const std::string f = ascii_lower(lo.filter);
        for (size_t i = 0; i < lo.items.size(); ++i) {
            if (!f.empty()) {
                const std::string hay = lo.items[i].search.empty() ? ascii_lower(lo.items[i].id) : lo.items[i].search;
                bool ok = true; size_t pos = 0;
                std::stringstream ss(f); std::string word;
                while (ss >> word) { if (hay.find(word, 0) == std::string::npos) { ok = false; break; } }
                (void)pos;
                if (!ok) continue;
            }
            v.push_back(static_cast<int>(i));
        }
        return v;
    }

    // ---------------------------------------------------------------- layout & drawing
    struct VRow { size_t a, b; };
    int main_width() const { return panel_visible() ? W - panel_width() - 1 : W; }
    int input_width() const { return std::max(8, main_width() - 6); }
    bool panel_visible() const { if (W < 96) return false; return panel_auto ? W >= 140 : panel_on; }
    int panel_width() const { return std::clamp(W * 3 / 10, 30, 46); }
    void layout_input(int iw, std::vector<VRow>& rows, int& cr, int& cc) const {
        rows.clear(); size_t i = 0, a = 0; int col = 0; cr = 0; cc = 0;
        while (true) {
            int w = 0;
            if (i < in.size() && in[i] != '\n') { size_t j = i; w = std::max(0, cpw(dec(in, j))); if (col + w > iw && col > 0) { rows.push_back({a, i}); a = i; col = 0; } }
            if (i == cur) { cr = static_cast<int>(rows.size()); cc = col; }
            if (i >= in.size()) { rows.push_back({a, i}); break; }
            if (in[i] == '\n') { rows.push_back({a, i}); ++i; a = i; col = 0; continue; }
            size_t j = i; dec(in, j); col += w; i = j;
        }
        if (cur == in.size() && cc >= iw) { rows.push_back({in.size(), in.size()}); cr = static_cast<int>(rows.size()) - 1; cc = 0; }
    }
    int dg_scroll = 0;
    Line box_row(const Line& content, int w, uint32_t bc) {   // "│ content │"
        return cat(cat(Line{sg("\xE2\x94\x82 ", bc)}, pad(fit(content, w - 4, true), w - 4)), Line{sg(" \xE2\x94\x82", bc)});
    }
    Line box_top(int w, uint32_t bc, const std::string& title = "", uint32_t tc = 0) {
        std::string h = "\xE2\x95\xAD";
        if (title.empty()) { for (int i = 0; i < w - 2; ++i) h += "\xE2\x94\x80"; return Line{sg(h + "\xE2\x95\xAE", bc)}; }
        Line l{sg("\xE2\x95\xAD\xE2\x94\x80 ", bc), sg(title, tc ? tc : bc, BOLD), sg(" ", bc)};
        std::string rest; for (int i = lwidth(l); i < w - 1; ++i) rest += "\xE2\x94\x80";
        l.push_back(sg(rest + "\xE2\x95\xAE", bc));
        return fit(l, w);
    }
    Line box_bottom(int w, uint32_t bc, const std::string& hint = "") {
        if (hint.empty()) { std::string h = "\xE2\x95\xB0"; for (int i = 0; i < w - 2; ++i) h += "\xE2\x94\x80"; return Line{sg(h + "\xE2\x95\xAF", bc)}; }
        Line l{sg("\xE2\x95\xB0\xE2\x94\x80 ", bc), sg(hint, MUTED), sg(" ", bc)};
        std::string rest; for (int i = lwidth(l); i < w - 1; ++i) rest += "\xE2\x94\x80";
        l.push_back(sg(rest + "\xE2\x95\xAF", bc));
        return fit(l, w);
    }
    std::vector<Line> welcome(int w) {
        std::vector<Line> out;
        const int bw = std::min(w, 84);
        const bool logo = bw >= 60;
        std::vector<Line> art = logo ? logo_art() : std::vector<Line>{};
        std::vector<Line> txt;
        txt.push_back(Line{sg("Welcome to "), sg("A.S.T.R.O AI", PEACH, BOLD), sg("  v" + std::string(VERSION), FAINT)});
        txt.push_back(Line{sg("All Systems Thinking, Reasoning as One", MUTED, ITAL)});
        txt.push_back(Line{});
        txt.push_back(Line{sg("/help", 0, BOLD), sg(" commands  \xC2\xB7  ", MUTED), sg("/model", 0, BOLD), sg(" models  \xC2\xB7  ", MUTED), sg("?", 0, BOLD), sg(" shortcuts", MUTED)});
        txt.push_back(Line{});
        txt.push_back(Line{sg("cwd     ", FAINT), sg(to_utf8(root), MUTED)});
        txt.push_back(Line{sg("engine  ", FAINT), sg(g_url + (nmodels ? "  \xC2\xB7  " + std::to_string(nmodels) + " models" : models_loading ? "  \xC2\xB7  loading models\xE2\x80\xA6" : ""), MUTED)});
        txt.push_back(Line{sg("best    ", FAINT), sg(best_model.empty() ? "picked per task, with automatic fallback" : best_model + ", with automatic fallback", MUTED)});
        out.push_back(box_top(bw, PEACH_D));
        const int rows = std::max(static_cast<int>(art.size()), static_cast<int>(txt.size()));
        const int tw = bw - 4 - (logo ? LOGO_N + 3 : 0);
        for (int r = 0; r < rows; ++r) {
            const int ti = r - (rows - static_cast<int>(txt.size())) / 2;
            Line row;
            if (logo) { row = r < static_cast<int>(art.size()) ? art[static_cast<size_t>(r)] : Line{sg(std::string(LOGO_N, ' '))}; row.push_back(sg("   ")); }
            Line t = ti >= 0 && ti < static_cast<int>(txt.size()) ? txt[static_cast<size_t>(ti)] : Line{};
            row = cat(row, fit(t, tw, true));
            out.push_back(box_row(row, bw, PEACH_D));
        }
        out.push_back(box_bottom(bw, PEACH_D));
        out.push_back(Line{});
        out.push_back(fit(Line{sg("  Tip: ", FAINT), sg(tip(), MUTED)}, w, true));
        return out;
    }
    std::string tip() const {
        static const char* tips[] = {
            "Shift+Tab switches between auto, plan and ask mode.",
            "/parallel \"add tests\" \"write docs\" runs agents side by side in isolated copies.",
            "Type @ to mention a file, ! to run a shell command.",
            "/undo restores the project to before the session.",
            "Ctrl+B shows the side panel with changes, to-do and agents.",
            "astro -p \"task\" runs one task from scripts and CI.",
        };
        return tips[(std::time(nullptr) / 60) % 6];
    }
    std::vector<Line> transcript(int w) {
        std::vector<Line> out;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (i > 0) for (int g = 0; g < blocks[i]->gap; ++g) out.push_back(Line{});
            const auto& ls = blocks[i]->lines(w);
            out.insert(out.end(), ls.begin(), ls.end());
        }
        if (running) {
            out.push_back(Line{});
            const long long el = now_ms() - (run_t0 ? run_t0 : now_ms());
            std::string act = stopping ? "Stopping" : status == "queued" ? "Waiting for a free agent slot" : !waiting.empty() ? "Waiting for you" : !live.empty() ? "Writing" : !last_tool.empty() ? last_tool : "Thinking";
            act = one_line(act);
            out.push_back(fit(Line{sg(std::string(SPIN[(now_ms() / 120) % 10]) + " ", PEACH, BOLD), sg(act + "\xE2\x80\xA6", PEACH),
                                   sg("  (" + mmss(static_cast<double>(el)) + (steps ? " \xC2\xB7 step " + std::to_string(steps) : "") + " \xC2\xB7 esc to interrupt)", FAINT)}, w, true));
            if (!live.empty()) {
                auto ls = split_lines(live);
                std::vector<std::string> keep;
                for (auto it = ls.rbegin(); it != ls.rend() && keep.size() < 3; ++it) if (!trim(*it).empty()) keep.insert(keep.begin(), *it);
                for (const auto& l : keep) out.push_back(fit(Line{sg("  \xE2\x8E\xBF ", FAINT), sg(l, MUTED, ITAL)}, w, true));
            }
        }
        return out;
    }
    std::vector<Line> bottom(int mw, int& crow, int& ccol, bool& show_cursor) {
        std::vector<Line> out;
        show_cursor = false;
        if (dg.on) {
            const uint32_t bc = dg.kind == "confirm" ? PEACH : AMBER;
            out.push_back(box_top(mw, bc, dg.title, bc));
            const int maxb = std::max(3, H / 2 - static_cast<int>(dg.opts.size()) - 4);
            std::vector<Line> body;
            for (const auto& l : dg.body) wrap(l, mw - 4, Line{}, Line{sg("  ")}, body);
            dg_scroll = std::clamp(dg_scroll, 0, std::max(0, static_cast<int>(body.size()) - maxb));
            for (int i = dg_scroll; i < static_cast<int>(body.size()) && i < dg_scroll + maxb; ++i) out.push_back(box_row(body[static_cast<size_t>(i)], mw, bc));
            if (static_cast<int>(body.size()) > maxb) out.push_back(box_row(Line{sg("\xE2\x80\xA6 " + std::to_string(body.size()) + " lines \xC2\xB7 PgUp/PgDn to scroll", FAINT)}, mw, bc));
            out.push_back(box_row(Line{}, mw, bc));
            for (size_t i = 0; i < dg.opts.size(); ++i) {
                const bool s = static_cast<int>(i) == dg.sel;
                out.push_back(box_row(Line{sg(s ? "\xE2\x9D\xAF " : "  ", PEACH, BOLD), sg(std::to_string(i + 1) + ". ", s ? PEACH : MUTED), sg(dg.opts[i], s ? PEACH : 0, s ? BOLD : 0)}, mw, bc));
            }
            out.push_back(box_bottom(mw, bc, dg.kind == "approval" ? "enter choose \xC2\xB7 y / a / n \xC2\xB7 esc tell it what to do instead" : "enter choose \xC2\xB7 esc cancel"));
            return out;
        }
        const Pop p = popup();
        if (!p.items.empty()) {
            const int np = static_cast<int>(p.items.size());
            pop_sel = std::clamp(pop_sel, 0, np - 1);
            const int show = std::min(np, 8);
            const int first = std::clamp(pop_sel - show + 1, 0, np - show);
            for (int i = first; i < first + show; ++i) {
                const auto& it = p.items[static_cast<size_t>(i)];
                const bool s = i == pop_sel;
                Line l;
                if (p.kind == "cmd") {
                    const Cmd* c = nullptr; for (const auto& x : CMDS) if (it.first == x.name) c = &x;
                    std::string nm = "/" + it.first + (c && c->args[0] ? std::string(" ") + c->args : "");
                    if (swidth(nm) < 30) nm += std::string(static_cast<size_t>(30 - swidth(nm)), ' ');
                    l = Line{sg("  "), sg(nm, s ? PEACH : 0, s ? BOLD : 0), sg("  " + it.second, s ? 0 : MUTED)};
                } else l = Line{sg("  "), sg("@", FAINT), sg(it.first, s ? PEACH : 0, s ? BOLD : 0)};
                Line f = pad(fit(l, mw, true), mw);
                if (s) for (auto& sgm : f) sgm.s.bg = SELBG;
                out.push_back(f);
            }
            if (np > show) out.push_back(Line{sg("  " + std::to_string(np) + " matches \xC2\xB7 \xE2\x86\x91\xE2\x86\x93 choose \xC2\xB7 tab complete", FAINT)});
        }
        const bool bash = !in.empty() && in[0] == '!';
        const bool answering = !answer_qid.empty();
        const uint32_t bc = answering ? AMBER : bash ? AMBER : BORDER;
        out.push_back(box_top(mw, bc, answering ? "Tell the agent what to do instead" : "", AMBER));
        const int iw = mw - 6;
        std::vector<VRow> rows; int cr = 0, cc = 0; layout_input(iw, rows, cr, cc);
        const int maxr = std::clamp(H / 3, 1, 10);
        if (cr < in_top) in_top = cr;
        if (cr >= in_top + maxr) in_top = cr - maxr + 1;
        in_top = std::clamp(in_top, 0, std::max(0, static_cast<int>(rows.size()) - 1));
        const int base = static_cast<int>(out.size());
        for (int r = in_top; r < static_cast<int>(rows.size()) && r < in_top + maxr; ++r) {
            const Seg pre = r == 0 ? (bash ? sg("! ", AMBER, BOLD) : sg("\xE2\x80\xBA ", answering ? AMBER : PEACH, BOLD)) : sg("  ");
            Line content;
            if (in.empty()) {
                std::string ph = answering ? "Type what the agent should do (Enter sends, Esc denies)" : running ? "Send a follow-up, or Esc to interrupt" : "Ask anything, or describe a task  (/ commands  @ files  ! shell)";
                content = Line{sg(ph, FAINT)};
            } else {
                std::string seg = in.substr(rows[static_cast<size_t>(r)].a, rows[static_cast<size_t>(r)].b - rows[static_cast<size_t>(r)].a);
                if (r == 0 && bash && !seg.empty()) seg = seg.substr(1);
                content = Line{sg(seg)};
            }
            out.push_back(cat(cat(Line{sg("\xE2\x94\x82 ", bc), pre}, pad(fit(content, iw, true), iw)), Line{sg(" \xE2\x94\x82", bc)}));
        }
        out.push_back(box_bottom(mw, bc));
        crow = base + (cr - in_top); ccol = 4 + cc - ((cr == 0 && bash && cc > 0) ? 1 : 0);
        show_cursor = true;
        // status line
        Line left;
        if (o.mode == "auto") left = Line{sg("  \xE2\x8F\xB5\xE2\x8F\xB5 auto mode", GREEN)};
        else if (o.mode == "plan") left = Line{sg("  \xE2\x8F\xB8 plan mode", BLUE)};
        else left = Line{sg("  \xE2\x97\x86 ask mode", PEACH)};
        left.push_back(sg(" (shift+tab to cycle)", FAINT));
        if (in.empty() && !running) left.push_back(sg("  \xC2\xB7  ? for shortcuts", FAINT));
        Line right;
        if (!toast.empty() && now_ms() - toast_t < 3500) right = Line{sg(toast + "  ", toast_c)};
        else if (scroll > 0) right = Line{sg("\xE2\x86\x91 scrolled \xC2\xB7 End or Esc to follow  ", AMBER)};
        else {
            right = Line{sg(model_label(o.model), MUTED)};
            if (o.review) right.push_back(sg(" \xC2\xB7 review", MUTED));
            if (!panel_visible()) right.push_back(sg(" \xC2\xB7 " + project, FAINT));
            right.push_back(sg("  "));
        }
        out.push_back(lr(left, right, mw));
        return out;
    }
    std::vector<Line> side_panel(int pw, int h) {
        std::vector<Line> out;
        const int w = pw - 2;
        auto head = [&](const std::string& t) { out.push_back(Line{}); out.push_back(Line{sg(t, FAINT, BOLD)}); };
        auto add_l = [&](Line l) { out.push_back(fit(l, w, true)); };
        add_l(Line{sg("\xE2\x9C\xBB ", PEACH, BOLD), sg("A.S.T.R.O AI", PEACH, BOLD)});
        add_l(Line{sg("terminal \xC2\xB7 v" + std::string(VERSION), FAINT)});
        head("PROJECT");
        add_l(Line{sg(project, 0, BOLD)});
        add_l(Line{sg(to_utf8(root), MUTED)});
        head("SESSION");
        if (sid.empty()) add_l(Line{sg("No session yet. Type a task.", MUTED)});
        else {
            std::vector<Line> t; wrap(Line{sg(title.empty() ? "(untitled)" : title)}, w, Line{}, Line{}, t);
            for (size_t i = 0; i < t.size() && i < 2; ++i) add_l(t[i]);
            const Seg dot = running ? sg(std::string(BRAILLE_SPIN[(now_ms() / 90) % 10]) + " ", PEACH) : status == "done" ? sg("\xE2\x9C\x94 ", GREEN) : status == "error" ? sg("\xE2\x9C\x97 ", RED) : sg("\xE2\x97\x8B ", MUTED);
            add_l(Line{dot, sg(running ? (waiting.empty() ? "working" : "waiting for you") : status, running ? PEACH : MUTED), sg(steps ? " \xC2\xB7 step " + std::to_string(steps) : "", FAINT), sg(running && run_t0 ? " \xC2\xB7 " + mmss(static_cast<double>(now_ms() - run_t0)) : "", FAINT)});
            add_l(Line{sg(sid, FAINT)});
        }
        head("MODE \xC2\xB7 MODEL");
        add_l(Line{sg(o.mode, o.mode == "auto" ? GREEN : o.mode == "plan" ? BLUE : PEACH, BOLD), sg("  \xC2\xB7  " + model_label(o.model), MUTED)});
        if (!model_used.empty()) add_l(Line{sg("last answer: " + model_used, FAINT)});
        head("CHANGES" + (changes.empty() ? std::string() : " (" + std::to_string(changes.size()) + ")"));
        if (changes.empty()) add_l(Line{sg("none yet", FAINT)});
        for (size_t i = 0; i < changes.size() && i < 12; ++i) { const auto& c = changes[i]; add_l(Line{c.second == "added" ? sg("+ ", GREEN) : c.second == "deleted" ? sg("- ", RED) : sg("~ ", AMBER), sg(c.first)}); }
        if (changes.size() > 12) add_l(Line{sg("\xE2\x80\xA6 " + std::to_string(changes.size() - 12) + " more \xC2\xB7 /diff", FAINT)});
        if (!todo.empty()) {
            head("TO-DO");
            for (const auto& [st, tx] : todo) add_l(Line{st == "done" ? sg("\xE2\x98\x91 ", GREEN) : st == "doing" ? sg("\xE2\x97\x90 ", AMBER) : sg("\xE2\x98\x90 ", FAINT), sg(tx, st == "done" ? MUTED : 0)});
        }
        bool any_group = false;
        for (const auto& g : groups) if (!g->ags.empty()) any_group = true;
        if (any_group) {
            head("AGENTS");
            for (const auto& g : groups) for (const auto& a : g->ags) {
                const bool run = a.status == "running" || a.status == "waiting" || a.status == "queued" || a.status == "starting";
                add_l(Line{run ? sg(std::string(BRAILLE_SPIN[(now_ms() / 90) % 10]) + " ", PEACH) : a.status == "done" ? sg("\xE2\x9C\x94 ", GREEN) : sg("\xE2\x9C\x97 ", RED), sg(a.id.empty() ? "\xE2\x80\xA6" : a.id, MUTED), sg("  " + a.task)});
            }
        }
        while (static_cast<int>(out.size()) < h - 2) out.push_back(Line{});
        if (static_cast<int>(out.size()) > h - 2) out.resize(static_cast<size_t>(std::max(0, h - 2)));
        out.push_back(Line{sg("ctrl+b hide \xC2\xB7 ? help", FAINT)});
        out.push_back(Line{});
        return out;
    }
    void overlay_into(std::vector<Line>& scr, int x, int y, const std::vector<Line>& box) {
        for (size_t r = 0; r < box.size(); ++r) {
            const int row = y + static_cast<int>(r);
            if (row < 0 || row >= static_cast<int>(scr.size())) continue;
            std::vector<Cell> base; cells_of(scr[static_cast<size_t>(row)], base);
            while (static_cast<int>(base.size()) < W) base.push_back({" ", St{}, 1});
            std::vector<Cell> top; cells_of(box[r], top);
            // expand to column positions
            std::vector<Cell> cols; for (const auto& c : base) { cols.push_back(c); if (c.w == 2) cols.push_back({"", c.s, 0}); }
            int col = x;
            for (const auto& c : top) {
                if (col + c.w > W) break;
                if (col >= 0) {
                    if (col > 0 && cols[static_cast<size_t>(col)].w == 0) cols[static_cast<size_t>(col - 1)] = {" ", cols[static_cast<size_t>(col - 1)].s, 1};
                    if (cols[static_cast<size_t>(col)].w == 2 && col + 1 < static_cast<int>(cols.size())) cols[static_cast<size_t>(col + 1)] = {" ", St{}, 1};
                    cols[static_cast<size_t>(col)] = c;
                    if (c.w == 2 && col + 1 < static_cast<int>(cols.size())) cols[static_cast<size_t>(col + 1)] = {"", c.s, 0};
                }
                col += c.w;
            }
            if (col < static_cast<int>(cols.size()) && cols[static_cast<size_t>(col)].w == 0) cols[static_cast<size_t>(col)] = {" ", St{}, 1};
            std::vector<Cell> fixed; for (const auto& c : cols) if (c.w > 0) fixed.push_back(c);
            scr[static_cast<size_t>(row)] = join_cells(fixed, 0, fixed.size());
        }
    }
    std::vector<Line> list_box(int bw, int bh) {
        std::vector<Line> b;
        b.push_back(box_top(bw, PEACH, lo.title, PEACH));
        int inner = bh - 2;
        if (lo.filterable) { b.push_back(box_row(Line{sg("\xE2\x8C\x95 ", PEACH), lo.filter.empty() ? sg("type to filter", FAINT) : sg(lo.filter, 0, BOLD)}, bw, PEACH)); b.push_back(box_row(Line{}, bw, PEACH)); inner -= 2; }
        const std::vector<int> vis = visible_items();
        if (lo.loading) b.push_back(box_row(Line{sg(std::string(BRAILLE_SPIN[(now_ms() / 90) % 10]) + " loading\xE2\x80\xA6", PEACH)}, bw, PEACH));
        else if (vis.empty()) b.push_back(box_row(Line{sg(lo.filter.empty() ? lo.empty : "No matches.", MUTED)}, bw, PEACH));
        else {
            int pos = 0; for (size_t i = 0; i < vis.size(); ++i) if (vis[i] == lo.sel) pos = static_cast<int>(i);
            if (pos < lo.top) lo.top = pos;
            if (pos >= lo.top + inner) lo.top = pos - inner + 1;
            lo.top = std::clamp(lo.top, 0, std::max(0, static_cast<int>(vis.size()) - inner));
            for (int i = lo.top; i < static_cast<int>(vis.size()) && i < lo.top + inner; ++i) {
                const Item& it = lo.items[static_cast<size_t>(vis[static_cast<size_t>(i)])];
                const bool s = vis[static_cast<size_t>(i)] == lo.sel;
                Line left = cat(Line{sg(s ? "\xE2\x9D\xAF " : "  ", PEACH, BOLD)}, it.label);
                if (s) for (auto& sg2 : left) { if (!sg2.s.fg) sg2.s.fg = PEACH; sg2.s.a |= BOLD; }
                Line row = lr(left, it.right, bw - 4);
                if (s) for (auto& sg2 : row) sg2.s.bg = SELBG;
                b.push_back(box_row(pad(row, bw - 4), bw, PEACH));
            }
        }
        while (static_cast<int>(b.size()) < bh - 1) b.push_back(box_row(Line{}, bw, PEACH));
        b.push_back(box_bottom(bw, PEACH, lo.hint));
        return b;
    }
    std::vector<Line> text_box(int bw, int bh) {
        std::vector<Line> b;
        b.push_back(box_top(bw, PEACH, to.title, PEACH));
        std::vector<Line> body; for (const auto& l : to.lines) wrap(l, bw - 4, Line{}, Line{sg("  ")}, body);
        const int inner = bh - 2;
        to.top = std::clamp(to.top, 0, std::max(0, static_cast<int>(body.size()) - inner));
        for (int i = to.top; i < static_cast<int>(body.size()) && i < to.top + inner; ++i) b.push_back(box_row(body[static_cast<size_t>(i)], bw, PEACH));
        while (static_cast<int>(b.size()) < bh - 1) b.push_back(box_row(Line{}, bw, PEACH));
        std::string hint = to.hint;
        if (static_cast<int>(body.size()) > inner) hint += "  \xC2\xB7  " + std::to_string(std::min(100, (to.top + inner) * 100 / std::max(1, static_cast<int>(body.size())))) + "%";
        b.push_back(box_bottom(bw, PEACH, hint));
        return b;
    }
    void render() {
        int w, h; term_size(w, h);
        if (w != W || h != H) { W = w; H = h; full = true; for (auto& b : blocks) b->cw = -1; }
        if (W < 30 || H < 10) { twrite("\x1b[H\x1b[2J\x1b[0mWindow too small for A.S.T.R.O AI."); prev.clear(); full = true; return; }
        const bool pv = panel_visible();
        const int PW = pv ? panel_width() : 0, MW = pv ? W - PW - 1 : W;
        int crow = 0, ccol = 0; bool showc = false;
        std::vector<Line> bot = bottom(MW, crow, ccol, showc);
        const int th = std::max(1, H - static_cast<int>(bot.size()));
        last_th = th;
        // the transcript, with the welcome card as the first block
        std::vector<Line> tl = transcript(MW - 2);
        const int total = static_cast<int>(tl.size());
        const int maxs = std::max(0, total - th);
        scroll = std::clamp(scroll, 0, maxs);
        const int start = std::max(0, total - th - scroll);
        std::vector<Line> mainrows;
        for (int r = 0; r < th && start + r < total; ++r) mainrows.push_back(cat(Line{sg(" ")}, tl[static_cast<size_t>(start + r)]));
        welcome_visible = start < 14;
        const int bot_at = static_cast<int>(mainrows.size());   // the prompt follows the conversation, like a normal terminal
        for (const auto& l : bot) mainrows.push_back(l);
        crow += bot_at;
        while (static_cast<int>(mainrows.size()) < H) mainrows.push_back(Line{});
        std::vector<Line> scr(static_cast<size_t>(H));
        std::vector<Line> side = pv ? side_panel(PW, H) : std::vector<Line>{};
        for (int r = 0; r < H; ++r) {
            Line l = pad(fit(mainrows[static_cast<size_t>(r)], MW), MW);
            if (pv) { l.push_back(sg("\xE2\x94\x82", BORDER)); l.push_back(sg(" ")); l = cat(l, fit(side[static_cast<size_t>(r)], PW - 1, true)); }
            scr[static_cast<size_t>(r)] = l;
        }
        if (lo.on) { const int bw = std::min(W - 4, 96), bh = std::min(H - 4, 26); overlay_into(scr, (W - bw) / 2, std::max(1, (H - bh) / 2), list_box(bw, bh)); showc = false; }
        if (to.on) { const int bw = std::min(W - 4, 110), bh = H - 2; overlay_into(scr, (W - bw) / 2, 1, text_box(bw, bh)); showc = false; }
        // emit only the rows that changed
        std::string o2 = "\x1b[?2026h\x1b[?25l";
        if (full) { o2 += "\x1b[0m\x1b[2J"; prev.assign(static_cast<size_t>(H), std::string("\x01")); full = false; }
        if (static_cast<int>(prev.size()) != H) prev.assign(static_cast<size_t>(H), std::string("\x01"));
        for (int r = 0; r < H; ++r) {
            std::string s; St cs; bool first = true; int used = 0;
            for (const auto& sgm : fit(scr[static_cast<size_t>(r)], W)) { if (sgm.t.empty()) continue; if (first || !(sgm.s == cs)) { s += sgr(sgm.s); cs = sgm.s; first = false; } s += sgm.t; used += swidth(sgm.t); }
            s += "\x1b[0m"; if (used < W) s += "\x1b[K";
            if (s != prev[static_cast<size_t>(r)]) { o2 += "\x1b[" + std::to_string(r + 1) + ";1H" + s; prev[static_cast<size_t>(r)] = s; }
        }
        if (showc && crow < H) o2 += "\x1b[" + std::to_string(crow + 1) + ";" + std::to_string(std::min(W, ccol + 1)) + "H\x1b[?25h";
        o2 += "\x1b[?2026l";
        twrite(o2);
        dirty = false; last_frame = now_ms();
    }
};

static bool wanted(const Opts& o) {
    if (o.classic || env("ASTRO_TUI") == "0" || env("TERM") == "dumb") return false;
#ifndef _WIN32
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#else
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#endif
}

static int run(Opts o, const std::string& project) {
    detect_colors();
    App& a = *new App();   // lives until exit: background threads post into it
    a.o = o; a.project = project;
    std::error_code ec;
    a.root = o.dir.empty() ? fs::current_path(ec) : fs::weakly_canonical(fs::absolute(from_utf8(o.dir), ec), ec);
    auto wb = std::make_shared<Block>(); wb->dynamic = false; wb->gap = 0;
    wb->gen = [&a](int w, std::vector<Line>& out) { auto ls = a.welcome(w); out.insert(out.end(), ls.begin(), ls.end()); };
    a.blocks.push_back(wb);
    term_enter();
    twrite("\x1b]0;A.S.T.R.O AI \xC2\xB7 " + project + "\x07");
    a.load_models();
    if (!o.resume.empty()) a.resume(o.resume);
    else if (o.cont) {
        a.async([project] { return api("/api/agent/list", Object{{"project", project}}, 20); }, [&a](const Value& r, const std::string& err) {
            if (!err.empty()) { a.error_block(err); return; }
            const Array* s = r["sessions"].arr();
            if (!s || s->empty()) { a.note("No earlier session in this project; starting fresh."); return; }
            const Value* best = nullptr;
            for (const auto& x : *s) if (!x["isolated"].truthy() && (!best || x["updated"].num() > (*best)["updated"].num())) best = &x;
            if (best) a.resume((*best)["id"].str()); else a.note("No earlier session in this project; starting fresh.");
        });
    }
    if (!o.prompt.empty()) a.send_prompt(o.prompt);
    long long last_anim = 0;
    while (!a.quit) {
        a.drain();
        std::vector<Key> keys;
        read_keys(a.dirty ? 0 : 40, keys);
        for (const auto& k : keys) { a.key(k); if (a.quit) break; }
        a.drain();
        if (a.lo.on && a.lo.kind == "agents" && now_ms() - a.last_agents > 2000) a.refresh_agents();
        bool anim = a.running || (!a.toast.empty() && now_ms() - a.toast_t < 3600) || a.lo.loading;
        for (const auto& g : a.groups) if (!g->finished) anim = true;
        for (const auto& b : a.blocks) if (b->dynamic && b != a.blocks[0]) anim = true;
        if (a.dirty || (anim && now_ms() - last_anim >= 100)) { a.render(); last_anim = now_ms(); }
    }
    ++a.gen;
    term_leave();
    if (!a.sid.empty()) std::printf("%s\n", (dim("A.S.T.R.O AI \xC2\xB7 session ") + a.sid + dim("  \xC2\xB7  continue it with: ") + "astro --resume " + a.sid).c_str());
    std::fflush(stdout);
    std::_Exit(0);   // detached pollers may still be waiting on the network
}

}  // namespace tui
