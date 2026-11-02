// trader-copycat/edgar13f: a C++ port of edgar13f.py (Jake Schincariol's trader-skill, MIT) for A.S.T.R.O AI.
// Reads an investor's public 13F-HR filings on SEC EDGAR and rebuilds the portfolio as weights you can copy.
// Same commands, flags, output and exit codes as the original (find, filings, holdings, diff, copy).
namespace packtools::trader_copycat {
using namespace pyrt;
struct EdgarError : std::runtime_error { using std::runtime_error::runtime_error; };
inline const std::string SUBMISSIONS = "https://data.sec.gov/submissions/CIK";   // + cik10 + ".json"
inline const std::string TICKERS_URL = "https://www.sec.gov/files/company_tickers.json";
inline const std::string SEARCH_URL = "https://efts.sec.gov/LATEST/search-index?keysTyped=";
inline const std::string FIGI_URL = "https://api.openfigi.com/v3/mapping";
inline const std::string PLACEHOLDER = "you@example.com";
inline const std::string UA_HELP =
    "SEC requires a User-Agent with your name and email on every request (https://www.sec.gov/os/accessing-edgar-data). Set it once, any of these:\n"
    "  1. add  \"sec_user_agent\": \"Your Name you@example.com\"  to trader/config.json (with YOUR name and email)\n"
    "  2. export SEC_USER_AGENT=\"Your Name you@example.com\"\n"
    "  3. pass --user-agent \"Your Name you@example.com\"";
inline std::string env_or(const char* k) { const char* v = std::getenv(k); return v ? v : ""; }
// bytes.decode("utf-8") (strict): the message Python gives for the first bad sequence
inline void utf8_strict(const std::string& b) {
    const std::size_t n = b.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(b[i]);
        if (c < 0x80) { ++i; continue; }
        int need = 0; unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) { need = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        char hx[8]; std::snprintf(hx, sizeof hx, "0x%02x", c);
        if (!need) throw PyErr("UnicodeDecodeError", std::string("'utf-8' codec can't decode byte ") + hx + " in position " + std::to_string(i) + ": invalid start byte");
        std::size_t k = i + 1; int got = 0;
        while (got < need && k < n) { const unsigned char d = static_cast<unsigned char>(b[k]); if (got == 0 ? (d < lo || d > hi) : (d < 0x80 || d > 0xBF)) break; ++k; ++got; }
        if (got == need) { i = k; continue; }
        const std::string why = k >= n ? "unexpected end of data" : "invalid continuation byte";
        if (got == 0) throw PyErr("UnicodeDecodeError", std::string("'utf-8' codec can't decode byte ") + hx + " in position " + std::to_string(i) + ": " + why);
        throw PyErr("UnicodeDecodeError", "'utf-8' codec can't decode bytes in position " + std::to_string(i) + "-" + std::to_string(i + static_cast<std::size_t>(got)) + ": " + why);
    }
}
inline Val loads_bytes(const std::string& b) {   // json.loads(b.decode("utf-8"))
    utf8_strict(b);
    if (starts(b, "\xEF\xBB\xBF")) throw PyErr("JSONDecodeError", "Unexpected UTF-8 BOM (decode using utf-8-sig): line 1 column 1 (char 0)");
    return loads(b);
}
// urllib.parse.quote(s) (safe="/") and the parse_qs(...)[k][0] the offline mode needs
inline std::string quote(const std::string& s) {
    static const char* H = "0123456789ABCDEF"; std::string o;
    for (unsigned char c : s) {
        if ((c < 128 && std::isalnum(c)) || c == '_' || c == '.' || c == '-' || c == '~' || c == '/') o += static_cast<char>(c);
        else { o += '%'; o += H[c >> 4]; o += H[c & 15]; }
    }
    return o;
}
inline std::string unquote_plus(const std::string& s) {
    std::string o; const auto hexv = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::isxdigit(static_cast<unsigned char>(c)) ? (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10) : -1; };
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { o += static_cast<char>(hexv(s[i + 1]) * 16 + hexv(s[i + 2])); i += 2; }
        else o += s[i];
    }
    return decode_utf8_replace(o);
}
inline std::string query_first(const std::string& query, const std::string& key) {
    for (const auto& field : split(query, "&")) {
        const std::size_t eq = field.find('=');
        if (field.empty() || eq == std::string::npos || eq + 1 == field.size()) continue;
        if (unquote_plus(field.substr(0, eq)) == key) return unquote_plus(field.substr(eq + 1));
    }
    return "";
}
// ---------------------------------------------------------------- fetching
inline std::optional<std::string> user_agent(const Val& cli, const std::string& config_path) {
    std::string ua = strip(truthy(cli) ? cli.s : env_or("SEC_USER_AGENT"));
    if (ua.empty() && !config_path.empty() && is_file(config_path)) {
        try { const Val d = loads_bytes(read_text(config_path)); const Val v = dget(d, "sec_user_agent"); ua = strip(truthy(v) ? pystr(v) : std::string()); }
        catch (const OSError&) { ua = ""; } catch (const PyErr&) { ua = ""; }
    }
    if (ua.empty() || ua.find('@') == std::string::npos || ua.find(PLACEHOLDER) != std::string::npos) return std::nullopt;
    return ua;
}
struct Fetcher {
    std::optional<std::string> ua; std::string offline; double pause = 0.15;
    std::optional<std::chrono::steady_clock::time_point> last;
    std::string local_path(const std::string& url) const {
        std::string rest = url; const auto sch = rest.find("://"); if (sch != std::string::npos) rest = rest.substr(sch + 3);
        const auto cut = rest.find_first_of("/?#"); const std::string netloc = rest.substr(0, cut); rest = cut == std::string::npos ? "" : rest.substr(cut);
        const auto frag = rest.find('#'); if (frag != std::string::npos) rest = rest.substr(0, frag);
        const auto qm = rest.find('?'); const std::string path = rest.substr(0, qm), query = qm == std::string::npos ? "" : rest.substr(qm + 1);
        std::vector<std::string> parts; for (const auto& p : split(path, "/")) if (!p.empty()) parts.push_back(p);
        std::string name;
        if (netloc == "efts.sec.gov") {
            static const std::regex NON(R"([^a-z0-9]+)");
            std::string q = std::regex_replace(lower(query_first(query, "keysTyped")), NON, "-");
            std::size_t a = 0, b = q.size(); while (a < b && q[a] == '-') ++a; while (b > a && q[b - 1] == '-') --b;
            name = "search-" + q.substr(a, b - a) + ".json";
        } else if (std::find(parts.begin(), parts.end(), "Archives") != parts.end() && parts.size() >= 2) name = path_join(parts[parts.size() - 2], parts.back());
        else name = parts.empty() ? "index" : parts.back();
        return path_join(offline, name);
    }
    void wait() {
        const auto now = std::chrono::steady_clock::now();
        if (last) { const double gap = std::chrono::duration<double>(now - *last).count(); if (gap < pause) std::this_thread::sleep_for(std::chrono::duration<double>(pause - gap)); }
        last = std::chrono::steady_clock::now();
    }
    std::string open(const std::string& url, const std::vector<std::string>& headers, const std::string* post) {
        wait();
        const HttpResp r = post ? http_post(url, *post, headers, 60) : http_get(url, headers, 60);
        if (!r.error.empty() || r.status == 0) throw EdgarError("could not reach " + url + ": " + (r.error.empty() ? std::string("no response") : r.error));
        if (r.status < 200 || r.status >= 300) {
            if (r.status == 403 && url.find("sec.gov") != std::string::npos)
                throw EdgarError("SEC said 403 Forbidden for " + url + ". Usually a missing or generic User-Agent, or too many requests. " + UA_HELP);
            throw EdgarError("HTTP " + std::to_string(r.status) + " for " + url);
        }
        if (starts(r.body, "\x1f\x8b")) return zip::gunzip(r.body);   // the engine client decodes gzip itself; this covers other hooks
        return r.body;
    }
    std::string get(const std::string& url, bool sec = true) {
        if (!offline.empty()) {
            const std::string path = local_path(url);
            if (!is_file(path)) throw EdgarError("offline: no file " + path + " for " + url);
            return read_bytes(path);
        }
        if (sec && !ua) throw EdgarError(UA_HELP);
        return open(url, {"User-Agent: " + (ua ? *ua : std::string("astro-trader/1.0"))}, nullptr);
    }
    Val post_json(const std::string& url, const Val& payload, const std::vector<std::string>& extra) {
        if (!offline.empty()) {
            const std::string path = path_join(offline, "openfigi.json");
            if (!is_file(path)) throw EdgarError("offline: no file " + path + " for " + url);
            return loads_bytes(read_bytes(path));
        }
        std::vector<std::string> h = {"Content-Type: application/json", "User-Agent: astro-trader/1.0"};
        for (const auto& x : extra) h.push_back(x);
        const std::string body = dumps(payload);
        return loads_bytes(open(url, h, &body));
    }
    Val get_json(const std::string& url, bool sec = true) {
        const std::string data = get(url, sec);
        try { return loads_bytes(data); } catch (const PyErr& e) { throw EdgarError(url + " did not return JSON: " + e.what()); }
    }
};
// ---------------------------------------------------------------- filers
inline std::string cik10(const Val& value) {
    std::string digits; for (char c : pystr(value)) if (c >= '0' && c <= '9') digits += c;
    if (digits.empty()) throw EdgarError("not a CIK: " + repr(value));
    return digits.size() >= 10 ? digits : std::string(10 - digits.size(), '0') + digits;
}
inline List load_filers() { return iter(get(loads_bytes(read_text(skill_file("filers.json"))), "filers")); }
inline List find_bundled(const std::string& query, const List& filers) {
    const std::string q = strip(lower(query));
    static const std::regex CIK(R"(^\d{1,10}$)");
    List hits;
    if (std::regex_match(q, CIK)) { const std::string c = cik10(Val::str(q)); for (const auto& f : filers) if (pystr(get(f, "cik")) == c) hits.push_back(f); return hits; }
    const auto words = split_ws(q);
    for (const auto& f : filers) {
        std::vector<std::string> bits = {pystr(get(f, "name")), pystr(dget(f, "person", Val::str("")))};
        for (const auto& a : iter(dget(f, "aliases", Val::list()))) bits.push_back(pystr(a));
        const std::string hay = lower(join(bits, " "));
        if (std::all_of(words.begin(), words.end(), [&](const std::string& w) { return hay.find(w) != std::string::npos; })) hits.push_back(f);
    }
    return hits;
}
inline List search_sec(Fetcher& f, const std::string& query) {
    const Val data = f.get_json(SEARCH_URL + quote(query));
    List out;
    for (const auto& hit : iter(dget(dget(data, "hits", Val::dict()), "hits", Val::list()))) {
        const Val src = dget(hit, "_source", Val::dict());
        Val h = Val::dict(); h.d->set("cik", Val::str(cik10(dget(hit, "_id", Val::str("0"))))); h.d->set("name", dget(src, "entity", Val::str("")));
        out.push_back(h);
    }
    return out;
}
// ---------------------------------------------------------------- filings
inline List list_filings(const Val& sub) {
    const Val rec = dget(dget(sub, "filings", Val::dict()), "recent", Val::dict());
    const auto at = [&](const char* k, std::size_t i, bool opt) -> Val {
        if (opt && !has(rec, k)) return Val::str("");
        const Val l = get(rec, k);
        if (l.t != Val::LIST || i >= l.l->size()) throw PyErr("IndexError", "list index out of range");
        return (*l.l)[i];
    };
    List out; std::size_t i = 0;
    for (const auto& form : iter(dget(rec, "form", Val::list()))) {
        if (form.t == Val::STR && (form.s == "13F-HR" || form.s == "13F-HR/A")) {
            Val f = Val::dict();
            f.d->set("form", form); f.d->set("accession", at("accessionNumber", i, false)); f.d->set("filed", at("filingDate", i, false));
            f.d->set("period", at("reportDate", i, true)); f.d->set("primary", at("primaryDocument", i, true));
            out.push_back(f);
        }
        ++i;
    }
    std::stable_sort(out.begin(), out.end(), [](const Val& a, const Val& b) {
        return std::make_pair(pystr(get(a, "filed")), pystr(get(a, "accession"))) > std::make_pair(pystr(get(b, "filed")), pystr(get(b, "accession"))); });
    return out;
}
inline std::pair<Val, List> pick_filing(const List& filings, const std::string& period, const std::string& accession, const std::string& before) {
    Val chosen;
    if (!accession.empty()) {
        for (const auto& f : filings) if (pystr(get(f, "accession")) == accession) { chosen = f; break; }
        if (chosen.is_none()) throw EdgarError("accession " + accession + " is not in this filer's 13F list");
    } else {
        List originals;
        for (const auto& f : filings) {
            if (get(f, "form").s != "13F-HR") continue;
            if (!period.empty() && pystr(get(f, "period")) != period) continue;
            if (!before.empty() && !(pystr(get(f, "period")) < before)) continue;
            originals.push_back(f);
        }
        if (originals.empty()) throw EdgarError("no 13F-HR found" + (period.empty() ? std::string() : " for period " + period));
        std::stable_sort(originals.begin(), originals.end(), [](const Val& a, const Val& b) {
            return std::make_pair(pystr(get(a, "period")), pystr(get(a, "filed"))) > std::make_pair(pystr(get(b, "period")), pystr(get(b, "filed"))); });
        chosen = originals[0];
    }
    List amendments;
    for (const auto& f : filings)
        if (get(f, "form").s == "13F-HR/A" && pystr(get(f, "period")) == pystr(get(chosen, "period")) && f.d.get() != chosen.d.get()) amendments.push_back(f);
    return {chosen, amendments};
}
inline std::string archive_url(const Val& cik, const std::string& accession) {
    std::string n = cik10(cik); const auto nz = n.find_first_not_of('0'); n = nz == std::string::npos ? "0" : n.substr(nz);
    return "https://www.sec.gov/Archives/edgar/data/" + n + "/" + replace_all(accession, "-", "") + "/";
}
inline std::string info_table_name(const Val& index_json) {
    std::vector<std::string> names, xmls;
    for (const auto& it : iter(dget(dget(index_json, "directory", Val::dict()), "item", Val::list()))) names.push_back(pystr(get(it, "name")));
    for (const auto& n : names) { const std::string l = lower(n); if (ends(l, ".xml") && l != "primary_doc.xml" && l != "filingsummary.xml") xmls.push_back(n); }
    if (xmls.empty()) throw EdgarError("no information table XML in this filing (files: " + join(names, ", ") + "). Filings before mid 2013 are plain text and are not supported.");
    static const std::regex HINT("info|table|13f", std::regex::ECMAScript | std::regex::icase);
    for (const auto& n : xmls) if (std::regex_search(n, HINT)) return n;
    return xmls[0];
}
inline int value_multiplier(const Val& filed) {   // 1 for dollars (filed on or after 2023-01-03), 1000 before
    if (filed.t != Val::STR) return 1;
    static const std::regex YMD(R"(^(\d{4})-(1[0-2]|0[1-9]|[1-9])-(3[01]|[12]\d|0[1-9]|[1-9]| [1-9])$)", std::regex::ECMAScript | std::regex::icase);
    const std::string s = cpslice(filed.s, 0, 10); std::smatch m;
    if (!std::regex_match(s, m, YMD)) return 1;
    const int y = std::stoi(m[1].str()), mo = std::stoi(m[2].str()), d = std::stoi(strip(m[3].str()));
    if (!valid_ymd(y, mo, d)) return 1;
    return std::make_tuple(y, mo, d) >= std::make_tuple(2023, 1, 3) ? 1 : 1000;
}
// ---------------------------------------------------------------- parsing
inline List parse_info_table(const std::string& data, int multiplier) {
    std::shared_ptr<xml::Node> root;
    try { root = xml::fromstring(data); } catch (const xml::ParseError& e) { throw EdgarError(std::string("information table is not valid XML: ") + e.what()); }
    List rows;
    root->iter([&](const xml::Node& el) {
        if (xml::local(el.tag) != "infoTable") return;
        std::vector<std::pair<std::string, std::string>> f;
        const auto fget = [&](const std::string& k, const std::string& d) { for (const auto& [a, v] : f) if (a == k) return v; return d; };
        const auto fhas = [&](const std::string& k) { for (const auto& kv : f) if (kv.first == k) return true; return false; };
        el.iter([&](const xml::Node& c) { const std::string name = xml::local(c.tag), t = strip(c.text); if (!fhas(name) && !t.empty()) f.push_back({name, t}); });
        const auto v = parse_float(replace_all(fget("value", "0"), ",", "")), sh = parse_float(replace_all(fget("sshPrnamt", "0"), ",", ""));
        if (!v || !sh) throw EdgarError("bad number in row for " + (fhas("nameOfIssuer") ? fget("nameOfIssuer", "") : std::string("None")));
        Val r = Val::dict();
        r.d->set("issuer", Val::str(fget("nameOfIssuer", ""))); r.d->set("title", Val::str(fget("titleOfClass", "")));
        r.d->set("cusip", Val::str(upper(fget("cusip", "")))); r.d->set("figi", Val::str(fget("figi", "")));
        r.d->set("value", Val::flt(*v * multiplier)); r.d->set("shares", Val::flt(*sh));
        r.d->set("share_type", Val::str(upper(fget("sshPrnamtType", "SH")))); r.d->set("put_call", Val::str(upper(fget("putCall", ""))));
        r.d->set("manager", Val::str(fget("otherManager", "")));
        rows.push_back(r);
    });
    if (rows.empty()) throw EdgarError("no infoTable rows found. Is this a 13F information table?");
    return rows;
}
inline Val parse_cover(const std::string& data) {
    Val out = Val::dict();
    std::shared_ptr<xml::Node> root;
    try { root = xml::fromstring(data); } catch (const xml::ParseError&) { return out; }
    static const std::set<std::string> want = {"tableValueTotal", "tableEntryTotal", "periodOfReport", "isAmendment", "amendmentType", "reportType", "otherIncludedManagersCount", "name"};
    root->iter([&](const xml::Node& el) { const std::string n = xml::local(el.tag), t = strip(el.text); if (want.count(n) && !out.d->has(n) && !t.empty()) out.d->set(n, Val::str(t)); });
    return out;
}
inline bool is_stock(const Val& p) { return get(p, "share_type").s == "SH" && get(p, "put_call").s.empty(); }
inline Val dcopy(const Val& p) { Val q = Val::dict(); for (const auto& [k, v] : p.d->items) q.d->set(k, v); return q; }
inline double pct_of(double v, double total) { return total != 0 ? round_n(100.0 * v / total, 2) : 0.0; }
inline void sort_by_value(List& ps) {
    std::stable_sort(ps.begin(), ps.end(), [](const Val& a, const Val& b) {
        const double va = -get(a, "value").f, vb = -get(b, "value").f;
        if (va != vb) return va < vb;
        return get(a, "cusip").s < get(b, "cusip").s; });
}
inline std::pair<List, double> aggregate(const List& rows) {
    std::vector<double> vals; for (const auto& r : rows) vals.push_back(get(r, "value").f);
    const double total = rows.empty() ? 0.0 : fsum(vals);
    std::map<std::string, std::size_t> at; List positions;
    for (const auto& r : rows) {
        const std::string key = get(r, "cusip").s + '\x1f' + get(r, "put_call").s + '\x1f' + get(r, "share_type").s;
        auto it = at.find(key);
        if (it == at.end()) {
            Val g = Val::dict();
            for (const char* k : {"issuer", "title", "cusip", "figi", "put_call", "share_type"}) g.d->set(k, get(r, k));
            g.d->set("value", Val::flt(0.0)); g.d->set("shares", Val::flt(0.0)); g.d->set("rows", Val::integer(0));
            it = at.emplace(key, positions.size()).first; positions.push_back(g);
        }
        Val& g = positions[it->second];
        g.d->set("value", Val::flt(get(g, "value").f + get(r, "value").f));
        g.d->set("shares", Val::flt(get(g, "shares").f + get(r, "shares").f));
        g.d->set("rows", Val::integer(get(g, "rows").i + 1));
    }
    for (auto& p : positions) { p.d->set("pct_of_13f", Val::flt(pct_of(get(p, "value").f, total))); p.d->set("cusips", Val::list({get(p, "cusip")})); }
    sort_by_value(positions);
    return {positions, total};
}
inline List combine_classes(const List& positions, double total) {
    List out; std::map<std::string, std::size_t> by_issuer;
    for (const auto& p : positions) {
        if (!is_stock(p)) { out.push_back(dcopy(p)); continue; }
        const std::string key = cpslice(get(p, "cusip").s, 0, 6);
        const auto it = by_issuer.find(key);
        if (it == by_issuer.end()) {
            Val q = dcopy(p);
            q.d->set("cusips", Val::list({get(p, "cusip")}));
            q.d->set("tickers", truthy(dget(p, "ticker")) ? Val::list({get(p, "ticker")}) : Val::list());
            q.d->set("classes", Val::list({get(p, "title")}));
            by_issuer[key] = out.size(); out.push_back(q); continue;
        }
        Val& q = out[it->second];
        q.d->set("value", Val::flt(get(q, "value").f + get(p, "value").f));
        q.d->set("shares", Val::flt(get(q, "shares").f + get(p, "shares").f));
        q.d->set("rows", Val::integer(get(q, "rows").i + get(p, "rows").i));
        get(q, "cusips").l->push_back(get(p, "cusip")); get(q, "classes").l->push_back(get(p, "title"));
        if (truthy(dget(p, "ticker"))) get(q, "tickers").l->push_back(get(p, "ticker"));
    }
    for (auto& q : out) q.d->set("pct_of_13f", Val::flt(pct_of(get(q, "value").f, total)));
    sort_by_value(out);
    return out;
}
// ---------------------------------------------------------------- tickers
inline const std::regex& SUFFIX_STATE() { static const std::regex r(R"(\s*/\s*[A-Z]{2,5}\s*/?\s*$)"); return r; }
inline const std::regex& NONCOMMON() { static const std::regex r(R"(-P[A-Z]*$|-WT[A-Z]*$|-WS$|-U$|-UN$|-R$|-RT$)"); return r; }
inline const std::map<std::string, std::string>& ABBREV() {
    static const std::map<std::string, std::string> m = {
        {"AMER", "AMERICA"}, {"AMERN", "AMERICAN"}, {"FINL", "FINANCIAL"}, {"FIN", "FINANCIAL"}, {"PETE", "PETROLEUM"}, {"HLDGS", "HOLDINGS"},
        {"HLDG", "HOLDING"}, {"HOLDG", "HOLDING"}, {"INTL", "INTERNATIONAL"}, {"GRP", "GROUP"}, {"SVCS", "SERVICES"}, {"SVC", "SERVICE"},
        {"SYS", "SYSTEMS"}, {"MGMT", "MANAGEMENT"}, {"INDS", "INDUSTRIES"}, {"NATL", "NATIONAL"}, {"ENTMT", "ENTERTAINMENT"}, {"PAC", "PACIFIC"},
        {"LABS", "LABORATORIES"}, {"PPTYS", "PROPERTIES"}, {"RLTY", "REALTY"}, {"TECHS", "TECHNOLOGIES"}, {"TECH", "TECHNOLOGY"},
        {"COMMNS", "COMMUNICATIONS"}, {"COMMUNICATN", "COMMUNICATIONS"}, {"MFG", "MANUFACTURING"}, {"ELEC", "ELECTRIC"},
        {"PHARMACEUTICALS", "PHARMACEUTICALS"}, {"PHARMS", "PHARMACEUTICALS"}, {"BANCORPORATION", "BANCORP"}, {"RES", "RESOURCES"},
        {"SOLUTIONS", "SOLUTIONS"}, {"WTR", "WATER"}, {"MTRS", "MOTORS"}, {"AIRLS", "AIRLINES"}, {"CHEM", "CHEMICAL"}};
    return m;
}
inline const std::set<std::string>& DROP() {
    static const std::set<std::string> s = {"INC", "INCORPORATED", "CORP", "CORPORATION", "CO", "COMPANY", "LTD", "LIMITED", "PLC", "LLC", "LP", "SA", "NV", "AG", "SE", "THE", "AND", "OF"};
    return s;
}
inline std::vector<std::string> norm_name(const std::string& name) {
    std::string s = replace_all(upper(name), "&", " AND ");
    for (int k = 0; k < 2; ++k) s = std::regex_replace(s, SUFFIX_STATE(), "");
    s = replace_all(replace_all(s, "'", ""), "\xE2\x80\x99", "");
    static const std::regex NON(R"([^A-Z0-9 ]+)");
    s = std::regex_replace(s, NON, " ");
    std::vector<std::string> out;
    for (auto w : split_ws(s)) { const auto it = ABBREV().find(w); if (it != ABBREV().end()) w = it->second; if (!DROP().count(w)) out.push_back(w); }
    return out;
}
inline std::pair<std::string, std::string> name_keys(const std::string& name) {
    auto words = norm_name(name); const std::string a = join(words, "");
    std::sort(words.begin(), words.end()); return {a, join(words, " ")};
}
inline std::string norm_ticker(const std::string& t0) {
    const std::string t = upper(strip(t0));
    static const std::regex CLS(R"([-/ ]([A-Z]{1,2})$)");
    return std::regex_search(t, NONCOMMON()) ? t : std::regex_replace(t, CLS, ".$1");
}
inline std::optional<std::string> class_letter(const std::string& title) {
    static const std::regex R(R"(\b(?:CL|CLASS|SER|SERIES)\s*([A-Z])\b)"); std::smatch m; const std::string u = upper(title);
    if (std::regex_search(u, m, R)) return m[1].str();
    return std::nullopt;
}
struct CikHit { std::optional<std::string> cik; std::string how; };
struct TickerIndex {
    std::map<std::string, std::vector<std::string>> by_cik;
    std::map<std::string, std::string> titles;
    std::vector<std::pair<std::string, std::set<std::string>>> spaceless; std::map<std::string, std::size_t> spaceless_at;
    std::map<std::string, std::set<std::string>> sorted_words;
    explicit TickerIndex(const std::vector<std::array<std::string, 3>>& rows) {
        for (const auto& [cik, ticker, title] : rows) {
            by_cik[cik].push_back(ticker);
            if (titles.count(cik)) continue;
            titles[cik] = title;
            const auto [a, b] = name_keys(title);
            if (!a.empty()) {
                auto it = spaceless_at.find(a);
                if (it == spaceless_at.end()) { it = spaceless_at.emplace(a, spaceless.size()).first; spaceless.push_back({a, {}}); }
                spaceless[it->second].second.insert(cik); sorted_words[b].insert(cik);
            }
        }
    }
    CikHit match_cik(const std::string& issuer) const {
        const auto [a, b] = name_keys(issuer);
        if (a.empty()) return {};
        if (const auto it = spaceless_at.find(a); it != spaceless_at.end() && spaceless[it->second].second.size() == 1) return {*spaceless[it->second].second.begin(), "name"};
        if (const auto it = sorted_words.find(b); it != sorted_words.end() && it->second.size() == 1) return {*it->second.begin(), "name"};
        // The 13F truncates or decorates names ("JEFFERIES FINANCIAL GROUP IN").
        std::vector<std::pair<std::size_t, std::string>> best;
        for (const auto& [key, ciks] : spaceless)
            if (key.size() >= 8 && ciks.size() == 1 && (starts(a, key) || starts(key, a))) best.push_back({key.size(), *ciks.begin()});
        if (!best.empty()) {
            std::sort(best.begin(), best.end(), std::greater<>());
            if (best.size() == 1 || best[0].first > best[1].first) return {best[0].second, "name-prefix"};
        }
        return {};
    }
    std::pair<std::optional<std::string>, bool> ticker_for(const std::string& cik, const std::string& title_of_class) const {
        static const std::vector<std::string> none;
        const auto f = by_cik.find(cik); const auto& tickers = f == by_cik.end() ? none : f->second;
        std::vector<std::string> cands; for (const auto& t : tickers) if (!std::regex_search(t, NONCOMMON())) cands.push_back(t);
        if (cands.empty()) cands = tickers;
        if (cands.empty()) return {std::nullopt, true};
        if (cands.size() == 1) return {norm_ticker(cands[0]), false};
        if (std::all_of(cands.begin() + 1, cands.end(), [&](const std::string& t) { return starts(t, cands[0]); })) return {norm_ticker(cands[0]), false};
        if (const auto letter = class_letter(title_of_class)) {
            for (const auto& t : cands) { const std::string u = upper(t); if (ends(u, "-" + *letter) || ends(u, "." + *letter) || ends(u, "/" + *letter)) return {norm_ticker(t), false}; }
            for (const auto& t : cands) if (cplen(t) >= 4 && ends(upper(t), *letter)) return {norm_ticker(t), true};
        }
        return {norm_ticker(cands[0]), true};
    }
};
inline std::vector<std::array<std::string, 3>> load_company_tickers(const Val& data) {
    std::vector<std::array<std::string, 3>> rows;
    for (const auto& v : iter(data.t == Val::DICT ? [&] { Val l = Val::list(); for (const auto& kv : data.d->items) l.l->push_back(kv.second); return l; }() : data))
        rows.push_back({cik10(get(v, "cik_str")), pystr(get(v, "ticker")), pystr(get(v, "title"))});
    return rows;
}
inline std::map<std::string, std::string> load_overrides(const std::vector<std::string>& paths) {
    std::map<std::string, std::string> out; static const std::regex CUSIP(R"([0-9A-Z]{9})");
    for (const auto& path : paths) {
        if (path.empty() || !is_file(path)) continue;
        const Val data = loads_bytes(read_text(path));
        const Val table = data.t == Val::DICT ? dget(data, "cusips", data) : Val::dict();
        for (const auto& [k, v] : table.d->items) {
            const std::string K = upper(k);
            if (std::regex_match(K, CUSIP)) out[K] = norm_ticker(truthy(v) ? pystr(v) : std::string());
        }
    }
    return out;
}
inline std::map<std::string, std::string> parse_figi(const std::vector<std::string>& cusips, const Val& answer) {
    std::map<std::string, std::string> out; const List ans = iter(answer);
    for (std::size_t i = 0; i < cusips.size() && i < ans.size(); ++i) {
        const Val data = truthy(ans[i]) ? orv(dget(ans[i], "data"), Val::list()) : Val::list();
        List stocks; for (const auto& d : iter(data)) if (dget(d, "marketSector").t == Val::STR && dget(d, "marketSector").s == "Equity") stocks.push_back(d);
        if (stocks.empty()) stocks = iter(data);
        if (!stocks.empty() && truthy(dget(stocks[0], "ticker"))) out[cusips[i]] = norm_ticker(pystr(get(stocks[0], "ticker")));
    }
    return out;
}
inline std::map<std::string, std::string> figi_lookup(Fetcher& f, const std::vector<std::string>& cusips0, const std::string& api_key, double pause = 2.5) {
    const std::size_t batch = api_key.empty() ? 10 : 100;
    std::vector<std::string> extra; if (!api_key.empty()) extra.push_back("X-OPENFIGI-APIKEY: " + api_key);
    std::vector<std::string> cusips; std::set<std::string> seen; for (const auto& c : cusips0) if (seen.insert(c).second) cusips.push_back(c);
    std::map<std::string, std::string> out;
    for (std::size_t i = 0; i < cusips.size(); i += batch) {
        const std::vector<std::string> chunk(cusips.begin() + static_cast<long>(i), cusips.begin() + static_cast<long>(std::min(cusips.size(), i + batch)));
        Val jobs = Val::list();
        for (const auto& c : chunk) { Val j = Val::dict(); j.d->set("idType", Val::str("ID_CUSIP")); j.d->set("idValue", Val::str(c)); j.d->set("exchCode", Val::str("US")); jobs.l->push_back(j); }
        if (i && f.offline.empty()) std::this_thread::sleep_for(std::chrono::duration<double>(pause));
        for (const auto& [k, v] : parse_figi(chunk, f.post_json(FIGI_URL, jobs, extra))) out[k] = v;
    }
    return out;
}
inline bool py_isupper(const std::string& s) { return upper(s) == s && lower(s) != s; }
inline std::string capitalize(const std::string& w) { return w.empty() ? w : upper(cpslice(w, 0, 1)) + lower(cpslice(w, 1)); }
inline std::string pretty_name(const std::string& issuer, const std::optional<std::string>& sec_title) {
    std::string name = sec_title && !sec_title->empty() ? *sec_title : issuer;
    name = std::regex_replace(name, SUFFIX_STATE(), "");
    static const std::regex CORP_I(R"(,?\s+(Inc\.?|Incorporated|Corp\.?|Corporation|Co\.?|Company|Ltd\.?|Limited|plc|PLC|LLC|L\.P\.|N\.V\.|S\.A\.)$)", std::regex::ECMAScript | std::regex::icase);
    static const std::regex CORP(R"(,?\s+(INC|CORP|CO|LTD|PLC)$)");
    name = std::regex_replace(strip(name), CORP_I, "");
    name = std::regex_replace(strip(name), CORP, "");
    if (py_isupper(name)) {
        static const std::set<std::string> small = {"OF", "AND", "THE", "FOR", "IN"};
        const auto words = split_ws(lower(name)); std::vector<std::string> o;
        for (std::size_t i = 0; i < words.size(); ++i) o.push_back(small.count(upper(words[i])) && i ? words[i] : capitalize(words[i]));
        name = join(o, " ");
    }
    return strip(name);
}
inline void resolve_tickers(List& positions, const TickerIndex* index, const std::map<std::string, std::string>& overrides, const std::map<std::string, std::string>& figi) {
    for (auto& p : positions) {
        const std::string c = get(p, "cusip").s;
        std::optional<std::string> sec_title; CikHit hit;
        if (index) {
            hit = index->match_cik(get(p, "issuer").s);
            if (hit.cik) { sec_title = index->titles.at(*hit.cik); p.d->set("issuer_cik", Val::str(*hit.cik)); }
        }
        const auto tick = [&](const std::optional<std::string>& t, const std::string& src, bool check) {
            p.d->set("ticker", t ? Val::str(*t) : Val()); p.d->set("ticker_source", Val::str(src)); p.d->set("ticker_check", Val::boolean(check)); };
        if (const auto o = overrides.find(c); o != overrides.end()) tick(o->second, "override", false);
        else if (const auto g = figi.find(c); g != figi.end()) tick(g->second, "openfigi", false);
        else if (hit.cik) { const auto [t, check] = index->ticker_for(*hit.cik, get(p, "title").s); tick(t, hit.how, check || hit.how == "name-prefix"); }
        else tick(std::nullopt, "none", true);
        p.d->set("name", Val::str(pretty_name(get(p, "issuer").s, sec_title)));
    }
}
// ---------------------------------------------------------------- analysis
inline List diff(const List& current, const List& prior) {
    using Key = std::pair<std::string, std::string>;
    const auto index = [](const List& ps) {   // a dict comprehension: the last row wins, the first one keeps its place
        std::vector<std::pair<Key, Val>> v; std::map<Key, std::size_t> at;
        for (const auto& p : ps) { const Key k{get(p, "cusip").s, get(p, "put_call").s}; const auto it = at.find(k); if (it == at.end()) { at[k] = v.size(); v.push_back({k, p}); } else v[it->second].second = p; }
        return std::make_pair(v, at);
    };
    const auto [before, bat] = index(prior); const auto [now, nat] = index(current);
    List out;
    for (const auto& [key, p] : now) {
        const auto bi = bat.find(key); const Val q = bi == bat.end() ? Val() : before[bi->second].second;
        const double ps = get(p, "shares").f; std::string status; double change;
        if (q.is_none()) { status = "NEW"; change = ps; }
        else if (ps > get(q, "shares").f) { status = "ADDED"; change = ps - get(q, "shares").f; }
        else if (ps < get(q, "shares").f) { status = "TRIMMED"; change = ps - get(q, "shares").f; }
        else { status = "UNCHANGED"; change = 0.0; }
        Val d = Val::dict();
        d.d->set("status", Val::str(status)); d.d->set("issuer", get(p, "issuer")); d.d->set("title", get(p, "title")); d.d->set("cusip", get(p, "cusip"));
        d.d->set("ticker", dget(p, "ticker")); d.d->set("put_call", get(p, "put_call"));
        d.d->set("shares_before", q.is_none() ? Val::flt(0.0) : get(q, "shares")); d.d->set("shares_after", get(p, "shares")); d.d->set("change", Val::flt(change));
        d.d->set("change_pct", !q.is_none() && get(q, "shares").f != 0 ? Val::flt(round_n(100.0 * change / get(q, "shares").f, 1)) : Val());
        d.d->set("value_after", get(p, "value")); d.d->set("value_before", q.is_none() ? Val::flt(0.0) : get(q, "value"));
        out.push_back(d);
    }
    for (const auto& [key, q] : before) {
        if (nat.count(key)) continue;
        Val d = Val::dict();
        d.d->set("status", Val::str("SOLD OUT")); d.d->set("issuer", get(q, "issuer")); d.d->set("title", get(q, "title")); d.d->set("cusip", get(q, "cusip"));
        d.d->set("ticker", dget(q, "ticker")); d.d->set("put_call", get(q, "put_call")); d.d->set("shares_before", get(q, "shares"));
        d.d->set("shares_after", Val::flt(0.0)); d.d->set("change", Val::flt(-get(q, "shares").f)); d.d->set("change_pct", Val::flt(-100.0));
        d.d->set("value_after", Val::flt(0.0)); d.d->set("value_before", get(q, "value"));
        out.push_back(d);
    }
    static const std::map<std::string, int> rank = {{"NEW", 0}, {"SOLD OUT", 1}, {"ADDED", 2}, {"TRIMMED", 3}, {"UNCHANGED", 4}};
    std::stable_sort(out.begin(), out.end(), [](const Val& a, const Val& b) {
        const int ra = rank.at(get(a, "status").s), rb = rank.at(get(b, "status").s);
        if (ra != rb) return ra < rb;
        return -std::max(get(a, "value_after").f, get(a, "value_before").f) < -std::max(get(b, "value_after").f, get(b, "value_before").f); });
    return out;
}
inline List py_head(const List& l, long long n) {   // l[:n]
    const long long sz = static_cast<long long>(l.size()); const long long e = n >= 0 ? std::min(n, sz) : std::max(0LL, sz + n);
    return List(l.begin(), l.begin() + e);
}
inline List py_tail(const List& l, long long n) {   // l[n:]
    const long long sz = static_cast<long long>(l.size()); const long long b = n >= 0 ? std::min(n, sz) : std::max(0LL, sz + n);
    return List(l.begin() + b, l.end());
}
inline std::pair<List, List> copy_weights(const List& positions, long long top, double min_pct, const std::vector<std::string>& exclude0) {
    std::set<std::string> exclude; for (const auto& e : exclude0) exclude.insert(upper(e));
    List chosen, skipped;
    const auto skip = [&](const Val& p, const std::string& why) { Val s = Val::dict(); s.d->set("issuer", get(p, "issuer")); s.d->set("ticker", dget(p, "ticker")); s.d->set("why", Val::str(why)); skipped.push_back(s); };
    for (const auto& p : positions) {
        std::string why;
        if (!is_stock(p)) why = (get(p, "put_call").s.empty() ? get(p, "share_type").s : get(p, "put_call").s) + " row, not a stock you can copy";
        else if (!truthy(dget(p, "ticker"))) why = "no ticker found (add it to trader/tickers.json or run with --figi)";
        else if (exclude.count(upper(get(p, "ticker").s))) why = "excluded by you";
        else if (get(p, "pct_of_13f").f < min_pct) why = "below --min-pct " + fmt_f(min_pct, 2);
        if (!why.empty()) { skip(p, why); continue; }
        chosen.push_back(p);
    }
    if (top) { for (const auto& p : py_tail(chosen, top)) skip(p, "outside the top " + std::to_string(top)); chosen = py_head(chosen, top); }
    std::vector<double> pcts; for (const auto& p : chosen) pcts.push_back(get(p, "pct_of_13f").f);
    const double base = chosen.empty() ? 0.0 : fsum(pcts);
    if (chosen.empty() || base <= 0) throw EdgarError("nothing left to copy");
    List weights;
    for (const auto& p : chosen) {
        Val w = Val::dict();
        w.d->set("ticker", get(p, "ticker")); w.d->set("name", truthy(dget(p, "name")) ? get(p, "name") : get(p, "issuer"));
        w.d->set("weight", Val::flt(round_n(100.0 * get(p, "pct_of_13f").f / base, 2)));
        w.d->set("pct_of_13f", get(p, "pct_of_13f")); w.d->set("value", get(p, "value")); w.d->set("shares", get(p, "shares"));
        w.d->set("cusips", has(p, "cusips") ? get(p, "cusips") : Val::list({get(p, "cusip")}));
        if (truthy(dget(p, "tickers")) && get(p, "tickers").l->size() > 1) w.d->set("classes", get(p, "tickers"));
        if (truthy(dget(p, "ticker_check"))) w.d->set("check_ticker", Val::boolean(true));
        weights.push_back(w);
    }
    return {weights, skipped};
}
inline const std::vector<std::string> CAVEATS = {
    "A 13F is a snapshot of the last day of the quarter, filed up to 45 days later. The investor may have sold or added since.",
    "It shows long positions in US-listed stocks only: no shorts, cash, bonds, foreign stocks, private companies or trades made inside the quarter.",
    "It is filed by the firm, not by the person it is known for.",
    "Education and tooling, not financial advice. Nothing here places an order."};
// ---------------------------------------------------------------- output
inline std::string money(double v) { return "$" + fmt_fc(v, 0); }
inline std::string shares_s(double v) { return std::isfinite(v) && v == std::floor(v) ? fmt_fc(v, 0) : fmt_fc(v, 2); }
inline std::string mget(const Val& meta, const char* k, const char* dflt) { return has(meta, k) ? pystr(get(meta, k)) : std::string(dflt); }
inline std::string render_holdings(const Val& meta, const List& positions, double total, long long top, bool combined) {
    std::vector<std::string> out;
    out.push_back(mget(meta, "filer", "?") + "  13F-HR for quarter ending " + mget(meta, "period", "?") + ", filed " + mget(meta, "filed", "?") + " (accession " + mget(meta, "accession", "?") + ")");
    out.push_back("Total " + money(total) + " across " + std::to_string(positions.size()) + " positions" + (combined ? ", share classes combined" : "") + ". Weights are % of that total.");
    if (truthy(dget(meta, "total_check"))) out.push_back(pystr(get(meta, "total_check")));
    out.push_back("");
    out.push_back(rjust("#", 3) + "  " + ljust("ticker", 11) + " " + ljust("issuer", 32) + " " + rjust("shares", 16) + " " + rjust("value", 18) + " " + rjust("%", 7));
    const List shown = top ? py_head(positions, top) : positions;
    std::size_t i = 0; bool any_check = false;
    for (const auto& p : shown) {
        ++i;
        std::string tick = truthy(dget(p, "ticker")) ? pystr(get(p, "ticker")) : "?";
        if (truthy(dget(p, "tickers")) && get(p, "tickers").l->size() > 1) { std::vector<std::string> ts; for (const auto& t : *get(p, "tickers").l) ts.push_back(pystr(t)); tick = join(ts, "+"); }
        const bool chk = truthy(dget(p, "ticker_check")); any_check = any_check || chk;
        const std::string kind = is_stock(p) ? "" : " [" + (get(p, "put_call").s.empty() ? get(p, "share_type").s : get(p, "put_call").s) + "]";
        out.push_back(rjust(std::to_string(i), 3) + "  " + ljust(tick, 11) + " " + ljust(cpslice(get(p, "issuer").s + kind, 0, 32), 32) + " " + rjust(shares_s(get(p, "shares").f), 16) + " " +
                      rjust(money(get(p, "value").f), 18) + " " + rjust(fmt_f(get(p, "pct_of_13f").f, 2), 7) + (chk ? " *" : ""));
    }
    if (top && static_cast<long long>(positions.size()) > top) out.push_back("     ... " + std::to_string(static_cast<long long>(positions.size()) - top) + " more");
    if (any_check) { out.push_back(""); out.push_back("* ticker matched by name or share class guess: confirm it before you copy, or run with --figi."); }
    for (const auto& note : iter(dget(meta, "notes", Val::list()))) { out.push_back(""); out.push_back(pystr(note)); }
    out.push_back("");
    out.push_back("Holdings as of " + mget(meta, "period", "the quarter end") + ". A 13F is quarterly and up to 45 days late, long US equity positions only.");
    return join(out, "\n") + "\n";
}
inline std::string render_diff(const Val& meta, const List& rows) {
    std::vector<std::string> out = {mget(meta, "filer", "?") + ": " + mget(meta, "period", "?") + " vs " + mget(meta, "prior_period", "?"), ""};
    for (const char* status : {"NEW", "SOLD OUT", "ADDED", "TRIMMED", "UNCHANGED"}) {
        List group; for (const auto& r : rows) if (get(r, "status").s == status) group.push_back(r);
        if (group.empty()) continue;
        out.push_back(std::string(status) + " (" + std::to_string(group.size()) + ")");
        for (const auto& r : group) {
            const std::string tick = truthy(dget(r, "ticker")) ? pystr(get(r, "ticker")) : "?";
            const std::string label = ljust(cpslice(get(r, "issuer").s + " " + get(r, "title").s, 0, 40), 40);
            if (std::string(status) == "UNCHANGED") out.push_back("  " + ljust(tick, 7) + " " + label + " " + shares_s(get(r, "shares_after").f) + " shares");
            else if (std::string(status) == "SOLD OUT") out.push_back("  " + ljust(tick, 7) + " " + label + " " + shares_s(get(r, "shares_before").f) + " shares (" + money(get(r, "value_before").f) + " at the prior quarter end)");
            else {
                const std::string pct = get(r, "change_pct").is_none() ? "" : " (" + fmt_fs(get(r, "change_pct").f, 1) + "%)";
                out.push_back("  " + ljust(tick, 7) + " " + label + " " + shares_s(get(r, "shares_before").f) + " -> " + shares_s(get(r, "shares_after").f) + pct);
            }
        }
        out.push_back("");
    }
    out.push_back("Share counts at two quarter ends. Trades inside the quarter, and their prices, are not in a 13F.");
    return join(out, "\n") + "\n";
}
// ---------------------------------------------------------------- the flow
struct Quarter { Val meta; List rows, filings; };
inline Quarter load_quarter(Fetcher& f, const Val& cik, const std::string& period, const std::string& accession, const std::string& before) {
    const Val sub = f.get_json(SUBMISSIONS + cik10(cik) + ".json");
    const List filings = list_filings(sub);
    if (filings.empty()) throw EdgarError(pystr(dget(sub, "name")) + " (CIK " + cik10(cik) + ") has no 13F-HR filings in its recent list. Is it a 13F filer? Try: edgar13f find NAME --sec");
    const auto [chosen, amendments] = pick_filing(filings, period, accession, before);
    const std::string base = archive_url(cik, pystr(get(chosen, "accession")));
    const std::string table = info_table_name(f.get_json(base + "index.json"));
    const int mult = value_multiplier(get(chosen, "filed"));
    Quarter q; q.filings = filings; q.rows = parse_info_table(f.get(base + table), mult);
    Val m = Val::dict();
    m.d->set("filer", dget(sub, "name")); m.d->set("cik", Val::str(cik10(cik))); m.d->set("period", get(chosen, "period")); m.d->set("filed", get(chosen, "filed"));
    m.d->set("accession", get(chosen, "accession")); m.d->set("form", get(chosen, "form")); m.d->set("info_table_url", Val::str(base + table)); m.d->set("notes", Val::list());
    Val cover = Val::dict();
    try { cover = parse_cover(f.get(base + "primary_doc.xml")); } catch (const EdgarError&) {}
    if (truthy(dget(cover, "tableValueTotal"))) {
        const std::string t = get(cover, "tableValueTotal").s; const auto v = parse_float(t);
        if (!v) throw PyErr("ValueError", "could not convert string to float: " + str_repr(t));
        m.d->set("cover_total", Val::flt(*v * mult));
    }
    if (!amendments.empty()) {
        std::vector<std::string> bits; for (const auto& a : amendments) bits.push_back(pystr(get(a, "accession")) + " on " + pystr(get(a, "filed")));
        get(m, "notes").l->push_back(Val::str("Also filed for this quarter: " + join(bits, ", ") + ". An amendment can restate the table or add holdings that were kept confidential. Read it with --accession."));
    }
    q.meta = m;
    return q;
}
inline std::pair<List, double> build(Val& meta, const List& rows, const TickerIndex* index, const std::map<std::string, std::string>& overrides, const std::map<std::string, std::string>& figi, bool combine) {
    auto [positions, total] = aggregate(rows);
    resolve_tickers(positions, index, overrides, figi);
    if (truthy(dget(meta, "cover_total"))) {
        const double ct = get(meta, "cover_total").f, gap = std::fabs(ct - total);
        meta.d->set("total_check", Val::str(gap < 1000 ? "Matches the cover page total." : "Cover page says " + money(ct) + "; the rows add up to " + money(total) + "."));
    }
    if (combine) positions = combine_classes(positions, total);
    return {positions, total};
}
inline std::unique_ptr<TickerIndex> ticker_index(Fetcher& f, const std::string& path) {
    if (!path.empty()) return std::make_unique<TickerIndex>(load_company_tickers(loads_bytes(read_text(path))));
    return std::make_unique<TickerIndex>(load_company_tickers(f.get_json(TICKERS_URL)));
}
inline Fetcher make_fetcher(const Val& a, bool sec) {
    const auto ua = user_agent(get(a, "user_agent"), S(a, "config"));
    if (sec && !ua && !truthy(get(a, "offline"))) throw EdgarError(UA_HELP);
    Fetcher f; f.ua = ua; f.offline = truthy(get(a, "offline")) ? S(a, "offline") : std::string(); return f;
}
inline std::vector<std::string> strs(const Val& v) { std::vector<std::string> o; for (const auto& x : iter(orv(v, Val::list()))) o.push_back(pystr(x)); return o; }
inline long long top_of(const Val& a) { const Val t = dget(a, "top"); return t.is_none() ? 0 : t.i; }
inline int run(const Val& a) {
    const std::string cmd = S(a, "cmd"); DumpOpts J; J.indent = 2;
    if (cmd == "find") {
        const std::string query = join(strs(get(a, "query")), " ");
        List hits; for (const auto& f : find_bundled(query, load_filers())) { Val h = dcopy(f); h.d->set("source", Val::str("bundled")); hits.push_back(h); }
        if (B(a, "sec")) {
            Fetcher f = make_fetcher(a, true);
            std::set<std::string> known; for (const auto& h : hits) known.insert(pystr(get(h, "cik")));
            for (const auto& h : search_sec(f, query)) if (!known.count(get(h, "cik").s)) { Val x = dcopy(h); x.d->set("source", Val::str("sec")); hits.push_back(x); }
        }
        if (B(a, "json")) print(dumps(Val::list(hits), J));
        else if (hits.empty()) print("No filer matched " + str_repr(query) + ". Try --sec to search every EDGAR filer, or a shorter name.");
        else for (const auto& h : hits) {
            const std::string extra = truthy(dget(h, "person")) ? pystr(get(h, "person")) : "";
            print(pystr(get(h, "cik")) + "  " + pystr(get(h, "name")) + (extra.empty() ? "" : "  (" + extra + ")") + (truthy(dget(h, "status")) ? "  [" + pystr(get(h, "status")) + "]" : ""));
        }
        return hits.empty() ? 1 : 0;
    }
    const bool has_xml = truthy(dget(a, "xml"));
    Fetcher fetcher = make_fetcher(a, !(has_xml && truthy(dget(a, "tickers")) && !truthy(dget(a, "figi"))));
    if (cmd == "filings") {
        const Val sub = fetcher.get_json(SUBMISSIONS + cik10(get(a, "cik")) + ".json");
        const List rows = py_head(list_filings(sub), get(a, "limit").i);
        if (B(a, "json")) { Val o = Val::dict(); o.d->set("filer", dget(sub, "name")); o.d->set("cik", Val::str(cik10(get(a, "cik")))); o.d->set("filings", Val::list(rows)); print(dumps(o, J)); }
        else {
            print(pystr(dget(sub, "name")) + " (CIK " + cik10(get(a, "cik")) + ")");
            for (const auto& f : rows) print("  " + ljust(pystr(get(f, "form")), 9) + " quarter " + pystr(get(f, "period")) + "  filed " + pystr(get(f, "filed")) + "  " + pystr(get(f, "accession")));
            if (rows.empty()) print("  no 13F filings");
        }
        return rows.empty() ? 1 : 0;
    }
    // holdings / diff / copy: _positions()
    const int file_mult = B(a, "thousands") ? 1000 : 1;
    Val meta; List rows;
    if (has_xml) {
        rows = parse_info_table(read_bytes(S(a, "xml")), file_mult);
        meta = Val::dict(); meta.d->set("filer", Val::str(basename(S(a, "xml")))); meta.d->set("period", Val::str(truthy(get(a, "period")) ? S(a, "period") : "?"));
        meta.d->set("filed", Val::str("?")); meta.d->set("accession", Val::str("?")); meta.d->set("notes", Val::list());
    } else {
        if (!truthy(get(a, "cik"))) throw EdgarError("give a CIK, or --xml FILE");
        Quarter q = load_quarter(fetcher, get(a, "cik"), S(a, "period"), S(a, "accession"), ""); meta = q.meta; rows = q.rows;
    }
    std::unique_ptr<TickerIndex> index;
    if (!B(a, "no_tickers")) {
        try { index = ticker_index(fetcher, S(a, "tickers")); }
        catch (const EdgarError& e) { get(meta, "notes").l->push_back(Val::str(std::string("Tickers by name skipped: ") + e.what())); }
    }
    const auto overrides = load_overrides({skill_file("share_classes.json"), S(a, "map")});
    std::map<std::string, std::string> figi;
    if (B(a, "figi")) {
        auto positions = aggregate(rows).first; resolve_tickers(positions, index.get(), overrides, {});
        std::vector<std::string> unsure; for (const auto& p : positions) if (truthy(dget(p, "ticker_check")) || !truthy(dget(p, "ticker"))) unsure.push_back(get(p, "cusip").s);
        if (!unsure.empty()) figi = figi_lookup(fetcher, unsure, S(a, "figi_key"));
    }
    const bool combine = B(a, "combine") || (cmd == "copy" && !B(a, "separate"));
    const auto [positions, total] = build(meta, rows, index.get(), overrides, figi, combine);
    const long long top = top_of(a);
    if (cmd == "holdings") {
        if (B(a, "json")) { Val o = Val::dict(); o.d->set("meta", meta); o.d->set("total", Val::flt(total)); o.d->set("positions", Val::list(top ? py_head(positions, top) : positions)); print(dumps(o, J)); }
        else out(render_holdings(meta, positions, total, top, combine));
        return 0;
    }
    if (cmd == "diff") {
        List prior_rows;
        if (has_xml) {
            if (!truthy(get(a, "prior_xml"))) throw EdgarError("with --xml, give the prior quarter as --prior-xml");
            prior_rows = parse_info_table(read_bytes(S(a, "prior_xml")), file_mult);
            meta.d->set("prior_period", Val::str("prior file"));
        } else {
            const Quarter pq = load_quarter(fetcher, get(a, "cik"), "", "", pystr(get(meta, "period")));
            prior_rows = pq.rows; meta.d->set("prior_period", get(pq.meta, "period"));
        }
        List cur = aggregate(rows).first, pri = aggregate(prior_rows).first;
        resolve_tickers(cur, index.get(), overrides, figi); resolve_tickers(pri, index.get(), overrides, figi);
        const List changes = diff(cur, pri);
        if (B(a, "json")) { Val o = Val::dict(); o.d->set("meta", meta); o.d->set("changes", Val::list(changes)); print(dumps(o, J)); }
        else out(render_diff(meta, changes));
        return 0;
    }
    // copy
    const auto [weights, skipped] = copy_weights(positions, top, get(a, "min_pct").num(), strs(get(a, "exclude")));
    const auto mg = [&](const char* k) { return has(meta, k) ? pystr(get(meta, k)) : std::string("None"); };
    Val targets = Val::dict();
    targets.d->set("source", Val::str(mg("filer") + " 13F-HR, quarter ending " + mg("period") + ", filed " + mg("filed") + " (accession " + mg("accession") + ")"));
    targets.d->set("as_of", dget(meta, "period")); targets.d->set("filed", dget(meta, "filed")); targets.d->set("cik", dget(meta, "cik"));
    targets.d->set("method", Val::str((top ? "top " + std::to_string(top) : std::string("all")) + " stock positions by value" + (combine ? ", share classes combined" : "") + ", pct_of_13f scaled to 100"));
    targets.d->set("weights", Val::list(weights)); targets.d->set("skipped", Val::list(skipped));
    { Val c = Val::list(); for (const auto& s : CAVEATS) c.l->push_back(Val::str(s)); targets.d->set("caveats", c); }
    if (B(a, "json") || B(a, "dry_run")) print(dumps(targets, J));
    if (!B(a, "dry_run")) {
        const std::string path = S(a, "out"), folder = dirname(path);
        if (!folder.empty()) makedirs(folder);
        write_text(path, dumps(targets, J) + "\n");
        if (!B(a, "json")) {
            print("wrote " + path);
            for (const auto& w : weights)
                print("  " + ljust(pystr(get(w, "ticker")), 7) + " " + ljust(cpslice(pystr(get(w, "name")), 0, 28), 28) + " " + rjust(fmt_f(get(w, "weight").f, 2), 6) + "%  (" +
                      fmt_f(get(w, "pct_of_13f").f, 2) + "% of the 13F)" + (truthy(dget(w, "check_ticker")) ? "  check ticker" : ""));
            print("Holdings as of " + mg("period") + ", filed " + mg("filed") + ". Next: trader-budget sizes these to your budget.");
        }
    }
    return 0;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("edgar13f", "13F reader for trader-copycat. Built into A.S.T.R.O AI.");
    p.subcommands("cmd", false);
    const auto common = [](Parser& s) {
        s.opt({"--user-agent"}).help("SEC User-Agent: \"Your Name you@example.com\""); s.opt({"--config"}).def("trader/config.json");
        s.opt({"--offline"}).metavar("DIR").help("read every URL from files in DIR"); s.opt({"--json"}).store_true().help("print JSON");
    };
    const std::string figi_env = env_or("OPENFIGI_API_KEY");
    const auto quarter = [&](Parser& s) {
        s.pos("cik").nargs("?").help("the filer's CIK (find it with: find NAME)");
        s.opt({"--period"}).help("quarter end, YYYY-MM-DD (default: the newest)"); s.opt({"--accession"}).help("a specific filing, 0001193125-26-352200");
        s.opt({"--xml"}).help("read this information table file instead of EDGAR");
        s.opt({"--thousands"}).store_true().help("with --xml: values are in thousands (filings before 2023)");
        s.opt({"--tickers"}).metavar("FILE").help("company_tickers.json to use instead of downloading it");
        s.opt({"--map"}).def("trader/tickers.json").help("your own CUSIP to ticker fixes (default trader/tickers.json)");
        s.opt({"--figi"}).store_true().help("look up unresolved or unsure tickers on OpenFIGI");
        if (figi_env.empty()) s.opt({"--figi-key"}); else s.opt({"--figi-key"}).def(Val::str(figi_env));
        s.opt({"--no-tickers"}).store_true().help("skip ticker matching");
    };
    { Parser& s = p.sub("find"); s.help_line = "find a filer's CIK by name or person"; s.pos("query").nargs("+"); s.opt({"--sec"}).store_true().help("also search every EDGAR filer"); common(s); }
    { Parser& s = p.sub("filings"); s.help_line = "list a filer's 13F filings"; s.pos("cik"); s.opt({"--limit"}).integer().def(8); common(s); }
    for (const auto& [name, text] : std::vector<std::pair<std::string, std::string>>{{"holdings", "the positions in one quarter"}, {"diff", "what changed since the quarter before"}, {"copy", "write trader/targets.json from the top positions"}}) {
        Parser& s = p.sub(name); s.help_line = text; quarter(s); common(s);
        s.opt({"--combine"}).store_true().help("sum share classes of one company (Alphabet A + C)"); s.opt({"--top"}).integer().help("only the N biggest positions");
        if (name == "diff") s.opt({"--prior-xml"}).help("with --xml: the prior quarter's table");
        if (name == "copy") {
            s.opt({"--min-pct"}).number().def_float(0.0).help("skip positions under this %% of the 13F");
            s.opt({"--exclude"}).nargs("*").def(Val::list()).help("tickers to leave out");
            s.opt({"--separate"}).store_true().help("keep share classes apart (default: combined)"); s.opt({"--out"}).def("trader/targets.json");
            s.opt({"--dry-run"}).store_true().help("print, do not write");
        }
    }
    const Val a = parse_args(p, argv);
    if (S(a, "cmd").empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    try { return run(a); }
    catch (const EdgarError& e) { eprint(std::string("edgar13f: ") + e.what()); return 2; }
}
PACKTOOL("trader-copycat/edgar13f", main);
}  // namespace packtools::trader_copycat
