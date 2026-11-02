// trader-pelosi/ptr - Congress trade tracker: the House Clerk's public financial disclosure index (<YEAR>FD.zip) and
// the Periodic Transaction Reports (PTRs) members file when they, a spouse or a dependent child trade a stock. Lists
// filings, remembers what it has shown (trader/state/pelosi.json), downloads a PTR and reads its rows from
// `pdftotext -layout` text. Public STOCK Act disclosures, filed days or weeks after the trade, amounts as ranges:
// not inside information. C++ port of trader-skill/trader-pelosi/ptr.py (Jake Schincariol, MIT); same sources,
// parsing rules, state file, output and exit codes. Unicode spaces in a PTR line count as plain spaces.
namespace packtools::trader_pelosi {
using namespace pyrt;
struct PtrError : std::runtime_error { using std::runtime_error::runtime_error; };
static const char* UA = "trader-skill/1.0 (+https://github.com/Jakeschincariol/trader-skill)";
static const std::vector<std::pair<std::string, std::string>> FILING_TYPES = {
    {"P", "Periodic Transaction Report"}, {"O", "Annual report"}, {"A", "Amendment"}, {"C", "Candidate report"}, {"X", "Extension"},
    {"T", "Termination report"}, {"W", "Waiver"}, {"D", "Disclosure (new member or candidate)"}, {"H", "New filer report"}};
static const std::vector<std::pair<std::string, std::string>> TXN_TYPES = {{"P", "purchase"}, {"S", "sale"}, {"S (partial)", "partial sale"}, {"E", "exchange"}};
static const std::vector<std::pair<std::string, std::string>> OWNERS = {{"SP", "spouse"}, {"JT", "joint"}, {"DC", "dependent child"}, {"", "self"}};
static const int LATE_DAYS = 45;
inline std::string tget(const std::vector<std::pair<std::string, std::string>>& t, const std::string& k) { for (const auto& kv : t) if (kv.first == k) return kv.second; return k; }
inline std::string ptr_url(long long y, const std::string& doc) { return "https://disclosures-clerk.house.gov/public_disc/ptr-pdfs/" + std::to_string(y) + "/" + doc + ".pdf"; }
inline std::string fd_url(long long y, const std::string& doc) { return "https://disclosures-clerk.house.gov/public_disc/financial-pdfs/" + std::to_string(y) + "/" + doc + ".pdf"; }
inline std::string index_url(long long y) { return "https://disclosures-clerk.house.gov/public_disc/financial-pdfs/" + std::to_string(y) + "FD.zip"; }
// Unicode whitespace -> ' ' so std::regex's \s and \S see what Python's do
inline std::string uspace(const std::string& s) {
    bool plain = true; for (unsigned char c : s) if (c >= 0x80 || (c >= 0x1C && c <= 0x1F)) { plain = false; break; }
    if (plain) return s;
    std::string o; for (const auto& ch : cps(s)) { const uint32_t c = cp_of(ch); o += (c > 0x20 && py_space(c)) ? std::string(" ") : ch; } return o;
}
inline long long dnum(const std::string& s) {   // date.fromisoformat -> day number; ValueError like Python
    long long y = -1, m = -1, d = -1;
    const auto dig = [&](std::size_t a, std::size_t n) -> long long { if (a + n > s.size()) return -1; long long x = 0; for (std::size_t k = a; k < a + n; ++k) { if (s[k] < '0' || s[k] > '9') return -1; x = x * 10 + (s[k] - '0'); } return x; };
    if (s.size() == 10 && s[4] == '-' && s[7] == '-') { y = dig(0, 4); m = dig(5, 2); d = dig(8, 2); } else if (s.size() == 8) { y = dig(0, 4); m = dig(4, 2); d = dig(6, 2); }
    if (y < 0 || m < 0 || d < 0 || !valid_ymd(y, m, d)) throw PyErr("ValueError", "Invalid isoformat string: " + str_repr(s));
    return civil_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
}
inline std::string diso(long long n) { long long y; unsigned m, d; civil_from(n - 719468, y, m, d); char b[48]; std::snprintf(b, sizeof b, "%04lld-%02u-%02u", y, m, d); return b; }
// '8/21/2026' or '08/21/2026' -> '2026-08-21' (two-digit years -> 20xx)
inline Val iso(const Val& mdy) {
    static const std::regex rx(R"(^\s*(\d{1,2})/(\d{1,2})/(\d{2,4})\s*$)");
    const std::string s = uspace(truthy(mdy) ? pystr(mdy) : std::string()); std::smatch m;
    if (!std::regex_search(s, m, rx)) return Val();
    const long long month = std::stoll(m[1].str()), day = std::stoll(m[2].str()); long long year = std::stoll(m[3].str());
    if (year < 100) year += 2000;
    if (!valid_ymd(year, month, day)) return Val();
    char b[48]; std::snprintf(b, sizeof b, "%04lld-%02lld-%02lld", year, month, day); return Val::str(b);
}
inline std::string download(const std::string& url, int timeout = 60) {
    const HttpResp r = http_get(url, {std::string("User-Agent: ") + UA}, timeout);
    if (r.status >= 400) throw PtrError("HTTP " + std::to_string(r.status) + " for " + url);
    if (!r.error.empty() || r.status == 0) throw PtrError("could not reach " + url + ": " + (r.error.empty() ? std::string("no response") : r.error));
    return r.body;
}
// ---- the index
inline std::string read_index_bytes(const std::string& data) {
    if (data.compare(0, 2, "PK") == 0) {
        const auto all = zip::namelist(data);
        for (const auto& n : all) if (ends(lower(n), ".xml")) return zip::read(data, n);
        throw PtrError("no XML inside the ZIP (files: " + join(all, ", ") + ")");
    }
    return data;
}
inline List parse_index(const std::string& xml_bytes, long long year) {
    std::shared_ptr<xml::Node> root;
    try { root = xml::fromstring(xml_bytes); } catch (const xml::ParseError& e) { throw PtrError(std::string("index is not valid XML: ") + e.what()); }
    List out;
    root->iter([&](const xml::Node& m) {
        if (m.tag != "Member") return;
        std::vector<std::pair<std::string, std::string>> f;
        for (const auto& c : m.kids) { bool put = false; for (auto& kv : f) if (kv.first == c->tag) { kv.second = strip(c->text); put = true; } if (!put) f.push_back({c->tag, strip(c->text)}); }
        const auto g = [&](const std::string& k) -> std::optional<std::string> { for (const auto& kv : f) if (kv.first == k) return kv.second; return std::nullopt; };
        const std::string doc = g("DocID").value_or(""), kind = g("FilingType").value_or("");
        long long y = year;
        if (const auto ys = g("Year"); ys && !ys->empty()) { const auto v = py_int(*ys); if (!v) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(*ys)); y = *v; }
        std::vector<std::string> nm; for (const char* k : {"Prefix", "First", "Last", "Suffix"}) if (const auto v = g(k); v && !v->empty()) nm.push_back(*v);
        Val r = Val::dict();
        r.d->set("doc_id", Val::str(doc)); r.d->set("type", Val::str(kind)); r.d->set("type_name", Val::str(tget(FILING_TYPES, kind)));
        r.d->set("last", Val::str(g("Last").value_or(""))); r.d->set("first", Val::str(g("First").value_or(""))); r.d->set("name", Val::str(join(nm, " ")));
        r.d->set("district", Val::str(g("StateDst").value_or(""))); r.d->set("year", Val::integer(y));
        r.d->set("filed", iso(Val::str(g("FilingDate").value_or("")))); r.d->set("url", Val::str(kind == "P" ? ptr_url(y, doc) : fd_url(y, doc)));
        out.push_back(r);
    });
    std::stable_sort(out.begin(), out.end(), [](const Val& a, const Val& b) {
        const auto ka = std::make_pair(truthy(get(a, "filed")) ? get(a, "filed").s : std::string(), get(a, "doc_id").s);
        const auto kb = std::make_pair(truthy(get(b, "filed")) ? get(b, "filed").s : std::string(), get(b, "doc_id").s);
        return ka > kb; });
    return out;
}
inline List load_index(long long year, const std::string& offline) {
    std::string data;
    if (!offline.empty()) data = read_bytes(offline);
    else data = download(index_url(year));
    return parse_index(read_index_bytes(data), year);
}
// 'pelosi', 'Nancy Pelosi' or 'Pelosi, Nancy' against an index row
inline bool member_match(const Val& row, const std::string& member0) {
    const std::string member = lower(strip(member0));
    if (member.empty()) return true;
    std::string last, first;
    if (member.find(',') != std::string::npos) { const std::size_t c = member.find(','); last = strip(member.substr(0, c)); first = strip(member.substr(c + 1)); }
    else { const auto parts = split_ws(member); last = parts.back(); first = join(std::vector<std::string>(parts.begin(), parts.end() - 1), " "); }
    if (lower(get(row, "last").s) != last) return false;
    return first.empty() || starts(lower(get(row, "first").s), first);
}
inline List select(const List& rows, const std::vector<std::string>& members, const std::string& kind, const std::string& since) {
    List out;
    for (const auto& r : rows) {
        if (!kind.empty() && get(r, "type").s != kind) continue;
        if (!members.empty()) { bool any = false; for (const auto& m : members) if (member_match(r, m)) { any = true; break; } if (!any) continue; }
        if (!since.empty() && (truthy(get(r, "filed")) ? get(r, "filed").s : std::string()) < since) continue;
        out.push_back(r);
    }
    return out;
}
// ---- state
inline Val load_state(const std::string& path) {
    if (!path.empty() && is_file(path)) return loads(read_text(path));
    Val s = Val::dict(); s.d->set("seen", Val::list()); s.d->set("last_check", Val()); return s;
}
inline void save_state(const std::string& path, const Val& state) {
    const std::string folder = dirname(path); if (!folder.empty()) makedirs(folder);
    DumpOpts o; o.indent = 2; write_text(path, dumps(state, o) + "\n");
}
inline List new_since(const List& rows, const Val& state, long long today, int first_days = LATE_DAYS) {
    std::set<std::string> seen; for (const auto& x : iter(dget(state, "seen", Val::list()))) if (x.t == Val::STR) seen.insert(x.s);
    List out;
    if (dget(state, "last_check").is_none()) {
        const std::string cutoff = diso(today - first_days);
        for (const auto& r : rows) if ((truthy(get(r, "filed")) ? get(r, "filed").s : std::string()) >= cutoff) out.push_back(r);
        return out;
    }
    for (const auto& r : rows) if (!seen.count(get(r, "doc_id").s)) out.push_back(r);
    return out;
}
// ---- parsing a PTR (pdftotext -layout text)
inline std::regex rx_i(const char* p) { return std::regex(p, std::regex::ECMAScript | std::regex::icase); }
inline std::pair<Val, Val> amount_range(const std::string& text) {
    static const std::regex AMOUNT(R"(\$([\d,]+))");
    std::vector<long long> nums;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), AMOUNT); it != std::sregex_iterator(); ++it) {
        const std::string d = replace_all((*it)[1].str(), ",", "");
        const auto v = py_int(d); if (!v) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(d));
        nums.push_back(*v);
    }
    return {nums.empty() ? Val() : Val::integer(nums[0]), nums.size() > 1 ? Val::integer(nums[1]) : Val()};
}
inline std::string remove_paren_ticker(const std::string& name, const std::string& tick) {   // re.sub(r"\s*\(TICK\)", "", name)
    const std::string pat = "(" + tick + ")"; std::string out; std::size_t from = 0;
    for (;;) {
        const std::size_t at = name.find(pat, from);
        if (at == std::string::npos) { out += name.substr(from); return out; }
        std::size_t ws = at; while (ws > from && std::isspace(static_cast<unsigned char>(name[ws - 1]))) --ws;
        out += name.substr(from, ws - from); from = at + pat.size();
    }
}
inline Val finish(Val row, const Val& filed) {
    static const std::regex TICK(R"(\(([A-Z][A-Z0-9.\-/]{0,9})\))"), CODE(R"(\[([A-Z]{2})\])"), CODE_SUB(R"(\s*\[[A-Z]{2}\])"), FULL_NUM(R"(^[\d,]+$)");
    static const std::regex OPTION = rx_i(R"(([\d,]+)\s+(call|put)\s+options?\b.*?strike price of \$([\d,.]+).*?expiration date of (\d{1,2}/\d{1,2}/\d{2,4}))");
    static const std::regex SHARES = rx_i(R"((?:purchased|sold|bought)\s+([\d,]+)\s+shares)"), EXERCISED = rx_i(R"(^\s*exercised\b)");
    const std::string asset = join(split_ws(get(row, "_asset").s), " ");
    auto& items = row.d->items; items.erase(std::remove_if(items.begin(), items.end(), [](const auto& kv) { return kv.first == "_asset"; }), items.end());
    std::vector<std::string> ticks, codes;
    for (auto it = std::sregex_iterator(asset.begin(), asset.end(), TICK); it != std::sregex_iterator(); ++it) ticks.push_back((*it)[1].str());
    for (auto it = std::sregex_iterator(asset.begin(), asset.end(), CODE); it != std::sregex_iterator(); ++it) codes.push_back((*it)[1].str());
    row.d->set("ticker", ticks.empty() ? Val() : Val::str(ticks.back()));
    row.d->set("asset_type", codes.empty() ? Val() : Val::str(codes.back()));
    std::string name = std::regex_replace(asset, CODE_SUB, "");
    if (!ticks.empty()) name = remove_paren_ticker(name, ticks.back());
    { std::size_t a = 0, b = name.size(); while (a < b && (name[a] == ' ' || name[a] == '-')) ++a; while (b > a && (name[b - 1] == ' ' || name[b - 1] == '-')) --b; name = name.substr(a, b - a); }
    row.d->set("asset", Val::str(name));
    const std::string amount = join(split_ws(get(row, "amount").s), " ");
    row.d->set("amount", Val::str(amount));
    const auto [lo, hi] = amount_range(amount); row.d->set("amount_low", lo); row.d->set("amount_high", hi);
    const std::string desc = join(split_ws(has(row, "description") ? pystr(get(row, "description")) : std::string()), " ");
    row.d->set("description", desc.empty() ? Val() : Val::str(desc));
    std::smatch m;
    if (std::regex_search(desc, m, OPTION)) {
        const std::string c = replace_all(m[1].str(), ",", ""), k = replace_all(m[3].str(), ",", "");
        const auto contracts = py_int(c); if (!contracts) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(c));
        const auto strike = parse_float(k); if (!strike) throw PyErr("ValueError", "could not convert string to float: " + str_repr(k));
        Val o = Val::dict();
        o.d->set("contracts", Val::integer(*contracts)); o.d->set("kind", Val::str(lower(m[2].str()))); o.d->set("strike", Val::flt(*strike));
        o.d->set("expires", iso(Val::str(m[4].str()))); o.d->set("exercised", Val::boolean(std::regex_search(desc, EXERCISED)));
        row.d->set("option", o);
    }
    if (std::regex_search(desc, m, SHARES)) {
        const std::string c = replace_all(m[1].str(), ",", ""); const auto v = py_int(c);
        if (!v) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(c));
        row.d->set("shares", Val::integer(*v));
    }
    if (truthy(filed) && truthy(get(row, "date"))) {
        const long long days = dnum(pystr(filed)) - dnum(get(row, "date").s);
        row.d->set("days_to_disclose", Val::integer(days)); row.d->set("late", Val::boolean(days > LATE_DAYS));
    }
    return row;
}
inline Val parse_ptr_text(const std::string& text, const Val& filed) {
    static const std::regex TXN(R"(^\s*(?:(SP|JT|DC)\s+)?(.*?)\s+(S \(partial\)|P|S|E)\s+(\d{2}/\d{2}/\d{4})\s+(\d{2}/\d{2}/\d{4})\s+(\S.*?)\s*$)");
    static const std::vector<std::pair<std::string, std::regex>> LABELS = {
        {"status", rx_i(R"(^\s*F(?:ILING)?\s+S(?:TATUS)?\s*:\s*(.*)$)")}, {"description", rx_i(R"(^\s*D(?:ESCRIPTION)?\s*:\s*(.*)$)")},
        {"account", rx_i(R"(^\s*S(?:UBHOLDING)?\s+O(?:F)?\s*:\s*(.*)$)")}, {"location", rx_i(R"(^\s*L(?:OCATION)?\s*:\s*(.*)$)")}};
    static const std::regex PAGE_HEADER = rx_i(R"(^\s*(ID\s+Owner\s+Asset|Type\s+Date\b|Type$|Date$|Gains >$|\$200\?$))");
    static const std::regex END = rx_i(R"(^\s*(\* For the complete|I CERTIFY|Digitally Signed|Filing ID #))");
    static const std::regex NAME(R"(^Name:\s+(.*)$)"), STATUS(R"(^Status:\s+(.*)$)"), DISTRICT(R"(^State/District:\s*(\S+))"), FILING_ID(R"(Filing ID #(\d+))");
    static const std::regex SIGNED(R"(^Digitally Signed:.*?,\s*(\d{2}/\d{2}/\d{4}))"), COLS(R"(\s{2,})"), AMOUNT_TAIL(R"(^\$[\d,]+$)");
    Val head = Val::dict();
    head.d->set("name", Val()); head.d->set("status", Val()); head.d->set("district", Val()); head.d->set("doc_id", Val()); head.d->set("filed", filed);
    List rows; Val cur; std::string mode; bool amount_open = false;
    for (const auto& raw : splitlines(text)) {
        const std::string line = uspace(rstrip(raw)), s = strip(line);
        std::smatch m;
        if (std::regex_search(s, m, NAME) && !truthy(get(head, "name"))) { head.d->set("name", Val::str(strip(m[1].str()))); continue; }
        if (std::regex_search(s, m, STATUS) && !truthy(get(head, "status"))) { head.d->set("status", Val::str(strip(m[1].str()))); continue; }
        if (std::regex_search(s, m, DISTRICT)) { head.d->set("district", Val::str(m[1].str())); continue; }
        if (std::regex_search(s, m, FILING_ID)) { head.d->set("doc_id", Val::str(m[1].str())); continue; }
        if (std::regex_search(s, m, SIGNED)) { if (!truthy(get(head, "filed"))) head.d->set("filed", iso(Val::str(m[1].str()))); continue; }
        if (std::regex_search(line, m, TXN) && truthy(iso(Val::str(m[4].str())))) {
            if (truthy(cur)) rows.push_back(cur);
            const std::string owner = m[1].matched ? m[1].str() : std::string(), type = m[3].str();
            cur = Val::dict();
            cur.d->set("owner", Val::str(owner)); cur.d->set("owner_name", Val::str(tget(OWNERS, owner))); cur.d->set("_asset", Val::str(m[2].str()));
            cur.d->set("type", Val::str(type)); cur.d->set("type_name", Val::str(tget(TXN_TYPES, type)));
            cur.d->set("date", iso(Val::str(m[4].str()))); cur.d->set("notified", iso(Val::str(m[5].str()))); cur.d->set("amount", Val::str(m[6].str()));
            cur.d->set("status", Val()); cur.d->set("account", Val());
            amount_open = ends(m[6].str(), "-"); mode = "asset";
            continue;
        }
        if (!truthy(cur)) continue;
        if (s.empty()) {
            if (mode == "description" || mode == "asset") mode = (mode == "description" || get(cur, "_asset").s.find('[') != std::string::npos) ? "labels" : mode;
            continue;
        }
        if (std::regex_search(s, PAGE_HEADER)) continue;
        if (std::regex_search(s, END)) { mode = "labels"; continue; }
        bool labelled = false;
        for (const auto& [key, rx] : LABELS) {
            if (!std::regex_search(line, m, rx)) continue;
            labelled = true; const std::string value = strip(m[1].str());
            if (key == "description") { cur.d->set("description", Val::str(value)); mode = "description"; }
            else if (key == "status" || key == "account") { cur.d->set(key, Val::str(value)); mode = "labels"; }
            else mode = "labels";
            break;
        }
        if (labelled) continue;
        if (mode == "asset") {
            std::vector<std::string> parts; { std::sregex_token_iterator it(s.begin(), s.end(), COLS, -1), e; for (; it != e; ++it) parts.push_back(it->str()); if (parts.empty()) parts.push_back(""); }
            if (amount_open && std::regex_search(parts.back(), AMOUNT_TAIL)) { cur.d->set("amount", Val::str(get(cur, "amount").s + " " + parts.back())); parts.pop_back(); amount_open = false; }
            if (!parts.empty() && !parts[0].empty()) cur.d->set("_asset", Val::str(get(cur, "_asset").s + " " + join(parts, " ")));
        } else if (mode == "description") {
            cur.d->set("description", Val::str((has(cur, "description") ? pystr(get(cur, "description")) : std::string()) + " " + s));
        }
    }
    if (truthy(cur)) rows.push_back(cur);
    Val out = Val::list(); for (auto& r : rows) out.l->push_back(finish(r, get(head, "filed")));
    head.d->set("rows", out);
    return head;
}
inline std::optional<std::string> pdf_to_text(const std::string& pdf) {
    const ExecResult r = run_cmd({"pdftotext", "-layout", pdf, "-"}, 60);
    if (r.code != 0) return std::nullopt;
    return decode_utf8_replace(r.out);
}
// ---- output
inline std::set<std::string> watchlist(const Val& a) {
    std::set<std::string> t; for (const auto& x : iter(orv(dget(a, "tickers"), Val::list()))) t.insert(upper(x.s));
    const Val path = dget(a, "portfolio");
    if (truthy(path) && is_file(path.s)) for (const auto& p : iter(dget(loads(read_text(path.s)), "positions", Val::list()))) if (truthy(dget(p, "ticker"))) t.insert(upper(pystr(get(p, "ticker"))));
    return t;
}
inline std::string pct_g(double x) { char b[64]; std::snprintf(b, sizeof b, "%g", x); return b; }
inline std::string describe(const Val& row) {
    std::vector<std::string> bits = {get(row, "owner_name").s + " " + get(row, "type_name").s};
    std::string what = get(row, "asset").s;
    if (truthy(dget(row, "ticker"))) what += " (" + get(row, "ticker").s + ")";
    if (truthy(dget(row, "option"))) {
        const Val o = get(row, "option");
        what += ": " + std::string(truthy(dget(o, "exercised")) ? "exercised " : "") + std::to_string(get(o, "contracts").i) + " " + get(o, "kind").s + " options, $" + pct_g(to_float(get(o, "strike"))) + " strike, expires " + pystr(get(o, "expires"));
    } else if (truthy(dget(row, "shares"))) what += ": " + commas(std::to_string(get(row, "shares").i)) + " shares";
    bits.push_back(what); bits.push_back(get(row, "amount").s);
    std::string when = "traded " + pystr(get(row, "date"));
    if (!dget(row, "days_to_disclose").is_none()) when += ", disclosed " + std::to_string(get(row, "days_to_disclose").i) + " days later" + (truthy(dget(row, "late")) ? " (LATE: over 45)" : "");
    bits.push_back(when);
    if (truthy(dget(row, "account"))) bits.push_back("account: " + pystr(get(row, "account")));
    return join(bits, ", ");
}
inline std::string render_report(const Val& rep, const std::set<std::string>& watch) {
    const auto q = [&](const char* k) { return truthy(dget(rep, k)) ? pystr(get(rep, k)) : std::string("?"); };
    std::vector<std::string> out = {q("name") + " (" + q("district") + "), " + q("status") + ". PTR " + q("doc_id") + ", filed " + q("filed") + "."};
    for (const auto& r : *get(rep, "rows").l) {
        const Val t = dget(r, "ticker");
        const std::string flag = t.t == Val::STR && watch.count(t.s) ? "  <- " + t.s + " is on your list" : "";
        out.push_back("  - " + describe(r) + flag);
        if (truthy(dget(r, "description")) && !truthy(dget(r, "option")) && !truthy(dget(r, "shares"))) out.push_back("      " + get(r, "description").s);
    }
    if (get(rep, "rows").l->empty()) out.push_back("  no transaction rows found: read the PDF directly");
    out.push_back("Public disclosure under the STOCK Act, filed up to 45 days after the trade. Amounts are ranges. Not inside information.");
    return join(out, "\n") + "\n";
}
inline std::string render_index(const List& rows) {
    std::vector<std::string> out;
    for (const auto& r : rows)
        out.push_back(pystr(get(r, "filed")) + "  " + ljust(cpslice(get(r, "name").s, 0, 28), 28) + " " + ljust(get(r, "district").s, 5) + " " + ljust(get(r, "type").s, 4) + " doc " + ljust(get(r, "doc_id").s, 9) + " " + get(r, "url").s);
    return join(out, "\n") + (out.empty() ? "" : "\n");
}
// ---- CLI
inline std::pair<std::string, Val> fetch_and_read(const std::string& doc_id, long long year, const std::string& pdf_dir, const Val& filed) {
    makedirs(pdf_dir);
    const std::string path = path_join(pdf_dir, doc_id + ".pdf");
    if (!is_file(path)) {
        const std::string data = download(ptr_url(year, doc_id));
        if (data.compare(0, 4, "%PDF") != 0) throw PtrError("doc " + doc_id + " did not come back as a PDF");
        write_text(path, data);
    }
    const auto text = pdf_to_text(path);
    if (!text) return {path, Val()};
    Val rep = parse_ptr_text(*text, filed);
    rep.d->set("pdf", Val::str(path)); rep.d->set("url", Val::str(ptr_url(year, doc_id)));
    return {path, rep};
}
inline std::vector<std::string> strs(const Val& v) { std::vector<std::string> o; for (const auto& x : iter(orv(v, Val::list()))) o.push_back(x.s); return o; }
inline int run_cmd_(const Val& a) {
    const std::string cmd = S(a, "cmd"); DumpOpts J; J.indent = 2;
    if (cmd == "parse") {
        const Val rep = parse_ptr_text(decode_utf8_replace(read_text(S(a, "text_file"))), get(a, "filed"));
        if (B(a, "json")) print(dumps(rep, J)); else out(render_report(rep, watchlist(a)));
        return get(rep, "rows").l->empty() ? 1 : 0;
    }
    if (cmd == "fetch") {
        const auto [path, rep] = fetch_and_read(S(a, "doc_id"), get(a, "year").i, S(a, "pdf_dir"), get(a, "filed"));
        if (rep.is_none()) { print("Saved " + path + ". pdftotext is not installed, so read the PDF directly, or install poppler (brew install poppler, or apt install poppler-utils)."); return 0; }
        if (B(a, "json")) print(dumps(rep, J)); else out(render_report(rep, watchlist(a)));
        return get(rep, "rows").l->empty() ? 1 : 0;
    }
    const std::string offline = truthy(get(a, "offline")) ? S(a, "offline") : std::string();
    const long long year = get(a, "year").i;
    List rows = load_index(year, offline);
    if (cmd == "new" && offline.empty()) {
        const long long today = truthy(get(a, "today")) ? dnum(S(a, "today")) : dnum(today_iso());
        long long y; unsigned mo, d; civil_from(today - 719468, y, mo, d);
        if (mo == 1 && year == y) { try { for (auto& r : load_index(year - 1, "")) rows.push_back(r); } catch (const PtrError&) {} }
    }
    List picked = select(rows, strs(get(a, "member")), truthy(get(a, "type")) ? S(a, "type") : std::string(), truthy(get(a, "since")) ? S(a, "since") : std::string());
    if (cmd == "index") {
        const long long n = static_cast<long long>(picked.size()), lim = get(a, "limit").i;
        picked.resize(static_cast<std::size_t>(lim >= 0 ? std::min(lim, n) : std::max(0LL, n + lim)));
        if (B(a, "json")) { Val l = Val::list(); for (const auto& r : picked) l.l->push_back(r); print(dumps(l, J)); }
        else { out(render_index(picked)); if (picked.empty()) print("No matching filings in the " + std::to_string(year) + " index."); }
        return picked.empty() ? 1 : 0;
    }
    // new
    const long long today = truthy(get(a, "today")) ? dnum(S(a, "today")) : dnum(today_iso());
    Val state = load_state(S(a, "state"));
    const List fresh = new_since(picked, state, today);
    const auto watch = watchlist(a);
    Val reports = Val::list();
    if (B(a, "read")) for (const auto& r : fresh) {
        if (get(r, "type").s != "P") continue;
        const auto [path, rep] = fetch_and_read(get(r, "doc_id").s, get(r, "year").i, S(a, "pdf_dir"), get(r, "filed"));
        Val e = Val::dict(); e.d->set("filing", r); e.d->set("pdf", Val::str(path)); e.d->set("report", rep); reports.l->push_back(e);
    }
    if (B(a, "json")) { Val l = Val::list(); for (const auto& r : fresh) l.l->push_back(r); Val o = Val::dict(); o.d->set("new", l); o.d->set("reports", reports); print(dumps(o, J)); }
    else {
        if (fresh.empty()) print("Nothing new since " + (truthy(dget(state, "last_check")) ? pystr(get(state, "last_check")) : std::string("the last check")) + ".");
        for (const auto& r : fresh) {
            print("NEW " + get(r, "type_name").s + ": " + get(r, "name").s + " (" + get(r, "district").s + ") filed " + pystr(get(r, "filed")) + ", doc " + get(r, "doc_id").s);
            print("    " + get(r, "url").s);
        }
        for (const auto& rep : *reports.l) {
            if (get(rep, "report").is_none()) print("    read " + get(rep, "pdf").s + " (pdftotext not installed)");
            else out(render_report(get(rep, "report"), watch));
        }
    }
    if (!B(a, "dry_run")) {
        std::set<std::string> seen; for (const auto& x : iter(dget(state, "seen", Val::list()))) seen.insert(pystr(x));
        for (const auto& r : picked) seen.insert(get(r, "doc_id").s);
        Val l = Val::list(); for (const auto& s : seen) l.l->push_back(Val::str(s));
        state.d->set("seen", l); state.d->set("last_check", Val::str(now_iso()));
        save_state(S(a, "state"), state);
    }
    return fresh.empty() ? 1 : 0;
}
inline int main(const std::vector<std::string>& argv) {
    Parser p("ptr", "Congress trade tracker for trader-pelosi. Built into A.S.T.R.O AI.");
    p.subcommands("cmd", false);
    const long long year = std::stoll(today_iso().substr(0, 4));
    const auto common = [&](Parser& s) {
        s.opt({"--year"}).integer().def(year); s.opt({"--member"}).append().help("last name or 'First Last'; repeat for more members");
        s.opt({"--type"}).def("P").help("FilingType (default P = PTR; '' for all)"); s.opt({"--since"}).help("only filings on or after YYYY-MM-DD");
        s.opt({"--offline"}).metavar("FILE").help("a saved <YEAR>FD.zip or .xml"); s.opt({"--json"}).store_true();
    };
    { Parser& s = p.sub("index"); s.help_line = "list filings from the yearly index"; common(s); s.opt({"--limit"}).integer().def(50); }
    { Parser& s = p.sub("new"); s.help_line = "filings not seen at the last check"; common(s); s.opt({"--state"}).def("trader/state/pelosi.json");
      s.opt({"--today"}).help("YYYY-MM-DD, for reproducible runs"); s.opt({"--dry-run"}).store_true().help("do not update the state file");
      s.opt({"--read"}).store_true().help("also download and read each new PTR (needs pdftotext)"); s.opt({"--pdf-dir"}).def("trader/state/ptr-pdfs");
      s.opt({"--tickers"}).nargs("*").help("flag trades in these tickers"); s.opt({"--portfolio"}).def("trader/portfolio.json"); }
    { Parser& s = p.sub("fetch"); s.help_line = "download one PTR PDF and read it"; s.pos("doc_id"); s.opt({"--year"}).integer().def(year);
      s.opt({"--filed"}).help("filing date YYYY-MM-DD if known"); s.opt({"--pdf-dir"}).def("trader/state/ptr-pdfs"); s.opt({"--tickers"}).nargs("*");
      s.opt({"--portfolio"}).def("trader/portfolio.json"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("parse"); s.help_line = "rows from pdftotext -layout text of a PTR"; s.pos("text_file");
      s.opt({"--filed"}).help("filing date YYYY-MM-DD (else read from the signature)"); s.opt({"--tickers"}).nargs("*");
      s.opt({"--portfolio"}).def("trader/portfolio.json"); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    if (S(a, "cmd").empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    try { return run_cmd_(a); }
    catch (const PtrError& e) { eprint(std::string("ptr: ") + e.what()); return 2; }
    catch (const OSError& e) { eprint(std::string("ptr: ") + e.what()); return 2; }
}
PACKTOOL("trader-pelosi/ptr", main);
PACKTOOL("daytrade-pelosi/ptr", main);   // day-trader-skill ships the same ptr.py (only its User-Agent string differs)
}  // namespace packtools::trader_pelosi
