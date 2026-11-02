// hunter-salary/offer: offers in plain numbers, benchmarks with their sources (BLS OEWS, posted ranges, real offers)
// and a counter you can say out loud. A C++ port of hunter-skill/skills/hunter-salary/offer.py (MIT, Jake Schincariol):
// same subcommands, numbers, text, JSON and exit codes (OfferError and OSError: "offer: ..." on stderr, exit 2).
//
// One deliberate difference: "counter --json" in the Python strips the "bench" reference from the evidence before it
// writes the script, and the script reads that reference, so it raises KeyError whenever a BLS benchmark backs the
// ask. Here the script is written first, which is what the code means to do; the JSON is otherwise the same.
namespace packtools::hunter_salary {
using namespace pyrt;

struct OfferError : std::runtime_error { using std::runtime_error::runtime_error; };

inline constexpr const char* BLS_API = "https://api.bls.gov/publicAPI/v2/timeseries/data/";
inline constexpr const char* UA = "hunter-skill/1.0 (+https://github.com/Jakeschincariol/hunter-skill)";
inline constexpr double MAX_BUMP = 0.15;
inline const std::vector<std::pair<std::string, std::vector<double>>>& schedules() {
    static const std::vector<std::pair<std::string, std::vector<double>>> s = {
        {"even-4", {25, 25, 25, 25}}, {"back-loaded", {5, 15, 40, 40}}, {"back-loaded-5-15-40-40", {5, 15, 40, 40}},
        {"front-loaded", {33, 33, 22, 12}}, {"front-loaded-33-33-22-12", {33, 33, 22, 12}},
        {"even-3", {100 / 3.0, 100 / 3.0, 100 / 3.0}}, {"even-5", {20, 20, 20, 20, 20}}};
    return s;
}
inline const std::vector<std::pair<std::string, std::string>> DATATYPES = {{"mean", "04"}, {"p10", "11"}, {"p25", "12"}, {"p50", "13"}, {"p75", "14"}, {"p90", "15"}};
inline const std::map<std::string, std::string> SOC_NAMES = {
    {"15-1252", "Software Developers"}, {"15-1253", "Software Quality Assurance Analysts and Testers"}, {"15-2051", "Data Scientists"},
    {"15-1211", "Computer Systems Analysts"}, {"15-1212", "Information Security Analysts"},
    {"15-1244", "Network and Computer Systems Administrators"}, {"11-3021", "Computer and Information Systems Managers"}};
inline const std::map<std::string, std::string> AREA_NAMES = {{"N0000000", "United States (national)"}, {"M0041940", "San Jose-Sunnyvale-Santa Clara, CA"}};
inline const std::map<std::string, std::string> STATE_FIPS = {
    {"AL", "01"}, {"AK", "02"}, {"AZ", "04"}, {"AR", "05"}, {"CA", "06"}, {"CO", "08"}, {"CT", "09"}, {"DE", "10"}, {"DC", "11"},
    {"FL", "12"}, {"GA", "13"}, {"HI", "15"}, {"ID", "16"}, {"IL", "17"}, {"IN", "18"}, {"IA", "19"}, {"KS", "20"}, {"KY", "21"},
    {"LA", "22"}, {"ME", "23"}, {"MD", "24"}, {"MA", "25"}, {"MI", "26"}, {"MN", "27"}, {"MS", "28"}, {"MO", "29"}, {"MT", "30"},
    {"NE", "31"}, {"NV", "32"}, {"NH", "33"}, {"NJ", "34"}, {"NM", "35"}, {"NY", "36"}, {"NC", "37"}, {"ND", "38"}, {"OH", "39"},
    {"OK", "40"}, {"OR", "41"}, {"PA", "42"}, {"RI", "44"}, {"SC", "45"}, {"SD", "46"}, {"TN", "47"}, {"TX", "48"}, {"UT", "49"},
    {"VT", "50"}, {"VA", "51"}, {"WA", "53"}, {"WV", "54"}, {"WI", "55"}, {"WY", "56"}, {"PR", "72"}};

// ---------------------------------------------------------------- Python pieces
inline std::string tname(const Val& v) { return type_name(v); }
inline Val pget(const Val& v, const std::string& k, const Val& dflt = Val()) {   // v.get(k, dflt)
    if (v.t != Val::DICT) throw PyErr("AttributeError", "'" + tname(v) + "' object has no attribute 'get'");
    return v.d->has(k) ? v.d->at(k) : dflt;
}
inline Val item(const Val& v, const std::string& k) {   // v[k]
    if (v.t != Val::DICT) throw PyErr("TypeError", "'" + tname(v) + "' object is not subscriptable");
    if (!v.d->has(k)) throw PyErr("KeyError", str_repr(k));
    return v.d->at(k);
}
inline bool is_num(const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; }   // isinstance(v, (int, float))
inline double num(const Val& v, const Val& other = Val::integer(0)) {   // a number in a comparison or in arithmetic
    if (is_num(v)) return v.num();
    throw PyErr("TypeError", "'<' not supported between instances of '" + tname(v) + "' and '" + tname(other) + "'");
}
inline bool is_str(const Val& v, const char* s) { return v.t == Val::STR && v.s == s; }
inline long long round_up(double v, double step = 1000) {   // int(math.ceil(v / float(step)) * step)
    const double q = v / step;
    if (std::isnan(q)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(q)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    return static_cast<long long>(std::ceil(q) * step);
}
inline long long py_round(double x) {   // round(x) -> int
    if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    return round_int(x);
}
inline std::string pct_d(double x) {   // "%d" % x (truncates a float)
    if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    return std::to_string(static_cast<long long>(std::trunc(x)));
}
inline std::string g1(const Val& x) {   // "%g" % round(x, 1)
    return fmt_g(x.t == Val::FLOAT ? round_n(x.f, 1) : x.num());
}

// money(v, cur): "{:,.0f}" of |v|, "$" for USD/CAD/AUD/"$", else "<n> <cur>"
inline std::string money_d(std::optional<double> v, const Val& cur) {
    if (!v) return "n/a";
    const std::string sign = *v < 0 ? "-" : "";
    const std::string s = fmt_fc(std::fabs(*v), 0);
    if (is_str(cur, "USD") || is_str(cur, "CAD") || is_str(cur, "AUD") || is_str(cur, "$")) return sign + "$" + s;
    return sign + s + " " + pystr(cur);
}
inline std::string money(const Val& v, const Val& cur) { return v.is_none() ? "n/a" : money_d(num(v), cur); }

inline Val read_json(const Val& pathv) {
    const std::string path = truthy(pathv) ? pystr(pathv) : "";
    if (path.empty() || !exists(path)) throw OfferError("no file at " + (pathv.is_none() ? std::string("None") : path));
    const std::string text = read_text(path);
    try { return loads(text); }
    catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError") throw OfferError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline void write_json(const std::string& path, const Val& data) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    DumpOpts o; o.indent = 2;
    write_text(path, dumps(data, o) + "\n");
}

// ---------------------------------------------------------------- an offer
inline Val schedule(const Val& eq) {
    const Val v = pget(orv(eq, Val::dict()), "vesting", Val::str("even-4"));
    if (v.t == Val::STR) {
        for (const auto& [k, s] : schedules())
            if (k == v.s) {
                List o;
                for (double x : s) o.push_back(k == "even-3" ? Val::flt(x) : Val::integer(static_cast<long long>(x)));
                return Val::list(o);
            }
        std::vector<std::string> names; for (const auto& [k, _] : schedules()) names.push_back(k);
        std::sort(names.begin(), names.end());
        throw OfferError("unknown vesting " + str_repr(v.s) + " (use " + join(names, ", ") + ", or a list of yearly percents)");
    }
    bool ok = v.t == Val::LIST && !v.l->empty();
    if (ok) for (const Val& x : *v.l) if (!is_num(x) || x.num() < 0) ok = false;
    if (!ok) throw OfferError("vesting must be a name or a list of yearly percents");
    bool any_float = false; long long isum = 0; std::vector<double> fl;
    for (const Val& x : *v.l) { if (x.t == Val::FLOAT) any_float = true; }
    double total;
    if (!any_float) { for (const Val& x : *v.l) isum += x.t == Val::BOOL ? (x.b ? 1 : 0) : x.i; total = static_cast<double>(isum); }
    else {   // sum(): ints add exactly until the first float, then the float path
        std::size_t k = 0; long long head = 0;
        while (k < v.l->size() && (*v.l)[k].t != Val::FLOAT) { head += (*v.l)[k].t == Val::BOOL ? ((*v.l)[k].b ? 1 : 0) : (*v.l)[k].i; k++; }
        fl.push_back(static_cast<double>(head));
        for (; k < v.l->size(); ++k) fl.push_back((*v.l)[k].num());
        total = fsum(fl);
    }
    if (std::fabs(total - 100) > 0.5) throw OfferError("vesting percents add up to " + fmt_f(total, 1) + ", not 100");
    List o; for (const Val& x : *v.l) o.push_back(Val::flt(x.num()));
    return Val::list(o);
}
inline double grant_value(const Val& eq) {
    if (!truthy(eq)) return 0.0;
    if (!pget(eq, "grant_value").is_none()) return to_float(item(eq, "grant_value"));
    if (!pget(eq, "shares").is_none() && !pget(eq, "share_price").is_none()) return to_float(item(eq, "shares")) * to_float(item(eq, "share_price"));
    if (!pget(eq, "shares").is_none()) throw OfferError("equity has shares but no share_price: ask the recruiter for the value or the price used");
    return 0.0;
}
inline double py_pow(double a, double b) {
    const double r = std::pow(a, b);
    if (std::isinf(r) && std::isfinite(a) && std::isfinite(b)) throw PyErr("OverflowError", "(34, 'Numerical result out of range')");
    return r;
}
// year-by-year compensation for one offer
inline Val yearly(const Val& offer) {
    if (pget(offer, "base").is_none()) throw OfferError("the offer needs a base salary");
    const double base0 = to_float(item(offer, "base"));
    const Val rp = pget(offer, "raise_pct", Val::integer(0));
    const double raise_pct = to_float(truthy(rp) ? rp : Val::integer(0));
    const Val eq = orv(pget(offer, "equity"), Val::dict());
    const Val sched = truthy(eq) ? schedule(eq) : Val::list({Val::integer(25), Val::integer(25), Val::integer(25), Val::integer(25)});
    const std::size_t years = sched.l->size();
    const double grant = grant_value(eq);
    const Val so = orv(pget(offer, "sign_on", Val::integer(0)), Val::integer(0));
    std::vector<double> so_list;
    if (so.t == Val::LIST) for (const Val& x : *so.l) so_list.push_back(to_float(x));
    else so_list.push_back(to_float(so));
    List rows; std::vector<double> tcs;
    for (std::size_t y = 1; y <= years; ++y) {
        const double base = base0 * py_pow(1 + raise_pct / 100.0, static_cast<double>(y - 1));
        double bonus;
        if (!pget(offer, "bonus").is_none()) bonus = to_float(item(offer, "bonus"));
        else { const Val bp = pget(offer, "bonus_target_pct", Val::integer(0)); bonus = base * to_float(truthy(bp) ? bp : Val::integer(0)) / 100.0; }
        const double equity = grant * (*sched.l)[y - 1].num() / 100.0;
        const double sign_on = y - 1 < so_list.size() ? so_list[y - 1] : 0.0;
        const double tc = base + bonus + equity + sign_on;
        Val r = Val::dict();
        r.d->set("year", Val::integer(static_cast<long long>(y))); r.d->set("base", Val::flt(base)); r.d->set("bonus", Val::flt(bonus));
        r.d->set("equity", Val::flt(equity)); r.d->set("sign_on", Val::flt(sign_on)); r.d->set("tc", Val::flt(tc));
        rows.push_back(r); tcs.push_back(tc);
    }
    const Val other_v = orv(pget(offer, "other"), Val::dict());
    if (other_v.t != Val::DICT) throw PyErr("AttributeError", "'" + tname(other_v) + "' object has no attribute 'values'");
    std::vector<double> others; for (const auto& [_, v] : other_v.d->items) if (is_num(v)) others.push_back(v.num());
    const Val one_time = others.empty() ? Val::integer(0) : Val::flt(fsum(others));
    const double total = fsum(tcs);
    Val t = Val::dict();
    t.d->set("company", pget(offer, "company", Val::str("?"))); t.d->set("title", pget(offer, "title", Val::str("")));
    t.d->set("currency", pget(offer, "currency", Val::str("USD"))); t.d->set("years", Val::list(rows));
    t.d->set("total", Val::flt(total)); t.d->set("average", Val::flt(total / static_cast<double>(rows.size())));
    t.d->set("grant", Val::flt(grant)); t.d->set("schedule", sched); t.d->set("cliff_months", pget(eq, "cliff_months")); t.d->set("one_time", one_time);
    return t;
}
inline const Val& yr(const Val& t, std::size_t k) { return (*get(t, "years").l)[k]; }
inline double tc_of(const Val& t, std::size_t k) { return get(yr(t, k), "tc").f; }
inline std::string sched_text(const Val& t) {
    std::vector<std::string> s; for (const Val& x : *get(t, "schedule").l) s.push_back(g1(x));
    return join(s, "/");
}
inline std::string render_total(const Val& t) {
    const Val cur = get(t, "currency");
    const auto M = [&](const Val& r, const char* k) { return money(get(r, k), cur); };
    std::vector<std::string> out = {pystr(get(t, "company")) + ", " + pystr(get(t, "title")) + ": " + money(get(t, "total"), cur) + " over " +
                                    std::to_string(get(t, "years").l->size()) + " years (average " + money(get(t, "average"), cur) + " a year)"};
    out.push_back("  year        base       bonus      equity     sign-on          TC");
    for (const Val& r : *get(t, "years").l)
        out.push_back("  " + rjust(std::to_string(get(r, "year").i), 4) + "  " + rjust(M(r, "base"), 10) + "  " + rjust(M(r, "bonus"), 10) + "  " +
                      rjust(M(r, "equity"), 10) + "  " + rjust(M(r, "sign_on"), 10) + "  " + rjust(M(r, "tc"), 10));
    const Val cliff = get(t, "cliff_months");
    std::string cl;
    if (truthy(cliff)) {
        if (!is_num(cliff)) throw PyErr("TypeError", "%d format: a real number is required, not " + tname(cliff));
        cl = ", " + pct_d(cliff.num()) + "-month cliff (leave before it and the equity is gone)";
    }
    out.push_back("  equity: " + money(get(t, "grant"), cur) + " granted, vesting " + sched_text(t) + cl);
    if (truthy(get(t, "one_time"))) out.push_back("  one-time (not in TC): " + money(get(t, "one_time"), cur));
    out.push_back("  bonus is a target, equity is valued at grant; neither is guaranteed");
    return join(out, "\n");
}
inline std::string company_str(const Val& t) {
    const Val c = get(t, "company");
    if (c.t != Val::STR) throw PyErr("TypeError", "object of type '" + tname(c) + "' has no len()");
    return c.s;
}
inline std::string render_compare(const List& ts) {
    const Val cur = get(ts[0], "currency");
    std::size_t width = 0; for (const Val& t : ts) width = std::max(width, cplen(company_str(t)));
    width += 2;
    const std::size_t w = std::max<std::size_t>(width, 12);
    std::vector<std::string> out = {"Total compensation by year (TC = base + bonus + equity + sign-on)"};
    std::string hdr = "  " + ljust("year", 8); for (const Val& t : ts) hdr += rjust(company_str(t), w);
    out.push_back(hdr);
    std::size_t years = 0; for (const Val& t : ts) years = std::max(years, get(t, "years").l->size());
    for (std::size_t y = 0; y < years; ++y) {
        std::string row = "  " + ljust(std::to_string(y + 1), 8);
        for (const Val& t : ts) row += rjust(y < get(t, "years").l->size() ? money_d(tc_of(t, y), cur) : "n/a", w);
        out.push_back(row);
    }
    std::string tot = "  " + ljust("total", 8); for (const Val& t : ts) tot += rjust(money(get(t, "total"), cur), w);
    out.push_back(tot);
    std::size_t b1 = 0, ba = 0;
    for (std::size_t k = 1; k < ts.size(); ++k) {
        if (tc_of(ts[k], 0) > tc_of(ts[b1], 0)) b1 = k;
        if (get(ts[k], "total").f > get(ts[ba], "total").f) ba = k;
    }
    out.push_back("");
    out.push_back("Highest in year 1: " + pystr(get(ts[b1], "company")) + ". Highest over the full schedule: " + pystr(get(ts[ba], "company")) + ".");
    for (const Val& t : ts) {
        const double first = tc_of(t, 0), last = tc_of(t, get(t, "years").l->size() - 1);
        if (std::fabs(last - first) > 0.10 * std::max(first, 1.0)) {
            std::vector<std::string> so;
            for (const Val& r : *get(t, "years").l) if (get(r, "sign_on").f != 0) so.push_back(money(get(r, "sign_on"), cur));
            out.push_back("  " + pystr(get(t, "company")) + " changes a lot by year: " + money_d(first, cur) + " in year 1, " + money_d(last, cur) + " in year " +
                          std::to_string(get(t, "years").l->size()) + " (vesting " + sched_text(t) + ", sign-on " + (so.empty() ? "none" : join(so, "/")) + ").");
        }
    }
    return join(out, "\n");
}

// ---------------------------------------------------------------- benchmarks
inline bool market_kind(const Val& k) { return is_str(k, "base") || is_str(k, "total") || is_str(k, "posting_range"); }
inline List load_bench(const Val& paths) {
    List items;
    for (const Val& p : iter(orv(paths, Val::list()))) {
        const Val data = read_json(p);
        if (data.t != Val::DICT) throw PyErr("AttributeError", "'" + tname(data) + "' object has no attribute 'get'");
        const Val b = pget(data, "benchmarks", Val::list());
        if (b.t == Val::LIST) for (const Val& x : *b.l) items.push_back(x);
        else if ((b.t == Val::DICT || b.t == Val::STR) && !truthy(b)) {}
        else if (b.t == Val::DICT || b.t == Val::STR) throw PyErr("AttributeError", "'str' object has no attribute 'get'");
        else throw PyErr("TypeError", "'" + tname(b) + "' object is not iterable");
    }
    for (const Val& b : items) {
        if (!truthy(pget(b, "source"))) throw OfferError("benchmark " + repr(pget(b, "label")) + " has no source: every number needs one (never invent a market number)");
        if (market_kind(pget(b, "kind")) && !truthy(pget(b, "url"))) throw OfferError("benchmark " + repr(pget(b, "label")) + " needs the URL it came from");
        if (!market_kind(pget(b, "kind"))) throw OfferError("benchmark kind must be one of base, total, posting_range, got " + repr(pget(b, "kind")));
    }
    return items;
}
inline const char* PCTS[] = {"p10", "p25", "p50", "p75", "p90"};
// where a value sits against one benchmark, in words
inline std::string position(double value, const Val& b) {
    if (is_str(item(b, "kind"), "posting_range")) {
        const Val lo = pget(b, "min"), hi = pget(b, "max");
        if (lo.is_none() || hi.is_none()) return "range incomplete";
        const double l = num(lo, Val::flt(value)), h = num(hi, Val::flt(value));
        if (value < l) return "below the posted range";
        if (value > h) return "above the posted range";
        double span = h - l;
        if (span == 0) span = 1;
        return std::to_string(py_round(100 * (value - l) / span)) + "% of the way up the posted range";
    }
    std::vector<std::pair<std::string, double>> marks;
    for (const char* k : PCTS) if (!pget(b, k).is_none()) marks.emplace_back(k, num(pget(b, k), Val::flt(value)));
    if (marks.empty()) return "no percentiles";
    std::vector<std::string> below;
    for (const auto& [k, v] : marks) if (value >= v) below.push_back(k);
    if (below.empty()) return marks[0].first == "p10" ? "below the 10th percentile" : "below the lowest percentile given";
    const std::string top = below.back();
    const std::map<std::string, std::string> names = {{"p10", "10th"}, {"p25", "25th"}, {"p50", "median"}, {"p75", "75th"}, {"p90", "90th"}};
    if (top == "p90") return "above the 90th percentile";
    std::size_t at = 0; while (marks[at].first != top) at++;
    if (at + 1 >= marks.size()) throw PyErr("IndexError", "list index out of range");
    const std::string nxt = marks[at + 1].first;
    return "between the " + names.at(top) + " and the " + names.at(nxt) + (nxt == "p50" ? "" : " percentile");
}
inline std::string render_bench(const Val& offer, const List& benches) {
    const Val cur = pget(offer, "currency", Val::str("USD"));
    const double base = to_float(item(offer, "base"));
    std::vector<std::string> out = {pystr(pget(offer, "company", Val::str("Offer"))) + " base " + money_d(base, cur) + " against cited benchmarks:"};
    if (benches.empty()) out.push_back("  none given. Run: offer.py bls --soc 15-1252 --area msa:41940 --out bench.json, or add a posted range with its URL.");
    for (const Val& b : benches) {
        std::vector<std::string> nums;
        for (const char* k : {"p10", "p25", "p50", "p75", "p90", "mean", "min", "max"})
            if (!pget(b, k).is_none()) nums.push_back(std::string(k) + " " + money(item(b, k), cur));
        const std::string pos = position(base, b);
        out.push_back("  " + pystr(pget(b, "label")) + "\n    " + join(nums, ", ") + "\n    your base is " + pos + "\n    source: " + pystr(item(b, "source")) + " (" + pystr(item(b, "url")) + ")");
    }
    return join(out, "\n");
}

// ---------------------------------------------------------------- BLS OEWS
inline bool isalpha_str(const std::string& s) {
    if (s.empty()) return false;
    for (const auto& ch : cps(s)) if (uni::category(cp_of(ch))[0] != 'L') return false;
    return true;
}
inline std::string area_code(const Val& area) {
    const std::string a = strip(truthy(area) ? area.s : std::string("national"));
    const std::string al = lower(a);
    if (al == "national" || al == "us" || al == "usa" || al == "n") return "N0000000";
    if (const auto k = a.find(':'); k != std::string::npos) {
        const std::string kind = lower(a.substr(0, k)), val = a.substr(k + 1);
        if (kind == "msa") {
            if (!isdigit_str(val) || cplen(val) != 5) throw OfferError("msa: needs the 5-digit metro code (CBSA), e.g. msa:41940");
            return "M00" + val;
        }
        if (kind == "state") {
            const auto it = STATE_FIPS.find(upper(val));
            const std::string fips = it != STATE_FIPS.end() ? it->second : (isdigit_str(val) && cplen(val) == 2 ? val : "");
            if (fips.empty()) throw OfferError("state: needs a 2-letter state or a 2-digit FIPS code");
            return "S" + fips + "00000";
        }
    }
    throw OfferError("--area must be national, state:XX or msa:NNNNN");
}
inline std::vector<std::pair<std::string, std::string>> series_ids(const std::string& soc, const Val& area) {
    const std::string occ = replace_all(soc, "-", "");
    if (!isdigit_str(occ) || cplen(occ) != 6) throw OfferError("--soc must look like 15-1252");
    const std::string a = area_code(area);
    std::vector<std::pair<std::string, std::string>> o;
    for (const auto& [k, dt] : DATATYPES) o.emplace_back(k, "OEU" + a + "000000" + occ + dt);
    return o;
}
inline std::string http_reason(long code) {
    static const std::map<long, std::string> R = {{400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"},
        {408, "Request Timeout"}, {429, "Too Many Requests"}, {500, "Internal Server Error"}, {502, "Bad Gateway"}, {503, "Service Unavailable"}, {504, "Gateway Timeout"}};
    const auto it = R.find(code);
    return it == R.end() ? "" : it->second;
}
struct NetError : std::runtime_error { using std::runtime_error::runtime_error; };   // urllib's URLError / HTTPError: an OSError
inline Val fetch_bls(const std::vector<std::pair<std::string, std::string>>& ids, long long year, const char* key) {
    Val body = Val::dict();
    List sid; for (const auto& [_, s] : ids) sid.push_back(Val::str(s));
    body.d->set("seriesid", Val::list(sid)); body.d->set("startyear", Val::str(std::to_string(year))); body.d->set("endyear", Val::str(std::to_string(year)));
    if (key && *key) body.d->set("registrationkey", Val::str(key));
    const HttpResp r = http_post(BLS_API, dumps(body), {"Content-Type: application/json", std::string("User-Agent: ") + UA}, 30);
    if (r.status >= 400) throw NetError("HTTP Error " + std::to_string(r.status) + ": " + http_reason(r.status));
    if (!r.error.empty() || r.status == 0) throw NetError("<urlopen error " + (r.error.empty() ? std::string("no response") : r.error) + ">");
    return loads(r.body);
}
inline Val bls_benchmark(const Val& resp, const std::string& soc, const Val& area, long long year, const Val& occupation, const Val& area_name) {
    if (!is_str(pget(resp, "status"), "REQUEST_SUCCEEDED")) {
        std::vector<std::string> msg;
        for (const Val& m : iter(orv(pget(resp, "message"), Val::list()))) {
            if (m.t != Val::STR) throw PyErr("TypeError", "sequence item " + std::to_string(msg.size()) + ": expected str instance, " + tname(m) + " found");
            msg.push_back(m.s);
        }
        throw OfferError("BLS answered " + pystr(pget(resp, "status")) + ": " + join(msg, "; "));
    }
    const auto ids = series_ids(soc, area);
    std::vector<std::pair<Val, Val>> by_id;   // later series with the same id win, as in a dict
    for (const Val& s : iter(pget(orv(pget(resp, "Results"), Val::dict()), "series", Val::list()))) {
        const Val id = pget(s, "seriesID");
        bool done = false;
        for (auto& [k, v] : by_id) if (k.t == id.t && pystr(k) == pystr(id)) { v = s; done = true; }
        if (!done) by_id.emplace_back(id, s);
    }
    std::vector<std::pair<std::string, double>> vals; List missing; Val used_year;
    const std::string ys = std::to_string(year);
    for (const auto& [k, sid] : ids) {
        Val s;
        for (const auto& [id, v] : by_id) if (id.t == Val::STR && id.s == sid) s = v;
        List pts;
        if (truthy(s)) for (const Val& d : iter(pget(s, "data", Val::list()))) if (pystr(pget(d, "year")) == ys) pts.push_back(d);
        if (pts.empty()) { missing.push_back(Val::str(k)); continue; }
        const std::string raw = replace_all(pystr(pget(pts[0], "value", Val::str(""))), ",", "");
        if (const auto f = parse_float(raw)) { vals.emplace_back(k, *f); used_year = pget(pts[0], "year"); }
        else missing.push_back(Val::str(k));   // BLS prints "-" or "*" when a value is top-coded or not released
    }
    if (vals.empty()) throw OfferError("BLS has no " + ys + " OEWS wages for " + soc + " in " + pystr(area) + " (check the codes, or try the year before)");
    const std::string a = area_code(area);
    const auto sn = SOC_NAMES.find(soc);
    const std::string occ_name = truthy(occupation) ? pystr(occupation) : (sn != SOC_NAMES.end() ? sn->second : "SOC " + soc);
    std::string place;
    if (a[0] == 'S' && truthy(area) && area.s.find(':') != std::string::npos) {
        const std::string state = upper(area.s.substr(area.s.find(':') + 1));
        const std::string guess = isalpha_str(state) ? state : "FIPS " + state;
        place = truthy(area_name) ? pystr(area_name) : "the state of " + guess;
    } else {
        const auto an = AREA_NAMES.find(a);
        place = truthy(area_name) ? pystr(area_name) : an != AREA_NAMES.end() ? an->second : "MSA " + a.substr(3);
    }
    const std::string uy = pystr(used_year);
    std::string p50id; Val series = Val::dict();
    for (const auto& [k, s] : ids) { series.d->set(k, Val::str(s)); if (k == "p50") p50id = s; }
    Val out = Val::dict();
    out.d->set("label", Val::str("BLS OEWS May " + uy + ", " + occ_name + " (SOC " + soc + "), " + place));
    out.d->set("kind", Val::str("base"));
    out.d->set("source", Val::str("U.S. Bureau of Labor Statistics, Occupational Employment and Wage Statistics (OEWS), May " + uy + " estimates, BLS Public Data API"));
    out.d->set("url", Val::str(BLS_API + p50id));
    out.d->set("as_of", Val::str("May " + uy));
    out.d->set("series", series);
    out.d->set("occupation", Val::str(occ_name));
    out.d->set("area", Val::str(place));
    out.d->set("cite", Val::str("BLS wage data (OEWS, May " + uy + ")"));
    out.d->set("note", Val::str("Annual wages for every employer and experience level in this occupation and area. They leave out stock grants and non-production bonuses: compare with base salary only."));
    for (const char* k : {"p10", "p25", "p50", "p75", "p90", "mean"})
        for (const auto& [vk, v] : vals) if (vk == k) out.d->set(k, Val::flt(v));
    if (!missing.empty()) out.d->set("missing", Val::list(missing));
    return out;
}

// ---------------------------------------------------------------- the counter
inline Val ev(double value, const std::string& why, const char* kind, const Val& bench = Val(), const char* stat = nullptr) {
    Val e = Val::dict();
    e.d->set("value", Val::flt(value)); e.d->set("why", Val::str(why)); e.d->set("kind", Val::str(kind));
    if (!bench.is_none()) e.d->set("bench", bench);
    if (stat) e.d->set("stat", Val::str(stat));
    return e;
}
inline double evv(const Val& e) { return get(e, "value").f; }
inline Val counter(const Val& offer, const List& benches, const Val& competing, const Val& floor, double max_bump) {
    const double base = to_float(item(offer, "base"));
    const Val t = yearly(offer);
    List evidence;
    for (const Val& b : benches) {
        const Val kind = item(b, "kind");
        if (is_str(kind, "base"))
            for (const auto& [k, name] : std::vector<std::pair<const char*, const char*>>{{"p50", "median"}, {"p75", "75th percentile"}})
                if (!pget(b, k).is_none() && num(item(b, k)) > base)
                    evidence.push_back(ev(to_float(item(b, k)), pystr(pget(b, "label")) + " " + name, "bench", b, name));
        if (is_str(kind, "posting_range") && !pget(b, "max").is_none() && num(item(b, "max")) > base)
            evidence.push_back(ev(to_float(item(b, "max")), "top of the posted range (" + pystr(pget(b, "label")) + ")", "posting", b));
    }
    Val comp_t;
    if (truthy(competing)) {
        comp_t = yearly(competing);
        if (to_float(item(competing, "base")) > base)
            evidence.push_back(ev(to_float(item(competing, "base")), pystr(pget(competing, "company", Val::str("the other"))) + "'s base offer", "competing"));
    }
    std::optional<double> cap_posting;
    for (const Val& b : benches)
        if (is_str(item(b, "kind"), "posting_range") && !pget(b, "max").is_none()) {
            const double m = to_float(item(b, "max"));
            if (!cap_posting || m < *cap_posting) cap_posting = m;
        }
    std::optional<double> ask; Val capped_by;
    if (!evidence.empty()) {
        std::size_t top = 0;
        for (std::size_t k = 1; k < evidence.size(); ++k) if (evv(evidence[k]) > evv(evidence[top])) top = k;
        double a = evv(evidence[top]);
        if (!is_str(get(evidence[top], "kind"), "competing") && a > base * (1 + max_bump)) {
            a = base * (1 + max_bump);
            capped_by = Val::str("the " + std::to_string(py_round(max_bump * 100)) + "% cap on a base ask (change with --max-bump)");
        }
        if (cap_posting && a > *cap_posting) { a = *cap_posting; capped_by = Val::str("the top of the posted range"); }
        a = static_cast<double>(round_up(a));
        if (cap_posting && a > *cap_posting) a = *cap_posting;
        ask = a;
    }
    const bool ask_true = ask && *ask != 0;
    const Val middle = ask_true ? Val::flt(static_cast<double>(round_up((base + *ask) / 2.0))) : Val();
    Val sign_on_ask, gaps;
    if (truthy(comp_t) && tc_of(comp_t, 0) > tc_of(t, 0)) sign_on_ask = Val::flt(static_cast<double>(round_up(tc_of(comp_t, 0) - tc_of(t, 0))));
    if (truthy(comp_t)) {
        gaps = Val::dict();
        gaps.d->set("year1", Val::flt(tc_of(comp_t, 0) - tc_of(t, 0)));
        gaps.d->set("total", Val::flt(get(comp_t, "total").f - get(t, "total").f));
    }
    List pool;
    if (ask_true) for (const Val& e : evidence) if (evv(e) <= *ask + 0.5) pool.push_back(e);
    if (pool.empty()) pool = evidence;
    std::stable_sort(pool.begin(), pool.end(), [](const Val& x, const Val& y) { return -evv(x) < -evv(y); });
    if (pool.size() > 2) pool.resize(2);
    Val c = Val::dict();
    c.d->set("base", Val::flt(base)); c.d->set("ask", ask ? Val::flt(*ask) : Val()); c.d->set("middle", middle); c.d->set("floor", floor);
    c.d->set("below_floor", Val::boolean(!floor.is_none() && base < floor.num())); c.d->set("capped_by", capped_by);
    c.d->set("evidence", Val::list(evidence)); c.d->set("support", Val::list(pool)); c.d->set("sign_on_ask", sign_on_ask);
    c.d->set("gaps", gaps); c.d->set("offer", t); c.d->set("competing", comp_t);
    return c;
}
inline std::string script(const Val& c, const Val& offer, const Val& me, const Val& recruiter, bool share_competing) {
    const Val cur = pget(offer, "currency", Val::str("USD"));
    std::string first = "[your name]";
    if (truthy(me)) {
        const auto parts = split_ws(me.s);
        if (parts.empty()) throw PyErr("IndexError", "list index out of range");
        first = parts[0];
    }
    const std::string hello = truthy(recruiter) ? "Hi " + pystr(recruiter) + "," : "Hi [recruiter's first name],";
    const Val tv = pget(offer, "title");
    const std::string title = truthy(tv) ? pystr(tv) : "the role";
    std::vector<std::string> lines = {hello, "", "Thank you for the offer for the " + title + " role at " + pystr(pget(offer, "company", Val::str("the company"))) +
                                                 ". I am excited about the team and I want to make this work."};
    if (truthy(get(c, "ask"))) {
        std::vector<std::string> cites;
        std::vector<std::pair<Val, std::vector<std::string>>> by_bench;
        List sup = *get(c, "support").l;
        std::stable_sort(sup.begin(), sup.end(), [](const Val& x, const Val& y) { return evv(x) < evv(y); });
        for (const Val& e : sup) {
            const Val kind = get(e, "kind");
            if (is_str(kind, "competing")) { if (share_competing) cites.push_back("I also have an offer with a base of " + money(get(e, "value"), cur)); }
            else if (is_str(kind, "posting")) cites.push_back("the posted range for this role goes up to " + money(get(e, "value"), cur));
            else {
                const Val b = item(e, "bench");
                std::size_t at = by_bench.size();
                for (std::size_t k = 0; k < by_bench.size(); ++k) if (by_bench[k].first.d.get() == b.d.get()) { at = k; break; }
                if (at == by_bench.size()) by_bench.emplace_back(b, std::vector<std::string>{});
                by_bench[at].second.push_back("the " + pystr(get(e, "stat")) + " at " + money(get(e, "value"), cur));
            }
        }
        for (const auto& [b, stats] : by_bench) {
            std::string who;
            if (truthy(pget(b, "occupation")) && truthy(pget(b, "area"))) {
                const Val occ = item(b, "occupation");
                if (occ.t != Val::STR) throw PyErr("AttributeError", "'" + tname(occ) + "' object has no attribute 'lower'");
                who = lower(occ.s) + " in the " + pystr(item(b, "area")) + " area";
            } else who = truthy(pget(b, "label")) ? pystr(pget(b, "label")) : "this role";
            const Val cite = pget(b, "cite");
            cites.insert(cites.begin(), (truthy(cite) ? pystr(cite) : pystr(item(b, "source"))) + " for " + who + " puts " + join(stats, " and "));
        }
        std::string reason = cites.empty() ? "" : join(cites, "; ") + ", so ";
        if (!reason.empty()) { const auto c0 = cps(reason); reason = upper(c0[0]) + reason.substr(c0[0].size()); }
        lines.push_back("");
        lines.push_back(reason + "I would like to ask for a base salary of " + money(get(c, "ask"), cur) + ".");
        if (is_str(get(c, "capped_by"), "the top of the posted range")) lines.push_back("If base is fixed at the top of the range, a sign-on bonus would help close the gap.");
    } else {
        lines.push_back("");
        lines.push_back("Is there any flexibility on base salary or a sign-on bonus?");
    }
    if (truthy(get(c, "sign_on_ask")) && share_competing)
        lines.push_back("My other offer is " + money(get(get(c, "gaps"), "year1"), cur) + " higher in the first year, so a sign-on bonus of " +
                        money(get(c, "sign_on_ask"), cur) + " would close that gap.");
    for (const char* l : {"", "Is there flexibility there? If we can get closer, I am ready to move quickly.", "", "Thank you,"}) lines.push_back(l);
    lines.push_back(first);
    return join(lines, "\n");
}
inline std::string render_counter(const Val& c, const Val& offer, const Val& me, const Val& recruiter, bool share_competing) {
    const Val cur = pget(offer, "currency", Val::str("USD"));
    std::vector<std::string> out = {"Counter for " + pystr(pget(offer, "company")) + ", " + pystr(pget(offer, "title", Val::str(""))) + " (base " + money(get(c, "base"), cur) + ")"};
    if (get(c, "below_floor").b) out.push_back("  WARNING: the base is below your floor of " + money(get(c, "floor"), cur) + ".");
    List evd = *get(c, "evidence").l;
    if (!evd.empty()) {
        out.push_back("  Evidence above your base:");
        std::stable_sort(evd.begin(), evd.end(), [](const Val& x, const Val& y) { return -evv(x) < -evv(y); });
        for (const Val& e : evd) out.push_back("    " + rjust(money(get(e, "value"), cur), 10) + "  " + get(e, "why").s);
    } else out.push_back("  No cited number is above your base. Ask about flexibility, sign-on or equity, without a number.");
    if (truthy(get(c, "ask"))) {
        out.push_back("  Ask:    " + money(get(c, "ask"), cur) + (truthy(get(c, "capped_by")) ? "  (capped by " + get(c, "capped_by").s + ")" : ""));
        out.push_back("  Middle: " + money(get(c, "middle"), cur) + "  (a result you would be glad to take)");
    }
    out.push_back("  Floor:  " + (get(c, "floor").is_none() ? std::string("yours to set (--floor); the tool never picks it") : money(get(c, "floor"), cur)));
    if (truthy(get(c, "gaps")))
        out.push_back("  Against " + pystr(get(get(c, "competing"), "company")) + ": year 1 " + money(get(get(c, "gaps"), "year1"), cur) + ", full schedule " +
                      money(get(get(c, "gaps"), "total"), cur) + " (positive = the other offer is higher).");
    for (const char* l : {"", "Script (a DRAFT: you send it, from your own email, after you approve it):", ""}) out.push_back(l);
    out.push_back(script(c, offer, me, recruiter, share_competing));
    out.push_back("");
    out.push_back("No outcome is guaranteed. Not financial, tax or legal advice.");
    return join(out, "\n");
}

// ---------------------------------------------------------------- CLI
inline Val profile_floor(const std::string& path) {
    if (path.empty() || !exists(path)) return Val();
    Val p;
    try { p = read_json(Val::str(path)); } catch (const OfferError&) { return Val(); }
    Val v = pget(p, "salary_floor");
    if (v.t == Val::DICT) { v = pget(v, "base"); if (!truthy(v)) v = pget(pget(p, "salary_floor"), "amount"); }
    if (v.is_none()) return Val();
    try { return Val::flt(to_float(v)); } catch (const PyErr& e) { if (e.type == "ValueError" || e.type == "TypeError") return Val(); throw; }
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("offer", "Offers, cited benchmarks and a counter script.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("total"); s.help_line = "year-by-year total compensation for one offer"; s.pos("offer"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("compare"); s.help_line = "two or more offers side by side"; s.pos("offers").nargs("+"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("bls"); s.help_line = "OEWS wage percentiles from the BLS API (or --offline FILE)";
      s.opt({"--soc"}).required().help("occupation code, e.g. 15-1252 (Software Developers)");
      s.opt({"--area"}).def(Val::str("national")).help("national, state:CA or msa:41940");
      s.opt({"--year"}).integer().def(Val::integer(2025)).help("OEWS reference year (May of that year)");
      s.opt({"--occupation"}).help("name for the label"); s.opt({"--area-name"}).help("name for the label");
      s.opt({"--offline"}).help("a saved BLS API response (JSON) instead of the network"); s.opt({"--out"}).help("add the benchmark to this bench file"); }
    { Parser& s = p.sub("bench"); s.help_line = "where the base sits against cited benchmarks"; s.pos("offer"); s.opt({"--bench"}).append().def(Val::list()); }
    { Parser& s = p.sub("counter"); s.help_line = "the counter range and a script"; s.pos("offer"); s.opt({"--bench"}).append().def(Val::list());
      s.opt({"--competing"}); s.opt({"--floor"}).number(); s.opt({"--profile"}).def(Val::str(path_join("hunter", "profile.json")));
      s.opt({"--max-bump"}).number().def(Val::flt(MAX_BUMP)).help("cap on a base ask as a fraction (default 0.15)");
      s.opt({"--me"}); s.opt({"--recruiter"});
      s.opt({"--share-competing"}).store_true().help("name the competing offer in the script (only if it is real and you are fine sharing it)");
      s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    DumpOpts jo; jo.indent = 2;
    try {
        if (cmd == "total") {
            const Val t = yearly(read_json(get(a, "offer")));
            print(B(a, "json") ? dumps(t, jo) : render_total(t));
            return 0;
        }
        if (cmd == "compare") {
            List ts; for (const Val& x : *get(a, "offers").l) ts.push_back(yearly(read_json(x)));
            print(B(a, "json") ? dumps(Val::list(ts), jo) : render_compare(ts));
            return 0;
        }
        if (cmd == "bls") {
            const std::string soc = S(a, "soc");
            const auto ids = series_ids(soc, get(a, "area"));
            const long long year = get(a, "year").i;
            const Val resp = truthy(get(a, "offline")) ? read_json(get(a, "offline")) : fetch_bls(ids, year, std::getenv("BLS_API_KEY"));
            const Val b = bls_benchmark(resp, soc, get(a, "area"), year, get(a, "occupation"), get(a, "area_name"));
            if (truthy(get(a, "out"))) {
                const std::string path = S(a, "out");
                Val data = exists(path) ? read_json(Val::str(path)) : Val::dict();
                if (!exists(path)) data.d->set("benchmarks", Val::list());
                List keep;
                for (const Val& x : iter(pget(data, "benchmarks", Val::list())))
                    if (!(pget(x, "label").t == Val::STR && pget(x, "label").s == get(b, "label").s)) keep.push_back(x);
                keep.push_back(b);
                data.d->set("benchmarks", Val::list(keep));
                write_json(path, data);
                print("Added to " + path + ": " + get(b, "label").s);
            }
            print(dumps(b, jo));
            return 0;
        }
        const Val offer = read_json(get(a, "offer"));
        const List benches = load_bench(get(a, "bench"));
        if (cmd == "bench") { print(render_bench(offer, benches)); return 0; }
        if (cmd == "counter") {
            const Val floor = !get(a, "floor").is_none() ? get(a, "floor") : profile_floor(S(a, "profile"));
            const Val comp = truthy(get(a, "competing")) ? read_json(get(a, "competing")) : Val();
            const Val c = counter(offer, benches, comp, floor, get(a, "max_bump").num());
            if (B(a, "json")) {
                const std::string text = script(c, offer, get(a, "me"), get(a, "recruiter"), B(a, "share_competing"));   // before the bench references go
                Val slim = Val::dict();
                for (const auto& [k, v] : c.d->items) if (k != "offer" && k != "competing") slim.d->set(k, v);
                const auto strip_bench = [](const Val& lst) {
                    List o;
                    for (const Val& e : *lst.l) { Val n = Val::dict(); for (const auto& [k, v] : e.d->items) if (k != "bench") n.d->set(k, v); o.push_back(n); }
                    return Val::list(o);
                };
                slim.d->set("evidence", strip_bench(get(c, "evidence")));
                slim.d->set("support", strip_bench(get(c, "support")));
                slim.d->set("script", Val::str(text));
                print(dumps(slim, jo));
            } else print(render_counter(c, offer, get(a, "me"), get(a, "recruiter"), B(a, "share_competing")));
            return 0;
        }
    } catch (const OfferError& e) {
        eprint(std::string("offer: ") + e.what()); return 2;
    } catch (const NetError& e) {
        eprint(std::string("offer: ") + e.what()); return 2;
    } catch (const OSError& e) {
        eprint(std::string("offer: ") + e.what()); return 2;
    }
    return 2;
}
PACKTOOL("hunter-salary/offer", main);
}  // namespace packtools::hunter_salary
