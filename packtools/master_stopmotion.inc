// master_stopmotion.inc - master-skills edit/tools/stopmotion.py: stop motion with ffmpeg filters only. Frames mode fits
// images (natural order) to a canvas, holds each `--hold` frames, adds deterministic per-drawing jitter and rotation (same
// --seed = same output, the seed drives the same Mersenne Twister as the original), optional loop / ping-pong / end hold.
// From-video mode drops the frame rate, adds the same jitter and keeps the audio in sync.
namespace packtools::master_stopmotion {
using namespace pyrt;
using namespace packtools::master_mk;

// natural_key: digit runs compare as numbers, the rest case-folded with lower()
inline int natural_cmp(const std::string& x, const std::string& y) {
    const auto px = re::split(R"((\d+))", x), py = re::split(R"((\d+))", y);
    for (std::size_t k = 0; k < px.size() && k < py.size(); ++k) {
        if (k % 2) {   // captured digit runs
            std::string a = master::ascii_digits(px[k]), b = master::ascii_digits(py[k]);
            a.erase(0, std::min(a.find_first_not_of('0'), a.size())); b.erase(0, std::min(b.find_first_not_of('0'), b.size()));
            if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
            if (a != b) return a < b ? -1 : 1;
        } else {
            const int c = cpcmp(lower(px[k]), lower(py[k]));
            if (c) return c;
        }
    }
    return px.size() == py.size() ? 0 : px.size() < py.size() ? -1 : 1;
}
inline std::vector<std::string> list_frames(const std::string& folder) {
    if (!is_dir(folder)) throw EditError("not a folder: " + folder, EXIT_USAGE);
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(fspath(folder), ec)) {
        const std::string f = e.path().filename().string();
        if (is_image_file(f) && !starts(f, ".")) files.push_back(f);
    }
    std::sort(files.begin(), files.end());
    std::stable_sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return natural_cmp(a, b) < 0; });
    if (files.empty()) throw EditError("no images in " + folder + " (png, jpg, webp, bmp, tif)", EXIT_USAGE);
    std::vector<std::string> out; for (const auto& f : files) out.push_back(path_join(folder, f));
    return out;
}
// deterministic pseudo-random 0..1 per drawing: fract(sin(i*a + b) * 43758.5453)
inline std::string hash_expr(const std::string& var, double a, double b) {
    const std::string inner = "sin(" + var + "*" + fmt_f(a, 4) + "+" + fmt_f(b, 4) + ")*43758.5453";
    return "(" + inner + "-floor(" + inner + "))";
}
// rotate + crop + scale: each drawing sits a few px off and a fraction of a degree askew; the crop margin hides the corners
inline std::string jitter_chain(long long width, long long height, double jitter, double rotate_deg, double flicker, long long seed, const std::string& index_var) {
    PyRandom rng(seed);
    double offsets[4]; for (double& o : offsets) o = rng.uniform(0.0, 100.0);
    const double pi = 3.141592653589793;
    const std::string h_rot = hash_expr(index_var, 12.9898, offsets[0]), h_x = hash_expr(index_var, 78.2330, offsets[1]), h_y = hash_expr(index_var, 39.3468, offsets[2]);
    long long margin = static_cast<long long>(std::ceil(jitter + static_cast<double>(std::max(width, height)) / 2 * std::sin(rotate_deg * (pi / 180.0)) + 2));
    margin += ((margin % 2) + 2) % 2;
    std::vector<std::string> parts;
    if (rotate_deg > 0) parts.push_back("rotate=a='" + fmt_f(rotate_deg * pi / 180, 6) + "*(2*" + h_rot + "-1)':c=black:ow=iw:oh=ih");
    if (jitter > 0 || rotate_deg > 0) {
        parts.push_back("crop=w=iw-" + std::to_string(2 * margin) + ":h=ih-" + std::to_string(2 * margin) + ":x='" + std::to_string(margin) + "+" + fmt_f(jitter, 3) + "*(2*" + h_x +
                        "-1)':y='" + std::to_string(margin) + "+" + fmt_f(jitter, 3) + "*(2*" + h_y + "-1)'");
        parts.push_back("scale=" + std::to_string(width) + ":" + std::to_string(height) + ":flags=bicubic");
    }
    if (flicker > 0) parts.push_back("eq=brightness='" + fmt_f(flicker, 4) + "*(2*" + hash_expr(index_var, 93.9898, offsets[3]) + "-1)':eval=frame");
    parts.push_back("setsar=1");
    return join(parts, ",");
}
inline void normalize(const std::string& src, const std::string& dst, long long width, long long height, const std::string& fit, const std::string& bg) {
    const std::string W = std::to_string(width), H = std::to_string(height);
    const std::string graph = "color=c=" + bg + ":s=" + W + "x" + H + ":d=1[bg];[0:v]scale=" + W + ":" + H + ":force_original_aspect_ratio=" +
                              (fit == "cover" ? "increase" : "decrease") + ",format=rgba[fg];[bg][fg]overlay=(W-w)/2:(H-h)/2:format=auto,format=rgb24";
    run({ffmpeg_bin(), "-nostdin", "-hide_banner", "-v", "error", "-y", "-i", src, "-filter_complex", graph, "-frames:v", "1", dst});
}
inline std::vector<std::string> encode_args(long long crf) {
    return {"-c:v", "libx264", "-crf", std::to_string(crf), "-preset", "medium", "-pix_fmt", "yuv420p", "-movflags", "+faststart"};
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("stopmotion", "Stop motion with ffmpeg filters only.");
    p.epilog = EXIT_CODES_HELP; p.raw_description = true;
    p.pos("frames").nargs("?").help("folder of frame images");
    p.opt({"--from-video"}).metavar("CLIP").help("turn a live clip into a stop-motion look");
    p.opt({"-o", "--output"}).required().help("output .mp4/.mov");
    p.opt({"--fps"}).number().def_float(12).help("drawings per second: 8, 10 or 12 (default 12)");
    p.opt({"--hold"}).integer().def(2).help("frames each drawing is held (2 = on twos)");
    p.opt({"--size"}).help("canvas WxH (frames default 1080x1920; video keeps its size)");
    p.opt({"--fit"}).choices({"cover", "contain"}).def("cover").help("fit images to the canvas");
    p.opt({"--bg"}).def("#F4EFE6").help("background colour for contain / transparent PNGs");
    p.opt({"--jitter"}).number().help("max position wobble in px (default 0.5%% of the short side)");
    p.opt({"--rotate"}).number().def_float(0.5).help("max rotation wobble in degrees");
    p.opt({"--flicker"}).number().def_float(0.0).help("exposure flicker, e.g. 0.01 (off by default)");
    p.opt({"--seed"}).integer().def(1).help("same seed, same wobble");
    p.opt({"--loop"}).integer().def(1).help("play the frames N times (wobble differs per pass)");
    p.opt({"--pingpong"}).store_true().help("forward then backward (seamless loop)");
    p.opt({"--end-hold"}).number().def_float(0.0).help("hold the last frame this many seconds");
    p.opt({"--crf"}).integer().def(18).help("H.264 quality (0 = lossless)");
    p.opt({"--json"}).store_true().help("print a JSON summary");
    const Val args = parse_args(p, argv);
    auto opt_s = [&](const char* k) { const Val v = get(args, k); return v.t == Val::STR ? v.s : std::string(); };
    const std::string frames_dir = opt_s("frames"), clip = opt_s("from_video"), output = S(args, "output");
    const auto P = ap::norm(p, p.prog);
    if (frames_dir.empty() == clip.empty()) ap::fail(P, "give a frames folder or --from-video CLIP (one of them)");
    const double fps = get(args, "fps").num(); const long long hold = get(args, "hold").i;
    if (!(1 <= fps && fps <= 30) || !(1 <= hold && hold <= 8)) ap::fail(P, "--fps must be 1-30 and --hold 1-8");
    const double out_fps = fps * static_cast<double>(hold);
    if (out_fps > 60) ap::fail(P, "fps x hold must be <= 60");
    const std::string rate = fmt_g(out_fps);
    const long long crf = get(args, "crf").i, seed = get(args, "seed").i;
    const double rotate = get(args, "rotate").num(), flicker = get(args, "flicker").num(), end_hold = get(args, "end_hold").num();
    const std::string fit = S(args, "fit"), bg = S(args, "bg");
    const Val jv = get(args, "jitter");

    return run_main([&]() -> int {
        { const std::string d = dirname(abspath(output)); makedirs(d.empty() ? "." : d); }
        Val summary = Val::dict();
        summary.d->set("output", Val::str(abspath(output))); summary.d->set("drawing_fps", Val::flt(fps)); summary.d->set("hold", Val::integer(hold));
        summary.d->set("output_fps", Val::flt(out_fps)); summary.d->set("seed", Val::integer(seed));
        if (!clip.empty()) {
            const Val info = probe(clip);
            if (!truthy(get(info, "video"))) throw EditError("that file has no video", EXIT_USAGE);
            long long width, height;
            if (!opt_s("size").empty()) std::tie(width, height) = parse_size(opt_s("size"));
            else { width = get(get(info, "video"), "display_width").i; height = get(get(info, "video"), "display_height").i; }
            width -= width % 2; height -= height % 2;
            const double jitter = jv.is_none() ? static_cast<double>(std::min(width, height)) * 0.005 : jv.num();
            const std::string W = std::to_string(width), H = std::to_string(height);
            const std::string pre = "scale=" + W + ":" + H + ":force_original_aspect_ratio=" + (fit == "cover" ? "increase" : "decrease") + "," +
                                    (fit == "cover" ? "crop" : "pad") + "=" + W + ":" + H + (fit == "cover" ? std::string() : ":(ow-iw)/2:(oh-ih)/2:color=" + bg) + ",";
            const std::string vf = pre + "fps=" + fmt_g(fps) + "," + jitter_chain(width, height, jitter, rotate, flicker, seed, "n") + ",fps=" + rate + ",format=yuv420p";
            std::vector<std::string> cmd{ffmpeg_bin(), "-nostdin", "-hide_banner", "-v", "error", "-y", "-i", clip, "-map", "0:v:0", "-map", "0:a?", "-vf", vf};
            for (const auto& x : encode_args(crf)) cmd.push_back(x);
            const Val audio = get(info, "audio");
            const bool copy = truthy(audio) && get(audio, "codec").t == Val::STR && get(audio, "codec").s == "aac";
            for (const auto& x : copy ? std::vector<std::string>{"-c:a", "copy"} : std::vector<std::string>{"-c:a", "aac", "-b:a", "256k"}) cmd.push_back(x);
            cmd.push_back(output);
            run(cmd);
            summary.d->set("mode", Val::str("from-video")); summary.d->set("size", Val::str(W + "x" + H));
        } else {
            const auto [width, height] = parse_size(!opt_s("size").empty() ? opt_s("size") : std::string("1080x1920"));
            const double jitter = jv.is_none() ? static_cast<double>(std::min(width, height)) * 0.005 : jv.num();
            const auto frames = list_frames(frames_dir);
            std::vector<std::size_t> order; for (std::size_t i = 0; i < frames.size(); ++i) order.push_back(i);
            if (B(args, "pingpong") && frames.size() > 2) for (std::size_t i = frames.size() - 2; i > 0; --i) order.push_back(i);
            { const std::vector<std::size_t> once = order; const long long loops = std::max<long long>(1, get(args, "loop").i); order.clear(); for (long long k = 0; k < loops; ++k) order.insert(order.end(), once.begin(), once.end()); }
            TempDir tmp("stopmotion-");
            const std::string norm_dir = path_join(tmp.path, "norm"), seq_dir = path_join(tmp.path, "seq");
            makedirs(norm_dir); makedirs(seq_dir);
            std::vector<std::string> normed;
            for (std::size_t i = 0; i < frames.size(); ++i) { char b[32]; std::snprintf(b, sizeof b, "%05zu.png", i); normed.push_back(path_join(norm_dir, b)); }
            {   // up to 8 at a time, like ThreadPoolExecutor(max_workers=min(8, cpu_count)); the first failure (in order) is raised
                const unsigned hw = std::thread::hardware_concurrency();
                const std::size_t workers = std::min<std::size_t>({8, hw ? hw : 2, frames.size()});
                std::vector<std::exception_ptr> errs(frames.size());
                std::atomic<std::size_t> next{0};
                std::vector<std::thread> pool;
                for (std::size_t t = 0; t < workers; ++t) pool.emplace_back([&] {
                    for (std::size_t i; (i = next++) < frames.size();) {
                        try { normalize(frames[i], normed[i], width, height, fit, bg); } catch (...) { errs[i] = std::current_exception(); }
                    }
                });
                for (auto& th : pool) th.join();
                for (const auto& e : errs) if (e) std::rethrow_exception(e);
            }
            long long n = 0;
            for (const std::size_t idx : order) {   // each drawing repeated `hold` times: exact frame counts, no fps filter
                for (long long h = 0; h < hold; ++h) {
                    char b[32]; std::snprintf(b, sizeof b, "%06lld.png", n);
                    const std::string link = path_join(seq_dir, b);
                    std::error_code ec;
                    std::filesystem::create_symlink(fspath(normed[idx]), fspath(link), ec);
                    if (ec) { ec.clear(); std::filesystem::copy_file(fspath(normed[idx]), fspath(link), ec); if (ec) throw OSError(ec.value(), normed[idx]); }
                    n++;
                }
            }
            std::string vf = jitter_chain(width, height, jitter, rotate, flicker, seed, "floor(n/" + std::to_string(hold) + ")");
            if (end_hold > 0) vf += ",tpad=stop_mode=clone:stop_duration=" + fmt_g(end_hold);
            vf += ",format=yuv420p";
            std::vector<std::string> cmd{ffmpeg_bin(), "-nostdin", "-hide_banner", "-v", "error", "-y", "-framerate", rate, "-i", path_join(seq_dir, "%06d.png"), "-vf", vf, "-r", rate};
            for (const auto& x : encode_args(crf)) cmd.push_back(x);
            cmd.push_back(output);
            run(cmd);
            summary.d->set("mode", Val::str("frames")); summary.d->set("images", Val::integer(static_cast<long long>(frames.size())));
            summary.d->set("drawings", Val::integer(static_cast<long long>(order.size()))); summary.d->set("frames", Val::integer(n));
            summary.d->set("size", Val::str(std::to_string(width) + "x" + std::to_string(height)));
            summary.d->set("duration_s", Val::flt(round_n(static_cast<double>(n) / out_fps + end_hold, 3)));
        }
        if (B(args, "json")) print(json_print(summary));
        else print("stopmotion: " + pystr(get(summary, "mode")) + ", " + fmt_g(fps) + " drawings/s held " + std::to_string(hold) + " -> " + fmt_g(out_fps) + " fps, " +
                   pystr(get(summary, "size")) + "\n  wrote: " + pystr(get(summary, "output")));
        return EXIT_OK;
    });
}
PACKTOOL("master-edit/stopmotion", main);
}  // namespace packtools::master_stopmotion
