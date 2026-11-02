// seed-dataroom/dataroom: lays out the data room investors expect, checks it against a checklist (present, missing,
// stale), writes INDEX.md, and says what to share before and after a term sheet. It never opens, uploads or shares a
// file. A C++ port of seed-skill/seed-dataroom/dataroom.py (MIT, Jake Schincariol); same output and exit codes.
namespace packtools::seed_dataroom {
using namespace pyrt;
using namespace packtools::seed;
struct RoomError : std::runtime_error { using std::runtime_error::runtime_error; };

inline const std::vector<std::string>& FOLDERS() {
    static const std::vector<std::string> F = {"01 Company", "02 Pitch deck", "03 Financials", "04 Market", "05 Product", "06 Team", "07 Legal", "08 Cap table", "09 Customers"};
    return F;
}
struct Item { std::string folder, key, label, stage; std::vector<std::string> words; long long limit; };   // limit -1 = never stale
inline const std::vector<Item>& CHECKLIST() {
    static const std::vector<Item> C = {
        {"01 Company", "one_pager", "One-pager / executive summary", "before", {"one-pager", "one pager", "onepager", "executive summary", "exec summary", "summary"}, 90},
        {"01 Company", "business_plan", "Business plan", "before", {"business plan", "plan"}, 90},
        {"02 Pitch deck", "deck", "Pitch deck (PDF)", "before", {"deck", "pitch"}, 90},
        {"03 Financials", "model", "Financial model (36 months)", "before", {"model", "forecast", "projection"}, 60},
        {"03 Financials", "use_of_funds", "Use of funds", "before", {"use of funds", "use-of-funds", "budget"}, 90},
        {"03 Financials", "historicals", "Historical financials (P&L, balance sheet)", "after", {"p&l", "pnl", "profit", "income statement", "balance sheet", "historical", "actuals"}, 45},
        {"03 Financials", "bank", "Bank statements (last 3 months)", "after", {"bank", "statement"}, 45},
        {"04 Market", "market_size", "Market sizing (TAM / SAM / SOM)", "before", {"market", "tam", "sizing"}, 180},
        {"04 Market", "competition", "Competitor analysis", "before", {"compet", "landscape"}, 90},
        {"05 Product", "demo", "Product demo (video, link or screenshots)", "before", {"demo", "screenshot", "video", "walkthrough"}, 90},
        {"05 Product", "metrics", "Traction and KPIs", "before", {"metric", "kpi", "traction", "dashboard"}, 45},
        {"05 Product", "roadmap", "Product roadmap", "before", {"roadmap"}, 120},
        {"06 Team", "bios", "Founder and team bios", "before", {"bio", "team", "founder"}, 365},
        {"06 Team", "hiring", "Org chart and hiring plan", "after", {"org", "hiring"}, 180},
        {"06 Team", "ip_assignments", "Employee and contractor IP assignments (PIIA / CIIAA)", "after", {"piia", "ciiaa", "ip assignment", "invention", "employment", "contractor", "offer letter"}, -1},
        {"07 Legal", "incorporation", "Certificate of incorporation and bylaws", "after", {"incorporation", "charter", "bylaws", "articles"}, -1},
        {"07 Legal", "board_consents", "Board consents and minutes", "after", {"consent", "minutes", "board"}, -1},
        {"07 Legal", "ip", "IP: trademarks, patents, domains", "after", {"trademark", "patent", "domain", " ip"}, -1},
        {"07 Legal", "contracts", "Material contracts", "after", {"contract", "agreement", "msa", "lease"}, -1},
        {"08 Cap table", "cap_table", "Cap table (fully diluted)", "after", {"cap table", "captable", "cap-table", "capitalization"}, 30},
        {"08 Cap table", "safes", "SAFEs and convertible notes already signed", "after", {"safe", "convertible", "note"}, -1},
        {"08 Cap table", "option_plan", "Equity incentive plan and option grants", "after", {"option", "equity incentive", "esop", "stock plan", "grant"}, -1},
        {"09 Customers", "testimonials", "Testimonials and reviews", "before", {"testimonial", "review", "case study", "quote"}, 180},
        {"09 Customers", "references", "Customer references (with their OK to be called)", "after", {"reference"}, 180},
        {"09 Customers", "pipeline", "Waitlist, pilots and LOIs (counts, not personal data)", "after", {"waitlist", "pilot", "loi", "letter of intent", "customer"}, 90}};
    return C;
}
inline const std::vector<std::pair<std::string, std::string>>& NEVER() {
    static const std::vector<std::pair<std::string, std::string>> N = {
        {R"(password|passcode|credential|login)", "a password or login"},
        {R"(\.env$|\.pem$|\.key$|\.p12$|\.pfx$|id_rsa|secret|api[ _-]?key|token)", "a key or secret"},
        {R"(passport|driver'?s? licen[cs]e|\bssn\b|social security|national id|id card)", "a personal ID document"},
        {R"(customer (list|emails|export)|user export|email list|contacts export)", "raw customer personal data"}};
    return N;
}
inline std::string share_note(const std::string& st) {
    return st == "before" ? "Share when an investor asks after a first meeting. A view-only link, one per investor, so you can see who opened what."
                          : "Share after a signed term sheet, for confirmatory diligence, ideally under an NDA. These hold personal data, contracts and the full cap table.";
}

inline long long parse_day(const Val& text, const std::string& what = "--today") {
    const std::string t = strip(text.is_none() ? std::string() : pystr(text));
    const re::Match m = re::match(R"(^(\d{4})-(\d{2})-(\d{2})$)", t);
    if (!m) throw RoomError(what + " wants a date like 2026-10-26, got " + repr(text));
    const long long y = to_ll(m.group(1)); const int mo = static_cast<int>(to_ll(m.group(2))), d = static_cast<int>(to_ll(m.group(3)));
    if (!valid_date(y, mo, d)) throw RoomError(what + ": " + pystr(text) + " is not a real date");
    return make_day(y, mo, d);
}
inline long long get_today(const Val& a) { return truthy(get(a, "today")) ? parse_day(get(a, "today")) : today_days(); }
inline std::string room_dir(const Val& a) { return join_path(S(a, "dir"), "dataroom"); }

inline std::string norm(const std::string& name) { return " " + re::sub(R"([_\s]+)", " ", lower(splitext_root(name))) + " "; }

// a date written in the file name: 2026-10-01, 2026_10_01, 20261001 or 2026-10 (the first of the month); -1 = none
inline long long name_date(const std::string& name) {
    const re::Match m = re::search(R"((20\d\d)[-_.]?(0[1-9]|1[0-2])[-_.]?(0[1-9]|[12]\d|3[01])(?!\d))", name);
    if (m) {
        const long long y = to_ll(m.group(1)); const int mo = static_cast<int>(to_ll(m.group(2))), d = static_cast<int>(to_ll(m.group(3)));
        if (valid_date(y, mo, d)) return make_day(y, mo, d);
    }
    const re::Match m2 = re::search(R"((20\d\d)[-_.](0[1-9]|1[0-2])(?![\d]))", name);
    if (m2) return make_day(to_ll(m2.group(1)), static_cast<int>(to_ll(m2.group(2))), 1);
    return -1;
}
inline std::pair<long long, std::string> file_date(const std::string& path) {
    const long long d = name_date(basename(path));
    if (d != -1) return {d, "name"};
    return {mtime_utc_day(path), "modified"};
}

struct F { std::string top, rel, full; };
inline void walk_into(const std::string& root, const std::string& base, const std::string& relbase, std::vector<F>& out) {
    std::vector<std::string> dirs, files; list_dir(base, dirs, files);
    for (const auto& f : files) {
        if (!f.empty() && f[0] == '.') continue;
        if (relbase.empty() && f == "INDEX.md") continue;
        const std::string rel = relbase.empty() ? f : relbase + SEP + f;
        const std::size_t k = rel.find(SEP);
        out.push_back({k == std::string::npos ? std::string() : rel.substr(0, k), rel, join_path(base, f)});
    }
    for (const auto& d : dirs) {
        if (!d.empty() && d[0] == '.') continue;
        const std::string full = join_path(base, d);
        std::error_code ec; if (std::filesystem::is_symlink(std::filesystem::symlink_status(fspath(full), ec))) continue;
        walk_into(root, full, relbase.empty() ? d : relbase + SEP + d, out);
    }
}
inline std::vector<F> walk(const std::string& root) { std::vector<F> out; if (is_dir(root)) walk_into(root, root, "", out); return out; }

inline std::string never_reason(const std::string& name) {   // "" = fine
    const std::string low = lower(name);
    for (const auto& [pat, what] : NEVER()) if (re::search(pat, low)) return what;
    return "";
}

struct Hit { std::string file, date, how; long long age; };
struct Row { const Item* it; std::string state; std::vector<Hit> hits; };
struct Survey { std::string root; long long today; std::vector<Row> items; Val other, loose, flagged, empty; std::size_t nfiles = 0; };

inline const Hit& newest(const std::vector<Hit>& h) { std::size_t b = 0; for (std::size_t k = 1; k < h.size(); ++k) if (h[k].age < h[b].age) b = k; return h[b]; }

inline Survey survey(const Val& a, long long today) {
    Survey s; s.root = room_dir(a); s.today = today;
    if (!is_dir(s.root)) throw RoomError("no data room at " + s.root + ": run dataroom.py init first");
    const std::vector<F> files = walk(s.root);
    std::vector<std::string> used;
    for (const Item& it : CHECKLIST()) {
        Row r; r.it = &it; r.state = "missing";
        for (const F& f : files) {
            if (f.top != it.folder || !never_reason(f.rel).empty()) continue;
            const std::string n = norm(basename(f.rel));
            bool any = false; for (const auto& w : it.words) if (n.find(w) != std::string::npos) { any = true; break; }
            if (!any) continue;
            const auto [d, how] = file_date(f.full);
            r.hits.push_back({f.rel, iso_day(d), how, today - d});
            if (std::find(used.begin(), used.end(), f.rel) == used.end()) used.push_back(f.rel);
        }
        if (!r.hits.empty()) r.state = (it.limit >= 0 && newest(r.hits).age > it.limit) ? "stale" : "present";
        s.items.push_back(std::move(r));
    }
    s.other = Val::list(); s.loose = Val::list(); s.flagged = Val::list(); s.empty = Val::list();
    for (const F& f : files) {
        const std::string why = never_reason(f.rel);
        if (!why.empty()) s.flagged.l->push_back(obj({{"file", sv(f.rel)}, {"why", sv(why)}}));
        else if (f.top.empty() || std::find(FOLDERS().begin(), FOLDERS().end(), f.top) == FOLDERS().end()) s.loose.l->push_back(sv(f.rel));
        else if (std::find(used.begin(), used.end(), f.rel) == used.end()) {
            const auto [d, how] = file_date(f.full);
            s.other.l->push_back(obj({{"file", sv(f.rel)}, {"date", sv(iso_day(d))}, {"folder", sv(f.top)}}));
        }
    }
    for (const auto& fo : FOLDERS()) {
        bool any = false; for (const F& f : files) if (f.top == fo) { any = true; break; }
        if (!is_dir(join_path(s.root, fo)) || !any) s.empty.l->push_back(sv(fo));
    }
    s.nfiles = files.size();
    return s;
}
inline Val item_val(const Row& r) {
    Val hits = Val::list();
    for (const Hit& h : r.hits) hits.l->push_back(obj({{"file", sv(h.file)}, {"date", sv(h.date)}, {"dated_by", sv(h.how)}, {"age_days", iv(h.age)}}));
    return obj({{"folder", sv(r.it->folder)}, {"key", sv(r.it->key)}, {"label", sv(r.it->label)}, {"stage", sv(r.it->stage)},
                {"max_age_days", r.it->limit >= 0 ? iv(r.it->limit) : Val()}, {"state", sv(r.state)}, {"files", hits}});
}

inline std::string startup_name(const Val& a) {
    try {
        const Val doc = loads(read_text(join_path(S(a, "dir"), "config.json")));
        const Val st = get(doc, "startup");
        const Val name = truthy(st) ? get(st, "name") : Val();
        return truthy(name) ? pystr(name) : "Your startup";
    } catch (const PyErr&) { return "Your startup"; }
}

inline int cmd_init(const Val& a) {
    const std::string root = room_dir(a);
    std::vector<std::string> made;
    for (const auto& f : FOLDERS()) { const std::string p = join_path(root, f); if (!is_dir(p)) { makedirs(p); made.push_back(f); } }
    print("Data room: " + root + " (" + (made.empty() ? std::string("every folder was already there; nothing changed")
                                         : "created " + std::to_string(made.size()) + " folder" + (made.size() == 1 ? "" : "s")) + ").");
    for (const auto& f : FOLDERS()) {
        std::vector<std::string> want; for (const Item& it : CHECKLIST()) if (it.folder == f) want.push_back(it.label);
        print("  " + ljust(f, 14) + " " + join(want, "; "));
    }
    print("From this pack: seed-deck's seed/deck.html (print it to PDF) goes in 02 Pitch deck, seed-plan's seed/plan.md in 01 Company, seed-model's export in 03 Financials, seed-market and seed-scout in 04 Market.");
    print("Then: dataroom.py check.");
    return 0;
}

inline std::vector<std::string> stages_of(const Val& a) { const std::string st = S(a, "stage"); return st == "all" ? std::vector<std::string>{"before", "after"} : std::vector<std::string>{st}; }
inline bool in(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

inline int cmd_check(const Val& a) {
    const long long today = get_today(a);
    const Survey s = survey(a, today);
    const auto stages = stages_of(a);
    std::vector<const Row*> items, bad;
    for (const Row& r : s.items) if (in(stages, r.it->stage)) { items.push_back(&r); if (r.state != "present") bad.push_back(&r); }
    const bool ok = bad.empty() && s.flagged.size() == 0;
    if (truthy(get(a, "json"))) {
        Val il = Val::list(); for (const Row* r : items) il.l->push_back(item_val(*r));
        const Val doc = obj({{"root", sv(s.root)}, {"today", sv(iso_day(today))}, {"items", il}, {"other", s.other}, {"loose", s.loose},
                             {"flagged", s.flagged}, {"empty_folders", s.empty}, {"files", iv(static_cast<long long>(s.nfiles))}, {"ok", bv(ok)}});
        DumpOpts o; o.indent = 2; print(dumps(doc, o));
        return ok ? 0 : 1;
    }
    long long present = 0; for (const Row* r : items) if (r->state == "present") ++present;
    const std::string st = S(a, "stage");
    print(startup_name(a) + " data room, " + iso_day(today) + ": " + std::to_string(present) + " of " + std::to_string(items.size()) + " items present" +
          (st == "all" ? std::string() : " (" + st + " a term sheet)") + ".");
    for (const auto& f : FOLDERS()) {
        std::vector<const Row*> rows; for (const Row* r : items) if (r->it->folder == f) rows.push_back(r);
        if (rows.empty()) continue;
        print(f);
        for (const Row* r : rows) {
            const std::string mark = r->state == "present" ? "ok     " : r->state == "missing" ? "MISSING" : "STALE  ";
            std::string detail;
            if (!r->hits.empty()) {
                const Hit& n = newest(r->hits);
                const std::size_t k = n.file.find(SEP);
                detail = "  " + (k == std::string::npos ? n.file : n.file.substr(k + 1)) + " (" + n.date + ", " + std::to_string(n.age) + " day" + (n.age == 1 ? "" : "s") + " old" +
                         (r->state == "stale" ? ", limit " + std::to_string(r->it->limit) : std::string()) + ")";
            }
            print("  " + mark + " " + r->it->label + " [" + r->it->stage + "]" + detail);
        }
    }
    for (const Val& o : *s.other.l) print("  other: " + get(o, "file").s + " (not on the checklist; index describes it as 'other')");
    for (const Val& f : *s.loose.l) print("  LOOSE: " + f.s + " is outside the nine folders: move it into one");
    for (const Val& f : *s.flagged.l) print("  DO NOT SHARE: " + get(f, "file").s + " looks like " + get(f, "why").s + ". Take it out of the data room.");
    if (!bad.empty()) {
        std::vector<std::string> parts; for (const Row* r : bad) parts.push_back(r->it->label + " " + r->state);
        print(std::to_string(bad.size()) + " to fix: " + join(parts, "; ") + ".");
    } else print("Everything on the list for this stage is there and fresh.");
    return ok ? 0 : 1;
}

inline std::string human_size(unsigned long long n0) {
    if (n0 < 1024) return std::to_string(n0) + " B";
    double n = static_cast<double>(n0) / 1024.0;
    for (const char* unit : {"KB", "MB", "GB"}) {
        if (n < 1024 || std::string(unit) == "GB") { char b[64]; std::snprintf(b, sizeof b, "%.1f %s", n, unit); return b; }
        n /= 1024.0;
    }
    return "";
}

inline int cmd_index(const Val& a) {
    const long long today = get_today(a);
    const Survey s = survey(a, today);
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> known;   // describe(): the first item wins
    for (const Row& r : s.items) for (const Hit& h : r.hits) {
        bool have = false; for (const auto& k : known) if (k.first == h.file) { have = true; break; }
        if (!have) known.push_back({h.file, {r.it->label, r.it->stage}});
    }
    const std::size_t nflag = s.flagged.size();
    std::vector<std::string> lines = {"# " + startup_name(a) + " data room", "",
        "Index written " + iso_day(today) + " by seed-dataroom. " + std::to_string(s.nfiles - nflag) + " files. \"Share\" says when: **before** = after a first meeting, on request; **after** = after a signed term sheet, for diligence.", ""};
    const std::vector<F> all = walk(s.root);
    for (const auto& fo : FOLDERS()) {
        lines.push_back("## " + fo); lines.push_back("");
        std::vector<std::string> rows;
        for (const F& f : all) {
            if (f.top != fo || !never_reason(f.rel).empty()) continue;
            std::string what = "Other", stage = "ask first";
            for (const auto& k : known) if (k.first == f.rel) { what = k.second.first; stage = k.second.second; break; }
            const auto [d, how] = file_date(f.full);
            std::string name = f.rel.substr(f.rel.find(SEP) + 1); for (char& c : name) if (c == '|') c = '/';
            rows.push_back("| " + name + " | " + what + " | " + iso_day(d) + " | " + human_size(file_size(f.full)) + " | " + stage + " |");
        }
        if (!rows.empty()) { lines.push_back("| File | What it is | Dated | Size | Share |"); lines.push_back("| --- | --- | --- | --- | --- |"); lines.insert(lines.end(), rows.begin(), rows.end()); }
        else lines.push_back("Empty.");
        std::vector<std::string> missing, stale;
        for (const Row& r : s.items) if (r.it->folder == fo) { if (r.state == "missing") missing.push_back(r.it->label); else if (r.state == "stale") stale.push_back(r.it->label); }
        if (!missing.empty()) { lines.push_back(""); lines.push_back("Missing: " + join(missing, "; ") + "."); }
        if (!stale.empty()) { lines.push_back(""); lines.push_back("Out of date: " + join(stale, "; ") + "."); }
        lines.push_back("");
    }
    if (s.loose.size()) { lines.push_back("## Not in a folder"); lines.push_back(""); for (const Val& x : *s.loose.l) lines.push_back("- " + x.s); lines.push_back(""); }
    if (nflag) { lines.push_back("Left out of this index on purpose: " + std::to_string(nflag) + " file" + (nflag == 1 ? "" : "s") + " that should not be in a data room."); lines.push_back(""); }
    const std::string path = join_path(s.root, "INDEX.md");
    write_text(path, rstrip(join(lines, "\n")) + "\n");
    long long nmiss = 0, nstale = 0; for (const Row& r : s.items) { if (r.state == "missing") ++nmiss; else if (r.state == "stale") ++nstale; }
    print("Wrote " + path + ": " + std::to_string(s.nfiles - nflag) + " files in " + std::to_string(FOLDERS().size()) + " folders, " + std::to_string(nmiss) +
          " checklist items missing, " + std::to_string(nstale) + " out of date.");
    for (const Val& f : *s.flagged.l) print("  DO NOT SHARE: " + get(f, "file").s + " looks like " + get(f, "why").s + "; it is not in the index.");
    return 0;
}

inline int cmd_share(const Val& a) {
    const long long today = get_today(a);
    const Survey s = survey(a, today);
    const auto stages = stages_of(a);
    Val out = Val::dict();
    for (const auto& st : stages) {
        Val share = Val::list(), missing = Val::list();
        for (const Row& r : s.items) {
            if (r.it->stage != st) continue;
            if (!r.hits.empty()) { Val fl = Val::list(); for (const Hit& h : r.hits) fl.l->push_back(sv(h.file)); share.l->push_back(obj({{"label", sv(r.it->label)}, {"files", fl}, {"state", sv(r.state)}})); }
            else missing.l->push_back(sv(r.it->label));
        }
        out.d->set(st, obj({{"note", sv(share_note(st))}, {"share", share}, {"missing", missing}}));
    }
    out.d->set("never", strlist({"passwords, keys and logins", "personal ID documents", "customers' personal data (share counts, not names and emails)",
                                 "other investors' private terms or side letters, unless they agree", "anything you are not allowed to share under a contract"}));
    if (truthy(get(a, "json"))) { DumpOpts o; o.indent = 2; print(dumps(out, o)); return 0; }
    for (const auto& st : stages) {
        const Val& o = *out.d->get(st);
        print(std::string(st == "before" ? "BEFORE a term sheet" : "AFTER a signed term sheet") + ": " + get(o, "note").s);
        for (const Val& r : *get(o, "share").l) {
            print("  " + get(r, "label").s + (get(r, "state").s == "stale" ? " (STALE: refresh it first)" : ""));
            for (const Val& f : *get(r, "files").l) print("      " + f.s);
        }
        for (const Val& m : *get(o, "missing").l) print("  missing: " + m.s);
    }
    std::vector<std::string> nv; for (const Val& x : *get(out, "never").l) nv.push_back(x.s);
    print("Never in the room: " + join(nv, "; ") + ".");
    for (const Val& f : *s.flagged.l) print("  DO NOT SHARE: " + get(f, "file").s + " looks like " + get(f, "why").s + ".");
    print("This tool shares nothing. The user decides who gets the link, and when.");
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("dataroom.py", "Data Room Builder: the folders, the checklist, the index and the share list. It never shares anything.");
    p.subcommands("cmd", false);
    auto base = [](Parser& s) {
        s.opt({"--dir"}).def("seed").help("the seed folder (default ./seed); the room is <dir>/dataroom");
        s.opt({"--today"}).help("YYYY-MM-DD (default: today on this computer)");
    };
    { Parser& s = p.sub("init"); s.help_line = "create the nine folders"; base(s); }
    { Parser& s = p.sub("check"); s.help_line = "present / missing / stale against the checklist"; base(s); s.opt({"--stage"}).choices({"before", "after", "all"}).def("all"); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("index"); s.help_line = "write seed/dataroom/INDEX.md"; base(s); }
    { Parser& s = p.sub("share-list"); s.help_line = "what to share before and after a term sheet"; base(s); s.opt({"--stage"}).choices({"before", "after", "all"}).def("all"); s.opt({"--json"}).store_true(); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { flush(); const std::string t = ap::help_text(ap::norm(p, p.prog)); std::fwrite(t.data(), 1, t.size(), stdout); return 1; }
    try {
        if (cmd == "init") return cmd_init(a);
        if (cmd == "check") return cmd_check(a);
        if (cmd == "index") return cmd_index(a);
        return cmd_share(a);
    } catch (const RoomError& e) { eprint(std::string("dataroom.py: ") + e.what()); return 1; }
}
PACKTOOL("seed-dataroom/dataroom", main);
}  // namespace packtools::seed_dataroom
