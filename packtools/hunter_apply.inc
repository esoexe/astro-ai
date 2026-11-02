// hunter-apply/answers: application answers, checklist and draft record (never submits), a C++ port of
// hunter-skill/skills/hunter-apply/answers.py (MIT, Jake Schincariol). Same subcommands, output, JSON and exit codes.
//
// Regex notes. The BANK patterns are re.I and Unicode-aware in Python; here they run over the interview port's shadow
// (one byte per code point; Python's \w letters/numbers -> '_', other whitespace -> '\f', the four letters re.I folds
// onto ASCII -> those letters), with '\r' made a '\f' too: Python's '.' matches it, ECMAScript's does not. The one
// non-ASCII literal, "résumé", is looked for in the lower-cased text instead.
namespace packtools::hunter_apply {
using namespace pyrt;
namespace hi = packtools::hunter_interview;

struct ApplyError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Rule { std::string key; std::regex rx; std::string kind; };
inline const std::vector<Rule>& bank() {
    static const std::vector<Rule> b = [] {
        const std::vector<std::array<const char*, 3>> src = {
            {"self_id.gender", "\\bgender\\b|\\bsex\\b", "sensitive"},
            {"self_id.race_ethnicity", "\\brace\\b|ethnicit|hispanic|latino", "sensitive"},
            {"self_id.veteran_status", "veteran|military service|armed forces", "sensitive"},
            {"self_id.disability_status", "disabilit", "sensitive"},
            {"salary_history", "(current|previous|past|prior)\\s+(salary|compensation|pay)|salary history", "sensitive"},
            {"sponsorship", "sponsor", "fact"},
            {"work_authorization", "(legally )?(authori[sz]ed|eligible|permitted) to work|work authori[sz]ation|right to work", "fact"},
            {"salary_expectation", "(desired|expected|expectation|target)s?\\b.*(salary|compensation|pay)|salary expectation|"
                                   "compensation expectation|(salary|compensation) requirement", "ask_if_empty"},
            {"start_date", "start date|when can you start|earliest (start|available)|availability to start|notice period", "fact"},
            {"relocation", "relocat", "fact"},
            {"onsite", "on-?site|in[- ]office|hybrid|commute|work from (the )?office", "fact"},
            {"how_did_you_hear", "how did you (hear|find|learn)|where did you (hear|find|see)|source of (application|referral)", "fact"},
            {"referral", "referr", "ask_if_empty"},
            {"previously_employed", "(previously|ever|formerly) (been )?(employed|worked)|worked (for|at) .* before|former employee", "fact"},
            {"why_this_company", "why (do you want|are you interested|this company|us\\b|join)|what interests you|why .* (role|position|team)", "role"},
            {"cover_letter", "cover letter|anything else you.*(share|tell)|additional information", "role"},
            {"why_leaving", "why (are you )?(leaving|looking)|reason for leaving", "ask_if_empty"},
            {"pronouns", "pronoun", "optional"},
            {"links.linkedin", "linkedin", "fact"},
            {"links.github", "github", "fact"},
            {"links.website", "website|portfolio|personal site|blog", "fact"},
            {"years_experience", "years of (professional |relevant |work )?experience|how many years", "fact"},
            {"current.company", "current (company|employer)|(company|employer) name", "fact"},
            {"current.title", "current (title|role|position)|job title", "fact"},
            {"education.school", "school|university|college|institution", "fact"},
            {"education.degree", "degree|qualification|field of study|major", "fact"},
            {"education.year", "graduation|year (of )?(graduat|complet)", "fact"},
            {"first_name", "first name|given name|preferred name", "fact"},
            {"last_name", "last name|family name|surname", "fact"},
            {"name", "(full |legal )?name", "fact"},
            {"email", "e-?mail", "fact"},
            {"phone", "phone|mobile|cell", "fact"},
            {"location", "location|city|where are you (based|located)|current address|address", "fact"},
            {"resume", "resume|\\bcv\\b", "file"},   // + "résumé", below
        };
        std::vector<Rule> o;
        for (const auto& r : src) o.push_back({r[0], std::regex(r[1], std::regex::icase), r[2]});
        return o;
    }();
    return b;
}

// lower() plus the letters Python's re.I folds onto ASCII (for the one non-ASCII literal)
inline std::string fold_lower(const std::string& s) {
    std::string o;
    for (const auto& ch : cps(s)) {
        const uint32_t c = cp_of(ch);
        if (c == 0x130 || c == 0x131) o += 'i';
        else if (c == 0x17F) o += 's';
        else if (c == 0x212A) o += 'k';
        else o += lower(ch);
    }
    return o;
}
inline std::string label_shadow(const std::string& s) {
    std::string sh = hi::shadow(s, true);
    for (char& c : sh) if (c == '\r') c = '\f';
    return sh;
}
inline std::pair<Val, std::string> classify(const std::string& label) {
    const std::string sh = label_shadow(label);
    for (const auto& r : bank()) {
        bool hit = std::regex_search(sh, r.rx);
        if (!hit && r.key == "resume") hit = fold_lower(label).find("r\xC3\xA9sum\xC3\xA9") != std::string::npos;
        if (hit) return {Val::str(r.key), r.kind};
    }
    return {Val(), "unknown"};
}

inline Val load_json(const std::string& path, const Val& dflt = Val()) {
    if (!path.empty() && is_file(path)) return loads(read_text(path));
    return dflt;
}
inline void save_json(const std::string& path, const Val& data) {
    const std::string folder = dirname(path);
    if (!folder.empty()) makedirs(folder);
    DumpOpts o; o.indent = 2; o.ensure_ascii = false;
    write_text(path, dumps(data, o) + "\n");
}
// str.lower() then [^a-z0-9]+ -> "-", strip "-"
inline std::string slug(const std::string& text) {
    std::string o; bool dash = false;
    for (const auto& ch : cps(text)) {
        for (const auto& lc : cps(lower(ch))) {
            const char x = lc.size() == 1 ? lc[0] : '\0';
            if ((x >= 'a' && x <= 'z') || (x >= '0' && x <= '9')) { if (dash && !o.empty()) o += '-'; dash = false; o += x; }
            else dash = true;
        }
    }
    return o;
}
inline Val yes_no(const Val& v) { return v.is_none() ? Val() : Val::str(truthy(v) ? "Yes" : "No"); }
inline Val or_dict(const Val& v) { return truthy(v) ? v : Val::dict(); }
inline std::string text_of(const Val& v, const char* what) {   // a value the Python code calls str methods on
    if (v.t == Val::STR) return v.s;
    throw PyErr("AttributeError", std::string("'") + type_name(v) + "' object has no attribute '" + what + "'");
}

// value for a bank key from the profile, or None (every entry is worked out first, like the Python dict literal)
inline Val lookup(const Val& profile, const std::string& key) {
    const Val p = or_dict(profile);
    const Val a = or_dict(dget(p, "answers")), auth = or_dict(dget(p, "work_authorization"));
    Val edu = Val::dict();
    if (truthy(dget(p, "education"))) {
        const Val e = dget(p, "education");
        if (e.t != Val::LIST) throw PyErr("KeyError", "0");
        edu = (*e.l)[0];
    }
    const Val links = or_dict(dget(p, "links")), cur = or_dict(dget(p, "current")), sid = or_dict(dget(p, "self_identification"));
    const Val name_v = orv(dget(p, "name"), Val::str(""));
    if (key.rfind("self_id.", 0) == 0) return dget(sid, key.substr(8));
    const std::string name = text_of(name_v, "split");
    const auto parts = split_ws(name);
    std::vector<std::pair<std::string, Val>> simple;
    const auto add = [&](const std::string& k, Val v) { simple.emplace_back(k, std::move(v)); };
    add("sponsorship", yes_no(dget(auth, "needs_sponsorship")));
    add("work_authorization", yes_no(dget(auth, "authorized")));
    add("salary_expectation", dget(a, "salary_expectation"));
    add("start_date", orv(dget(a, "start_date"), dget(a, "notice_period")));
    add("relocation", !dget(a, "relocation").is_none() ? dget(a, "relocation") : yes_no(dget(p, "relocate")));
    for (const char* k : {"onsite", "how_did_you_hear", "referral", "previously_employed", "why_leaving", "pronouns"}) add(k, dget(a, k));
    add("links.linkedin", dget(links, "linkedin")); add("links.github", dget(links, "github")); add("links.website", dget(links, "website"));
    add("years_experience", !dget(p, "years_experience").is_none() ? Val::str(pystr(dget(p, "years_experience"))) : Val());
    add("current.company", dget(cur, "company")); add("current.title", dget(cur, "title"));
    add("education.school", dget(edu, "school")); add("education.degree", dget(edu, "degree"));
    add("education.year", truthy(dget(edu, "year")) ? Val::str(pystr(dget(edu, "year"))) : Val());
    if (truthy(dget(p, "first_name"))) add("first_name", dget(p, "first_name"));
    else if (!name.empty()) {
        if (parts.empty()) throw PyErr("IndexError", "list index out of range");
        add("first_name", Val::str(parts[0]));
    } else add("first_name", Val());
    add("last_name", parts.size() > 1 ? Val::str(parts.back()) : Val());
    add("name", name.empty() ? Val() : Val::str(name));
    add("email", dget(p, "email")); add("phone", dget(p, "phone")); add("location", dget(p, "location"));
    add("resume", dget(p, "resume_path")); add("salary_history", Val());
    add("why_this_company", dget(a, "why_this_company")); add("cover_letter", Val());
    for (const auto& [k, v] : simple) if (k == key) return v;
    return Val();
}

// map an answer to one of a select's options, or None
inline Val pick_option(const Val& value, const Val& options) {
    if (!truthy(options) || value.is_none()) return value;
    const List opts = iter(options);
    const std::string v = lower(strip(pystr(value)));
    for (const Val& o : opts) if (lower(strip(pystr(o))) == v) return o;
    for (const Val& o : opts) {
        const std::string ol = lower(strip(pystr(o)));
        if ((v == "yes" || v == "no") && (ol.rfind(v + " ", 0) == 0 || ol.rfind(v + ",", 0) == 0 || ol == v)) return o;
    }
    if (v.rfind("yes", 0) == 0 || v.rfind("no", 0) == 0) {
        const std::string head = v.rfind("yes", 0) == 0 ? "yes" : "no";
        for (const Val& o : opts) if (lower(strip(pystr(o))).rfind(head, 0) == 0) return o;
    }
    return Val();
}

// one form question -> {label, key, answer, status, source, note, required}
inline Val answer(const Val& question, const Val& profile, const Val& role_id, const std::string& workspace, std::optional<Val> options = std::nullopt) {
    Val label;
    if (question.t == Val::STR) label = question;
    else if (question.t == Val::DICT) label = dget(question, "label", Val::str(""));
    else throw PyErr("AttributeError", std::string("'") + type_name(question) + "' object has no attribute 'get'");
    const Val opts = options ? *options : (question.t == Val::DICT ? dget(question, "options") : Val());
    const bool required = question.t == Val::DICT && truthy(dget(question, "required"));
    if (truthy(label) && label.t != Val::STR) throw PyErr("TypeError", "expected string or bytes-like object, got '" + std::string(type_name(label)) + "'");
    const auto [key, kind] = classify(truthy(label) ? label.s : "");
    Val out = Val::dict();
    out.d->set("label", label); out.d->set("key", key); out.d->set("answer", Val()); out.d->set("status", Val::str("ask"));
    out.d->set("source", Val()); out.d->set("note", Val()); out.d->set("required", Val::boolean(required));
    const auto set = [&](const char* k, Val v) { out.d->set(k, std::move(v)); };
    if (kind == "unknown") { set("note", Val::str("not a question the profile answers: ask the user, never guess")); return out; }
    const std::string k = key.s;
    const Val value = lookup(profile, k);
    if (kind == "sensitive") {
        set("status", Val::str("sensitive"));
        if (k == "salary_history")
            set("note", Val::str("salary history: the user's call. Several US states and cities bar employers from asking; leave it blank if the form allows, never estimate it"));
        else if (truthy(value)) {
            set("answer", orv(pick_option(value, opts), value)); set("source", Val::str("profile.self_identification"));
            set("note", Val::str("voluntary: filled with the user's own saved choice"));
        } else set("note", Val::str("voluntary self-identification: the user answers this themselves (or declines)"));
        return out;
    }
    if (kind == "role") {
        const std::string cover = truthy(role_id) ? path_join(path_join(workspace, "covers"), pystr(role_id) + ".md") : "";
        set("status", Val::str("role-specific"));
        if (truthy(value)) { set("answer", value); set("source", Val::str("profile.answers." + k)); }
        else if (!cover.empty() && is_file(cover)) { set("source", Val::str(cover)); set("note", Val::str("use the approved cover letter for this role (or its first paragraph)")); }
        else set("note", Val::str("write it for this role with /hunter-cover, from the research brief; never generic"));
        return out;
    }
    if (kind == "file") {
        set("status", Val::str(truthy(value) ? "filled" : "ask"));
        set("answer", value); set("source", Val::str("profile.resume_path"));
        set("note", Val::str(truthy(role_id) ? "upload the tailored resume for this role if one exists (hunter/resumes/" + pystr(role_id) + ".md, exported to PDF)"
                                             : "the user picks the file in the browser's upload dialog"));
        return out;
    }
    if (value.is_none() || (value.t == Val::STR && value.s.empty())) {
        set("note", Val::str("missing from hunter/profile.json: ask the user, then save the answer there for next time"));
        if (k == "salary_expectation") set("note", Val::str("ask the user: /hunter-salary benchmarks the role from cited sources; a range or \"open to discussing\" is fine where the form allows"));
        if (k == "referral") set("note", Val::str("only a real referral: the name of someone who agreed to refer the user, else leave blank"));
        if (kind == "optional") { set("status", Val::str("skip")); set("note", Val::str("optional: leave blank unless the user wants to answer")); }
        return out;
    }
    const Val chosen = pick_option(value, opts);
    if (truthy(opts) && chosen.is_none()) {
        set("note", Val::str("the saved answer " + repr(value) + " matches none of the options " + pystr(opts) + ": ask the user"));
        return out;
    }
    set("answer", truthy(opts) ? chosen : value);
    set("status", Val::str("filled"));
    set("source", Val::str("profile." + k));
    return out;
}

inline Val fill(const List& questions, const Val& profile, const Val& role_id, const std::string& workspace) {
    List rows;
    for (const Val& q : questions) rows.push_back(answer(q, profile, role_id, workspace));
    Val counts = Val::dict();
    bool ask = false;
    for (const Val& r : rows) {
        const std::string s = get(r, "status").s;
        counts.d->set(s, Val::integer(get(counts, s, Val::integer(0)).i + 1));
        if (s == "ask") ask = true;
    }
    Val o = Val::dict();
    o.d->set("role_id", role_id); o.d->set("questions", Val::list(rows)); o.d->set("counts", counts); o.d->set("ready", Val::boolean(!ask));
    return o;
}

// ---------------------------------------------------------------- pipeline
inline Val load_pipeline(const std::string& path) {
    Val data = orv(load_json(path, Val::dict()), Val::dict());
    if (data.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(data) + "' object has no attribute 'setdefault'");
    for (const char* k : {"roles", "contacts", "threads"}) if (!data.d->has(k)) data.d->set(k, Val::list());
    return data;
}
// notes are dated lines, "YYYY-MM-DD: text", the format /hunter-tracker reads
inline void add_note(Val& rec, const std::string& when, const std::string& text) {
    const Val n = orv(dget(rec, "notes"), Val::str(""));
    rec.d->set("notes", Val::str(strip(rstrip(text_of(n, "rstrip")) + "\n" + when + ": " + text)));
}
inline Val find_role(const Val& pipe, const std::string& rid) {
    for (const Val& r : iter(get(pipe, "roles"))) {
        const Val id = dget(r, "id");
        if (id.t == Val::STR && id.s == rid) return r;
    }
    throw ApplyError("no role " + rid + " in the pipeline (add it with /hunter-scout pick or /hunter-tracker)");
}
inline bool stage_open(const Val& stage) { return stage.is_none() || (stage.t == Val::STR && stage.s == "saved"); }

inline List checklist(const Val& pipe, const std::string& rid, const std::string& ws, const Val& profile) {
    const Val r = find_role(pipe, rid);
    const std::string company = text_of(orv(dget(r, "company"), Val::str("")), "lower");
    const Val plan = load_json(path_join(path_join(ws, "applications"), rid + ".json"));
    List contacts;
    for (const Val& c : iter(get(pipe, "contacts")))
        if (lower(text_of(orv(dget(c, "company"), Val::str("")), "lower")) == lower(company)) contacts.push_back(hi::item(c, "id"));
    bool warm = false;
    for (const Val& t : iter(get(pipe, "threads"))) {
        const Val trid = dget(t, "role_id"), cid = dget(t, "contact_id");
        bool mine = trid.t == Val::STR && trid.s == rid;
        for (const Val& c : contacts) if (!mine && trader_screener::eq(c, cid)) mine = true;
        const Val st = dget(t, "status");
        if (mine && st.t == Val::STR && (st.s == "replied" || st.s == "booked")) warm = true;
    }
    const Val auth = or_dict(dget(or_dict(profile), "work_authorization"));
    const Val app = or_dict(dget(r, "application"));
    bool plan_ok = truthy(plan);
    if (plan_ok) for (const Val& q : iter(dget(plan, "questions", Val::list()))) { const Val s = hi::item(q, "status"); if (s.t == Val::STR && s.s == "ask") plan_ok = false; }
    const Val ast = dget(app, "status");
    const auto is = [](const Val& v, const char* s) { return v.t == Val::STR && v.s == s; };
    const std::string cs = slug(company);
    const std::vector<std::tuple<std::string, bool, std::string>> items = {
        {"Resume tailored for this role", is_file(path_join(path_join(ws, "resumes"), rid + ".md")), "/hunter-resume writes hunter/resumes/" + rid + ".md"},
        {"Cover letter written", is_file(path_join(path_join(ws, "covers"), rid + ".md")), "/hunter-cover writes hunter/covers/" + rid + ".md (skip if the form has no place for one)"},
        {"Company researched", is_file(path_join(path_join(ws, "companies"), cs + ".md")), "/hunter-research writes hunter/companies/" + cs + ".md"},
        {"Work authorization answered in the profile", !dget(auth, "authorized").is_none() && !dget(auth, "needs_sponsorship").is_none(), "set work_authorization in hunter/profile.json"},
        {"Form answers prepared, nothing left to ask", plan_ok, "answers.py fill --questions <form.json> --out hunter/applications/" + rid + ".json"},
        {"Warm intro (optional): the hiring manager replied or a chat is booked", warm, "/hunter-connector"},
        {"Form filled in the browser and saved as a draft, not submitted", is(ast, "draft"), "answers.py draft --role " + rid + " --form-url <url>"},
        {"The user reviewed and submitted it themselves", !stage_open(dget(r, "stage")) || is(ast, "submitted"), "answers.py submitted --role " + rid + " --confirm \"...\""},
    };
    List out;
    for (const auto& [i, d, h] : items) {
        Val o = Val::dict();
        o.d->set("item", Val::str(i)); o.d->set("done", Val::boolean(d)); o.d->set("how", Val::str(h));
        out.push_back(o);
    }
    return out;
}

inline void record_draft(Val& pipe, const std::string& rid, const std::string& form_url, const Val& answers_path, const std::string& today) {
    Val r = find_role(pipe, rid);
    if (!stage_open(dget(r, "stage"))) throw ApplyError(rid + " is already at stage " + pystr(dget(r, "stage")));
    Val app = Val::dict();
    app.d->set("status", Val::str("draft")); app.d->set("saved", Val::str(today)); app.d->set("form_url", Val::str(form_url)); app.d->set("answers", answers_path);
    r.d->set("application", app);
    r.d->set("next_step", Val::str("Review the application draft and submit it yourself"));
    r.d->set("next_date", Val::str(today));
    add_note(r, today, "Application filled and saved as a draft, not submitted (" + form_url + ")");
}

const std::regex SUBMITTED_RE("\\b(submitted|sent|applied|done|clicked submit)\\b", std::regex::icase);
inline void record_submitted(Val& pipe, const std::string& rid, const std::string& confirm, long long when) {
    Val r = find_role(pipe, rid);
    if (!std::regex_search(hi::shadow(confirm, true), SUBMITTED_RE))
        throw ApplyError("record it only after the user says they submitted it themselves (for example \"I submitted it\"); got " + str_repr(confirm));
    if (!r.d->has("application")) r.d->set("application", Val::dict());
    Val app = get(r, "application");
    if (app.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(app) + "' object does not support item assignment");
    app.d->set("status", Val::str("submitted"));
    app.d->set("confirmed", Val::str(confirm));
    r.d->set("stage", Val::str("applied"));
    r.d->set("applied", Val::str(date_iso(when)));
    r.d->set("next_step", Val::str("Follow up if nothing in 7 days (/hunter-followup)"));
    r.d->set("next_date", Val::str(date_iso(when + 7)));
    add_note(r, date_iso(when), "Application submitted by the user (" + confirm + ")");
}

// ---------------------------------------------------------------- CLI
// re.match(r"(\d{4})-(\d{2})-(\d{2})$", value), then datetime.date (whose ValueError is not caught)
inline long long day(const std::string& value) {
    std::string v = value;
    if (!v.empty() && v.back() == '\n') v.pop_back();   // $ also matches before a final newline
    static const std::regex D("^([0-9]{4})-([0-9]{2})-([0-9]{2})$");
    std::smatch m;
    if (!std::regex_match(v, m, D)) throw ApplyError("dates are YYYY-MM-DD");
    return py_date(std::stoll(m[1]), std::stoll(m[2]), std::stoll(m[3]));
}
inline long long today_days() {
    const std::string t = today_iso();
    return py_date(std::stoll(t.substr(0, 4)), std::stoll(t.substr(5, 2)), std::stoll(t.substr(8, 2)));
}

inline std::string render_fill(const Val& res) {
    std::vector<std::string> lines;
    const std::map<std::string, std::string> marks = {{"filled", "fill"}, {"ask", "ASK "}, {"sensitive", "USER"}, {"role-specific", "ROLE"}, {"skip", "skip"}};
    for (const Val& q : *get(res, "questions").l) {
        const Val a = get(q, "answer");
        const std::string st = get(q, "status").s;
        const auto mk = marks.find(st);
        lines.push_back((mk != marks.end() ? mk->second : st) + "  " + ljust(cpslice(text_of(get(q, "label"), "__getitem__"), 0, 55), 55) + " " + (a.is_none() ? "" : pystr(a)));
        if (truthy(get(q, "note"))) lines.push_back("      " + pystr(get(q, "note")));
    }
    const Val c = get(res, "counts");
    const auto n = [&](const char* k) { return std::to_string(get(c, k, Val::integer(0)).i); };
    lines.push_back(n("filled") + " filled from the profile, " + n("ask") + " to ask the user, " + n("sensitive") + " for the user alone, " + n("role-specific") + " written for this role.");
    lines.push_back("Fill only the 'fill' rows. Stop before Submit: the user reviews and submits.");
    return join(lines, "\n") + "\n";
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("answers", "Application answers, checklist and draft record. Never submits.");
    p.opt({"--profile"}).def(Val::str("hunter/profile.json"));
    p.opt({"--pipeline"}).def(Val::str("hunter/pipeline.json"));
    p.opt({"--workspace"}).def(Val::str("hunter"));
    p.subcommands("cmd", false);
    { Parser& s = p.sub("answer"); s.help_line = "one question";
      s.opt({"--question"}).required(); s.opt({"--options"}).nargs("*"); s.opt({"--role"}); }
    { Parser& s = p.sub("fill"); s.help_line = "every question on a form";
      s.opt({"--questions"}).required().help("JSON list of the form's questions"); s.opt({"--role"}); s.opt({"--out"}); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("checklist"); s.opt({"--role"}).required(); }
    { Parser& s = p.sub("draft"); s.help_line = "record the filled, unsubmitted application";
      s.opt({"--role"}).required(); s.opt({"--form-url"}).required(); s.opt({"--answers"}); s.opt({"--today"}); }
    { Parser& s = p.sub("submitted"); s.help_line = "record that the user submitted it themselves";
      s.opt({"--role"}).required(); s.opt({"--confirm"}).required().help("the user's words, for example \"I submitted it\""); s.opt({"--date"}); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd");
    if (cmd.empty()) { out(ap::help_text(ap::norm(p, p.prog))); return 2; }
    const Val profile = load_json(S(a, "profile"));
    const std::string ws = S(a, "workspace");
    DumpOpts jo; jo.indent = 2; jo.ensure_ascii = false;
    try {
        if ((cmd == "answer" || cmd == "fill") && profile.is_none())
            throw ApplyError("no " + S(a, "profile") + ": copy skills/hunter-apply/profile.example.json there and fill it in with the user's own answers");
        if (cmd == "answer") {
            const Val o = get(a, "options");
            const Val res = answer(get(a, "question"), profile, get(a, "role"), ws, o.is_none() ? std::nullopt : std::optional<Val>(o));
            print(dumps(res, jo));
            return get(res, "status").s == "filled" ? 0 : 1;
        }
        if (cmd == "fill") {
            const Val questions = load_json(S(a, "questions"));
            if (questions.t != Val::LIST) throw ApplyError(S(a, "questions") + " must be a JSON list of questions");
            const Val res = fill(*questions.l, profile, get(a, "role"), ws);
            if (truthy(get(a, "out"))) save_json(S(a, "out"), res);
            if (B(a, "json")) print(dumps(res, jo));
            else out(render_fill(res));
            return 0;
        }
        Val pipe = load_pipeline(S(a, "pipeline"));
        if (cmd == "checklist") {
            const List items = checklist(pipe, S(a, "role"), ws, profile);
            bool first5 = true;
            for (std::size_t i = 0; i < items.size(); ++i) {
                const bool done = get(items[i], "done").b;
                if (i < 5 && !done) first5 = false;
                print(std::string("[") + (done ? "x" : " ") + "] " + get(items[i], "item").s + (done ? "" : "  (" + get(items[i], "how").s + ")"));
            }
            return first5 ? 0 : 1;
        }
        if (cmd == "draft") {
            const long long today = truthy(get(a, "today")) ? day(S(a, "today")) : today_days();
            record_draft(pipe, S(a, "role"), S(a, "form_url"), get(a, "answers"), date_iso(today));
            save_json(S(a, "pipeline"), pipe);
            print("Recorded as a draft, not submitted. The form is filled in the user's browser: they review it and click Submit themselves.");
            return 0;
        }
        if (cmd == "submitted") {
            const long long when = truthy(get(a, "date")) ? day(S(a, "date")) : today_days();
            record_submitted(pipe, S(a, "role"), S(a, "confirm"), when);
            save_json(S(a, "pipeline"), pipe);
            print("Stage: applied on " + date_iso(when) + ". Next: a polite follow-up after 7 days if nothing comes back.");
            return 0;
        }
    } catch (const ApplyError& e) {
        eprint(std::string("error: ") + e.what());
        return 2;
    }
    return 2;
}
PACKTOOL("hunter-apply/answers", main);
}  // namespace packtools::hunter_apply
