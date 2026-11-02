// hunter-research/brief: company research brief (SEC EDGAR filings, the company's own blogs, interview loops with a
// source on every line), a C++ port of hunter-skill/skills/hunter-research/brief.py (MIT, Jake Schincariol). Same
// subcommands, text, JSON, notes and exit codes (3 = the brief exists and --force was not given). Offline mode
// (--offline DIR) reads submissions-CIK*.json, company_tickers.json and feed-<id>.xml, as the Python does.
//
// Dates are day numbers (py_date's epoch); a missing one is std::nullopt, which is where Python's code would subtract
// from None and raise TypeError (that happens here too). Python's \d matches any Unicode decimal digit and int()
// reads them, so the date matchers below do the same with py_digit().
namespace packtools::hunter_research {
using namespace pyrt;
using Day = std::optional<long long>;

struct BriefError : std::runtime_error { using std::runtime_error::runtime_error; };

inline constexpr const char* SUBMISSIONS = "https://data.sec.gov/submissions/CIK";
inline constexpr const char* TICKERS = "https://www.sec.gov/files/company_tickers.json";
inline constexpr const char* EDGAR_PAGE = "https://www.sec.gov/cgi-bin/browse-edgar?action=getcompany&CIK=";
inline constexpr const char* FEED_UA = "hunter-skill/1.0 (company research; +https://github.com/Jakeschincariol/hunter-skill)";
inline const std::vector<std::pair<std::string, std::string>>& items_8k() {
    static const std::vector<std::pair<std::string, std::string>> m = {
        {"1.01", "material agreement"}, {"1.02", "agreement ended"}, {"1.03", "bankruptcy"},
        {"1.05", "cybersecurity incident"}, {"2.01", "acquisition or sale of assets"},
        {"2.02", "earnings results"}, {"2.03", "new debt"}, {"2.05", "restructuring costs"},
        {"2.06", "impairment"}, {"3.01", "delisting notice"}, {"3.03", "shareholder rights change"},
        {"4.01", "auditor change"}, {"4.02", "past financials no longer reliable"},
        {"5.01", "change in control"}, {"5.02", "director or officer change"},
        {"5.03", "bylaws or fiscal year change"}, {"5.07", "shareholder vote results"},
        {"7.01", "Regulation FD disclosure"}, {"8.01", "other events"}};
    return m;
}
inline const char* MONTHS[] = {"January", "February", "March", "April", "May", "June", "July", "August",
                               "September", "October", "November", "December"};

inline std::string skill_path(const char* name) { return skill_file(name); }

// ---------------------------------------------------------------- small Python pieces
// the value of a Unicode decimal digit (Nd), else -1. Nd characters come in runs of ten starting at zero.
inline int py_digit(uint32_t c) {
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
    if (c < 0x660 || std::strcmp(uni::category(c), "Nd") != 0) return -1;
    uint32_t a = c;
    while (a > 0 && std::strcmp(uni::category(a - 1), "Nd") == 0) a--;
    return static_cast<int>((c - a) % 10);
}
inline std::vector<uint32_t> u32(const std::string& s) { std::vector<uint32_t> v; for (const auto& ch : cps(s)) v.push_back(cp_of(ch)); return v; }
inline long long digits_at(const std::vector<uint32_t>& v, std::size_t a, std::size_t n) {   // n \d's at a, else -1
    if (a + n > v.size()) return -1;
    long long x = 0;
    for (std::size_t k = a; k < a + n; ++k) { const int d = py_digit(v[k]); if (d < 0) return -1; x = x * 10 + d; }
    return x;
}
inline std::string type_of(const Val& v) { return type_name(v); }
inline Val pget(const Val& v, const std::string& k, const Val& dflt = Val()) {   // v.get(k, dflt) on what should be a dict
    if (v.t != Val::DICT) throw PyErr("AttributeError", "'" + type_of(v) + "' object has no attribute 'get'");
    return v.d->has(k) ? v.d->at(k) : dflt;
}
inline Val item(const Val& v, const std::string& k) {   // v[k]
    if (v.t != Val::DICT) throw PyErr("TypeError", "'" + type_of(v) + "' object is not subscriptable");
    if (!v.d->has(k)) throw PyErr("KeyError", str_repr(k));
    return v.d->at(k);
}
inline std::string must_str(const Val& v, const char* method) {
    if (v.t == Val::STR) return v.s;
    throw PyErr("AttributeError", "'" + type_of(v) + "' object has no attribute '" + method + "'");
}
inline Val load_json(const std::string& path, const Val& dflt = Val()) {
    if (!path.empty() && is_file(path)) return loads(read_text(path));
    return dflt;
}
inline std::string zfill(const std::string& s, std::size_t w) {
    const std::size_t n = cplen(s);
    if (n >= w) return s;
    const std::string pad(w - n, '0');
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) return s.substr(0, 1) + pad + s.substr(1);
    return pad + s;
}
// re.sub(r"[^a-z0-9]+", "-", (text or "").lower()).strip("-") or "company"
inline std::string slug(const std::string& text) {
    std::string o; bool dash = false;
    for (const auto& ch : cps(lower(text))) {
        const char x = ch.size() == 1 ? ch[0] : '\0';
        if ((x >= 'a' && x <= 'z') || (x >= '0' && x <= '9')) { if (dash && !o.empty()) o += '-'; dash = false; o += x; }
        else dash = true;
    }
    return o.empty() ? "company" : o;
}
inline long long today_days_local() {
    const std::string t = today_iso();
    return py_date(std::stoll(t.substr(0, 4)), std::stoll(t.substr(5, 2)), std::stoll(t.substr(8, 2)));
}
inline std::string day_text(const Day& d) { return d ? date_iso(*d) : "undated"; }
[[noreturn]] inline void none_minus() { throw PyErr("TypeError", "unsupported operand type(s) for -: 'NoneType' and 'datetime.date'"); }
inline long long days_between(const Day& today, long long d) { if (!today) none_minus(); return *today - d; }

// re.match(r"(\d{4})-(\d{2})-(\d{2})", str(value or "")) -> datetime.date (ValueError when impossible, not caught)
inline Day parse_day(const Val& value) {
    const std::string s = truthy(value) ? pystr(value) : "";
    const auto v = u32(s);
    const long long y = digits_at(v, 0, 4), m = digits_at(v, 5, 2), d = digits_at(v, 8, 2);
    if (y < 0 || m < 0 || d < 0 || v[4] != '-' || v[7] != '-') return std::nullopt;
    return py_date(y, m, d);
}

inline Val sec_user_agent_v(const Val& cli, const std::string& profile_path) {
    std::string ua;
    if (truthy(cli)) ua = strip(must_str(cli, "strip"));
    else if (const char* e = std::getenv("SEC_USER_AGENT"); e && *e) ua = strip(e);
    if (ua.empty()) {
        const Val prof = orv(load_json(profile_path), Val::dict());
        const Val x = pget(prof, "sec_user_agent");
        ua = strip(truthy(x) ? pystr(x) : "");
    }
    if (ua.empty() || ua.find('@') == std::string::npos || ua.find("you@example.com") != std::string::npos) return Val();
    return Val::str(ua);
}

// ---------------------------------------------------------------- fetching
struct Fetcher {
    std::optional<std::string> sec_ua, offline;
    double pause = 0.2;
    long long last = 0;
    std::string get(const std::string& url, const std::string& name, bool sec = false) {
        if (offline) {
            const std::string path = path_join(*offline, name);
            if (!is_file(path)) throw BriefError("offline: no file " + name);
            return read_bytes(path);
        }
        if (sec && !sec_ua) throw BriefError("no SEC User-Agent set");
        const long long gap = now_us() - last, want = static_cast<long long>(pause * 1e6);
        if (gap < want) std::this_thread::sleep_for(std::chrono::microseconds(want - gap));
        last = now_us();
        const HttpResp r = http_get(url, {"User-Agent: " + (sec ? *sec_ua : std::string(FEED_UA))}, 30);
        if (r.status >= 400) throw BriefError("HTTP " + std::to_string(r.status) + " for " + url);
        if (!r.error.empty() || r.status == 0) throw BriefError("could not reach " + url + ": " + (r.error.empty() ? std::string("no response") : r.error));
        return r.body;
    }
};
inline Val json_bytes(const std::string& b) { return loads(b); }   // json.loads(data.decode("utf-8"))

// ---------------------------------------------------------------- SEC
inline std::string cik_for(Fetcher& f, const Val& ticker) {
    const Val data = json_bytes(f.get(TICKERS, "company_tickers.json", true));
    const std::string t = replace_all(upper(must_str(ticker, "upper")), ".", "-");
    if (data.t != Val::DICT) throw PyErr("AttributeError", "'" + type_of(data) + "' object has no attribute 'values'");
    for (const auto& [_, v] : data.d->items)
        if (upper(must_str(item(v, "ticker"), "upper")) == t) return zfill(pystr(item(v, "cik_str")), 10);
    throw BriefError("no SEC filer with ticker " + ticker.s + " (private companies do not file with the SEC)");
}
inline long long int_of(const std::string& s) {
    const auto n = py_int(s);
    if (!n) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(s));
    return *n;
}
inline std::string filing_url(const std::string& cik, const Val& accession, const Val& doc) {
    return "https://www.sec.gov/Archives/edgar/data/" + std::to_string(int_of(cik)) + "/" + replace_all(must_str(accession, "replace"), "-", "") + "/" + pystr(doc);
}
// re.findall(r"\d+\.\d+", text)
inline std::vector<std::string> item_numbers(const std::string& text) {
    const auto v = u32(text);
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < v.size()) {
        std::size_t j = i;
        while (j < v.size() && py_digit(v[j]) >= 0) j++;
        if (j > i && j < v.size() && v[j] == '.') {
            std::size_t k = j + 1;
            while (k < v.size() && py_digit(v[k]) >= 0) k++;
            if (k > j + 1) {
                std::string s; for (std::size_t x = i; x < k; ++x) s += utf8(v[x]);
                out.push_back(s); i = k; continue;
            }
        }
        i++;
    }
    return out;
}
inline Val sec_summary(const Val& data, const Day& today, long long days) {
    const Val filings = orv(pget(data, "filings"), Val::dict());
    const Val recent = orv(pget(filings, "recent"), Val::dict());
    const Val forms = pget(recent, "form", Val::list());
    std::vector<Val> rows;
    for (std::size_t i = 0; i < iter(forms).size(); ++i) {
        Val row = Val::dict();
        for (const auto& [k, col] : recent.d->items) {
            if (col.t != Val::LIST) throw PyErr("TypeError", "'" + type_of(col) + "' object is not subscriptable");
            if (i >= col.l->size()) throw PyErr("IndexError", "list index out of range");
            row.d->set(k, (*col.l)[i]);
        }
        rows.push_back(row);
    }
    const std::string cik = zfill(pystr(pget(data, "cik", Val::str(""))), 10);
    const Val addr = orv(pget(orv(pget(data, "addresses"), Val::dict()), "business"), Val::dict());
    const Val fye_v = pget(data, "fiscalYearEnd");
    const std::string fye = truthy(fye_v) ? pystr(fye_v) : "";
    std::vector<std::string> hq;
    for (const char* k : {"street1", "city", "stateOrCountry"}) {
        const Val x = pget(addr, k);
        if (!truthy(x)) continue;
        if (x.t != Val::STR) throw PyErr("TypeError", "sequence item " + std::to_string(hq.size()) + ": expected str instance, " + type_of(x) + " found");
        hq.push_back(cplen(x.s) > 2 ? title(x.s) : x.s);
    }
    Val fy;
    if (cplen(fye) == 4) {
        const long long m = int_of(cpslice(fye, 0, 2)) - 1;
        if (m < -12 || m > 11) throw PyErr("IndexError", "list index out of range");
        fy = Val::str(std::string(MONTHS[m < 0 ? m + 12 : m]) + " " + std::to_string(int_of(cpslice(fye, 2))));
    }
    Val out = Val::dict();
    out.d->set("name", pget(data, "name"));
    out.d->set("cik", Val::str(cik));
    out.d->set("tickers", orv(pget(data, "tickers"), Val::list()));
    out.d->set("exchanges", orv(pget(data, "exchanges"), Val::list()));
    out.d->set("industry", pget(data, "sicDescription"));
    out.d->set("hq", Val::str(join(hq, ", ")));
    out.d->set("fiscal_year_end", fy);
    out.d->set("edgar", Val::str(EDGAR_PAGE + cik));
    out.d->set("annual", Val()); out.d->set("quarterly", Val()); out.d->set("proxy", Val()); out.d->set("events", Val::list());
    const auto is = [](const Val& v, const char* s) { return v.t == Val::STR && v.s == s; };
    for (const Val& r : rows) {
        const std::string url = filing_url(cik, item(r, "accessionNumber"), item(r, "primaryDocument"));
        const Val form = item(r, "form");
        Val rec = Val::dict();
        rec.d->set("form", form); rec.d->set("filed", item(r, "filingDate"));
        rec.d->set("period", truthy(pget(r, "reportDate")) ? pget(r, "reportDate") : Val()); rec.d->set("url", Val::str(url));
        if ((is(form, "10-K") || is(form, "20-F")) && !truthy(get(out, "annual"))) out.d->set("annual", rec);
        else if (is(form, "10-Q") && !truthy(get(out, "quarterly"))) out.d->set("quarterly", rec);
        else if (is(form, "DEF 14A") && !truthy(get(out, "proxy"))) out.d->set("proxy", rec);
        else if (is(form, "8-K") || is(form, "8-K/A")) {
            const Day filed = parse_day(item(r, "filingDate"));
            if (filed && days_between(today, *filed) <= days) {
                const Val iv = pget(r, "items");
                if (truthy(iv) && iv.t != Val::STR) throw PyErr("TypeError", "expected string or bytes-like object, got '" + type_of(iv) + "'");
                List items; std::vector<std::string> about;
                for (const auto& x : item_numbers(truthy(iv) ? iv.s : "")) {
                    if (x == "9.01") continue;
                    items.push_back(Val::str(x));
                    std::string a = "item " + x;
                    for (const auto& [k, t] : items_8k()) if (k == x) a = t;
                    about.push_back(a);
                }
                rec.d->set("items", Val::list(items));
                rec.d->set("about", Val::str(about.empty() ? "see filing" : join(about, ", ")));
                get(out, "events").l->push_back(rec);
            }
        }
    }
    return out;
}
inline Val fetch_sec(Fetcher& f, const Val& cik0, const Val& ticker, const Day& today, long long days) {
    const std::string cik = zfill(truthy(cik0) ? pystr(cik0) : cik_for(f, ticker), 10);
    return sec_summary(json_bytes(f.get(SUBMISSIONS + cik + ".json", "submissions-CIK" + cik + ".json", true)), today, days);
}

// ---------------------------------------------------------------- feeds
// strptime("%Y-%m-%d %H:%M:%S") field rules: %m 1[0-2]|0[1-9], %d/%H/%M/%S two digits whose first is ASCII
inline Day feed_day(const std::string& value0) {
    const std::string value = strip(value0);
    if (value.empty()) return std::nullopt;
    if (const auto us = parsedate_us(value)) {
        const long long D = 86400LL * 1000000LL;
        return (*us >= 0 ? *us / D : -((-*us + D - 1) / D));
    }
    const auto v = u32(value);
    const long long y = digits_at(v, 0, 4), mo = digits_at(v, 5, 2), d = digits_at(v, 8, 2), h = digits_at(v, 11, 2), mi = digits_at(v, 14, 2), s = digits_at(v, 17, 2);
    if (y >= 0 && mo >= 0 && d >= 0 && h >= 0 && mi >= 0 && s >= 0 && v[4] == '-' && v[7] == '-' && v[10] == 'T' && v[13] == ':' && v[16] == ':') {
        std::size_t i = 19;
        if (i < v.size() && v[i] == '.' && i + 1 < v.size() && py_digit(v[i + 1]) >= 0) { i++; while (i < v.size() && py_digit(v[i]) >= 0) i++; }
        long long off = 0;   // minutes east of UTC
        if (i < v.size() && (v[i] == '+' || v[i] == '-')) {
            const long long th = digits_at(v, i + 1, 2), tm = digits_at(v, i + 4, 2);
            if (th >= 0 && tm >= 0 && v[i + 3] == ':') off = (v[i] == '+' ? 1 : -1) * (th * 60 + tm);
        }
        const auto in = [&](std::size_t k, char lo, char hi) { return v[k] >= static_cast<uint32_t>(lo) && v[k] <= static_cast<uint32_t>(hi); };
        const auto dg = [&](std::size_t k) { return py_digit(v[k]) >= 0; };
        const bool m_ok = (v[5] == '1' && in(6, '0', '2')) || (v[5] == '0' && in(6, '1', '9'));
        const bool d_ok = (v[8] == '3' && in(9, '0', '1')) || (in(8, '1', '2') && dg(9)) || (v[8] == '0' && in(9, '1', '9'));
        const bool h_ok = (v[11] == '2' && in(12, '0', '3')) || (in(11, '0', '1') && dg(12));
        const bool mi_ok = in(14, '0', '5') && dg(15), s_ok = in(17, '0', '5') && dg(18);
        if (!m_ok || !d_ok || !h_ok || !mi_ok || !s_ok) {
            std::string shown; for (std::size_t k = 0; k < 19; ++k) shown += utf8(k == 10 ? ' ' : v[k]);
            throw PyErr("ValueError", "time data " + str_repr(shown) + " does not match format '%Y-%m-%d %H:%M:%S'");
        }
        const long long day = py_date(y, mo, d);
        const long long mins = h * 60 + mi - off;
        return day + (mins >= 0 ? mins / 1440 : -((-mins + 1439) / 1440));
    }
    return parse_day(Val::str(value));
}

struct Post { std::string title, link; Day date; Val feed; };
inline const xml::Node* child(const xml::Node& el, const std::string& name) {
    for (const auto& c : el.kids) if (xml::local(c->tag) == name) return c.get();
    return nullptr;
}
// re.sub(r"\s+", " ", t).replace(EM DASH, ",").strip()
inline std::string clean_title(const std::string& t) {
    std::string o; bool sp = false;
    for (const auto& ch : cps(t)) {
        if (py_space(cp_of(ch))) { if (!sp) o += ' '; sp = true; continue; }
        sp = false;
        o += ch == "\xE2\x80\x94" ? std::string(",") : ch;
    }
    return strip(o);
}
inline std::vector<Post> parse_feed(const std::string& data, const std::string& name) {
    std::shared_ptr<xml::Node> root;
    try { root = xml::fromstring(data); } catch (const xml::ParseError& e) { throw BriefError("feed " + name + " is not valid XML: " + e.what()); }
    std::vector<Post> out;
    root->iter([&](const xml::Node& el) {
        const std::string tag = xml::local(el.tag);
        if (tag == "item") {
            const auto g = [&](const char* n) { const auto* c = child(el, n); return c ? strip(c->text) : std::string(); };
            Post p; p.title = g("title"); p.link = g("link"); if (p.link.empty()) p.link = g("guid");
            std::string when = g("pubDate"); if (when.empty()) when = g("date");
            p.date = feed_day(when);
            out.push_back(p);
        } else if (tag == "entry") {
            Post p;
            if (const auto* t = child(el, "title")) p.title = strip(t->text);
            for (const auto& c : el.kids)
                if (xml::local(c->tag) == "link" && c->get("rel", "alternate") == "alternate") { p.link = c->get("href", ""); break; }
            const xml::Node* stamp = child(el, "published");
            if (!stamp) stamp = child(el, "updated");
            p.date = feed_day(stamp ? stamp->text : "");
            out.push_back(p);
        }
    });
    for (auto& p : out) p.title = clean_title(p.title);
    return out;
}
inline std::vector<Post> fetch_feeds(Fetcher& f, const List& feeds, const Day& today, long long days, std::vector<std::string>& notes, std::size_t per_feed = 5) {
    std::vector<Post> out;
    for (const Val& fd : feeds) {
        std::vector<Post> items;
        try { items = parse_feed(f.get(pystr(item(fd, "url")), "feed-" + pystr(item(fd, "id")) + ".xml"), pystr(item(fd, "name"))); }
        catch (const BriefError& e) { notes.push_back(pystr(item(fd, "name")) + ": " + e.what()); continue; }
        std::vector<Post> fresh;
        for (const auto& i : items)
            if (i.date) { const long long n = days_between(today, *i.date); if (n >= 0 && n <= days) fresh.push_back(i); }
        std::stable_sort(fresh.begin(), fresh.end(), [](const Post& a, const Post& b) { return *a.date > *b.date; });
        bool any = false; for (const auto& i : items) if (i.date) any = true;
        if (!any) fresh.assign(items.begin(), items.begin() + static_cast<long>(std::min<std::size_t>(3, items.size())));
        for (std::size_t k = 0; k < fresh.size() && k < per_feed; ++k) { Post p = fresh[k]; p.feed = item(fd, "name"); out.push_back(p); }
    }
    const long long MIN = py_date(1, 1, 1);
    std::stable_sort(out.begin(), out.end(), [&](const Post& a, const Post& b) { return a.date.value_or(MIN) > b.date.value_or(MIN); });
    return out;
}

// ---------------------------------------------------------------- loops
inline std::pair<Val, Val> find(const Val& mapping, const Val& company) {
    const Val m = orv(mapping, Val::dict());
    if (m.t != Val::DICT) throw PyErr("AttributeError", "'" + type_of(m) + "' object has no attribute 'items'");
    const std::string want = lower(truthy(company) ? must_str(company, "lower") : "");
    for (const auto& [k, v] : m.d->items) if (lower(k) == want) return {Val::str(k), v};
    return {company, Val()};
}
inline std::vector<std::string> loop_lines(const Val& entry) {
    if (!truthy(entry)) return {"(fill in) No bundled loop for this company. Read its own careers and interview-prep pages "
                                "and ask the recruiter. Mark anything from candidate reports as community-reported."};
    std::vector<std::string> lines;
    if (truthy(pget(entry, "reel_summary"))) lines.push_back("In one line (community summary): " + pystr(pget(entry, "reel_summary")));
    const List notes = iter(pget(entry, "notes", Val::list()));
    const auto official = [](const Val& n) { const Val k = pget(n, "kind"); return k.t == Val::STR && k.s == "official"; };
    for (const Val& n : notes) if (official(n)) lines.push_back("- " + pystr(item(n, "text")) + " [source: " + pystr(item(n, "source")) + "]");
    for (const Val& n : notes) if (!official(n)) lines.push_back("- Community-reported, not confirmed by the company: " + pystr(item(n, "text")));
    return lines;
}

// ---------------------------------------------------------------- the brief
inline List head(const Val& v, std::size_t n) {   // v[:n] for a list or a str (a str slices into characters)
    List o;
    if (v.t == Val::STR) { const auto c = cps(v.s); for (std::size_t k = 0; k < c.size() && k < n; ++k) o.push_back(Val::str(c[k])); return o; }
    const List l = iter(v);
    for (std::size_t k = 0; k < l.size() && k < n; ++k) o.push_back(l[k]);
    return o;
}
inline std::string join_strs(const List& l) {
    std::vector<std::string> s;
    for (std::size_t k = 0; k < l.size(); ++k) {
        if (l[k].t != Val::STR) throw PyErr("TypeError", "sequence item " + std::to_string(k) + ": expected str instance, " + type_of(l[k]) + " found");
        s.push_back(l[k].s);
    }
    return join(s, ", ");
}
inline std::string rstrip_dots(std::string s) { while (!s.empty() && s.back() == '.') s.pop_back(); return s; }
inline std::vector<std::string> business_lines(const Val& sec, const Val& info) {
    if (!truthy(sec)) {
        const bool cik = truthy(pget(orv(info, Val::dict()), "cik"));
        return {std::string("(fill in) No SEC filings in this brief. ") + (!cik ? "Private companies do not file with the SEC: use the company's own about, press and blog pages."
                                                                              : "Set an SEC User-Agent and run it again for the 10-K.")};
    }
    const std::string t = join_strs(head(item(sec, "tickers"), 2));
    const Val exv = item(sec, "exchanges");
    const std::string ex = truthy(exv) ? pystr(head(exv, 1)[0]) : "";
    const Val ind = item(sec, "industry");
    const std::string industry = rstrip_dots(truthy(ind) ? must_str(ind, "rstrip") : "n/a");
    const Val hq = item(sec, "hq"), fy = item(sec, "fiscal_year_end");
    std::vector<std::string> lines = {"- " + pystr(item(sec, "name")) + (t.empty() ? "" : " (" + t + " on " + ex + ")") + ". Industry code: " + industry +
                                      ". Headquarters on file with the SEC: " + (truthy(hq) ? pystr(hq) : "n/a") + ". Fiscal year ends " + (truthy(fy) ? pystr(fy) : "n/a") + "."};
    if (const Val a = item(sec, "annual"); truthy(a))
        lines.push_back("- Annual report: " + pystr(item(a, "form")) + " for the year ended " + pystr(item(a, "period")) + ", filed " + pystr(item(a, "filed")) + ": " +
                        pystr(item(a, "url")) + ". Item 1 (Business) says what the company does and how it makes money; Item 1A lists its risks.");
    if (const Val q = item(sec, "quarterly"); truthy(q))
        lines.push_back("- Latest quarter: 10-Q for the period ended " + pystr(item(q, "period")) + ", filed " + pystr(item(q, "filed")) + ": " + pystr(item(q, "url")));
    if (const Val p = item(sec, "proxy"); truthy(p))
        lines.push_back("- Leadership and pay: proxy statement (DEF 14A) filed " + pystr(item(p, "filed")) + ": " + pystr(item(p, "url")));
    lines.push_back("- (fill in) Two or three lines in your own words from Item 1, including the segment this team sits in.");
    return lines;
}
inline std::vector<std::string> news_lines(const Val& sec, const std::vector<Post>& posts, long long days) {
    std::vector<std::string> lines;
    if (truthy(sec)) {
        const List events = iter(item(sec, "events"));
        const Val* earn = nullptr;
        for (const Val& e : events) {
            for (const Val& x : iter(pget(e, "items", Val::list()))) if (x.t == Val::STR && x.s == "2.02") { earn = &e; break; }
            if (earn) break;
        }
        for (std::size_t k = 0; k < events.size() && k < 8; ++k) {
            const Val& e = events[k];
            lines.push_back("- " + pystr(item(e, "filed")) + " " + pystr(item(e, "form")) + ": " + pystr(item(e, "about")) + " " + pystr(item(e, "url")));
        }
        if (earn) lines.push_back("- Latest results: the 8-K of " + pystr(item(*earn, "filed")) + " (item 2.02) carries the press release as an exhibit. "
                                  "Quote revenue and growth from it, never from memory.");
        if (events.empty()) lines.push_back("- No 8-K filings in the last " + std::to_string(days) + " days.");
    }
    for (std::size_t k = 0; k < posts.size() && k < 10; ++k)
        lines.push_back("- " + day_text(posts[k].date) + " " + pystr(posts[k].feed) + ": " + posts[k].title + " " + posts[k].link);
    if (lines.empty()) lines.push_back("(fill in) Nothing fetched. Check the company's newsroom and blog.");
    lines.push_back("- (fill in) What this news means for the team you'd join, in one line.");
    return lines;
}
const std::regex CULTURE_RE("culture|principles|how we hire|hiring process", std::regex::icase);
inline std::string build(const Val& company, const Val& role, const Val& team, const Day& today, const Val& info, const Val& entry, const Val& sec,
                         const std::vector<Post>& posts, const std::vector<std::string>& notes, long long days, const Val& candidate) {
    std::string text = read_text(skill_path("brief.template.md"));
    const Val inf = orv(info, Val::dict()), ent = orv(entry, Val::dict());
    std::vector<std::string> sources;
    if (truthy(sec)) sources.push_back("- SEC EDGAR filings for " + pystr(item(sec, "name")) + ": " + pystr(item(sec, "edgar")));
    for (const Val& f : iter(pget(inf, "feeds", Val::list()))) sources.push_back("- " + pystr(item(f, "name")) + " (feed): " + pystr(item(f, "url")));
    if (truthy(pget(inf, "careers"))) sources.push_back("- Careers site: " + pystr(item(inf, "careers")));
    if (truthy(pget(inf, "investors"))) sources.push_back("- Investor relations: " + pystr(item(inf, "investors")));
    const List pages = iter(pget(ent, "prep_pages", Val::list()));
    for (const Val& p : pages) sources.push_back("- " + pystr(item(p, "title")) + ": " + pystr(item(p, "url")));
    std::vector<std::string> culture;
    for (const Val& p : pages)
        if (std::regex_search(hunter_interview::shadow(must_str(item(p, "title"), "search"), true), CULTURE_RE))
            culture.push_back("- Read the company's own words: " + pystr(item(p, "title")) + " (" + pystr(item(p, "url")) + ")");
    culture.push_back("- (fill in) What the company says it values, from its own pages, and what employees say "
                      "in public talks or posts (label those as personal views).");
    const std::vector<std::string> team_lines = {
        truthy(team) ? "- Team named in the posting: " + pystr(team) : "- (fill in) The team, from the posting.",
        "- (fill in) What the team builds, its stack and its recent launches, from the posting, the team's "
        "engineering blog posts and public talks.",
        "- (fill in) The hiring manager, if public: /hunter-connector researches them from public "
        "professional sources only."};
    if (!today) throw PyErr("AttributeError", "'NoneType' object has no attribute 'isoformat'");
    const std::vector<std::pair<std::string, std::string>> values = {
        {"company", pystr(company)}, {"role", truthy(role) ? pystr(role) : "(fill in)"}, {"team", truthy(team) ? pystr(team) : "the team"},
        {"today", date_iso(*today)}, {"for_line", truthy(candidate) ? " for " + pystr(candidate) : ""},
        {"business", join(business_lines(sec, info), "\n")}, {"news", join(news_lines(sec, posts, days), "\n")},
        {"culture", join(culture, "\n")}, {"team_section", join(team_lines, "\n")}, {"loop", join(loop_lines(entry), "\n")},
        {"sources", sources.empty() ? "(fill in)" : join(sources, "\n")}};
    for (const auto& [k, v] : values) text = replace_all(text, "{{" + k + "}}", v);
    if (!notes.empty()) {
        text += "\n## Notes from the build\n\n";
        for (std::size_t k = 0; k < notes.size(); ++k) text += (k ? "\n- " : "- ") + notes[k];
        text += "\n";
    }
    return text;
}

// ---------------------------------------------------------------- CLI
struct Gathered { Val name, info; Day today; Val sec; std::vector<Post> posts; std::vector<std::string> notes; };
inline Gathered gather(const Val& a, bool need_sec = true, bool need_feeds = true) {
    Gathered g;
    const Val companies = pget(orv(load_json(S(a, "companies")), Val::dict()), "companies", Val::dict());
    auto [name, info0] = find(companies, get(a, "company"));
    g.name = name;
    const Val src = orv(info0, Val::dict());
    if (src.t != Val::DICT) throw PyErr("TypeError", "'" + type_of(src) + "' object is not iterable");
    Val info = Val::dict();
    for (const auto& [k, v] : src.d->items) info.d->set(k, v);
    const auto drop = [&](const std::string& k) {
        Val n = Val::dict(); for (const auto& [kk, v] : info.d->items) if (kk != k) n.d->set(kk, v); info = n;
    };
    if (truthy(get(a, "ticker", Val()))) { info.d->set("ticker", get(a, "ticker")); drop("cik"); }
    if (truthy(get(a, "cik", Val()))) info.d->set("cik", get(a, "cik"));
    List feeds = iter(pget(info, "feeds", Val::list()));
    const List extra = iter(orv(get(a, "feed", Val()), Val::list()));
    for (std::size_t i = 0; i < extra.size(); ++i) {
        const std::string url = extra[i].s;
        Val f = Val::dict();
        f.d->set("id", Val::str("extra-" + std::to_string(i + 1)));
        f.d->set("name", Val::str(url.find("//") != std::string::npos ? split(url, "/")[2] : url));
        f.d->set("url", Val::str(url));
        feeds.push_back(f);
    }
    info.d->set("feeds", Val::list(feeds));
    g.today = truthy(get(a, "today")) ? parse_day(get(a, "today")) : Day(today_days_local());
    const Val ua = sec_user_agent_v(get(a, "user_agent", Val()), truthy(get(a, "profile", Val())) ? S(a, "profile") : "hunter/profile.json");
    Fetcher f;
    if (truthy(ua)) f.sec_ua = ua.s;
    if (truthy(get(a, "offline"))) f.offline = S(a, "offline");
    const long long days = get(a, "days").i;
    if (need_sec && (truthy(pget(info, "cik")) || truthy(pget(info, "ticker")))) {
        if (!truthy(ua) && !f.offline)
            g.notes.push_back("SEC filings skipped: pass --user-agent \"Your Name you@example.com\", set SEC_USER_AGENT, "
                              "or add sec_user_agent to hunter/profile.json (SEC asks every script to identify itself)");
        else {
            try { g.sec = fetch_sec(f, pget(info, "cik"), pget(info, "ticker"), g.today, days); }
            catch (const BriefError& e) { g.notes.push_back(std::string("SEC: ") + e.what()); }
        }
    } else if (need_sec)
        g.notes.push_back(pystr(g.name) + " is not in companies.json and no --ticker was given: no SEC filings (private companies do not file)");
    if (need_feeds) g.posts = fetch_feeds(f, feeds, g.today, std::min<long long>(days, 60), g.notes);
    g.info = info;
    return g;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("brief", "Company research brief: SEC filings, company blogs and interview loops.");
    p.subcommands("cmd", false);
    const auto common = [](Parser& s, bool sec) {
        s.opt({"--company"}).required();
        s.opt({"--companies"}).def(Val::str(skill_path("companies.json")));
        s.opt({"--loops"}).def(Val::str(skill_path("loops.json")));
        s.opt({"--today"}).help("YYYY-MM-DD (default: today)");
        s.opt({"--days"}).integer().def(Val::integer(180)).help("look back this many days for 8-Ks (posts: up to 60)");
        s.opt({"--offline"}).metavar("DIR");
        s.opt({"--json"}).store_true();
        if (sec) {
            s.opt({"--ticker"});
            s.opt({"--cik"});
            s.opt({"--feed"}).append().def(Val::list()).help("extra RSS/Atom feed URL");
            s.opt({"--user-agent"}).help("SEC User-Agent: \"Your Name you@example.com\"");
            s.opt({"--profile"}).def(Val::str("hunter/profile.json"));
        }
    };
    { Parser& b = p.sub("build"); b.help_line = "write hunter/companies/<company>.md"; common(b, true);
      b.opt({"--role"}); b.opt({"--team"}); b.opt({"--candidate"}).help("name on the brief (default: profile name)");
      b.opt({"--out"}).help("default: hunter/companies/<company>.md"); b.opt({"--stdout"}).store_true().help("print instead of writing");
      b.opt({"--force"}).store_true().help("overwrite an existing brief"); }
    { Parser& l = p.sub("loop"); l.help_line = "the interview loop with sources"; common(l, false); }
    { Parser& s = p.sub("sec"); s.help_line = "SEC facts and filings only"; common(s, true); }
    { Parser& f = p.sub("feeds"); f.help_line = "recent posts from the company's own blogs"; common(f, true); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    DumpOpts jo; jo.indent = 2;
    const long long days = get(a, "days").i;
    const Val loops = pget(orv(load_json(S(a, "loops")), Val::dict()), "companies", Val::dict());
    if (cmd == "loop") {
        const auto [name, entry] = find(loops, get(a, "company"));
        if (B(a, "json")) {
            Val o = Val::dict(); o.d->set("company", name); o.d->set("loop", entry);
            print(dumps(o, jo));
        } else {
            print(pystr(name) + " interview loop (verified " + pystr(pget(orv(load_json(S(a, "loops")), Val::dict()), "verified")) + ")");
            print(join(loop_lines(entry), "\n"));
        }
        return truthy(entry) ? 0 : 1;
    }
    if (cmd == "sec") {
        const Gathered g = gather(a, true, false);
        if (B(a, "json")) {
            Val o = Val::dict(); o.d->set("sec", g.sec);
            List n; for (const auto& x : g.notes) n.push_back(Val::str(x));
            o.d->set("notes", Val::list(n));
            print(dumps(o, jo));
        } else {
            auto lines = business_lines(g.sec, g.info);
            auto news = news_lines(g.sec, {}, days); news.pop_back();
            lines.insert(lines.end(), news.begin(), news.end());
            print(join(lines, "\n"));
            for (const auto& n : g.notes) print("note: " + n);
        }
        return truthy(g.sec) ? 0 : 1;
    }
    if (cmd == "feeds") {
        const Gathered g = gather(a, false, true);
        if (B(a, "json")) {
            List posts;
            for (const auto& x : g.posts) {
                Val d = Val::dict();
                d.d->set("title", Val::str(x.title)); d.d->set("link", Val::str(x.link));
                d.d->set("date", x.date ? Val::str(date_iso(*x.date)) : Val()); d.d->set("feed", x.feed);
                posts.push_back(d);
            }
            Val o = Val::dict(); o.d->set("posts", Val::list(posts));
            List n; for (const auto& x : g.notes) n.push_back(Val::str(x));
            o.d->set("notes", Val::list(n));
            print(dumps(o, jo));
        } else {
            for (const auto& x : g.posts)
                print(ljust(day_text(x.date), 10) + "  " + ljust(cpslice(pystr(x.feed), 0, 24), 24) + " " + x.title + "\n            " + x.link);
            for (const auto& n : g.notes) print("note: " + n);
            if (g.posts.empty()) print("No posts in the window.");
        }
        return g.posts.empty() ? 1 : 0;
    }
    const Gathered g = gather(a);
    const Val entry = find(loops, get(a, "company")).second;
    Val candidate = get(a, "candidate");
    if (!truthy(candidate)) candidate = pget(orv(load_json(S(a, "profile")), Val::dict()), "name");
    const std::string text = build(g.name, get(a, "role"), get(a, "team"), g.today, g.info, entry, g.sec, g.posts, g.notes, days, candidate);
    if (B(a, "stdout")) { out(text); return 0; }
    const std::string path = truthy(get(a, "out")) ? S(a, "out") : path_join(path_join("hunter", "companies"), slug(pystr(g.name)) + ".md");
    if (exists(path) && !B(a, "force")) {
        eprint("brief: " + path + " already exists (it may hold your edits). Use --stdout to compare or --force to rebuild it.");
        return 3;
    }
    const std::string folder = dirname(path);
    if (!folder.empty()) makedirs(folder);
    write_text(path, text);
    const long long filings = (truthy(g.sec) && truthy(get(g.sec, "annual")) ? 1 : 0) + (truthy(g.sec) ? static_cast<long long>(get(g.sec, "events").l->size()) : 0);
    print("Wrote " + path + ": " + std::to_string(filings) + " filings, " + std::to_string(g.posts.size()) + " blog posts, " +
          std::to_string(iter(pget(orv(entry, Val::dict()), "notes", Val::list())).size()) + " loop notes. Fill the (fill in) lines from the sources.");
    for (const auto& n : g.notes) print("note: " + n);
    return 0;
}
PACKTOOL("hunter-research/brief", main);
}  // namespace packtools::hunter_research
