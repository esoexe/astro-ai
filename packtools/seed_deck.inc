// seed-deck/deck: turns seed/plan.json (seed-plan's numbers block) into a 10-slide pitch deck, ONE self-contained HTML
// file (inline CSS and JS, 16:9, arrow keys, a print stylesheet), and lints it: 35 words a slide, one idea a slide,
// the use of funds adds up, every number traces to a seed/ file. A C++ port of seed-skill/seed-deck/deck.py (MIT, Jake
// Schincariol); same output, files and exit codes: build 0, outline 0, lint 0 clean / 1 problems, 2 bad input.
namespace packtools::seed_deck {
using namespace pyrt;
using namespace packtools::seed;
using seed_scout::pct_d;

#include "seed_deck_assets.inc"

struct DeckError : std::runtime_error { int code; explicit DeckError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {} };

inline const std::vector<std::pair<std::string, std::string>> SECTIONS = {{"executive_summary", "Executive summary"}, {"problem", "Problem"},
    {"solution", "Solution"}, {"market", "Market"}, {"competition", "Competition"}, {"business_model", "Business model"},
    {"go_to_market", "Go-to-market"}, {"financial_plan", "Financial plan"}, {"use_of_funds", "Use of funds"}};
inline const std::vector<std::string> ORDER = {"title", "problem", "solution", "market", "competition", "business_model", "go_to_market", "team", "financials", "ask"};
inline constexpr long long MAX_WORDS = 35, MAX_SENTENCES = 2, MAX_POINTS = 3;
inline const char* const MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
inline const char* const NUM_RE = R"((?<![\w.])(\$)?(\d{1,3}(?:,\d{3})+|\d+)(?:\.(\d+))?(?:\s?(thousand|million|billion|bn|[KkMB])(?![A-Za-z]))?(\s?%|x(?![A-Za-z]))?(?![\w]))";
inline const char* const DATE_RE = R"(^\d{4}-\d{2}(-\d{2})?([T ].*)?$)";
inline const char* const HEX = R"(^#[0-9A-Fa-f]{6}$)";

// ---------------------------------------------------------------- dynamic-typing helpers (Python's errors, not silence)
inline Val pget(const Val& o, const std::string& k) {   // o.get(k)
    if (o.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(o) + "' object has no attribute 'get'");
    return get(o, k);
}
inline Val req(const Val& o, const std::string& k) {   // o[k]
    if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    if (!o.d->get(k)) throw PyErr("KeyError", str_repr(k));
    return get(o, k);
}
inline Val vor(const Val& v, const Val& dflt) { return truthy(v) ? v : dflt; }   // v or dflt
inline std::string strip_str(const Val& v) {   // v.strip() (v must be a str)
    if (v.t != Val::STR) throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute 'strip'");
    return strip(v.s);
}
inline std::string join_str(const Val& v) {   // an element of " · ".join(...)
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("sequence item: expected str instance, ") + type_name(v) + " found");
    return v.s;
}
inline bool is_none(const Val& v) { return v.is_none(); }

// ---------------------------------------------------------------- numbers (Decimal, like the original)
inline Decimal one_e(long n) { return Decimal::triple(false, BigInt(1), n); }   // Decimal(1).scaleb(n)
inline Decimal scaleb(const Decimal& d, long n) { return d * one_e(n); }   // the same value and context rounding as d.scaleb(n)
inline Decimal D(const Val& x) {
    if (x.t == Val::BOOL || x.t == Val::NONE) throw DeckError("not a number: " + repr(x));
    if (x.t == Val::INT) return Decimal::from_int(big_of(x));
    if (x.t == Val::FLOAT) return Decimal::parse(float_repr(x.f));
    std::string s = strip(pystr(x));
    std::size_t k = 0; while (k < s.size() && s[k] == '$') ++k;
    s = replace_all(s.substr(k), ",", "");
    try { return Decimal::parse(ascii_digits(s)); } catch (const DecimalError&) { throw DeckError("not a number: " + repr(x)); }
}
inline Decimal rnd(const Decimal& d, long dp) { return d.quantize(one_e(-dp), Rounding::HALF_UP); }
inline Decimal rnd(const Val& x, long dp) { return rnd(D(x), dp); }
inline std::string trim(const Decimal& q) {
    const std::string s = q.format("f");
    return s.find('.') != std::string::npos ? rstrip_chars(rstrip_chars(s, "0"), ".") : s;
}
inline std::string money(const Decimal& d, int dp = -1) {
    if (dp < 0) dp = d == d.to_integral_value() ? 0 : 2;
    const Decimal q = rnd(d, dp);
    return std::string(q < Decimal(0) ? "-" : "") + "$" + q.abs().format(",f");
}
inline std::string money(const Val& x, int dp = -1) { return money(D(x), dp); }
inline std::string short_money(const Decimal& d) {
    const std::string sign = d < Decimal(0) ? "-" : "";
    const Decimal a = d.abs();
    if (a >= Decimal::parse("1e9")) return sign + "$" + trim(rnd(a / Decimal::parse("1e9"), 1)) + "B";
    if (a >= Decimal::parse("1e6")) return sign + "$" + trim(rnd(a / Decimal::parse("1e6"), a >= Decimal::parse("1e7") ? 1 : 2)) + "M";
    if (a >= Decimal::parse("1e3")) return sign + "$" + trim(rnd(a / Decimal::parse("1e3"), a >= Decimal::parse("1e5") ? 0 : 1)) + "K";
    return sign + "$" + trim(rnd(a, 0));
}
inline std::string short_money(const Val& x) { return short_money(D(x)); }
inline std::string pct(const Val& x, long dp = 1) { return trim(rnd(x, dp)) + "%"; }
inline std::string count(const Val& x) {   // "{:,}".format(int(D(x)))
    const Decimal d = D(x);
    if (d.is_nan()) throw PyErr("ValueError", "cannot convert NaN to integer");
    if (d.is_infinite()) throw PyErr("OverflowError", "cannot convert Infinity to integer");
    return commas(d.to_int());
}
inline std::string month_name(const Val& ym) {
    const std::string s = pystr(vor(ym, sv("")));
    const re::Match m = re::match(R"(^(\d{4})-(\d{2})$)", s);
    if (!m) return s;
    const long idx = std::stol(ascii_digits(m.group(2))) - 1;
    if (idx > 11) throw PyErr("IndexError", "list index out of range");
    return std::string(MONTH_NAMES[idx < 0 ? 11 : idx]) + " " + m.group(1);
}
struct Tok { std::string raw, kind; Decimal base; long dp; long scale; };
inline std::vector<Tok> tokens(const std::string& text) {
    static const std::string pat = NUM_RE;
    const re::Regex& rx = re::compile(pat);
    std::vector<Tok> out;
    for (const auto& m : rx.finditer(text)) {
        const std::string whole = ascii_digits(replace_all(m.group(2), ",", "")), frac = m.has(3) ? ascii_digits(m.group(3)) : "";
        const Decimal base = Decimal::parse(whole + (frac.empty() ? "" : "." + frac));
        long scale = 0;
        if (m.has(4) && !m.group(4).empty()) {
            const std::string s = lower(m.group(4));
            scale = (s == "k" || s == "thousand") ? 3 : (s == "m" || s == "million") ? 6 : (s == "b" || s == "bn" || s == "billion") ? 9 : 0;
        }
        const std::string tail = m.has(5) ? m.group(5) : "";
        const std::string kind = !tail.empty() && tail.find('%') != std::string::npos ? "pct" : !tail.empty() ? "mult" : (m.has(1) && !m.group(1).empty()) ? "money" : "plain";
        out.push_back({strip(m.group(0)), kind, base, static_cast<long>(frac.size()), scale});
    }
    return out;
}
inline bool worth_checking(const Tok& t) {
    if (t.kind == "plain" && !t.scale && t.dp == 0) {
        if (t.base <= Decimal(10)) return false;
        if (Decimal(1900) <= t.base && t.base <= Decimal(2100) && t.raw.find(',') == std::string::npos) return false;
    }
    return true;
}
inline void walk_known(const Val& x, std::vector<Decimal>& vals) {
    switch (x.t) {
        case Val::BOOL: case Val::NONE: return;
        case Val::INT: case Val::FLOAT: vals.push_back(D(x)); return;
        case Val::STR:
            if (re::match(DATE_RE, x.s)) return;
            for (const auto& t : tokens(x.s)) vals.push_back(scaleb(t.base, t.scale));
            return;
        case Val::DICT: for (const auto& [k, v] : x.d->items) { walk_known(sv(k), vals); walk_known(v, vals); } return;
        case Val::LIST: for (const Val& v : *x.l) walk_known(v, vals); return;
        default: return;
    }
}
inline bool matches(const Tok& t, const std::vector<Decimal>& known) {
    std::string digits = replace_all(t.base.format("f"), ".", "");
    std::size_t z = 0; while (z < digits.size() && digits[z] == '0') ++z;
    const bool exact = digits.size() - z < 2;   // "$9B" is not a fair rounding of 8.52B: one digit must be exact
    for (const Decimal& k : known) {
        std::vector<Decimal> cands = {k.abs()};
        if (t.kind == "pct") cands.push_back(k.abs() * Decimal(100));
        for (const Decimal& c : cands) {
            const Decimal v = scaleb(c, -t.scale);
            if (exact ? (v == t.base) : (rnd(v, t.dp) == t.base)) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- colours
inline int hex2(const std::string& h, std::size_t i) { return static_cast<int>(std::stol(h.substr(i, 2), nullptr, 16)); }
inline std::string mix(const std::string& a, const std::string& b, double t) {
    char buf[8];
    int c[3];
    for (int i = 0; i < 3; ++i) c[i] = static_cast<int>(std::nearbyint(hex2(a, 1 + 2 * i) * t + hex2(b, 1 + 2 * i) * (1 - t)));
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", c[0], c[1], c[2]);
    return buf;
}
struct Brand { std::string brand, ink, accent; };
inline Brand brand_of(const Val& plan, const std::map<std::string, std::string>& over) {
    std::vector<std::pair<std::string, Val>> b = {{"brand", sv("#2F6FEB")}, {"ink", sv("#0E1B33")}, {"accent", sv("#FFB020")}};
    const Val pb = vor(pget(plan, "brand"), Val::dict());
    if (pb.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(pb) + "' object has no attribute 'items'");
    for (const auto& [k, v] : pb.d->items)
        for (auto& e : b) if (e.first == k && truthy(v)) e.second = v;
    for (auto& e : b) { auto it = over.find(e.first); if (it != over.end() && !it->second.empty()) e.second = sv(it->second); }
    for (const auto& e : b)
        if (!re::match(HEX, pystr(e.second))) throw DeckError("the " + e.first + " colour wants #RRGGBB, like #2F6FEB (got " + repr(e.second) + ")");
    return {pystr(b[0].second), pystr(b[1].second), pystr(b[2].second)};
}

// ---------------------------------------------------------------- the slides
struct Item { std::string a, b, c, d; Decimal num; bool us = false; };
struct Block { std::string type, text, caption; std::vector<Item> items; };
struct Slide { std::string key, title, foot; std::vector<Block> blocks; long n = 0; };
struct Sec { std::string title, pitch; std::vector<std::string> points; };

inline std::map<std::string, Sec> section_map(const Val& plan) {
    std::map<std::string, Val> have;
    for (const Val& s : items_of(vor(pget(plan, "sections"), Val::list())))
        if (s.t == Val::DICT) { const Val k = get(s, "key"); if (k.t == Val::STR) have[k.s] = s; }
    std::map<std::string, Sec> out;
    for (const auto& [key, title] : SECTIONS) {
        auto it = have.find(key);
        const Val s = it != have.end() && truthy(it->second) ? it->second : Val::dict();
        const std::string text = strip_str(vor(get(s, "text"), sv(""))); 
        std::string pitch = strip_str(vor(get(s, "pitch"), sv("")));
        if (pitch.empty() && !text.empty()) {
            const re::Match m = re::match(R"(^(.+?[.!?])(\s|$))", text);
            pitch = m ? m.group(1) : text;
        }
        Sec sec{title, pitch, {}};
        for (const Val& p : items_of(vor(get(s, "points"), Val::list()))) if (truthy(p)) sec.points.push_back(strip(pystr(p)));
        out[key] = sec;
    }
    return out;
}
inline std::string cap(const std::string& t) { return upper(cpslice(t, 0, 1)) + cpslice(t, 1, static_cast<long>(cplen(t))); }
inline std::string theme_word(const Val& theme) {
    const std::string w = strip(split(pystr(vor(theme, sv(""))), "/")[0]);
    return w.empty() ? "issues" : w;
}
inline bool eq_one(const Val& v) { return (v.t == Val::INT && big_of(v) == BigInt(1)) || (v.t == Val::FLOAT && v.f == 1.0) || (v.t == Val::BOOL && v.b); }

inline std::vector<Slide> build_slides(const Val& plan) {
    const Val st = vor(pget(plan, "startup"), Val::dict()), fo = vor(pget(plan, "founder"), Val::dict()), n = vor(pget(plan, "numbers"), Val::dict());
    auto sec = section_map(plan);
    const std::string name = pystr(vor(pget(st, "name"), sv("Your startup")));
    const Val ue = pget(n, "unit_economics"), fin = pget(n, "financials"), mk = pget(n, "market");
    std::vector<Val> comps;
    for (const Val& c : items_of(vor(pget(n, "competitors"), Val::list()))) if (truthy(pget(c, "name")) && comps.size() < 3) comps.push_back(c);
    const Val raise_amt = pget(n, "raise");
    const std::string stage = pystr(vor(pget(st, "stage"), sv("seed")));
    std::vector<Slide> S;

    std::vector<std::string> meta;
    if (truthy(raise_amt)) meta.push_back("Raising a " + short_money(raise_amt) + " " + stage);
    if (truthy(pget(n, "instrument"))) meta.push_back(join_str(req(n, "instrument")));
    std::vector<std::string> whov;
    for (const char* k : {"name", "email"}) { const Val x = pget(fo, k); if (truthy(x)) whov.push_back(join_str(x)); }
    {
        const Val tg = vor(pget(plan, "tagline"), sv(""));
        std::string tagline = strip_str(tg);
        if (tagline.empty()) { const Val ol = pget(st, "one_liner"); tagline = truthy(ol) ? pystr(ol) : ""; }
        S.push_back({"title", name, join(whov, " · "), {Block{"tagline", tagline, "", {}}, Block{"meta", join(meta, " · "), "", {}}}});
    }

    std::vector<Block> blocks = {Block{"lead", sec["problem"].pitch, "", {}}};
    std::vector<Item> stats;
    for (std::size_t i = 0; i < comps.size() && i < 2; ++i) {
        const Val& c = comps[i];
        if (!is_none(pget(c, "top_count")) && truthy(pget(c, "low_reviews"))) {
            const Val low = vor(pget(n, "low_stars"), iv(2));
            stats.push_back({pystr(req(c, "top_count")) + " of " + pystr(req(c, "low_reviews")),
                             "recent " + (eq_one(low) ? std::string("1★") : "1-" + pystr(low) + "★") + " " + pystr(req(c, "name")) + " reviews mention " +
                                 theme_word(pget(c, "top_theme")), "", "", Decimal(0), false});
        }
    }
    if (!stats.empty()) blocks.push_back({"stats", "", "", stats});
    else if (!sec["problem"].points.empty()) {
        Block b{"points", "", "", {}};
        for (std::size_t i = 0; i < sec["problem"].points.size() && i < static_cast<std::size_t>(MAX_POINTS + 1); ++i) b.items.push_back({sec["problem"].points[i], "", "", "", Decimal(0), false});
        blocks.push_back(b);
    }
    std::string foot;
    if (!stats.empty()) {
        std::set<std::string> read;
        for (const Val& c : comps) { const Val r = pget(c, "reviews_read"); if (truthy(r)) read.insert(pystr(r)); }
        const Val cg = pget(n, "competitors_generated");
        foot = "Source: App Store reviews" + (read.size() == 1 ? ", the " + *read.begin() + " most recent per app" : std::string()) +
               (truthy(cg) ? ", read " + pystr(cg) : std::string()) + " (seed/competitors.json).";
    }
    S.push_back({"problem", "The problem", foot, blocks});

    auto lead_points = [&](const std::string& key, const std::string& type) {
        std::vector<Block> bl = {Block{"lead", sec[key].pitch, "", {}}};
        if (!sec[key].points.empty()) {
            Block b{type, "", "", {}};
            for (const auto& p : sec[key].points) b.items.push_back({p, "", "", "", Decimal(0), false});
            bl.push_back(b);
        }
        return bl;
    };
    S.push_back({"solution", "The solution", "", lead_points("solution", "points")});

    blocks = {Block{"lead", sec["market"].pitch, "", {}}};
    foot.clear();
    if (truthy(mk) && !is_none(pget(mk, "tam")) && !is_none(pget(mk, "sam")) && !is_none(pget(mk, "som"))) {
        blocks.push_back({"market", "", "", {{"TAM", short_money(req(mk, "tam")), "the whole market", "", Decimal(0), false},
                                             {"SAM", short_money(req(mk, "sam")), "what we can serve", "", Decimal(0), false},
                                             {"SOM", short_money(req(mk, "som")), "what we can win", "", Decimal(0), false}}});
        std::vector<std::string> srcs;
        for (const Val& it : items_of(vor(pget(mk, "inputs"), Val::list()))) {
            const std::string s = pystr(vor(pget(it, "source"), sv("")));
            if (!s.empty() && lower(s).rfind("assumption", 0) != 0 && std::find(srcs.begin(), srcs.end(), s) == srcs.end()) srcs.push_back(s);
        }
        foot = (srcs.empty() ? std::string() : "Sources: " + join(srcs, "; ") + ". ") + "Method: " + pystr(vor(pget(mk, "method"), sv("see the plan"))) + " (seed/market.json).";
    }
    S.push_back({"market", "The market", foot, blocks});

    blocks = {Block{"lead", sec["competition"].pitch, "", {}}};
    std::vector<Item> cards;
    for (const Val& c : comps) {
        std::vector<std::string> sub;
        if (!is_none(pget(c, "rating"))) sub.push_back(pystr(req(c, "rating")) + "★");
        if (!is_none(pget(c, "ratings"))) sub.push_back(count(req(c, "ratings")) + " ratings");
        const Val wp = pget(c, "weak_point");
        cards.push_back({pystr(req(c, "name")), join(sub, " · "), "Weak point", cap(truthy(wp) ? pystr(wp) : theme_word(pget(c, "top_theme"))), Decimal(0), false});
    }
    if (!cards.empty()) {
        cards.push_back({name, "", "", !sec["executive_summary"].pitch.empty() ? sec["executive_summary"].pitch : sec["solution"].pitch, Decimal(0), true});
        blocks.push_back({"cards", "", "", cards});
    }
    S.push_back({"competition", "Competition", comps.empty() ? "" : "Ratings from the App Store (seed/competitors.json).", blocks});

    blocks = {Block{"lead", sec["business_model"].pitch, "", {}}};
    if (truthy(ue)) {
        const std::string u = pystr(vor(pget(ue, "unit"), sv("order")));
        std::vector<Item> tiles = {{money(req(ue, "price")), "a " + u, "", "", Decimal(0), false},
                                   {money(req(ue, "revenue_per_order"), 2), "we keep (" + pct(req(ue, "take_rate_pct")) + ")", "", "", Decimal(0), false},
                                   {money(req(ue, "contribution_per_order"), 2), "contribution a " + u, "", "", Decimal(0), false}};
        if (!is_none(pget(ue, "ltv_cac"))) tiles.push_back({trim(rnd(req(ue, "ltv_cac"), 2)) + "x", "LTV:CAC", "", "", Decimal(0), false});
        if (!is_none(pget(ue, "cac_payback_months"))) tiles.push_back({trim(rnd(req(ue, "cac_payback_months"), 2)) + " mo", "CAC payback", "", "", Decimal(0), false});
        blocks.push_back({"tiles", "", "", tiles});
    }
    S.push_back({"business_model", "Business model", truthy(ue) ? "Unit economics from seed/model.json assumptions." : "", blocks});

    S.push_back({"go_to_market", "Go to market", "", lead_points("go_to_market", "steps")});

    std::vector<Item> team;
    for (const Val& t : items_of(vor(pget(plan, "team"), Val::list())))
        if (t.t == Val::DICT && truthy(get(t, "name")) && team.size() < 4)
            team.push_back({pystr(req(t, "name")), pystr(vor(get(t, "role"), sv(""))), pystr(vor(get(t, "line"), sv(""))), "", Decimal(0), false});
    S.push_back({"team", "Team", "", team.empty() ? std::vector<Block>{} : std::vector<Block>{Block{"team", "", "", team}}});

    blocks = {Block{"lead", sec["financial_plan"].pitch, "", {}}};
    foot.clear();
    if (truthy(fin) && !is_none(pget(fin, "revenue_y1"))) {
        Block bars{"bars", "", "Revenue", {}};
        for (int y = 1; y <= 3; ++y) {
            const Val v = req(fin, "revenue_y" + std::to_string(y));
            const Decimal dv = D(v);
            bars.items.push_back({"Year " + std::to_string(y), short_money(v), "", "", dv, false});
        }
        blocks.push_back(bars);
        Block side{"side", "", "", {}};
        if (truthy(pget(fin, "breakeven_month")))
            side.items.push_back({"Month " + pct_d(req(fin, "breakeven_month")), "breakeven (" + month_name(pget(fin, "breakeven_date")) + ")", "", "", Decimal(0), false});
        if (truthy(pget(fin, "peak_burn"))) side.items.push_back({short_money(req(fin, "peak_burn")), "peak monthly burn", "", "", Decimal(0), false});
        if (truthy(pget(fin, "cash_out_month"))) side.items.push_back({pct_d(req(fin, "runway_months")) + " months", "of runway", "", "", Decimal(0), false});
        blocks.push_back(side);
        foot = "From seed/model.json, " + month_name(pget(fin, "start_month")) + " to " + month_name(pget(fin, "end_month")) +
               ". Projections from our own assumptions, not a forecast.";
    }
    S.push_back({"financials", "Financials", foot, blocks});

    blocks = {Block{"lead", sec["use_of_funds"].pitch, "", {}}};
    const Val funds = vor(pget(n, "use_of_funds"), Val::dict()), shares = vor(pget(n, "use_of_funds_pct"), Val::dict());
    const bool said = truthy(raise_amt) && sec["use_of_funds"].pitch.find(short_money(raise_amt)) != std::string::npos;
    std::vector<std::string> mparts;
    const std::string first = (said || !truthy(raise_amt)) ? "" : short_money(raise_amt) + " " + stage;
    if (!first.empty()) mparts.push_back(first);
    { const Val ins = vor(pget(n, "instrument"), sv("")); const std::string s = join_str(ins); if (!s.empty()) mparts.push_back(s); }
    const std::string m = join(mparts, " · ");
    if (!m.empty()) blocks.push_back({"meta", cap(m), "", {}});
    if (truthy(funds)) {
        if (funds.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(funds) + "' object has no attribute 'items'");
        if (shares.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(shares) + "' use_of_funds_pct is not a mapping");
        Block fb{"funds", "", "", {}};
        for (const auto& [k, v] : funds.d->items) {
            const Decimal dv = D(v);
            fb.items.push_back({k, short_money(v) + " · " + (shares.d->get(k) ? pct(get(shares, k)) : std::string()), "", "", dv, false});
        }
        blocks.push_back(fb);
    }
    S.push_back({"ask", "The ask", "", blocks});
    for (std::size_t i = 0; i < S.size(); ++i) S[i].n = static_cast<long>(i + 1);
    return S;
}

inline std::vector<std::string> block_texts(const Block& b) {
    const std::string& t = b.type;
    std::vector<std::string> o;
    if (t == "lead" || t == "tagline" || t == "meta") { if (!b.text.empty()) o.push_back(b.text); return o; }
    if (t == "points" || t == "steps") { for (const auto& it : b.items) o.push_back(it.a); return o; }
    if (t == "stats" || t == "tiles" || t == "side") { for (const auto& it : b.items) { o.push_back(it.a); o.push_back(it.b); } return o; }
    if (t == "market") { for (const auto& it : b.items) { o.push_back(it.a); o.push_back(it.b); o.push_back(it.c); } return o; }
    if (t == "cards") { for (const auto& it : b.items) for (const std::string* x : {&it.a, &it.b, &it.c, &it.d}) if (!x->empty()) o.push_back(*x); return o; }
    if (t == "team") { for (const auto& it : b.items) for (const std::string* x : {&it.a, &it.b, &it.c}) if (!x->empty()) o.push_back(*x); return o; }
    if (t == "bars" || t == "funds") { if (!b.caption.empty()) o.push_back(b.caption); for (const auto& it : b.items) { o.push_back(it.a); o.push_back(it.b); } return o; }
    return o;
}
inline std::vector<std::string> body_texts(const Slide& s) {
    std::vector<std::string> o;
    for (const auto& b : s.blocks) for (auto& x : block_texts(b)) if (!x.empty()) o.push_back(x);
    return o;
}
inline bool has_alnum(const std::string& w) { for (char ch : w) if (std::isalnum(static_cast<unsigned char>(ch))) return true; return false; }
inline long long words(const std::vector<std::string>& texts) {
    long long n = 0;
    for (const auto& t : texts) for (const auto& w : split_ws(t)) if (has_alnum(w)) ++n;
    return n;
}
inline long long sentences(const std::string& text) {
    long long n = 0;
    for (const auto& s : re::split(R"((?<=[.!?])\s+)", strip(text))) if (has_alnum(s)) ++n;
    return n;
}

// ---------------------------------------------------------------- lint
using Files = std::vector<std::pair<std::string, std::optional<Val>>>;   // model, market, competitors, config: None when missing
inline std::optional<Val> file_of(const Files& f, const std::string& k) { for (const auto& [n, v] : f) if (n == k) return v; return std::nullopt; }
inline std::vector<std::string> lint(const Val& plan, const std::vector<Slide>& slides, const Files& files) {
    std::vector<std::string> issues;
    const Val n = vor(pget(plan, "numbers"), Val::dict());
    std::vector<Decimal> known;
    walk_known(n, known); walk_known(vor(pget(plan, "startup"), Val::dict()), known);
    for (const auto& [nm, d] : files) if (d && !d->is_none()) walk_known(*d, known);
    std::vector<std::string> keys; for (const auto& s : slides) keys.push_back(s.key);
    if (keys != ORDER) issues.push_back("The slides are not in the standard order.");
    for (const auto& s : slides) {
        const std::string where = "Slide " + std::to_string(s.n) + " (" + s.title + ")";
        const auto texts = body_texts(s);
        if (texts.empty()) { issues.push_back(where + " is empty: write its section in seed/plan.json, then plan.py build."); continue; }
        const long long w = words(texts);
        if (w > MAX_WORDS) issues.push_back(where + " has " + std::to_string(w) + " words; keep it to " + std::to_string(MAX_WORDS) + " or fewer.");
        for (const auto& b : s.blocks) {
            if (b.type == "lead" && b.text.empty()) issues.push_back(where + " has no line: write the pitch for it in seed/plan.json.");
            if (b.type == "lead" && sentences(b.text) > MAX_SENTENCES)
                issues.push_back(where + " says " + std::to_string(sentences(b.text)) + " things in its lead: one idea a slide, " + std::to_string(MAX_SENTENCES) + " sentences at most.");
            if ((b.type == "points" || b.type == "steps") && static_cast<long long>(b.items.size()) > MAX_POINTS)
                issues.push_back(where + " has " + std::to_string(b.items.size()) + " points: one idea a slide, " + std::to_string(MAX_POINTS) + " points at most.");
        }
        std::vector<std::string> bad;
        std::vector<std::string> all = texts; if (!s.foot.empty()) all.push_back(s.foot);
        for (const auto& t : all)
            for (const auto& tok : tokens(t))
                if (worth_checking(tok) && !matches(tok, known) && std::find(bad.begin(), bad.end(), tok.raw) == bad.end()) bad.push_back(tok.raw);
        if (!bad.empty()) issues.push_back(where + " shows " + join(bad, ", ") + ", in none of plan.json, model.json, market.json, competitors.json or config.json.");
    }
    auto types_of = [&](const std::string& key) { std::vector<std::string> t; for (const auto& s : slides) if (s.key == key) for (const auto& b : s.blocks) t.push_back(b.type); return t; };
    const std::vector<std::array<std::string, 3>> need = {{"market", "market", "run /seed-market"}, {"competition", "cards", "run /seed-scout"},
        {"business_model", "tiles", "run /seed-model"}, {"financials", "bars", "run /seed-model"}, {"ask", "funds", "run /seed-model"},
        {"team", "team", "add the team to seed/plan.json"}};
    for (const auto& [key, nd, fix] : need) {
        const auto t = types_of(key);
        if (std::find(t.begin(), t.end(), nd) == t.end())
            issues.push_back("The " + replace_all(key, "_", " ") + " slide has no " + (nd != "team" ? "data" : "people") + ": " + fix + ", then plan.py build.");
    }
    const Val funds = vor(pget(n, "use_of_funds"), Val::dict());
    if (truthy(funds) && truthy(pget(n, "raise"))) {
        Decimal total(0);
        for (const auto& [k, v] : funds.d->items) total = total + D(v);
        if (total != D(req(n, "raise"))) issues.push_back("The ask: the use of funds adds up to " + money(total) + ", not the " + money(req(n, "raise")) + " raise.");
    } else if (!truthy(pget(n, "raise"))) {
        issues.push_back("The ask has no raise: set startup.raise in seed/config.json, then plan.py build.");
    }
    const auto model = file_of(files, "model.json");
    const Val fin = vor(pget(n, "financials"), Val::dict());
    if (model && truthy(*model) && truthy(fin)) {
        const Val s = vor(pget(*model, "summary"), Val::dict());
        for (const char* k : {"revenue_y1", "revenue_y2", "revenue_y3", "breakeven_month", "peak_burn"}) {
            if (!is_none(pget(s, k)) && !is_none(pget(fin, k)) && D(req(s, k)) != D(req(fin, k))) {
                issues.push_back("plan.json is older than model.json: run plan.py build again, then build the deck.");
                break;
            }
        }
    }
    return issues;
}

// ---------------------------------------------------------------- HTML
inline std::string esc(const std::string& s) {   // html.escape(s, quote=True)
    std::string o;
    for (char ch : s) {
        switch (ch) {
            case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break; case '\'': o += "&#x27;"; break; default: o += ch;
        }
    }
    return o;
}
inline const std::string CHECK = "<svg width=\"26\" height=\"26\" viewBox=\"0 0 26 26\" aria-hidden=\"true\"><circle cx=\"13\" cy=\"13\" r=\"13\" "
                                 "fill=\"var(--brand)\"/><path d=\"M7.5 13.5l3.6 3.6 7.4-7.6\" fill=\"none\" stroke=\"#fff\" stroke-width=\"2.6\" "
                                 "stroke-linecap=\"round\" stroke-linejoin=\"round\"/></svg>";
inline std::string initials(const std::string& name) {
    std::string o; int k = 0;
    for (const auto& p : re::split(R"(\s+)", strip(name))) { if (p.empty()) continue; if (k++ == 2) break; o += cpslice(p, 0, 1); }
    o = upper(o);
    return o.empty() ? "?" : o;
}
inline std::string I(long long v) { return std::to_string(v); }
inline std::string market_svg(const std::vector<Item>& items) {
    const int radii[] = {180, 122, 64};
    const char* fills[] = {"var(--soft)", "var(--soft2)", "var(--brand)"};
    std::string o = "<svg width=\"380\" height=\"380\" viewBox=\"0 0 380 380\" role=\"img\" aria-label=\"TAM, SAM and SOM, not to scale\">";
    for (std::size_t i = 0; i < 3 && i < items.size(); ++i) {
        const int r = radii[i], cy = 370 - r;
        o += "<circle cx=\"190\" cy=\"" + I(cy) + "\" r=\"" + I(r) + "\" fill=\"" + fills[i] + "\"/>";
        const int ty = cy - r + (r > 100 ? 34 : 0);
        if (r > 100)
            o += "<text x=\"190\" y=\"" + I(ty) + "\" text-anchor=\"middle\" font-size=\"19\" font-weight=\"800\" fill=\"var(--ink)\" letter-spacing=\"1.5\">" + esc(items[i].a) + "</text>";
        else
            o += "<text x=\"190\" y=\"" + I(cy) + "\" text-anchor=\"middle\" font-size=\"19\" font-weight=\"800\" fill=\"#fff\" letter-spacing=\"1.5\" dominant-baseline=\"middle\">" + esc(items[i].a) + "</text>";
    }
    return o + "</svg>";
}
inline Decimal peak_of(const std::vector<Item>& items) {   // max([values] + [Decimal(1)]): the first of the largest
    std::vector<Decimal> v; for (const auto& it : items) v.push_back(it.num); v.push_back(Decimal(1));
    Decimal cur = v[0];
    for (std::size_t i = 1; i < v.size(); ++i) if (v[i] > cur) cur = v[i];
    return cur;
}
inline std::string bars_svg(const std::vector<Item>& items, const std::string& caption) {
    const long long w = 560, h = 380, top = 54, bottom = 44, bw = 120, gap = 60;
    const Decimal peak = peak_of(items);
    const long long n = static_cast<long long>(items.size());
    const long long span = w - (n * bw + (n - 1) * gap);
    const long long x0 = span >= 0 ? span / 2 : -((-span + 1) / 2);   // floor division
    std::string o = "<svg width=\"" + I(w) + "\" height=\"" + I(h) + "\" viewBox=\"0 0 " + I(w) + " " + I(h) + "\" role=\"img\" aria-label=\"" + esc(caption) + " by year\">";
    const long long base = h - bottom;
    o += "<line x1=\"0\" y1=\"" + I(base) + "\" x2=\"" + I(w) + "\" y2=\"" + I(base) + "\" stroke=\"var(--line)\" stroke-width=\"2\"/>";
    for (long long i = 0; i < n; ++i) {
        const auto& it = items[static_cast<std::size_t>(i)];
        const Decimal v = Decimal(0) > it.num ? Decimal(0) : it.num;
        const long long bh = !peak.is_zero() ? std::stoll(((v / peak) * Decimal(base - top)).to_int().str()) : 0;
        const long long x = x0 + i * (bw + gap);
        const char* fill = i == n - 1 ? "var(--brand)" : "var(--soft2)";
        o += "<rect x=\"" + I(x) + "\" y=\"" + I(base - bh) + "\" width=\"" + I(bw) + "\" height=\"" + I(std::max(bh, 2LL)) + "\" rx=\"12\" fill=\"" + fill + "\"/>";
        o += "<text x=\"" + I(x + bw / 2) + "\" y=\"" + I(base - bh - 14) + "\" text-anchor=\"middle\" font-size=\"26\" font-weight=\"750\" fill=\"var(--ink)\">" + esc(it.b) + "</text>";
        o += "<text x=\"" + I(x + bw / 2) + "\" y=\"" + I(base + 30) + "\" text-anchor=\"middle\" font-size=\"20\" fill=\"var(--muted)\">" + esc(it.a) + "</text>";
    }
    o += "<text x=\"0\" y=\"22\" font-size=\"18\" font-weight=\"800\" fill=\"var(--muted)\" letter-spacing=\"1.5\">" + esc(upper(caption)) + "</text>";
    return o + "</svg>";
}
inline std::string render_block(const Block& b) {
    const std::string& t = b.type;
    std::string o;
    if (t == "lead") return "<p class=\"lead\">" + esc(b.text.empty() ? "(not written yet)" : b.text) + "</p>";
    if (t == "tagline") return "<p class=\"tagline\">" + esc(b.text) + "</p>";
    if (t == "meta") return b.text.empty() ? "" : "<p class=\"meta\">" + esc(b.text) + "</p>";
    if (t == "points") { for (const auto& it : b.items) o += "<li>" + CHECK + "<span>" + esc(it.a) + "</span></li>"; return "<ul class=\"points\">" + o + "</ul>"; }
    if (t == "steps") { for (const auto& it : b.items) o += "<li>" + esc(it.a) + "</li>"; return "<ol class=\"steps\">" + o + "</ol>"; }
    if (t == "stats") { for (const auto& it : b.items) o += "<div class=\"stat\"><b>" + esc(it.a) + "</b><span>" + esc(it.b) + "</span></div>"; return "<div class=\"stats\">" + o + "</div>"; }
    if (t == "market") {
        for (const auto& it : b.items) o += "<li><span class=\"tag\">" + esc(it.a) + "</span><b>" + esc(it.b) + "</b><span>" + esc(it.c) + "</span></li>";
        return "<div class=\"market\">" + market_svg(b.items) + "<ul class=\"legend\">" + o + "</ul></div>";
    }
    if (t == "cards") {
        for (const auto& it : b.items)
            o += std::string("<div class=\"card") + (it.us ? " us" : "") + "\"><h3>" + esc(it.a) + "</h3>" + (it.b.empty() ? "" : "<div class=\"sub\">" + esc(it.b) + "</div>") +
                 (it.c.empty() ? "" : "<div class=\"lab\">" + esc(it.c) + "</div>") + "<div class=\"val\">" + esc(it.d) + "</div></div>";
        return "<div class=\"cards\" style=\"grid-template-columns:repeat(" + I(static_cast<long long>(b.items.size())) + ",1fr)\">" + o + "</div>";
    }
    if (t == "tiles") {
        for (const auto& it : b.items) {
            const std::size_t sp = it.a.find(' ');
            const std::string head = sp == std::string::npos ? it.a : it.a.substr(0, sp), tail = sp == std::string::npos ? "" : it.a.substr(sp + 1);
            o += "<div class=\"tile\"><b>" + esc(head) + (tail.empty() ? "" : " <small>" + esc(tail) + "</small>") + "</b><span>" + esc(it.b) + "</span></div>";
        }
        return std::string("<div class=\"tiles") + (b.items.size() >= 5 ? " five" : "") + "\" style=\"grid-template-columns:repeat(" + I(static_cast<long long>(b.items.size())) + ",1fr)\">" + o + "</div>";
    }
    if (t == "team") {
        const long long cols = std::min<long long>(std::max<long long>(static_cast<long long>(b.items.size()), 1), 2);
        for (const auto& it : b.items)
            o += "<div class=\"person\"><div class=\"face\">" + esc(initials(it.a)) + "</div><div><h3>" + esc(it.a) + "</h3><div class=\"role\">" + esc(it.b) + "</div>" +
                 (it.c.empty() ? "" : "<p>" + esc(it.c) + "</p>") + "</div></div>";
        return "<div class=\"team\" style=\"grid-template-columns:repeat(" + I(cols) + ",1fr)\">" + o + "</div>";
    }
    if (t == "bars") return bars_svg(b.items, b.caption);
    if (t == "side") { for (const auto& it : b.items) o += "<div><b>" + esc(it.a) + "</b><span>" + esc(it.b) + "</span></div>"; return "<div class=\"side\">" + o + "</div>"; }
    if (t == "funds") {
        const Decimal peak = peak_of(b.items);
        for (const auto& it : b.items)
            o += "<div class=\"fund\"><span>" + esc(it.a) + "</span><div class=\"track\"><div class=\"fill\" style=\"width:" + trim(rnd(it.num / peak * Decimal(100), 1)) +
                 "%\"></div></div><span class=\"amt\">" + esc(it.b) + "</span></div>";
        return "<div class=\"funds\">" + o + "</div>";
    }
    return "";
}
inline std::string render_slide(const Slide& s, const std::string& name) {
    std::string head, inner;
    if (s.key == "title") {
        inner = "<h1><span class=\"bigmark\">" + esc(cpslice(initials(name), 0, 1)) + "</span>" + esc(s.title) + "</h1>";
        for (const auto& b : s.blocks) inner += render_block(b);
    } else {
        head = "<div class=\"brandbar\"><span class=\"mark\">" + esc(cpslice(initials(name), 0, 1)) + "</span>" + esc(name) + "</div><h2>" + esc(s.title) + "</h2>";
        bool bars = false; for (const auto& b : s.blocks) if (b.type == "bars") bars = true;
        if (s.key == "financials" && bars) {
            std::string lead, rest;
            for (const auto& b : s.blocks) (b.type == "lead" ? lead : rest) += render_block(b);
            inner = lead + "<div class=\"fin\">" + rest + "</div>";
        } else for (const auto& b : s.blocks) inner += render_block(b);
    }
    const std::string foot = s.foot.empty() ? "" : "<div class=\"foot\">" + esc(s.foot) + "</div>";
    return "<section class=\"slide s-" + replace_all(s.key, "_", "-") + "\" id=\"s" + I(s.n) + "\" aria-label=\"Slide " + I(s.n) + ": " + esc(s.title) + "\">" + head +
           "<div class=\"body\">" + inner + "</div>" + foot + "<div class=\"num\">" + I(s.n) + " / " + I(static_cast<long long>(ORDER.size())) + "</div></section>";
}
inline std::string css_format(const std::map<std::string, std::string>& vals) {   // CSS % {...}
    const std::string c = CSS;
    std::string o;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (c[i] != '%') { o += c[i]; continue; }
        if (i + 1 < c.size() && c[i + 1] == '%') { o += '%'; ++i; continue; }
        const std::size_t close = c.find(")s", i);
        const std::string key = c.substr(i + 2, close - i - 2);
        o += vals.at(key);
        i = close + 1;
    }
    return o;
}
inline std::string render(const Val& plan, const std::vector<Slide>& slides, const Brand& brand) {
    const std::string name = pystr(vor(pget(vor(pget(plan, "startup"), Val::dict()), "name"), sv("Your startup")));
    const std::string ink = brand.ink, white = "#FFFFFF";
    const std::string css = css_format({{"brand", brand.brand}, {"ink", ink}, {"accent", brand.accent}, {"soft", mix(brand.brand, white, 0.08)},
                                        {"soft2", mix(brand.brand, white, 0.22)}, {"line", mix(ink, white, 0.12)}, {"muted", mix(ink, white, 0.62)},
                                        {"on_brand", white}, {"stage", mix(ink, "#000000", 0.85)}});
    std::vector<std::string> body; for (const auto& s : slides) body.push_back(render_slide(s, name));
    return "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>" + esc(name) +
           ": pitch deck</title>\n<style>" + css + "</style>\n</head>\n<body>\n<main class=\"deck\">\n" + join(body, "\n") +
           "\n</main>\n<div class=\"hud\" id=\"hud\" aria-live=\"polite\"></div>\n<script>" + JS + "</script>\n</body>\n</html>\n";
}

// ---------------------------------------------------------------- commands
inline std::optional<Val> read_json(const std::string& path, const std::string& what, bool required = true) {
    if (!exists(path)) {
        if (required) throw DeckError("no " + what + " at " + path);
        return std::nullopt;
    }
    try { return loads(read_text(path)); }
    catch (const OSError&) { throw; }
    catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw DeckError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline std::pair<Val, Files> load(const Val& a) {
    const std::string plan_path = !S(a, "plan").empty() ? S(a, "plan") : join_path(S(a, "dir"), "plan.json");
    const Val plan = *read_json(plan_path, "plan (run /seed-plan first: plan.py init, then plan.py build)");
    if (!truthy(pget(plan, "built")) || !truthy(pget(plan, "numbers"))) throw DeckError(plan_path + " has no numbers yet: run plan.py build first");
    Files files;
    for (const char* f : {"model.json", "market.json", "competitors.json", "config.json"}) files.push_back({f, read_json(join_path(S(a, "dir"), f), f, false)});
    return {plan, files};
}
inline int cmd_build(const Val& a) {
    const auto [plan, files] = load(a);
    const Brand brand = brand_of(plan, {{"brand", S(a, "brand")}, {"ink", S(a, "ink")}, {"accent", S(a, "accent")}});
    const auto slides = build_slides(plan);
    const std::string text = render(plan, slides, brand);
    const std::string out = !S(a, "out").empty() ? S(a, "out") : join_path(S(a, "dir"), "deck.html");
    const std::string d = pyrt::dirname(out);
    if (!d.empty()) makedirs(d);
    write_text(out, text);
    const auto issues = lint(plan, slides, files);
    print("Wrote " + out + ": " + std::to_string(slides.size()) + " slides, " + std::to_string((text.size() + 1023) / 1024) + " KB, one self-contained file.");
    print("Open it in a browser: the arrow keys move, P prints. Print > Save as PDF gives one slide a page.");
    if (!issues.empty()) {
        print("The lint found " + std::to_string(issues.size()) + " thing" + (issues.size() == 1 ? "" : "s") + " to fix:");
        for (const auto& x : issues) print("  - " + x);
    } else {
        print("Lint: clean (35 words or fewer a slide, one idea a slide, the numbers trace to your files).");
    }
    print("Next: /seed-coach drills the questions this deck invites; /seed-closer attaches it to the outreach.");
    return 0;
}
inline Val str_list(const std::vector<std::string>& v) { Val l = Val::list(); for (const auto& s : v) l.l->push_back(sv(s)); return l; }
inline int cmd_outline(const Val& a) {
    const auto [plan, files] = load(a);
    const auto slides = build_slides(plan);
    if (B(a, "json")) {
        Val l = Val::list();
        for (const auto& s : slides) {
            const auto t = body_texts(s);
            l.l->push_back(obj({{"n", iv(s.n)}, {"key", sv(s.key)}, {"title", sv(s.title)}, {"body", str_list(t)}, {"words", iv(words(t))}, {"foot", sv(s.foot)}}));
        }
        DumpOpts o; o.indent = 2;
        print(dumps(l, o));
        return 0;
    }
    const std::string name = pystr(vor(pget(vor(pget(plan, "startup"), Val::dict()), "name"), sv("Your startup")));
    print(name + ": pitch deck outline (" + std::to_string(slides.size()) + " slides)");
    for (const auto& s : slides) {
        const auto t = body_texts(s);
        const std::string body = join(t, " | ");
        print(rjust(std::to_string(s.n), 2) + ". " + ljust(s.title, 16) + " " + rjust(std::to_string(words(t)), 2) + " words  " + (body.empty() ? "(empty)" : body));
    }
    return 0;
}
inline int cmd_lint(const Val& a) {
    const auto [plan, files] = load(a);
    const auto slides = build_slides(plan);
    const auto issues = lint(plan, slides, files);
    if (B(a, "json")) {
        Val w = Val::dict(); for (const auto& s : slides) w.d->set(s.key, iv(words(body_texts(s))));
        DumpOpts o; o.indent = 2;
        print(dumps(obj({{"ok", Val::boolean(issues.empty())}, {"issues", str_list(issues)}, {"words", w}}), o));
    } else if (!issues.empty()) {
        for (const auto& x : issues) print("  - " + x);
    } else {
        long long most = 0; for (const auto& s : slides) most = std::max(most, words(body_texts(s)));
        print("Lint: clean. " + std::to_string(slides.size()) + " slides, the most words on one is " + std::to_string(most) + ".");
    }
    return issues.empty() ? 0 : 1;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("deck.py", "Pitch Deck Designer (seed-deck): the plan, as a 10-slide deck.");
    p.opt({"--dir"}).def("seed").help("the seed/ folder (default ./seed)");
    p.opt({"--plan"}).metavar("PATH").help("default <dir>/plan.json");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("build"); s.help_line = "write seed/deck.html";
      s.opt({"--out"}).metavar("PATH").help("default <dir>/deck.html");
      s.opt({"--brand"}).help("#RRGGBB (default plan.json brand.brand)");
      s.opt({"--ink"}).help("#RRGGBB (default plan.json brand.ink)");
      s.opt({"--accent"}).help("#RRGGBB (default plan.json brand.accent)");
      s.opt({"--dir"}).no_default(); s.opt({"--plan"}).metavar("PATH").no_default(); }
    { Parser& s = p.sub("outline"); s.help_line = "print what each slide says"; s.opt({"--json"}).store_true();
      s.opt({"--dir"}).no_default(); s.opt({"--plan"}).metavar("PATH").no_default(); }
    { Parser& s = p.sub("lint"); s.help_line = "check the deck against the rules"; s.opt({"--json"}).store_true();
      s.opt({"--dir"}).no_default(); s.opt({"--plan"}).metavar("PATH").no_default(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "build") return cmd_build(a);
        if (cmd == "outline") return cmd_outline(a);
        return cmd_lint(a);
    } catch (const DeckError& e) { eprint(std::string("deck.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-deck/deck", main);
}  // namespace packtools::seed_deck
