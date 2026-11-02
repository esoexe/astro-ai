// seed-scout/scout: finds the weak points of your competitors in their own App Store reviews (iTunes Search, Lookup
// and the customer-reviews RSS; free, public, no keys). A C++ port of seed-skill/seed-scout/scout.py (MIT, Jake
// Schincariol); same output, files and exit codes: 0 done, 1 nothing found or refused, 2 bad input or a network error.
// --offline DIR reads saved responses (search.json, lookup-<id>.json, rss-<id>-<page>.json) instead of the network.
#include <chrono>
#include <thread>
namespace packtools::seed_scout {
using namespace pyrt;
using namespace packtools::seed;

struct ScoutError : std::runtime_error {
    int code;
    explicit ScoutError(const std::string& m, int c = 2) : std::runtime_error(m), code(c) {}
};

inline const std::string UA = "seed-skill/1.0 (+https://github.com/Jakeschincariol/seed-skill)";
inline constexpr long MAX_PAGES = 10;
inline constexpr double PAUSE = 1.0;
inline constexpr long TIMEOUT = 20;
inline constexpr double MIN_SHARE = 5.0;   // a weak point needs at least 5% of the app's low reviews
inline constexpr long long EXAMPLES = 3;
inline const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

struct Theme { std::string key; std::string label; std::vector<std::string> pats; };
inline const std::vector<Theme>& THEMES() {
    static const std::vector<Theme> t = {
        {"fees", "fees and pricing", {
            R"(\bfees?\b)", R"(\bpric(?:e|es|ed|ey|ing)\b)", R"(\boverpriced\b)", R"(\bexpensive\b)", R"(\bovercharg)",
            R"(\bcosts?\b|\bcostly\b)", R"(\d\s*%|\bpercent\b)", R"(\bcommission\b)", R"(\bgreed(?:y)?\b)",
            R"(\b(?:money|cash) (?:grab|hungry)\b)", R"(\bafford)", R"(\bcheaper\b)", R"(\bmark ?up\b)", R"(\bsurcharge)"}},
        {"cancellations", "cancellations and no-shows", {
            R"(\bcancel)", R"(\bno[\s-]?shows?\b)", R"(\bnever show(?:ed)? up\b)", R"x(\bdid(?:n't| not) show up\b)x",
            R"(\bstood (?:me|us) up\b)"}},
        {"support", "customer support", {
            R"(\bcustomer (?:service|support|care)\b)", R"(\bsupport\b)", R"(\bno (?:response|reply|answer)\b)",
            R"(\bnever (?:respond|replied|answer|got back|heard back))", R"(\bunhelpful\b)", R"(\bchat ?bots?\b)",
            R"x(\bcan'?t (?:reach|contact|get a hold)\b)x", R"(\bhelp center\b)", R"(\bon hold\b)"}},
        {"bugs", "bugs and app quality", {
            R"(\bcrash)", R"(\bbugs?\b|\bbuggy\b)", R"(\bglitch)", R"(\bfreez|\bfroze)", R"x(\bwon'?t (?:load|open)\b)x",
            R"(\berror\b)", R"(\bslow\b)", R"(\blog ?in\b|\bsign ?in\b)", R"(\b(?:new|latest|recent|last|this|the) update\b)",
            R"x(\bnot working\b|\bdoesn'?t work\b|\bdoes not work\b)x"}},
        {"trust", "trust and safety", {
            R"(\bunsafe\b|\bsafety\b)", R"(\blost (?:my |our |the )?(?:dog|cat|pet)\b)", R"(\binjur)",
            R"(\bdied\b|\bdeath\b|\bkilled\b)", R"(\bst(?:ole|olen|eal))", R"(\bbackground checks?\b)", R"(\bscam)",
            R"(\bfraud)", R"(\btrust)", R"(\bdistrust)"}},
        {"payments", "payments and refunds", {
            R"(\brefund)", R"(\bpayments?\b)", R"(\bpayouts?\b)", R"(\bmoney back\b)", R"(\bcharged\b)", R"(\bchargeback)",
            R"(\bdispute)"}},
        {"availability", "availability", {
            R"(\b(?:nobody|no one) (?:is )?(?:available|accepted|applied)\b)", R"(\bunavailable\b|\bnot available\b)",
            R"(\bin my area\b)", R"x(\bcan'?t find (?:a |any |anyone))x", R"(\bno (?:walkers?|sitters?|drivers?|providers?|pros)\b)"}},
    };
    return t;
}
struct Rule { std::string key; Val label; std::vector<std::string> pats; };   // label: the rules file may give any JSON value
using Rules = std::vector<Rule>;

// ---------------------------------------------------------------- small helpers
inline const Val& at(const Val& o, const std::string& k) {   // o[k]
    if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    const Val* v = o.d->get(k);
    if (!v) throw PyErr("KeyError", str_repr(k));
    return *v;
}
inline Val oget(const Val& o, const std::string& k) {   // o.get(k)
    if (o.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(o) + "' object has no attribute 'get'");
    return get(o, k);
}
inline Val either(const Val& a, const Val& b) { return truthy(a) ? a : b; }   // a or b
inline std::optional<BigInt> py_int_str(const std::string& s) {   // int(str), None where Python raises ValueError
    try { return BigInt::parse(strip(ascii_digits(s))); } catch (const std::invalid_argument&) { return std::nullopt; }
}
inline std::string int_str(const BigInt& b) { const std::string s = b.str(); return s == "-0" ? "0" : s; }
inline BigInt py_int_val(const Val& v) {   // int(v)
    if (v.t == Val::STR) {
        const auto b = py_int_str(v.s);
        if (!b) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(v.s));
        return *b;
    }
    if (v.t == Val::FLOAT && std::isnan(v.f)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (v.t == Val::FLOAT && std::isinf(v.f)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    return int_of(v);
}
inline std::string pct_d(const Val& v) {   // "%d" % v
    if (v.t == Val::INT || v.t == Val::BOOL || v.t == Val::FLOAT) return int_str(py_int_val(v));
    throw PyErr("TypeError", std::string("%d format: a real number is required, not ") + type_name(v));
}
inline double num_f(const Val& v) {   // "%.1f" % v
    if (v.t == Val::INT || v.t == Val::BOOL || v.t == Val::FLOAT) return to_float(v);
    throw PyErr("TypeError", std::string("must be real number, not ") + type_name(v));
}
inline std::string str_of(const Val& v, const char* what = "object") {   // the value must be a str (slicing, `in`, .lower())
    if (v.t != Val::STR) throw PyErr("TypeError", std::string("'") + type_name(v) + "' " + what);
    return v.s;
}
inline bool num_t(const Val& v) { return v.t == Val::INT || v.t == Val::FLOAT || v.t == Val::BOOL; }
inline bool py_lt(const Val& a, const Val& b) {   // a < b for the values sorted here
    if (a.t == Val::STR && b.t == Val::STR) return a.s < b.s;   // UTF-8 byte order = code point order
    if (num_t(a) && num_t(b)) return num_cmp(a, b) < 0;
    throw PyErr("TypeError", std::string("'<' not supported between instances of '") + type_name(a) + "' and '" + type_name(b) + "'");
}
inline bool py_eq(const Val& a, const Val& b) {   // a == b for JSON values
    if (a.t == Val::STR || b.t == Val::STR) return a.t == b.t && a.s == b.s;
    if (a.t == Val::NONE || b.t == Val::NONE) return a.t == b.t;
    if (num_t(a) && num_t(b)) { if ((a.t == Val::FLOAT && std::isnan(a.f)) || (b.t == Val::FLOAT && std::isnan(b.f))) return false; return num_cmp(a, b) == 0; }
    return a.t == b.t && dumps(a) == dumps(b);
}
inline bool same_key(const Val& a, const Val& b) {   // dict-key / set equality (1 == 1.0 == True)
    if (a.t == Val::LIST || a.t == Val::DICT || b.t == Val::LIST || b.t == Val::DICT) throw PyErr("TypeError", std::string("unhashable type: '") + type_name(a.t == Val::LIST || a.t == Val::DICT ? a : b) + "'");
    if (a.t == Val::NONE || b.t == Val::NONE) return a.t == b.t;
    if (a.t == Val::STR || b.t == Val::STR) return a.t == b.t && a.s == b.s;
    if ((a.t == Val::FLOAT && std::isnan(a.f)) || (b.t == Val::FLOAT && std::isnan(b.f))) return false;
    return num_cmp(a, b) == 0;
}
template <class T> std::vector<T> head(const std::vector<T>& v, long long n) {   // v[:n]
    const long long sz = static_cast<long long>(v.size());
    long long end = n < 0 ? std::max(0LL, sz + n) : std::min(sz, n);
    return std::vector<T>(v.begin(), v.begin() + end);
}

inline std::string iso_of(long long day) { return iso_day(day); }
inline long long today_of(const Val& a) {   // datetime.strptime(text, "%Y-%m-%d"): one-digit months and days are fine
    const Val tv = get(a, "today");
    if (!truthy(tv)) return today_days();
    const std::string text = pystr(tv);
    const std::string bad = "--today wants YYYY-MM-DD, not " + str_repr(text);
    const re::Match m = re::match(R"(^(\d\d\d\d)-(1[0-2]|0[1-9]|[1-9])-(3[01]|[12]\d|0[1-9]|[1-9]| [1-9])$)", text, re::I);
    if (!m || text.back() == '\n') throw ScoutError(bad);
    const long long y = to_ll(m.group(1));
    const int mo = static_cast<int>(to_ll(m.group(2))), d = static_cast<int>(to_ll(strip(m.group(3))));
    if (!valid_date(y, mo, d)) throw ScoutError(bad);
    return make_day(y, mo, d);
}
inline std::string nice_date(const Val& iso) {   // 2026-10-14 -> Oct 14 2026
    const std::string s = str_of(iso, "object is not subscriptable");
    const auto parts = split(cpslice(s, 0, 10), "-");
    std::vector<BigInt> n;
    for (const auto& x : parts) { const auto b = py_int_str(x); if (!b) return s; n.push_back(*b); }
    if (n.size() != 3) return s;
    const std::string ms = int_str(n[1]);
    if (ms.size() > 4) return s;   // far outside the list: IndexError
    long long idx = std::stoll(ms) - 1;
    if (idx < -12 || idx > 11) return s;
    if (idx < 0) idx += 12;
    return std::string(MONTHS[idx]) + " " + int_str(n[2]) + " " + int_str(n[0]);
}
inline double pct(long long n, long long m) { return !m ? 0.0 : round_n(100.0 * static_cast<double>(n) / static_cast<double>(m), 1); }
inline Val rating1(const Val& x) {   // App Store average to one decimal, half up: 4.88527 -> 4.9
    if (x.is_none()) return Val();
    const double v = to_float(x) * 10 + 0.5;
    if (std::isnan(v)) throw PyErr("ValueError", "cannot convert float NaN to integer");
    if (std::isinf(v)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
    return Val::flt(std::trunc(v) / 10);
}
inline std::string short_name(const std::string& name) {   // 'Rover – Pet Sitters' -> 'Rover'
    for (const char* sep : {" \xE2\x80\x93 ", " - ", ": ", " | "})
        if (name.find(sep) != std::string::npos) return strip(split(name, sep)[0]);
    return strip(name);
}
inline Val read_json(const std::string& path, const std::string& what = "file") {
    std::string text;
    try { text = read_text(path); } catch (const OSError&) { throw ScoutError("no " + what + " at " + path, 1); }
    catch (const PyErr& e) { if (e.type == "UnicodeDecodeError") throw ScoutError(path + " is not valid JSON: " + e.what()); throw; }
    try { return loads(text); }
    catch (const PyErr& e) {
        if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError" || e.type == "UnicodeDecodeError") throw ScoutError(path + " is not valid JSON: " + e.what());
        throw;
    }
}
inline DumpOpts json_opts() { DumpOpts o; o.indent = 2; o.ensure_ascii = false; return o; }
inline void write_json(const std::string& path, const Val& doc) {
    const std::string d = dirname(path);
    if (!d.empty()) makedirs(d);
    write_text(path, dumps(doc, json_opts()) + "\n");
}
inline std::string quote_plus(const std::string& s) {   // urllib.parse.quote_plus, safe=""
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') o += static_cast<char>(c);
        else if (c == ' ') o += '+';
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

// ---------------------------------------------------------------- fetching
struct Source {   // live network, or a folder of saved responses (--offline)
    std::string offline, country;
    double pause = PAUSE;
    std::optional<std::chrono::steady_clock::time_point> last;
    Source(std::string off, const std::string& c) : offline(std::move(off)), country(lower(c)) {}
    static std::string decode(const std::string& raw, bool live) {
        std::string b = raw;
        if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0x1f && static_cast<unsigned char>(b[1]) == 0x8b) {
            if (!live) b = pyrt::zip::gunzip(b);
            else { try { b = pyrt::zip::gunzip(b); } catch (const std::exception& e) { throw ScoutError(std::string("network error: ") + e.what()); } }
        }
        return decode_utf8_replace(b);
    }
    std::optional<std::string> get_url(const std::string& url) {
        const auto now = std::chrono::steady_clock::now();
        if (last) {
            const double gap = std::chrono::duration<double>(now - *last).count();
            if (gap < pause) std::this_thread::sleep_for(std::chrono::duration<double>(pause - gap));
        }
        last = std::chrono::steady_clock::now();
        const HttpResp r = packtools::http_get(url, {"User-Agent: " + UA, "Accept: application/json", "Accept-Encoding: gzip"}, TIMEOUT);
        if (!r.error.empty()) throw ScoutError("could not reach the App Store: " + r.error);
        if (r.status < 200 || r.status >= 300) {
            if (r.status == 400 || r.status == 404) return std::nullopt;   // past the last page, or no such app
            throw ScoutError("the App Store answered HTTP " + std::to_string(r.status) + " for " + split(url, "?")[0]);
        }
        return decode(r.body, true);
    }
    std::optional<std::string> file(const std::vector<std::string>& names) const {
        for (const auto& n : names) {
            const std::string p = join_path(offline, n);
            if (is_file(p)) return decode(read_bytes(p), false);
        }
        return std::nullopt;
    }
    static Val json(const std::optional<std::string>& text, const std::string& what) {
        if (!text) return Val();
        try { return loads(*text); }
        catch (const PyErr& e) {
            if (e.type == "json.decoder.JSONDecodeError" || e.type == "ValueError") throw ScoutError("the App Store sent something that is not JSON for " + what);
            throw;
        }
    }
    Val search(const std::string& term, long long limit) {
        if (!offline.empty()) {
            const std::string path = is_file(offline) ? offline : join_path(offline, "search.json");
            if (!is_file(path)) throw ScoutError("offline: no " + path, 1);
            return json(decode(read_bytes(path), false), "the search");
        }
        const std::string q = "term=" + quote_plus(term) + "&country=" + quote_plus(country) + "&media=software&entity=software&limit=" + std::to_string(limit);
        const Val d = json(get_url("https://itunes.apple.com/search?" + q), "the search");
        return truthy(d) ? d : obj({{"resultCount", iv(0)}, {"results", Val::list()}});
    }
    Val lookup(const std::string& app_id) {
        if (!offline.empty()) return json(file({"lookup-" + app_id + ".json"}), "lookup " + app_id);
        return json(get_url("https://itunes.apple.com/lookup?id=" + app_id + "&country=" + country), "lookup " + app_id);
    }
    std::string rss(long n, const std::string& app_id) const { return "https://itunes.apple.com/" + country + "/rss/customerreviews/page=" + std::to_string(n) + "/id=" + app_id + "/sortby=mostrecent/json"; }
    Val page(const std::string& app_id, long n) {
        const std::string what = "reviews page " + std::to_string(n);
        if (!offline.empty()) return json(file({"rss-" + app_id + "-" + std::to_string(n) + ".json", "rev-" + app_id + "-" + std::to_string(n) + ".json"}), what);
        return json(get_url(rss(n, app_id)), what);
    }
};

inline Val app_row(const Val& r) {   // one iTunes Search/Lookup result -> the fields we keep
    const Val id = int_val(py_int_val(oget(r, "trackId")));
    const Val tn = either(oget(r, "trackName"), sv(""));
    const std::string name = str_of(tn, "object is not a string");
    const Val url = either(oget(r, "trackViewUrl"), sv(""));
    return obj({{"app_id", id}, {"app_name", tn}, {"name", sv(short_name(name))}, {"seller", either(oget(r, "sellerName"), oget(r, "artistName"))},
                {"rating", rating1(oget(r, "averageUserRating"))}, {"ratings", oget(r, "userRatingCount")}, {"genre", oget(r, "primaryGenreName")},
                {"price", either(oget(r, "formattedPrice"), oget(r, "price"))}, {"url", sv(split(str_of(url, "object has no attribute 'split'"), "?")[0])}});
}
inline List find_apps(Source& src, const std::string& term, long long limit = 10) {
    const Val doc = src.search(term, limit);
    List out;
    const Val res = truthy(doc) ? either(oget(doc, "results"), Val::list()) : Val::list();
    for (const Val& r : items_of(res)) {
        if (!truthy(oget(r, "trackId"))) continue;
        const Val kind = get(r, "kind"), wrapper = get(r, "wrapperType");
        if (kind.is_none() || (kind.t == Val::STR && kind.s == "software") || (wrapper.t == Val::STR && wrapper.s == "software")) out.push_back(app_row(r));
    }
    return head(out, limit);
}
inline std::pair<std::string, Val> resolve(Source& src, const std::string& ref0) {   // an app id, or a name: most ratings first
    const std::string ref = strip(ref0);
    if (re::match(R"(^(?:id)?\d{6,12}$)", ref)) {
        std::size_t k = 0; while (k < ref.size() && (ref[k] == 'i' || ref[k] == 'd')) ++k;
        return {int_str(BigInt::parse(ascii_digits(ref.substr(k)))), Val()};
    }
    const List apps = find_apps(src, ref, 10);
    if (apps.empty()) throw ScoutError("no App Store app found for " + str_repr(ref), 1);
    const std::string low = lower(ref);
    List named;
    for (const Val& x : apps) if (lower(get(x, "app_name").s).find(low) != std::string::npos) named.push_back(x);
    if (named.empty()) {
        std::vector<std::string> ids;
        for (const Val& x : head(apps, 3)) ids.push_back(pystr(get(x, "app_name")) + " (" + pystr(get(x, "app_id")) + ")");
        throw ScoutError("no app named " + str_repr(ref) + " in the search; use its id: " + join(ids, ", "), 1);
    }
    Val pick = named[0]; Val best = either(get(pick, "ratings"), iv(0));
    for (std::size_t k = 1; k < named.size(); ++k) {
        const Val r = either(get(named[k], "ratings"), iv(0));
        if (py_lt(best, r)) { best = r; pick = named[k]; }
    }
    return {pystr(get(pick, "app_id")), pick};
}
inline Val lab(const Val& e, const std::string& k) {   // ((e.get(k) or {}).get("label") or "")
    const Val x = either(get(e, k), Val::dict());
    return either(oget(x, "label"), sv(""));
}
inline List parse_feed(const Val& doc) {   // the customer-reviews RSS (JSON flavour) -> rows
    const Val feed = either(truthy(doc) ? oget(doc, "feed") : Val(), Val::dict());
    Val entries = either(oget(feed, "entry"), Val::list());
    if (entries.t == Val::DICT) { Val l = Val::list(); l.l->push_back(entries); entries = l; }
    List out;
    for (const Val& e : items_of(entries)) {
        if (e.t != Val::DICT || !e.d->has("im:rating")) continue;   // older feeds put the app itself first
        long long stars;
        try {
            const BigInt b = py_int_val(lab(e, "im:rating"));
            stars = b.to_ll();
        } catch (const PyErr& x) { if (x.type == "ValueError") continue; throw; }
        const Val up = lab(e, "updated");
        const std::string updated = str_of(up, "object is not subscriptable");
        const Val vs = lab(e, "im:voteSum");
        const std::string vss = str_of(vs, "object has no attribute 'lstrip'");
        std::size_t k = 0; while (k < vss.size() && vss[k] == '-') ++k;
        Val votes = iv(0);
        if (isdigit_str(vss.substr(k))) votes = int_val(py_int_val(either(vs, iv(0))));
        out.push_back(obj({{"id", lab(e, "id")}, {"date", sv(cpslice(updated, 0, 10))}, {"updated", up}, {"stars", iv(stars)},
                           {"title", lab(e, "title")}, {"text", lab(e, "content")}, {"version", lab(e, "im:version")}, {"votes", votes}}));
    }
    return out;
}
inline std::pair<List, long> fetch_reviews(Source& src, const std::string& app_id, long long pages = MAX_PAGES) {
    if (pages < 1 || pages > MAX_PAGES) throw ScoutError("--pages is 1 to 10 (the feed stops at 10 pages of 50: the 500 most recent)");
    List seen, out; long read = 0;
    for (long n = 1; n <= pages; ++n) {
        const List rows = parse_feed(src.page(app_id, n));
        if (rows.empty()) break;
        read = n;
        for (const Val& r : rows) {
            const Val id = get(r, "id");
            bool dup = false;
            if (truthy(id)) for (const Val& s : seen) if (same_key(s, id)) { dup = true; break; }
            if (dup) continue;
            seen.push_back(id);
            out.push_back(r);
        }
    }
    return {out, read};
}
inline std::pair<Val, bool> load_app(Source& src, const std::string& ref, long long pages, const std::string& seed_dir, long long today) {
    const auto [app_id, picked] = resolve(src, ref);
    if (!seed_dir.empty()) {
        const std::string saved = join_path(join_path(seed_dir, "reviews"), app_id + ".json");
        if (is_file(saved) && src.offline.empty()) return {read_json(saved, "reviews"), false};
    }
    Val info = picked;
    const Val look = src.lookup(app_id);
    if (truthy(look) && truthy(oget(look, "results"))) {
        const Val res = at(look, "results");
        if (res.t != Val::LIST) throw PyErr("KeyError", "0");
        info = app_row(res.l->at(0));
    }
    const auto [reviews, read] = fetch_reviews(src, app_id, pages);
    if (reviews.empty()) throw ScoutError("no reviews for app " + app_id + " (a wrong id, or an app with no reviews in this country)", 1);
    if (!truthy(info)) info = obj({{"app_id", sv(app_id)}, {"app_name", sv(app_id)}, {"name", sv(app_id)}, {"rating", Val()}, {"ratings", Val()}});
    Val rv = Val::list(); *rv.l = reviews;
    const Val doc = obj({{"created_by", sv("seed-scout")}, {"app_id", int_val(BigInt::parse(app_id))}, {"name", at(info, "name")}, {"app_name", at(info, "app_name")},
                         {"seller", get(info, "seller")}, {"rating", get(info, "rating")}, {"ratings", get(info, "ratings")}, {"country", sv(src.country)},
                         {"fetched", sv(iso_of(today))}, {"pages", iv(read)},
                         {"source", sv(src.rss(1, app_id) + (read > 1 ? " (pages 1-" + std::to_string(read) + ")" : ""))}, {"reviews", rv}});
    return {doc, true};
}

// ---------------------------------------------------------------- themes
inline Rules load_rules(const std::string& path) {
    Rules rules;
    for (const auto& t : THEMES()) rules.push_back({t.key, sv(t.label), t.pats});
    if (path.empty()) return rules;
    const Val doc = read_json(path, "rules file");
    if (doc.t != Val::DICT || doc.d->items.empty()) throw ScoutError(path + " wants {\"key\": {\"label\": \"...\", \"patterns\": [\"regex\", ...]}}");
    std::vector<std::string> keys; for (const auto& r : rules) keys.push_back(r.key);
    for (const auto& [k, v] : doc.d->items) {
        if (v.t != Val::DICT || !truthy(get(v, "patterns"))) throw ScoutError("rules " + str_repr(k) + " needs a list of patterns");
        std::vector<std::string> pats;
        for (const Val& p : items_of(get(v, "patterns"))) {
            if (p.t != Val::STR) throw PyErr("TypeError", "first argument must be string or compiled pattern");
            try { (void)re::compile(p.s, re::I); }
            catch (const PyErr& e) { if (e.type == "re.error") throw ScoutError("rules " + str_repr(k) + " has a bad pattern: " + e.what()); throw; }
            pats.push_back(p.s);
        }
        Rule row{k, either(get(v, "label"), sv(k)), pats};
        const auto it = std::find(keys.begin(), keys.end(), k);
        if (it != keys.end()) rules[static_cast<std::size_t>(it - keys.begin())] = row;
        else rules.push_back(row);
    }
    return rules;
}
inline std::string norm_text(const Val& r) {
    const std::string t = pystr(either(oget(r, "title"), sv(""))) + " " + pystr(either(oget(r, "text"), sv("")));
    return replace_all(replace_all(t, "\xE2\x80\x99", "'"), "\xE2\x80\x98", "'");
}
struct Stats {
    std::vector<long long> counts; long long low = 0, read = 0, unthemed = 0;
    std::vector<List> matches;
    Val app_id, name, app_name, rating, ratings, source, first, last, fetched;
};
inline Stats theme_counts(const Val& reviews, const Rules& rules, long long max_stars) {
    Stats st; st.counts.assign(rules.size(), 0); st.matches.assign(rules.size(), List());
    const List all = items_of(reviews);
    List low;
    for (const Val& r : all) {
        const Val s = oget(r, "stars");
        if (!s.is_none() && cmp(Decimal::from_int(py_int_val(at(r, "stars"))), Decimal(max_stars)) <= 0) low.push_back(r);
    }
    for (const Val& r : low) {
        const std::string t = norm_text(r);
        bool hit = false;
        for (std::size_t k = 0; k < rules.size(); ++k) {
            bool any = false;
            for (const auto& p : rules[k].pats) if (re::compile(p, re::I).search(t)) { any = true; break; }
            if (any) { st.counts[k]++; st.matches[k].push_back(r); hit = true; }
        }
        if (!hit) st.unthemed++;
    }
    st.low = static_cast<long long>(low.size()); st.read = static_cast<long long>(all.size());
    return st;
}
inline std::optional<std::size_t> most_mentioned(const std::vector<long long>& counts) {
    std::optional<std::size_t> best;
    for (std::size_t k = 0; k < counts.size(); ++k) if (!best || counts[k] > counts[*best]) best = k;
    return best && counts[*best] > 0 ? best : std::nullopt;
}
inline Val examples_of(const List& rows0, long long n = EXAMPLES) {
    List rows = rows0;
    auto key = [](const Val& r) { return std::make_pair(either(either(oget(r, "updated"), oget(r, "date")), sv("")), either(oget(r, "id"), sv(""))); };
    std::stable_sort(rows.begin(), rows.end(), [&](const Val& a, const Val& b) {   // reverse=True, stable
        const auto ka = key(a), kb = key(b);
        if (py_lt(kb.first, ka.first)) return true;
        if (py_lt(ka.first, kb.first)) return false;
        return py_lt(kb.second, ka.second);
    });
    Val out = Val::list();
    for (const Val& r : head(rows, n))
        out.l->push_back(obj({{"title", either(oget(r, "title"), sv(""))}, {"date", oget(r, "date")}, {"stars", int_val(py_int_val(at(r, "stars")))}}));
    return out;
}
struct Weak { std::optional<std::size_t> key; std::string rule; std::optional<double> gap; };
inline std::vector<Weak> weak_points(const std::vector<Stats>& stats, const std::string& by = "gap") {
    std::vector<Weak> out;
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const Stats& s = stats[i];
        const auto top = most_mentioned(s.counts);
        if (by == "count" || stats.size() < 2 || !s.low) { out.push_back({top, "most mentioned", std::nullopt}); continue; }
        long long others_low = 0; for (std::size_t j = 0; j < stats.size(); ++j) if (j != i) others_low += stats[j].low;
        std::optional<std::size_t> best; double best_gap = 0;
        for (std::size_t k = 0; k < s.counts.size(); ++k) {
            const long long n = s.counts[k];
            if (n < 2 || pct(n, s.low) < MIN_SHARE) continue;
            long long other = 0; for (std::size_t j = 0; j < stats.size(); ++j) if (j != i) other += stats[j].counts[k];
            const double gap = 100.0 * static_cast<double>(n) / static_cast<double>(s.low) - (others_low ? 100.0 * static_cast<double>(other) / static_cast<double>(others_low) : 0.0);
            if (!best || gap > best_gap + 1e-9 || (std::fabs(gap - best_gap) <= 1e-9 && n > s.counts[*best])) { best = k; best_gap = gap; }
        }
        if (!best || best_gap <= 0) out.push_back({top, "most mentioned", std::nullopt});
        else out.push_back({best, "sets it apart", round_n(best_gap, 1)});
    }
    return out;
}
inline Val counts_val(const Stats& s, const Rules& rules) { Val d = Val::dict(); for (std::size_t k = 0; k < rules.size(); ++k) d.d->set(rules[k].key, iv(s.counts[k])); return d; }
inline Val key_val(const std::optional<std::size_t>& k, const Rules& rules) { return k ? sv(rules[*k].key) : Val(); }
inline Stats app_summary(const Val& doc, const Rules& rules, long long max_stars) {
    const Val reviews = at(doc, "reviews");
    Stats st = theme_counts(reviews, rules, max_stars);
    List dates;
    for (const Val& r : items_of(reviews)) if (truthy(oget(r, "date"))) dates.push_back(at(r, "date"));
    std::stable_sort(dates.begin(), dates.end(), py_lt);
    st.app_id = at(doc, "app_id"); st.name = oget(doc, "name"); st.app_name = oget(doc, "app_name"); st.rating = oget(doc, "rating");
    st.ratings = oget(doc, "ratings"); st.source = oget(doc, "source");
    st.first = dates.empty() ? Val() : dates.front(); st.last = dates.empty() ? Val() : dates.back(); st.fetched = oget(doc, "fetched");
    return st;
}
inline std::string stars_word(long long max_stars) { return max_stars == 1 ? "1-star" : "1-" + std::to_string(max_stars) + " star"; }
inline Val build_competitors(const List& docs, const Rules& rules, long long max_stars, const std::string& by, long long today) {
    std::vector<Stats> stats; for (const Val& d : docs) stats.push_back(app_summary(d, rules, max_stars));
    const auto weak = weak_points(stats, by);
    Val apps = Val::list();
    for (std::size_t i = 0; i < stats.size(); ++i) {
        const Stats& s = stats[i]; const Weak& w = weak[i];
        const bool has = w.key.has_value(); const std::size_t k = has ? *w.key : 0;
        apps.l->push_back(obj({{"name", s.name}, {"app_name", s.app_name}, {"app_id", s.app_id}, {"rating", s.rating}, {"ratings", s.ratings},
            {"reviews_read", iv(s.read)}, {"low_reviews", iv(s.low)}, {"themes", counts_val(s, rules)}, {"top_theme", key_val(w.key, rules)},
            {"weak_point", has ? rules[k].label : Val()}, {"weak_point_count", iv(has ? s.counts[k] : 0)},
            {"weak_point_share", Val::flt(has ? pct(s.counts[k], s.low) : 0.0)}, {"weak_point_rule", sv(w.rule)},
            {"gap_points", w.gap ? Val::flt(*w.gap) : Val()}, {"most_mentioned", key_val(most_mentioned(s.counts), rules)},
            {"weak_point_line", sv(has ? std::to_string(s.counts[k]) + " of " + std::to_string(s.low) + " recent " + stars_word(max_stars) + " reviews mention " + pystr(rules[k].label) : "no theme matched")},
            {"examples", has ? examples_of(s.matches[k]) : Val::list()}, {"reviews_from", s.first}, {"reviews_to", s.last}, {"fetched", s.fetched}, {"source", s.source}}));
    }
    Val labels = Val::dict(); for (const auto& r : rules) labels.d->set(r.key, r.label);
    return obj({{"created_by", sv("seed-scout")}, {"generated", sv(iso_of(today))}, {"low_stars", iv(max_stars)},
                {"weak_point_by", sv(by == "count" || docs.size() < 2 ? "count" : "gap")}, {"theme_labels", labels}, {"apps", apps}});
}

// ---------------------------------------------------------------- printing
inline std::string fmt_int(const Val& n) {   // "{:,}".format(n) if isinstance(n, int) else "?"
    if (n.t == Val::INT) return commas(big_of(n));
    if (n.t == Val::BOOL) return n.b ? "1" : "0";
    return "?";
}
inline std::string star_s(const Val& stars) { return truthy(stars) && num_t(stars) && num_cmp(stars, iv(1)) == 0 ? "" : "s"; }
inline void print_themes(const Stats& s, const Rules& rules, long long max_stars, long long n_examples) {
    const Val name = either(s.app_name, s.name);
    print(pystr(name) + " (" + pystr(s.app_id) + "): " + std::to_string(s.low) + " of the " + std::to_string(s.read) + " most recent reviews are " +
          (max_stars == 1 ? std::string("1 star") : "1 or " + std::to_string(max_stars) + " stars") + " (" + nice_date(either(s.first, sv(""))) + " to " +
          nice_date(either(s.last, sv(""))) + ")");
    if (!s.rating.is_none()) print("  App Store: " + fmt_f(num_f(s.rating), 1) + " stars from " + fmt_int(s.ratings) + " ratings");
    std::vector<std::size_t> ranked; for (std::size_t k = 0; k < rules.size(); ++k) ranked.push_back(k);
    std::stable_sort(ranked.begin(), ranked.end(), [&](std::size_t a, std::size_t b) { return s.counts[a] > s.counts[b]; });
    print("  " + ljust("theme", 28) + " " + rjust("low reviews", 14) + " " + rjust("share", 7));
    auto row = [&](const std::string& label, long long n) {
        print("  " + ljust(label, 28) + " " + rjust(std::to_string(n), 7) + " of " + ljust(std::to_string(s.low), 4) + " " + rjust(fmt_f(pct(n, s.low), 1), 6) + "%");
    };
    for (const std::size_t k : ranked) row(pystr(rules[k].label), s.counts[k]);
    row("(no theme)", s.unthemed);
    if (const auto top = most_mentioned(s.counts)) {
        const std::string l = pystr(rules[*top].label);
        print("Most mentioned: " + l + ", " + std::to_string(s.counts[*top]) + " of " + std::to_string(s.low) + " low reviews.");
        print("Examples (" + l + ", newest first):");
        const Val ex = examples_of(s.matches[*top], n_examples);
        for (const Val& e : *ex.l)
            print("  " + ljust(nice_date(get(e, "date")), 12) + " " + pct_d(get(e, "stars")) + " star" + star_s(get(e, "stars")) + "  \"" + pystr(get(e, "title")) + "\"");
    }
    print("A review can mention several themes. Read the examples before you quote a theme.");
}
inline void print_compare(const Val& doc) {
    const Val ls = at(doc, "low_stars");
    print("Competitors: their most recent App Store reviews, " + (same_key(ls, iv(1)) ? std::string("1-star") : "1-" + pct_d(ls) + " star") + " only");
    print("  " + ljust("app", 14) + " " + rjust("rating", 6) + " " + rjust("ratings", 10) + " " + rjust("low/read", 9) + "   " + ljust("weak point", 27) + " evidence");
    const List apps = items_of(at(doc, "apps"));
    for (const Val& a : apps) {
        const Val r = at(a, "rating");
        print("  " + ljust(cpslice(str_of(at(a, "name"), "object is not subscriptable"), 0, 14), 14) + " " + rjust(r.is_none() ? "?" : fmt_f(num_f(r), 1), 6) + " " +
              rjust(fmt_int(at(a, "ratings")), 10) + " " + rjust(pct_d(at(a, "low_reviews")) + "/" + pct_d(at(a, "reviews_read")), 9) + "   " +
              ljust(pystr(either(at(a, "weak_point"), sv("none"))), 27) + " " + pct_d(at(a, "weak_point_count")) + " of " + pct_d(at(a, "low_reviews")) +
              " (" + fmt_f(num_f(at(a, "weak_point_share")), 1) + "%)");
    }
    const Val by = at(doc, "weak_point_by");
    if (by.t == Val::STR && by.s == "gap") print("Weak point = the complaint that sets each app apart: its share of low reviews minus the others'.");
    else print("Weak point = the most mentioned complaint.");
    for (const Val& a : apps) {
        const Val mm = oget(a, "most_mentioned");
        if (truthy(mm) && !py_eq(mm, at(a, "top_theme"))) {
            const Val labels = at(doc, "theme_labels");
            const std::string mk = str_of(mm, "object is not a str key");
            const Val l = oget(labels, mk).is_none() && !labels.d->has(mk) ? mm : get(labels, mk);
            print("  " + pystr(at(a, "name")) + ": the most mentioned is " + pystr(l) + " (" + pct_d(at(at(a, "themes"), mk)) + " of " + pct_d(at(a, "low_reviews")) +
                  "), but the other apps get it about as often.");
        }
        for (const Val& ex : items_of(at(a, "examples")))
            print("  " + cpslice(str_of(at(a, "name"), "object is not subscriptable"), 0, 10) + "  " + ljust(nice_date(at(ex, "date")), 12) + " " + pct_d(at(ex, "stars")) +
                  " star" + star_s(at(ex, "stars")) + "  \"" + pystr(at(ex, "title")) + "\"");
    }
}

// ---------------------------------------------------------------- commands
inline std::string seed_dir(const Val& a) { const std::string d = S(a, "dir"); return d.empty() ? "seed" : d; }
inline Source source_of(const Val& a) { return Source(S(a, "offline"), S(a, "country")); }
inline long long I(const Val& a, const std::string& k) { return py_int_val(get(a, k)).to_ll(); }

inline int cmd_find(const Val& a) {
    Source src = source_of(a);
    const std::string term = S(a, "term");
    const List apps = find_apps(src, term, I(a, "limit"));
    if (B(a, "json")) {
        Val l = Val::list(); *l.l = apps;
        print(dumps(obj({{"term", sv(term)}, {"apps", l}}), json_opts()));
        return apps.empty() ? 1 : 0;
    }
    if (apps.empty()) { print("No App Store apps for " + str_repr(term) + ". Try the words a customer would type."); return 1; }
    print("App Store search " + str_repr(term) + " (" + upper(S(a, "country")) + "), in the store's own order:");
    print("  " + ljust("app id", 11) + " " + ljust("app", 34) + " " + rjust("rating", 6) + " " + rjust("ratings", 10) + "  seller");
    for (const Val& x : apps) {
        const Val r = get(x, "rating");
        print("  " + ljust(pystr(get(x, "app_id")), 11) + " " + ljust(cpslice(get(x, "app_name").s, 0, 34), 34) + " " + rjust(r.is_none() ? "?" : fmt_f(num_f(r), 1), 6) + " " +
              rjust(fmt_int(get(x, "ratings")), 10) + "  " + pystr(either(get(x, "seller"), sv(""))));
    }
    print("Next: scout.py reviews APP_ID --write for the ones that are real rivals (most ratings first).");
    return 0;
}
inline int cmd_reviews(const Val& a) {
    Source src = source_of(a);
    const long long today = today_of(a);
    const Val doc = load_app(src, S(a, "app"), I(a, "pages"), "", today).first;
    const List& rv = *get(doc, "reviews").l;
    long long lows = 0, stars[6] = {0, 0, 0, 0, 0, 0};
    List dates;
    for (const Val& r : rv) {
        const long long s = get(r, "stars").i;
        if (s <= 2) lows++;
        if (s >= 1 && s <= 5) stars[s]++;
        dates.push_back(get(r, "date"));
    }
    std::stable_sort(dates.begin(), dates.end(), py_lt);
    const long long pages = get(doc, "pages").i;
    if (B(a, "json")) print(dumps(doc, json_opts()));
    else {
        print(pystr(get(doc, "app_name")) + " (" + pystr(get(doc, "app_id")) + "): read " + std::to_string(rv.size()) + " reviews, the most recent first (" +
              std::to_string(pages) + " page" + (pages == 1 ? "" : "s") + "), " + nice_date(dates.front()) + " to " + nice_date(dates.back()));
        if (!get(doc, "rating").is_none()) print("  App Store: " + fmt_f(num_f(get(doc, "rating")), 1) + " stars from " + fmt_int(get(doc, "ratings")) + " ratings");
        std::vector<std::string> parts; for (int s = 5; s >= 1; --s) parts.push_back(std::to_string(s) + ": " + std::to_string(stars[s]));
        print("  Stars: " + join(parts, "  "));
        print("  1-2 star reviews: " + std::to_string(lows) + " of " + std::to_string(rv.size()));
    }
    if (B(a, "write")) {
        const std::string path = join_path(join_path(seed_dir(a), "reviews"), pystr(get(doc, "app_id")) + ".json");
        write_json(path, doc);
        if (!B(a, "json")) print("Wrote " + path + ".");
    }
    if (!B(a, "json")) print("Next: scout.py themes " + pystr(get(doc, "app_id")) + ", then scout.py compare with the other rivals.");
    return 0;
}
inline List docs_for(const Val& a, const std::vector<std::string>& refs) {
    Source src = source_of(a);
    List docs;
    for (const auto& ref : refs) {
        if (is_file(ref)) { docs.push_back(read_json(ref, "reviews file")); continue; }
        const long long pages = I(a, "pages");
        const auto [doc, fetched] = load_app(src, ref, pages, seed_dir(a), today_of(a));
        if (fetched && B(a, "write")) write_json(join_path(join_path(seed_dir(a), "reviews"), pystr(get(doc, "app_id")) + ".json"), doc);
        docs.push_back(doc);
    }
    List ids; for (const Val& d : docs) ids.push_back(at(d, "app_id"));
    for (std::size_t i = 0; i < ids.size(); ++i)
        for (std::size_t j = 0; j < i; ++j) if (same_key(ids[i], ids[j])) throw ScoutError("the same app is in the list twice");
    return docs;
}
inline long long check_stars(long long n) {
    if (n < 1 || n > 3) throw ScoutError("--stars is 1, 2 or 3 (the low reviews are the ones at or under it)");
    return n;
}
inline int cmd_themes(const Val& a) {
    const Rules rules = load_rules(S(a, "rules"));
    const List docs = docs_for(a, {S(a, "app")});
    const long long stars = I(a, "stars");
    const Stats s = app_summary(docs[0], rules, check_stars(stars));
    if (B(a, "json")) {
        const auto top = most_mentioned(s.counts);
        print(dumps(obj({{"app_id", s.app_id}, {"name", s.name}, {"reviews_read", iv(s.read)}, {"low_reviews", iv(s.low)}, {"themes", counts_val(s, rules)},
                         {"unthemed", iv(s.unthemed)}, {"most_mentioned", key_val(top, rules)}, {"examples", top ? examples_of(s.matches[*top]) : Val::list()}}), json_opts()));
        return 0;
    }
    if (!s.low) { print(pystr(s.app_name) + " has no reviews at " + std::to_string(stars) + " stars or under among the " + std::to_string(s.read) + " read."); return 1; }
    print_themes(s, rules, stars, I(a, "examples"));
    return 0;
}
inline int cmd_compare(const Val& a) {
    const Rules rules = load_rules(S(a, "rules"));
    std::vector<std::string> refs; for (const Val& x : items_of(get(a, "apps"))) refs.push_back(x.s);
    const List docs = docs_for(a, refs);
    const long long stars = check_stars(I(a, "stars"));
    Val doc = build_competitors(docs, rules, stars, S(a, "by"), today_of(a));
    const std::string path = join_path(seed_dir(a), "competitors.json");
    if (B(a, "write") && is_file(path)) {
        const Val old = read_json(path, "competitors");
        std::vector<std::pair<Val, Val>> notes;
        for (const Val& x : items_of(either(oget(old, "apps"), Val::list()))) {
            if (!truthy(oget(x, "pricing"))) continue;
            const Val id = get(x, "app_id"), pr = get(x, "pricing");
            bool set = false;
            for (auto& n : notes) if (same_key(n.first, id)) { n.second = pr; set = true; break; }
            if (!set) notes.emplace_back(id, pr);
        }
        for (Val& x : *doc.d->get("apps")->l)
            for (const auto& n : notes) if (same_key(n.first, get(x, "app_id"))) { if (truthy(n.second)) x.d->set("pricing", n.second); break; }
    }
    if (B(a, "json")) print(dumps(doc, json_opts()));
    else print_compare(doc);
    if (B(a, "write")) { write_json(path, doc); if (!B(a, "json")) print("Wrote " + path + "."); }
    if (!B(a, "json")) print("Next: read each rival's pricing page and record it (scout.py pricing), then /seed-market.");
    return 0;
}
inline int cmd_pricing(const Val& a) {
    const std::string source = S(a, "source"), note = S(a, "note");
    if (!re::match(R"(^https?://\S+\.\S+)", source)) throw ScoutError("--source needs the URL of the page you read (their pricing or help page)", 1);
    if (strip(note).empty()) throw ScoutError("--note needs what the page says, in a sentence", 1);
    const std::string path = join_path(seed_dir(a), "competitors.json");
    Val doc = read_json(path, "competitors.json (run scout.py compare --write first)");
    const std::string ref = lower(strip(S(a, "app")));
    Val hit;
    for (const Val& x : items_of(either(oget(doc, "apps"), Val::list()))) {
        const bool m = pystr(oget(x, "app_id")) == ref || lower(str_of(either(get(x, "name"), sv("")), "object has no attribute 'lower'")) == ref;
        if (m && hit.is_none()) hit = x;   // the list comprehension reads every app
    }
    if (hit.is_none()) throw ScoutError(S(a, "app") + " is not in " + path, 1);
    const Val entry = obj({{"note", sv(strip(note))}, {"source", sv(source)}, {"read", sv(iso_of(today_of(a)))}});
    if (!hit.d->has("pricing")) hit.d->set("pricing", Val::list());
    Val pr = get(hit, "pricing");
    if (pr.t != Val::LIST) throw PyErr("AttributeError", std::string("'") + type_name(pr) + "' object has no attribute 'append'");
    pr.l->push_back(entry);
    print(pystr(at(hit, "name")) + " pricing: " + strip(note) + " (" + source + ")");
    if (B(a, "write")) { write_json(path, doc); print("Wrote " + path + "."); }
    else print("Not saved: add --write.");
    return 0;
}
inline int cmd_show(const Val& a) {
    const std::string path = join_path(seed_dir(a), "competitors.json");
    if (!is_file(path)) { print("No " + path + " yet. Run: scout.py compare APP_ID APP_ID --write"); return 1; }
    const Val doc = read_json(path, "competitors");
    if (B(a, "json")) { print(dumps(doc, json_opts())); return 0; }
    print_compare(doc);
    for (const Val& x : items_of(either(oget(doc, "apps"), Val::list())))
        for (const Val& p : items_of(either(oget(x, "pricing"), Val::list())))
            print("  " + pystr(at(x, "name")) + " pricing: " + pystr(at(p, "note")) + " (" + pystr(at(p, "source")) + ")");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("scout.py", "Competitor Scout (seed-scout): your rivals' weak points, from their reviews.");
    p.subcommands("cmd", false);
    auto common = [](Parser& s, bool net) {
        s.opt({"--dir"}).help("the seed folder (default ./seed)");
        s.opt({"--json"}).store_true();
        s.opt({"--today"}).help("YYYY-MM-DD for the dates written (default: today)");
        if (net) {
            s.opt({"--offline"}).metavar("DIR").help("saved App Store responses instead of the network");
            s.opt({"--country"}).def("us").help("App Store country (default us)");
        }
    };
    {
        Parser& s = p.sub("find"); s.help_line = "search the App Store for apps like yours";
        s.pos("term"); s.opt({"--limit"}).integer().def(10LL);
        common(s, true);
    }
    {
        Parser& s = p.sub("reviews"); s.help_line = "read an app's most recent reviews";
        s.pos("app").help("App Store id (547320928) or a name");
        s.opt({"--pages"}).integer().def(static_cast<long long>(MAX_PAGES)).help("pages of 50, 1 to 10 (default 10)");
        s.opt({"--write"}).store_true().help("save to seed/reviews/<app_id>.json");
        common(s, true);
    }
    {
        Parser& s = p.sub("themes"); s.help_line = "what the low reviews complain about";
        s.pos("app").help("App Store id, name, or a saved reviews file");
        s.opt({"--pages"}).integer().def(static_cast<long long>(MAX_PAGES));
        s.opt({"--stars"}).integer().def(2LL).help("low = at or under this many stars (default 2)");
        s.opt({"--examples"}).integer().def(EXAMPLES);
        s.opt({"--rules"}).help("a JSON file of extra or replacement themes");
        s.opt({"--write"}).store_true().help("save the reviews it had to fetch");
        common(s, true);
    }
    {
        Parser& s = p.sub("compare"); s.help_line = "the weak point of each rival -> seed/competitors.json";
        s.pos("apps").nargs("+");
        s.opt({"--pages"}).integer().def(static_cast<long long>(MAX_PAGES));
        s.opt({"--stars"}).integer().def(2LL);
        s.opt({"--by"}).choices({"gap", "count"}).def("gap").help("gap: what sets each app apart (default); count: the most mentioned");
        s.opt({"--rules"});
        s.opt({"--write"}).store_true();
        common(s, true);
    }
    {
        Parser& s = p.sub("pricing"); s.help_line = "record what a rival's pricing page says, with its URL";
        s.pos("app").help("app id or short name in competitors.json");
        s.opt({"--note"}).required();
        s.opt({"--source"}).required().help("the URL you read");
        s.opt({"--write"}).store_true();
        common(s, false);
    }
    { Parser& s = p.sub("show"); s.help_line = "print seed/competitors.json"; common(s, false); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 2; }
    try {
        if (cmd == "find") return cmd_find(a);
        if (cmd == "reviews") return cmd_reviews(a);
        if (cmd == "themes") return cmd_themes(a);
        if (cmd == "compare") return cmd_compare(a);
        if (cmd == "pricing") return cmd_pricing(a);
        return cmd_show(a);
    } catch (const ScoutError& e) { eprint(std::string("scout.py: ") + e.what()); return e.code; }
}
PACKTOOL("seed-scout/scout", main);
}  // namespace packtools::seed_scout
