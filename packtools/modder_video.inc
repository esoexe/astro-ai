// modder_video.inc - `um video`: a C++ port of universal-modder's um/video.py (Rehan and universal-modder
// contributors, MIT). Showcase videos with ffmpeg; the title graphics are drawn by modder_font.inc instead of Pillow.
namespace packtools::modder::video {
using namespace pyrt;
using namespace packtools::modder;
using namespace packtools::modder::img;

inline const char* DOC =
    "Showcase videos: pick moments, cut, title, transition, score, export. ffmpeg only (graphics drawn built in).\n\n"
    "    um video probe clip.mp4                              # duration, size, fps, audio\n"
    "    um video contact clip.mp4 sheet.png --every 2        # timestamped grid of frames: look, then choose in/out points\n"
    "    um video first-frame take.mkv                        # first real gameplay frame (skips dark/flat loading screens)\n"
    "    um video beats music.mp3                             # tempo + first beat, to cut on the beat\n"
    "    um video mux take.mkv take.mkv.audio.raw take.mkv.json out.mp4   # recorder output -> mp4 (see `um win record`)\n"
    "    um video compile edl.json out.mp4 [--preview]        # the edit, described as JSON (below)\n\n"
    "EDL (edit decision list) - every key but \"segments\" is optional:\n"
    "  {\"size\": [1920, 1080], \"fps\": 30, \"bpm\": 128,\n"
    "   \"theme\": {\"accent\": \"#B6FF3B\", \"text\": \"#FFFFFF\", \"chip\": \"#0A0D12E0\"},   # + \"title_font\"/\"body_font\"/\"mono_font\": .ttf paths\n"
    "   \"transition\": {\"type\": \"fade\", \"duration\": 0.3},        # any ffmpeg xfade type, or \"cut\"\n"
    "   \"clip_volume\": 0.6, \"music\": {\"path\": \"track.mp3\", \"volume\": 0.8, \"start\": 0, \"fade_in\": 0.2},\n"
    "   \"watermark\": {\"path\": \"logo.png\", \"height\": 54, \"corner\": \"br\", \"opacity\": 0.9},\n"
    "   \"counter\": false, \"fade_out\": 0.8, \"beat_lock\": true, \"normalize_audio\": true,\n"
    "   \"sfx\": [{\"path\": \"whoosh.mp3\", \"at\": 12.5, \"volume\": 0.8}],\n"
    "   \"segments\": [\n"
    "     {\"card\": {\"title\": \"Every game is moddable now.\", \"sub\": \"\", \"bg\": \"#05070A\", \"logo\": \"logo.png\"}, \"dur\": 2.2},\n"
    "     {\"clip\": \"a.mp4\", \"in\": 12.0, \"dur\": 4.0, \"title\": \"Minecraft inside Elden Ring\", \"credit\": \"@TobynJacobs\",\n"
    "      \"fill\": \"blur\", \"volume\": 0.6, \"speed\": 1.0, \"zoom\": 1.0, \"crop\": [0, 0, 1920, 1080], \"transition\": {\"type\": \"pixelize\"}},\n"
    "     {\"clip\": \"b.mp4\", \"in\": 0, \"beats\": 8, \"hook\": \"Point Claude at any game.\"}]}\n"
    "  \"dur\" is seconds, or give \"beats\" with a top-level \"bpm\" (+ \"beat_lock\" so cuts land on the grid despite transitions).\n"
    "  \"fill\": blur | crop | pad for clips that aren't 16:9. \"watermark\": false on a segment hides the logo there.\n"
    "  Titles are one line, bottom-left, sliding in after the transition; \"credit\" sits on the same line.\n"
    "  Space Grotesk has Latin glyphs only: for Cyrillic, Greek or CJK titles set the theme's *_font to a font that has them.\n\n"
    "Style that works on X: open on the strongest moment (<= 2-3 s before real gameplay), one-line titles,\n"
    "3-5 s per clip, credit every creator whose footage you use, end on the name/URL, fade out.\n";

// ------------------------------------------------------------------ small helpers
inline Val jg(const Val& d, const std::string& k, Val dflt = Val()) { if (d.t != Val::DICT) return dflt; const Val* p = d.d->get(k); return p ? *p : dflt; }
inline bool jhas(const Val& d, const std::string& k) { return d.t == Val::DICT && d.d->has(k); }
inline double num_of(const Val& v, double dflt = 0) {
    if (v.is_real()) return v.num();
    if (v.t == Val::STR) { const auto f = parse_float(v.s); if (f) return *f; throw PyErr("ValueError", "could not convert string to float: " + repr(v)); }
    if (v.is_none()) return dflt;
    throw PyErr("TypeError", "float() argument must be a string or a real number");
}
inline Val F(double x) { return Val::flt(x); }
inline std::string f2(double x, int nd) { return fmt_f(x, nd); }
inline std::string font_dir() {
    std::vector<std::string> c;
    if (!env("UM_FONTS").empty()) c.push_back(env("UM_FONTS"));
    if (!skill_dir_ref().empty()) { c.push_back(path_join(skill_dir_ref(), "fonts")); c.push_back(path_join(skill_dir_ref(), "../../assets/fonts")); }
    c.push_back(path_join(data_dir(), "fonts"));
    for (const auto& d : c) if (isfile(path_join(d, "SpaceGrotesk-Bold.ttf"))) return d;
    // A.S.T.R.O AI: Library packs are text-only, so the fonts (and their OFL texts) are built into the engine and
    // written to <data dir>/fonts the first time a video needs them.
    const std::string d = path_join(data_dir(), "fonts");
    makedirs(d);
    for (const auto& e : video_fonts::files) { const std::string p = path_join(d, e.name); if (!isfile(p) || fsize(p) != static_cast<long long>(e.size)) spit(p, video_fonts::b64(e)); }
    return d;
}
inline std::map<std::string, std::string>& font_overrides() { static std::map<std::string, std::string> m; return m; }
inline SizedFont font(const std::string& which, long long size) {   // which: title | body | mono
    const auto it = font_overrides().find(which);
    std::string p = it != font_overrides().end() ? it->second
                  : path_join(font_dir(), which == "title" ? "SpaceGrotesk-Bold.ttf" : which == "body" ? "SpaceGrotesk-Medium.ttf" : "JetBrainsMono-Bold.ttf");
    if (!isfile(p)) die("font not found: " + p + " (the pack ships Space Grotesk and JetBrains Mono in its fonts folder; set UM_FONTS to that folder or the theme's *_font)");
    return truetype(p, static_cast<int>(std::max(1LL, size)));
}
inline std::array<int, 4> rgba(const Val& v, const std::string& dflt) {
    std::string s = (v.t == Val::STR && !v.s.empty()) ? v.s : (truthy(v) ? pystr(v) : dflt);
    std::size_t k = 0; while (k < s.size() && s[k] == '#') ++k; s = s.substr(k);
    auto hx = [&](std::size_t at) {
        const std::string t = at < s.size() ? s.substr(at, 2) : std::string();
        char* end = nullptr; const long x = std::strtol(t.c_str(), &end, 16);
        if (t.empty() || *end) throw PyErr("ValueError", "invalid literal for int() with base 16: " + repr(Val::str(t)));
        return static_cast<int>(x);
    };
    return {hx(0), hx(2), hx(4), s.size() >= 8 ? hx(6) : 255};
}

// ------------------------------------------------------------------ ffmpeg
inline mk::Proc ff(const std::vector<std::string>& args, bool quiet = true) {
    std::vector<std::string> cmd{"ffmpeg", "-hide_banner", "-y"}; if (quiet) { cmd.push_back("-loglevel"); cmd.push_back("error"); }
    cmd.insert(cmd.end(), args.begin(), args.end());
    if (which_tool("ffmpeg").empty()) die("ffmpeg not found on PATH (um video needs it: winget install Gyan.FFmpeg / brew install ffmpeg / apt install ffmpeg)");
    std::vector<std::string> run_cmd = cmd; run_cmd[0] = which_tool("ffmpeg");
    const mk::Proc r = mk::run_proc(run_cmd);
    if (r.code != 0) {
        std::string line = join(cmd, " "); if (cplen(line) > 3000) line = cpslice(line, 0, 3000);
        std::string err = decode_utf8_replace(r.err); if (cplen(err) > 3000) err = cpslice(err, static_cast<long long>(cplen(err)) - 3000, static_cast<long long>(cplen(err)));
        die("ffmpeg failed:\n" + line + "\n" + err);
    }
    return r;
}
inline Val probe_ffmpeg(const std::string& path) {
    if (which_tool("ffmpeg").empty()) die("ffmpeg not found on PATH");
    const std::string err = decode_utf8_replace(mk::run_proc({which_tool("ffmpeg"), "-hide_banner", "-i", path}).err);
    if (err.find("Invalid data") != std::string::npos || err.find("No such file") != std::string::npos) { const auto ls = splitlines(strip(err)); die("cannot read " + path + ": " + (ls.empty() ? std::string() : ls.back())); }
    double dur = 0.0;
    if (auto m = re::search(R"(Duration: (\d+):(\d+):([\d.]+))", err); m.ok) dur = static_cast<double>(std::stoll(m.group(1)) * 3600 + std::stoll(m.group(2)) * 60) + std::stod(m.group(3));
    const auto v = re::search(R"(Stream #\S+.*Video: .*?(\d{2,5})x(\d{2,5}))", err);
    const auto fps = re::search(R"(([\d.]+) fps)", err);
    const auto a = re::search(R"(Stream #\S+.*Audio: .*?(\d+) Hz)", err);
    Val d = Val::dict();
    d.d->set("path", Val::str(path)); d.d->set("duration", F(dur));
    d.d->set("width", v.ok ? Val::integer(std::stoll(v.group(1))) : Val()); d.d->set("height", v.ok ? Val::integer(std::stoll(v.group(2))) : Val());
    d.d->set("fps", fps.ok ? F(std::stod(fps.group(1))) : Val()); d.d->set("audio", Val::boolean(a.ok)); d.d->set("audio_rate", a.ok ? Val::integer(std::stoll(a.group(1))) : Val());
    return d;
}
inline Val probe(const std::string& path) {
    if (which_tool("ffprobe").empty()) return probe_ffmpeg(path);
    const auto r = mk::run_proc({which_tool("ffprobe"), "-v", "error", "-print_format", "json", "-show_streams", "-show_format", path});
    if (r.code != 0) die("ffprobe " + path + ": " + strip(decode_utf8_replace(r.err)));
    const Val d = loads(decode_utf8_replace(r.out));
    Val v = Val::dict(), a; bool have_a = false, have_v = false;
    const Val streams = jg(d, "streams", Val::list()); for (const auto& s : *streams.l) {
        const std::string t = pystr(jg(s, "codec_type"));
        if (t == "video" && !have_v) { v = s; have_v = true; }
        if (t == "audio" && !have_a) { a = s; have_a = true; }
    }
    std::string rate = truthy(jg(v, "avg_frame_rate")) ? pystr(jg(v, "avg_frame_rate")) : "0/1";
    const auto sl = rate.find('/'); const double num = num_of(Val::str(rate.substr(0, sl))), den = num_of(Val::str(sl == std::string::npos ? "1" : rate.substr(sl + 1)));
    Val fmt = jg(d, "format", Val::dict());
    Val dv = truthy(jg(fmt, "duration")) ? jg(fmt, "duration") : truthy(jg(v, "duration")) ? jg(v, "duration") : Val::integer(0);
    Val o = Val::dict();
    o.d->set("path", Val::str(path)); o.d->set("duration", F(num_of(dv)));
    o.d->set("width", jg(v, "width")); o.d->set("height", jg(v, "height"));
    o.d->set("fps", den != 0 ? F(round_n(num / den, 3)) : Val()); o.d->set("audio", Val::boolean(have_a));
    o.d->set("audio_rate", have_a ? Val::integer(static_cast<long long>(num_of(jg(a, "sample_rate")))) : Val());
    return o;
}
inline std::string mkdtemp_um(const std::string& prefix) {
    std::error_code ec; const fs::path base = fs::temp_directory_path(ec);
    for (int i = 0; i < 1000; ++i) {
        char suf[32]; std::snprintf(suf, sizeof suf, "%08x", static_cast<unsigned>(std::random_device{}()));
        const fs::path p = base / (prefix + suf);
        if (fs::create_directory(p, ec)) return u8native(p);
    }
    die("cannot create a temporary folder"); return "";
}
inline void rmtree(const std::string& p) { std::error_code ec; fs::remove_all(P(p), ec); }

// ------------------------------------------------------------------ look at footage
inline std::vector<double> frange(double a, double b, double step) { std::vector<double> o; for (double t = a; t <= b + 1e-9; t += step) o.push_back(round_n(t, 3)); return o; }
inline void contact(const std::string& path, const std::string& out, double every, long long cols, long long width, double start, const Val& end_v) {
    if (every <= 0) die("--every must be > 0");
    const Val info = probe(path); const double dur = num_of(jg(info, "duration"));
    const double end = std::min(truthy(end_v) ? end_v.num() : dur, dur);
    const auto times = frange(start, end - 0.05, every);
    const std::string tmp = mkdtemp_um("um_contact_");
    const double ih = truthy(jg(info, "height")) ? jg(info, "height").num() : 9.0, iw = truthy(jg(info, "width")) ? jg(info, "width").num() : 16.0;
    const long long h = round_int(static_cast<double>(width) * ih / iw / 2.0) * 2;
    std::vector<std::pair<double, Image>> tiles;
    if ((end - start) / every <= 240 && (end - start) <= 600) {
        ff({"-ss", f2(start, 3), "-t", f2(end - start, 3), "-i", path, "-vf", "fps=1/" + float_repr(every) + ",scale=" + std::to_string(width) + ":" + std::to_string(h), path_join(tmp, "%04d.png")});
        std::vector<std::string> shots; std::error_code ec;
        for (fs::directory_iterator it(P(tmp), ec), e; !ec && it != e; it.increment(ec)) if (lower(u8native(it->path().extension())) == ".png") shots.push_back(u8native(it->path()));
        std::sort(shots.begin(), shots.end());
        for (std::size_t k = 0; k < shots.size(); ++k) { const double t = start + static_cast<double>(k) * every; if (t <= end) tiles.emplace_back(t, load(shots[k])); }
    } else {
        for (std::size_t i = 0; i < times.size(); ++i) {
            char nm[32]; std::snprintf(nm, sizeof nm, "%04zu.png", i); const std::string p = path_join(tmp, nm);
            ff({"-ss", f2(times[i], 3), "-i", path, "-frames:v", "1", "-vf", "scale=" + std::to_string(width) + ":" + std::to_string(h), p});
            if (pexists(p)) tiles.emplace_back(times[i], load(p));
        }
    }
    if (tiles.empty()) die("no frames extracted");
    const long long rows = -floordiv(-static_cast<long long>(tiles.size()), cols);
    Image sheet(cols * width, rows * (h + 22), {12, 14, 18, 255});
    const SizedFont fnt = font("mono", 15);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const long long x = (static_cast<long long>(i) % cols) * width, y = (static_cast<long long>(i) / cols) * (h + 22);
        Image t = tiles[i].second; for (std::size_t k = 3; k < t.px.size(); k += 4) t.px[k] = 255;
        paste(sheet, t, x, y + 22);
        char lab[48]; std::snprintf(lab, sizeof lab, "%6.1fs", tiles[i].first);
        draw_text(sheet, static_cast<double>(x + 6), static_cast<double>(y + 3), lab, fnt, {182, 255, 59, 255}, true);
    }
    save_rgb(sheet, out);
    rmtree(tmp);
    print(out + " " + std::to_string(tiles.size()) + " frames, " + f2(start, 1) + "-" + f2(end, 1) + "s every " + float_repr(every) + "s");
}
inline double first_frame(const std::string& path, double start = 0.0, double step = 0.5, double limit = 90, double min_mean = 45, double min_std = 18) {
    if (which_tool("ffmpeg").empty()) die("ffmpeg not found on PATH");
    for (double t = start; t < limit; t += step) {
        const auto r = mk::run_proc({which_tool("ffmpeg"), "-v", "error", "-ss", f2(t, 2), "-i", path, "-frames:v", "1", "-vf", "scale=160:90,format=gray", "-f", "rawvideo", "-"});
        const std::string& raw = r.out; if (raw.empty()) break;
        const long long rows = static_cast<long long>(raw.size()) / 160;
        double s = 0, s2 = 0; long long n = 0;
        for (long long y = 5; y < rows - 5; ++y) for (long long x = 20; x < 140; ++x) { const double v = static_cast<unsigned char>(raw[static_cast<std::size_t>(y * 160 + x)]); s += v; s2 += v * v; ++n; }
        if (!n) { continue; }
        const double mean = s / static_cast<double>(n), sd = std::sqrt(std::max(0.0, s2 / static_cast<double>(n) - mean * mean));
        if (mean > min_mean && sd > min_std) return t;
    }
    return 0.0;
}
inline void fft(std::vector<std::complex<double>>& a) {   // in-place radix-2
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) { std::size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * 3.14159265358979323846 / static_cast<double>(len); const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (std::size_t j = 0; j < len / 2; ++j) { const auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
inline Val beats(const std::string& path) {
    if (which_tool("ffmpeg").empty()) die("ffmpeg not found on PATH");
    const int sr = 22050;
    const auto r = mk::run_proc({which_tool("ffmpeg"), "-v", "error", "-i", path, "-ac", "1", "-ar", std::to_string(sr), "-f", "f32le", "-"});
    const std::size_t ns = r.out.size() / 4; std::vector<float> x(ns); if (ns) std::memcpy(x.data(), r.out.data(), ns * 4);
    if (static_cast<long long>(ns) < sr) die("audio too short");
    const std::size_t hop = 512, win = 1024, bins = win / 2 + 1;
    std::vector<double> han(win); for (std::size_t i = 0; i < win; ++i) han[i] = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(win - 1));
    const std::size_t nf = (ns - win) / hop + 1;
    std::vector<double> prev(bins), flux; flux.reserve(nf);
    std::vector<std::complex<double>> buf(win);
    for (std::size_t f = 0; f < nf; ++f) {
        for (std::size_t i = 0; i < win; ++i) buf[i] = std::complex<double>(static_cast<double>(x[f * hop + i]) * han[i], 0);
        fft(buf);
        std::vector<double> cur(bins); for (std::size_t k = 0; k < bins; ++k) cur[k] = std::log1p(std::abs(buf[k]));
        if (f) { double s = 0; for (std::size_t k = 0; k < bins; ++k) s += std::max(cur[k] - prev[k], 0.0); flux.push_back(s); }
        prev.swap(cur);
    }
    const std::size_t n = flux.size(); if (n < 2) die("audio too short");
    double mean = 0; for (double v : flux) mean += v; mean /= static_cast<double>(n);
    double var = 0; for (double v : flux) var += (v - mean) * (v - mean); const double sd = std::sqrt(var / static_cast<double>(n));
    for (double& v : flux) v = (v - mean) / (sd + 1e-9);
    const double fps = static_cast<double>(sr) / static_cast<double>(hop);
    long long lag = -1; double best = 0;
    for (std::size_t L = 0; L < n; ++L) {
        const double bpm = 60 * fps / static_cast<double>(std::max<std::size_t>(L, 1));
        if (bpm < 70 || bpm > 180) continue;
        double ac = 0; for (std::size_t i = 0; i + L < n; ++i) ac += flux[i + L] * flux[i];
        if (lag < 0 || ac > best) { best = ac; lag = static_cast<long long>(L); }
    }
    if (lag < 1) die("audio too short to find a tempo");
    const double bpm = 60 * fps / static_cast<double>(lag);
    std::size_t bo = 0; double bs = 0;
    for (long long o = 0; o < lag; ++o) { double s = 0; for (std::size_t i = static_cast<std::size_t>(o); i < n; i += static_cast<std::size_t>(lag)) s += flux[i]; if (o == 0 || s > bs) { bs = s; bo = static_cast<std::size_t>(o); } }
    Val d = Val::dict();
    d.d->set("bpm", F(round_n(bpm, 2))); d.d->set("first_beat", F(round_n(static_cast<double>(bo) / fps, 3))); d.d->set("duration", F(round_n(static_cast<double>(ns) / sr, 2)));
    return d;
}
inline void mux(const std::string& video, const std::string& raw_audio, const std::string& meta_json, const std::string& out, const Val& offset) {
    const Val meta = loads(slurp(meta_json));
    const double dur = num_of(jg(probe(video), "duration"));
    const double start = !offset.is_none() ? offset.num() : num_of(jg(meta, "audio_offset_s", F(0.0)));
    ff({"-i", video, "-f", pystr(jg(meta, "format", Val::str("f32le"))), "-ar", pystr(jg(meta, "rate", Val::integer(48000))), "-ac", pystr(jg(meta, "channels", Val::integer(2))),
        "-ss", f2(std::max(0.0, start), 3), "-i", raw_audio, "-map", "0:v", "-map", "1:a", "-t", f2(dur, 3), "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out});
    print(out);
}

// ------------------------------------------------------------------ graphics
inline long long title_chip(const std::string& text, const std::string& credit, long long W, long long H, const Val& theme, const std::string& out) {
    const double s = static_cast<double>(H) / 1080.0;
    const SizedFont big = font("title", round_int(50 * s)), small = font("mono", round_int(30 * s));
    Image img(W, H);
    const long long pad_x = round_int(30 * s), bar = round_int(8 * s);
    const double tw = textlength(big, text), cw = credit.empty() ? 0 : textlength(small, credit);
    const long long gap = credit.empty() ? 0 : round_int(22 * s);
    const long long w = static_cast<long long>(static_cast<double>(bar + pad_x) + tw + static_cast<double>(gap) + cw + static_cast<double>(pad_x));
    const long long h = round_int(84 * s), x0 = round_int(64 * s), y0 = H - round_int(70 * s) - h;
    const auto chip = rgba(jg(theme, "chip"), "#0A0D12E0"), accent = rgba(jg(theme, "accent"), "#B6FF3B");
    rounded_rectangle(img, x0, y0, x0 + w, y0 + h, round_int(16 * s), chip);
    rounded_rectangle(img, x0, y0, x0 + bar + round_int(10 * s), y0 + h, round_int(16 * s), accent);
    rectangle(img, x0 + bar, y0, x0 + bar + round_int(10 * s), y0 + h, chip);
    const auto bb = getbbox(big, "Ag");
    const long long ty = y0 + floordiv(h - (bb[3] - bb[1]), 2) - bb[1];
    draw_text(img, static_cast<double>(x0 + bar + pad_x), static_cast<double>(ty), text, big, rgba(jg(theme, "text"), "#FFFFFF"));
    if (!credit.empty()) {
        const auto cb = getbbox(small, "@g");
        const long long cy = y0 + floordiv(h - (cb[3] - cb[1]), 2) - cb[1];
        draw_text(img, static_cast<double>(x0 + bar + pad_x) + tw + static_cast<double>(gap), static_cast<double>(cy), credit, small, accent);
    }
    save(img, out);
    return x0;
}
inline void hook_text(const std::string& text, long long W, long long H, const Val& theme, const std::string& out, double size) {
    const double s = static_cast<double>(H) / 1080.0;
    const SizedFont f = font("title", round_int(size * s));
    const auto lines = split(text, "\n");
    Image img(W, H), sh(W, H);
    const long long lh = round_int(f.size * 1.08);
    long long y = floordiv(H - lh * static_cast<long long>(lines.size()), 2);
    for (const auto& line : lines) {
        const double x = (static_cast<double>(W) - textlength(f, line)) / 2;
        draw_text(sh, x, static_cast<double>(y + round_int(6 * s)), line, f, {0, 0, 0, 200});
        draw_text(img, x, static_cast<double>(y), line, f, rgba(jg(theme, "text"), "#FFFFFF"));
        y += lh;
    }
    sh = gaussian_blur(sh, static_cast<double>(round_int(14 * s)));
    alpha_composite(sh, img);
    save(sh, out);
}
inline void card(const Val& spec, long long W, long long H, const Val& theme, const std::string& out) {
    const double s = static_cast<double>(H) / 1080.0;
    const Val bg = jg(spec, "bg", Val::str("#05070A"));
    Image im;
    if (bg.t == Val::STR && pexists(bg.s)) {
        Image src = load(bg.s); for (std::size_t k = 3; k < src.px.size(); k += 4) src.px[k] = 255;
        const double r = std::max(static_cast<double>(W) / static_cast<double>(src.w), static_cast<double>(H) / static_cast<double>(src.h));
        src = resize(src, round_int(static_cast<double>(src.w) * r), round_int(static_cast<double>(src.h) * r), Filter::LANCZOS);
        const long long cx = floordiv(src.w - W, 2), cy = floordiv(src.h - H, 2);
        im = crop(src, {cx, cy, cx + W, cy + H});
        const double dim = num_of(jg(spec, "dim", F(0.45)));
        if (dim != 0) { Image layer(W, H, {0, 0, 0, static_cast<uint8_t>(static_cast<int>(255 * dim))}); alpha_composite(im, layer); }
    } else {
        const auto c = rgba(bg, "#FFFFFF");
        im = Image(W, H, {static_cast<uint8_t>(c[0]), static_cast<uint8_t>(c[1]), static_cast<uint8_t>(c[2]), static_cast<uint8_t>(c[3])});
        Image glow(W, H); const auto acc = rgba(jg(theme, "accent"), "#B6FF3B");
        ellipse(glow, static_cast<double>(W) * 0.2, static_cast<double>(H) * 0.05, static_cast<double>(W) * 0.8, static_cast<double>(H) * 0.95, {acc[0], acc[1], acc[2], 46});
        alpha_composite(im, gaussian_blur(glow, static_cast<double>(round_int(220 * s))));
    }
    const Val logo = jg(spec, "logo");
    const std::string title = truthy(jg(spec, "title")) ? pystr(jg(spec, "title")) : "", sub = truthy(jg(spec, "sub")) ? pystr(jg(spec, "sub")) : "";
    const SizedFont tf = font("title", round_int(num_of(jg(spec, "size", Val::integer(104))) * s)), sf = font("body", round_int(num_of(jg(spec, "sub_size", Val::integer(44))) * s));
    struct Block { int kind; Image img; std::string text; long long h; };
    std::vector<Block> blocks;
    if (truthy(logo) && pexists(pystr(logo))) {
        Image L = load(pystr(logo)); const long long lh = round_int(num_of(jg(spec, "logo_height", Val::integer(150))) * s);
        L = resize(L, round_int(static_cast<double>(L.w) * static_cast<double>(lh) / static_cast<double>(L.h)), lh, Filter::LANCZOS);
        blocks.push_back({0, L, "", lh});
    }
    if (!title.empty()) for (const auto& line : split(title, "\n")) blocks.push_back({1, Image(), line, round_int(tf.size * 1.12)});
    if (!sub.empty()) blocks.push_back({2, Image(), sub, round_int(sf.size * 1.6)});
    long long total = 0; for (const auto& b : blocks) total += b.h; if (truthy(logo)) total += round_int(30 * s);
    double y = static_cast<double>(H - total) / 2;
    const Val subc = truthy(jg(spec, "sub_color")) ? jg(spec, "sub_color") : jg(theme, "accent");
    for (const auto& b : blocks) {
        if (b.kind == 0) { alpha_composite_at(im, b.img, static_cast<long long>(static_cast<double>(W - b.img.w) / 2), static_cast<long long>(y)); y += static_cast<double>(b.h + round_int(30 * s)); }
        else if (b.kind == 1) { draw_text(im, (static_cast<double>(W) - textlength(tf, b.text)) / 2, y, b.text, tf, rgba(jg(theme, "text"), "#FFFFFF")); y += static_cast<double>(b.h); }
        else { draw_text(im, (static_cast<double>(W) - textlength(sf, b.text)) / 2, y + static_cast<double>(round_int(10 * s)), b.text, sf, rgba(subc, "#B6FF3B")); y += static_cast<double>(b.h); }
    }
    save_rgb(im, out);
}
inline void badge(const std::string& text, long long W, long long H, const Val& theme, const std::string& out) {
    const double s = static_cast<double>(H) / 1080.0;
    const SizedFont f = font("mono", round_int(26 * s));
    Image img(W, H);
    const double tw = textlength(f, text); const long long x0 = round_int(64 * s), y0 = round_int(56 * s);
    rounded_rectangle(img, x0, y0, static_cast<long long>(static_cast<double>(x0) + tw + static_cast<double>(round_int(28 * s))), y0 + round_int(46 * s), round_int(10 * s), rgba(jg(theme, "chip"), "#0A0D12E0"));
    draw_text(img, static_cast<double>(x0 + round_int(14 * s)), static_cast<double>(y0 + round_int(8 * s)), text, f, rgba(jg(theme, "accent"), "#B6FF3B"));
    save(img, out);
}

// ------------------------------------------------------------------ compile
inline double seg_dur(const Val& seg, const Val& bpm) {
    if (jhas(seg, "beats")) { if (!truthy(bpm)) die("segment uses 'beats' but the EDL has no 'bpm'"); return num_of(jg(seg, "beats")) * 60.0 / bpm.num(); }
    return num_of(jg(seg, "dur", F(4.0)));
}
inline std::string fill_filter(const std::string& fill, long long W, long long H, const Val& zoom, const Val& crop_v) {
    std::string pre;
    if (truthy(crop_v)) pre = "crop=" + pystr((*crop_v.l)[2]) + ":" + pystr((*crop_v.l)[3]) + ":" + pystr((*crop_v.l)[0]) + ":" + pystr((*crop_v.l)[1]) + ",";
    const std::string Ws = std::to_string(W), Hs = std::to_string(H);
    const std::string z = (truthy(zoom) && zoom.num() != 1.0) ? ",scale=iw*" + pystr(zoom) + ":ih*" + pystr(zoom) + ",crop=" + Ws + ":" + Hs : "";
    if (fill == "crop") return pre + "scale=" + Ws + ":" + Hs + ":force_original_aspect_ratio=increase:flags=lanczos,crop=" + Ws + ":" + Hs + z;
    if (fill == "pad") return pre + "scale=" + Ws + ":" + Hs + ":force_original_aspect_ratio=decrease:flags=lanczos,pad=" + Ws + ":" + Hs + ":(ow-iw)/2:(oh-ih)/2:color=black" + z;
    const std::string w4 = std::to_string(W / 4), h4 = std::to_string(H / 4);
    return pre + "split[fa][fb];[fa]scale=" + w4 + ":" + h4 + ":force_original_aspect_ratio=increase,crop=" + w4 + ":" + h4 + ",boxblur=12:2," +
           "eq=brightness=-0.12:saturation=1.2,scale=" + Ws + ":" + Hs + "[bgd];[fb]scale=" + Ws + ":" + Hs + ":force_original_aspect_ratio=decrease:flags=lanczos[fgd];" +
           "[bgd][fgd]overlay=(W-w)/2:(H-h)/2" + z;
}
inline std::string idx2(long long i) { char b[24]; std::snprintf(b, sizeof b, "%02lld", i); return b; }
inline std::string render_segment(long long i, const Val& seg, const Val& edl, const std::string& work, long long W, long long H, const Val& fps, double dur, double t_in, double t_out, long long total_n) {
    const Val theme = jg(edl, "theme", Val::dict());
    const std::string out = path_join(work, "seg_" + idx2(i) + ".mkv");
    const std::string fps_s = pystr(fps);
    const std::vector<std::string> vcodec{"-c:v", "libx264", "-preset", "veryfast", "-crf", "14", "-pix_fmt", "yuv420p", "-r", fps_s};
    const std::vector<std::string> acodec{"-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2"};
    auto cat = [](std::vector<std::string> a, const std::vector<std::string>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
    if (jhas(seg, "card")) {
        const std::string png = path_join(work, "card_" + idx2(i) + ".png");
        card(jg(seg, "card"), W, H, theme, png);
        const long long frames = std::max(1LL, round_int(dur * fps.num()));
        const std::string zin = pystr(jg(jg(seg, "card"), "push", F(0.04)));
        const std::string vf = "scale=" + std::to_string(W * 2) + ":" + std::to_string(H * 2) + ",zoompan=z='1+" + zin + "*on/" + std::to_string(frames) + "':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=" +
                               std::to_string(frames) + ":s=" + std::to_string(W) + "x" + std::to_string(H) + ":fps=" + fps_s + ",format=yuv420p";
        ff(cat(cat({"-loop", "1", "-framerate", fps_s, "-t", f2(dur, 3), "-i", png, "-f", "lavfi", "-t", f2(dur, 3), "-i", "anullsrc=r=48000:cl=stereo",
                    "-filter_complex", "[0:v]" + vf + "[v]", "-map", "[v]", "-map", "1:a", "-t", f2(dur, 3)}, vcodec), cat(acodec, {out})));
        return out;
    }
    const std::string src = pystr(jg(seg, "clip"));
    const Val info = probe(src);
    const double speed = num_of(jg(seg, "speed", F(1.0))), src_len = dur * speed, start = num_of(jg(seg, "in", F(0.0)));
    const double idur = num_of(jg(info, "duration"));
    if (start + src_len > idur + 0.05) die("segment " + std::to_string(i) + ": " + src + " is " + f2(idur, 2) + "s; in=" + float_repr(start) + " + " + f2(src_len, 2) + "s runs past the end");
    std::vector<std::string> inputs{"-ss", f2(start, 3), "-t", f2(src_len + 0.1, 3), "-i", src};
    long long n_in = 1;
    std::vector<std::string> graph{"[0:v]setpts=(PTS-STARTPTS)/" + float_repr(speed) + ",fps=" + fps_s + "," +
                                   fill_filter(pystr(jg(seg, "fill", Val::str("blur"))), W, H, jg(seg, "zoom", F(1.0)), jg(seg, "crop")) + ",setsar=1[base]"};
    std::string last = "base";
    std::vector<std::pair<std::string, std::string>> overlays;
    if (truthy(jg(seg, "title"))) {
        const std::string png = path_join(work, "title_" + idx2(i) + ".png");
        title_chip(pystr(jg(seg, "title")), truthy(jg(seg, "credit")) ? pystr(jg(seg, "credit")) : "", W, H, theme, png);
        overlays.emplace_back("title", png);
    }
    if (truthy(jg(seg, "hook"))) {
        const std::string png = path_join(work, "hook_" + idx2(i) + ".png");
        hook_text(pystr(jg(seg, "hook")), W, H, theme, png, num_of(jg(seg, "hook_size", Val::integer(112))));
        overlays.emplace_back("hook", png);
    }
    if (truthy(jg(edl, "counter")) && truthy(jg(seg, "title"))) {
        const std::string png = path_join(work, "badge_" + idx2(i) + ".png");
        badge(idx2(static_cast<long long>(num_of(jg(seg, "index", Val::integer(i))))) + "/" + idx2(total_n), W, H, theme, png);
        overlays.emplace_back("badge", png);
    }
    for (const auto& [kind, png] : overlays) {
        inputs.insert(inputs.end(), {"-loop", "1", "-framerate", fps_s, "-t", f2(dur, 3), "-i", png});
        const long long k = n_in++; const std::string ks = std::to_string(k);
        if (kind == "hook") {
            const double a0 = num_of(jg(seg, "hook_in", F(0.15))), a1 = num_of(jg(seg, "hook_out", F(dur - t_out - 0.1)));
            graph.push_back("[" + ks + ":v]format=rgba,fade=t=in:st=" + f2(a0, 2) + ":d=0.3:alpha=1,fade=t=out:st=" + f2(std::max(a0 + 0.3, a1 - 0.3), 2) + ":d=0.3:alpha=1[o" + ks + "]");
            graph.push_back("[" + last + "][o" + ks + "]overlay=0:0:shortest=1[v" + ks + "]");
        } else {
            const double t0 = t_in + num_of(jg(seg, "title_delay", F(0.2)));
            const double t1 = std::min(dur - t_out - 0.1, t0 + num_of(jg(seg, "title_hold", Val::integer(99))));
            graph.push_back("[" + ks + ":v]format=rgba,fade=t=in:st=" + f2(t0, 2) + ":d=0.25:alpha=1,fade=t=out:st=" + f2(std::max(t0 + 0.3, t1 - 0.25), 2) + ":d=0.25:alpha=1[o" + ks + "]");
            const std::string slide = kind == "title" ? "-70*pow(1-clip((t-" + f2(t0, 2) + ")/0.4\\,0\\,1)\\,3)" : "0";
            graph.push_back("[" + last + "][o" + ks + "]overlay=x='" + slide + "':y=0:shortest=1[v" + ks + "]");
        }
        last = "v" + ks;
    }
    graph.push_back("[" + last + "]format=yuv420p[v]");
    const double vol = num_of(jg(seg, "volume", jg(edl, "clip_volume", F(0.6))));
    std::string amap;
    if (truthy(jg(info, "audio"))) {
        std::vector<std::string> tempo; double sp = speed;
        while (sp > 2.0) { tempo.push_back("atempo=2.0"); sp /= 2.0; }
        while (sp < 0.5) { tempo.push_back("atempo=0.5"); sp /= 0.5; }
        tempo.push_back("atempo=" + f2(sp, 4));
        const std::string norm = truthy(jg(edl, "normalize_audio")) ? "loudnorm=I=-18:TP=-2:LRA=11," : "";
        graph.push_back("[0:a]asetpts=PTS-STARTPTS," + join(tempo, ",") + ",aresample=48000,aformat=channel_layouts=stereo," + norm + "aresample=48000,volume=" + float_repr(vol) + ",apad[a]");
        amap = "[a]";
    } else {
        inputs.insert(inputs.end(), {"-f", "lavfi", "-t", f2(dur, 3), "-i", "anullsrc=r=48000:cl=stereo"});
        amap = std::to_string(n_in) + ":a";
    }
    ff(cat(cat(cat(inputs, {"-filter_complex", join(graph, ";"), "-map", "[v]", "-map", amap, "-t", f2(dur, 3)}), vcodec), cat(acodec, {out})));
    return out;
}
inline void compile_edl(const std::string& edl_path, const std::string& out, bool preview, bool keep) {
    Val edl = loads(slurp(edl_path));
    if (edl.t != Val::DICT) die("the EDL must be a JSON object");
    const fs::path base = P(edl_path).parent_path();
    auto rel = [&](const Val& p) -> Val {
        if (!truthy(p)) return p;
        const std::string s = pystr(p);
        if (P(s).is_absolute()) return p;
        std::error_code ec; fs::path full = fs::weakly_canonical(fs::absolute(base / P(s), ec), ec);
        return Val::str(u8native(full));
    };
    font_overrides().clear();
    const Val theme = jg(edl, "theme", Val::dict());
    for (const auto& [key, which] : std::vector<std::pair<std::string, std::string>>{{"title_font", "title"}, {"body_font", "body"}, {"mono_font", "mono"}})
        if (truthy(jg(theme, key))) font_overrides()[which] = pystr(rel(jg(theme, key)));
    Val segs = jg(edl, "segments");
    if (segs.t != Val::LIST || segs.l->empty()) die("the EDL needs a non-empty \"segments\" list");
    for (auto& seg : *segs.l) {
        if (jhas(seg, "clip")) seg.d->set("clip", rel(jg(seg, "clip")));
        if (jhas(seg, "card")) { Val c = jg(seg, "card"); for (const char* k : {"bg", "logo"}) if (truthy(jg(c, k)) && !starts(pystr(jg(c, k)), "#")) c.d->set(k, rel(jg(c, k))); }
    }
    const Val size = jg(edl, "size", Val::list({Val::integer(1920), Val::integer(1080)}));
    long long W = static_cast<long long>(num_of((*size.l)[0])), H = static_cast<long long>(num_of((*size.l)[1]));
    if (W % 2 || H % 2) die("size " + std::to_string(W) + "x" + std::to_string(H) + ": width and height must be even (yuv420p H.264 refuses odd sizes)");
    Val fps = jg(edl, "fps", Val::integer(30));
    if (preview) { W = W / 4 * 2; H = H / 4 * 2; if (fps.num() > 30) fps = Val::integer(30); }
    const Val bpm = jg(edl, "bpm");
    Val default_tr = jg(edl, "transition"); if (!jhas(edl, "transition")) { default_tr = Val::dict(); default_tr.d->set("type", Val::str("fade")); default_tr.d->set("duration", F(0.3)); }
    const std::size_t ns = segs.l->size();
    std::vector<double> durs; for (const auto& s : *segs.l) durs.push_back(seg_dur(s, bpm));
    struct Tr { std::string type; double dur; Val dur_v; };
    std::vector<Tr> trans;
    for (std::size_t k = 0; k + 1 < ns; ++k) {
        Val t = Val::dict(); if (default_tr.t == Val::DICT) for (const auto& [a, v] : default_tr.d->items) t.d->set(a, v);
        const Val over = jg((*segs.l)[k + 1], "transition", Val::dict()); if (over.t == Val::DICT) for (const auto& [a, v] : over.d->items) t.d->set(a, v);
        if (pystr(jg(t, "type")) == "cut" || num_of(jg(t, "duration", Val::integer(0))) < 1.5 / fps.num()) trans.push_back({"cut", 0.0, F(0.0)});
        else trans.push_back({jhas(t, "type") ? pystr(jg(t, "type")) : "fade", num_of(jg(t, "duration")), jg(t, "duration")});
    }
    if (truthy(jg(edl, "beat_lock"))) for (std::size_t k = 0; k < durs.size(); ++k) durs[k] += k < trans.size() ? trans[k].dur : 0.0;
    const std::string work = mkdtemp_um("um_video_");
    long long n_titled = 0; for (const auto& s : *segs.l) if (truthy(jg(s, "title"))) ++n_titled;
    long long idx = 0; std::vector<std::string> files;
    for (std::size_t i = 0; i < ns; ++i) {
        Val& seg = (*segs.l)[i]; const double dur = durs[i];
        if (truthy(jg(seg, "title"))) { ++idx; seg.d->set("index", Val::integer(idx)); }
        const double t_in = i > 0 ? trans[i - 1].dur : 0.0, t_out = i < trans.size() ? trans[i].dur : 0.0;
        print("  segment " + std::to_string(i + 1) + "/" + std::to_string(ns) + ": " + (jhas(seg, "card") ? std::string("card") : name_of_path(pystr(jg(seg, "clip")))) + " " + f2(dur, 2) + "s");
        flush();
        files.push_back(render_segment(static_cast<long long>(i), seg, edl, work, W, H, fps, dur, t_in, t_out, n_titled));
    }
    const std::string fps_s = pystr(fps);
    std::vector<std::string> inputs, graph;
    for (const auto& f : files) { inputs.push_back("-i"); inputs.push_back(f); }
    for (std::size_t k = 0; k < files.size(); ++k) graph.push_back("[" + std::to_string(k) + ":v]setpts=PTS-STARTPTS,fps=" + fps_s + ",settb=AVTB[s" + std::to_string(k) + "]");
    double total = durs[0]; std::string vlast = "s0", alast = "0:a";
    for (std::size_t k = 1; k < files.size(); ++k) {
        const std::string ks = std::to_string(k); const double d = trans[k - 1].dur;
        if (trans[k - 1].type == "cut") {
            graph.push_back("[" + vlast + "][s" + ks + "]concat=n=2:v=1:a=0[xv" + ks + "]");
            graph.push_back("[" + alast + "][" + ks + ":a]concat=n=2:v=0:a=1[xa" + ks + "]");
        } else {
            const std::string ds = pystr(trans[k - 1].dur_v);
            graph.push_back("[" + vlast + "][s" + ks + "]xfade=transition=" + trans[k - 1].type + ":duration=" + ds + ":offset=" + f2(total - d, 4) + "[xv" + ks + "]");
            graph.push_back("[" + alast + "][" + ks + ":a]acrossfade=d=" + ds + ":c1=tri:c2=tri[xa" + ks + "]");
        }
        vlast = "xv" + ks; alast = "xa" + ks; total = total + durs[k] - d;
    }
    long long n = static_cast<long long>(files.size());
    const Val fade_out_v = jg(edl, "fade_out", F(0.8)); const double fade_out = num_of(fade_out_v); const std::string fo = pystr(fade_out_v);
    const Val wm = jg(edl, "watermark");
    if (truthy(wm) && wm.t == Val::DICT && pexists(pystr(rel(jg(wm, "path"))))) {
        Image L = load(pystr(rel(jg(wm, "path"))));
        const double s = static_cast<double>(H) / 1080.0; const long long lh = round_int(num_of(jg(wm, "height", Val::integer(54))) * s);
        L = resize(L, round_int(static_cast<double>(L.w) * static_cast<double>(lh) / static_cast<double>(L.h)), lh, Filter::LANCZOS);
        const double op = num_of(jg(wm, "opacity", F(0.9)));
        for (std::size_t k = 3; k < L.px.size(); k += 4) L.px[k] = static_cast<uint8_t>(static_cast<int>(L.px[k] * op));
        const std::string wpath = path_join(work, "wm.png"); save(L, wpath);
        inputs.insert(inputs.end(), {"-loop", "1", "-framerate", fps_s, "-t", f2(total, 3), "-i", wpath});
        const std::string m = std::to_string(round_int(num_of(jg(wm, "margin", Val::integer(40))) * s));
        const std::string corner = pystr(jg(wm, "corner", Val::str("br")));
        std::string pos;
        if (corner == "br") pos = "W-w-" + m + ":H-h-" + m; else if (corner == "bl") pos = m + ":H-h-" + m; else if (corner == "tr") pos = "W-w-" + m + ":" + m; else if (corner == "tl") pos = m + ":" + m;
        else throw PyErr("KeyError", repr(Val::str(corner)));
        std::vector<std::string> spans; double t = 0.0;
        for (std::size_t k = 0; k < ns; ++k) {
            const Val& seg = (*segs.l)[k];
            if (jhas(seg, "card") || (jhas(seg, "watermark") && jg(seg, "watermark").t == Val::BOOL && !jg(seg, "watermark").b)) spans.push_back("between(t," + f2(t, 2) + "," + f2(t + durs[k], 2) + ")");
            t += durs[k] - (k < trans.size() ? trans[k].dur : 0);
        }
        const std::string en = spans.empty() ? "0" : join(spans, "+");
        graph.push_back("[" + vlast + "][" + std::to_string(n) + ":v]overlay=" + pos + ":enable='not(" + en + ")'[wmv]");
        vlast = "wmv"; ++n;
    }
    graph.push_back("[" + vlast + "]fade=t=out:st=" + f2(total - fade_out, 3) + ":d=" + fo + ",format=yuv420p[vout]");
    std::vector<std::string> mix{alast};
    const Val sfx = jg(edl, "sfx", Val::list());
    if (sfx.t == Val::LIST) for (std::size_t j = 0; j < sfx.l->size(); ++j) {
        const Val& fx = (*sfx.l)[j]; const std::string fp = pystr(rel(jg(fx, "path")));
        if (!pexists(fp)) continue;
        inputs.push_back("-i"); inputs.push_back(fp);
        const long long ms = static_cast<long long>(num_of(jg(fx, "at", Val::integer(0))) * 1000);
        graph.push_back("[" + std::to_string(n) + ":a]aresample=48000,aformat=channel_layouts=stereo,volume=" + pystr(jg(fx, "volume", F(0.8))) + ",adelay=" + std::to_string(ms) + "|" + std::to_string(ms) + "[fx" + std::to_string(j) + "]");
        mix.push_back("fx" + std::to_string(j)); ++n;
    }
    if (mix.size() > 1) { std::string g; for (const auto& m : mix) g += "[" + m + "]"; graph.push_back(g + "amix=inputs=" + std::to_string(mix.size()) + ":duration=first:normalize=0[sfxmix]"); alast = "sfxmix"; }
    const Val music = jg(edl, "music");
    if (truthy(music) && music.t == Val::DICT && pexists(pystr(rel(jg(music, "path"))))) {
        inputs.insert(inputs.end(), {"-ss", pystr(jg(music, "start", Val::integer(0))), "-i", pystr(rel(jg(music, "path")))});
        graph.push_back("[" + std::to_string(n) + ":a]aresample=48000,aformat=channel_layouts=stereo,volume=" + pystr(jg(music, "volume", F(0.8))) + ",afade=t=in:d=" + pystr(jg(music, "fade_in", F(0.2))) + ",atrim=0:" + f2(total, 3) + "[mus]");
        graph.push_back("[" + alast + "][mus]amix=inputs=2:duration=first:normalize=0,afade=t=out:st=" + f2(total - fade_out, 3) + ":d=" + fo + ",alimiter=limit=0.95[aout]");
    } else graph.push_back("[" + alast + "]afade=t=out:st=" + f2(total - fade_out, 3) + ":d=" + fo + ",alimiter=limit=0.95[aout]");
    std::vector<std::string> args = inputs;
    for (const auto& x : std::vector<std::string>{"-filter_complex", join(graph, ";"), "-map", "[vout]", "-map", "[aout]", "-t", f2(total, 3), "-c:v", "libx264", "-preset", preview ? "veryfast" : "slow",
                                                 "-crf", preview ? "23" : "18", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", out}) args.push_back(x);
    ff(args);
    if (!keep) rmtree(work);
    print("wrote " + out + "  (" + f2(total, 2) + "s, " + std::to_string(W) + "x" + std::to_string(H) + "@" + fps_s + ")");
}

inline void reg(Parser& sub) {
    Parser& p = sub.sub("video", DOC); p.help_line = "compile styled showcase videos, contact sheets, trim, mux"; p.raw_description = true;
    p.subcommands("cmd", false); p.sub_metavar = "<cmd>";
    { Parser& q = p.sub("probe"); q.help_line = "duration / size / fps / audio"; q.pos("input"); }
    { Parser& q = p.sub("contact"); q.help_line = "timestamped frame grid for choosing moments"; q.pos("input"); q.pos("output");
      q.opt({"--every"}).number().def_float(2.0); q.opt({"--cols"}).integer().def(6LL); q.opt({"--width"}).integer().def(320LL);
      q.opt({"--start"}).number().def_float(0.0); q.opt({"--end"}).number(); }
    { Parser& q = p.sub("first-frame"); q.help_line = "seconds until the first gameplay frame"; q.pos("input"); }
    { Parser& q = p.sub("beats"); q.help_line = "tempo and first beat of a music file"; q.pos("input"); }
    { Parser& q = p.sub("mux"); q.help_line = "recorder .mkv + .audio.raw + .json -> mp4"; q.pos("video"); q.pos("audio"); q.pos("meta"); q.pos("output");
      q.opt({"--offset"}).number().help("audio offset override (s)"); }
    { Parser& q = p.sub("compile"); q.help_line = "render an EDL json into a finished video"; q.pos("edl"); q.pos("output");
      q.opt({"--preview"}).store_true().help("half resolution, fast encode"); q.opt({"--keep"}).store_true().help("keep the work folder"); }
}
inline int main_(const Val& a) {
    const std::string c = S(a, "cmd");
    if (c == "probe") { DumpOpts o; o.indent = 2; print(dumps(probe(S(a, "input")), o)); }
    else if (c == "contact") contact(S(a, "input"), S(a, "output"), get(a, "every").num(), get(a, "cols").i, get(a, "width").i, get(a, "start").num(), get(a, "end"));
    else if (c == "first-frame") print(float_repr(first_frame(S(a, "input"))));
    else if (c == "beats") print(dumps(beats(S(a, "input"))));
    else if (c == "mux") mux(S(a, "video"), S(a, "audio"), S(a, "meta"), S(a, "output"), get(a, "offset"));
    else if (c == "compile") compile_edl(S(a, "edl"), S(a, "output"), truthy(get(a, "preview")), truthy(get(a, "keep")));
    return 0;
}
}  // namespace packtools::modder::video
