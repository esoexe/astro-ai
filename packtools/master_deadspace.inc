// master_deadspace.inc - master-skills edit/tools/deadspace.py: cut dead space (silences, plus ums and retakes when word
// timings are given). A 10 ms speech envelope with an adaptive gate finds pauses; each cut keeps a breath pad on both sides;
// keep ranges snap to the video frame grid; audio joins get short equal-power crossfades while video stays frame-accurate.
// Writes an edit decision list (JSON); with a transcript also the words re-timed to the cut.
namespace packtools::master_deadspace {
using namespace pyrt;
using namespace packtools::master_mk;

struct Pace { double min_silence, pad_before, pad_after, min_cut; };
inline const std::map<std::string, Pace>& paces() {
    static const std::map<std::string, Pace> p{{"tight", {0.25, 0.06, 0.10, 0.10}}, {"normal", {0.35, 0.08, 0.14, 0.12}}, {"loose", {0.60, 0.12, 0.22, 0.20}}};
    return p;
}
inline const char* DEFAULT_FILLERS = "um,umm,ummm,uh,uhh,uhm,er,erm,ah,ahh,hmm,hm,mm,mmm";
inline bool is_video_ext(const std::string& e) { return e == ".mp4" || e == ".mov" || e == ".m4v" || e == ".mkv"; }
inline const std::map<std::string, std::vector<std::string>>& audio_codecs() {
    static const std::map<std::string, std::vector<std::string>> m{
        {".wav", {"-c:a", "pcm_s24le"}}, {".flac", {"-c:a", "flac"}}, {".m4a", {"-c:a", "aac", "-b:a", "256k"}}, {".aac", {"-c:a", "aac", "-b:a", "256k"}},
        {".mp3", {"-c:a", "libmp3lame", "-q:a", "2"}}, {".opus", {"-c:a", "libopus", "-b:a", "160k"}}};
    return m;
}

// ---------------------------------------------------------------- numbers that remember int vs float (JSON EDLs may hold ints)
struct Num { double v = 0; bool is_int = false; };
inline Num num_of(const Val& x) { return x.t == Val::INT ? Num{static_cast<double>(x.i), true} : Num{x.num(), false}; }
inline Val num_val(const Num& n) { return n.is_int ? Val::integer(static_cast<long long>(n.v)) : Val::flt(n.v); }
inline Num nmax(const Num& a, const Num& b) { return b.v > a.v ? b : a; }   // max(a, b): the first on ties
inline Num nmin(const Num& a, const Num& b) { return b.v < a.v ? b : a; }
inline Num nround(const Num& n, int nd) { return n.is_int ? n : Num{round_n(n.v, nd), false}; }

// ---------------------------------------------------------------- interval helpers
inline Val make_cut(double s, double e, const std::string& reason, const Val& text) {
    Val c = Val::dict(); c.d->set("start", Val::flt(s)); c.d->set("end", Val::flt(e)); c.d->set("reason", Val::str(reason)); c.d->set("text", text); return c;
}
inline Val copy_dict(const Val& d) { Val c = Val::dict(); if (d.t == Val::DICT) c.d->items = d.d->items; return c; }
// union of cut dicts {start, end, reason, text}; overlapping cuts merge their reasons
inline List merge_cuts(const List& cuts) {
    List sorted = cuts;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Val& a, const Val& b) { return get(a, "start").num() < get(b, "start").num(); });
    List out;
    for (const auto& cut : sorted) {
        if (!out.empty() && get(cut, "start").num() <= get(out.back(), "end").num() + 1e-6) {
            Val& last = out.back();
            last.d->set("end", num_val(nmax(num_of(get(last, "end")), num_of(get(cut, "end")))));
            auto reasons = split(pystr(get(last, "reason")), "+");
            const std::string r = pystr(get(cut, "reason"));
            if (std::find(reasons.begin(), reasons.end(), r) == reasons.end()) { reasons.push_back(r); last.d->set("reason", Val::str(join(reasons, "+"))); }
            if (truthy(get(cut, "text"))) {
                const Val lt = get(last, "text");
                last.d->set("text", Val::str(strip((truthy(lt) ? pystr(lt) : std::string()) + " " + pystr(get(cut, "text")))));
            }
        } else out.push_back(copy_dict(cut));
    }
    return out;
}
struct Keep { Num s, e; };
inline std::vector<Keep> keeps_from_cuts(const List& cuts, double start, double end) {
    List applied; for (const auto& c : cuts) if (truthy(get(c, "apply", Val::boolean(true)))) applied.push_back(c);
    std::vector<Keep> keeps; Num cursor{start, false}; const Num nstart{start, false}, nend{end, false};
    for (const auto& cut : merge_cuts(applied)) {
        const Num cs = nmax(nstart, num_of(get(cut, "start"))), ce = nmin(nend, num_of(get(cut, "end")));
        if (ce.v <= cs.v) continue;
        if (cs.v > cursor.v) keeps.push_back({cursor, cs});
        cursor = nmax(cursor, ce);
    }
    if (end > cursor.v) keeps.push_back({cursor, nend});
    return keeps;
}
struct Silences { std::vector<std::pair<double, double>> runs; std::optional<double> first, last; };
// silent runs as (start, end) seconds from the first sample, plus the first and last sound
inline Silences find_silences(const std::vector<double>& levels, double hop, double threshold, double min_sound = 0.06) {
    struct Run { bool loud; long long i, j; };
    std::vector<Run> runs;
    std::size_t i = 0;
    while (i < levels.size()) {
        std::size_t j = i; const bool l = levels[i] >= threshold;
        while (j < levels.size() && (levels[j] >= threshold) == l) j++;
        runs.push_back({l, static_cast<long long>(i), static_cast<long long>(j)});
        i = j;
    }
    for (std::size_t k = 0; k < runs.size(); ++k)   // a click shorter than min_sound inside silence is still silence
        if (runs[k].loud && k > 0 && k + 1 < runs.size() && static_cast<double>(runs[k].j - runs[k].i) * hop < min_sound) runs[k].loud = false;
    std::vector<Run> merged;
    for (const auto& r : runs) { if (!merged.empty() && merged.back().loud == r.loud) merged.back().j = r.j; else merged.push_back(r); }
    Silences s;
    std::vector<const Run*> sound; for (const auto& r : merged) if (r.loud) sound.push_back(&r);
    if (sound.empty()) return s;
    for (const auto& r : merged) if (!r.loud) s.runs.emplace_back(static_cast<double>(r.i) * hop, static_cast<double>(r.j) * hop);
    s.first = static_cast<double>(sound.front()->i) * hop; s.last = static_cast<double>(sound.back()->j) * hop;
    return s;
}

// ---------------------------------------------------------------- transcript cuts
inline std::string norm_token(const std::string& text) { return master::strip_chars(re::sub(R"([^\w']+)", "", lower(text)), "'"); }
// difflib.SequenceMatcher(None, a, b).ratio() for short token lists (no autojunk below 200 items)
inline double seq_ratio(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::map<std::string, std::vector<std::size_t>> b2j;
    for (std::size_t j = 0; j < b.size(); ++j) b2j[b[j]].push_back(j);
    auto longest = [&](std::size_t alo, std::size_t ahi, std::size_t blo, std::size_t bhi) {
        std::size_t besti = alo, bestj = blo, bestsize = 0;
        std::map<std::size_t, std::size_t> j2len;
        for (std::size_t i = alo; i < ahi; ++i) {
            std::map<std::size_t, std::size_t> nj;
            const auto it = b2j.find(a[i]);
            if (it != b2j.end()) for (const std::size_t j : it->second) {
                if (j < blo) continue;
                if (j >= bhi) break;
                const auto pj = j > 0 ? j2len.find(j - 1) : j2len.end();
                const std::size_t k = (pj != j2len.end() ? pj->second : 0) + 1;
                nj[j] = k;
                if (k > bestsize) { besti = i - k + 1; bestj = j - k + 1; bestsize = k; }
            }
            j2len = std::move(nj);
        }
        while (besti > alo && bestj > blo && a[besti - 1] == b[bestj - 1]) { besti--; bestj--; bestsize++; }
        while (besti + bestsize < ahi && bestj + bestsize < bhi && a[besti + bestsize] == b[bestj + bestsize]) bestsize++;
        return std::array<std::size_t, 3>{besti, bestj, bestsize};
    };
    std::size_t matches = 0;
    std::vector<std::array<std::size_t, 4>> queue{{0, a.size(), 0, b.size()}};
    while (!queue.empty()) {
        const auto q = queue.back(); queue.pop_back();
        const auto m = longest(q[0], q[1], q[2], q[3]);
        if (!m[2]) continue;
        matches += m[2];
        if (q[0] < m[0] && q[2] < m[1]) queue.push_back({q[0], m[0], q[2], m[1]});
        if (m[0] + m[2] < q[1] && m[1] + m[2] < q[3]) queue.push_back({m[0] + m[2], q[1], m[1] + m[2], q[3]});
    }
    const std::size_t la = a.size() + b.size();
    return la ? 2.0 * static_cast<double>(matches) / static_cast<double>(la) : 1.0;
}
// cut boundaries in the gap before word k: (left edge keeping word k-1's pad, right edge keeping word k's pad)
inline std::pair<double, double> edges(const std::vector<Word>& words, long long k, double pad_before, double pad_after, double t0, double t1) {
    if (k <= 0) return {t0, std::max(t0, words[0].start - pad_before)};
    if (k >= static_cast<long long>(words.size())) return {std::min(t1, words.back().end + pad_after), t1};
    const double prev_end = words[static_cast<std::size_t>(k - 1)].end, start = words[static_cast<std::size_t>(k)].start;
    const double gap = std::max(0.0, start - prev_end);
    return {prev_end + std::min(pad_after, gap / 2), start - std::min(pad_before, gap / 2)};
}
inline List transcript_cuts(const std::vector<Word>& words, const std::set<std::string>& fillers, const std::string& retakes,
                            double pad_before, double pad_after, double t0, double t1) {
    List cuts;
    for (std::size_t k = 0; k < words.size(); ++k) {
        if (!fillers.count(norm_token(words[k].text))) continue;
        const double left = edges(words, static_cast<long long>(k), pad_before, pad_after, t0, t1).first;
        const double right = edges(words, static_cast<long long>(k) + 1, pad_before, pad_after, t0, t1).second;
        if (right > left) cuts.push_back(make_cut(left, right, "filler", Val::str(words[k].text)));
    }
    if (retakes == "off") return cuts;
    std::vector<std::size_t> idx; std::vector<std::string> tokens;
    for (std::size_t k = 0; k < words.size(); ++k) { const std::string t = norm_token(words[k].text); if (!fillers.count(t) && !t.empty()) { idx.push_back(k); tokens.push_back(t); } }
    for (std::size_t j = 1; j < tokens.size(); ++j) {
        std::size_t found = 0;
        for (std::size_t length = std::min<std::size_t>(25, j); length > 1; --length) {   // longest abandoned attempt first
            const std::vector<std::string> a(tokens.begin() + static_cast<long>(j - length), tokens.begin() + static_cast<long>(j));
            const std::vector<std::string> b(tokens.begin() + static_cast<long>(j), tokens.begin() + static_cast<long>(std::min(tokens.size(), j + length)));
            const Word& first = words[idx[j - length]]; const Word& restart = words[idx[j]];
            if (restart.start - first.start > 15.0) continue;
            if (a == b) { found = length; break; }
            if (retakes == "fuzzy" && length >= 4 && b.size() == length && a[0] == b[0] && a[1] == b[1] &&
                restart.start - words[idx[j - 1]].end >= 0.25 && seq_ratio(a, b) >= 0.8) { found = length; break; }
        }
        if (!found) continue;
        const double left = edges(words, static_cast<long long>(idx[j - found]), pad_before, pad_after, t0, t1).first;
        const double right = edges(words, static_cast<long long>(idx[j]), pad_before, pad_after, t0, t1).second;
        std::vector<std::string> t; for (std::size_t m = j - found; m < j; ++m) t.push_back(words[idx[m]].text);
        if (right > left) cuts.push_back(make_cut(left, right, "retake", Val::str(join(t, " "))));
    }
    return cuts;
}
// move word times from the source onto the cut timeline; words inside cuts are dropped
inline std::vector<Word> remap_words(const std::vector<Word>& words, const std::vector<std::pair<double, double>>& keeps) {
    struct Seg { double s, e, out; };
    std::vector<Seg> segs; double offset = 0.0;
    for (const auto& [s, e] : keeps) { segs.push_back({s, e, offset}); offset += e - s; }
    std::vector<Word> out;
    for (const auto& w : words) {
        const double mid = (w.start + w.end) / 2;
        for (const auto& g : segs) {
            if (g.s <= mid && mid < g.e) {
                const double ws = g.out + std::max(w.start, g.s) - g.s, we = g.out + std::min(w.end, g.e) - g.s;
                Word item = w; item.start = round_n(ws, 3); item.end = round_n(std::max(we, ws + 0.01), 3);
                out.push_back(item);
                break;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- analysis
struct Settings { std::string pace, retakes; double min_silence, pad_before, pad_after, min_cut, crossfade_ms; std::optional<double> threshold;
                  long long audio_stream, crf; std::set<std::string> filler_set; };
struct Analysis { Val levels, threshold_db; List cuts; double start = 0.0, end = 0.0; };
inline Analysis analyze(const std::string& path, const Val& info, const Settings& a, const std::optional<std::vector<Word>>& words) {
    const Val audio = get(info, "audio_streams")[static_cast<std::size_t>(a.audio_stream)];
    const double a_off = get(audio, "offset").num();
    const double t0 = 0.0;
    const Val endv = truthy(get(info, "end")) ? get(info, "end") : get(info, "duration");
    const double t1 = endv.num();
    const auto levels = audio_envelope(path, info, a.audio_stream, 0.01);
    const SpeechLevels stats = speech_levels(levels);
    const double threshold = a.threshold ? *a.threshold : stats.threshold;
    if (!stats.reliable && !a.threshold)
        warn("speech is only " + float_repr(stats.spread) + " dB above the noise floor; detection is unreliable here, pass --transcript or --threshold");
    Silences sil = find_silences(levels, 0.01, threshold);
    List cuts;
    if (!stats.reliable && !a.threshold) {
        // no trustworthy gate: cut nothing by level rather than guess
    } else if (!sil.first) {
        warn("no speech found above the gate; nothing to cut");
    } else {
        const double first = *sil.first + a_off, last = *sil.last + a_off;
        const double head_end = first - a.pad_before;
        if (head_end - t0 >= a.min_cut) cuts.push_back(make_cut(t0, head_end, "silence", Val()));
        const double tail_start = last + a.pad_after;
        if (t1 - tail_start >= a.min_cut) cuts.push_back(make_cut(tail_start, t1, "silence", Val()));
        for (auto [s, e] : sil.runs) {
            s += a_off; e += a_off;
            if (s <= first || e >= last || e - s < a.min_silence) continue;
            const double cs = s + a.pad_after, ce = e - a.pad_before;
            if (ce - cs >= a.min_cut) cuts.push_back(make_cut(cs, ce, "silence", Val()));
        }
    }
    if (words) {
        std::vector<Word> shifted = *words;
        for (auto& w : shifted) { w.start += a_off; w.end += a_off; }
        List guarded;
        for (const auto& cut : cuts) {   // never cut through a transcribed word that has real energy
            std::vector<std::pair<double, double>> pieces{{get(cut, "start").num(), get(cut, "end").num()}};
            for (const auto& w : shifted) {
                const double core_s = w.start + 0.06, core_e = w.end - 0.06;
                if (core_e <= core_s || a.filler_set.count(norm_token(w.text))) continue;
                const long long lo = static_cast<long long>((core_s - a_off) / 0.01), hi = static_cast<long long>((core_e - a_off) / 0.01) + 1;
                const long long n = static_cast<long long>(levels.size());
                const long long b0 = std::min(std::max<long long>(0, lo), n), b1 = std::min(std::max<long long>(0, hi), n);
                if (b1 <= b0 || *std::max_element(levels.begin() + b0, levels.begin() + b1) < stats.noise + 6) continue;
                std::vector<std::pair<double, double>> nxt;
                for (const auto& [ps, pe] : pieces) {
                    if (core_e <= ps || core_s >= pe) { nxt.emplace_back(ps, pe); continue; }
                    if (core_s - ps >= a.min_cut) nxt.emplace_back(ps, core_s);
                    if (pe - core_e >= a.min_cut) nxt.emplace_back(core_e, pe);
                }
                pieces = std::move(nxt);
            }
            for (const auto& [ps, pe] : pieces) { Val c = copy_dict(cut); c.d->set("start", Val::flt(ps)); c.d->set("end", Val::flt(pe)); guarded.push_back(c); }
        }
        for (const auto& c : transcript_cuts(shifted, a.filler_set, a.retakes, a.pad_before, a.pad_after, t0, t1)) guarded.push_back(c);
        cuts = std::move(guarded);
    }
    Analysis r; r.levels = levels_val(stats); r.threshold_db = Val::flt(round_n(threshold, 1)); r.cuts = std::move(cuts); r.start = t0; r.end = t1;
    return r;
}
struct Grid { std::string rate; std::vector<std::array<long long, 2>> frames; };
// snap keep ranges outward to the CFR frame grid; drop cuts that leave slivers
inline std::pair<std::vector<Keep>, std::optional<Grid>> snap(const std::vector<Keep>& keeps, const Val& info, double min_keep) {
    const Val video = get(info, "video");
    if (!truthy(video)) {
        std::vector<Keep> out;
        for (const auto& k : keeps) if (k.e.v - k.s.v > 1e-3) out.push_back({nround(k.s, 6), nround(k.e, 6)});
        return {out, std::nullopt};
    }
    const Frac rate = cfr_rate(video);
    const double fr = rate.f();
    const Frac g0 = Frac(round_int(get(video, "offset").num() * fr)) / rate;
    const double g0f = g0.f();
    const long long total = static_cast<long long>(std::floor((get(info, "end").num() - g0f) * fr + 1e-6));
    std::vector<std::array<long long, 2>> frames;
    for (const auto& k : keeps) {
        const long long a = std::max<long long>(0, static_cast<long long>(std::floor((k.s.v - g0f) * fr + 1e-6)));
        const long long m = std::min<long long>(total, static_cast<long long>(std::ceil((k.e.v - g0f) * fr - 1e-6)));
        if (m <= a) continue;
        if (!frames.empty() && a <= frames.back()[1]) frames.back()[1] = std::max(frames.back()[1], m);
        else frames.push_back({a, m});
    }
    const long long min_frames = std::max<long long>(2, static_cast<long long>(std::ceil(min_keep * fr)));
    bool changed = true;
    while (changed && frames.size() > 1) {   // merge slivers into a neighbour by removing the smaller cut
        changed = false;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const long long k = frames[i][0], m = frames[i][1];
            if (m - k >= min_frames) continue;
            const std::optional<long long> gap_l = i > 0 ? std::optional<long long>(k - frames[i - 1][1]) : std::nullopt;
            const std::optional<long long> gap_r = i + 1 < frames.size() ? std::optional<long long>(frames[i + 1][0] - m) : std::nullopt;
            if (!gap_r || (gap_l && *gap_l <= *gap_r)) frames[i - 1][1] = m;
            else frames[i + 1][0] = k;
            frames.erase(frames.begin() + static_cast<long>(i));
            changed = true;
            break;
        }
    }
    std::vector<Keep> secs;
    for (const auto& f : frames) secs.push_back({{(g0 + Frac(f[0]) / rate).f(), false}, {(g0 + Frac(f[1]) / rate).f(), false}});
    return {secs, Grid{rate.str(), frames}};
}

// ---------------------------------------------------------------- render
inline void render(const std::string& path, const Val& info, const std::vector<Keep>& keeps, const std::optional<Grid>& grid, const std::string& out, const Settings& a) {
    const std::string ext = lower(splitext(out).second);
    const bool has_video = truthy(get(info, "video")) && is_video_ext(ext);
    if (truthy(get(info, "video")) && !has_video && !audio_codecs().count(ext))
        throw EditError("unsupported output extension " + ext + "; use .mp4/.mov/.mkv or an audio type", EXIT_USAGE);
    const double xf = a.crossfade_ms / 1000.0;
    const std::size_t n = keeps.size();
    const std::string as = "0:a:" + std::to_string(a.audio_stream);
    std::vector<std::string> parts;
    auto I = [](std::size_t i) { return std::to_string(i); };
    if (has_video) {
        const std::string& rate = grid->rate;
        if (n == 1) {
            parts.push_back("[0:v:0]fps=" + rate + ",trim=start_frame=" + std::to_string(grid->frames[0][0]) + ":end_frame=" + std::to_string(grid->frames[0][1]) + ",setpts=PTS-STARTPTS[v]");
        } else {
            std::string sp = "[0:v:0]fps=" + rate + ",split=" + I(n); for (std::size_t i = 0; i < n; ++i) sp += "[s" + I(i) + "]";
            parts.push_back(sp);
            for (std::size_t i = 0; i < grid->frames.size(); ++i)
                parts.push_back("[s" + I(i) + "]trim=start_frame=" + std::to_string(grid->frames[i][0]) + ":end_frame=" + std::to_string(grid->frames[i][1]) + ",setpts=PTS-STARTPTS[v" + I(i) + "]");
            std::string cc; for (std::size_t i = 0; i < n; ++i) cc += "[v" + I(i) + "]";
            parts.push_back(cc + "concat=n=" + I(n) + ":v=1:a=0[v]");
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        const double s2 = keeps[i].s.v - (i > 0 ? xf / 2 : 0), e2 = keeps[i].e.v + (i < n - 1 ? xf / 2 : 0);
        parts.push_back("[" + as + "]atrim=start=" + fmt_f(s2, 6) + ":end=" + fmt_f(e2, 6) + ",asetpts=PTS-STARTPTS[a" + I(i) + "]");
    }
    std::string cur = "a0";
    for (std::size_t i = 1; i < n; ++i) { parts.push_back("[" + cur + "][a" + I(i) + "]acrossfade=d=" + fmt_f(xf, 4) + ":c1=qsin:c2=qsin[x" + I(i) + "]"); cur = "x" + I(i); }
    double total = 0; for (const auto& k : keeps) total += k.e.v - k.s.v;
    std::vector<std::string> fades;
    if (keeps.front().s.v > 0.001) fades.push_back("afade=t=in:d=0.005");
    if (keeps.back().e.v < get(info, "end").num() - 0.001) fades.push_back("afade=t=out:st=" + fmt_f(std::max(0.0, total - 0.005), 6) + ":d=0.005");
    parts.push_back("[" + cur + "]" + (fades.empty() ? std::string("anull") : join(fades, ",")) + "[a]");
    const std::string graph = join(parts, ";");
    std::vector<std::string> cmd{ffmpeg_bin(), "-nostdin", "-hide_banner", "-v", "error", "-y", "-i", path};
    if (graph.size() > 100000) {
        const std::string script = out + ".graph.txt";
        write_text(script, graph);
        const auto ver = ffmpeg_version();
        if (!ver || ver->first >= 7) { cmd.push_back("-/filter_complex"); } else { cmd.push_back("-filter_complex_script"); }
        cmd.push_back(script);
    } else { cmd.push_back("-filter_complex"); cmd.push_back(graph); }
    if (has_video) {
        const auto vargs = video_codec_args(get(info, "video"), a.crf).first;
        for (const auto& x : std::vector<std::string>{"-map", "[v]", "-map", "[a]"}) cmd.push_back(x);
        cmd.insert(cmd.end(), vargs.begin(), vargs.end());
        /* pin the output rate: ffmpeg 7.0.x falls back to 25 fps after fps+trim(+concat) in a filter_complex, which drops
           frames and drifts A/V (newer builds carry the graph rate through, so this changes nothing there) */
        cmd.push_back("-r"); cmd.push_back(grid->rate);
        for (const auto& x : std::vector<std::string>{"-c:a", "aac", "-b:a", "256k"}) cmd.push_back(x);
        if (ext == ".mp4" || ext == ".mov" || ext == ".m4v") { cmd.push_back("-movflags"); cmd.push_back("+faststart"); }
    } else {
        const auto it = audio_codecs().find(ext);
        if (it == audio_codecs().end()) throw EditError("unsupported audio output " + ext, EXIT_USAGE);
        cmd.push_back("-map"); cmd.push_back("[a]"); cmd.insert(cmd.end(), it->second.begin(), it->second.end());
    }
    cmd.push_back(out);
    run(cmd);
}

// ---------------------------------------------------------------- main
inline int main(const std::vector<std::string>& argv) {
    Parser p("deadspace", "Cut dead space (silences, plus ums and retakes when word timings are given).");
    p.epilog = EXIT_CODES_HELP; p.raw_description = true;
    p.pos("input").help("video or audio file");
    p.opt({"-o", "--output"}).help("output file (default: <input>.cut.<ext>)");
    p.opt({"--edl"}).help("EDL JSON path (default: <output stem>.edl.json)");
    p.opt({"--transcript"}).help("word timings JSON/SRT/VTT for filler + retake cuts");
    p.opt({"--pace"}).choices({"loose", "normal", "tight"}).def("normal").help("preset: tight (shorts), normal (default), loose (talks)");
    p.opt({"--threshold"}).number().help("silence gate in dBFS (default: adaptive)");
    p.opt({"--min-silence"}).number().help("pauses shorter than this are kept (s)");
    p.opt({"--pad-before"}).number().help("air kept before speech starts (s)");
    p.opt({"--pad-after"}).number().help("air kept after speech ends (s)");
    p.opt({"--min-cut"}).number().help("skip cuts shorter than this (s)");
    p.opt({"--crossfade-ms"}).number().def_float(30.0).help("audio crossfade at joins (ms)");
    p.opt({"--fillers"}).def(DEFAULT_FILLERS).help("comma list of filler words to cut");
    p.opt({"--no-fillers"}).store_true().help("do not cut filler words");
    p.opt({"--retakes"}).choices({"off", "exact", "fuzzy"}).def("fuzzy").help("repeated-line detection (needs --transcript); last take wins");
    p.opt({"--audio-stream"}).integer().def(0).help("which audio stream to analyze");
    p.opt({"--dry-run"}).store_true().help("write the EDL only, do not render");
    p.opt({"--from-edl"}).help("render the cuts in this EDL (apply=false vetoes a cut)");
    p.opt({"--crf"}).integer().def(16).help("video quality, lower is better (default 16)");
    p.opt({"--json"}).store_true().help("print a JSON summary");
    const Val args = parse_args(p, argv);

    Settings st;
    st.pace = S(args, "pace");
    const Pace pc = paces().at(st.pace);
    auto opt_num = [&](const char* k) -> std::optional<double> { const Val v = get(args, k); if (v.is_none()) return std::nullopt; return v.num(); };
    st.min_silence = opt_num("min_silence").value_or(pc.min_silence); st.pad_before = opt_num("pad_before").value_or(pc.pad_before);
    st.pad_after = opt_num("pad_after").value_or(pc.pad_after); st.min_cut = opt_num("min_cut").value_or(pc.min_cut);
    st.threshold = opt_num("threshold"); st.crossfade_ms = get(args, "crossfade_ms").num();
    st.retakes = S(args, "retakes"); st.audio_stream = get(args, "audio_stream").i; st.crf = get(args, "crf").i;
    if (!B(args, "no_fillers")) for (const auto& f : split(S(args, "fillers"), ",")) if (!strip(f).empty()) st.filler_set.insert(norm_token(f));
    const std::string input = S(args, "input");
    const Val outv = get(args, "output"), edlv = get(args, "edl"), trv = get(args, "transcript"), fev = get(args, "from_edl");

    return run_main([&]() -> int {
        const Val info = probe(input);
        const std::size_t n_streams = get(info, "audio_streams").size();
        if (!n_streams) throw EditError("no audio stream: dead space is found in the audio. Use a transcript-driven edit or cut by hand.", EXIT_USAGE);
        if (st.audio_stream >= static_cast<long long>(n_streams)) throw EditError("there is no audio stream #" + std::to_string(st.audio_stream), EXIT_USAGE);
        const std::string out = truthy(outv) ? outv.s : default_output(input, ".cut");
        const std::string edl_path = truthy(edlv) ? edlv.s : default_output(out, "", std::string(".edl.json"));
        std::optional<std::vector<Word>> words;
        if (truthy(trv)) { const Val d = get(info, "duration"); words = load_words(trv.s, d.is_none() ? std::nullopt : std::optional<double>(d.num())); }

        Analysis analysis; List cuts;
        if (truthy(fev)) {
            const Val edl = loads(read_text(fev.s));
            const Val c = get(edl, "cuts");
            cuts = truthy(c) && c.t == Val::LIST ? *c.l : List{};
            const Val an = get(edl, "analysis", Val::dict());
            analysis.start = 0.0; analysis.end = get(info, "end").num(); analysis.cuts = cuts;
            analysis.levels = get(an, "levels"); analysis.threshold_db = get(an, "threshold_db");
        } else {
            analysis = analyze(input, info, st, words);
            cuts = merge_cuts(analysis.cuts);
            for (auto& cut : cuts) {
                cut.d->set("start", num_val(nround(num_of(get(cut, "start")), 4))); cut.d->set("end", num_val(nround(num_of(get(cut, "end")), 4)));
                cut.d->set("apply", Val::boolean(true));
            }
        }
        const auto raw_keeps = keeps_from_cuts(cuts, analysis.start, analysis.end);
        if (raw_keeps.empty()) throw EditError("every second would be cut; check --threshold", EXIT_USAGE);
        const double min_keep = std::max(0.07, st.crossfade_ms / 1000.0 + 0.04);
        const auto [keeps, grid] = snap(raw_keeps, info, min_keep);
        const double src_len = analysis.end - analysis.start;
        double out_len = 0; for (const auto& k : keeps) out_len += k.e.v - k.s.v;
        List applied; for (const auto& c : cuts) if (truthy(get(c, "apply", Val::boolean(true)))) applied.push_back(c);
        Val counts = Val::dict();
        for (const auto& cut : applied) for (const auto& r : split(pystr(get(cut, "reason")), "+")) counts.d->set(r, Val::integer((has(counts, r) ? get(counts, r).i : 0) + 1));

        Val edl = Val::dict();
        edl.d->set("version", Val::integer(1)); edl.d->set("tool", Val::str("master-skills edit/deadspace")); edl.d->set("source", get(info, "path"));
        edl.d->set("duration", Val::flt(round_n(src_len, 4))); edl.d->set("fps", grid ? Val::str(grid->rate) : Val());
        Val settings = Val::dict();
        settings.d->set("pace", Val::str(st.pace)); settings.d->set("min_silence", Val::flt(st.min_silence)); settings.d->set("pad_before", Val::flt(st.pad_before));
        settings.d->set("pad_after", Val::flt(st.pad_after)); settings.d->set("min_cut", Val::flt(st.min_cut)); settings.d->set("crossfade_ms", Val::flt(st.crossfade_ms));
        settings.d->set("retakes", Val::str(st.retakes));
        edl.d->set("settings", settings);
        Val an = Val::dict(); an.d->set("levels", analysis.levels); an.d->set("threshold_db", analysis.threshold_db); edl.d->set("analysis", an);
        edl.d->set("note", Val::str("cuts[] is the source of truth: set apply=false to veto a cut, append {start, end, reason: 'manual'} to add one, then run --from-edl."));
        edl.d->set("cuts", Val::list(cuts));
        Val kl = Val::list();
        for (const auto& k : keeps) { Val d = Val::dict(); d.d->set("start", num_val(nround(k.s, 4))); d.d->set("end", num_val(nround(k.e, 4))); kl.l->push_back(d); }
        edl.d->set("keep", kl);
        Val o = Val::dict(); o.d->set("path", Val::str(abspath(out))); o.d->set("duration", Val::flt(round_n(out_len, 4))); o.d->set("saved", Val::flt(round_n(src_len - out_len, 4)));
        edl.d->set("output", o);
        write_json(edl_path, edl);

        Val summary = Val::dict();
        summary.d->set("input", get(info, "path")); summary.d->set("edl", Val::str(abspath(edl_path))); summary.d->set("cuts", Val::integer(static_cast<long long>(applied.size())));
        summary.d->set("by_reason", counts); summary.d->set("source_s", Val::flt(round_n(src_len, 2))); summary.d->set("output_s", Val::flt(round_n(out_len, 2)));
        summary.d->set("saved_s", Val::flt(round_n(src_len - out_len, 2)));
        summary.d->set("saved_pct", Val::flt(src_len ? round_n(100 * (src_len - out_len) / src_len, 1) : 0.0));
        summary.d->set("threshold_db", analysis.threshold_db); summary.d->set("rendered", Val::boolean(false));
        if (!B(args, "dry_run")) {
            std::vector<Keep> kv = keeps;
            render(input, info, kv, grid, out, st);
            const Val result = probe(out);
            summary.d->set("rendered", Val::boolean(true)); summary.d->set("output", get(result, "path"));
            std::vector<double> durations;
            List ss; if (truthy(get(result, "video"))) ss.push_back(get(result, "video"));
            for (const auto& s : master::iterate(get(result, "audio_streams"))) ss.push_back(s);
            for (const auto& s : ss) if (truthy(s) && truthy(get(s, "duration"))) durations.push_back(get(s, "duration").num());
            if (durations.size() == 2) summary.d->set("av_drift_ms", Val::flt(round_n(std::fabs(durations[0] - durations[1]) * 1000, 1)));
            if (words) {
                const std::string words_out = default_output(out, "", std::string(".words.json"));
                const double a_off = get(get(info, "audio_streams")[static_cast<std::size_t>(st.audio_stream)], "offset").num();
                std::vector<Word> shifted = *words; for (auto& w : shifted) { w.start += a_off; w.end += a_off; }
                std::vector<std::pair<double, double>> kp; for (const auto& k : keeps) kp.emplace_back(k.s.v, k.e.v);
                save_words(words_out, remap_words(shifted, kp), out, "remapped by deadspace");
                summary.d->set("words", Val::str(abspath(words_out)));
            }
        }
        if (B(args, "json")) { print(json_print(summary)); return EXIT_OK; }
        std::vector<std::pair<std::string, long long>> sorted; for (const auto& kv : counts.d->items) sorted.emplace_back(kv.first, kv.second.i);
        std::sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return cpcmp(x.first, y.first) < 0; });
        std::vector<std::string> rs; for (const auto& [k, v] : sorted) rs.push_back(k + " " + std::to_string(v));
        print("deadspace: " + std::to_string(applied.size()) + " cuts (" + (rs.empty() ? std::string("none") : join(rs, ", ")) + "), threshold " + pystr(get(summary, "threshold_db")) + " dBFS");
        print("  " + fmt_f(src_len, 2) + " s -> " + fmt_f(out_len, 2) + " s, saved " + fmt_f(get(summary, "saved_s").f, 2) + " s (" + pystr(get(summary, "saved_pct")) + "%)");
        print("  edl: " + edl_path);
        if (get(summary, "rendered").b)
            print("  wrote: " + out + (has(summary, "av_drift_ms") ? "  (A/V drift " + pystr(get(summary, "av_drift_ms")) + " ms)" : std::string()));
        if (has(summary, "words")) print("  words on the cut timeline: " + pystr(get(summary, "words")));
        for (const auto& cut : applied) {
            if (pystr(get(cut, "reason")) == "silence") continue;
            const Val t = get(cut, "text");
            print("  " + rjust(pystr(get(cut, "reason")), 15) + " " + fmt_time(get(cut, "start").num()) + "-" + fmt_time(get(cut, "end").num()) + "  " + (truthy(t) ? pystr(t) : std::string()));
        }
        return EXIT_OK;
    });
}
PACKTOOL("master-edit/deadspace", main);
}  // namespace packtools::master_deadspace
