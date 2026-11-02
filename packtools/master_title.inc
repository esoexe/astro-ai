// master-virality/title - lint a YouTube title and thumbnail (or Reel cover) pairing: length against the desktop and
// mobile cuts, shouting, vague words, a concrete figure, front-loading, and thumbnail text that repeats the title.
// C++ port of master-skills/skills/virality/tools/title.py (Jake Schincariol, MIT); same output, same exit codes.
namespace packtools::master_title {
using namespace pyrt;
using namespace packtools::master;

inline const char* DOC = R"DOC(title - lint a YouTube title and thumbnail pairing before you publish it.

    astro-server tool master-virality/title --title "..." --thumb "AI RAN IT"
    astro-server tool master-virality/title titles.txt            # one per line, ranked
    astro-server tool master-virality/title --title "..." --json

The pairing is the unit, not the title. A title that repeats the thumbnail text wastes half the
click surface, and that is the single most common mistake this checks for.

Length: YouTube truncates around 60 characters on desktop search and around 40 on a mobile home
feed. Both limits are reported because they are different failures - a desktop truncation loses the
tail, a mobile one can lose the subject.
)DOC";

constexpr long long DESKTOP = 60, MOBILE = 40, HARD = 100;
inline const std::set<std::string> VAGUE = {"amazing", "incredible", "insane", "crazy", "huge", "massive", "ultimate", "best", "powerful",
                                            "secret", "revolutionary", "mindblowing", "epic", "perfect", "complete", "everything"};
inline const std::set<std::string> STOP = {"the", "a", "an", "of", "for", "to", "in", "on", "and", "or", "is", "are", "with", "your", "you", "my", "i",
                                           "this", "that", "it", "how", "what", "why"};

inline std::vector<std::string> words(const std::string& t) { return re::findall("[a-z0-9']+", lower(t)); }

struct Row { std::string title; long long chars = 0, score = 0; std::vector<std::pair<std::string, std::string>> issues; std::vector<std::string> good; };

inline Row check(const std::string& title, const std::optional<std::string>& thumb) {
    Row r; const std::string t = strip(title); const long long n = static_cast<long long>(cplen(t));
    auto& issues = r.issues; auto& good = r.good;
    const std::string ns = std::to_string(n);
    if (n > HARD) issues.emplace_back("length", ns + " characters - YouTube's hard limit is " + std::to_string(HARD));
    else if (n > DESKTOP) issues.emplace_back("length", ns + " characters - desktop search cuts near " + std::to_string(DESKTOP));
    else good.push_back(ns + " characters, inside the " + std::to_string(DESKTOP) + "-character desktop cut");
    if (n > MOBILE) {
        std::string head = cpslice(t, 0, MOBILE);
        const std::size_t sp = head.rfind(' ');
        if (sp != std::string::npos) head = head.substr(0, sp);
        issues.emplace_back("mobile", "a mobile feed shows about \"" + head + "...\" - check the subject survives");
    }
    std::size_t caps = 0;
    for (const auto& w : split_ws(t)) if (cplen(w) > 2 && isupper_str(w)) ++caps;
    if (caps > 2) issues.emplace_back("shouting", std::to_string(caps) + " all-caps words - two is the ceiling before it reads as spam");
    else if (caps) good.push_back(std::to_string(caps) + " all-caps word for emphasis");
    const auto tws = words(t);
    std::set<std::string> v; for (const auto& w : tws) if (VAGUE.count(w)) v.insert(w);
    if (!v.empty()) issues.emplace_back("vague", join(std::vector<std::string>(v.begin(), v.end()), ", ") + " - swap for a number, a name or a date");
    const auto nums = re::findall(R"(\d[\d,.]*%?)", t);
    if (!nums.empty()) good.push_back("carries a concrete figure (" + join(std::vector<std::string>(nums.begin(), nums.begin() + static_cast<long>(std::min<std::size_t>(3, nums.size()))), ", ") + ")");
    else issues.emplace_back("no-number", "no number, date or name - the most reliable single fix");
    if (ends(t, "?")) good.push_back("open question in the title");
    bool front = false;
    for (std::size_t k = 0; k < tws.size() && k < 3; ++k) if (!STOP.count(tws[k])) front = true;
    if (!front) issues.emplace_back("front-load", "the first three words are all filler - move the subject forward");
    if (thumb && !thumb->empty()) {
        std::set<std::string> tw, hw;
        for (const auto& w : tws) if (!STOP.count(w)) tw.insert(w);
        const auto thw = words(*thumb);
        for (const auto& w : thw) if (!STOP.count(w)) hw.insert(w);
        std::vector<std::string> shared;
        for (const auto& w : tw) if (hw.count(w)) shared.push_back(w);
        if (!shared.empty()) issues.emplace_back("duplicate", "thumbnail repeats the title on " + join(shared, ", ") + " - the thumbnail should say what the title does not");
        else good.push_back("thumbnail and title carry different words");
        if (thw.size() > 4) issues.emplace_back("thumb-length", std::to_string(thw.size()) + " words on the thumbnail - three is the ceiling at feed size");
    }
    const long long sc = 100 - 14 * static_cast<long long>(issues.size()) + 4 * static_cast<long long>(good.size());
    r.title = t; r.chars = n; r.score = std::max(0LL, std::min(100LL, sc));
    return r;
}

inline void show(const Row& r) {
    print("\n  \"" + r.title + "\"");
    print("  " + std::to_string(r.chars) + " chars   score " + std::to_string(r.score) + "/100");
    for (const auto& [k, m] : r.issues) print("    x  " + ljust(k, 12) + " " + m);
    for (const auto& m : r.good) print("    ok              " + m);
}

// master: the virality copy answers -h/--help and takes --cover; the YouTube Pack original (yt-package/title) does not
inline int main_with(const std::vector<std::string>& argv, bool master, const std::string& doc) {
    std::vector<std::string> a = argv;
    if (master && (index_of(a, "-h") >= 0 || index_of(a, "--help") >= 0)) { print(doc); return 0; }
    if (const int k = index_of(a, "--cover"); master && k >= 0) a[static_cast<std::size_t>(k)] = "--thumb";   // a Reel cover is the thumbnail
    const bool as_json = index_of(a, "--json") >= 0;
    a.erase(std::remove(a.begin(), a.end(), std::string("--json")), a.end());
    std::optional<std::string> thumb;
    if (const int k = index_of(a, "--thumb"); k >= 0) thumb = arg_after(a, k);
    std::vector<Row> rows;
    if (const int k = index_of(a, "--title"); k >= 0) rows.push_back(check(arg_after(a, k), thumb));
    else if (!a.empty() && exists(a[0])) {
        for (const auto& l : splitlines(read_text_utf8(a[0]))) if (!strip(l).empty()) rows.push_back(check(l, thumb));
    } else { print(doc); return 1; }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) { return x.score > y.score; });
    if (as_json) {
        Val out = Val::list();
        for (const auto& r : rows) {
            Val d = Val::dict(); d.d->set("title", Val::str(r.title)); d.d->set("chars", Val::integer(r.chars)); d.d->set("score", Val::integer(r.score));
            Val is = Val::list(); for (const auto& [k, m] : r.issues) is.l->push_back(Val::list({Val::str(k), Val::str(m)}));
            Val g = Val::list(); for (const auto& m : r.good) g.l->push_back(Val::str(m));
            d.d->set("issues", is); d.d->set("good", g); out.l->push_back(d);
        }
        DumpOpts o; o.indent = 1; print(dumps(out, o)); return 0;
    }
    for (const auto& r : rows) show(r);
    print();
    return 0;
}
inline int main(const std::vector<std::string>& argv) { return main_with(argv, true, DOC); }
PACKTOOL("master-virality/title", main);
}  // namespace packtools::master_title
