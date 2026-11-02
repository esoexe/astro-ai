// contract-writer/contract and proposal-writer/proposal: the agreement and the priced proposal, written from what
// was agreed, and logged only after the user's explicit yes. C++ ports of closer-skill/contract-writer/contract.py
// and closer-skill/proposal-writer/proposal.py (MIT, Jake Schincariol).
// One deliberate difference: the proposal's placeholders read "[Astro: ...]" (the pack's skills call the assistant
// Astro); record refuses a proposal that still has "[Astro:" or the original "[Claude:" in it.
namespace packtools::closer {
// shutil.which: PATH (and PATHEXT on Windows)
inline std::string which_exe(const std::string& name) {
    const char* pe = std::getenv("PATH"); if (!pe) return "";
#ifdef _WIN32
    const char sep = ';'; std::vector<std::string> exts = {""}; { const char* x = std::getenv("PATHEXT"); for (const auto& e : split(x ? x : ".COM;.EXE;.BAT;.CMD", ";")) if (!e.empty()) exts.push_back(e); }
#else
    const char sep = ':'; const std::vector<std::string> exts = {""};
#endif
    for (const auto& dir : split(pe, std::string(1, sep))) {
        if (dir.empty()) continue;
        for (const auto& e : exts) {
            const std::string p = path_join(dir, name + e); std::error_code ec;
            if (!std::filesystem::is_regular_file(fspath(p), ec)) continue;
#ifndef _WIN32
            if (access(p.c_str(), X_OK) != 0) continue;
#endif
            return p;
        }
    }
    return "";
}
inline std::string abspath(const std::string& p) { std::error_code ec; const auto a = std::filesystem::absolute(fspath(p), ec).lexically_normal().u8string(); return std::string(a.begin(), a.end()); }
inline std::string long_date(long long day) {   // strftime("%B %d, %Y").replace(" 0", " ")
    static const char* M[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    long long y; unsigned m, d; civil_from(day, y, m, d);
    return std::string(M[m - 1]) + " " + std::to_string(d) + ", " + std::to_string(y);
}
inline Val contact_of(const Val& data, const Val& deal) {   // P.find(data["contacts"], deal["contact_id"]), None when missing
    const Val* c = find(at(data, "contacts"), at(deal, "contact_id")); return c ? *c : Val();
}
inline Val cget(const Val& c, const std::string& k) {   // c.get(k) where c may be None
    if (c.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(c) + "' object has no attribute 'get'");
    return g(c, k);
}
inline std::string first_name(const Val& c) { const auto w = split_ws(str_of(at(c, "name"))); if (w.empty()) throw PyErr("IndexError", "list index out of range"); return w[0]; }
}  // namespace packtools::closer

namespace packtools::closer_contract {
using namespace pyrt;
using namespace packtools::closer;
struct ContractError : std::runtime_error { using std::runtime_error::runtime_error; };

inline const std::vector<std::pair<std::string, std::string>> STANDARD = {
    {"Payment", "Invoices are due within 7 days. Work pauses if an invoice is more than 14 days late."},
    {"Ownership", "Once paid, the client owns the website content and design made for them. Ad accounts are set up in the client's name."},
    {"Confidentiality", "Each side keeps the other's private business information private."},
    {"Liability", "Neither side is liable for indirect losses. Total liability is capped at the fees paid in the 3 months before the claim."},
    {"Changes", "Changes to this agreement are made in writing and agreed by both sides."}};

inline std::set<std::string> amounts(const Val& text) {
    std::set<std::string> out;
    for (const auto& m : re::compile(R"(\$\s?\d[\d,]*(?:\.\d{2})?)").findall(truthy(text) ? str_of(text) : std::string())) out.insert(re::sub(R"([\s,])", "", m));
    return out;
}
// Warnings when the fee is not what was said on the call or in the proposal.
inline std::vector<std::string> check_fee(const Val& data, const Val& deal, const std::string& fee) {
    std::set<std::string> said;
    for (const Val& a : history_deal(data, at(deal, "id"))) if (in(at(a, "type"), {"call", "proposal", "reply", "note"})) { const auto s = amounts(g(a, "text")); said.insert(s.begin(), s.end()); }
    const auto fee_amounts = amounts(sv(fee));
    if (fee_amounts.empty()) return {"The fee has no dollar amount in it."};
    bool overlap = false; for (const auto& x : fee_amounts) overlap |= said.count(x) > 0;
    if (!said.empty() && !overlap)
        return {"The fee (" + join(std::vector<std::string>(fee_amounts.begin(), fee_amounts.end()), ", ") + ") is not an amount said on the call or in the proposal (" +
                join(std::vector<std::string>(said.begin(), said.end()), ", ") + "). Check it."};
    return {};
}
inline std::string build(const Val& data, const Val& deal, const std::string& date, const std::vector<std::pair<std::string, std::string>>& sections) {
    const Val me = g(data, "me", Val::dict());
    const Val c = contact_of(data, deal);
    for (const auto& [h, t] : sections) if (t.empty() || t.find("[fill") != std::string::npos) throw ContractError("The " + h + " section is empty. Say what was agreed.");
    if (!truthy(g(me, "company")) || !truthy(cget(c, "company"))) throw ContractError("Both parties need a company name (me.company and the contact's company).");
    const std::string mine = ps(at(me, "company")), theirs = ps(at(c, "company"));
    std::vector<std::string> md = {"# Services Agreement", "", "**" + mine + "** and **" + theirs + "**", "", date, ""};
    int n = 0;
    std::vector<std::pair<std::string, std::string>> all = sections; all.insert(all.end(), STANDARD.begin(), STANDARD.end());
    for (const auto& [h, t] : all) { n++; md.insert(md.end(), {"## " + std::to_string(n) + ". " + h, "", t, ""}); }
    md.insert(md.end(), {"## Signatures", "", mine + ": ______________________  Date: ________", "", ps(at(c, "name")) + ", " + theirs + ": ______________________  Date: ________", ""});
    return join(md, "\n");
}
inline std::string html_escape(const std::string& s) {   // html.escape(s, quote=True)
    std::string o; for (char ch : s) { switch (ch) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; case '\'': o += "&#x27;"; break; default: o += ch; } }
    return o;
}
inline std::string to_html(const std::string& md) {
    std::vector<std::string> body;
    for (const auto& line : splitlines(md)) {
        const std::string esc = re::sub(R"(\*\*(.+?)\*\*)", R"(<strong>\1</strong>)", html_escape(line));
        if (starts(line, "# ")) body.push_back("<h1>" + cpslice(esc, 2) + "</h1>");
        else if (starts(line, "## ")) body.push_back("<h2>" + cpslice(esc, 3) + "</h2>");
        else if (!strip(line).empty()) body.push_back("<p>" + esc + "</p>");
    }
    const std::string css = "@page{size:Letter;margin:22mm}body{font:11.5pt/1.55 Georgia,serif;color:#1c1c1c;max-width:680px;"
        "margin:40px auto}h1{font:600 22pt Helvetica,Arial,sans-serif;margin:0 0 6px}h2{font:600 12pt "
        "Helvetica,Arial,sans-serif;margin:22px 0 4px}p{margin:0 0 8px}";
    return "<!doctype html><html><head><meta charset=\"utf-8\"><title>Services Agreement</title><style>" + css + "</style></head><body>" + join(body, "\n") + "</body></html>\n";
}
struct Paths { std::string md, html, pdf; };
inline Paths paths_for(const Val& data, const Val& deal) {
    const Val c = contact_of(data, deal);
    const std::string base = path_join(path_join("closer", "contracts"), slug(str_of(orv(cget(c, "company"), at(c, "name")))) + "-agreement");
    return {base + ".md", base + ".html", base + ".pdf"};
}
inline std::string chrome() {
    for (const char* n : {"google-chrome", "chromium", "chromium-browser", "chrome"}) { const std::string w = which_exe(n); if (!w.empty()) return w; }
    const std::string mac = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
    return exists(mac) ? mac : "";
}
inline std::pair<bool, std::string> record(Val& data, const std::string& deal_id, const Val& approval, const std::string& when) {
    const auto [ok, why] = parse_yes(approval);
    if (!ok) return {false, "Not recorded: " + why + "."};
    Val* dp = find(at(data, "deals"), deal_id);
    if (!dp) return {false, "No deal " + deal_id};
    Val deal = *dp;
    const Paths p = paths_for(data, deal);
    if (!exists(p.md)) return {false, "Not recorded: no agreement drafted for " + deal_id + " yet."};
    const Val c = contact_of(data, deal);
    add_activity(data, {{"date", sv(when)}, {"contact_id", at(c, "id")}, {"deal_id", sv(deal_id)}, {"channel", sv("email")}, {"type", sv("contract")}, {"direction", sv("out")},
                        {"subject", sv("Your agreement with " + ps(g(at(data, "me"), "company")))}, {"text", sv("Agreement sent")}, {"approval", approval},
                        {"attachment", sv(exists(p.pdf) ? p.pdf : p.html)}});
    deal.d->set("stage", sv("contract-sent"));
    if (!truthy(g(deal, "close_date"))) deal.d->set("close_date", sv(iso(date_fromiso(sv(head(when, 10))) + 7)));
    deal.d->set("next_step", sv(first_name(c) + " signs the agreement"));
    return {true, "Logged: agreement sent to " + ps(at(c, "name")) + ". Deal is contract-sent."};
}
inline std::string list_repr(const std::vector<std::string>& v) { std::vector<std::string> r; for (const auto& x : v) r.push_back(str_repr(x)); return "[" + join(r, ", ") + "]"; }

inline int main(const std::vector<std::string>& argv) {
    Parser p("contract.py", "Write the contract.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    { Parser& s = p.sub("draft"); s.opt({"--deal"}).required(); s.opt({"--services"}).required(); s.opt({"--fee"}).required(); s.opt({"--term"}).required();
      s.opt({"--launch"}).required(); s.opt({"--date"}).help("the agreement date as written, e.g. 'October 29, 2026'"); }
    { Parser& s = p.sub("pdf"); s.opt({"--deal"}).required(); }
    { Parser& s = p.sub("record"); s.opt({"--deal"}).required(); s.opt({"--approval"}).required(); s.opt({"--at"}).required(); }
    { Parser& s = p.sub("signed"); s.opt({"--deal"}).required(); s.opt({"--today"}); }
    const Val a = parse_args(p, argv);
    const std::string cmd = S(a, "cmd"), file = S(a, "file");
    try {
        Val data = load(file);
        Val* dp = find(at(data, "deals"), S(a, "deal"));
        if (!dp) throw ContractError("No deal " + S(a, "deal"));
        const Val deal = *dp;
        if (cmd == "draft") {
            const std::string date = truthy(get(a, "date")) ? S(a, "date") : long_date(date_fromiso(today_iso()));
            const std::vector<std::pair<std::string, std::string>> sections = {{"Services", S(a, "services")}, {"Fee", S(a, "fee")}, {"Term", S(a, "term")}, {"Launch", S(a, "launch")}};
            const std::string md = build(data, deal, date, sections);
            const Paths ps_ = paths_for(data, deal);
            makedirs(path_join("closer", "contracts"));
            write_text(ps_.md, md);
            write_text(ps_.html, to_html(md));
            print("Wrote " + ps_.md + " and " + ps_.html);
            for (const auto& w : check_fee(data, deal, S(a, "fee"))) print("CHECK: " + w);
            print("A template, not legal advice. Read it, then: contract.py pdf, and send only on a yes.");
        } else if (cmd == "pdf") {
            const Paths ps_ = paths_for(data, deal);
            const std::string exe = chrome();
            if (exe.empty()) { print("No Chrome found. Open " + ps_.html + " and Print > Save as PDF."); return 1; }
            const std::vector<std::string> args = {exe, "--headless", "--disable-gpu", "--no-pdf-header-footer", "--print-to-pdf=" + abspath(ps_.pdf), "file://" + abspath(ps_.html)};
            const ExecResult r = packtools::run_cmd(args, 60);
            if (r.code == -1) throw ContractError("Command '" + list_repr(args) + "' timed out after 60 seconds");
            if (r.code != 0) throw ContractError("Command '" + list_repr(args) + "' returned non-zero exit status " + std::to_string(r.code) + ".");
            print("Wrote " + ps_.pdf);
        } else if (cmd == "record") {
            const auto [ok, msg] = record(data, S(a, "deal"), get(a, "approval"), S(a, "at"));
            if (ok) save(data, file);
            print(msg);
            return ok ? 0 : 1;
        } else if (cmd == "signed") {
            move(data, S(a, "deal"), "won", today_arg(get(a, "today")), Val(), sv("Kickoff"));
            save(data, file);
            print(ps(at(deal, "title")) + " is won.");
        }
    } catch (const ContractError& e) { eprint(e.what()); return 1; }
    catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const OSError& e) { eprint(e.what()); return 1; }
    return 0;
}
PACKTOOL("contract-writer/contract", main);
}  // namespace packtools::closer_contract

namespace packtools::closer_proposal {
using namespace pyrt;
using namespace packtools::closer;
struct ProposalError : std::runtime_error { using std::runtime_error::runtime_error; };
inline constexpr int VALID_DAYS = 14;

inline Val oget(const Val& o, const std::string& k, Val dflt = Val()) { return g(o, k, std::move(dflt)); }
inline double num_or0(const Val& o, const std::string& k) { return numv(oget(o, k, Val::integer(0))); }
inline double first_year(const Val& o) { return num_or0(o, "setup") + 12 * num_or0(o, "monthly"); }
inline void check_options(const Val& options) {
    const List& os = lst(options);
    if (os.size() < 1 || os.size() > 3) throw ProposalError("Give 1 to 3 options. More than 3 makes it harder to say yes.");
    for (const Val& o : os) {
        for (const char* k : {"name", "includes"}) if (!truthy(oget(o, k))) throw ProposalError(std::string("Every option needs ") + k + ".");
        if (num_or0(o, "setup") < 0 || num_or0(o, "monthly") < 0 || first_year(o) == 0) throw ProposalError("Option " + ps(at(o, "name")) + " needs a price (setup and/or monthly).");
    }
    int rec = 0; for (const Val& o : os) if (truthy(oget(o, "recommended"))) rec++;
    if (rec > 1) throw ProposalError("Recommend one option at most.");
}
// What they said: their replies and the call notes, newest last.
inline std::vector<std::string> their_words(const Val& data, const Val& deal) {
    std::vector<std::string> out;
    for (const Val& a : history_deal(data, at(deal, "id"))) {
        if (is(g(a, "direction"), "in") && truthy(g(a, "text"))) out.push_back(ps(at(a, "text")));
        if (is(at(a, "type"), "call") && truthy(g(a, "text"))) out.push_back(ps(at(a, "text")));
    }
    return out;
}
inline std::string price_line(const Val& o) {
    std::vector<std::string> parts;
    if (truthy(oget(o, "setup"))) parts.push_back(money(at(o, "setup")) + " setup");
    if (truthy(oget(o, "monthly"))) parts.push_back(money(at(o, "monthly")) + "/month");
    return join(parts, " + ");
}
inline std::string build(const Val& data, const Val& deal, const Val& options, long long today) {
    check_options(options);
    const Val me = g(data, "me", Val::dict());
    const Val c = contact_of(data, deal);
    std::vector<std::string> lines = {"# Proposal for " + ps(cget(c, "company")), "",
        "Prepared for " + ps(at(c, "name")) + " by " + ps(g(me, "name")) + ", " + ps(g(me, "company")) + ". Valid until " + long_date(today + VALID_DAYS) + ".", "",
        "## Where you are today", ""};
    const auto said = their_words(data, deal);
    if (said.empty()) lines.push_back("- [Astro: their situation, in their own words, from the call]");
    for (const auto& s : said) lines.push_back("- " + s);
    lines.insert(lines.end(), {"", "## What we'll do", "", "- [Astro: 3 to 5 lines, what they get and by when]", "", "## Options", "", "| | Price | First year | Includes |", "| --- | --- | --- | --- |"});
    for (const Val& o : lst(options)) {
        const std::string name = ps(at(o, "name")) + (truthy(oget(o, "recommended")) ? " (recommended)" : "");
        std::vector<std::string> inc; for (const Val& x : lst(at(o, "includes"))) inc.push_back(str_of(x));
        lines.push_back("| " + name + " | " + price_line(o) + " | " + money(first_year(o)) + " | " + join(inc, "; ") + " |");
    }
    lines.push_back("");
    if (truthy(g(me, "value_line"))) lines.insert(lines.end(), {"## What it pays back", "", ps(at(me, "value_line")), ""});
    lines.insert(lines.end(), {"## Next step", "", "Reply with the option you want and I'll send the agreement the same day.", ""});
    return join(lines, "\n");
}
inline std::string path_for(const Val& c) { return path_join(path_join("closer", "proposals"), slug(str_of(orv(cget(c, "company"), at(c, "name")))) + ".md"); }
inline std::pair<bool, std::string> record(Val& data, const std::string& deal_id, const Val& approval, const std::string& when) {
    const auto [ok, why] = parse_yes(approval);
    if (!ok) return {false, "Not recorded: " + why + "."};
    Val* dp = find(at(data, "deals"), deal_id);
    if (!dp) return {false, "No deal " + deal_id};
    Val deal = *dp;
    const Val c = contact_of(data, deal);
    const std::string path = path_for(c);
    if (!exists(path)) return {false, "Not recorded: no proposal drafted for " + deal_id + " yet."};
    const std::string text = read_text(path);
    if (text.find("[Astro:") != std::string::npos || text.find("[Claude:") != std::string::npos) return {false, "Not recorded: the proposal still has a [Astro: ...] placeholder in it."};
    std::vector<std::string> prices;
    for (const auto& l : splitlines(text)) if (starts(l, "| ") && l.find('$') != std::string::npos) { const auto cells = split(l, "|"); if (cells.size() < 3) throw PyErr("IndexError", "list index out of range"); prices.push_back(strip(cells[2])); }
    add_activity(data, {{"date", sv(when)}, {"contact_id", at(c, "id")}, {"deal_id", sv(deal_id)}, {"channel", sv("email")}, {"type", sv("proposal")}, {"direction", sv("out")},
                        {"subject", sv("Proposal for " + ps(cget(c, "company")))}, {"approval", approval}, {"text", sv("Proposal sent: " + join(prices, " / "))}, {"attachment", sv(path)}});
    const auto idx = [](const Val& s) { for (std::size_t k = 0; k < STAGES.size(); ++k) if (is(s, STAGES[k])) return k; throw PyErr("ValueError", repr(s) + " is not in tuple"); };
    if (idx(at(deal, "stage")) < idx(sv("proposal"))) deal.d->set("stage", sv("proposal"));
    deal.d->set("next_step", sv(first_name(c) + " picks an option"));
    return {true, "Logged: proposal sent to " + ps(at(c, "name")) + "."};
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("proposal.py", "Write a priced proposal.");
    p.opt({"--file"}).def(DEFAULT_FILE);
    p.subcommands("cmd", true);
    { Parser& s = p.sub("draft"); s.opt({"--deal"}).required(); s.opt({"--options"}).required(); s.opt({"--today"}); }
    { Parser& s = p.sub("record"); s.opt({"--deal"}).required(); s.opt({"--approval"}).required(); s.opt({"--at"}).required(); }
    const Val a = parse_args(p, argv);
    const std::string file = S(a, "file");
    try {
        Val data = load(file);
        if (S(a, "cmd") == "draft") {
            Val* dp = find(at(data, "deals"), S(a, "deal"));
            if (!dp) throw ProposalError("No deal " + S(a, "deal"));
            const Val deal = *dp;
            const Val options = loads(read_text(S(a, "options")));
            const std::string md = build(data, deal, options, today_arg(get(a, "today")));
            const Val c = contact_of(data, deal);
            makedirs(path_join("closer", "proposals"));
            const std::string path = path_for(c);
            write_text(path, md);
            print("Wrote " + path + ". Fill every [Astro: ...] from the call before it goes.");
        } else {
            const auto [ok, msg] = record(data, S(a, "deal"), get(a, "approval"), S(a, "at"));
            if (ok) save(data, file);
            print(msg);
            return ok ? 0 : 1;
        }
    } catch (const ProposalError& e) { eprint(e.what()); return 1; }
    catch (const PipelineError& e) { eprint(e.what()); return 1; }
    catch (const OSError& e) { eprint(e.what()); return 1; }
    catch (const PyErr& e) { if (e.type != "ValueError" && e.type != "json.decoder.JSONDecodeError") throw; eprint(e.what()); return 1; }   // json.JSONDecodeError is a ValueError
    return 0;
}
PACKTOOL("proposal-writer/proposal", main);
}  // namespace packtools::closer_proposal
