// master-virality/humanize - strip the machine fingerprint out of a draft: invisible characters, typography (em dashes,
// curly quotes, ellipses) and the slop lexicon in tools/slop.json, leaving URLs untouched. Structural tells are reported,
// never rewritten. C++ port of master-skills/skills/virality/tools/humanize.py (Jake Schincariol, MIT); same output.
namespace packtools::master_humanize {
using namespace pyrt;
using namespace packtools::master;

inline const char* URL_RE = R"(https?://\S+|www\.\S+|\S+@\S+\.\S+)";
inline const char* SENT_RE = R"([^.!?\n]+[.!?]*)";

inline std::size_t count_sub(const std::string& s, const std::string& sub) {
    if (sub.empty()) return cplen(s) + 1;
    std::size_t n = 0, k = 0; while ((k = s.find(sub, k)) != std::string::npos) { ++n; k += sub.size(); }
    return n;
}
inline std::string ph(std::size_t i) { return std::string(1, '\0') + "URL" + std::to_string(i) + std::string(1, '\0'); }

// "U+200B" -> one code point; "U+E0000-U+E007F" -> a range
inline std::pair<uint32_t, uint32_t> cp_spec(const std::string& spec) {
    auto hex = [](const std::string& h) {
        std::size_t used = 0; unsigned long x = 0;   // int(h, 16)
        try { x = std::stoul(h, &used, 16); } catch (const std::exception&) { used = 0; }
        if (h.empty() || used != h.size()) throw PyErr("ValueError", "invalid literal for int() with base 16: " + str_repr(h));
        return static_cast<uint32_t>(x);
    };
    const std::size_t dash = spec.find('-');
    if (dash == std::string::npos) { const uint32_t c = hex(cpslice(spec, 2)); return {c, c}; }
    const auto parts = split(spec, "-");
    if (parts.size() != 2) throw PyErr("ValueError", "too many values to unpack (expected 2)");
    return {hex(cpslice(parts[0], 2)), hex(cpslice(parts[1], 2))};
}
inline Val hit3(const std::string& k1, Val v1, const std::string& k2, Val v2, const std::string& k3, Val v3) {
    Val d = Val::dict(); d.d->set(k1, std::move(v1)); d.d->set(k2, std::move(v2)); d.d->set(k3, std::move(v3)); return d;
}

// x-human keeps two invisible characters that do a job inside emoji: the joiner between two pictographs (👨‍💻) and the
// tag characters that spell the England, Scotland and Wales flags. A joiner or a tag anywhere else is still deleted.
inline const char* X_PICTO = R"(\U0001F000-\U0001FAFF\u2600-\u27BF\u2B00-\u2BFF\u2300-\u23FF)";
inline const std::string& x_emoji_zwj() { static const std::string p = std::string("(?<=[") + X_PICTO + R"(\uFE0F\U0001F3FB-\U0001F3FF])\u200D(?=[)" + X_PICTO + "])"; return p; }
inline const char* X_FLAG_TAG = R"(\U0001F3F4\U000E0067\U000E0062(?:\U000E0065\U000E006E\U000E0067|\U000E0073\U000E0063\U000E0074|\U000E0077\U000E006C\U000E0073)\U000E007F)";
inline std::string pass_invisible(std::string text, const Val& lex, Val& hits, bool x = false) {
    std::vector<std::string> flags;
    if (x) {
        text = re::compile(X_FLAG_TAG).sub_fn([&](const re::Match& m) { flags.push_back(m.group(0)); return "\x02" + std::to_string(flags.size() - 1) + "\x03"; }, text, 0);
        text = re::sub(x_emoji_zwj(), "\x01", text);
    }
    for (const auto& e : iterate(get(lex, "invisible"))) {
        const std::string spec = re_text(get(e, "cp"));
        const auto [lo, hi] = cp_spec(spec);
        long long n = 0; for (const uint32_t c : ucase_detail::u32(text)) if (c >= lo && c <= hi) ++n;
        if (!n) continue;
        const std::string action = pystr(get(e, "action"));
        hits.l->push_back(hit3("name", Val::str(spec + " " + pystr(get(e, "name"))), "count", Val::integer(n), "action", Val::str(action)));
        std::string o; for (const auto& ch : cps(text)) { const uint32_t c = cp_of(ch); if (c >= lo && c <= hi) { if (action != "delete") o += " "; } else o += ch; }
        text = o;
    }
    // any remaining Cf (format) character is invisible by definition
    long long stray = 0; std::string o;
    for (const auto& ch : cps(text)) { if (std::string(uni::category(cp_of(ch))) == "Cf") ++stray; else o += ch; }
    if (stray) { hits.l->push_back(hit3("name", Val::str("other invisible format chars"), "count", Val::integer(stray), "action", Val::str("delete"))); text = o; }
    if (x) {
        text = replace_all(text, "\x01", "\xE2\x80\x8D");
        for (std::size_t i = 0; i < flags.size(); ++i) text = replace_all(text, "\x02" + std::to_string(i) + "\x03", flags[i]);
    }
    return text;
}
inline std::string pass_typographic(std::string text, const Val& lex, Val& hits) {
    for (const auto& e : iterate(get(lex, "typographic"))) {
        const std::string ch = re_text(get(e, "from")), to = re_text(get(e, "to"));
        const std::size_t n = count_sub(text, ch);
        if (!n) continue;
        const std::string shown = strip(to);
        hits.l->push_back(hit3("name", Val::str(ch + " " + pystr(get(e, "name"))), "count", Val::integer(static_cast<long long>(n)), "to", Val::str(shown.empty() ? "(space)" : shown)));
        if (ch == "\xE2\x80\x94") {
            // " word EMDASH word " and "wordEMDASHword" both collapse to a comma + space
            text = re::sub("\\s*\xE2\x80\x94\\s*", ", ", text);
        } else if (ch == "\xE2\x80\x93") {
            text = re::sub("\\s*\xE2\x80\x93\\s*(?=\\d)", "-", text);   // 5-10 with an en dash -> 5-10
            text = re::sub("\\s+\xE2\x80\x93\\s+", ", ", text);         // used as an em dash
            text = replace_all(text, "\xE2\x80\x93", "-");
        } else text = replace_all(text, ch, to);
    }
    // a comma inserted before existing punctuation reads wrong
    text = re::sub(R"(,\s*([,.;:!?]))", R"(\1)", text);
    text = re::sub(R"(,\s*\n)", "\n", text);
    return text;
}
inline std::string match_case(const std::string& src, const std::string& repl) {
    if (repl.empty()) return repl;
    if (isupper_str(src) && cplen(src) > 1) return upper(repl);
    if (isupper_str(cpslice(src, 0, 1))) return upper(cpslice(repl, 0, 1)) + cpslice(repl, 1);
    return repl;
}
inline std::string pass_lexical(std::string text, const Val& lex, Val& hits, bool tidy = false, bool x = false) {   // tidy: ig-human's fuller clean-up
    // longest first so phrases win (a stable sort, like sorted(..., reverse=True))
    List entries = iterate(get(lex, "phrases")); for (const auto& e : iterate(get(lex, "words"))) entries.push_back(e);
    std::stable_sort(entries.begin(), entries.end(), [](const Val& x, const Val& y) { return cplen(re_text(get(x, "find"))) > cplen(re_text(get(y, "find"))); });
    for (const auto& e : entries) {
        const std::string find = get(e, "find").s;
        const std::string pat = slop_pattern(find);
        const auto& rx = re::compile(pat, re::I);
        const std::size_t n = rx.findall(text).size();
        if (!n) continue;
        const std::string repl = pystr(get(e, "replace"));
        Val h = Val::dict(); h.d->set("find", Val::str(find)); h.d->set("replace", Val::str(repl.empty() ? "(deleted)" : repl));
        h.d->set("count", Val::integer(static_cast<long long>(n))); h.d->set("family", get(e, "family")); hits.l->push_back(h);
        text = rx.sub_fn([&](const re::Match& m) { return match_case(m.group(0), repl); }, text, 0);
    }
    // clean up after deletions
    text = re::sub(R"([ \t]{2,})", " ", text);
    if (tidy) {   // deleting a whole clause leaves orphaned punctuation ("system. ." or a line that opens on a comma)
        text = re::sub(R"((?m)^[ \t]*(?:[,.;:]+[ \t]*)+)", "", text);
        text = re::sub(R"((?m)^[ \t](?=\S))", "", text);   /* one space left by a deletion; deeper indents are deliberate */
        text = re::sub(R"(\s+([,.;:!?]))", R"(\1)", text);
        text = re::sub(R"(,\s*([,.;:!?]))", R"(\1)", text);   /* an em dash became a comma, then the clause after it went */
        const std::string ell("\0ELL\0", 5);
        text = replace_all(text, "...", ell);   /* protect real ellipses */
        text = re::sub(R"(\.\s*\.+)", ".", text);
        text = re::sub(R"(([!?])\s*\.)", R"(\1)", text);
        text = replace_all(text, ell, "...");
        /* x-human: deleting two bait phrases joined by "and" leaves the "and" behind; a line that is now only a connective goes */
        if (x) text = re::sub(R"((?im)^[ \t]*(?:and|or|but|so|then|plus)[ \t]*[.!?,]*[ \t]*(?=#|$))", "", text);
    } else {
        text = re::sub(R"((?m)^[ \t]*([,.;:])\s*)", "", text);
        text = re::sub(R"(\s+([,.;:!?]))", R"(\1)", text);
    }
    text = re::sub(R"((?m)^[ \t]+$)", "", text);
    text = re::sub(R"(\n{3,})", "\n\n", text);
    // an em dash that became a comma, followed by a sentence connective, leaves a splice: promote it to a full stop
    text = re::compile(R"(,\s*(also|so|still|basically|in the end)\s*,\s*)").sub_fn([](const re::Match& m) {
        const std::string g = m.group(1); return ". " + upper(cpslice(g, 0, 1)) + cpslice(g, 1) + ", "; }, text, 0);
    return text;
}
inline Val scan_structures(const std::string& text, const Val& lex) {
    Val flags = Val::list();
    for (const auto& s : iterate(get(lex, "structures"))) {
        std::size_t n;
        try { n = re::compile(re_text(get(s, "regex")), re::M).findall(text).size(); }
        catch (const PyErr& e) { if (e.type != "re.error") throw; continue; }
        if (n) flags.l->push_back(hit3("name", get(s, "name"), "count", Val::integer(static_cast<long long>(n)), "fix", get(s, "fix")));
    }
    // sentence-length uniformity is structural too
    std::vector<double> lens;
    for (const auto& s : re::findall(SENT_RE, text)) { const auto k = split_ws(s).size(); if (k > 2) lens.push_back(static_cast<double>(k)); }
    if (lens.size() >= 4) {
        const double n = static_cast<double>(lens.size());
        const double mean = fsum(lens) / n;
        std::vector<double> sq; for (const double x : lens) sq.push_back((x - mean) * (x - mean));
        const double var = fsum(sq) / n;
        const double cv = mean != 0.0 ? std::pow(var, 0.5) / mean : 0.0;
        if (cv < 0.35)
            flags.l->push_back(hit3("name", Val::str("Uniform sentence length (variation " + fmt_f(cv, 2) + ")"), "count", Val::integer(static_cast<long long>(lens.size())),
                                    "fix", Val::str("Break one sentence in half. Let another run long. Machines write even.")));
    }
    return flags;
}
// ig-human: deleting an opener leaves the next word lower case. Only fixed for writers who capitalise their sentences.
inline std::string restore_capitals(const std::string& original, const std::string& text) {
    const std::string starts_re = R"((?:^|[.!?]\s+|\n)\s*([A-Za-z]))";
    const auto starts = re::findall(starts_re, original);
    std::size_t up = 0;
    for (const auto& c : starts) if (!c.empty() && c[0] >= 'A' && c[0] <= 'Z') up++;
    if (starts.empty() || up * 2 < starts.size()) return text;
    const std::string fix_re = R"((?:^|(?<=[.!?] )|(?<=[.!?]\n)|(?<=\n))\s*([a-z]))";
    return re::compile(fix_re).sub_fn([](const re::Match& m) { std::string g = m.group(0); g.pop_back(); return g + upper(m.group(1)); }, text, 0);
}
inline std::string humanize(const std::string& raw, const Val& lex, Val& report, bool ig = false, bool x = false) {
    std::vector<std::string> urls;   // swap URLs for placeholders so no pass rewrites inside a link
    std::string text = re::compile(URL_RE).sub_fn([&](const re::Match& m) { urls.push_back(m.group(0)); return ph(urls.size() - 1); }, raw, 0);
    Val inv = Val::list(), typo = Val::list(), lexi = Val::list();
    text = pass_invisible(text, lex, inv, x);
    text = pass_typographic(text, lex, typo);
    text = pass_lexical(text, lex, lexi, ig || x, x);
    if (ig || x) text = restore_capitals(raw, text);
    for (std::size_t i = 0; i < urls.size(); ++i) text = replace_all(text, ph(i), urls[i]);
    report = Val::dict(); report.d->set("invisible", inv); report.d->set("typographic", typo); report.d->set("lexical", lexi);
    report.d->set("structures", scan_structures(text, lex));
    return strip(text) + "\n";
}
inline void render_report(const Val& report) {
    std::string o;
    auto line = [&](const std::string& s) { o += s; o += "\n"; };
    auto head = [&](const std::string& title) { line("\n" + title + "\n" + std::string(cplen(title), '-')); };
    auto cnt = [](const Val& h) { return rjust(std::to_string(get(h, "count").i), 3); };
    long long total = 0;
    for (const char* k : {"invisible", "typographic", "lexical"}) for (const auto& h : iter(get(report, k))) total += get(h, "count").i;
    head("HUMANIZE REPORT");
    line(std::to_string(total) + " machine artefacts removed, " + std::to_string(get(report, "structures").size()) + " structural tells flagged for rewrite");
    if (get(report, "invisible").size()) { head("1. INVISIBLE CHARACTERS"); for (const auto& h : iter(get(report, "invisible"))) line("  " + cnt(h) + "x  " + pystr(get(h, "name")) + "  -> " + pystr(get(h, "action"))); }
    if (get(report, "typographic").size()) { head("2. TYPOGRAPHY"); for (const auto& h : iter(get(report, "typographic"))) line("  " + cnt(h) + "x  " + pystr(get(h, "name")) + "  -> " + pystr(get(h, "to"))); }
    if (get(report, "lexical").size()) {
        head("3. SLOP LEXICON");
        for (const auto& h : iter(get(report, "lexical"))) line("  " + cnt(h) + "x  " + pystr(get(h, "find")) + "  -> " + pystr(get(h, "replace")) + "   [" + pystr(get(h, "family")) + "]");
    }
    if (get(report, "structures").size()) {
        head("4. STRUCTURAL TELLS  (not auto-fixed - rewrite these yourself)");
        for (const auto& h : iter(get(report, "structures"))) line("  " + cnt(h) + "x  " + pystr(get(h, "name")) + "\n        " + pystr(get(h, "fix")));
    }
    bool any = false; for (const auto& [k, v] : report.d->items) if (v.size()) any = true;
    if (!any) { head("CLEAN"); line("  Nothing to strip."); }
    line("");
    flush(); std::fwrite(o.data(), 1, o.size(), stderr); std::fflush(stderr);
}

inline int main_with(const std::vector<std::string>& argv, const std::string& lexicon, bool ig = false, bool x = false) {
    Parser p("humanize", "Strip the machine fingerprint out of a draft.");
    p.pos("input").nargs("?").def("-").help("file, or - for stdin");
    p.opt({"-o", "--out"}).help("write cleaned text here instead of stdout");
    p.opt({"--report"}).store_true().help("print what changed, to stderr");
    p.opt({"--json"}).store_true().help("emit {text, report} as JSON");
    p.opt({"--lexicon"}).def(Val::str(skill_file(lexicon))).help("path to slop.json");
    const Val a = parse_args(p, argv);
    const std::string input = S(a, "input");
    const std::string raw = input == "-" ? read_stdin_text() : read_text_utf8(input);
    const Val lex = loads(read_text_utf8(S(a, "lexicon")));
    Val report; const std::string clean = humanize(raw, lex, report, ig, x);
    if (B(a, "json")) {
        Val d = Val::dict(); d.d->set("text", Val::str(clean)); d.d->set("report", report);
        DumpOpts o; o.indent = 2; o.ensure_ascii = false; print(dumps(d, o)); return 0;
    }
    const std::string outp = S(a, "out");
    if (!outp.empty()) { write_text(outp, clean); eprint("wrote " + outp); }
    else out(clean);
    if (B(a, "report")) render_report(report);
    return 0;
}
inline int main(const std::vector<std::string>& argv) { return main_with(argv, "tools/slop.json"); }
PACKTOOL("master-virality/humanize", main);
}  // namespace packtools::master_humanize
