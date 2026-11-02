// seed-investors/investors: the investors who back ideas like yours, from public sources, ranked by fit with the reasons
// and the links. formd reads SEC EDGAR Form D filings (full-text search, then each filing's primary_doc.xml); import and
// add write seed/investors.json; rank scores fit 0-100 (stage 25, sectors 25, check 20, recency 15, geography 10, warm
// intro 5); show prints the list or one investor. A C++ port of seed-skill/seed-investors/investors.py (MIT, Jake
// Schincariol); same files, output and exit codes: 0 done, 1 refused or nothing found, 2 bad input or a network error.
// One addition for the test suite: ASTRO_SEED_INVESTORS_PAGE (1-100) sets the search page size, so two saved pages of
// three hits exercise the paging the way upstream's tests do by setting PAGE = 3. Unset, the page is 100 like SEC's.
#include <chrono>
#include <cmath>
#include <optional>
#include <set>
#include <thread>
namespace packtools::seed_investors {
using namespace pyrt;
using namespace packtools::seed;
using seed_model::capitalize;
using seed_model::py_eq;
using seed_plan::pget;
using seed_plan::vor;
using seed_plan::join_strs;
using seed_plan::str_of;
using seed_plan::fmt_d;
using seed_scout::int_str;

struct InvError : std::runtime_error { int code; explicit InvError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {} };

inline const char* const EFTS = "https://efts.sec.gov/LATEST/search-index?";
inline constexpr double PAUSE = 0.2;   // SEC allows 10 requests a second; stay far under it
inline constexpr long TIMEOUT = 30;
inline const std::vector<std::string> STAGES = {"pre-seed", "seed", "series a", "series b", "growth"};
inline constexpr long long W_STAGE = 25, W_SECTORS = 25, W_CHECK = 20, W_RECENCY = 15, W_GEO = 10, W_WARM = 5, RECENCY_DAYS = 730;
inline const std::vector<std::string> FREEMAIL = {"gmail.com", "googlemail.com", "yahoo.com", "hotmail.com", "outlook.com", "live.com",
                                                  "icloud.com", "me.com", "aol.com", "proton.me", "protonmail.com"};
inline const std::vector<std::string> PRIVATE_COLUMNS = {"phone", "mobile", "cell", "telephone", "tel", "whatsapp", "address", "home_address",
                                                         "street", "home", "zip", "postcode"};
inline const std::vector<std::string> FIELDS = {"name", "firm", "role", "email", "stage", "check", "check_min", "check_max", "sectors", "geo",
                                                "last_deal", "last_deal_date", "sources", "source", "warm_intro", "personal_line", "notes"};
inline const char* const PHONE = R"((?<![\d$])(?:\+\d{1,3}[\s.-]?)?(?:\(\d{2,4}\)[\s.-]?|\d{2,4}[\s.-])\d{3,4}[\s.-]\d{3,4}(?!\d))";
inline const char* const EMAIL = R"(^[^@\s]+@[^@\s]+\.[^@\s]+$)";
inline const char* const URL = R"(^https?://[^\s/]+\.[^\s]+$)";
inline const std::vector<std::pair<std::string, std::string>> ALIASES = {
    {"email_address", "email"}, {"e-mail", "email"}, {"e_mail", "email"}, {"mail", "email"}, {"url", "sources"},
    {"urls", "sources"}, {"link", "sources"}, {"links", "sources"}, {"source_url", "sources"}, {"check_size", "check"},
    {"checks", "check"}, {"sector", "sectors"}, {"focus", "sectors"}, {"stages", "stage"}, {"geography", "geo"},
    {"location", "geo"}, {"region", "geo"}, {"warm", "warm_intro"}, {"title", "role"}, {"company", "firm"},
    {"fund", "firm"}, {"last_deal_on", "last_deal_date"}, {"line", "personal_line"}};
inline const std::vector<std::string> GLOBAL = {"global", "any", "anywhere", "worldwide", "remote"};
inline const std::set<std::string> GEO_STOP = {"new", "north", "south", "east", "west", "san", "los", "las", "the", "of", "city", "and", "bay", "area"};
inline const std::vector<std::pair<std::string, std::string>> GEO_ALIASES = {{"usa", "us"}, {"united states", "us"}, {"united states of america", "us"},
    {"u.s.", "us"}, {"u.s.a.", "us"}, {"uk", "gb"}, {"united kingdom", "gb"}};
inline const std::vector<std::pair<std::string, std::string>> SECURITIES = {{"isEquityType", "equity"}, {"isDebtType", "debt"},
    {"isOptionToAcquireType", "options"}, {"isSecurityToBeAcquiredType", "securities on exercise"},
    {"isPooledInvestmentFundInterestsType", "fund interests"}, {"isTenantInCommonType", "tenant in common"},
    {"isMineralPropertyType", "mineral property"}};

inline long long page_size() {   // efts returns 100 hits a page
    const char* e = std::getenv("ASTRO_SEED_INVESTORS_PAGE");
    if (!e || !*e) return 100;
    const std::string s(e);
    if (s.size() > 3 || s.find_first_not_of("0123456789") != std::string::npos) return 100;
    const long long n = std::stoll(s);
    return n >= 1 && n <= 100 ? n : 100;
}
inline bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
inline const std::string* lookup(const std::vector<std::pair<std::string, std::string>>& m, const std::string& k) {
    for (const auto& [a, b] : m) if (a == k) return &b;
    return nullptr;
}

// ---------------------------------------------------------------- dynamic-typing helpers
inline std::string sor(const Val& v) { return truthy(v) ? pystr(v) : std::string(); }   // str(v or "")
inline Val at(const Val& o, const std::string& k) {   // o[k]
    if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    const Val* v = o.d->get(k);
    if (!v) throw PyErr("KeyError", str_repr(k));
    return *v;
}
inline Val dict_pop(Val& o, const std::string& k) {
    auto& it = o.d->items;
    for (auto p = it.begin(); p != it.end(); ++p) if (p->first == k) { Val v = p->second; it.erase(p); return v; }
    throw PyErr("KeyError", str_repr(k));
}
inline std::vector<std::string> keys_of(const Val& o) { std::vector<std::string> k; for (const auto& kv : o.d->items) k.push_back(kv.first); return k; }
inline void need_real(const Val& a, const Val& b, const char* op) {
    if (!a.is_real() || !b.is_real())
        throw PyErr("TypeError", std::string("'") + op + "' not supported between instances of '" + type_name(a) + "' and '" + type_name(b) + "'");
}
inline std::optional<int> ncmp(const Val& a, const Val& b, const char* op) {   // Python's ordering of two numbers; nullopt = NaN
    need_real(a, b, op);
    if ((a.t == Val::FLOAT && std::isnan(a.f)) || (b.t == Val::FLOAT && std::isnan(b.f))) return std::nullopt;
    if (a.t == Val::FLOAT && std::isinf(a.f)) return b.t == Val::FLOAT && b.f == a.f ? 0 : a.f > 0 ? 1 : -1;
    if (b.t == Val::FLOAT && std::isinf(b.f)) return b.f > 0 ? -1 : 1;
    return num_cmp(a, b);
}
inline bool lt(const Val& a, const Val& b) { const auto c = ncmp(a, b, "<"); return c && *c < 0; }
inline bool le(const Val& a, const Val& b) { const auto c = ncmp(a, b, "<="); return c && *c <= 0; }
inline bool gt(const Val& a, const Val& b) { const auto c = ncmp(a, b, ">"); return c && *c > 0; }
inline bool ge(const Val& a, const Val& b) { const auto c = ncmp(a, b, ">="); return c && *c >= 0; }
inline Val times2(const Val& v) {   // v * 2 for a number
    if (v.t == Val::INT) return int_val(big_of(v) * BigInt(2));
    if (v.t == Val::BOOL) return iv(v.b ? 2 : 0);
    if (v.t == Val::FLOAT) return Val::flt(v.f * 2);
    if (v.t == Val::STR) return sv(v.s + v.s);
    if (v.t == Val::LIST) { List l = *v.l; l.insert(l.end(), v.l->begin(), v.l->end()); return Val::list(l); }
    throw PyErr("TypeError", std::string("unsupported operand type(s) for *: '") + type_name(v) + "' and 'int'");
}
inline bool py_in(const Val& x, const Val& c) {   // x in c
    if (c.t == Val::LIST) { for (const auto& e : *c.l) if (py_eq(e, x)) return true; return false; }
    if (c.t == Val::STR) {
        if (x.t != Val::STR) throw PyErr("TypeError", std::string("'in <string>' requires string as left operand, not ") + type_name(x));
        return c.s.find(x.s) != std::string::npos;
    }
    if (c.t == Val::DICT) {
        if (x.t == Val::LIST || x.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(x) + "'");
        return x.t == Val::STR && c.d->has(x.s);
    }
    throw PyErr("TypeError", std::string("argument of type '") + type_name(c) + "' is not iterable");
}
inline std::string head_of(const Val& v, long n) {   // "%s" % v[:n] for a str (or a list)
    if (v.t == Val::STR) return cpslice(v.s, 0, n);
    if (v.t == Val::LIST) { List l(v.l->begin(), v.l->begin() + std::min<long>(n, static_cast<long>(v.l->size()))); return pystr(Val::list(l)); }
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not subscriptable");
}
inline std::size_t slice_n(std::size_t size, long long n) {   // len(rows[:n])
    const long long s = static_cast<long long>(size);
    if (n < 0) return static_cast<std::size_t>(std::max(0LL, s + n));
    return static_cast<std::size_t>(std::min(s, n));
}
inline Val round_int(double x) {   // int(round(x)): half to even
    if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    return int_val(Decimal::from_double(std::nearbyint(x)).to_int());
}
inline BigInt py_int(const Val& v) {   // int(v)
    if (v.t == Val::STR) {
        const std::string s = strip(v.s);
        if (!re::match(R"(^[+-]?\d+(?:_\d+)*$)", s) || s.back() == '\n')
            throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(v.s));
        return BigInt::parse(ascii_digits(replace_all(s, "_", "")));
    }
    if (v.t == Val::FLOAT) {
        if (std::isnan(v.f)) throw PyErr("ValueError", "cannot convert float NaN to integer");
        if (std::isinf(v.f)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    }
    return int_of(v);
}
inline Val first_item(const Val& v) {   // v[0]
    if (v.t == Val::LIST) { if (v.l->empty()) throw PyErr("IndexError", "list index out of range"); return (*v.l)[0]; }
    if (v.t == Val::STR) return sv(cpslice(v.s, 0, 1));
    if (v.t == Val::DICT) throw PyErr("KeyError", "0");
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not subscriptable");
}
inline std::string fmt_010d(const BigInt& n) {   // "%010d" % n
    const std::string d = int_str(n.abs()); const bool neg = n.negative() && d != "0";
    const std::size_t w = neg ? 9 : 10;
    return (neg ? "-" : "") + (d.size() < w ? std::string(w - d.size(), '0') : std::string()) + d;
}

// ---------------------------------------------------------------- parsing helpers
inline std::optional<long long> strptime_day(const std::string& text, bool with_day) {   // strptime(text, "%Y-%m-%d" / "%Y-%m")
    const char* pat = with_day ? R"(^(\d\d\d\d)-(1[0-2]|0[1-9]|[1-9])-(3[01]|[12]\d|0[1-9]|[1-9]| [1-9])$)" : R"(^(\d\d\d\d)-(1[0-2]|0[1-9]|[1-9])$)";
    const re::Match m = re::match(pat, text, re::I);
    if (!m || text.empty() || text.back() == '\n') return std::nullopt;
    const long long y = to_ll(m.group(1));
    const int mo = static_cast<int>(to_ll(m.group(2))), d = with_day ? static_cast<int>(to_ll(strip(m.group(3)))) : 1;
    if (!valid_date(y, mo, d)) return std::nullopt;
    return make_day(y, mo, d);
}
inline long long today_of(const Val& tv) {
    if (!truthy(tv)) return today_days();
    const std::string text = pystr(tv);
    if (const auto d = strptime_day(text, true)) return *d;
    throw InvError("--today wants YYYY-MM-DD, not " + repr(tv));
}
inline long long parse_date(const Val& text, const std::string& what) {   // YYYY-MM-DD, or YYYY-MM (the 1st of the month)
    const std::string t = strip(pystr(text));
    if (const auto d = strptime_day(t, true)) return *d;
    if (const auto d = strptime_day(t, false)) return *d;
    throw InvError(what + " wants YYYY-MM-DD or YYYY-MM, not " + repr(text));
}
inline Val money(const Val& text, const std::string& what = "an amount") {   // $250K, 1M, 1.5m, 1,500,000 -> int dollars
    if (text.t == Val::BOOL) throw InvError(what + " is not an amount: " + repr(text));
    if (text.t == Val::INT) return text;
    if (text.t == Val::FLOAT) return round_int(text.f);
    const std::string t = replace_all(replace_all(replace_all(lower(strip(pystr(text))), ",", ""), "$", ""), " ", "");
    const re::Match m = re::match(R"(^(\d+(?:\.\d+)?)([km]?|mm|bn?)$)", t);
    if (!m) throw InvError(what + " is not an amount like $250K or 1.5M: " + repr(text));
    const std::string u = m.group(2);
    const double mult = u.empty() ? 1 : u == "k" ? 1e3 : (u == "m" || u == "mm") ? 1e6 : 1e9;
    return round_int(std::strtod(ascii_digits(m.group(1)).c_str(), nullptr) * mult);
}
inline std::pair<Val, Val> money_range(const Val& text, const std::string& what = "--check") {   // '250K-1M', '$250K to $1M', '$100K'
    const auto parts = re::split("\\s*(?:-|\xE2\x80\x93|to)\\s*", strip(pystr(text)), 1);
    const Val lo = money(sv(parts[0]), what);
    const Val hi = parts.size() > 1 && !parts[1].empty() ? money(sv(parts[1]), what) : lo;
    if (le(lo, iv(0)) || lt(hi, lo)) throw InvError(what + " wants a range like 250K-1M with the low end first, not " + repr(text));
    return {lo, hi};
}
inline std::string trim_num(std::string s) { s = rstrip_chars(s, "0"); return rstrip_chars(s, "."); }
inline std::string short_money(const Val& n) {
    if (n.is_none()) return "?";
    char buf[64];
    if (ge(n, iv(1000000))) { std::snprintf(buf, sizeof buf, "%.2f", n.num() / 1e6); return "$" + trim_num(buf) + "M"; }
    if (ge(n, iv(1000))) { std::snprintf(buf, sizeof buf, "%.1f", n.num() / 1e3); return "$" + trim_num(buf) + "K"; }
    return "$" + fmt_d(n);
}
inline std::string full_money(const Val& n) { return n.is_none() ? "?" : "$" + packtools::seed::commas(py_int(n)); }
inline std::vector<std::string> split_list(const Val& v) {
    std::vector<std::string> items, o;
    if (v.is_none()) return o;
    if (v.t == Val::LIST) for (const auto& x : *v.l) items.push_back(pystr(x));
    else items = re::split(R"(\s*[;|]\s*|\s*,\s*)", pystr(v));
    for (const auto& x : items) if (!strip(x).empty()) o.push_back(strip(x));
    return o;
}
inline std::vector<std::string> split_urls(const Val& v) {   // URLs separated by spaces, semicolons, pipes or commas (or a list)
    std::vector<std::string> items, o;
    if (v.is_none()) return o;
    if (v.t == Val::LIST) for (const auto& x : *v.l) items.push_back(pystr(x));
    else items = re::split(R"([\s;|,]+)", pystr(v));
    for (const auto& x : items) for (const auto& u : re::split(R"([\s;|]+)", strip(x))) if (!u.empty()) o.push_back(u);
    return o;
}
inline std::string norm_stage(const Val& s) {
    std::string t = re::sub(R"([\s_-]+)", " ", lower(strip(pystr(s))));
    static const std::vector<std::pair<std::string, std::string>> M = {{"preseed", "pre-seed"}, {"pre seed", "pre-seed"}, {"a", "series a"},
        {"series-a", "series a"}, {"b", "series b"}, {"seriesa", "series a"}, {"seriesb", "series b"}, {"late", "growth"}, {"growth equity", "growth"}};
    if (const auto* m = lookup(M, t)) t = *m;
    if (!has(STAGES, t)) throw InvError("stage " + repr(s) + " is not one of: " + join(STAGES, ", "));
    return t;
}
inline std::set<std::string> words(const Val& s) {
    const std::string t = replace_all(lower(pystr(s)), "&", " and ");
    std::set<std::string> o;
    for (std::string w : re::findall("[a-z0-9]+", t)) {
        if (w.size() > 3 && w.back() == 's' && w.compare(w.size() - 2, 2, "ss") != 0) w.pop_back();
        if (w != "and" && w != "the" && w != "of" && w != "for") o.insert(w);
    }
    return o;
}
inline bool subset(const std::set<std::string>& a, const std::set<std::string>& b) { return std::includes(b.begin(), b.end(), a.begin(), a.end()); }
inline List sector_match(const List& ours, const List& theirs) {   // our sectors that match one of theirs
    List hit;
    for (const auto& o : ours) {
        const auto ow = words(o);
        for (const auto& t : theirs) {
            const auto tw = words(t);
            if (!ow.empty() && !tw.empty() && (subset(ow, tw) || subset(tw, ow))) { hit.push_back(o); break; }
        }
    }
    return hit;
}
inline std::string norm_geo(const Val& g) {
    const std::string t = re::sub(R"(\s+)", " ", lower(strip(pystr(g))));
    const auto* a = lookup(GEO_ALIASES, t);
    return a ? *a : t;
}
inline std::set<std::string> geo_words(const std::string& s) {
    std::set<std::string> w;
    for (const auto& x : re::findall(R"([a-z0-9.]+)", s)) if (!GEO_STOP.count(x)) w.insert(x);
    w.insert(s);
    return w;
}
inline std::pair<bool, std::string> geo_match(const List& ours, const List& theirs) {
    std::vector<std::string> th;
    for (const auto& g : theirs) th.push_back(norm_geo(g));
    for (const auto& g : th) if (has(GLOBAL, g)) return {true, "invests globally"};
    for (const auto& o : ours) {
        const std::string on = norm_geo(o);
        const auto ow = geo_words(on);
        for (const auto& t : th) {
            const auto tw = geo_words(t);
            bool meet = on == t;
            for (const auto& x : ow) if (tw.count(x)) meet = true;
            if (meet) return {true, cplen(t) <= 3 ? "invests in " + upper(t) : "invests in " + title(t)};
        }
    }
    return {false, ""};
}
inline std::string slug(const Val& name) {
    const std::string s = strip_chars(re::sub("[^a-z0-9]+", "-", lower(pystr(name))), "-");
    return s.empty() ? "investor" : s;
}
inline bool yes_ish(const Val& v) {   // investors.py truthy()
    if (v.t == Val::BOOL) return v.b;
    const std::string t = lower(strip(sor(v)));
    return t == "1" || t == "y" || t == "yes" || t == "true" || t == "warm" || t == "x";
}

// ---------------------------------------------------------------- the seed folder
inline std::string seed_dir(const Val& a) { const std::string d = S(a, "dir"); return d.empty() ? "seed" : d; }
inline Val read_json(const std::string& path, const std::string& what) {
    std::string text;
    try { text = read_text(path); } catch (const OSError&) { throw InvError("no " + what + " at " + path, 1); }
    catch (const PyErr& e) { if (e.type == "UnicodeDecodeError") throw InvError(path + " is not valid JSON: " + e.what()); throw; }
    try { return loads(text); }
    catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw InvError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline std::string dump_json(const Val& v) { return dumps(v, seed_scout::json_opts()); }
inline Val load_book(const std::string& sd) {
    const std::string path = join_path(sd, "investors.json");
    if (is_file(path)) {
        Val doc = read_json(path, "investors");
        if (doc.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(doc) + "' object has no attribute 'setdefault'");
        if (!doc.d->has("investors")) doc.d->set("investors", Val::list());
        return doc;
    }
    return obj({{"created_by", sv("seed-investors")}, {"generated", Val()}, {"ask", Val()}, {"criteria", Val::dict()}, {"investors", Val::list()}});
}
inline Val load_config(const std::string& sd) {
    const std::string path = join_path(sd, "config.json");
    return is_file(path) ? read_json(path, "config") : Val::dict();
}

// ---------------------------------------------------------------- one investor row
// A dict from a CSV row, a JSON object or the add flags -> (investor, warnings). Raises InvError(code 1).
inline std::pair<Val, std::vector<std::string>> clean_row(const Val& raw0, long long today, const std::string& where = "row") {
    std::vector<std::string> warnings;
    if (raw0.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(raw0) + "' object has no attribute 'items'");
    Val raw = Val::dict();
    for (const auto& [k, v] : raw0.d->items) raw.d->set(replace_all(lower(strip(k)), " ", "_"), v);
    for (const auto& k : keys_of(raw)) {
        const std::string* to = lookup(ALIASES, k);
        if (to && strip(sor(raw.d->at(*to))).empty()) { Val v = dict_pop(raw, k); raw.d->set(*to, v); }
    }
    for (const auto& k : keys_of(raw)) {
        if (has(PRIVATE_COLUMNS, k) || packtools::seed::starts(k, "phone") || (k.size() >= 6 && k.compare(k.size() - 6, 6, "_phone") == 0) || k.find("address") != std::string::npos) {
            if (!strip(sor(raw.d->at(k))).empty())
                warnings.push_back(where + ": dropped the " + str_repr(k) + " column: phone numbers and home addresses are never collected");
            dict_pop(raw, k);
        } else if (!has(FIELDS, k) && k != "id" && k != "fit" && k != "reasons" && k != "added" && k != "last_relevant_deal") {
            if (!strip(sor(raw.d->at(k))).empty()) warnings.push_back(where + ": ignored the unknown column " + str_repr(k));
            dict_pop(raw, k);
        }
    }
    const auto rget = [&](const std::string& k) { return raw.d->at(k); };
    const std::string name = strip(sor(rget("name")));
    if (name.empty()) throw InvError(where + " has no name", 1);
    const std::string label = where + " (" + name + ")";
    for (const auto& [k, v] : raw.d->items) {
        if (k == "sources" || k == "source") continue;
        if (v.t == Val::STR && re::search(PHONE, v.s))
            throw InvError(label + ": " + k + " holds what looks like a phone number; phone numbers are never collected. Remove it and try again.", 1);
    }
    std::vector<std::string> all = split_urls(rget("sources")), more = split_urls(rget("source")), sources;
    all.insert(all.end(), more.begin(), more.end());
    for (const auto& s : all) if (!has(sources, s)) sources.push_back(s);
    for (const auto& s : sources)
        if (!re::match(URL, s)) throw InvError(label + ": a source must be a public URL (https://...), not " + str_repr(s), 1);
    if (sources.empty())
        throw InvError(label + " has no source: add the public page this came from (their site, a profile found through search, an SEC filing, the press)", 1);
    const std::string email = strip(sor(rget("email")));
    if (!email.empty()) {
        if (!re::match(EMAIL, email)) throw InvError(label + ": " + str_repr(email) + " is not an email address", 1);
        if (has(FREEMAIL, lower(split(email, "@")[1])))
            warnings.push_back(label + ": " + email + " is a personal-style address; use it only if they publish it for pitches");
    }
    Val stages = Val::list();
    for (const auto& s : split_list(rget("stage"))) stages.l->push_back(sv(norm_stage(sv(s))));
    Val cmin, cmax;
    if (!strip(sor(rget("check"))).empty()) std::tie(cmin, cmax) = money_range(rget("check"), label + " check");
    if (!strip(sor(rget("check_min"))).empty()) cmin = money(rget("check_min"), label + " check_min");
    if (!strip(sor(rget("check_max"))).empty()) cmax = money(rget("check_max"), label + " check_max");
    if (!cmin.is_none() && cmax.is_none()) cmax = cmin;
    if (!cmax.is_none() && cmin.is_none()) cmin = cmax;
    if (!cmin.is_none() && lt(cmax, cmin)) throw InvError(label + ": check_max is under check_min");
    const Val deal = rget("last_relevant_deal").t == Val::DICT ? rget("last_relevant_deal") : Val();
    Val wv = rget("last_deal"); if (!truthy(wv)) wv = deal.is_none() ? Val() : deal.d->at("what");
    Val dv = rget("last_deal_date"); if (!truthy(dv)) dv = deal.is_none() ? Val() : deal.d->at("date");
    const std::string what = strip(sor(wv)), when = strip(sor(dv));
    Val last;
    if (!what.empty() || !when.empty()) {
        if (when.empty()) throw InvError(label + ": last_deal needs last_deal_date (YYYY-MM-DD) for the recency score");
        const long long d = parse_date(sv(when), label + " last_deal_date");
        if (d > today) throw InvError(label + ": last_deal_date " + iso_day(d) + " is in the future", 1);
        last = obj({{"what", sv(what.empty() ? "a relevant deal" : what)}, {"date", sv(iso_day(d))}});
    }
    const std::string id = strip(sor(rget("id")));
    Val inv = obj({{"id", sv(id.empty() ? slug(sv(name)) : id)}, {"name", sv(name)}, {"firm", sv(strip(sor(rget("firm"))))},
                   {"role", sv(strip(sor(rget("role"))))}, {"email", sv(email)}, {"stage", stages}, {"check_min", cmin}, {"check_max", cmax},
                   {"sectors", strlist(split_list(rget("sectors")))}, {"geo", strlist(split_list(rget("geo")))}, {"last_relevant_deal", last},
                   {"sources", strlist(sources)}, {"warm_intro", bv(yes_ish(rget("warm_intro")))}, {"personal_line", sv(strip(sor(rget("personal_line"))))},
                   {"notes", sv(strip(sor(rget("notes"))))}, {"fit", Val()}, {"reasons", Val::list()}});
    if (inv.d->at("personal_line").s.empty())
        warnings.push_back(label + " has no personal_line yet: /seed-closer needs one before it drafts an email");
    return {inv, warnings};
}

inline bool falsy_literal(const Val& v) {   // v in (None, "", [], False)
    switch (v.t) {
        case Val::NONE: return true;
        case Val::STR: return v.s.empty();
        case Val::LIST: return v.l->empty();
        case Val::BOOL: return !v.b;
        case Val::INT: return !v.big() && v.i == 0;
        case Val::FLOAT: return v.f == 0;
        default: return false;
    }
}
// Add or update by email, else by name + firm. Returns 'added' or 'updated'.
inline std::pair<std::string, Val> merge(Val& book, Val inv, long long today) {
    const Val investors = at(book, "investors");
    const std::string email = inv.d->at("email").s, name = inv.d->at("name").s, firm = inv.d->at("firm").s;
    for (Val old : items_of(investors)) {
        const bool same_email = !email.empty() && lower(str_of(pget(old, "email", sv("")), "lower")) == lower(email);
        const bool same_name = lower(str_of(pget(old, "name", sv("")), "lower")) == lower(name) &&
                               lower(str_of(pget(old, "firm", sv("")), "lower")) == lower(firm);
        if (same_email || same_name) {
            const auto items = inv.d->items;
            for (const auto& [k, v] : items) {
                if (k == "id") continue;
                if (k == "sources") {
                    const Val have = vor(pget(old, "sources"), Val::list());
                    List merged = items_of(have);
                    for (const auto& s : items_of(v)) if (!py_in(s, have)) merged.push_back(s);
                    old.d->set("sources", Val::list(merged));
                } else if (!falsy_literal(v) || k == "warm_intro") old.d->set(k, v);
            }
            old.d->set("fit", Val()); old.d->set("reasons", Val::list());
            return {"updated", old};
        }
    }
    List ids;
    for (const Val& x : items_of(investors)) {
        const Val id = pget(x, "id");
        if (id.t == Val::LIST || id.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(id) + "'");
        ids.push_back(id);
    }
    const std::string base = inv.d->at("id").s;
    long long n = 2;
    auto taken = [&](const std::string& s) { for (const auto& x : ids) if (x.t == Val::STR && x.s == s) return true; return false; };
    while (taken(inv.d->at("id").s)) inv.d->set("id", sv(base + "-" + std::to_string(n++)));
    inv.d->set("added", sv(iso_day(today)));
    if (investors.t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(investors) + "' object has no attribute 'append'");
    investors.l->push_back(inv);
    return {"added", inv};
}

inline std::vector<std::pair<std::string, Val>> read_rows(const std::string& path) {
    if (!is_file(path)) throw InvError("no file " + path, 1);
    std::vector<std::pair<std::string, Val>> o;
    const std::string lp = lower(path);
    if (lp.size() >= 5 && lp.compare(lp.size() - 5, 5, ".json") == 0) {
        const Val doc = read_json(path, "investors file");
        const Val rows = doc.t == Val::DICT ? doc.d->at("investors") : doc;
        if (rows.t != Val::LIST) throw InvError(path + " wants a list of investors (or {\"investors\": [...]})");
        for (std::size_t i = 0; i < rows.l->size(); ++i) o.emplace_back("item " + std::to_string(i + 1), (*rows.l)[i]);
        return o;
    }
    std::string text;
    try { text = read_text(path, true, true); } catch (const OSError&) { throw InvError("no file " + path, 1); }
    const CsvDict cd = csv_dicts(text);
    for (std::size_t i = 0; i < cd.rows.size(); ++i) {
        Val r = Val::dict();
        for (const auto& [k, v] : cd.rows[i]) if (k) r.d->set(*k, v);   // the extra fields of a long row have no name
        o.emplace_back("row " + std::to_string(i + 2), r);
    }
    return o;
}

// ---------------------------------------------------------------- fit
inline std::pair<Val, Val> criteria_of(const Val& a, const Val& book, const Val& cfg) {
    const Val st = vor(pget(cfg, "startup"), Val::dict());
    const Val old = vor(pget(book, "criteria"), Val::dict());
    Val stage = get(a, "stage");
    if (!truthy(stage)) stage = pget(old, "stage");
    if (!truthy(stage)) stage = pget(st, "stage");
    Val sectors, geo;
    if (truthy(get(a, "sectors"))) sectors = strlist(split_list(get(a, "sectors")));
    else { sectors = pget(old, "sectors"); if (!truthy(sectors)) sectors = strlist(split_list(pget(st, "sector"))); }
    if (truthy(get(a, "geo"))) geo = strlist(split_list(get(a, "geo")));
    else { geo = pget(old, "geo"); if (!truthy(geo)) geo = strlist(split_list(pget(st, "geography"))); }
    Val ask;
    if (truthy(get(a, "ask"))) ask = money(get(a, "ask"), "--ask");
    else if (truthy(pget(book, "ask"))) ask = money(at(book, "ask"), "the ask");
    else if (truthy(pget(st, "raise"))) ask = money(at(st, "raise"), "startup.raise in config.json");
    std::vector<std::string> missing;
    if (!truthy(stage)) missing.push_back("stage");
    if (!truthy(sectors)) missing.push_back("sectors");
    if (!truthy(geo)) missing.push_back("geo");
    if (!truthy(ask)) missing.push_back("ask");
    if (!missing.empty())
        throw InvError("rank needs your " + join(missing, ", ") + ": pass --" + missing[0] + ", or fill seed/config.json (startup.stage, sector, geography, raise)", 2);
    Val cmin, cmax;
    if (truthy(get(a, "check"))) std::tie(cmin, cmax) = money_range(get(a, "check"));
    else if (truthy(pget(old, "check")) && !truthy(get(a, "ask"))) { cmin = money(at(at(old, "check"), "min")); cmax = money(at(at(old, "check"), "max")); }
    else { cmin = round_int(ask.num() * 0.05); cmax = round_int(ask.num() * 0.5); }
    return {obj({{"stage", sv(norm_stage(stage))}, {"sectors", sectors}, {"geo", geo}, {"check", obj({{"min", cmin}, {"max", cmax}})}}), ask};
}
inline long long recency_points(long long days) {   // 15 x (1 - days / 730), rounded half up, exact in integers
    if (days <= 0) return W_RECENCY;
    if (days >= RECENCY_DAYS) return 0;
    const long long num = W_RECENCY * (RECENCY_DAYS - days);
    return (2 * num + RECENCY_DAYS) / (2 * RECENCY_DAYS);
}
inline long long stage_index(const std::string& s) { return std::find(STAGES.begin(), STAGES.end(), s) - STAGES.begin(); }
// (fit, reasons) or (None, [why it was refused])
inline std::pair<std::optional<long long>, std::vector<std::string>> score(Val inv, const Val& crit, const Val& ask, long long today) {
    for (const char* k : {"stage", "sectors", "geo", "sources"}) {   // a hand-edited file may hold "seed", not ["seed"]
        const Val v = pget(inv, k);
        if (v.t == Val::STR) inv.d->set(k, strlist(std::string(k) != "sources" ? split_list(v) : split_urls(v)));
    }
    bool sourced = false;
    for (const auto& s : items_of(vor(pget(inv, "sources"), Val::list()))) if (re::match(URL, pystr(s))) sourced = true;
    if (!sourced) return {std::nullopt, {"refused: no public source; add the page this came from, then rank again"}};
    long long pts = 0; std::vector<std::string> why;
    std::vector<std::string> stages;
    for (const auto& s : items_of(vor(pget(inv, "stage"), Val::list()))) { try { stages.push_back(norm_stage(s)); } catch (const InvError&) {} }
    const std::string ours = crit.d->at("stage").s;
    long long off = 1LL << 40;
    for (const auto& s : stages) off = std::min(off, std::llabs(stage_index(s) - stage_index(ours)));
    if (has(stages, ours)) { pts += W_STAGE; why.push_back("invests at " + ours + " (+" + std::to_string(W_STAGE) + ")"); }
    else if (!stages.empty() && off == 1) { pts += 12; why.push_back("invests at " + join(stages, ", ") + ", one stage off " + ours + " (+12)"); }
    else if (!stages.empty()) why.push_back("invests at " + join(stages, ", ") + ", not " + ours + " (+0)");
    else why.push_back("stage unknown: check their site (+0)");

    const List hit = sector_match(items_of(crit.d->at("sectors")), items_of(vor(pget(inv, "sectors"), Val::list())));
    const long long p = hit.size() >= 2 ? W_SECTORS : hit.empty() ? 0 : 15;
    pts += p;
    why.push_back(!hit.empty() ? "sectors match: " + join_strs(hit, ", ") + " (+" + std::to_string(p) + ")"
                               : "no sector overlap with " + join_strs(items_of(crit.d->at("sectors")), ", ") + " (+0)");

    const Val lo = pget(inv, "check_min"), hi = pget(inv, "check_max");
    const Val chk = crit.d->at("check"), cmin = chk.d->at("min"), cmax = chk.d->at("max");
    const std::string want = short_money(cmin) + "-" + short_money(cmax);
    if (lo.is_none() || hi.is_none()) why.push_back("check size unknown (+0)");
    else {
        const std::string theirs = py_eq(lo, hi) ? short_money(lo) : short_money(lo) + "-" + short_money(hi);
        if (gt(lo, ask)) why.push_back("their smallest check, " + short_money(lo) + ", is bigger than your whole " + short_money(ask) + " round (+0)");
        else if (le(lo, cmax) && ge(hi, cmin)) { pts += W_CHECK; why.push_back("checks of " + theirs + " fit the " + want + " you want (+" + std::to_string(W_CHECK) + ")"); }
        else if (ge(times2(hi), cmin) && le(lo, times2(cmax))) { pts += 10; why.push_back("checks of " + theirs + " are close to the " + want + " you want (+10)"); }
        else why.push_back("checks of " + theirs + " are far from the " + want + " you want (+0)");
    }

    const Val deal = pget(inv, "last_relevant_deal");
    if (truthy(deal) && truthy(pget(deal, "date"))) {
        const long long d = parse_date(at(deal, "date"), "last_relevant_deal.date");
        const long long days = today - d, rp = recency_points(days);
        pts += rp;
        why.push_back(pystr(vor(pget(deal, "what"), sv("a relevant deal"))) + ", " + std::to_string(std::max(days, 0LL)) + " days ago (+" + std::to_string(rp) + ")");
    } else why.push_back("no relevant deal on record (+0)");

    const auto [ok, gr] = geo_match(items_of(crit.d->at("geo")), items_of(vor(pget(inv, "geo"), Val::list())));
    if (ok) { pts += W_GEO; why.push_back(gr + " (+" + std::to_string(W_GEO) + ")"); }
    else why.push_back(truthy(pget(inv, "geo")) ? "invests in " + join_strs(items_of(at(inv, "geo")), ", ") + ", not " + join_strs(items_of(crit.d->at("geo")), ", ") + " (+0)"
                                                : "geography unknown (+0)");

    if (truthy(pget(inv, "warm_intro"))) { pts += W_WARM; why.push_back("warm intro available (+" + std::to_string(W_WARM) + ")"); }
    return {pts, why};
}
inline std::pair<List, List> rank_book(Val& book, const Val& crit, const Val& ask, long long today) {
    List ranked, refused;
    for (Val inv : items_of(at(book, "investors"))) {
        const auto [fit, why] = score(inv, crit, ask, today);
        inv.d->set("fit", fit ? iv(*fit) : Val()); inv.d->set("reasons", strlist(why));
        (fit ? ranked : refused).push_back(inv);
    }
    std::vector<std::pair<std::pair<long long, std::string>, Val>> keyed;
    for (const auto& x : ranked) keyed.push_back({{-x.d->at("fit").i, lower(str_of(at(x, "name"), "lower"))}, x});
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    ranked.clear();
    for (const auto& k : keyed) ranked.push_back(k.second);
    List all = ranked; all.insert(all.end(), refused.begin(), refused.end());
    book.d->set("investors", Val::list(all));
    book.d->set("criteria", crit);
    book.d->set("ask", ask);
    book.d->set("generated", sv(iso_day(today)));
    book.d->set("created_by", sv("seed-investors"));
    return {ranked, refused};
}
inline std::string table_row(const std::string& a, const std::string& b, const std::string& c, const std::string& d, const std::string& e, const std::string& f) {
    return "  " + rjust(a, 3) + "  " + ljust(b, 16) + " " + ljust(c, 22) + " " + ljust(d, 12) + " " + ljust(e, 16) + " " + f;
}
inline void print_table(const List& rows, long long n = 0) {
    print(table_row("fit", "name", "firm", "check", "stage", "id"));
    const std::size_t count = n ? slice_n(rows.size(), n) : rows.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Val& x = rows[i];
        std::string chk = "?";
        if (!pget(x, "check_min").is_none()) {
            const Val lo = at(x, "check_min"), hi = at(x, "check_max");
            chk = py_eq(lo, hi) ? short_money(lo) : short_money(lo) + "-" + short_money(hi);
        }
        print(table_row(pget(x, "fit").is_none() ? "-" : pystr(at(x, "fit")), head_of(at(x, "name"), 16), head_of(vor(pget(x, "firm"), sv("")), 22), chk,
                        cpslice(join_strs(items_of(vor(pget(x, "stage"), Val::list())), ", "), 0, 16), pystr(at(x, "id"))));
    }
}

// ---------------------------------------------------------------- SEC EDGAR: Form D
inline std::string sec_user_agent(const std::string& cli, const std::string& sd) {
    const char* env = std::getenv("SEC_USER_AGENT");
    std::string ua = strip(!cli.empty() ? cli : (env && *env) ? std::string(env) : std::string());
    if (ua.empty()) ua = strip(sor(pget(load_config(sd), "sec_user_agent")));
    if (ua.empty())
        throw InvError("SEC asks every automated request to say who is asking: set sec_user_agent in seed/config.json (or SEC_USER_AGENT) to \"Your Name your@email\"", 1);
    const re::Match m = re::search(R"([^@\s]+@[^@\s]+\.[A-Za-z]{2,})", ua);
    const std::string low = lower(ua);
    bool placeholder = static_cast<bool>(re::search(R"(@example\.|\.example\b|\byou@|\byour[ ._-]?(?:name|email)\b|\bname@|\bemail@)", low));
    if (!placeholder && m) {
        const std::string who = lower(split(m.group(0), "@")[0]);
        placeholder = who == "you" || who == "user" || who == "name" || who == "email" || who == "me";
    }
    if (!m || strip(replace_all(ua, m.group(0), "")).empty() || placeholder)
        throw InvError("sec_user_agent must be your real name and email, like \"Ada Lovelace ada@yourstartup.com\" (got " + str_repr(ua) + ")", 1);
    return ua;
}
struct Sec {   // live SEC requests, or a folder of saved answers (--offline)
    std::string ua, offline;
    double pause = PAUSE;
    std::optional<std::chrono::steady_clock::time_point> last;
    std::optional<std::string> get(const std::string& url, const std::string& name) {
        std::string raw;
        if (!offline.empty()) {
            const std::string path = join_path(offline, name);
            if (!is_file(path)) return std::nullopt;
            raw = read_bytes(path);
        } else {
            if (last) {
                const double gap = std::chrono::duration<double>(std::chrono::steady_clock::now() - *last).count();
                if (gap < pause) std::this_thread::sleep_for(std::chrono::duration<double>(pause - gap));
            }
            last = std::chrono::steady_clock::now();
            const HttpResp r = packtools::http_get(url, {"User-Agent: " + ua, "Accept-Encoding: gzip"}, TIMEOUT);
            if (!r.error.empty()) throw InvError("could not reach SEC EDGAR: " + r.error);
            if (r.status < 200 || r.status >= 300) {
                if (r.status == 404) return std::nullopt;
                if (r.status == 403)
                    throw InvError("SEC refused the request (HTTP 403): its fair-access rule wants your real name and email in sec_user_agent, and fewer than 10 requests a second");
                if (r.status == 429) throw InvError("SEC says slow down (HTTP 429): wait ten minutes and try again");
                throw InvError("SEC answered HTTP " + std::to_string(r.status) + " for " + split(url, "?")[0]);
            }
            raw = r.body;
        }
        if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0x1f && static_cast<unsigned char>(raw[1]) == 0x8b) raw = pyrt::zip::gunzip(raw);
        return decode_utf8_replace(raw);
    }
};
inline std::string efts_url(const std::string& q, const std::string& since, const std::string& until, long long start) {
    std::string query = strip(q);
    if (query.find(' ') != std::string::npos && query.find('"') == std::string::npos) query = "\"" + query + "\"";   // a phrase, not any of the words
    std::vector<std::pair<std::string, std::string>> p = {{"q", query}, {"dateRange", "custom"}, {"startdt", since}, {"enddt", until}, {"forms", "D"}};
    if (start) p.emplace_back("from", std::to_string(start));
    std::vector<std::string> kv;
    for (const auto& [k, v] : p) kv.push_back(seed_scout::quote_plus(k) + "=" + seed_scout::quote_plus(v));
    return EFTS + join(kv, "&");
}
inline std::string company_of(const Val& display) {   // 'Pawprint Labs, Inc.  (CIK 0001999999)' -> 'Pawprint Labs, Inc.'
    std::string s;
    if (truthy(display)) {
        if (display.t != Val::STR) throw PyErr("TypeError", std::string("expected string or bytes-like object, got '") + type_name(display) + "'");
        s = display.s;
    }
    return strip(re::sub(R"(\s*\((?:[A-Z0-9.\- ,]+)\)\s*)", " ", re::sub(R"(\s*\(CIK \d+\)\s*$)", "", s)));
}
using XNode = pyrt::xml::Node;
inline std::vector<const XNode*> xfindall(const XNode* el, const std::string& path) {   // Element.findall on a namespace-stripped tree
    std::vector<const XNode*> cur{el};
    for (const auto& step : split(path, "/")) {
        std::vector<const XNode*> next;
        for (const auto* c : cur) for (const auto& k : c->kids) if (pyrt::xml::local(k->tag) == step) next.push_back(k.get());
        cur.swap(next);
    }
    return cur;
}
inline const XNode* xfind(const XNode* el, const std::string& path) { if (!el) return nullptr; const auto v = xfindall(el, path); return v.empty() ? nullptr : v[0]; }
inline std::string txt(const XNode* el, const std::string& path) { const XNode* x = xfind(el, path); return x && !x->text.empty() ? strip(x->text) : ""; }
inline Val amount(const std::string& s0) {
    const std::string s = strip(s0);
    if (s.empty()) return Val();
    if (packtools::seed::starts(lower(s), "indefinite")) return sv("indefinite");
    const auto f = parse_float(s);
    if (!f || std::isnan(*f)) return Val();   // float() refused it, or round(nan) raised ValueError
    return round_int(*f);
}
inline std::string as_utf8_text(const std::string& text) {   // ET.fromstring(str) parses text as UTF-8 whatever the declaration says
    if (!packtools::seed::starts(text, "<?xml")) return text;
    const auto e = text.find("?>");
    if (e == std::string::npos) return text;
    const auto k = text.find("encoding");
    if (k == std::string::npos || k > e) return text;
    const auto q = text.find_first_of("\"'", k);
    if (q == std::string::npos || q > e) return text;
    const auto q2 = text.find(text[q], q + 1);
    if (q2 == std::string::npos || q2 > e) return text;
    return text.substr(0, q + 1) + "utf-8" + text.substr(q2);
}
// primary_doc.xml -> the facts a founder needs. Names and roles of related persons, never addresses.
inline Val parse_form_d(const std::string& xml_text) {
    std::shared_ptr<XNode> rootp;
    try { rootp = pyrt::xml::fromstring(as_utf8_text(xml_text)); }
    catch (const pyrt::xml::ParseError& e) { throw InvError(std::string("a Form D document is not valid XML: ") + e.what()); }
    const XNode* root = rootp.get();
    const XNode* iss = xfind(root, "primaryIssuer");
    const XNode* off = xfind(root, "offeringData");
    const XNode* sec = xfind(off, "typesOfSecuritiesOffered");
    Val securities = Val::list();
    for (const auto& [tag, label] : SECURITIES) if (lower(txt(sec, tag)) == "true") securities.l->push_back(sv(label));
    if (lower(txt(sec, "isOtherType")) == "true") { const std::string d = txt(sec, "descriptionOfOtherType"); securities.l->push_back(sv(d.empty() ? "other" : d)); }
    Val people = Val::list();
    for (const XNode* p : xfindall(root, "relatedPersonsList/relatedPersonInfo")) {
        std::vector<std::string> parts;
        for (const auto& x : {txt(p, "relatedPersonName/firstName"), txt(p, "relatedPersonName/lastName")}) if (!x.empty()) parts.push_back(x);
        const std::string n = join(parts, " ");
        Val rel = Val::list();
        for (const XNode* r : xfindall(p, "relatedPersonRelationshipList/relationship")) if (!r->text.empty()) rel.l->push_back(sv(strip(r->text)));
        if (!n.empty()) people.l->push_back(obj({{"name", sv(n)}, {"relationships", rel}, {"clarification", sv(txt(p, "relationshipClarification"))}}));
    }
    const std::string first = txt(off, "typeOfFiling/dateOfFirstSale/value");
    const std::string year = txt(iss, "yearOfInc/value"), fund = txt(off, "industryGroup/investmentFundInfo/investmentFundType");
    Val first_sale = !first.empty() ? sv(first) : (off && xfind(off, "typeOfFiling/dateOfFirstSale/yetToOccur")) ? sv("yet to occur") : Val();
    return obj({{"company", sv(txt(iss, "entityName"))}, {"cik", sv(txt(iss, "cik"))},
                {"city", sv(txt(iss, "issuerAddress/city"))}, {"state", sv(txt(iss, "issuerAddress/stateOrCountry"))},
                {"incorporated", sv(txt(iss, "jurisdictionOfInc"))}, {"year_of_inc", year.empty() ? Val() : sv(year)},
                {"entity_type", sv(txt(iss, "entityType"))},
                {"industry", sv(txt(off, "industryGroup/industryGroupType"))},
                {"fund_type", fund.empty() ? Val() : sv(fund)},
                {"amendment", bv(lower(txt(off, "typeOfFiling/newOrAmendment/isAmendment")) == "true")},
                {"first_sale", first_sale},
                {"securities", securities},
                {"amount_offered", amount(txt(off, "offeringSalesAmounts/totalOfferingAmount"))},
                {"amount_sold", amount(txt(off, "offeringSalesAmounts/totalAmountSold"))},
                {"amount_remaining", amount(txt(off, "offeringSalesAmounts/totalRemaining"))},
                {"investors_count", amount(txt(off, "investors/totalNumberAlreadyInvested"))},
                {"minimum_investment", amount(txt(off, "minimumInvestmentAccepted"))},
                {"related_persons", people}});
}
inline std::pair<List, Val> search_form_d(Sec& sec, const std::string& q, const std::string& since, const std::string& until, long long max_hits) {
    List hits; Val total; long long start = 0;
    const long long page_n = page_size();
    while (static_cast<long long>(hits.size()) < max_hits) {
        const auto text = sec.get(efts_url(q, since, until, start), "efts-" + std::to_string(start) + ".json");
        if (!text) break;
        Val doc;
        try { doc = loads(*text); }
        catch (const PyErr& e) {
            if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError") throw InvError("EDGAR full-text search sent something that is not JSON");
            throw;
        }
        const Val block = vor(pget(doc, "hits"), Val::dict());
        const Val tot = pget(block, "total");
        total = tot.t == Val::DICT ? pget(tot, "value") : tot;
        const Val page = vor(pget(block, "hits"), Val::list());
        const List items = items_of(page);
        hits.insert(hits.end(), items.begin(), items.end());
        start += page_n;
        if (!truthy(page) || static_cast<long long>(items.size()) < page_n || (!total.is_none() && ge(iv(start), total))) break;
    }
    if (static_cast<long long>(hits.size()) > max_hits) hits.resize(static_cast<std::size_t>(max_hits));
    return {hits, total};
}
inline int key_cmp(const Val& a, const Val& b) {   // Python's < between two sort-key items
    if (a.t == Val::STR && b.t == Val::STR) return a.s < b.s ? -1 : a.s > b.s ? 1 : 0;
    if (py_eq(a, b)) return 0;
    return lt(a, b) ? -1 : 1;
}
inline std::pair<List, Val> collect(Sec& sec, const std::string& q, const std::string& since, const std::string& until, long long max_hits = 40,
                                    bool details = true, bool amendments = true) {
    const auto [hits, total] = search_form_d(sec, q, since, until, max_hits);
    List seen, filings;
    for (const Val& h : hits) {
        const Val s = vor(pget(h, "_source"), Val::dict());
        auto id_str = [&]() { const Val id = pget(h, "_id"); return truthy(id) ? str_of(id, "split") : std::string(); };
        Val adsh = pget(s, "adsh");
        if (!truthy(adsh)) adsh = sv(split(id_str(), ":")[0]);
        if (!truthy(adsh) || py_in(adsh, Val::list(seen))) continue;
        const Val form = vor(vor(pget(s, "form"), pget(s, "file_type")), sv("D"));
        if (!amendments && upper(str_of(form, "upper")) != "D") continue;
        seen.push_back(adsh);
        const Val ciks = vor(pget(s, "ciks"), Val::list());
        const std::string ad = str_of(adsh, "replace");
        const BigInt cik = truthy(ciks) ? py_int(first_item(ciks)) : py_int(sv(split(ad, "-")[0]));
        const std::string idv = id_str();
        const auto colon = idv.find(':');
        const std::string fname = colon != std::string::npos ? idv.substr(colon + 1) : "primary_doc.xml";
        const std::string folder = replace_all(ad, "-", "");
        const Val names = vor(pget(s, "display_names"), Val::list());
        const std::string archive = "https://www.sec.gov/Archives/edgar/data/" + int_str(cik) + "/" + folder + "/";
        Val f = obj({{"adsh", adsh}, {"cik", sv(fmt_010d(cik))}, {"company", sv(truthy(names) ? company_of(first_item(names)) : "")},
                     {"form", form}, {"filed", pget(s, "file_date")}, {"location", sv(join_strs(items_of(vor(pget(s, "biz_locations"), Val::list())), ", "))},
                     {"state_of_inc", sv(join_strs(items_of(vor(pget(s, "inc_states"), Val::list())), ", "))},
                     {"url", sv(archive + ad + "-index.htm")}, {"doc_url", sv(archive + fname)}});
        if (details) {
            const auto xml_text = sec.get(archive + fname, ad + ".xml");
            if (xml_text && !xml_text->empty()) {
                const Val d = parse_form_d(*xml_text);
                for (const auto& [k, v] : d.d->items) if ((k != "company" && k != "cik") || !truthy(f.d->at(k))) f.d->set(k, v);
                f.d->set("amendment", bv(d.d->at("amendment").b || upper(str_of(form, "upper")) != "D"));
            }
        }
        filings.push_back(f);
    }
    std::vector<std::pair<std::pair<Val, Val>, Val>> keyed;
    for (const auto& f : filings) keyed.push_back({{vor(f.d->at("filed"), sv("")), f.d->at("adsh")}, f});
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {   // reverse=True keeps equal keys in their order
        const int c = key_cmp(b.first.first, a.first.first);
        return c != 0 ? c < 0 : key_cmp(b.first.second, a.first.second) < 0;
    });
    List sorted;
    for (const auto& k : keyed) sorted.push_back(k.second);
    return {sorted, total};
}
inline Val summarise(const List& filings) {
    std::vector<BigInt> sold;
    for (const auto& f : filings) { const Val v = f.d->at("amount_sold"); if (v.t == Val::INT && gt(v, iv(0))) sold.push_back(big_of(v)); }
    List ciks;
    for (const auto& f : filings) { const Val c = f.d->at("cik"); bool dup = false; for (const auto& x : ciks) if (py_eq(x, c)) dup = true; if (!dup) ciks.push_back(c); }
    Val median, total;
    if (!sold.empty()) {
        std::vector<BigInt> s = sold;
        std::sort(s.begin(), s.end(), [](const BigInt& a, const BigInt& b) { return a < b; });
        const std::size_t n = s.size();
        median = int_val(n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / BigInt(2));
        BigInt t(0); for (const auto& x : sold) t = t + x;
        total = int_val(t);
    }
    return obj({{"filings", iv(static_cast<long long>(filings.size()))}, {"companies", iv(static_cast<long long>(ciks.size()))},
                {"with_amounts", iv(static_cast<long long>(sold.size()))}, {"median_sold", median}, {"total_sold", total}});
}
inline std::vector<std::string> leads_of(const Val& f) {   // directors who are not officers: often an investor's board seat
    std::vector<std::string> out;
    for (const auto& p : items_of(vor(pget(f, "related_persons"), Val::list()))) {
        std::vector<std::string> rel;
        for (const auto& r : items_of(vor(pget(p, "relationships"), Val::list()))) rel.push_back(lower(str_of(r, "lower")));
        if (has(rel, "director") && !has(rel, "executive officer")) out.push_back(pystr(at(p, "name")));
    }
    return out;
}

// ---------------------------------------------------------------- commands
inline int cmd_formd(const Val& a) {
    const long long today = today_of(get(a, "today"));
    const std::string since = iso_day(parse_date(get(a, "since"), "--since"));
    const std::string until = truthy(get(a, "until")) ? iso_day(parse_date(get(a, "until"), "--until")) : iso_day(today);
    if (until < since) throw InvError("--until is before --since");
    const std::string q = S(a, "q");
    if (strip(q).empty()) throw InvError("--q needs the words to search for");
    const long long mx = get(a, "max").i;
    if (mx < 1 || mx > 500) throw InvError("--max is 1 to 500");
    const std::string offline = S(a, "offline");
    Sec sec{offline.empty() ? sec_user_agent(S(a, "ua"), seed_dir(a)) : "", offline, PAUSE, std::nullopt};
    const auto [filings, total] = collect(sec, q, since, until, mx, !B(a, "no_details"), !B(a, "no_amendments"));
    const Val summ = summarise(filings);
    const Val doc = obj({{"created_by", sv("seed-investors")}, {"generated", sv(iso_day(today))}, {"query", sv(q)}, {"since", sv(since)},
                         {"until", sv(until)}, {"total_hits", total}, {"source", sv(efts_url(q, since, until, 0))},
                         {"summary", summ}, {"filings", Val::list(filings)}});
    const bool js = B(a, "json");
    if (js) print(dump_json(doc));
    else if (filings.empty())
        print("No Form D filings matched " + str_repr(q) + " from " + since + " to " + until + ". Form D has no business description: try the words "
              "founders put in company names (one word, like \"pet\" or \"paw\").");
    else {
        print("SEC EDGAR Form D filings matching " + str_repr(q) + ", " + since + " to " + until + ": " + std::to_string(filings.size()) + " shown of " +
              (total.is_none() ? std::string("?") : pystr(total)));
        print("  " + ljust("filed", 10) + " " + ljust("company", 30) + " " + ljust("where", 18) + " " + rjust("sold", 12) + " " + rjust("offered", 12) + " " +
              rjust("inv.", 5) + "  securities");
        for (const auto& f : filings) {
            const Val off = pget(f, "amount_offered"), sold = pget(f, "amount_sold"), cnt = pget(f, "investors_count");
            const std::string offered = off.t == Val::STR && off.s == "indefinite" ? "indefinite" : off.t == Val::INT ? full_money(off) : "?";
            print("  " + ljust(truthy(pget(f, "filed")) ? pystr(pget(f, "filed")) : "?", 10) + " " + ljust(head_of(vor(at(f, "company"), sv("?")), 30), 30) + " " +
                  ljust(head_of(vor(pget(f, "location"), sv("")), 18), 18) + " " + rjust(sold.t == Val::INT ? full_money(sold) : "?", 12) + " " +
                  rjust(offered, 12) + " " + rjust(cnt.is_none() ? "?" : pystr(cnt), 5) + "  " +
                  join_strs(items_of(vor(pget(f, "securities"), Val::list())), ", ") + (truthy(pget(f, "amendment")) ? " (amendment)" : ""));
            if (truthy(pget(f, "fund_type")))
                print("  " + std::string(10, ' ') + " a " + lower(str_of(at(f, "fund_type"), "lower")) + " raising money: an investor to research, not a comparable startup");
            const auto lead = leads_of(f);
            if (!lead.empty()) print("  " + std::string(10, ' ') + " directors who are not officers (look them up): " + join(lead, ", "));
        }
        const Val med = summ.d->at("median_sold"), tot = summ.d->at("total_sold");
        print(pystr(summ.d->at("filings")) + " filings from " + pystr(summ.d->at("companies")) + " companies. Amount sold: median " +
              (truthy(med) ? full_money(med) : "?") + ", total " + (truthy(tot) ? full_money(tot) : "?") + " (the " + pystr(summ.d->at("with_amounts")) +
              " that state it).");
        print("Form D names the company, not its investors. Search the press for \"<company> seed round\" to find who led, then add them with a source.");
    }
    if (B(a, "write")) {
        const std::string path = join_path(seed_dir(a), "formd.json");
        seed_scout::write_json(path, doc);
        if (!js) print("Wrote " + path + ".");
    }
    return filings.empty() ? 1 : 0;
}
inline int save_rows(const Val& a, const std::vector<std::pair<std::string, Val>>& rows) {
    const long long today = today_of(get(a, "today"));
    const std::string sd = seed_dir(a);
    Val book = load_book(sd);
    std::vector<std::pair<std::string, Val>> done;
    std::vector<std::string> refused, warnings;
    for (const auto& [where, raw] : rows) {
        try {
            auto [inv, w] = clean_row(raw, today, where);
            warnings.insert(warnings.end(), w.begin(), w.end());
            done.push_back(merge(book, inv, today));
        } catch (const InvError& e) { refused.push_back(e.what()); }
    }
    for (const auto& w : warnings) print("Note: " + w);
    for (const auto& r : refused) print("Refused: " + r);
    if (!done.empty()) {
        book.d->set("created_by", sv("seed-investors"));
        book.d->set("generated", sv(iso_day(today)));
        const std::string path = join_path(sd, "investors.json");
        seed_scout::write_json(path, book);
        for (const auto& [how, inv] : done) {
            const std::size_t n = items_of(at(inv, "sources")).size();
            print(capitalize(how) + " " + pystr(at(inv, "name")) + " (" + pystr(at(inv, "id")) + "), " + std::to_string(n) + " source" + (n == 1 ? "" : "s") + ".");
        }
        print("Wrote " + path + ": " + std::to_string(items_of(at(book, "investors")).size()) + " investors. Next: investors.py rank --write.");
    }
    return !done.empty() && refused.empty() ? 0 : 1;
}
inline int cmd_import(const Val& a) { return save_rows(a, read_rows(S(a, "file"))); }
inline int cmd_add(const Val& a) {
    Val raw = Val::dict();
    const Val src = get(a, "source");
    const std::vector<std::pair<std::string, Val>> kv = {{"name", get(a, "name")}, {"firm", get(a, "firm")}, {"role", get(a, "role")},
        {"email", get(a, "email")}, {"stage", get(a, "stage_list")}, {"check", get(a, "check")}, {"sectors", get(a, "sectors")}, {"geo", get(a, "geo")},
        {"last_deal", get(a, "last_deal")}, {"last_deal_date", get(a, "last_deal_date")}, {"sources", truthy(src) ? src : Val::list()},
        {"warm_intro", bv(B(a, "warm_intro"))}, {"personal_line", get(a, "personal_line")}, {"notes", get(a, "notes")}};
    for (const auto& [k, v] : kv) if (!v.is_none() && !(v.t == Val::STR && v.s.empty())) raw.d->set(k, v);
    return save_rows(a, {{"--name", raw}});
}
inline int cmd_rank(const Val& a) {
    const std::string sd = seed_dir(a);
    Val book = load_book(sd);
    if (!truthy(at(book, "investors"))) {
        print("No investors in " + join_path(sd, "investors.json") + " yet. Add them: investors.py import FILE.csv, or investors.py add.");
        return 1;
    }
    const Val cfg = load_config(sd);
    const auto [crit, ask] = criteria_of(a, book, cfg);
    const long long today = today_of(get(a, "today"));
    const auto [ranked, refused] = rank_book(book, crit, ask, today);
    const bool js = B(a, "json");
    if (js) print(dump_json(book));
    else {
        const Val chk = crit.d->at("check");
        print("Investor fit for a " + short_money(ask) + " " + crit.d->at("stage").s + " round in " + join_strs(items_of(crit.d->at("geo")), ", ") + ", sectors " +
              join_strs(items_of(crit.d->at("sectors")), ", ") + "; checks of " + short_money(chk.d->at("min")) + "-" + short_money(chk.d->at("max")) + " wanted");
        print_table(ranked);
        const std::size_t n = slice_n(ranked.size(), get(a, "reasons").i);
        for (std::size_t i = 0; i < n; ++i) {
            const Val& x = ranked[i];
            print("  " + pystr(at(x, "name")) + " (" + fmt_d(at(x, "fit")) + "): " + join_strs(items_of(at(x, "reasons")), "; "));
        }
        for (const auto& x : refused) print("Refused: " + pystr(at(x, "name")) + " (" + pystr(at(x, "id")) + "): " + pystr(first_item(at(x, "reasons"))));
    }
    if (B(a, "write")) {
        const std::string path = join_path(sd, "investors.json");
        seed_scout::write_json(path, book);
        if (!js) print("Wrote " + path + ".");
    } else if (!js) print("Not saved: add --write so /seed-closer can use the fits.");
    return ranked.empty() ? 1 : 0;
}
inline int cmd_show(const Val& a) {
    const std::string sd = seed_dir(a), path = join_path(sd, "investors.json");
    if (!is_file(path)) { print("No " + path + " yet. Add investors: investors.py import FILE.csv"); return 1; }
    const Val book = load_book(sd);
    const Val id = get(a, "id");
    if (truthy(id)) {
        List hit;
        for (const auto& x : items_of(at(book, "investors")))
            if (py_eq(at(x, "id"), id) || lower(str_of(at(x, "name"), "lower")) == lower(id.s)) hit.push_back(x);
        if (hit.empty()) { print("No investor " + str_repr(id.s) + " in " + path + "."); return 1; }
        const Val x = hit[0];
        if (B(a, "json")) { print(dump_json(x)); return 0; }
        print(pystr(at(x, "name")) + ", " + pystr(vor(pget(x, "role"), sv("role ?"))) + ", " + pystr(vor(pget(x, "firm"), sv("firm ?"))) + " (" + pystr(at(x, "id")) + ")");
        print("  fit       " + (pget(x, "fit").is_none() ? std::string("not ranked yet") : pystr(at(x, "fit"))));
        for (const auto& r : items_of(vor(pget(x, "reasons"), Val::list()))) print("            " + pystr(r));
        print("  email     " + pystr(vor(pget(x, "email"), sv("none yet"))));
        print("  stage     " + join_strs(items_of(vor(pget(x, "stage"), Val::list())), ", "));
        print("  checks    " + (pget(x, "check_min").is_none() ? std::string("?") : short_money(at(x, "check_min")) + "-" + short_money(at(x, "check_max"))));
        print("  sectors   " + join_strs(items_of(vor(pget(x, "sectors"), Val::list())), ", "));
        print("  geo       " + join_strs(items_of(vor(pget(x, "geo"), Val::list())), ", "));
        const Val d = pget(x, "last_relevant_deal");
        print("  last deal " + (truthy(d) ? pystr(at(d, "what")) + " (" + pystr(at(d, "date")) + ")" : std::string("none on record")));
        print("  warm      " + std::string(truthy(pget(x, "warm_intro")) ? "yes" : "no"));
        print("  line      " + pystr(vor(pget(x, "personal_line"), sv("none yet: /seed-closer needs one"))));
        for (const auto& s : items_of(vor(pget(x, "sources"), Val::list()))) print("  source    " + pystr(s));
        return 0;
    }
    if (B(a, "json")) { print(dump_json(book)); return 0; }
    const List all = items_of(at(book, "investors"));
    print(std::to_string(all.size()) + " investors in " + path + " (ranked " + pystr(vor(pget(book, "generated"), sv("never"))) + ")");
    const Val top = get(a, "top");
    print_table(all, top.is_none() ? 0 : top.i);
    return 0;
}

inline void common(Parser& s) {
    s.opt({"--dir"}).help("the seed folder (default ./seed)");
    s.opt({"--today"}).help("YYYY-MM-DD (default: today)");
    s.opt({"--json"}).store_true();
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("investors.py", "Investor Finder (seed-investors): public research, ranked by fit.");
    p.subcommands("cmd", false);
    { Parser& s = p.sub("formd"); s.help_line = "SEC EDGAR Form D: who in your space raised, how much, when";
      s.opt({"--q"}).required().help("the words, e.g. \"pet care\" (a phrase when it has spaces)");
      s.opt({"--since"}).required().help("YYYY-MM-DD");
      s.opt({"--until"}).help("YYYY-MM-DD (default today)");
      s.opt({"--max"}).integer().def(40).help("filings to read, 1 to 500 (default 40)");
      s.opt({"--no-details"}).store_true().help("skip each filing's amounts (one request a filing)");
      s.opt({"--no-amendments"}).store_true().help("drop D/A amendments");
      s.opt({"--ua"}).help("SEC User-Agent (default: SEC_USER_AGENT or sec_user_agent in config.json)");
      s.opt({"--offline"}).metavar("DIR").help("saved efts-<from>.json and <adsh>.xml files");
      s.opt({"--write"}).store_true().help("save seed/formd.json");
      common(s); }
    { Parser& s = p.sub("import"); s.help_line = "add investors from a CSV or JSON file";
      s.pos("file");
      common(s); }
    { Parser& s = p.sub("add"); s.help_line = "add one investor";
      s.opt({"--name"}).required();
      s.opt({"--firm"}); s.opt({"--role"}); s.opt({"--email"});
      s.opt({"--stage"}).dest("stage_list").help("e.g. \"pre-seed; seed\"");
      s.opt({"--check"}).help("their check range, e.g. 250K-1M");
      s.opt({"--sectors"}).help("e.g. \"pet care; marketplaces\"");
      s.opt({"--geo"});
      s.opt({"--last-deal"}).help("their latest relevant deal, in a few words");
      s.opt({"--last-deal-date"}).help("YYYY-MM-DD");
      s.opt({"--source"}).append().help("a public URL (repeat for more)");
      s.opt({"--warm-intro"}).store_true();
      s.opt({"--personal-line"}).help("the first line of the email, from your research");
      s.opt({"--notes"});
      common(s); }
    { Parser& s = p.sub("rank"); s.help_line = "fit 0-100 with reasons";
      s.opt({"--stage"}); s.opt({"--sectors"}); s.opt({"--geo"});
      s.opt({"--ask"}).help("your round, e.g. 1.5M");
      s.opt({"--check"}).help("the check you want from one investor, e.g. 75K-750K");
      s.opt({"--reasons"}).integer().def(10).help("print the reasons for the top N (default 10)");
      s.opt({"--write"}).store_true();
      common(s); }
    { Parser& s = p.sub("show"); s.help_line = "the ranked list, or one investor";
      s.pos("id").nargs("?");
      s.opt({"--top"}).integer();
      common(s); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "formd") return cmd_formd(a);
        if (cmd == "import") return cmd_import(a);
        if (cmd == "add") return cmd_add(a);
        if (cmd == "rank") return cmd_rank(a);
        return cmd_show(a);
    } catch (const InvError& e) { eprint(std::string("investors.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-investors/investors", main);
}  // namespace packtools::seed_investors
