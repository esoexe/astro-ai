// researcher/brief: reads a business's own public website (the home page plus up to 3 of its about, services, team
// and contact pages), pulls out the facts and writes a one-page brief to closer/research/<contact-id>.md.
// A C++ port of closer-skill/researcher/brief.py (MIT, Jake Schincariol). The brief leaves [Astro: ...] lines for
// the assistant to finish (the upstream writes [Claude: ...]; the skill text is rebranded to match).
namespace packtools::closer_brief {
using namespace pyrt;
using namespace packtools::closer;
inline const char* UA = "CloserSkill/1.0 (+https://github.com/Jakeschincariol/closer-skill; reads public pages only)";
inline constexpr std::size_t MAX_PAGES = 4;
inline const char* WANT = R"(\b(about|our story|services|treatments|what we do|menu|team|staff|meet|doctors?|contact|pricing)\b)";
inline const char* PHONE_RE = R"((?:\+1[\s.-]?)?\(?\b\d{3}\)?[\s.-]\d{3}[\s.-]\d{4}\b)";
inline const char* EMAIL_RE = R"(\b[\w.+-]+@[\w-]+\.[\w.-]+\b)";
inline const char* SINCE_RE = R"(\b(?:since|established|est\.?|founded in|serving [^.]{0,40} since)\s*(19\d\d|20\d\d)\b)";
inline const char* RATING_RE = R"x("ratingValue"\s*:\s*"?([\d.]+)"?.{0,120}?"reviewCount"\s*:\s*"?(\d+))x";
inline const char* SOCIAL_RE = R"(https?://(?:www\.)?(facebook|instagram|linkedin|tiktok|youtube|x|twitter|yelp)\.com/[^"'\s<>]+)";
inline const std::vector<std::pair<std::string, std::string>>& tools() {
    static const std::vector<std::pair<std::string, std::string>> T = {
        {"booking", R"(calendly|acuityscheduling|zocdoc|mindbody|booksy|vagaro|setmore|janeapp|nexhealth|book (an |your )?(appointment|online|now))"},
        {"ordering", R"(toasttab|chownow|order\.online|order (online|now))"},
        {"google ads tag", R"(googleadservices|AW-\d{6,})"},
        {"chat widget", R"(intercom|drift\.com|tawk\.to|livechat|podium)"}};
    return T;
}
inline std::string text_of(const std::string& fragment) {
    return strip(html_unescape(re::sub(R"(\s+)", " ", re::compile("<[^>]+>").sub(" ", fragment))));
}
inline std::string rstrip_slash(std::string s) { while (!s.empty() && s.back() == '/') s.pop_back(); return s; }
// Up to MAX_PAGES-1 same-site links whose text says about / services / team / contact.
inline std::vector<std::string> pick_pages(const std::string& home_url, const std::string& page) {
    const std::string host = urlp::urlparse(home_url).netloc;
    std::vector<std::string> picked;
    for (const auto& m : re::compile(R"(<a[^>]+href=["']([^"'#]+)["'][^>]*>(.*?)</a>)", re::I | re::S).finditer(page)) {
        const std::string url = urlp::urljoin(home_url, m.group(1));
        if (urlp::urlparse(url).netloc != host || rstrip_slash(url) == rstrip_slash(home_url)) continue;
        if (re::compile(WANT, re::I).search(text_of(m.group(2))) && std::find(picked.begin(), picked.end(), url) == picked.end()) picked.push_back(url);
        if (picked.size() >= MAX_PAGES - 1) break;
    }
    return picked;
}
inline bool ends_with(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }
inline void add_unique(const Val& obj, const char* key, const Val& v) { for (const Val& x : lstk(obj, key)) if (repr(x) == repr(v)) return; lstk(obj, key).push_back(v); }
inline bool contains(const Val& list, const std::string& s) { for (const Val& x : lst(list)) if (is(x, s)) return true; return false; }
// pages: (url, html), home page first. Business facts with the page each one came from.
inline Val facts(const std::vector<std::pair<std::string, std::string>>& pages) {
    Val out = Val::dict();
    for (const char* k : {"title", "description"}) out.d->set(k, Val());
    for (const char* k : {"headings", "phones", "emails"}) out.d->set(k, Val::list());
    out.d->set("since", Val()); out.d->set("rating", Val());
    for (const char* k : {"socials", "tools_seen", "tools_missing"}) out.d->set(k, Val::list());
    List sources; for (const auto& [u, _] : pages) sources.push_back(sv(u));
    out.d->set("sources", Val::list(sources));
    for (const auto& [url, page] : pages) {
        if (at(out, "title").is_none()) { const auto m = re::compile(R"(<title[^>]*>(.*?)</title>)", re::I | re::S).search(page); out.d->set("title", m ? sv(text_of(m.group(1))) : Val()); }
        if (at(out, "description").is_none()) {
            const auto m = re::compile(R"(<meta[^>]+name=["']description["'][^>]+content=["']([^"']*)["'])", re::I).search(page);
            out.d->set("description", m ? sv(strip(html_unescape(m.group(1)))) : Val());
        }
        for (const auto& m : re::compile(R"(<h([12])[^>]*>(.*?)</h\1>)", re::I | re::S).finditer(page)) {
            const std::string t = text_of(m.group(2));
            if (!t.empty() && !contains(at(out, "headings"), t) && lstk(out, "headings").size() < 12) lstk(out, "headings").push_back(sv(t));
        }
        const std::string visible = text_of(page);
        for (const std::string& ph : re::compile(PHONE_RE).findall(visible)) add_unique(out, "phones", sv(ph));
        for (const std::string& em : re::compile(EMAIL_RE).findall(visible)) {
            const std::string l = lower(em);
            if (!contains(at(out, "emails"), l) && !ends_with(l, ".png") && !ends_with(l, ".jpg") && !ends_with(l, ".webp")) lstk(out, "emails").push_back(sv(l));
        }
        if (const auto m = re::compile(SINCE_RE, re::I).search(visible); m && !truthy(at(out, "since"))) out.d->set("since", Val::integer(std::stoll(m.group(1))));
        if (const auto m = re::compile(RATING_RE, re::I | re::S).search(page); m && !truthy(at(out, "rating"))) {
            const auto v = parse_float(m.group(1));
            if (!v) throw PyErr("ValueError", "could not convert string to float: " + str_repr(m.group(1)));
            Val r = Val::dict(); r.d->set("value", Val::flt(*v)); r.d->set("count", Val::integer(std::stoll(m.group(2)))); r.d->set("source", sv(url));
            out.d->set("rating", r);
        }
        for (const auto& m : re::compile(SOCIAL_RE, re::I).finditer(page)) add_unique(out, "socials", sv(rstrip_slash(m.group(0))));
    }
    std::vector<std::string> bodies; for (const auto& [_, pg] : pages) bodies.push_back(pg);
    const std::string every = join(bodies, " ");
    for (const auto& [name, rxs] : tools()) lstk(out, re::compile(rxs, re::I).search(every) ? "tools_seen" : "tools_missing").push_back(sv(name));
    return out;
}
struct SiteError : std::runtime_error { using std::runtime_error::runtime_error; };
inline std::vector<std::pair<std::string, std::string>> gather(std::string url, double wait = 1.0) {
    if (!re::compile("^https?://").match(url)) url = "https://" + url;
    const auto home = closer_prospect::fetch_page(url, UA);
    if (!home.ok) throw SiteError(home.error);
    std::vector<std::pair<std::string, std::string>> pages = {{url, home.body}};
    for (const std::string& link : pick_pages(url, home.body)) {
        if (!(std::getenv("ASTRO_TOOL_PAGES") && std::getenv("ASTRO_TOOL_OFFLINE"))) std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        const auto pg = closer_prospect::fetch_page(link, UA);
        if (!pg.ok) continue;
        bool dup = false; for (auto& [u, b] : pages) if (u == link) { b = pg.body; dup = true; }
        if (!dup) pages.emplace_back(link, pg.body);
    }
    return pages;
}
inline std::string brief(const Val& contact, const Val& deal, const Val& f) {
    std::vector<std::string> lines = {"# Brief: " + ps(orv(g(contact, "company"), at(contact, "name"))), "",
        "Prepared for the call with " + ps(at(contact, "name")) + " (" + ps(orv(g(contact, "title"), sv("contact"))) + ").", "", "## The business", ""};
    if (truthy(at(f, "title"))) lines.push_back("- Site title: " + ps(at(f, "title")));
    if (truthy(at(f, "description"))) lines.push_back("- How they describe themselves: \"" + ps(at(f, "description")) + "\"");
    if (truthy(at(f, "since"))) lines.push_back("- In business since " + std::to_string(at(f, "since").i));
    if (truthy(at(f, "rating"))) lines.push_back("- Rating on their site: " + fmt_f(at(at(f, "rating"), "value").num(), 1) + " from " + std::to_string(at(at(f, "rating"), "count").i) + " reviews");
    const auto first_n = [](const Val& l, std::size_t n) { std::vector<std::string> o; for (const Val& x : lst(l)) { if (o.size() >= n) break; o.push_back(ps(x)); } return o; };
    if (!lst(at(f, "phones")).empty()) lines.push_back("- Phone: " + join(first_n(at(f, "phones"), 2), ", "));
    if (!lst(at(f, "socials")).empty()) lines.push_back("- Social: " + join(first_n(at(f, "socials"), 5), ", "));
    for (const char* l : {"", "## What they offer (their own headings)", ""}) lines.push_back(l);
    for (const std::string& h : first_n(at(f, "headings"), 8)) lines.push_back("- " + h);
    for (const char* l : {"", "## What we noticed", ""}) lines.push_back(l);
    const std::string seen = join(first_n(at(f, "tools_seen"), 99), ", "), missing = join(first_n(at(f, "tools_missing"), 99), ", ");
    lines.push_back("- Seen on the site: " + (seen.empty() ? std::string("none of the usual tools") : seen));
    lines.push_back("- Not seen: " + (missing.empty() ? std::string("-") : missing) + " (a hint, not proof: ask)");
    if (!deal.is_none() && truthy(g(deal, "gaps"))) lines.push_back("- Prospector found: " + join(first_n(at(deal, "gaps"), 1u << 30), ", "));
    for (const char* l : {"", "## Talking points", "", "- [Astro: 3 specific, true points from the facts above]", "", "## Questions to ask", "",
                          "- How do most new customers find you today?", "- What happens when someone wants to book after hours?",
                          "- [Astro: 2 more, specific to this business]", "", "## Sources", ""}) lines.push_back(l);
    for (const Val& s : lst(at(f, "sources"))) lines.push_back("- " + ps(s));
    return join(lines, "\n") + "\n";
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("brief.py", "Research a prospect before the call.");
    p.opt({"--file"}).def(Val::str(path_join("closer", "pipeline.json")));
    p.subcommands("cmd", true);
    { Parser& s = p.sub("build"); s.opt({"--contact"}).required(); s.opt({"--url"}).help("defaults to the contact's website"); }
    { Parser& s = p.sub("facts"); s.opt({"--url"}).required(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        if (cmd == "facts") { DumpOpts o; o.indent = 2; print(dumps(facts(gather(S(a, "url"))), o)); return 0; }
        const Val data = load(file);
        const Val* cp = find(at(data, "contacts"), S(a, "contact"));
        if (!cp) throw PipelineError("No contact " + S(a, "contact"));
        const Val c = *cp;
        const Val url = orv(get(a, "url"), g(c, "website"));
        if (!truthy(url)) throw PipelineError(ps(at(c, "name")) + " has no website. Pass --url.");
        const Val f = facts(gather(str_of(url)));
        Val deal; for (const Val& d : lstk(data, "deals")) if (repr(at(d, "contact_id")) == repr(at(c, "id"))) { deal = d; break; }
        const std::string dir = path_join("closer", "research");
        makedirs(dir);
        const std::string path = path_join(dir, str_of(at(c, "id")) + ".md");
        write_text(path, brief(c, deal, f));
        print("Wrote " + path + " from " + std::to_string(lst(at(f, "sources")).size()) + " pages.");
    } catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const SiteError& e) { eprint(std::string("Could not read the site: ") + e.what()); return 1; }
    catch (const OSError& e) { eprint(std::string("Could not read the site: ") + e.what()); return 1; }
    catch (const PyErr& e) {
        if (e.type != "ValueError" && e.type != "json.decoder.JSONDecodeError" && e.type != "UnicodeDecodeError") throw;
        eprint(std::string("Could not read the site: ") + e.what()); return 1;
    }
    return 0;
}
PACKTOOL("researcher/brief", main);
}  // namespace packtools::closer_brief
