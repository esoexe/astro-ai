// master_mediakit.inc - the shared helpers of the edit skill (master-skills edit/tools/mediakit.py): finding and running
// ffmpeg/ffprobe with readable errors, probing media (rotation, VFR, HDR, audio layout), which filters and encoders this
// ffmpeg has, a fast 10 ms speech envelope, EBU R128 loudness, and word timings from any common transcript format.
namespace packtools::master_mk {
using master::splitext;

constexpr int EXIT_OK = 0, EXIT_FAIL = 1, EXIT_USAGE = 2, EXIT_MISSING = 3;
inline const char* EXIT_CODES_HELP =
    "exit codes:\n"
    "  0  success\n"
    "  1  ffmpeg or runtime failure\n"
    "  2  bad input or arguments (missing file, no audio stream, ...)\n"
    "  3  a required program or feature is missing (ffmpeg, whisper.cpp, libass)";
inline const char* FFMPEG_INSTALL_HINT =
    "Install ffmpeg first (macOS: brew install ffmpeg, Debian/Ubuntu: sudo apt install ffmpeg, Windows: winget install Gyan.FFmpeg), "
    "or point EDIT_FFMPEG / EDIT_FFPROBE at the binaries.";

struct EditError : std::runtime_error { int code; EditError(const std::string& m, int c = EXIT_FAIL) : std::runtime_error(m), code(c) {} };
inline int run_main(const std::function<int()>& f) {   // EditError -> "error: ..." on stderr and its exit code
    try { return f(); }
    catch (const EditError& e) { eprint(std::string("error: ") + e.what()); return e.code; }
}
inline void warn(const std::string& m) { eprint("warning: " + m); }

// ------------------------------------------------------------------ ffmpeg / ffprobe
inline std::string find_binary(const std::string& name, const char* env_var) {
    const std::string o = env(env_var);
    if (!o.empty()) return o;
    const std::string found = which(name);
    if (found.empty()) throw EditError(name + " not found on PATH. " + FFMPEG_INSTALL_HINT, EXIT_MISSING);
    return found;
}
inline std::string ffmpeg_bin() { return find_binary("ffmpeg", "EDIT_FFMPEG"); }
inline std::string ffprobe_bin() { return find_binary("ffprobe", "EDIT_FFPROBE"); }
inline std::string short_cmd(const std::vector<std::string>& cmd) {
    const std::string t = shlex_join(cmd);
    return t.size() < 1500 ? t : cpslice(t, 0, 1500) + " ...";
}
struct Run { int code = 0; std::string out, err; };   // text output, decoded with errors="replace"
inline std::string tail_cps(const std::string& s, long n) { const long len = static_cast<long>(cplen(s)); return len <= n ? s : cpslice(s, len - n); }
inline Run run(const std::vector<std::string>& cmd, const std::string& cwd = "", bool check = true) {
    const Proc p = run_proc(cmd, cwd);
    if (p.os_errno) {
        const std::string what = p.not_found ? "No such file or directory" : std::strerror(p.os_errno);
        throw EditError("cannot run " + cmd[0] + ": [Errno " + std::to_string(p.not_found ? 2 : p.os_errno) + "] " + what + ": " + str_repr(p.not_found && !cwd.empty() && !is_dir(cwd) ? cwd : cmd[0]), EXIT_MISSING);
    }
    Run r; r.code = p.code; r.out = decode_utf8_replace(p.out); r.err = decode_utf8_replace(p.err);
    if (check && r.code != 0) {
        throw EditError(basename(cmd[0]) + " failed (exit " + std::to_string(r.code) + "):\n" + tail_cps(strip(r.err), 2500) + "\ncommand: " + short_cmd(cmd), EXIT_FAIL);
    }
    return r;
}
inline std::vector<std::string> splitlines(const std::string& s) {   // str.splitlines() for \n, \r\n and \r
    std::vector<std::string> v; std::string cur;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\n' || c == '\r') { v.push_back(cur); cur.clear(); if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i; }
        else cur += c;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}
struct Cache { bool have_version = false; std::optional<std::pair<int, int>> version; std::string version_line;
               std::optional<std::set<std::string>> filters, encoders; std::map<std::string, std::set<std::string>> opts; };
inline Cache& cache() { static Cache c; return c; }
inline std::optional<std::pair<int, int>> ffmpeg_version() {
    Cache& c = cache();
    if (!c.have_version) {
        const auto lines = splitlines(run({ffmpeg_bin(), "-hide_banner", "-version"}).out);
        const std::string first = lines.empty() ? "" : lines[0];
        const auto& m = re::search(R"(version\s+n?(\d+)\.(\d+))", first);
        if (m.ok) c.version = std::make_pair(std::stoi(master::ascii_digits(m.group(1))), std::stoi(master::ascii_digits(m.group(2))));
        c.version_line = first; c.have_version = true;
    }
    return c.version;
}
inline const std::set<std::string>& ffmpeg_filters() {
    Cache& c = cache();
    if (!c.filters) {
        std::set<std::string> names;
        for (const auto& line : splitlines(run({ffmpeg_bin(), "-hide_banner", "-filters"}).out)) {
            const auto& m = re::match(R"(^\s*[A-Z.|]{2,3}\s+(\S+)\s+\S*->\S*)", line);
            if (m.ok) names.insert(m.group(1));
        }
        c.filters = std::move(names);
    }
    return *c.filters;
}
inline bool has_filter(const std::string& name) { return ffmpeg_filters().count(name) > 0; }
inline const std::set<std::string>& filter_options(const std::string& name) {
    Cache& c = cache();
    auto it = c.opts.find(name);
    if (it == c.opts.end()) {
        const std::string out = run({ffmpeg_bin(), "-hide_banner", "-h", "filter=" + name}, "", false).out;
        std::set<std::string> s; for (const auto& x : re::findall(R"(^\s{2,}([a-z_0-9]+)\s+<)", out, re::M)) s.insert(x);
        it = c.opts.emplace(name, std::move(s)).first;
    }
    return it->second;
}
inline const std::set<std::string>& ffmpeg_encoders() {
    Cache& c = cache();
    if (!c.encoders) {
        const std::string out = run({ffmpeg_bin(), "-hide_banner", "-encoders"}).out;
        std::set<std::string> s; for (const auto& x : re::findall(R"(^\s*[VAS][A-Z.]{5}\s+(\S+))", out, re::M)) s.insert(x);
        c.encoders = std::move(s);
    }
    return *c.encoders;
}
inline bool has_encoder(const std::string& name) { return ffmpeg_encoders().count(name) > 0; }

// ------------------------------------------------------------------ probing
inline List or_empty(const Val& v) { return truthy(v) ? master::iterate(v) : List{}; }   // `x or []`
inline std::optional<double> num(const Val& v) {   // _num: float(value), None for TypeError/ValueError and non-finite
    try { const double x = to_float(v); if (std::isfinite(x)) return x; return std::nullopt; }
    catch (const PyErr&) { return std::nullopt; }
}
inline double num_or(const Val& v, double d) { const auto x = num(v); return x ? *x : d; }
inline Val opt_flt(const std::optional<double>& x) { return x ? Val::flt(*x) : Val(); }
inline std::optional<Frac> frac(const Val& v) {   // _frac: Fraction(str(value)) if > 0
    const auto f = parse_frac(v.t == Val::STR ? v.s : pystr(v));
    if (!f || !f->positive()) return std::nullopt;
    return f;
}
inline long long int_of(const Val& v) {   // int(x) for a JSON int/float/bool/str
    if (v.t == Val::INT) return v.i;
    if (v.t == Val::BOOL) return v.b ? 1 : 0;
    if (v.t == Val::FLOAT) return static_cast<long long>(std::trunc(v.f));
    if (v.t == Val::STR) { const auto x = parse_int(v.s); if (x) return *x; throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(v.s)); }
    throw PyErr("TypeError", std::string("int() argument must be a string, a bytes-like object or a real number, not '") + type_name(v) + "'");
}
inline Val video_info(const Val& s, double fmt_start) {
    const long long width = truthy(get(s, "width")) ? int_of(get(s, "width")) : 0, height = truthy(get(s, "height")) ? int_of(get(s, "height")) : 0;
    long long rotation = 0;
    for (const auto& side : or_empty(get(s, "side_data_list")))
        if (has(side, "rotation")) rotation = round_int(num_or(get(side, "rotation"), 0));
    const Val tags = truthy(get(s, "tags")) ? get(s, "tags") : Val::dict();
    if (!rotation && has(tags, "rotate")) rotation = static_cast<long long>(std::trunc(num_or(get(tags, "rotate"), 0)));
    rotation = ((rotation % 360) + 360) % 360;
    const bool swap = rotation == 90 || rotation == 270;
    const auto r_rate = frac(get(s, "r_frame_rate")), avg_rate = frac(get(s, "avg_frame_rate"));
    const auto fps = avg_rate ? avg_rate : r_rate;
    const bool vfr = r_rate && avg_rate && std::fabs(r_rate->f() - avg_rate->f()) > 0.01 * r_rate->f();
    const Val pf = get(s, "pix_fmt"); const std::string pix_fmt = truthy(pf) ? pystr(pf) : "";
    long long depth = static_cast<long long>(std::trunc(num_or(get(s, "bits_per_raw_sample"), 0)));
    if (!depth) depth = pix_fmt.find("12") != std::string::npos ? 12 : pix_fmt.find("10") != std::string::npos ? 10 : 8;
    const Val transfer = get(s, "color_transfer");
    const double start = num_or(get(s, "start_time"), fmt_start);
    const Val nb = get(s, "nb_frames");
    const std::string nbs = truthy(nb) ? pystr(nb) : "";
    Val d = Val::dict();
    d.d->set("index", get(s, "index")); d.d->set("codec", get(s, "codec_name")); d.d->set("profile", get(s, "profile"));
    d.d->set("width", Val::integer(width)); d.d->set("height", Val::integer(height)); d.d->set("rotation", Val::integer(rotation));
    d.d->set("display_width", Val::integer(swap ? height : width)); d.d->set("display_height", Val::integer(swap ? width : height));
    d.d->set("pix_fmt", Val::str(pix_fmt)); d.d->set("bit_depth", Val::integer(depth));
    d.d->set("fps", fps ? Val::flt(fps->f()) : Val());
    d.d->set("r_frame_rate", r_rate ? Val::str(r_rate->str()) : Val()); d.d->set("avg_frame_rate", avg_rate ? Val::str(avg_rate->str()) : Val());
    d.d->set("vfr", Val::boolean(vfr));
    d.d->set("color_primaries", get(s, "color_primaries")); d.d->set("color_transfer", transfer);
    d.d->set("color_space", get(s, "color_space")); d.d->set("color_range", get(s, "color_range"));
    d.d->set("hdr", Val::boolean(transfer.t == Val::STR && (transfer.s == "arib-std-b67" || transfer.s == "smpte2084")));
    d.d->set("start", Val::flt(start)); d.d->set("offset", Val::flt(std::max(0.0, start - fmt_start)));
    d.d->set("duration", opt_flt(num(get(s, "duration"))));
    d.d->set("nb_frames", !nbs.empty() && isdigit_str(nbs) ? Val::integer(std::stoll(master::ascii_digits(nbs))) : Val());
    return d;
}
inline Val audio_info(const Val& s, double fmt_start, long long order) {
    const double start = num_or(get(s, "start_time"), fmt_start);
    Val d = Val::dict();
    d.d->set("index", get(s, "index")); d.d->set("order", Val::integer(order)); d.d->set("codec", get(s, "codec_name"));
    d.d->set("sample_rate", Val::integer(static_cast<long long>(std::trunc(num_or(get(s, "sample_rate"), 0)))));
    d.d->set("channels", Val::integer(truthy(get(s, "channels")) ? int_of(get(s, "channels")) : 0));
    d.d->set("channel_layout", get(s, "channel_layout"));
    d.d->set("start", Val::flt(start)); d.d->set("offset", Val::flt(std::max(0.0, start - fmt_start)));
    d.d->set("duration", opt_flt(num(get(s, "duration"))));
    const long long br = static_cast<long long>(std::trunc(num_or(get(s, "bit_rate"), 0)));
    d.d->set("bit_rate", br ? Val::integer(br) : Val());
    return d;
}
inline Val probe(const std::string& path) {   // times use ffmpeg's filter timeline (format start = 0)
    if (!is_file(path)) throw EditError("file not found: " + path, EXIT_USAGE);
    const Run proc = run({ffprobe_bin(), "-v", "error", "-print_format", "json", "-show_format", "-show_streams", path}, "", false);
    if (proc.code != 0) throw EditError("ffprobe cannot read " + path + ": " + tail_cps(strip(proc.err), 400), EXIT_USAGE);
    const Val data = loads(proc.out.empty() ? "{}" : proc.out);
    const Val fmt = truthy(get(data, "format")) ? get(data, "format") : Val::dict();
    const double fmt_start = num_or(get(fmt, "start_time"), 0.0);
    Val video; Val audio_streams = Val::list();
    for (const auto& stream : or_empty(get(data, "streams"))) {
        const Val kind = get(stream, "codec_type");
        if (kind.t != Val::STR) continue;
        if (kind.s == "video") {
            if (truthy(get(get(stream, "disposition"), "attached_pic"))) continue;   // cover art inside an audio file
            if (video.is_none()) video = video_info(stream, fmt_start);
        } else if (kind.s == "audio") {
            audio_streams.l->push_back(audio_info(stream, fmt_start, static_cast<long long>(audio_streams.l->size())));
        }
    }
    const auto duration = num(get(fmt, "duration"));
    std::vector<double> ends;
    if (!video.is_none() && truthy(get(video, "duration"))) ends.push_back(get(video, "offset").f + get(video, "duration").f);
    if (!audio_streams.l->empty() && truthy(get((*audio_streams.l)[0], "duration"))) ends.push_back(get((*audio_streams.l)[0], "offset").f + get((*audio_streams.l)[0], "duration").f);
    if (ends.empty() && duration && *duration) ends.push_back(*duration);
    Val d = Val::dict();
    d.d->set("path", Val::str(abspath(path))); d.d->set("format", get(fmt, "format_name"));
    d.d->set("duration", opt_flt(duration));
    d.d->set("end", ends.empty() ? opt_flt(duration) : Val::flt(*std::min_element(ends.begin(), ends.end())));
    d.d->set("size", Val::integer(static_cast<long long>(std::trunc(num_or(get(fmt, "size"), 0)))));
    const long long br = static_cast<long long>(std::trunc(num_or(get(fmt, "bit_rate"), 0)));
    d.d->set("bit_rate", br ? Val::integer(br) : Val());
    d.d->set("video", video);
    d.d->set("audio", audio_streams.l->empty() ? Val() : (*audio_streams.l)[0]);
    d.d->set("audio_streams", audio_streams);
    return d;
}
inline Frac cfr_rate(const Val& video) {   // the source rate, snapped to a standard rate when within 2%
    static const Frac RATES[] = {Frac(24000, 1001), Frac(24), Frac(25), Frac(30000, 1001), Frac(30), Frac(48), Frac(50), Frac(60000, 1001), Frac(60), Frac(120)};
    const auto r_rate = frac(get(video, "r_frame_rate")), avg_rate = frac(get(video, "avg_frame_rate"));
    const Frac base = (r_rate && *r_rate <= Frac(121)) ? *r_rate : avg_rate ? *avg_rate : r_rate ? *r_rate : Frac(30);
    const Frac* best = &RATES[0]; double best_key = 0; bool first = true;
    for (const auto& c : RATES) {
        const double key = std::fabs((c - base).f()) / c.f();
        if (first || key < best_key) { best = &c; best_key = key; first = false; }
    }
    if (std::fabs((*best - base).f()) / best->f() <= 0.02) return *best;
    return limit_denominator(base, 1001);
}
inline bool is_image_file(const std::string& path) {
    static const std::set<std::string> EXTS{".png", ".jpg", ".jpeg", ".webp", ".bmp", ".tif", ".tiff"};
    return EXTS.count(lower(splitext(path).second)) > 0;
}
inline std::pair<long long, long long> parse_size(const std::string& text) {
    const auto& m = re::fullmatch(R"(\s*(\d+)\s*[xX:]\s*(\d+)\s*)", text);
    if (!m.ok) throw EditError("bad size " + str_repr(text) + ", expected WIDTHxHEIGHT like 1080x1920", EXIT_USAGE);
    const std::string ws = master::ascii_digits(m.group(1)), hs = master::ascii_digits(m.group(2));
    if (ws.size() > 9 || hs.size() > 9) throw EditError("size " + ws + "x" + hs + " must be even and at least 16 px", EXIT_USAGE);
    const long long w = std::stoll(ws), h = std::stoll(hs);
    if (w < 16 || h < 16 || w % 2 || h % 2) throw EditError("size " + std::to_string(w) + "x" + std::to_string(h) + " must be even and at least 16 px", EXIT_USAGE);
    return {w, h};
}
// encoder arguments that keep the picture honest: HDR stays 10-bit HEVC with its colour tags, else H.264 yuv420p
inline std::pair<std::vector<std::string>, std::string> video_codec_args(const Val& video, long long crf = 18, const std::string& preset = "medium") {
    std::vector<std::string> tags;
    for (const auto& [key, flag] : std::vector<std::pair<std::string, std::string>>{{"color_primaries", "-color_primaries"}, {"color_transfer", "-color_trc"}, {"color_space", "-colorspace"}}) {
        const Val v = get(video, key);
        if (truthy(v) && v.t == Val::STR && v.s != "unknown" && v.s != "reserved" && v.s != "unspecified") { tags.push_back(flag); tags.push_back(v.s); }
    }
    const bool hdr = truthy(get(video, "hdr"));
    if (hdr && has_encoder("libx265")) {
        std::vector<std::string> a{"-c:v", "libx265", "-crf", std::to_string(crf), "-preset", preset, "-pix_fmt", "yuv420p10le", "-tag:v", "hvc1", "-x265-params", "log-level=error"};
        a.insert(a.end(), tags.begin(), tags.end());
        return {a, "HEVC 10-bit (HDR kept)"};
    }
    if (has_encoder("libx264")) {
        std::string note = "H.264 8-bit";
        if (hdr) note += " (HDR source without libx265: colours are not tone mapped)";
        std::vector<std::string> a{"-c:v", "libx264", "-crf", std::to_string(crf), "-preset", preset, "-pix_fmt", "yuv420p"};
        a.insert(a.end(), tags.begin(), tags.end());
        return {a, note};
    }
    throw EditError("this ffmpeg has no libx264 encoder; install a full ffmpeg build", EXIT_MISSING);
}

// ------------------------------------------------------------------ audio envelope and loudness
// short-term level in dBFS every `hop` seconds (power averaged over channels); frame i covers [offset + i*hop, offset + (i+1)*hop)
inline std::vector<double> audio_envelope(const std::string& path, const Val& info, long long stream = 0, double hop = 0.01, long long rate = 16000, double highpass = 70.0) {
    const Val streams = get(info, "audio_streams");
    if (stream < 0 || stream >= static_cast<long long>(streams.size())) throw EditError(basename(path) + " has no audio stream #" + std::to_string(stream), EXIT_USAGE);
    const long long ch_v = get(streams[static_cast<std::size_t>(stream)], "channels").i;
    const long long channels = std::max<long long>(1, ch_v ? ch_v : 1);
    std::vector<std::string> filters;
    if (highpass) filters.push_back("highpass=f=" + fmt_g(highpass));
    filters.push_back("aresample=" + std::to_string(rate));
    const std::vector<std::string> cmd{ffmpeg_bin(), "-nostdin", "-hide_banner", "-v", "error", "-i", path, "-map", "0:a:" + std::to_string(stream),
                                       "-vn", "-sn", "-dn", "-af", join(filters, ","), "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1"};
    const long long hop_samples = std::max<long long>(1, round_int(static_cast<double>(rate) * hop)) * channels;
    const std::size_t frame_bytes = static_cast<std::size_t>(hop_samples) * 2;
    const double full_scale = 32768.0 * 32768.0;
    std::vector<double> levels; std::string pending;
    auto level = [&](const unsigned char* p, std::size_t nsamples) {
        long long power = 0;
        for (std::size_t k = 0; k < nsamples; ++k) { const int16_t v = static_cast<int16_t>(static_cast<uint16_t>(p[2 * k] | (p[2 * k + 1] << 8))); power += static_cast<long long>(v) * v; }
        levels.push_back(power ? 10 * std::log10(static_cast<double>(power) / static_cast<double>(nsamples) / full_scale) : -120.0);
    };
    const Proc p = run_proc(cmd, "", [&](const char* data, std::size_t n) {
        pending.append(data, n);
        const std::size_t whole = pending.size() / frame_bytes;
        if (!whole) return;
        const auto* u = reinterpret_cast<const unsigned char*>(pending.data());
        for (std::size_t i = 0; i < whole; ++i) level(u + i * frame_bytes, static_cast<std::size_t>(hop_samples));
        pending.erase(0, whole * frame_bytes);
    });
    if (p.os_errno) throw EditError("cannot run " + cmd[0] + ": [Errno 2] No such file or directory: " + str_repr(cmd[0]), EXIT_MISSING);
    const std::size_t tail = pending.size() - pending.size() % 2;
    if (tail >= 2) level(reinterpret_cast<const unsigned char*>(pending.data()), tail / 2);
    if (p.code != 0) throw EditError("ffmpeg could not decode the audio of " + path + ":\n" + tail_cps(strip(decode_utf8_replace(p.err)), 1500), EXIT_FAIL);
    return levels;
}
inline double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return std::nan("");
    const long long pos = std::min<long long>(static_cast<long long>(sorted.size()) - 1, std::max<long long>(0, round_int(fraction * static_cast<double>(sorted.size() - 1))));
    return sorted[static_cast<std::size_t>(pos)];
}
struct SpeechLevels { double noise, speech, threshold, spread; bool reliable; };
// adaptive gate: noise floor = 10th percentile, speech = loud frames; the gate sits 30 dB under speech, >= 8 dB over the floor
inline SpeechLevels speech_levels(const std::vector<double>& levels) {
    std::vector<double> v = levels; std::sort(v.begin(), v.end());
    if (v.empty()) throw EditError("the audio stream is empty", EXIT_USAGE);
    const double noise = percentile(v, 0.10);
    const double speech = std::max({percentile(v, 0.95), percentile(v, 0.99) - 6.0, v.back() - 12.0});
    const double spread = speech - noise;
    const double threshold = spread >= 18.0 ? std::max(noise + 8.0, speech - 30.0) : noise + spread * 0.5;
    return {round_n(noise, 1), round_n(speech, 1), round_n(threshold, 1), round_n(spread, 1), spread >= 12.0};
}
inline Val levels_val(const SpeechLevels& s) {
    Val d = Val::dict();
    d.d->set("noise_db", Val::flt(s.noise)); d.d->set("speech_db", Val::flt(s.speech)); d.d->set("threshold_db", Val::flt(s.threshold));
    d.d->set("spread_db", Val::flt(s.spread)); d.d->set("reliable", Val::boolean(s.reliable));
    return d;
}
inline Val parse_ebur128(const std::string& err) {
    const std::size_t k = err.rfind("Summary:");
    const std::string summary = k == std::string::npos ? err : err.substr(k);
    auto grab = [&](const std::string& label) -> Val {
        const auto& m = re::search(label + R"(:\s+(-?inf|-?\d+(?:\.\d+)?))", summary);
        if (!m.ok) return Val();
        const std::string g = m.group(1);
        if (g.find("inf") != std::string::npos) return Val();
        return Val::flt(parse_float(g).value_or(0.0));
    };
    Val d = Val::dict();
    d.d->set("integrated_lufs", grab("I")); d.d->set("lra_lu", grab("LRA")); d.d->set("true_peak_dbtp", grab("Peak"));
    return d;
}
inline Val measure_loudness(const std::string& path, long long stream = 0, const std::string& pre_filter = "") {
    const std::string chain = (pre_filter.empty() ? "" : pre_filter + ",") + "ebur128=peak=true:framelog=verbose";
    const Run r = run({ffmpeg_bin(), "-nostdin", "-hide_banner", "-nostats", "-i", path, "-map", "0:a:" + std::to_string(stream), "-af", chain, "-f", "null", "-"});
    return parse_ebur128(r.err);
}

// ------------------------------------------------------------------ word timings (transcripts)
struct Word { std::string text; double start = 0, end = 0; std::optional<double> prob; };
inline Val first_of(const Val& item, std::initializer_list<const char*> keys) {   // _first
    for (const char* k : keys) { const Val v = get(item, k); if (has(item, k) && !v.is_none()) return v; }
    return Val();
}
#define MK_START_KEYS {"start", "start_time", "startTime", "t0", "begin"}
#define MK_END_KEYS {"end", "end_time", "endTime", "t1", "stop"}
#define MK_TEXT_KEYS {"punctuated_word", "text", "word", "token"}
inline bool wordish(const Val& item) {
    if (item.t != Val::DICT) return false;
    const bool has_time = !first_of(item, MK_START_KEYS).is_none() || get(item, "offsets").t == Val::DICT;
    return has_time && !first_of(item, MK_TEXT_KEYS).is_none();
}
inline bool all_wordish(const List& v, std::size_t limit) {
    for (std::size_t k = 0; k < v.size() && k < limit; ++k) if (!wordish(v[k])) return false;
    return true;
}
// the word list in Scribe, Whisper, faster-whisper, whisper.cpp, Deepgram, AssemblyAI, HyperFrames or this skill's JSON
inline List find_word_list(const Val& data) {
    if (data.t == Val::LIST && !data.l->empty() && all_wordish(*data.l, 50)) return *data.l;
    if (data.t == Val::DICT) {
        const Val w = get(data, "words");
        if (w.t == Val::LIST && !w.l->empty() && wordish((*w.l)[0])) return *w.l;
        const Val segments = get(data, "segments");
        if (segments.t == Val::LIST) {
            bool any = false;
            for (const auto& s : *segments.l) if (s.t == Val::DICT && truthy(get(s, "words"))) { any = true; break; }
            if (any) {
                List out;
                for (const auto& s : *segments.l) {
                    if (s.t != Val::DICT) continue;
                    const Val sw = get(s, "words");
                    if (!truthy(sw)) continue;
                    if (sw.t == Val::LIST) for (const auto& x : *sw.l) out.push_back(x);
                    else if (sw.t == Val::DICT) for (const auto& kv : sw.d->items) out.push_back(Val::str(kv.first));
                    else if (sw.t == Val::STR) for (const auto& c : cps(sw.s)) out.push_back(Val::str(c));
                }
                return out;
            }
        }
        const Val r = get(data, "results");
        if (r.t == Val::DICT) {
            const Val chans = get(r, "channels");
            if (chans.t == Val::LIST && !chans.l->empty()) {
                const Val alts = get((*chans.l)[0], "alternatives");
                if (alts.t == Val::LIST && !alts.l->empty()) {
                    const Val dw = get((*alts.l)[0], "words");
                    if (dw.t == Val::LIST) return *dw.l;
                }
            }
        }
        const Val tr = get(data, "transcription");
        if (tr.t == Val::LIST) return *tr.l;
    }
    List best; bool found = false;
    std::function<void(const Val&)> walk = [&](const Val& node) {
        if (node.t == Val::LIST) {
            if (node.l->size() > best.size() && !node.l->empty() && all_wordish(*node.l, 20)) { best = *node.l; found = true; }
            for (const auto& c : *node.l) walk(c);
        } else if (node.t == Val::DICT) {
            for (const auto& kv : node.d->items) walk(kv.second);
        }
    };
    walk(data);
    if (found) return best;
    throw EditError("no word timings found in that JSON (expected a list of {text, start, end})", EXIT_USAGE);
}
struct RawWord { Val text; double start, end; std::optional<double> prob; };
inline std::vector<Word> clean_words(std::vector<RawWord> words) {
    std::stable_sort(words.begin(), words.end(), [](const RawWord& a, const RawWord& b) { return a.start < b.start || (a.start == b.start && a.end < b.end); });
    std::vector<Word> out;
    for (const auto& w : words) {
        const std::string text = strip(re::sub(R"(\s+)", " ", w.text.t == Val::STR ? w.text.s : pystr(w.text)));
        if (text.empty()) continue;
        const double start = std::max(0.0, w.start), end = std::max(start + 0.01, w.end);
        Word item{text, round_n(start, 3), round_n(end, 3), std::nullopt};
        if (w.prob) item.prob = round_n(*w.prob, 3);
        out.push_back(item);
    }
    return out;
}
inline std::vector<Word> clean_words(const std::vector<Word>& words) {
    std::vector<RawWord> raw; for (const auto& w : words) raw.push_back({Val::str(w.text), w.start, w.end, w.prob});
    return clean_words(raw);
}
inline double ts_seconds(const re::Match& m, int offset) {
    const std::string h = m.group(offset);
    const long long hours = h.empty() ? 0 : std::stoll(master::ascii_digits(h));
    const long long minutes = std::stoll(master::ascii_digits(m.group(offset + 1))), seconds = std::stoll(master::ascii_digits(m.group(offset + 2)));
    const std::string fr = master::ascii_digits(m.group(offset + 3));
    return static_cast<double>(hours * 3600 + minutes * 60 + seconds) + static_cast<double>(std::stoll(fr)) / std::pow(10.0, static_cast<double>(fr.size()));
}
// SRT/VTT cues to approximate word timings (spread by character length); cue boundaries stay exact
inline std::vector<Word> words_from_subtitles(const std::string& text) {
    const std::string TS = R"((?:(\d+):)?(\d{1,2}):(\d{2})[,.](\d{1,3}))";
    const std::string pat = TS + R"(\s*-->\s*)" + TS;
    std::vector<RawWord> words;
    for (const auto& block : re::split(R"(\n\s*\n)", replace_all(replace_all(text, "\r\n", "\n"), "\r", "\n"))) {
        std::vector<std::string> lines; for (const auto& ln : split(block, "\n")) if (!strip(ln).empty()) lines.push_back(ln);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto& m = re::search(pat, lines[i]);
            if (!m.ok) continue;
            const double start = ts_seconds(m, 1), end = ts_seconds(m, 5);
            std::vector<std::string> rest(lines.begin() + static_cast<long>(i) + 1, lines.end());
            const std::string body = re::sub(R"(<[^>]+>|\{[^}]*\})", "", join(rest, " "));
            const auto tokens = split_ws(body);
            if (tokens.empty() || end <= start) break;
            std::vector<long long> weights; long long sum = 0;
            for (const auto& t : tokens) { weights.push_back(static_cast<long long>(cplen(t)) + 1); sum += weights.back(); }
            const double total = static_cast<double>(sum); double cursor = start;
            for (std::size_t k = 0; k < tokens.size(); ++k) {
                const double span = (end - start) * static_cast<double>(weights[k]) / total;
                words.push_back({Val::str(tokens[k]), cursor, cursor + span * 0.92, std::nullopt});
                cursor += span;
            }
            break;
        }
    }
    if (words.empty()) throw EditError("no cues found in that subtitle file", EXIT_USAGE);
    return clean_words(words);
}
// word timings as [{text, start, end, prob?}] in seconds, sorted; JSON transcripts, or .srt/.vtt
inline std::vector<Word> load_words(const std::string& path, std::optional<double> media_duration = std::nullopt) {
    if (!is_file(path)) throw EditError("transcript not found: " + path, EXIT_USAGE);
    const std::string ext = lower(splitext(path).second);
    const std::string raw = read_text(path, true);
    if (ext == ".srt" || ext == ".vtt") return words_from_subtitles(raw);
    Val data;
    try { data = loads(raw); }
    catch (const PyErr& e) { if (e.type.find("JSONDecodeError") == std::string::npos) throw; throw EditError(path + " is not valid JSON: " + e.what(), EXIT_USAGE); }
    const List items = find_word_list(data);
    std::vector<RawWord> words; bool all_int = true; double max_end = 0.0;
    for (const auto& item : items) {
        if (item.t != Val::DICT) continue;
        const Val ty = get(item, "type");
        if (ty.t == Val::STR && (ty.s == "spacing" || ty.s == "audio_event")) continue;
        const Val text = first_of(item, MK_TEXT_KEYS);
        std::optional<double> start, end;
        const bool offs = get(item, "offsets").t == Val::DICT;
        if (offs) {
            const Val o = get(item, "offsets");
            start = num(get(o, "from")); end = num(get(o, "to"));
            if (start) { const double e = end ? *end : *start; start = *start / 1000.0; end = e / 1000.0; }
        } else {
            start = num(first_of(item, MK_START_KEYS)); end = num(first_of(item, MK_END_KEYS));
        }
        if (text.is_none() || !start) continue;
        if (!end) end = start;
        if (offs) all_int = false;
        else if (!(std::trunc(*start) == *start && std::trunc(*end) == *end)) all_int = false;
        max_end = std::max(max_end, *end);
        const auto prob = num(first_of(item, {"probability", "confidence", "prob", "logprob"}));
        words.push_back({text, *start, *end, prob && *prob >= 0 && *prob <= 1 ? prob : std::nullopt});
    }
    if (words.empty()) throw EditError(path + " has no usable word timings", EXIT_USAGE);
    bool in_ms = max_end > 36000 || (all_int && max_end > 1000);
    if (media_duration && *media_duration && max_end > *media_duration * 2 && max_end / 1000.0 <= *media_duration * 1.5) in_ms = true;
    if (in_ms) for (auto& w : words) { w.start /= 1000.0; w.end /= 1000.0; }
    return clean_words(words);
}

// ------------------------------------------------------------------ small utilities
inline void write_json(const std::string& path, const Val& obj) {   // atomic: temp file in the same folder, then replace
    const std::string folder = dirname(abspath(path));
    makedirs(folder);
    static std::atomic<unsigned> counter{0};
    const std::string tmp = path_join(folder, ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007) + "-" + std::to_string(counter++) + ".json");
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(tmp, dumps(obj, o) + "\n");
    std::error_code ec; std::filesystem::rename(fspath(tmp), fspath(path), ec);
    if (ec) { std::filesystem::remove(fspath(tmp), ec); throw OSError(ec.value(), path); }
}
inline Val words_val(const std::vector<Word>& words, bool with_type) {
    Val l = Val::list();
    for (const auto& w : words) {
        Val d = Val::dict();
        d.d->set("text", Val::str(w.text)); d.d->set("start", Val::flt(round_n(w.start, 3))); d.d->set("end", Val::flt(round_n(w.end, 3)));
        if (with_type) d.d->set("type", Val::str("word"));
        if (w.prob) d.d->set("prob", Val::flt(round_n(*w.prob, 3)));
        l.l->push_back(d);
    }
    return l;
}
// a schema HyperFrames' caption workflow can also reuse as transcript.json: {text, language_code, words: [{text, start, end, type}]}
inline Val save_words(const std::string& path, const std::vector<Word>& words, const std::string& source = "", const std::string& engine = "",
                      const std::string& language = "", const Val& extra = Val()) {
    Val doc = Val::dict();
    std::vector<std::string> texts; for (const auto& w : words) texts.push_back(w.text);
    doc.d->set("version", Val::integer(1)); doc.d->set("kind", Val::str("words")); doc.d->set("text", Val::str(join(texts, " ")));
    doc.d->set("language_code", Val::str(language.empty() ? "en" : language)); doc.d->set("engine", Val::str(engine.empty() ? "unknown" : engine));
    if (!source.empty() && is_file(source)) {
        Val s = Val::dict(); s.d->set("path", Val::str(abspath(source))); s.d->set("size", Val::integer(size_of(source))); s.d->set("mtime", Val::flt(round_n(mtime_of(source), 3)));
        doc.d->set("source", s);
    }
    if (extra.t == Val::DICT) for (const auto& kv : extra.d->items) doc.d->set(kv.first, kv.second);
    doc.d->set("words", words_val(words, true));
    write_json(path, doc);
    return doc;
}
inline std::string zpad(const std::string& s, std::size_t w) {   // format(x, "0{w}.Nf") for x >= 0
    return s.size() >= w ? s : std::string(w - s.size(), '0') + s;
}
// a packed, phrase-per-line transcript: '[012.34-015.67] text'; breaks on pauses >= gap, or at sentence ends once long
inline std::vector<std::string> phrase_lines(const std::vector<Word>& words, double gap = 0.5, std::size_t max_words = 16) {
    std::vector<std::string> lines; std::vector<const Word*> cur;
    auto flush_line = [&] {
        if (cur.empty()) return;
        std::vector<std::string> t; for (const auto* w : cur) t.push_back(w->text);
        lines.push_back("[" + zpad(fmt_f(cur.front()->start, 2), 7) + "-" + zpad(fmt_f(cur.back()->end, 2), 7) + "] " + join(t, " "));
        cur.clear();
    };
    for (const auto& w : words) {
        if (!cur.empty()) {
            const double pause = w.start - cur.back()->end;
            const std::string last = cpslice(cur.back()->text, -1);
            const bool sentence_end = last.empty() || last == "." || last == "!" || last == "?";
            if (pause >= gap || cur.size() >= max_words || (sentence_end && cur.size() >= 8)) flush_line();
        }
        cur.push_back(&w);
    }
    flush_line();
    return lines;
}
inline std::string fmt_time(double seconds) {
    seconds = std::max(0.0, seconds);
    double rest = 0; const double minutes = master::py_divmod(seconds, 60.0, rest);
    return std::to_string(static_cast<long long>(minutes)) + ":" + zpad(fmt_f(rest, 2), 5);
}
inline std::string default_output(const std::string& path, const std::string& suffix, const std::optional<std::string>& ext = std::nullopt) {
    const auto [stem, original] = splitext(path);
    return stem + suffix + (ext ? *ext : original);
}
inline std::string stem_of(const std::string& path) { return splitext(basename(path)).first; }
inline std::string json_print(const Val& v) { DumpOpts o; o.indent = 2; return dumps(v, o); }   // json.dumps(x, indent=2)
}  // namespace packtools::master_mk
namespace packtools::master_mk {
// whisper.cpp's command line program (C++, no Python): EDIT_WHISPER, else whisper-cli / whisper-cpp on PATH
inline std::string whisper_bin() {
    const std::string o = env("EDIT_WHISPER");
    if (!o.empty()) return o;
    for (const char* n : {"whisper-cli", "whisper-cpp", "whisper.cpp"}) { const std::string w = which(n); if (!w.empty()) return w; }
    return "";
}
}  // namespace packtools::master_mk
