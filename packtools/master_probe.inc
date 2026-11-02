// master_probe.inc - master-skills edit/tools/probe.py: probe media before editing, or check what this machine can do
// (--doctor). The doctor reports whisper.cpp (the C++ transcriber the captions tool uses) where the original reported
// the faster-whisper Python package.
namespace packtools::master_probe {
using namespace pyrt;
using namespace packtools::master_mk;

inline const std::vector<std::string> KEY_FILTERS{"ass", "subtitles", "drawtext", "silencedetect", "acrossfade", "alimiter", "loudnorm", "ebur128", "rotate", "crop", "zscale", "tonemap"};
inline const std::vector<std::string> KEY_ENCODERS{"libx264", "libx265", "aac", "libmp3lame", "libopus"};

inline Val notes_for(const Val& info) {   // plain-English warnings an editor should know before cutting
    Val notes = Val::list();
    auto add = [&](const std::string& s) { notes.l->push_back(Val::str(s)); };
    const Val video = get(info, "video"), audio = get(info, "audio");
    if (!truthy(audio)) add("no audio stream: dead-space cutting and transcription need audio");
    if (truthy(audio) && get(audio, "channels").i == 1) add("mono audio: fine for voice; finish keeps it mono unless a music bed is added");
    const std::size_t n_streams = get(info, "audio_streams").size();
    if (truthy(audio) && n_streams > 1) add(std::to_string(n_streams) + " audio streams: tools use the first (--audio-stream to change)");
    if (truthy(video)) {
        const long long dw = get(video, "display_width").i, dh = get(video, "display_height").i;
        if (get(video, "rotation").i)
            add("rotation " + std::to_string(get(video, "rotation").i) + " deg: displays as " + std::to_string(dw) + "x" + std::to_string(dh) + " (ffmpeg rotates automatically)");
        if (truthy(get(video, "vfr")))
            add("variable frame rate (avg " + pystr(get(video, "avg_frame_rate")) + " vs " + pystr(get(video, "r_frame_rate")) + "): deadspace renders constant " + cfr_rate(video).str() + " fps");
        if (truthy(get(video, "hdr")))
            add("HDR (HLG/PQ) footage: tools keep it 10-bit HEVC; motion engines that render SDR need one SDR conversion first, done once, not per step");
        if (dh && dw) {
            const double ratio = static_cast<double>(dw) / static_cast<double>(dh);
            if (std::fabs(ratio - 9.0 / 16.0) > 0.01)
                add("aspect " + std::to_string(dw) + "x" + std::to_string(dh) + " is not 9:16: reframe for Reels/TikTok/Shorts (finish --fit cover|contain)");
        }
    }
    return notes;
}
inline std::string describe(const Val& info) {
    const Val dur = get(info, "duration");
    std::vector<std::string> lines{pystr(get(info, "path")),
        "  container " + pystr(get(info, "format")) + ", " + fmt_f(truthy(dur) ? dur.num() : 0.0, 2) + " s, " + fmt_f(static_cast<double>(get(info, "size").i) / 1e6, 1) + " MB"};
    const Val video = get(info, "video"), audio = get(info, "audio");
    if (truthy(video)) {
        std::string rate = "?";
        if (truthy(get(video, "fps"))) { rate = fmt_f(get(video, "fps").num(), 3); while (!rate.empty() && rate.back() == '0') rate.pop_back(); if (!rate.empty() && rate.back() == '.') rate.pop_back(); }
        lines.push_back("  video  " + pystr(get(video, "codec")) + " " + std::to_string(get(video, "display_width").i) + "x" + std::to_string(get(video, "display_height").i) +
                        " @ " + rate + " fps" + (truthy(get(video, "vfr")) ? " (VFR)" : "") + ", " + pystr(get(video, "pix_fmt")) + ", " +
                        std::to_string(get(video, "bit_depth").i) + "-bit" + (truthy(get(video, "hdr")) ? ", HDR" : ""));
    }
    if (truthy(audio)) {
        const Val cl = get(audio, "channel_layout");
        lines.push_back("  audio  " + pystr(get(audio, "codec")) + " " + std::to_string(get(audio, "sample_rate").i) + " Hz, " + std::to_string(get(audio, "channels").i) + " ch" +
                        (truthy(cl) ? " (" + pystr(cl) + ")" : ""));
    }
    const Val loud = get(info, "loudness");
    if (truthy(loud))
        lines.push_back("  loudness " + pystr(get(loud, "integrated_lufs")) + " LUFS integrated, true peak " + pystr(get(loud, "true_peak_dbtp")) + " dBTP, range " + pystr(get(loud, "lra_lu")) + " LU");
    for (const auto& n : master::iterate(get(info, "notes"))) lines.push_back("  note: " + pystr(n));
    return join(lines, "\n");
}
inline Val version_of(const std::vector<std::string>& cmd) {   // first line of `cmd`'s output, or None
    const Proc p = run_proc(cmd, "", {}, 20);
    if (p.os_errno || p.timed_out) return Val();
    const std::string out = strip(decode_utf8_replace(p.out)), err = strip(decode_utf8_replace(p.err));
    const auto lines = splitlines(!out.empty() ? out : err);
    return lines.empty() ? Val() : Val::str(lines[0]);
}
inline Val doctor() {
    Val report = Val::dict();
    report.d->set("engine", Val::str("A.S.T.R.O AI built-in tools")); report.d->set("platform", Val::str(platform_terse()));
    try {
        ffmpeg_version();
        Val ff = Val::dict(); ff.d->set("path", Val::str(ffmpeg_bin())); ff.d->set("version", Val::str(cache().version_line)); ff.d->set("ffprobe", Val::str(ffprobe_bin()));
        report.d->set("ffmpeg", ff);
        const auto& filters = ffmpeg_filters();
        Val f = Val::dict(); for (const auto& n : KEY_FILTERS) f.d->set(n, Val::boolean(filters.count(n) > 0));
        report.d->set("filters", f);
        report.d->set("alimiter_latency", Val::boolean(filter_options("alimiter").count("latency") > 0));
        const auto& enc = ffmpeg_encoders();
        Val e = Val::dict(); for (const auto& n : KEY_ENCODERS) e.d->set(n, Val::boolean(enc.count(n) > 0));
        report.d->set("encoders", e);
    } catch (const EditError& err) {
        Val ff = Val::dict(); ff.d->set("error", Val::str(err.what())); report.d->set("ffmpeg", ff);
    }
    const std::string wb = whisper_bin();
    report.d->set("whisper_cpp", wb.empty() ? Val() : Val::str(wb));
    const Val node = which("node").empty() ? Val() : version_of({"node", "--version"});
    report.d->set("node", node);
    long long major = 0;
    if (node.t == Val::STR) { const std::string d = re::sub(R"(\D)", "", split(node.s, ".")[0]); if (!d.empty() && d.size() < 12) major = std::stoll(master::ascii_digits(d)); }
    report.d->set("hyperframes_ready", Val::boolean(major >= 22));
    report.d->set("npx", Val::boolean(!which("npx").empty()));
    return report;
}
inline std::string describe_doctor(const Val& report) {
    std::vector<std::string> out{pystr(get(report, "engine")) + " on " + pystr(get(report, "platform"))};
    const Val ff = get(report, "ffmpeg");
    if (has(ff, "error")) out.push_back("ffmpeg: MISSING (" + pystr(get(ff, "error")) + ")");
    else {
        out.push_back("ffmpeg: " + pystr(get(ff, "version")) + " (" + pystr(get(ff, "path")) + ")");
        std::vector<std::string> missing; for (const auto& kv : get(report, "filters").d->items) if (!kv.second.b) missing.push_back(kv.first);
        out.push_back("  filters missing: " + (missing.empty() ? std::string("none") : join(missing, ", ")));
        if (!(get(get(report, "filters"), "ass").b || get(get(report, "filters"), "subtitles").b))
            out.push_back("  captions: no libass, so captions --burn cannot burn in here; use the HyperFrames route or a libass ffmpeg (see SKILL.md)");
        if (!get(report, "alimiter_latency").b) out.push_back("  alimiter has no latency option: finish compensates the 5 ms delay itself");
        std::vector<std::string> me; for (const auto& kv : get(report, "encoders").d->items) if (!kv.second.b) me.push_back(kv.first);
        out.push_back("  encoders missing: " + (me.empty() ? std::string("none") : join(me, ", ")));
    }
    const Val wb = get(report, "whisper_cpp");
    out.push_back("whisper.cpp: " + (wb.t == Val::STR ? wb.s : std::string("not found (captions needs --words, or install whisper.cpp so whisper-cli is on PATH)")));
    const Val node = get(report, "node");
    if (truthy(node)) out.push_back("node " + pystr(node) + ": " + (get(report, "hyperframes_ready").b ? "ok for HyperFrames" : "HyperFrames needs Node 22+"));
    else out.push_back("node: not found (HyperFrames and Remotion need Node; HyperFrames needs 22+)");
    return join(out, "\n");
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("probe", "Probe media for editing, or check this machine's editing toolchain.");
    p.epilog = EXIT_CODES_HELP; p.raw_description = true;
    p.pos("files").nargs("*").help("media files to probe");
    p.opt({"--doctor"}).store_true().help("report ffmpeg filters, encoders, whisper.cpp and Node availability");
    p.opt({"--loudness"}).store_true().help("also measure integrated loudness and true peak (decodes the audio)");
    p.opt({"--json"}).store_true().help("print JSON");
    const Val a = parse_args(p, argv);
    std::vector<std::string> files; for (const auto& f : master::iterate(get(a, "files"))) files.push_back(f.s);
    const bool doc = B(a, "doctor"), as_json = B(a, "json"), loudness = B(a, "loudness");
    if (files.empty() && !doc) ap::fail(ap::norm(p, p.prog), "give a media file or --doctor");
    return run_main([&]() -> int {
        Val results = Val::dict();
        if (doc) results.d->set("doctor", doctor());
        Val probes = Val::list();
        for (const auto& path : files) {
            Val info = probe(path);
            if (loudness && truthy(get(info, "audio"))) info.d->set("loudness", measure_loudness(path));
            info.d->set("notes", notes_for(info));
            probes.l->push_back(info);
        }
        if (as_json) {
            if (doc && probes.l->empty()) print(json_print(get(results, "doctor")));
            else if (!doc && probes.l->size() == 1) print(json_print((*probes.l)[0]));
            else { results.d->set("files", probes); print(json_print(results)); }
        } else {
            if (doc) print(describe_doctor(get(results, "doctor")));
            for (const auto& info : *probes.l) print(describe(info));
        }
        if (doc && has(get(get(results, "doctor"), "ffmpeg"), "error")) return EXIT_MISSING;
        return EXIT_OK;
    });
}
PACKTOOL("master-edit/probe", main);
}  // namespace packtools::master_probe
