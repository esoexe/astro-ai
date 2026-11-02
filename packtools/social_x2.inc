// x-agent-skill helpers, part 2 (Jake Schincariol, MIT), ported to C++: same output, same exit codes as the Python originals.
//   x-audit/audit   audit an X analytics export (which hook formulas, lengths and formats beat the rest)
//   x-viral/swipe   rank collected posts by how far each beat its own account's median, and write the swipe file

// ---------------------------------------------------------------------------------------------- x-audit/audit
namespace packtools::x_audit {
using namespace pyrt;
namespace pm = packtools::master;

// datetime.strptime for the formats audit.py tries: CPython's _strptime regexes (IGNORECASE, \s+ for whitespace),
// a full match, then datetime()'s range checks. A failure is the ValueError parse_date swallows.
struct DT { long long y = 1900, mo = 1, d = 1, h = 0, mi = 0, s = 0, us = 0; bool aware = false; long long off_us = 0; };
inline long long days_from_civil(long long y, long long m, long long d) {   // days since 1970-01-01
    y -= m <= 2; const long long era = (y >= 0 ? y : y - 399) / 400; const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
inline int month_len(long long y, long long m) {
    static const int L[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return L[m - 1];
}
inline const std::vector<std::string> ABDAY = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
inline const std::vector<std::string> ABMON = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
inline std::string fmt_regex(const std::string& fmt) {
    std::string o; bool ws = false;
    for (std::size_t k = 0; k < fmt.size(); ++k) {
        const char c = fmt[k];
        if (c == ' ' || c == '\t' || c == '\n') { if (!ws) o += "\\s+"; ws = true; continue; }
        ws = false;
        if (c == '%' && k + 1 < fmt.size()) {
            switch (fmt[++k]) {
                case 'a': o += "(?P<a>mon|tue|wed|thu|fri|sat|sun)"; break;
                case 'b': o += "(?P<b>jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)"; break;
                case 'd': o += "(?P<d>3[0-1]|[1-2]\\d|0[1-9]|[1-9]| [1-9])"; break;
                case 'f': o += "(?P<f>[0-9]{1,6})"; break;
                case 'H': o += "(?P<H>2[0-3]|[0-1]\\d|\\d)"; break;
                case 'M': o += "(?P<M>[0-5]\\d|\\d)"; break;
                case 'S': o += "(?P<S>6[0-1]|[0-5]\\d|\\d)"; break;
                case 'm': o += "(?P<m>1[0-2]|0[1-9]|[1-9])"; break;
                case 'Y': o += "(?P<Y>\\d\\d\\d\\d)"; break;
                case 'z': o += "(?P<z>[+-]\\d\\d:?[0-5]\\d(:?[0-5]\\d(\\.\\d{1,6})?)?|Z)"; break;   // Z is case-sensitive: checked below
                default: o += re::escape(std::string(1, fmt[k])); break;
            }
            continue;
        }
        o += re::escape(std::string(1, c));
    }
    return o;
}
inline long long to_int(const std::string& s) { return std::stoll(pm::ascii_digits(strip(s))); }
inline std::optional<DT> strptime(const std::string& v, const std::string& fmt) {
    const std::string pat = fmt_regex(fmt);
    const re::Regex* rx = &re::compile(pat, re::I);
    const re::Match m = rx->match(v);
    if (!m || m.end(0) != static_cast<long>(cplen(v))) return std::nullopt;   // no match, or unconverted data remains
    DT t;
    auto g = [&](const char* name) -> std::optional<std::string> { const int i = rx->group_index(name); if (i < 0 || !m.has(i)) return std::nullopt; return m.group(i); };
    if (auto x = g("Y")) t.y = to_int(*x);
    if (auto x = g("m")) t.mo = to_int(*x);
    if (auto x = g("b")) t.mo = (std::find(ABMON.begin(), ABMON.end(), lower(*x)) - ABMON.begin()) + 1;
    if (auto x = g("d")) t.d = to_int(*x);
    if (auto x = g("H")) t.h = to_int(*x);
    if (auto x = g("M")) t.mi = to_int(*x);
    if (auto x = g("S")) t.s = to_int(*x);
    if (auto x = g("f")) { std::string f = *x; f += std::string(6 - f.size(), '0'); t.us = std::stoll(f); }
    if (auto x = g("z")) {
        std::string z = *x;
        if (z == "z") return std::nullopt;   // (?-i:Z)
        t.aware = true;
        if (z != "Z") {
            if (z[3] == ':') {
                z = z.substr(0, 3) + z.substr(4);
                if (z.size() > 5) { if (z[5] != ':') return std::nullopt; z = z.substr(0, 5) + z.substr(6); }   // "Inconsistent use of :"
            }
            const auto u = to_u32(z);
            auto sub = [&](std::size_t a, std::size_t b) { return from_u32(u, a, b); };
            const long long hh = to_int(sub(1, 3)), mm = to_int(sub(3, 5));
            const std::string ss = sub(5, 7);
            long long sec = 0;
            if (!ss.empty()) { if (!re::fullmatch(R"(\d+)", ss)) return std::nullopt; sec = to_int(ss); }   // int(z[5:7]) raises on ":1"
            std::string rem = sub(8, std::string::npos); rem += std::string(6 - std::min<std::size_t>(6, cplen(rem)), '0');
            const long long frac = std::stoll(pm::ascii_digits(rem));
            long long off = (hh * 3600 + mm * 60 + sec) * 1000000 + frac;
            if (z[0] == '-') off = -off;
            if (off <= -86400000000LL || off >= 86400000000LL) return std::nullopt;   // timezone(): strictly within a day
            t.off_us = off;
        }
    }
    if (t.y < 1 || t.y > 9999 || t.mo < 1 || t.mo > 12 || t.d < 1 || t.d > month_len(t.y, t.mo) || t.s > 59) return std::nullopt;
    return t;
}
inline const std::vector<std::string> DATE_FORMATS = {"%a, %b %d, %Y", "%Y-%m-%d %H:%M %z", "%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S.%fZ",
    "%Y-%m-%dT%H:%M:%SZ", "%Y-%m-%d", "%m/%d/%Y %H:%M", "%m/%d/%Y", "%b %d, %Y", "%d/%m/%Y"};
inline std::optional<DT> parse_date(const std::string& v0) {
    const std::string v = strip(v0);
    for (const auto& f : DATE_FORMATS) if (auto t = strptime(v, f)) return t;
    return std::nullopt;
}
inline long long instant(const DT& t) { return ((days_from_civil(t.y, t.mo, t.d) * 24 + t.h) * 60 + t.mi) * 60000000LL + t.s * 1000000LL + t.us - (t.aware ? t.off_us : 0); }
inline bool dt_less(const DT& a, const DT& b) {
    if (a.aware != b.aware) throw PyErr("TypeError", "can't compare offset-naive and offset-aware datetimes");
    return instant(a) < instant(b);
}
inline std::string two(long long n) { char b[32]; std::snprintf(b, sizeof b, "%02lld", n); return b; }
inline std::string weekday(const DT& t) {
    static const char* N[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    const long long dn = days_from_civil(t.y, t.mo, t.d);   // 1970-01-01 was a Thursday
    return N[((dn % 7) + 7 + 3) % 7];
}
inline std::string month_abbr(const DT& t) { static const char* N[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}; return N[t.mo - 1]; }
inline std::string isoformat(const DT& t) {
    char b[64]; std::snprintf(b, sizeof b, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld", t.y, t.mo, t.d, t.h, t.mi, t.s);
    std::string o = b;
    if (t.us) { std::snprintf(b, sizeof b, ".%06lld", t.us); o += b; }
    if (t.aware) {
        long long off = t.off_us; o += off < 0 ? "-" : "+"; off = std::llabs(off);
        const long long us = off % 1000000, secs = off / 1000000;
        o += two(secs / 3600) + ":" + two(secs / 60 % 60);
        if (secs % 60 || us) o += ":" + two(secs % 60);
        if (us) { std::snprintf(b, sizeof b, ".%06lld", us); o += b; }
    }
    return o;
}

inline const std::vector<std::pair<std::string, std::vector<std::string>>> FIELDS = {
    {"text", {"post text", "tweet text", "text", "content"}}, {"link", {"post link", "tweet permalink", "permalink", "url", "link"}},
    {"date", {"date", "time", "created at", "created"}}, {"impressions", {"impressions", "views"}}, {"engagements", {"engagements"}},
    {"likes", {"likes", "favorites"}}, {"replies", {"replies"}}, {"reposts", {"reposts", "retweets"}}, {"bookmarks", {"bookmarks"}},
    {"shares", {"shares"}}, {"follows", {"new follows", "follows"}}, {"visits", {"profile visits", "user profile clicks"}}};
inline const std::vector<std::string> NUMS = {"impressions", "engagements", "likes", "replies", "reposts", "bookmarks", "shares", "follows", "visits"};
inline const char* TCO = R"(https?://t\.co/\S+|https?://\S+)";

inline std::vector<std::pair<std::string, std::size_t>> map_columns(const std::vector<std::string>& header) {
    std::vector<std::string> norm; for (const auto& h : header) norm.push_back(strip(re::sub(R"([^a-z0-9 ])", "", lower(h))));
    std::vector<std::pair<std::string, std::size_t>> out;
    auto taken = [&](std::size_t i) { for (const auto& kv : out) if (kv.second == i) return true; return false; };
    for (const auto& [field, names] : FIELDS) {
        bool done = false;
        for (const auto& name : names) {   // exact first, then contains
            const auto it = std::find(norm.begin(), norm.end(), name);
            if (it != norm.end()) { out.emplace_back(field, static_cast<std::size_t>(it - norm.begin())); done = true; break; }
        }
        if (done) continue;
        for (std::size_t i = 0; i < norm.size() && !done; ++i) {
            bool any = false; for (const auto& n : names) if (starts(norm[i], n) || ends(norm[i], n)) any = true;
            if (any && !taken(i)) { out.emplace_back(field, i); done = true; }
        }
    }
    return out;
}
inline std::optional<double> to_num(const std::string& v0) {
    const std::string v = replace_all(strip(v0), ",", "");
    try { return pm::py_float(v); } catch (const PyErr& e) { if (e.type != "ValueError") throw; return std::nullopt; }
}

struct Post {
    std::map<std::string, std::optional<double>> n;
    std::string text, link; std::optional<DT> date;
    double er = 0, r1k = 0, s1k = 0, b1k = 0, f1k = 0;
    std::string hook; Val formula; std::string has_tco, length; std::optional<std::string> wday;
};
inline double nz(const std::optional<double>& v) { return v && *v != 0.0 ? *v : 0.0; }   // p.get(k) or 0

inline Val key_of(const Post& p, const std::string& key) {   // p.get(key)
    if (key == "formula") return p.formula;
    if (key == "length") return Val::str(p.length);
    if (key == "has_tco") return Val::str(p.has_tco);
    return p.wday ? Val::str(*p.wday) : Val();
}
struct Finding { std::string dimension, group; long long n = 0; double mean_er = 0, rest_er = 0, ratio = 0; };
inline std::vector<Finding> findings(const std::vector<const Post*>& posts, const std::string& key, const std::string& label) {
    std::vector<std::pair<Val, std::vector<const Post*>>> groups;
    for (const Post* p : posts) {
        const Val k = key_of(*p, key);
        if (k.is_none()) continue;
        bool hit = false; for (auto& g : groups) if (packtools::ig::py_eq(g.first, k)) { g.second.push_back(p); hit = true; break; }
        if (!hit) groups.push_back({k, {p}});
    }
    std::vector<Finding> res;
    for (const auto& [k, items] : groups) {
        std::vector<double> a, b;
        for (const Post* p : items) a.push_back(p->er);
        for (const Post* p : posts) { const Val pk = key_of(*p, key); if (!packtools::ig::py_eq(pk, k)) b.push_back(p->er); }
        if (a.size() < 3 || b.size() < 3) continue;
        const double ma = pm::stat_mean(a), mb = pm::stat_mean(b);
        if (mb == 0) continue;
        const double ratio = ma / mb;
        if (ratio >= 1.4 || ratio <= 0.7) res.push_back({label, pystr(k), static_cast<long long>(items.size()), ma, mb, ratio});
    }
    if (groups.size() == 2 && res.size() == 2) res = {res[1].ratio > res[0].ratio ? res[1] : res[0]};   // two groups are one finding, not two
    return res;
}
inline std::string confidence(long long n) { return n >= 10 ? "solid" : n >= 5 ? "moderate" : "weak"; }
inline std::string pct(double v) { return fmt_f(v * 100, 1) + "%"; }
inline Val finding_val(const Finding& f) {
    Val o = Val::dict(); o.d->set("dimension", Val::str(f.dimension)); o.d->set("group", Val::str(f.group)); o.d->set("n", Val::integer(f.n));
    o.d->set("mean_er", Val::flt(f.mean_er)); o.d->set("rest_er", Val::flt(f.rest_er)); o.d->set("ratio", Val::flt(f.ratio));
    return o;
}
inline Val post_val(const Post& p) {
    Val o = Val::dict();
    for (const auto& k : NUMS) { const auto& v = p.n.at(k); o.d->set(k, v ? Val::flt(*v) : Val()); }
    o.d->set("text", Val::str(p.text)); o.d->set("link", Val::str(p.link)); o.d->set("date", p.date ? Val::str(isoformat(*p.date)) : Val());
    o.d->set("er", Val::flt(p.er)); o.d->set("replies_1k", Val::flt(p.r1k)); o.d->set("shares_1k", Val::flt(p.s1k)); o.d->set("bookmarks_1k", Val::flt(p.b1k));
    o.d->set("follows_1k", Val::flt(p.f1k)); o.d->set("hook", Val::str(p.hook)); o.d->set("formula", p.formula); o.d->set("has_tco", Val::str(p.has_tco));
    o.d->set("length", Val::str(p.length)); o.d->set("weekday", p.wday ? Val::str(*p.wday) : Val());
    return o;
}

inline int main(const std::vector<std::string>& argv) {
    // audit.py imports classify and first_line from ../x-post/hookscore.py; when that import fails it falls back to its own first_line
    packtools::ig_swipe::Formulas fs; bool have_classify = true;
    try { fs = packtools::x_hook::formulas(); } catch (const std::exception&) { have_classify = false; }
    Parser p("audit", "Audit an X analytics export.");
    p.pos("input").help("CSV export");
    p.opt({"--min-impressions"}).integer().help("ignore posts under this many impressions when ranking (default: 10th percentile, min 50)");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);

    std::string raw = read_text_utf8(S(args, "input"));
    if (starts(raw, "\xEF\xBB\xBF")) raw = raw.substr(3);   // utf-8-sig
    const auto csv = csv_rows(raw);
    std::vector<std::pair<std::string, std::size_t>> cols;
    std::vector<Post> rows;
    if (!csv.empty()) {
        cols = map_columns(csv[0]);
        auto col = [&](const std::string& f) -> const std::size_t* { for (const auto& kv : cols) if (kv.first == f) return &kv.second; return nullptr; };
        for (std::size_t r = 1; r < csv.size(); ++r) {
            const auto& line = csv[r];
            auto cell = [&](const std::string& f) { const std::size_t* c = col(f); return c && *c < line.size() ? line[*c] : std::string(); };
            Post q;
            for (const auto& k : NUMS) q.n[k] = to_num(cell(k));
            q.text = cell("text"); q.link = cell("link"); q.date = parse_date(cell("date"));
            const auto& imp = q.n["impressions"];
            if (imp && *imp != 0.0) rows.push_back(q);
        }
    }
    if (rows.empty()) { eprint("no rows with impressions. Is this the content export (one row per post)?"); return 2; }
    std::vector<std::string> missing;
    for (const char* f : {"text", "impressions"}) { bool has = false; for (const auto& kv : cols) if (kv.first == f) has = true; if (!has) missing.push_back(f); }
    if (!missing.empty()) { eprint("could not find column(s): " + join(missing, ", ")); return 2; }

    for (auto& q : rows) {   // derive
        const double imp = *q.n["impressions"];
        std::vector<double> parts; for (const char* k : {"likes", "replies", "reposts", "bookmarks", "shares"}) parts.push_back(nz(q.n[k]));
        const double eng = q.n["engagements"] ? *q.n["engagements"] : fsum(parts);
        q.er = eng / imp;
        auto per1k = [&](double v) { return v * 1000 / imp; };
        q.r1k = per1k(nz(q.n["replies"])); q.s1k = per1k(nz(q.n["reposts"]) + nz(q.n["shares"]));
        q.b1k = per1k(nz(q.n["bookmarks"])); q.f1k = per1k(nz(q.n["follows"]));
        if (have_classify) q.hook = xs::first_line(q.text);
        else { const std::string t = strip(q.text); q.hook = t.empty() ? "" : splitlines(t)[0]; }
        q.formula = have_classify ? packtools::ig_swipe::classify(q.hook, fs).second : Val::str("unclassified");
        const std::string body = strip(re::sub(TCO, "", q.text));
        q.has_tco = re::search(TCO, q.text) ? "has a link or media" : "text only";
        const std::size_t n = cplen(body);
        q.length = n < 100 ? "under 100" : n < 200 ? "100-199" : n <= 280 ? "200-280" : "long (280+)";
        if (q.date) q.wday = weekday(*q.date);
    }
    std::vector<const Post*> replies_out, posts;
    for (const auto& q : rows) (starts(pm::lstrip_ws(q.text), "@") ? replies_out : posts).push_back(&q);
    if (posts.empty()) { eprint("every row is a reply. Nothing to audit as posts."); return 2; }
    std::vector<double> imps; for (const Post* q : posts) imps.push_back(*q->n.at("impressions"));
    std::stable_sort(imps.begin(), imps.end());
    Val floor;
    if (!get(args, "min_impressions").is_none()) floor = get(args, "min_impressions");
    else if (imps.size() >= 10 && imps[imps.size() / 10] > 50) floor = Val::flt(imps[imps.size() / 10]);
    else floor = Val::integer(50);
    const double fl = floor.num();
    std::vector<const Post*> ranked; for (const Post* q : posts) if (*q->n.at("impressions") >= fl) ranked.push_back(q);
    std::stable_sort(ranked.begin(), ranked.end(), [](const Post* a, const Post* b) { return -a->er < -b->er; });

    std::vector<Finding> found;
    for (const auto& [key, label] : std::vector<std::pair<std::string, std::string>>{{"formula", "hook formula"}, {"length", "length"}, {"has_tco", "format"}}) {
        const auto f = findings(ranked, key, label); found.insert(found.end(), f.begin(), f.end());
    }
    auto fkey = [](const Finding& f) { return -std::fabs(f.ratio - 1) * static_cast<double>(std::min<long long>(f.n, 10)); };
    std::stable_sort(found.begin(), found.end(), [&](const Finding& a, const Finding& b) { return fkey(a) < fkey(b); });
    const auto days = findings(ranked, "weekday", "day of week");

    std::vector<DT> dates; for (const Post* q : posts) if (q->date) dates.push_back(*q->date);
    std::string span = "dates unread";
    if (!dates.empty()) {
        DT lo = dates[0], hi = dates[0];
        for (std::size_t k = 1; k < dates.size(); ++k) if (dt_less(dates[k], lo)) lo = dates[k];
        for (std::size_t k = 1; k < dates.size(); ++k) if (dt_less(hi, dates[k])) hi = dates[k];
        span = month_abbr(lo) + " " + two(lo.d) + " - " + month_abbr(hi) + " " + two(hi.d);
    }

    if (B(args, "json")) {
        Val o = Val::dict();
        o.d->set("posts", Val::integer(static_cast<long long>(posts.size()))); o.d->set("replies_set_aside", Val::integer(static_cast<long long>(replies_out.size())));
        o.d->set("floor", floor);
        Val rk = Val::list(); for (const Post* q : ranked) rk.l->push_back(post_val(*q));
        o.d->set("ranked", rk);
        Val fv = Val::list(); for (const auto& f : found) fv.l->push_back(finding_val(f));
        o.d->set("findings", fv);
        Val dv = Val::list(); for (const auto& f : days) dv.l->push_back(finding_val(f));
        o.d->set("day_of_week", dv);
        DumpOpts d; d.indent = 2; print(dumps(o, d));
        return 0;
    }
    const std::string dot = "  \xC2\xB7  ";
    print("\nAUDIT" + dot + std::to_string(posts.size()) + " posts" + dot + span + dot + std::to_string(replies_out.size()) + " replies set aside");
    print(std::string(78, '='));
    print("ranked by engagement rate, posts under " + commas(fmt_f(fl, 0)) + " impressions left out as noise\n");
    auto row = [&](const Post* q) {
        const std::string hook = cplen(q->hook) <= 44 ? q->hook : cpslice(q->hook, 0, 41) + "...";
        return "  " + rjust(pct(q->er), 6) + "  " + rjust(fmt_f(q->r1k, 1), 5) + " rep/1k  " + rjust(fmt_f(q->s1k, 1), 5) + " shr/1k  " +
               rjust(commas(fmt_f(*q->n.at("impressions"), 0)), 8) + " imp  \"" + hook + "\"";
    };
    const std::size_t k = std::min<std::size_t>(5, std::max<std::size_t>(1, ranked.size() / 3));
    print("TOP BY ENGAGEMENT RATE");
    for (std::size_t i = 0; i < k && i < ranked.size(); ++i) print(row(ranked[i]));
    print("\nBOTTOM");
    for (std::size_t i = ranked.size() > k ? ranked.size() - k : 0; i < ranked.size(); ++i) print(row(ranked[i]));

    const Post* most_seen = posts[0];
    for (const Post* q : posts) if (*q->n.at("impressions") > *most_seen->n.at("impressions")) most_seen = q;
    if (!ranked.empty() && most_seen != ranked[0]) {
        std::string where = "below the noise floor";
        for (std::size_t i = 0; i < ranked.size(); ++i) if (ranked[i] == most_seen) { where = "#" + std::to_string(i + 1) + " of " + std::to_string(ranked.size()); break; }
        print("\n  Your most-seen post ranks " + where + " by engagement rate. Reach and resonance are not the same number.");
    }
    print("\nWHAT THE DATA SAYS");
    if (ranked.size() < 12)
        print("  " + std::to_string(ranked.size()) + " posts is too few to find a pattern. Post 20 more and run this again. Anything printed below is a hint, not a finding.");
    if (found.empty())
        print("  Nothing separates cleanly. No hook formula, length or link/media group is 1.4x above or below the rest with 3+ posts. That is a real answer: "
              "your topic and your first line matter more than any format lever here.");
    for (std::size_t i = 0; i < found.size() && i < 4; ++i) {
        const Finding& f = found[i];
        print("  " + std::to_string(i + 1) + ". " + f.dimension + " = " + f.group + ": " + pct(f.mean_er) + " vs " + pct(f.rest_er) + " for everything else (" +
              fmt_f(f.ratio, 1) + "x, " + (f.ratio > 1 ? "above" : "below") + "). n=" + std::to_string(f.n) + ", " + confidence(f.n) + ".");
    }
    if (!days.empty() && found.empty()) {
        const Finding* f = &days[0]; for (const auto& d : days) if (std::fabs(d.ratio - 1) > std::fabs(f->ratio - 1)) f = &d;
        print("  Day of week: " + f->group + " at " + fmt_f(f->ratio, 1) + "x, n=" + std::to_string(f->n) + ". Checked last on purpose. "
              "It is almost never the cause, and it is where people want it to be.");
    } else if (days.empty()) print("  Day of week shows nothing. Stop optimising it.");
    if (!replies_out.empty()) {
        std::vector<double> ri, pi; for (const Post* q : replies_out) ri.push_back(*q->n.at("impressions")); for (const Post* q : posts) pi.push_back(*q->n.at("impressions"));
        const double rimp = fsum(ri), pimp = fsum(pi);
        print("\n  Replies: " + std::to_string(replies_out.size()) + " of them earned " + commas(fmt_f(rimp, 0)) + " impressions, " +
              fmt_f(rimp / (pimp >= 1 ? pimp : 1.0) * 100, 0) + "% of what your posts earned.");
    }
    print("");
    return 0;
}
PACKTOOL("x-audit/audit", main);
}  // namespace packtools::x_audit

// ---------------------------------------------------------------------------------------------- x-viral/swipe
namespace packtools::x_swipe {
using namespace pyrt;
namespace pm = packtools::master;
using packtools::ig::py_eq;

inline const char* LIST_LINE = R"(^\s*(?:\d{1,2}[.)]|[-•*→>]|[\U0001F300-\U0001FAFF\u2705\u27A1\u2714])\s*\S)";

inline Val num(const Val* v) {
    const std::string s = replace_all(lower(strip(v && v->t == Val::STR ? v->s : std::string())), ",", "");
    const re::Match m = re::match(R"(^(\d+(?:\.\d+)?)\s*([km]?)$)", s);
    if (!m) return Val();
    const std::string u = m.group(2);
    const double n = pm::py_float(m.group(1)) * (u.empty() ? 1.0 : u == "k" ? 1e3 : 1e6);
    return Val::integer(packtools::ig::py_trunc(n));
}
inline Val rget(const Val& r, const std::string& k) { const Val* v = r.d->get(k); return v ? *v : Val(); }
inline std::string sget(const Val& r, const std::string& k) { const Val v = rget(r, k); return v.t == Val::STR ? v.s : std::string(); }   // r.get(k) or ""

inline std::vector<Val> read_rows(const std::string& path) {
    std::vector<std::string> lines;
    for (const auto& l : splitlines(packtools::ig::read_input(path))) if (!strip(l).empty() && !starts(pm::lstrip_ws(l), "#")) lines.push_back(l);
    if (lines.empty()) return {};
    std::vector<std::string> cols; for (const auto& c : split(lines[0], "\t")) cols.push_back(lower(strip(c)));
    std::vector<Val> rows;
    for (std::size_t li = 1; li < lines.size(); ++li) {
        const auto cells = split(lines[li], "\t");
        Val r = Val::dict();
        for (std::size_t k = 0; k < cols.size(); ++k) r.d->set(cols[k], Val::str(k < cells.size() ? strip(cells[k]) : ""));
        std::string t = sget(r, "text"); if (t.empty()) t = sget(r, "hook");
        r.d->set("text", Val::str(replace_all(replace_all(t, " \xE2\x8F\x8E ", "\n"), "\xE2\x8F\x8E", "\n")));
        for (const char* k : {"views", "likes", "replies", "reposts", "median", "followers"}) { const Val v = rget(r, k); r.d->set(k, num(&v)); }
        const Val views = rget(r, "views"), likes = rget(r, "likes");
        r.d->set("metric_name", truthy(views) ? Val::str("views") : truthy(likes) ? Val::str("likes") : Val());
        const Val metric = truthy(views) ? views : likes;
        r.d->set("metric", metric);
        if (truthy(metric) && (!sget(r, "hook").empty() || !sget(r, "text").empty())) rows.push_back(r);
    }
    return rows;
}
inline std::string hook_of(const Val& r) {
    std::string hook = strip(sget(r, "hook"));
    if (hook.empty()) for (const auto& l : splitlines(sget(r, "text"))) if (!strip(l).empty()) { hook = strip(l); break; }
    if (cplen(hook) > 140) { if (const re::Match m = re::match(R"((.+?[.!?])(?:\s|$))", hook)) hook = m.group(1); }   // one long paragraph: the hook is its first sentence
    return hook;
}
inline std::string structure_of(const Val& r) {
    const std::string text = sget(r, "text");
    std::vector<std::string> lines; for (const auto& l : splitlines(text)) if (!strip(l).empty()) lines.push_back(l);
    long long listy = 0; for (const auto& l : lines) if (re::match(LIST_LINE, l)) ++listy;
    const std::string fmt = lower(sget(r, "format"));
    if (fmt == "thread") return "thread";
    if (fmt == "long" || cplen(text) > 280) return "long post";
    if (listy >= 3) return "list (" + std::to_string(listy) + " items)";
    if (lines.size() <= 1) return split_ws(text).size() <= 20 ? "one line" : "one paragraph";
    return std::to_string(lines.size()) + " short lines";
}
inline Val median_of(std::vector<Val> vals) {   // statistics.median: the middle value, or the mean of the two middle ones
    std::stable_sort(vals.begin(), vals.end(), [](const Val& x, const Val& y) { return x.num() < y.num(); });
    const std::size_t n = vals.size();
    if (n % 2) return vals[n / 2];
    const Val &x = vals[n / 2 - 1], &y = vals[n / 2];
    if (x.t == Val::INT && y.t == Val::INT) return Val::flt(static_cast<double>(x.i + y.i) / 2.0);
    return Val::flt((x.num() + y.num()) / 2.0);
}
inline Val rounded(const Val& v, int digits) { return v.t == Val::FLOAT ? Val::flt(round_n(v.f, digits)) : v; }   // round(int, n) stays an int
inline double ratio_div(const Val& a, const Val& b) { return a.num() / b.num(); }

struct Hooks { bool ok = false; packtools::ig_swipe::Formulas formulas; std::vector<std::pair<Val, Val>> by_id; };
inline Hooks load_hooks() {
    Hooks h;
    try {
        h.formulas = packtools::x_hook::formulas();   // import hookscore
        const Val d = loads(read_text_utf8(skill_file("../x-post/hook-formulas.json")));
        for (const auto& x : packtools::ig::iterate_py(packtools::ig::getitem(d, "hooks"))) {
            const Val id = packtools::ig::getitem(x, "id"); packtools::ig::hashable(id);
            bool hit = false; for (auto& kv : h.by_id) if (py_eq(kv.first, id)) { kv.second = x; hit = true; break; }
            if (!hit) h.by_id.push_back({id, x});
        }
        h.ok = true;
    } catch (const std::exception&) { h = Hooks(); }   // x-viral copied on its own: formulas are not named
    return h;
}

inline Val tally(const std::vector<Val>& items, const std::string& key) {
    std::vector<std::pair<Val, long long>> out;
    for (const auto& r : items) {
        const Val k = rget(r, key); bool hit = false;
        for (auto& kv : out) if (py_eq(kv.first, k)) { kv.second++; hit = true; break; }
        if (!hit) out.push_back({k, 1});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return -x.second < -y.second; });
    Val l = Val::list(); for (const auto& [k, c] : out) { Val p = Val::list(); p.l->push_back(k); p.l->push_back(Val::integer(c)); l.l->push_back(p); }
    return l;
}
template <class F> inline Val med(const std::vector<Val>& items, F f, int digits) {
    std::vector<Val> vals; for (const auto& r : items) { const Val v = f(r); if (!v.is_none()) vals.push_back(v); }
    return vals.empty() ? Val() : rounded(median_of(vals), digits);
}
inline Val third_summary(const std::vector<Val>& items) {
    Val o = Val::dict();
    o.d->set("formulas", tally(items, "formula")); o.d->set("formats", tally(items, "format")); o.d->set("structures", tally(items, "structure"));
    o.d->set("hook_words", med(items, [](const Val& r) { return Val::integer(static_cast<long long>(split_ws(sget(r, "hook_line")).size())); }, 1));
    o.d->set("reply_ratio", med(items, [](const Val& r) { return rget(r, "reply_ratio"); }, 3));
    return o;
}
inline Val analyse(std::vector<Val>& rows, long long min_posts, const Hooks& hooks) {
    std::vector<std::pair<std::string, std::vector<Val>>> by_acct;
    for (const auto& r : rows) {
        const Val* a = r.d->get("account");
        const std::string acct = a ? pystr(*a) : "?";
        bool hit = false; for (auto& g : by_acct) if (g.first == acct) { g.second.push_back(r); hit = true; break; }
        if (!hit) by_acct.push_back({acct, {r}});
    }
    std::vector<Val> ranked; Val skipped = Val::list();
    auto skip = [&](const std::string& who, long long n) { Val p = Val::list(); p.l->push_back(Val::str(who)); p.l->push_back(Val::integer(n)); skipped.l->push_back(p); };
    for (auto& [acct, items] : by_acct) {
        Val base; bool have = false;
        for (const auto& r : items) if (truthy(rget(r, "median"))) { base = rget(r, "median"); have = true; break; }
        if (!have && static_cast<long long>(items.size()) < min_posts) { skip(acct, static_cast<long long>(items.size())); continue; }
        for (auto& r : items) {
            Val b = base;
            if (!have) {   // median within the metric each row actually has
                std::vector<Val> same; for (const auto& x : items) if (py_eq(rget(x, "metric_name"), rget(r, "metric_name"))) same.push_back(rget(x, "metric"));
                if (static_cast<long long>(same.size()) < min_posts) { skip(acct + " (" + pystr(rget(r, "metric_name")) + ")", static_cast<long long>(same.size())); continue; }
                b = median_of(same);
            }
            r.d->set("baseline", b);
            r.d->set("outlier", truthy(b) ? Val::flt(round_n(ratio_div(rget(r, "metric"), b), 2)) : Val());
            const std::string hook = hook_of(r);
            r.d->set("hook_line", Val::str(hook));
            if (hooks.ok) {
                const Val p = packtools::x_hook::score_payload(hook, hooks.formulas);
                r.d->set("formula_id", get(p, "formula_id")); r.d->set("formula", get(p, "formula")); r.d->set("hook_score", get(p, "score"));
            } else { r.d->set("formula_id", Val()); r.d->set("formula", Val::str("unclassified")); r.d->set("hook_score", Val()); }
            r.d->set("structure", Val::str(structure_of(r)));
            const std::string fmt = sget(r, "format");
            r.d->set("format", Val::str(fmt.empty() ? "text" : fmt));
            const Val rep = rget(r, "replies"), lk = rget(r, "likes");
            r.d->set("reply_ratio", !rep.is_none() && truthy(lk) ? Val::flt(round_n(ratio_div(rep, lk), 3)) : Val());
            ranked.push_back(r);
        }
    }
    auto key = [](const Val& r) { const Val o = rget(r, "outlier"); return -(truthy(o) ? o.num() : 0.0); };
    std::stable_sort(ranked.begin(), ranked.end(), [&](const Val& x, const Val& y) { return key(x) < key(y); });
    const std::size_t third = std::max<std::size_t>(1, ranked.size() / 3);
    const std::vector<Val> top(ranked.begin(), ranked.begin() + static_cast<long>(std::min(third, ranked.size()))),
                           bottom(ranked.end() - static_cast<long>(std::min(third, ranked.size())), ranked.end());
    std::vector<Val> accts; for (const auto& r : ranked) { const Val a = rget(r, "account"); bool seen = false; for (const auto& x : accts) if (py_eq(x, a)) seen = true; if (!seen) accts.push_back(a); }
    std::set<std::string> metrics; for (const auto& r : ranked) metrics.insert(pystr(rget(r, "metric_name")));
    Val a = Val::dict();
    a.d->set("n", Val::integer(static_cast<long long>(ranked.size()))); a.d->set("accounts", Val::integer(static_cast<long long>(accts.size())));
    Val ml = Val::list(); for (const auto& m : metrics) ml.l->push_back(Val::str(m));
    a.d->set("metrics", ml); a.d->set("posts", Val::list(ranked)); a.d->set("skipped", skipped);
    a.d->set("top", third_summary(top)); a.d->set("bottom", third_summary(bottom));
    Val un = Val::list(); for (const auto& r : ranked) if (py_eq(rget(r, "formula"), Val::str("unclassified"))) un.l->push_back(rget(r, "hook_line"));
    a.d->set("unclassified", un);
    return a;
}
inline std::string fmt_tally(const Val& t, std::size_t k = 3) {
    std::vector<std::string> s; for (std::size_t i = 0; i < t.l->size() && i < k; ++i) s.push_back(pystr((*t.l)[i][0]) + " x" + pystr((*t.l)[i][1]));
    return s.empty() ? "-" : join(s, ", ");
}
inline std::string str_slice(const Val& v, long n) {   // v[:n] on something that should be a str
    if (v.t != Val::STR) throw PyErr("TypeError", "'" + packtools::ig::type_of(v) + "' object is not subscriptable");
    return cpslice(v.s, 0, n);
}
inline void render(const Val& a) {
    const std::string dot = "  \xC2\xB7  ";
    std::vector<std::string> ms; for (const auto& m : *get(a, "metrics").l) ms.push_back(m.s);
    const std::string head = "SWIPE FILE" + dot + std::to_string(get(a, "n").i) + " posts" + dot + std::to_string(get(a, "accounts").i) + " accounts" + dot +
                             "multiple over each account's own median " + join(ms, "/");
    const std::size_t rule = std::max<std::size_t>(cplen(head), 78);
    print("\n" + head);
    print(std::string(rule, '='));
    for (const auto& r : *get(a, "posts").l) {
        const Val o = rget(r, "outlier"), fid = rget(r, "formula_id");
        const std::string mult = truthy(o) ? fmt_f(o.num(), 1) + "x" : "  ?";
        const std::string id = truthy(fid) ? packtools::ig_swipe::fmt_id(fid) : "-  ";
        print("  " + rjust(mult, 6) + "  " + id + " " + ljust(str_slice(rget(r, "formula"), 20), 20) + " " + ljust(cpslice(sget(r, "structure"), 0, 15), 15) + " " +
              ljust(sget(r, "format"), 6) + " " + ljust(cpslice(sget(r, "account"), 0, 16), 16) + " " + rjust(commas(std::to_string(rget(r, "metric").i)), 9) + " " +
              pystr(rget(r, "metric_name")));
        const std::string h = sget(r, "hook_line");
        print("          \"" + (cplen(h) <= 92 ? h : cpslice(h, 0, 89) + "...") + "\"");
    }
    print(std::string(rule, '-'));
    const Val t = get(a, "top"), b = get(a, "bottom");
    print("TOP THIRD vs BOTTOM THIRD");
    print("  hook       top: " + fmt_tally(get(t, "formulas")));
    print("             bottom: " + fmt_tally(get(b, "formulas")));
    print("  structure  top: " + fmt_tally(get(t, "structures")) + "   bottom: " + fmt_tally(get(b, "structures")));
    print("  format     top: " + fmt_tally(get(t, "formats")) + "   bottom: " + fmt_tally(get(b, "formats")));
    print("  hook words top: " + pystr(get(t, "hook_words")) + "   bottom: " + pystr(get(b, "hook_words")));
    if (!get(t, "reply_ratio").is_none() && !get(b, "reply_ratio").is_none())
        print("  replies per like   top: " + pystr(get(t, "reply_ratio")) + "   bottom: " + pystr(get(b, "reply_ratio")));
    const Val un = get(a, "unclassified");
    if (un.size()) {
        print("\n  UNCLASSIFIED (" + std::to_string(un.size()) + "). Read these by hand. Either noise, or a formula hooks.json does not have yet:");
        for (std::size_t i = 0; i < un.l->size() && i < 8; ++i) print("    \"" + cpslice((*un.l)[i].s, 0, 90) + "\"");
    }
    const Val sk = get(a, "skipped");
    if (sk.size()) {
        std::vector<std::string> s; for (const auto& x : *sk.l) s.push_back(x[0].s + " (" + std::to_string(x[1].i) + ")");
        print("\n  SKIPPED (too few posts to know the account's normal): " + join(s, ", "));
    }
    const long long n = get(a, "n").i;
    print("\n  " + std::to_string(n) + " posts is " + (n >= 40 ? "a sample worth reading" : "a first look, not a finding") +
          ". Forty across six accounts shows you something; twelve shows you nothing.\n");
}
inline std::string to_markdown(const Val& a, const Hooks& hooks) {
    std::vector<std::string> lines = {"# Swipe file", "",
        std::to_string(get(a, "n").i) + " posts across " + std::to_string(get(a, "accounts").i) + " accounts, ranked by multiple over each account's own median. "
        "Copy the shape, never the words.", ""};
    const std::string dot = "  \xC2\xB7  ";
    for (const auto& r : *get(a, "posts").l) {
        const Val o = rget(r, "outlier");
        const std::string mult = truthy(o) ? fmt_f(o.num(), 1) + "x" : "?";
        const Val fid = rget(r, "formula_id");
        const Val* h = nullptr; for (const auto& kv : hooks.by_id) if (py_eq(kv.first, fid)) { h = &kv.second; break; }
        lines.push_back("## " + mult + "  " + pystr(rget(r, "formula")) + dot + sget(r, "account"));
        lines.push_back("- " + pystr(rget(r, "metric_name")) + ": " + commas(std::to_string(rget(r, "metric").i)) + "  (account median " + commas(fmt_f(rget(r, "baseline").num(), 0)) + ")");
        lines.push_back("- hook: \"" + sget(r, "hook_line") + "\"");
        lines.push_back("- structure: " + sget(r, "structure") + dot + "format: " + sget(r, "format"));
        lines.push_back("- source: " + pystr(r.d->get("url") ? *r.d->get("url") : Val::str("")));
        if (h && truthy(*h)) lines.push_back("- shape to reuse: " + pystr(packtools::ig::getitem(*h, "template")));
        lines.push_back("- your version: "); lines.push_back("");
    }
    return join(lines, "\n") + "\n";
}
inline int main(const std::vector<std::string>& argv) {
    const Hooks hooks = load_hooks();
    Parser p("swipe", "Rank collected X posts by outlier multiple.");
    p.pos("input").nargs("?").def("-").help("TSV from pull.py, or - for stdin");
    p.opt({"--min-posts"}).integer().def(5).help("posts needed per account to trust its median (default 5)");
    p.opt({"--out"}).help("also write the swipe file as markdown here");
    p.opt({"--json"}).store_true();
    const Val args = parse_args(p, argv);
    auto rows = read_rows(S(args, "input"));
    if (rows.empty()) { eprint("no usable rows. Fill in the views (or likes) column first."); return 2; }
    const long long min_posts = get(args, "min_posts").i;
    const Val a = analyse(rows, min_posts, hooks);
    if (!get(a, "posts").size()) {
        eprint("every account had fewer than " + std::to_string(min_posts) + " posts. Collect each account's last 10-15 posts, in order, and run it again.");
        return 2;
    }
    if (!hooks.ok) eprint("note: ../x-post/hookscore.py not found, formulas not named.");
    if (B(args, "json")) { DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(a, o)); }
    else render(a);
    const std::string outp = S(args, "out");
    if (!outp.empty()) {
        const std::string path = packtools::ig::expanduser(outp);
        const std::string dir = dirname(path);
        flush();
        makedirs(dir.empty() ? "." : dir);
        write_text(path, to_markdown(a, hooks));
        eprint("wrote " + path);
    }
    return 0;
}
PACKTOOL("x-viral/swipe", main);
}  // namespace packtools::x_swipe
