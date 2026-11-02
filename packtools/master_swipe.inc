// master-virality/swipe - rank collected videos by how far each beat its own channel's median, then name the hook
// formula (from tools/hook-formulas.json) the hook or title matches.
// C++ port of master-skills/skills/virality/tools/swipe.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::master_swipe {
using namespace pyrt;
using namespace packtools::master;

inline const char* DOC = R"DOC(swipe - rank collected videos by how far each beat its OWN channel, then name the formula.

    astro-server tool master-virality/swipe collected.json
    astro-server tool master-virality/swipe collected.json --min 2.0 --json

Input is a list you collected - one object per video:

    [{"channel":"Some Channel","title":"...","views":412000,"url":"...","duration":613}, ...]

Raw view counts rank channel size, not ideas. A 400k video on a 2M-subscriber channel is a normal
Tuesday; a 400k video on a channel whose median is 30k is the thing worth studying. So every video
is scored as a MULTIPLE OF ITS OWN CHANNEL'S MEDIAN, which needs at least four videos per channel
to mean anything - the tool says so rather than quietly ranking on noise.

Input can be collect output. The formula comes from hook-formulas.json in this folder, matched
against the HOOK when there is one, else the TITLE. It is a judgement
about the words on screen, not a claim about why the video worked.
)DOC";

// most matching patterns wins; ties go to the larger name (Python sorts (count, name) descending)
inline std::string classify(const List& formulas, const Val& title) {
    long long best_n = 0; std::string best;
    for (const auto& f : formulas) {
        long long n = 0;
        for (const auto& p : iterate(get(f, "match"))) if (re::search(re_text(p), re_text(title), re::I)) ++n;
        if (!n) continue;
        const std::string name = pystr(get(f, "name"));
        if (n > best_n || (n == best_n && name > best)) { best_n = n; best = name; }
    }
    return best_n ? best : "Unclassified";
}

inline double median(std::vector<double> v) {
    std::sort(v.begin(), v.end()); const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}
inline std::string slice_str(const Val& v, long n) {   // str(x[:n]) for the f-string columns
    if (v.t == Val::STR) return cpslice(v.s, 0, n);
    if (v.t == Val::LIST) { List l(v.l->begin(), v.l->begin() + std::min<long>(n, static_cast<long>(v.l->size()))); return repr(Val::list(l)); }
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not subscriptable");
}

// master: the virality copy (tools/hook-formulas.json, -h/--help, posts without views left out, hook before title);
// the YouTube Pack original (yt-viral/swipe) reads yt-script's formulas and classifies the title only
struct Cfg { const char* doc; std::string formulas; bool master; };

struct Out { Val channel, title, url, multiple; long long views = 0, median = 0; std::string formula; };

inline int main_with(const std::vector<std::string>& argv, const Cfg& cfg) {
    const List formulas = iterate(get(loads(read_text_utf8(skill_file(cfg.formulas))), "hooks"));   // loaded first, like the original's module import
    std::vector<std::string> a = argv;
    if (cfg.master && (index_of(a, "-h") >= 0 || index_of(a, "--help") >= 0)) { print(cfg.doc); return 0; }
    const bool as_json = index_of(a, "--json") >= 0;
    a.erase(std::remove(a.begin(), a.end(), std::string("--json")), a.end());
    double lo = 1.5;
    if (const int k = index_of(a, "--min"); k >= 0) lo = to_float(Val::str(arg_after(a, k)));
    std::vector<std::string> files;
    for (const auto& x : a) if (!starts(x, "--") && !re::match(R"(^[\d.]+$)", x)) files.push_back(x);
    if (files.empty() || !exists(files[0])) { print(cfg.doc); return 1; }
    Val data = loads(read_text_utf8(files[0]));
    if (data.t == Val::DICT) data = data.d->at("videos", Val::list());
    List rows;
    // Posts with no count are left out, not counted as zero, or they drag the median down.
    for (const auto& r : iterate(data)) {
        if (cfg.master) {
            const Val v = as_dict(r).at("views");
            if (v.is_none() || (v.t == Val::STR && v.s.empty())) continue;
        }
        rows.push_back(r);
    }
    std::vector<std::pair<Val, List>> by;
    for (const auto& r : rows) {
        const Val ch = as_dict(r).has("channel") ? r.d->at("channel") : Val::str("?");
        need_hashable(ch);
        auto it = std::find_if(by.begin(), by.end(), [&](const auto& g) { return key_eq(g.first, ch); });
        if (it == by.end()) by.emplace_back(ch, List{r}); else it->second.push_back(r);
    }
    std::vector<Out> out; std::vector<std::pair<Val, long long>> thin;
    auto views_of = [](const Val& v) { const Val x = v.d->at("views", Val::integer(0)); return truthy(x) ? x : Val::integer(0); };
    for (const auto& [ch, vids] : by) {
        std::vector<double> views; for (const auto& v : vids) views.push_back(to_float(views_of(v)));
        const double med = median(views);
        if (vids.size() < 4) { thin.emplace_back(ch, static_cast<long long>(vids.size())); continue; }
        for (const auto& v : vids) {
            Out o; o.channel = ch; o.title = v.d->at("title", Val::str(""));
            o.multiple = med != 0.0 ? Val::flt(round_n(to_float(views_of(v)) / med, 2)) : Val::integer(0);
            o.views = to_int(views_of(v)); o.median = to_int(Val::flt(med));
            const Val hook = cfg.master ? v.d->at("hook") : Val();
            o.formula = classify(formulas, truthy(hook) ? hook : v.d->at("title", Val::str("")));
            o.url = v.d->at("url", Val::str(""));
            out.push_back(o);
        }
    }
    std::vector<Out> kept; for (auto& o : out) if (o.multiple.num() >= lo) kept.push_back(o);
    std::stable_sort(kept.begin(), kept.end(), [](const Out& x, const Out& y) { return -x.multiple.num() < -y.multiple.num(); });
    if (as_json) {
        Val ol = Val::list();
        for (const auto& o : kept) {
            Val d = Val::dict(); d.d->set("channel", o.channel); d.d->set("title", o.title); d.d->set("views", Val::integer(o.views));
            d.d->set("median", Val::integer(o.median)); d.d->set("multiple", o.multiple); d.d->set("formula", Val::str(o.formula)); d.d->set("url", o.url);
            ol.l->push_back(d);
        }
        Val tl = Val::list(); for (const auto& [c, n] : thin) tl.l->push_back(Val::list({c, Val::integer(n)}));
        Val res = Val::dict(); res.d->set("outliers", ol); res.d->set("skipped_thin_channels", tl);
        DumpOpts op; op.indent = 1; print(dumps(res, op)); return 0;
    }
    print("\n  " + std::to_string(rows.size()) + " videos across " + std::to_string(by.size()) + " channels, outliers at " + float_repr(lo) + "x or better\n");
    for (std::size_t k = 0; k < kept.size() && k < 25; ++k) {
        const Out& r = kept[k];
        print("    " + rjust(fmt_f(r.multiple.num(), 2), 5) + "x  " + rjust(commas(std::to_string(r.views)), 9) + "  vs " + rjust(commas(std::to_string(r.median)), 9) +
              " median   " + ljust(slice_str(r.channel, 22), 22) + " " + slice_str(r.title, 52));
        print("            " + r.formula);
    }
    if (kept.empty()) print("    nothing cleared the threshold - collect more per channel or lower --min");
    if (!thin.empty()) {
        print("\n  skipped " + std::to_string(thin.size()) + " channel(s) with under 4 videos collected - a median off one or");
        std::vector<std::string> parts;
        for (std::size_t k = 0; k < thin.size() && k < 6; ++k) parts.push_back(pystr(thin[k].first) + " (" + std::to_string(thin[k].second) + ")");
        print("  two videos is not a median: " + join(parts, ", "));
    }
    std::vector<std::pair<std::string, long long>> counts;
    for (const auto& r : kept) {
        auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == r.formula; });
        if (it == counts.end()) counts.emplace_back(r.formula, 1); else ++it->second;
    }
    if (!counts.empty()) {
        print("\n  formulas among the outliers");
        std::stable_sort(counts.begin(), counts.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
        for (const auto& [f, n] : counts) print("    " + rjust(std::to_string(n), 2) + "x  " + f);
    }
    print();
    return 0;
}
inline int main(const std::vector<std::string>& argv) { return main_with(argv, Cfg{DOC, "tools/hook-formulas.json", true}); }
PACKTOOL("master-virality/swipe", main);
}  // namespace packtools::master_swipe
