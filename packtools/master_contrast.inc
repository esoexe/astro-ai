// master-build/contrast - WCAG contrast for text/background pairs, from literal colors or your CSS custom properties.
// C++ port of master-skills/skills/build/tools/contrast.py (Jake Schincariol, MIT); same output, same exit codes:
// 0 every pair passes, 1 at least one pair fails, 2 bad input. When a pair fails it suggests the nearest passing
// color with the same hue (lightness moved in OKLCH).
namespace packtools::master_contrast {
using namespace pyrt;
using namespace packtools::master;

inline const char* VERSION = "1.0.0";
inline double threshold(const std::string& m) { return m == "text" ? 4.5 : m == "aaa" ? 7.0 : 3.0; }   // large, ui: 3
inline const std::vector<std::pair<std::string, std::string>> NAMED = {
    {"black", "#000000"}, {"white", "#ffffff"}, {"gray", "#808080"}, {"grey", "#808080"},
    {"silver", "#c0c0c0"}, {"red", "#ff0000"}, {"maroon", "#800000"}, {"yellow", "#ffff00"},
    {"olive", "#808000"}, {"lime", "#00ff00"}, {"green", "#008000"}, {"aqua", "#00ffff"},
    {"cyan", "#00ffff"}, {"teal", "#008080"}, {"blue", "#0000ff"}, {"navy", "#000080"},
    {"fuchsia", "#ff00ff"}, {"magenta", "#ff00ff"}, {"purple", "#800080"}, {"orange", "#ffa500"},
    {"pink", "#ffc0cb"}, {"brown", "#a52a2a"}, {"gold", "#ffd700"}, {"indigo", "#4b0082"},
    {"transparent", "#00000000"},
};
struct ColorError : PyErr { explicit ColorError(const std::string& m) : PyErr("ColorError", m) {} };
using RGBA = std::array<double, 4>;
using RGB = std::array<double, 3>;

inline double clamp(double v) { const double m = v < 1.0 ? v : 1.0; return m > 0.0 ? m : 0.0; }   // max(0.0, min(1.0, v)), NaN -> 1.0
inline double degrees(double x) { return x * (180.0 / 3.141592653589793); }
inline double radians(double x) { return x * (3.141592653589793 / 180.0); }

// -------------------------------------------------------------------------- parsing
inline double num(const std::string& token0, double scale = 1.0) {
    const std::string token = strip(token0);
    if (ends(token, "%")) return py_float(token.substr(0, token.size() - 1)) / 100.0 * scale;
    return py_float(token);
}
inline double hue_of(const std::string& token) {
    const std::string t = lower(strip(token));
    auto cut = [&](std::size_t n) { return t.substr(0, t.size() - n); };
    if (ends(t, "deg")) return py_float(cut(3));
    if (ends(t, "turn")) return py_float(cut(4)) * 360.0;
    if (ends(t, "rad")) return degrees(py_float(cut(3)));
    if (ends(t, "grad")) return py_float(cut(4)) * 0.9;
    return py_float(t);
}
struct Channels { std::vector<std::string> parts; std::optional<double> alpha; };
inline Channels split_args(std::string inner) {   // 'a b c / d' or 'a, b, c, d' -> ([a, b, c], alpha or None)
    Channels o;
    const std::size_t k = inner.find('/');
    if (k != std::string::npos) { o.alpha = num(inner.substr(k + 1)); inner = inner.substr(0, k); }
    for (const auto& p : re::split(R"([\s,]+)", strip(inner)))
        if (!p.empty()) o.parts.push_back(p);
    if (!o.alpha && o.parts.size() == 4) { o.alpha = num(o.parts.back()); o.parts.pop_back(); }
    return o;
}
inline double hls_v(double m1, double m2, double hue) {   // colorsys._v
    hue = py_mod(hue, 1.0);
    if (hue < 1.0 / 6.0) return m1 + (m2 - m1) * hue * 6.0;
    if (hue < 0.5) return m2;
    if (hue < 2.0 / 3.0) return m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0;
    return m1;
}
inline RGB hls_to_rgb(double h, double l, double s) {   // colorsys.hls_to_rgb
    if (s == 0.0) return {l, l, l};
    const double m2 = l <= 0.5 ? l * (1.0 + s) : l + s - (l * s);
    const double m1 = 2.0 * l - m2;
    return {hls_v(m1, m2, h + 1.0 / 3.0), hls_v(m1, m2, h), hls_v(m1, m2, h - 1.0 / 3.0)};
}
inline RGB oklch_to_srgb(double light, double chroma, double hue);

// (r, g, b, a) with channels in 0..1; throws ColorError
inline RGBA parse_color(const std::string& text) {
    std::string s = lower(strip(text));
    for (const auto& [n, h] : NAMED)
        if (n == s) { s = h; break; }
    try {
        if (starts(s, "#")) {
            std::string h = cpslice(s, 1);
            if (cplen(h) == 3 || cplen(h) == 4) { std::string d; for (const auto& c : cps(h)) d += c + c; h = d; }
            if ((cplen(h) != 6 && cplen(h) != 8) || !re::fullmatch("[0-9a-f]+", h)) throw ColorError("bad hex color: " + text);
            std::vector<double> v;
            for (std::size_t i = 0; i < h.size(); i += 2) v.push_back(std::stoi(h.substr(i, 2), nullptr, 16) / 255.0);
            return v.size() == 4 ? RGBA{v[0], v[1], v[2], v[3]} : RGBA{v[0], v[1], v[2], 1.0};
        }
        const re::Match m = re::fullmatch(R"((rgba?|hsla?|oklch)\((.*)\))", s);
        if (!m) throw ColorError("unsupported color: " + text);
        const std::string fn = m.group(1);
        const Channels c = split_args(m.group(2));
        if (c.parts.size() != 3) throw ColorError("expected 3 channels in " + text);
        const double a = c.alpha ? *c.alpha : 1.0;
        const auto& p = c.parts;
        if (starts(fn, "rgb")) {
            const double r = num(p[0], 255.0) / 255.0;
            const double g = num(p[1], 255.0) / 255.0;
            const double b = num(p[2], 255.0) / 255.0;
            return {clamp(r), clamp(g), clamp(b), clamp(a)};
        }
        if (starts(fn, "hsl")) {
            const double hue = py_mod(hue_of(p[0]), 360.0);
            double sat = num(p[1]);
            double light = num(p[2]);
            if (!ends(p[1], "%")) sat /= 100.0;
            if (!ends(p[2], "%")) light /= 100.0;
            const RGB rgb = hls_to_rgb(hue / 360.0, clamp(light), clamp(sat));
            return {rgb[0], rgb[1], rgb[2], clamp(a)};
        }
        // oklch(L C H): L as 0..1 or %, C as number (or % of 0.4), H in degrees.
        const double light = num(p[0]);
        const double chroma = num(p[1], 0.4);
        const double hue = p[2] == "none" ? 0.0 : hue_of(p[2]);
        const RGB rgb = oklch_to_srgb(light, chroma, hue);
        return {rgb[0], rgb[1], rgb[2], clamp(a)};
    } catch (const ColorError&) {
        throw;
    } catch (const PyErr& e) {
        if (e.type != "ValueError" && e.type != "IndexError") throw;
        throw ColorError("could not read color " + str_repr(text) + " (" + e.what() + ")");
    }
}

// -------------------------------------------------------------------------- color math (WCAG 2.x, OKLab/OKLCH)
inline double to_linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
inline double to_gamma(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
inline double luminance(const RGB& rgb) {
    const double r = to_linear(rgb[0]), g = to_linear(rgb[1]), b = to_linear(rgb[2]);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}
inline RGB composite(const RGBA& top, const RGBA& under) {
    const double a = top[3];
    return {top[0] * a + under[0] * (1 - a), top[1] * a + under[1] * (1 - a), top[2] * a + under[2] * (1 - a)};
}
inline RGBA opaque(const RGB& c) { return {c[0], c[1], c[2], 1.0}; }
// WCAG contrast ratio. Alpha in fg is composited over bg; bg alpha over white.
inline double ratio(const RGBA& fg, const RGBA& bg) {
    const RGB bg_rgb = composite(bg, {1.0, 1.0, 1.0, 1.0});
    const RGB fg_rgb = composite(fg, opaque(bg_rgb));
    const double l1 = luminance(fg_rgb), l2 = luminance(bg_rgb);
    const double hi = l2 > l1 ? l2 : l1, lo = l2 < l1 ? l2 : l1;
    return (hi + 0.05) / (lo + 0.05);
}
inline RGB srgb_to_oklch(const RGB& rgb) {
    const double r = to_linear(rgb[0]), g = to_linear(rgb[1]), b = to_linear(rgb[2]);
    double l_ = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    double m_ = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    double s_ = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;
    l_ = std::copysign(std::pow(std::fabs(l_), 1.0 / 3), l_);
    m_ = std::copysign(std::pow(std::fabs(m_), 1.0 / 3), m_);
    s_ = std::copysign(std::pow(std::fabs(s_), 1.0 / 3), s_);
    const double light = 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_;
    const double a = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_;
    const double bb = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_;
    return {light, std::hypot(a, bb), py_mod(degrees(std::atan2(bb, a)), 360.0)};
}
inline RGB oklch_to_linear(double light, double chroma, double hue) {
    const double a = chroma * std::cos(radians(hue));
    const double b = chroma * std::sin(radians(hue));
    const double l_ = std::pow(light + 0.3963377774 * a + 0.2158037573 * b, 3.0);
    const double m_ = std::pow(light - 0.1055613458 * a - 0.0638541728 * b, 3.0);
    const double s_ = std::pow(light - 0.0894841775 * a - 1.2914855480 * b, 3.0);
    return {4.0767416621 * l_ - 3.3077115913 * m_ + 0.2309699292 * s_,
            -1.2684380046 * l_ + 2.6097574011 * m_ - 0.3413193965 * s_,
            -0.0041960863 * l_ - 0.7034186147 * m_ + 1.7076147010 * s_};
}
inline bool in_gamut(const RGB& lin) {
    for (double c : lin)
        if (!(-1e-6 <= c && c <= 1 + 1e-6)) return false;
    return true;
}
inline RGB gamma_clamped(const RGB& lin) { return {clamp(to_gamma(clamp(lin[0]))), clamp(to_gamma(clamp(lin[1]))), clamp(to_gamma(clamp(lin[2])))}; }
// OKLCH to sRGB 0..1, reducing chroma until the color fits the sRGB gamut.
inline RGB oklch_to_srgb(double light, double chroma, double hue) {
    light = clamp(light);
    double lo = 0.0, hi = chroma > 0.0 ? chroma : 0.0;
    RGB lin = oklch_to_linear(light, hi, hue);
    if (in_gamut(lin)) return gamma_clamped(lin);
    for (int k = 0; k < 24; ++k) {
        const double mid = (lo + hi) / 2;
        lin = oklch_to_linear(light, mid, hue);
        if (in_gamut(lin)) lo = mid;
        else hi = mid;
    }
    return gamma_clamped(oklch_to_linear(light, lo, hue));
}
inline std::string to_hex(const RGB& rgb) {
    char b[16];
    auto ch = [](double c) { return static_cast<int>(std::nearbyint(clamp(c) * 255)); };
    std::snprintf(b, sizeof b, "#%02x%02x%02x", ch(rgb[0]), ch(rgb[1]), ch(rgb[2]));
    return b;
}
// Nearest color with fg's hue and chroma whose contrast against bg meets target: OKLCH lightness moves away from
// the background. Returns (hex, ratio) or nothing.
inline std::optional<std::pair<std::string, double>> suggest(const RGBA& fg, const RGBA& bg, double target) {
    const RGB bg_rgb = composite(bg, {1.0, 1.0, 1.0, 1.0});
    const RGB lch = srgb_to_oklch(composite(fg, opaque(bg_rgb)));
    const double light = lch[0], chroma = lch[1], hue = lch[2];
    const double bg_light = srgb_to_oklch(bg_rgb)[0];
    const int dirs[2] = {bg_light >= 0.5 ? -1 : 1, bg_light >= 0.5 ? 1 : -1};
    for (const int direction : dirs) {
        const double end = direction < 0 ? 0.0 : 1.0;
        if (ratio(opaque(oklch_to_srgb(end, chroma, hue)), opaque(bg_rgb)) < target) continue;
        double lo = light, hi = end;   // lo fails (or is the start), hi passes
        for (int k = 0; k < 30; ++k) {
            const double mid = (lo + hi) / 2;
            if (ratio(opaque(oklch_to_srgb(mid, chroma, hue)), opaque(bg_rgb)) >= target) hi = mid;
            else lo = mid;
        }
        RGB rgb = oklch_to_srgb(hi, chroma, hue);
        for (int k = 0; k < 8; ++k) {   // round to hex and nudge one more step if rounding fell just short
            const double r = ratio(parse_color(to_hex(rgb)), opaque(bg_rgb));
            if (r >= target) return std::make_pair(to_hex(rgb), r);
            hi = hi + direction * 0.005;
            rgb = oklch_to_srgb(clamp(hi), chroma, hue);
        }
    }
    return std::nullopt;
}

// -------------------------------------------------------------------------- CSS custom properties
inline const char* VAR_DEF = R"((--[A-Za-z0-9_-]+)\s*:\s*([^;{}]+))";
inline std::vector<std::string> dark_blocks(const std::string& css) {
    std::vector<std::string> out;
    const U32 u = to_u32(css);
    const std::string pat = R"(@media[^{]*prefers-color-scheme\s*:\s*dark[^{]*\{)";
    const auto& rx = re::compile(pat, re::I);
    for (const auto& m : rx.finditer(u)) {
        std::size_t depth = 1, i = static_cast<std::size_t>(m.end());
        while (i < u.size() && depth) {
            if (u[i] == '{') ++depth;
            else if (u[i] == '}') --depth;
            ++i;
        }
        out.push_back(from_u32(u, static_cast<std::size_t>(m.end()), i - 1));
    }
    return out;
}
// Custom properties from CSS. First definition wins; dark applies the prefers-color-scheme: dark overrides (and
// [data-theme=dark] blocks) on top.
inline Dict read_vars(const std::string& css0, bool dark) {
    const std::string css = re::sub(R"(/\*.*?\*/)", "", css0, 0, re::S);
    Dict found;
    const std::string vd = VAR_DEF;
    const auto& rx = re::compile(vd);
    for (const auto& m : rx.finditer(css))
        if (!found.has(m.group(1))) found.set(m.group(1), Val::str(strip(m.group(2))));
    if (dark) {
        std::vector<std::string> overrides = dark_blocks(css);
        const std::string tp = R"(\[data-theme=["']?dark["']?\][^{]*\{([^}]*)\})";
        const auto& theme = re::compile(tp, re::I);
        for (const auto& m : theme.finditer(css)) overrides.push_back(m.group(1));
        for (const auto& block : overrides)
            for (const auto& m : rx.finditer(block)) found.set(m.group(1), Val::str(strip(m.group(2))));
    }
    return found;
}
inline std::string resolve_token(const std::string& token, const Dict& vars, int depth = 0) {
    const std::string t = strip(token);
    if (depth > 10) throw ColorError("var() loop at " + token);
    if (const re::Match m = re::fullmatch(R"(var\(\s*(--[A-Za-z0-9_-]+)\s*(?:,\s*(.+))?\))", t)) {
        const std::string name = m.group(1);
        if (const Val* v = vars.get(name)) return resolve_token(v->s, vars, depth + 1);
        if (m.has(2) && !m.group(2).empty()) return resolve_token(m.group(2), vars, depth + 1);
        throw ColorError("undefined variable " + name);
    }
    if (!vars.items.empty() && (starts(t, "--") || vars.has("--" + t))) {
        const std::string name = starts(t, "--") ? t : "--" + t;
        const Val* v = vars.get(name);
        if (!v) throw ColorError("undefined variable " + name);
        return resolve_token(v->s, vars, depth + 1);
    }
    return t;
}
inline std::pair<std::string, std::string> split_pair(const std::string& text) {
    const std::string st = strip(text);
    const bool fn = starts(st, "rgb") || starts(st, "hsl") || starts(st, "oklch");
    for (const char* sep : {" on ", ":", "/"}) {
        const std::size_t k = text.find(sep);
        if (k != std::string::npos && !fn) return {strip(text.substr(0, k)), strip(text.substr(k + std::strlen(sep)))};
    }
    throw ColorError("pair must look like fg:bg, fg/bg or 'fg on bg': " + text);
}

// -------------------------------------------------------------------------- CLI
struct PairIn { std::string label_fg, fg, label_bg, bg; };
inline List check(const std::vector<PairIn>& pairs, const std::string& minimum) {
    const double target = threshold(minimum);
    List rows;
    for (const auto& pr : pairs) {
        const RGBA fg = parse_color(pr.fg);
        const RGBA bg = parse_color(pr.bg);
        const double r = ratio(fg, bg);
        Val row = Val::dict();
        row.d->set("fg", Val::str(pr.label_fg)); row.d->set("bg", Val::str(pr.label_bg));
        row.d->set("fg_color", Val::str(pr.fg)); row.d->set("bg_color", Val::str(pr.bg));
        row.d->set("ratio", Val::flt(round_n(r, 2)));
        row.d->set("aa_text", Val::boolean(r >= 4.5)); row.d->set("aa_large", Val::boolean(r >= 3.0));
        row.d->set("aaa_text", Val::boolean(r >= 7.0)); row.d->set("pass", Val::boolean(r >= target));
        row.d->set("required", Val::flt(target));
        if (!(r >= target))
            if (const auto s = suggest(fg, bg, target)) {
                Val sg = Val::dict(); sg.d->set("color", Val::str(s->first)); sg.d->set("ratio", Val::flt(round_n(s->second, 2)));
                row.d->set("suggest", sg);
            }
        rows.push_back(row);
    }
    return rows;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("contrast", "Check WCAG contrast for color pairs, from literal colors or your CSS tokens.");
    p.epilog = "Examples: contrast \"#777\" white | contrast --css styles.css --pair ink:paper";
    p.pos("colors").nargs("*").help("pairs of colors: FG BG [FG BG ...]");
    p.opt({"--css"}).metavar("FILE").help("read --custom-properties from this CSS file");
    p.opt({"--pair"}).append().def(Val::list()).metavar("FG:BG").help("pair of token names or colors, e.g. ink:paper or '--muted on --bg' (repeatable)");
    p.opt({"--dark"}).store_true().help("use the dark theme overrides from --css");
    p.opt({"--min"}).choices({"aaa", "large", "text", "ui"}).def("text").help("required level: text 4.5 (default), large 3, ui 3, aaa 7");
    p.opt({"--json"}).store_true().help("print JSON");
    p.opt({"--version"}).version(std::string("contrast ") + VERSION);
    const Val a = parse_args(p, argv);
    std::vector<std::string> colors; for (const auto& c : iterate(get(a, "colors"))) colors.push_back(c.s);
    if (colors.size() % 2) { eprint("contrast: colors must come in FG BG pairs"); return 2; }
    Dict vars;
    const std::string css = S(a, "css");
    if (!css.empty()) {
        try {
            vars = read_vars(read_text(css), B(a, "dark"));
        } catch (const OSError& e) {
            eprint(std::string("contrast: ") + e.what()); return 2;
        }
    }
    const std::string minimum = S(a, "min");
    List rows;
    try {
        std::vector<PairIn> pairs;
        for (std::size_t i = 0; i < colors.size(); i += 2) {
            const std::string fg = colors[i], bg = colors[i + 1];
            const std::string rf = resolve_token(fg, vars);
            pairs.push_back({fg, rf, bg, resolve_token(bg, vars)});
        }
        for (const auto& item : iterate(get(a, "pair"))) {
            const auto [fg, bg] = split_pair(item.s);
            const std::string rf = resolve_token(fg, vars);
            pairs.push_back({fg, rf, bg, resolve_token(bg, vars)});
        }
        if (pairs.empty()) {
            const std::string u = ap::usage(ap::norm(p, p.prog));
            flush(); std::fwrite(u.data(), 1, u.size(), stderr);
            eprint("contrast: give FG BG colors or --pair (with --css for token names)");
            return 2;
        }
        rows = check(pairs, minimum);
    } catch (const ColorError& e) {
        eprint(std::string("contrast: ") + e.what()); return 2;
    }
    bool failed_any = false; long long failed = 0;
    for (const auto& r : rows) if (!r.d->at("pass").b) { failed_any = true; ++failed; }
    if (B(a, "json")) {
        Val o = Val::dict();
        o.d->set("tool", Val::str("contrast")); o.d->set("version", Val::str(VERSION));
        o.d->set("min", Val::str(minimum)); o.d->set("pairs", Val::list(rows));
        DumpOpts d; d.indent = 2;
        print(dumps(o, d));
    } else {
        for (const auto& r : rows) {
            const std::string lf = r.d->at("fg").s, cf = r.d->at("fg_color").s, lb = r.d->at("bg").s, cb = r.d->at("bg_color").s;
            const std::string fg = lf == cf ? lf : lf + " (" + cf + ")";
            const std::string bg = lb == cb ? lb : lb + " (" + cb + ")";
            auto yn = [&](const char* k) { return std::string(r.d->at(k).b ? "yes" : "no"); };
            print(std::string(r.d->at("pass").b ? "PASS" : "FAIL") + "  " + rjust(fmt_f(r.d->at("ratio").f, 2), 5) + ":1  " + fg + " on " + bg +
                  "  [AA text " + yn("aa_text") + ", AA large " + yn("aa_large") + ", AAA " + yn("aaa_text") + "]");
            if (const Val* s = r.d->get("suggest"))
                print("        try " + s->d->at("color").s + " for the foreground (" + fmt_f(s->d->at("ratio").f, 2) + ":1, same hue)");
        }
        print(std::to_string(static_cast<long long>(rows.size()) - failed) + " of " + std::to_string(rows.size()) + " pairs meet " +
              fmt_f(threshold(minimum), 1) + ":1 (" + minimum + ").");
    }
    return failed_any ? 1 : 0;
}
PACKTOOL("master-build/contrast", main);
}  // namespace packtools::master_contrast
