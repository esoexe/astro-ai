// prospector/leads: finds new leads (one public page per site, slowly), ranks them by the gaps you sell a fix for,
// puts them on the board and writes the opening email with the legal footer. A C++ port of
// closer-skill/prospector/leads.py (MIT, Jake Schincariol); openers.json ships with the skill.
// Pages come through the engine's network client (public addresses only), decoded as UTF-8 with replacement.
// For the test suites only: with ASTRO_TOOL_OFFLINE=1, ASTRO_TOOL_PAGES=DIR serves DIR/<host>.html (and the load
// time from DIR/<host>.secs) instead of the network.
namespace packtools::closer_prospect {
using namespace pyrt;
using namespace packtools::closer;
struct ProspectError : std::runtime_error { using std::runtime_error::runtime_error; };

inline const char* UA = "CloserSkill/1.0 (+https://github.com/Jakeschincariol/closer-skill; one public page per site)";
inline constexpr int DAILY_CAP = 50;
inline constexpr double SLOW_SECONDS = 4.0;
inline const char* BOOKING = R"(calendly\.com|acuityscheduling|zocdoc|mindbodyonline|booksy|vagaro|squareup\.com/appointments|square\.site/book|setmore|simplybook|schedulicity|janeapp|nexhealth|localmed|opencare|book (an |your )?(appointment|online|now|a class)|schedule (an |your )?(appointment|visit)|request (an )?appointment|booking)";
inline const char* ORDERING = R"(toasttab|order\.online|square\.site|chownow|olo\.com|doordash|ubereats|grubhub|order (online|now|ahead|pickup))";
inline const char* ADS = R"(googleadservices|AW-\d{6,}|gtag\(\s*['"]config['"]\s*,\s*['"]AW-|googleads\.g\.doubleclick)";
inline const char* REVIEWS = R"(aggregateRating|reviewCount|testimonial|reviews|elfsight|birdeye|podium|trustindex)";
inline const char* VIEWPORT = R"(<meta[^>]+name=["']viewport["'][^>]*>)";
inline const char* TITLE = R"(<title[^>]*>(.*?)</title>)";
inline const char* NEEDS_BOOKING = R"(dent|chiro|spa|salon|yoga|gym|fitness|clinic|physio|therapy|vet|groom|barber|studio|med)";
inline const char* NEEDS_ORDERING = R"(bakery|cafe|coffee|restaurant|pizza|deli|kitchen|bar\b|food|catering)";
inline bool rx(const char* p, const std::string& s, int flags = re::I) { return static_cast<bool>(re::compile(p, flags).search(s)); }
inline std::pair<int, std::string> gap_info(const std::string& k) {
    static const std::map<std::string, std::pair<int, std::string>> G = {{"no-booking", {3, "No online booking"}}, {"not-mobile", {3, "Site not mobile-friendly"}},
        {"slow", {2, "Site loads in %.0f s"}}, {"no-ads", {2, "No Google Ads tag on the site"}}, {"no-ordering", {2, "No online ordering"}},
        {"no-reviews", {1, "No reviews on site"}}, {"no-site", {4, "No website found"}}};
    return G.at(k);
}
inline Val gap_val(const std::string& key, std::optional<double> secs = std::nullopt) {
    const auto [score, why] = gap_info(key);
    Val g = Val::dict(); g.d->set("key", sv(key)); g.d->set("score", Val::integer(score));
    g.d->set("why", sv(secs ? replace_all(why, "%.0f", fmt_f(*secs, 0)) : why));
    return g;
}

// ---------------------------------------------------------------- fetch and analyze
struct Fetched { bool ok = false; std::string body, error; double secs = 0; };
inline std::string reason(long code) {
    static const std::map<long, std::string> R = {{400, "Bad Request"}, {401, "Unauthorized"}, {402, "Payment Required"}, {403, "Forbidden"}, {404, "Not Found"},
        {405, "Method Not Allowed"}, {406, "Not Acceptable"}, {408, "Request Timeout"}, {410, "Gone"}, {429, "Too Many Requests"}, {500, "Internal Server Error"},
        {501, "Not Implemented"}, {502, "Bad Gateway"}, {503, "Service Unavailable"}, {504, "Gateway Timeout"}};
    const auto it = R.find(code); return it == R.end() ? "" : it->second;
}
// one public page; the researcher reads with its own User-Agent
inline Fetched fetch_page(const std::string& url, const char* ua, long timeout = 15) {
    Fetched f;
    const char* off = std::getenv("ASTRO_TOOL_OFFLINE"); const char* pages = std::getenv("ASTRO_TOOL_PAGES");
    if (off && std::string(off) == "1" && pages && *pages) {   // the test suites' saved pages
        const auto parts = pyrt::urlp::urlsplit(url);   // DIR/<host>.html, or DIR/<host>_<path with / as _>.html
        std::string name = lower(parts.netloc), path = parts.path; while (!path.empty() && path.back() == '/') path.pop_back();
        if (!path.empty()) name += replace_all(path, "/", "_");
        const std::string p = path_join(pages, name + ".html");
        if (!is_file(p)) { f.error = "<urlopen error [Errno -2] Name or service not known>"; return f; }
        f.ok = true; f.body = decode_utf8_replace(read_bytes(p));
        const std::string sp = path_join(pages, name + ".secs"); f.secs = is_file(sp) ? std::strtod(read_bytes(sp).c_str(), nullptr) : 0.25;
        return f;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const HttpResp r = packtools::http_get(url, {std::string("User-Agent: ") + ua, "Accept: text/html"}, timeout);
    f.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!r.error.empty()) { f.error = "<urlopen error " + r.error + ">"; return f; }
    if (r.status >= 400) { f.error = "HTTP Error " + std::to_string(r.status) + ": " + reason(r.status); return f; }
    f.ok = true; f.body = decode_utf8_replace(r.body.substr(0, 2000000));
    return f;
}
inline Fetched fetch(std::string url, long timeout = 15) {
    if (!re::compile("^https?://").match(url)) url = "https://" + url;
    return fetch_page(url, UA, timeout);
}
// The gaps on one page; page = nullopt means no site could be loaded. (gaps, title)
inline std::pair<List, Val> analyze(const std::optional<std::string>& page, std::optional<double> seconds, const std::string& type = "") {
    if (!page) return {List{gap_val("no-site")}, Val()};
    List found;
    if (rx(NEEDS_BOOKING, type) && !rx(BOOKING, *page)) found.push_back(gap_val("no-booking"));
    if (rx(NEEDS_ORDERING, type) && !rx(ORDERING, *page)) found.push_back(gap_val("no-ordering"));
    if (!rx(VIEWPORT, *page)) found.push_back(gap_val("not-mobile"));
    if (seconds && *seconds >= SLOW_SECONDS) found.push_back(gap_val("slow", *seconds));
    if (!rx(ADS, *page)) found.push_back(gap_val("no-ads"));
    if (!rx(REVIEWS, *page)) found.push_back(gap_val("no-reviews"));
    const auto m = re::compile(TITLE, re::I | re::S).search(*page);
    const Val title = m ? sv(strip(html_unescape(re::sub(R"(\s+)", " ", m.group(1))))) : Val();
    std::stable_sort(found.begin(), found.end(), [](const Val& a, const Val& b) { return -at(a, "score").i < -at(b, "score").i; });
    return {found, title};
}
inline Val audit(const std::string& url, const std::string& type = "") {
    const Fetched f = fetch(url);
    Val r = Val::dict(); r.d->set("url", sv(url));
    if (!f.ok) {
        r.d->set("ok", Val::boolean(false)); r.d->set("error", sv(cpslice(f.error, 0, 120))); r.d->set("gaps", Val::list(analyze(std::nullopt, std::nullopt).first));
        r.d->set("title", Val()); r.d->set("seconds", Val()); return r;
    }
    const auto [gaps, title] = analyze(f.body, f.secs, type);
    r.d->set("ok", Val::boolean(true)); r.d->set("gaps", Val::list(gaps)); r.d->set("title", title); r.d->set("seconds", Val::flt(round_n(f.secs, 1)));
    return r;
}

// ---------------------------------------------------------------- leads
inline List read_csv(const std::string& path) {
    const CsvDict cd = csv_dicts(read_text(path, true));
    List rows;
    for (const auto& row : cd.rows) { Val d = Val::dict(); for (const auto& [k, v] : row) d.d->set(k ? *k : "null", v); rows.push_back(d); }
    for (const Val& r : rows) for (const char* need : {"name", "website"}) if (!truthy(g(r, need))) throw ProspectError(std::string("Every row needs ") + need + " (row: " + repr(r) + ")");
    return rows;
}
inline List rank(const List& rows, const List& audits) {
    List out;
    for (std::size_t k = 0; k < std::min(rows.size(), audits.size()); ++k) {
        const Val& r = rows[k]; const Val& a = audits[k];
        long long score = 0; List keys; for (const Val& gp : lst(at(a, "gaps"))) { score += at(gp, "score").i; keys.push_back(at(gp, "key")); }
        Val o = Val::dict(); for (const auto& [kk, v] : r.d->items) o.d->set(kk, v);
        o.d->set("score", Val::integer(score)); o.d->set("why", lst(at(a, "gaps")).empty() ? Val() : at(lst(at(a, "gaps"))[0], "why"));
        o.d->set("gaps", Val::list(keys)); o.d->set("audit", a);
        out.push_back(o);
    }
    std::stable_sort(out.begin(), out.end(), [](const Val& a, const Val& b) {
        const long long x = at(a, "score").i, y = at(b, "score").i; if (x != y) return x > y; return str_of(at(a, "name")) < str_of(at(b, "name")); });
    return out;
}
inline std::pair<std::vector<std::string>, std::vector<std::pair<std::string, std::string>>> add_leads(Val& data, const List& ranked, long long top, const Val& value, long long today) {
    std::vector<std::string> added; std::vector<std::pair<std::string, std::string>> skipped;
    const long long n = static_cast<long long>(ranked.size());
    const long long stop = top < 0 ? std::max(0LL, n + top) : std::min(n, top);   // ranked[:top]
    for (long long k = 0; k < stop; ++k) {
        const Val& r = ranked[static_cast<std::size_t>(k)];
        const std::string email = lower(strip(str_of(orv(g(r, "email"), sv("")))));
        const std::string name = ps(at(r, "name"));
        if (!email.empty() && suppressed(data, sv(email))) { skipped.emplace_back(name, "on the suppression list"); continue; }
        const std::string cid = slug(split(str_of(orv(g(r, "who"), sv(""))), " ")[0] + " " + name);
        bool dup = find(at(data, "contacts"), cid) != nullptr;
        if (!email.empty()) for (const Val& c : lstk(data, "contacts")) dup |= is(g(c, "email"), email);
        if (dup) { skipped.emplace_back(name, "already on the board"); continue; }
        upsert_contact(data, {{"id", sv(cid)}, {"name", orv(g(r, "who"), at(r, "name"))}, {"title", truthy(g(r, "title")) ? g(r, "title") : Val()}, {"company", at(r, "name")},
                              {"email", email.empty() ? Val() : sv(email)}, {"website", at(r, "website")}, {"source", sv("prospector")}, {"notes", sv("Why: " + ps(orv(g(r, "why"), sv("-"))))}});
        upsert_deal(data, today, {{"id", sv(slug(name))}, {"contact_id", sv(cid)}, {"title", orv(g(r, "offer"), sv("New business"))}, {"stage", sv("lead")}, {"value", value},
                                  {"next_step", sv("Opening email: " + ps(orv(g(r, "why"), sv("intro"))))}, {"gaps", g(r, "gaps", Val::list())}});
        added.push_back(cid);
    }
    return {added, skipped};
}

// ---------------------------------------------------------------- the opening email
inline std::string footer(const Val& me) {
    if (!truthy(g(me, "address"))) throw ProspectError("No postal address in closer/pipeline.json (me.address). US law (CAN-SPAM) needs one in every commercial email. Add it: deals.py init or edit me.address.");
    return "\n\n--\n" + ps(orv(g(me, "name"), sv(""))) + ", " + ps(orv(g(me, "company"), sv(""))) + "\n" + ps(at(me, "address")) + "\nNot interested? Reply \"no thanks\" and I won't email again.";
}
// "Harbor Lane Dental" -> "Harbor Lane"; keeps the full name when trimming would leave a single generic word.
inline std::string short_name(const std::string& company) {
    const std::string trimmed = strip(re::sub(R"(\s+(dental|fitness|co\.?|company|llc|inc\.?|ltd\.?|studio|spa|loft|bakery|group)\s*$)", "", company, 0, re::I));
    return split_ws(trimmed).size() >= 2 ? trimmed : company;
}
inline std::string format(const std::string& t, const std::map<std::string, std::string>& f) {   // str.format(**fields) for {name} fields
    std::string o;
    for (std::size_t k = 0; k < t.size(); ++k) {
        if (t[k] == '{' && k + 1 < t.size() && t[k + 1] == '{') { o += '{'; ++k; continue; }
        if (t[k] == '}' && k + 1 < t.size() && t[k + 1] == '}') { o += '}'; ++k; continue; }
        if (t[k] == '{') {
            const std::size_t e = t.find('}', k); if (e == std::string::npos) throw PyErr("ValueError", "expected '}' before end of string");
            const std::string key = t.substr(k + 1, e - k - 1); const auto it = f.find(key);
            if (it == f.end()) throw PyErr("KeyError", str_repr(key));
            o += it->second; k = e; continue;
        }
        if (t[k] == '}') throw PyErr("ValueError", "Single '}' encountered in format string");
        o += t[k];
    }
    return o;
}
struct Draft { std::string to, subject, body, gap; std::vector<std::string> missing; };
inline Draft draft(const Val& data, const Val& openers, const std::string& contact_id, const Val& gap, const Val& offer) {
    const Val* cp = find(at(data, "contacts"), contact_id);
    if (!cp) throw ProspectError("No contact " + contact_id);
    const Val c = *cp;
    if (truthy(g(c, "do_not_contact")) || suppressed(data, g(c, "email"))) throw ProspectError(ps(at(c, "name")) + " is do not contact. No email.");
    if (!truthy(g(c, "email"))) throw ProspectError(ps(at(c, "name")) + " has no email address. Find one published on their site, or call.");
    for (const Val& a : history_contact(data, sv(contact_id))) if (is(at(a, "type"), "opening")) throw ProspectError(ps(at(c, "name")) + " already got an opening email. /follow-up takes it from here.");
    Val deal = Val::dict(); for (const Val& d : lstk(data, "deals")) if (is(at(d, "contact_id"), contact_id)) { deal = d; break; }
    std::string key = "generic";
    if (truthy(gap)) key = str_of(gap);   // key = gap or the first known gap or "generic"
    const Val gaps = g(deal, "gaps", Val::list());
    if (!truthy(gap)) for (const Val& gg : lst(gaps)) if (gg.t == Val::STR && has(openers, gg.s)) { key = gg.s; break; }
    const Val t = has(openers, key) ? at(openers, key) : at(openers, "generic");
    const Val me = g(data, "me", Val::dict());
    const std::string company = str_of(orv(g(c, "company"), at(c, "name")));
    const auto words = split_ws(str_of(at(c, "name"))); if (words.empty()) throw PyErr("IndexError", "list index out of range");
    const std::map<std::string, std::string> fields = {{"first", words[0]}, {"company", company}, {"short", short_name(company)},
        {"offer", ps(orv(orv(offer, g(me, "offer")), sv("[fill: offer]")))}, {"me", split(str_of(orv(g(me, "name"), sv(""))), " ")[0]}};
    Draft d; d.body = format(str_of(at(t, "body")), fields); d.body += footer(me);
    d.to = ps(at(c, "email")); d.subject = format(str_of(at(t, "subject")), fields); d.gap = key;
    d.missing = re::compile(R"(\[fill: (\w+)\])").findall(d.body);
    return d;
}
inline std::pair<bool, std::string> record(Val& data, const std::string& contact_id, const std::string& text, const Val& approval, const std::string& when, const Val& subject) {
    const auto [ok, why] = parse_yes(approval);
    if (!ok) return {false, "Not recorded: " + why + "."};
    const Val* cp = find(at(data, "contacts"), contact_id);
    if (!cp) return {false, "No contact " + contact_id};
    const Val c = *cp;
    if (truthy(g(c, "do_not_contact")) || suppressed(data, g(c, "email"))) return {false, "Not recorded: " + ps(at(c, "name")) + " is do not contact."};
    if (text.find("[fill:") != std::string::npos) return {false, "Not recorded: the message still has a [fill: ...] in it."};
    const std::string day = cpslice(when, 0, 10);
    long long today_count = 0; for (const Val& a : lstk(data, "activities")) if (is(at(a, "type"), "opening") && head(at(a, "date"), 10) == day) today_count++;
    if (today_count >= DAILY_CAP) return {false, "Not recorded: " + std::to_string(DAILY_CAP) + " opening emails today is the cap. Send the rest tomorrow."};
    Val deal; for (const Val& d : lstk(data, "deals")) if (is(at(d, "contact_id"), contact_id) && in(at(d, "stage"), OPEN)) { deal = d; break; }
    add_activity(data, {{"date", sv(when)}, {"contact_id", sv(contact_id)}, {"deal_id", deal.is_none() ? Val() : at(deal, "id")}, {"channel", sv("email")}, {"type", sv("opening")},
                        {"direction", sv("out")}, {"subject", subject}, {"text", sv(text)}, {"approval", approval}});
    if (!deal.is_none() && is(at(deal, "stage"), "lead")) { deal.d->set("stage", sv("contacted")); deal.d->set("next_step", sv("Follow-up 1 (/follow-up)")); }
    return {true, "Opening to " + ps(at(c, "name")) + " logged (" + std::to_string(today_count + 1) + " of " + std::to_string(DAILY_CAP) + " today). /follow-up has it now."};
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("leads.py", "Find leads and write the cold outreach.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    const std::string LEADS_FILE = path_join("closer", "leads.json");
    { Parser& s = p.sub("audit"); s.pos("url"); s.opt({"--type"}).def(""); }
    { Parser& s = p.sub("rank"); s.opt({"--csv"}).required(); s.opt({"--offer"}); s.opt({"--out"}).def(Val::str(LEADS_FILE)); s.opt({"--wait"}).number().def_float(2.0); }
    { Parser& s = p.sub("add"); s.opt({"--leads"}).def(Val::str(LEADS_FILE)); s.opt({"--top"}).integer().def(10); s.opt({"--value"}).number().def(0); s.opt({"--today"}); }
    { Parser& s = p.sub("draft"); s.opt({"--contact"}).required(); s.opt({"--gap"}); s.opt({"--offer"}); }
    { Parser& s = p.sub("record"); s.opt({"--contact"}).required(); s.opt({"--text"}); s.opt({"--text-file"}); s.opt({"--subject"}); s.opt({"--approval"}).required(); s.opt({"--at"}).required(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        if (cmd == "audit") {
            const Val r = audit(S(a, "url"), S(a, "type"));
            print(S(r, "url") + "  " + (truthy(at(r, "ok")) ? "(" + ps(at(r, "seconds")) + " s)" : "could not load: " + S(r, "error")));
            for (const Val& gp : lst(at(r, "gaps"))) print("  gap: " + ps(at(gp, "why")));
            if (lst(at(r, "gaps")).empty()) print("  no gaps found");
            return 0;
        }
        if (cmd == "rank") {
            const List rows = read_csv(S(a, "csv"));
            List audits;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                if (i) std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.0, get(a, "wait").num())));
                audits.push_back(audit(S(rows[i], "website"), ps(g(rows[i], "type", sv("")))));
                eprint("  checked " + ps(at(rows[i], "name")));
            }
            List ranked = rank(rows, audits);
            for (Val& r : ranked) r.d->set("offer", truthy(get(a, "offer")) ? get(a, "offer") : g(r, "offer"));
            const std::string out = S(a, "out");
            makedirs(dirname(out).empty() ? "." : dirname(out));
            DumpOpts o; o.indent = 2; write_text(out, dumps(Val::list(ranked), o));
            for (const Val& r : ranked)
                print(rjust(std::to_string(at(r, "score").i), 3) + "  " + ljust(head(at(r, "name"), 30), 30) + " " + ljust(head(orv(g(r, "who"), sv("")), 20), 20) + " " + ps(orv(at(r, "why"), sv("-"))));
            print("Saved " + std::to_string(ranked.size()) + " leads to " + out);
            return 0;
        }
        Val data = load(file);
        if (cmd == "add") {
            const Val ranked = loads(read_text(S(a, "leads")));
            Val value = get(a, "value"); const double x = value.num();
            if (std::isnan(x)) throw PyErr("ValueError", "cannot convert float NaN to integer");
            if (std::isinf(x)) throw PyErr("OverflowError", "cannot convert float infinity to integer");
            if (x == std::trunc(x)) value = Val::integer(static_cast<long long>(x)); else value = Val::flt(x);
            const auto [added, skipped] = add_leads(data, lst(ranked), get(a, "top").i, value, today_arg(get(a, "today")));
            save(data, file);
            print("Added " + std::to_string(added.size()) + " leads: " + join(added, ", "));
            for (const auto& [name, why] : skipped) print("  skipped " + name + ": " + why);
        } else if (cmd == "draft") {
            const Val openers = loads(read_text(packtools::skill_file("openers.json")));
            const Draft d = draft(data, openers, S(a, "contact"), get(a, "gap"), get(a, "offer"));
            print("To: " + d.to + "\nSubject: " + d.subject + "\n\n" + d.body);
            if (!d.missing.empty()) print("\nFill before it can go: " + join(d.missing, ", "));
        } else if (cmd == "record") {
            Val text = get(a, "text");
            if (truthy(get(a, "text_file"))) text = sv(strip(read_text(S(a, "text_file"))));
            if (!truthy(text)) throw ProspectError("Give --text or --text-file: exactly what went out.");
            const auto [ok, msg] = record(data, S(a, "contact"), text.s, get(a, "approval"), S(a, "at"), get(a, "subject"));
            if (ok) save(data, file);
            print(msg);
            return ok ? 0 : 1;
        }
    } catch (const ProspectError& e) { eprint(e.what()); return 1; }
    catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const OSError& e) { eprint(e.what()); return 1; }
    return 0;
}
PACKTOOL("prospector/leads", main);
}  // namespace packtools::closer_prospect
