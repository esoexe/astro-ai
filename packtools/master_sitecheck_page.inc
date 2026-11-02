// master_sitecheck_page.inc - master-build/sitecheck, one page at a time: the rules, URL helpers, srcset and CSS
// scanning, PageParser (html.parser, see htmlparser.h) and analyze(). A port of sitecheck.py (master-skills, MIT).
namespace packtools::master_sitecheck {

struct Rule { const char* id; const char* severity; const char* what; const char* fix; };
// errors first; this order is also the report order
inline const std::vector<Rule>& rules() {
    static const std::vector<Rule> r = {
        {"broken-link", "error", "Link to a page that does not load", "Fix the href or create the page. Paths are case-sensitive on most hosts."},
        {"broken-anchor", "error", "Link to an #id that does not exist on the target page", "Add the id to the target section or correct the href."},
        {"broken-asset", "error", "Image, script, stylesheet, font or media file that does not load", "Fix the path (case-sensitive on most hosts) or add the missing file."},
        {"external-link", "error", "External link is dead (404, 410 or the domain does not resolve)", "Update or remove the link."},
        {"external-asset", "error", "External image, script, stylesheet or font does not load", "Fix the URL or self-host the file."},
        {"missing-title", "error", "Page has no <title>", "Add a specific <title> in <head> (what the page is, then the brand)."},
        {"missing-viewport", "error", "No viewport meta tag, so phones show the desktop layout zoomed out", "Add <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">."},
        {"zoom-disabled", "error", "Viewport blocks pinch zoom (user-scalable=no or maximum-scale below 2)", "Remove user-scalable=no and maximum-scale; people with low vision need zoom."},
        {"missing-lang", "error", "<html> has no lang attribute", "Add the page language, for example <html lang=\"en\">."},
        {"missing-doctype", "error", "No <!doctype html>, so the page renders in quirks mode", "Make <!doctype html> the very first line of the file."},
        {"img-missing-alt", "error", "Image without alt text", "Add alt=\"what the image shows\". Use alt=\"\" only for purely decorative images."},
        {"unlabeled-field", "error", "Form field without a label", "Add <label for=\"field-id\">, wrap the field in <label>, or add aria-label. A placeholder is not a label."},
        {"unnamed-link", "error", "Link with no text or accessible name (usually an icon link)", "Add visible text or aria-label=\"where it goes\"."},
        {"unnamed-button", "error", "Button with no text or accessible name (usually an icon button)", "Add visible text or aria-label=\"what it does\"."},
        {"duplicate-id", "error", "The same id is used more than once on a page", "Make every id unique; anchors, labels and scripts pick the wrong element otherwise."},
        {"mixed-content", "error", "HTTPS page loads a resource or submits a form over plain HTTP", "Use https:// (or a relative URL) for every resource and form action."},
        {"local-url", "error", "Points at localhost or a file:// path that visitors cannot reach", "Use a relative URL or the real production URL."},
        {"missing-description", "warn", "No meta description", "Add <meta name=\"description\" content=\"one or two plain sentences\">; search results and link previews use it."},
        {"missing-charset", "warn", "No character encoding declared", "Add <meta charset=\"utf-8\"> as the first element in <head>."},
        {"missing-h1", "warn", "Page has no <h1>", "Give every page one clear <h1> that says what the page is."},
        {"img-missing-size", "warn", "Image without width and height, so the layout jumps while it loads", "Add width and height attributes with the image's real pixel size; CSS can still scale it (img { height: auto; })."},
        {"image-heavy", "warn", "Image file is heavy", "Resize to about 2x its displayed width and compress (WebP or AVIF, or JPEG quality 75 to 85)."},
        {"image-oversized", "warn", "Image has far more pixels than the size it is displayed at", "Export it at about 2x the width attribute, or add srcset with smaller versions."},
        {"placeholder-link", "warn", "Link goes nowhere (href=\"#\", empty href or javascript:)", "Point it at a real page or section, or use a <button> if it runs code."},
        {"lorem-ipsum", "warn", "Placeholder lorem ipsum text left on the page", "Replace it with the owner's real words."},
        {"noindex", "warn", "Search engines are told not to index this page", "Remove robots noindex before launch unless the page should stay out of search."},
        {"missing-favicon", "warn", "No favicon, so browsers log a 404 for /favicon.ico", "Add <link rel=\"icon\" href=\"/favicon.svg\"> (or .png / .ico)."},
        {"missing-og-image", "warn", "No og:image, so shared links show no preview image", "Add <meta property=\"og:image\" content=\"https://your-domain/share.png\"> (1200x630, absolute URL)."},
        {"relative-og-image", "warn", "og:image is a relative URL; most link previews ignore it", "Use an absolute https:// URL for og:image."},
        {"external-unverified", "warn", "External URL could not be verified (blocked, rate limited, server error or timed out)", "Open it in a browser and confirm it works."},
    };
    return r;
}
inline int rule_index(const std::string& id) {
    const auto& r = rules();
    for (std::size_t k = 0; k < r.size(); ++k)
        if (id == r[k].id) return static_cast<int>(k);
    return -1;
}
inline const Rule& rule_of(const std::string& id) { return rules()[static_cast<std::size_t>(rule_index(id))]; }

struct Finding {
    std::string rule, page, url;
    long line = 0;
    std::string detail;
    long also = 0;
    std::string severity() const { return rule_of(rule).severity; }
    Val to_dict() const {
        Val d = Val::dict();
        d.d->set("severity", Val::str(severity()));
        d.d->set("rule", Val::str(rule));
        d.d->set("page", Val::str(page));
        d.d->set("url", Val::str(url));
        d.d->set("line", Val::integer(line));
        d.d->set("detail", Val::str(detail));
        d.d->set("also_on", Val::integer(also));
        d.d->set("fix", Val::str(rule_of(rule).fix));
        return d;
    }
};

// ---- URL helpers ----------------------------------------------------------------------------------------------------
inline bool is_local_host(const std::optional<std::string>& host) {
    std::string h = lower(host.value_or(""));
    std::size_t a = 0, b = h.size();
    while (a < b && (h[a] == '[' || h[a] == ']')) ++a;
    while (b > a && (h[b - 1] == '[' || h[b - 1] == ']')) --b;
    h = h.substr(a, b - a);
    auto ends = [&](const std::string& s) { return h.size() >= s.size() && h.compare(h.size() - s.size(), s.size(), s) == 0; };
    return h == "localhost" || h == "127.0.0.1" || h == "0.0.0.0" || h == "::1" || h.rfind("127.", 0) == 0 || ends(".localhost") || ends(".local") || ends(".test");
}

// canonical form for de-duplication: lowercase scheme and host, no default port, percent-encoded path, no fragment
inline std::optional<std::string> normalize(const std::string& url) {
    up::SplitResult p;
    std::string scheme, host;
    std::optional<long long> port;
    try {
        p = up::urlsplit(url);
        scheme = lower(p.scheme);
        host = p.hostname().value_or("");
        port = p.port();
    } catch (const PyErr& e) {
        if (e.type == "ValueError") return std::nullopt;
        throw;
    }
    if (!(scheme == "http" || scheme == "https") || host.empty()) return std::nullopt;
    bool ascii = true;
    for (unsigned char c : host)
        if (c >= 0x80) ascii = false;
    if (!ascii) {
        const auto enc = up::idna_encode(host);
        if (!enc) return std::nullopt;
        host = *enc;
    }
    std::string netloc = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    if (port && *port && !((scheme == "http" && *port == 80) || (scheme == "https" && *port == 443))) netloc += ":" + std::to_string(*port);
    const std::string path = up::quote(p.path.empty() ? std::string("/") : p.path, "/%:@!$&'()*+,;=-._~");
    const std::string query = up::quote(p.query, "=&%/?:@!$'()*+,;-._~");
    return up::urlunsplit(scheme, netloc, path, query, "");
}

inline std::string origin_of(const std::string& url) {
    const auto p = up::urlsplit(url);
    return p.scheme + "://" + p.netloc;
}

struct Resolved { std::string kind, url; std::optional<std::string> frag; };
inline std::string drop_tab_crlf(const std::string& s) {   // _WS_RE.sub("", s)
    std::string o;
    for (char c : s)
        if (c != '\t' && c != '\n' && c != '\r') o += c;
    return o;
}
// kind: http | empty | js | file | skip (mailto:, tel:, data:, ...) | invalid
inline Resolved resolve(const std::optional<std::string>& raw, const std::string& base) {
    if (!raw) return {"empty", "", std::nullopt};
    const std::string u = strip(drop_tab_crlf(*raw));
    if (u.empty()) return {"empty", "", std::nullopt};
    const std::string scheme_pat = "^([a-zA-Z][a-zA-Z0-9+.\\-]*):";
    const auto m = re::match(scheme_pat, u);
    if (m.ok) {
        const std::string scheme = lower(m.group(1));
        if (scheme == "javascript") return {"js", u, std::nullopt};
        if (scheme == "file") return {"file", u, std::nullopt};
        if (scheme != "http" && scheme != "https") return {"skip", u, std::nullopt};
    }
    const auto [absolute, frag] = up::urldefrag(up::urljoin(base, u));
    const bool has_frag = u.find('#') != std::string::npos;
    const auto norm = normalize(absolute);
    if (!norm) return {"invalid", u, std::nullopt};
    return {"http", *norm, has_frag ? std::optional<std::string>(frag) : std::nullopt};
}

// short form for reports: path for same-origin URLs, full URL otherwise
inline std::string display(const std::string& url, const std::set<std::string>& origins) {
    if (!url.empty() && origins.count(origin_of(url))) {
        const auto p = up::urlsplit(url);
        return (p.path.empty() ? std::string("/") : p.path) + (p.query.empty() ? "" : "?" + p.query) + (p.fragment.empty() ? "" : "#" + p.fragment);
    }
    return url;
}

inline std::string url_ext(const std::string& url) { return lower(sv::posix_splitext(up::urlsplit(url).path).second); }
inline bool looks_like_page(const std::string& url) {
    static const std::set<std::string> page = {"", ".html", ".htm", ".php", ".asp", ".aspx", ".jsp", ".shtml"};
    return page.count(url_ext(url)) > 0;
}
inline std::string kind_from_ext(const std::string& url, const std::string& def = "other") {
    static const std::set<std::string> font = {".woff2", ".woff", ".ttf", ".otf", ".eot"};
    static const std::set<std::string> img = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".avif", ".svg", ".ico", ".bmp"};
    static const std::set<std::string> media = {".mp4", ".m4v", ".webm", ".mov", ".mp3", ".m4a", ".wav", ".ogg", ".vtt"};
    const std::string ext = url_ext(url);
    if (font.count(ext)) return "font";
    if (img.count(ext)) return "img";
    if (media.count(ext)) return "media";
    if (ext == ".css") return "css";
    if (ext == ".js" || ext == ".mjs") return "js";
    return def;
}

// URLs from a srcset / imagesrcset value (descriptors dropped)
inline std::vector<std::string> parse_srcset(const std::string& value) {
    const auto s = to_u32(value);
    const std::size_t n = s.size();
    std::vector<std::string> urls;
    std::size_t i = 0;
    while (i < n) {
        while (i < n && (py_space(s[i]) || s[i] == ',')) ++i;
        if (i >= n) break;
        std::size_t j = i;
        while (j < n && !py_space(s[j])) ++j;
        std::string url = from_u32(s, i, j);
        if (!url.empty() && url.back() == ',') {
            while (!url.empty() && url.back() == ',') url.pop_back();
            i = j;
        } else {
            long depth = 0;
            while (j < n) {
                const uint32_t c = s[j];
                if (c == '(') depth += 1;
                else if (c == ')') depth = std::max(0L, depth - 1);
                else if (c == ',' && depth == 0) { j += 1; break; }
                j += 1;
            }
            i = j;
        }
        if (!url.empty()) urls.push_back(url);
    }
    return urls;
}

// (url, newlines before it, kind) referenced by CSS: url(...) and @import. kind is "css" for @import targets,
// otherwise guessed from the extension. (sitecheck.py returns the offset and counts the newlines itself.)
struct CssUrl { std::string url; long newlines; std::string kind; };
inline std::vector<CssUrl> css_urls(const std::string& text) {
    // blank out comments, keeping every newline (and every offset) in place
    U32 css = to_u32(text);
    for (std::size_t i = 0; i + 1 < css.size();) {
        if (!(css[i] == '/' && css[i + 1] == '*')) { ++i; continue; }
        std::size_t e = std::string::npos;
        for (std::size_t k = i + 2; k + 1 < css.size(); ++k)
            if (css[k] == '*' && css[k + 1] == '/') { e = k; break; }
        if (e == std::string::npos) break;
        for (std::size_t k = i; k < e + 2; ++k)
            if (css[k] != '\n') css[k] = ' ';
        i = e + 2;
    }
    std::vector<std::size_t> nl_prefix(css.size() + 1, 0);
    for (std::size_t k = 0; k < css.size(); ++k) nl_prefix[k + 1] = nl_prefix[k] + (css[k] == '\n' ? 1 : 0);
    std::vector<CssUrl> out;
    const std::string url_pat = "url\\(\\s*(?:\"([^\"]*)\"|'([^']*)'|([^)\\s]*))\\s*\\)";
    const std::string import_tail = "@import\\s*$";
    const std::string import_pat = "@import\\s+(?:\"([^\"]+)\"|'([^']+)')";
    const auto& url_rx = re::compile(url_pat, re::I);
    for (const auto& m : url_rx.finditer(css)) {
        std::string g = m.group(1);
        if (g.empty()) g = m.group(2);
        if (g.empty()) g = m.group(3);
        const std::string u = strip(g);
        const std::string lu = lower(u);
        if (u.empty() || lu.rfind("data:", 0) == 0 || lu.rfind("#", 0) == 0 || lu.rfind("about:", 0) == 0 || lu.rfind("var(", 0) == 0) continue;
        const auto st = static_cast<std::size_t>(m.start());
        const std::string window = from_u32(css, st >= 24 ? st - 24 : 0, st);
        const bool is_import = re::search(import_tail, window, re::I).ok;
        out.push_back({u, static_cast<long>(nl_prefix[st]), is_import ? std::string("css") : kind_from_ext(u)});
    }
    const auto& imp_rx = re::compile(import_pat, re::I);
    for (const auto& m : imp_rx.finditer(css)) {
        std::string g = m.group(1);
        if (g.empty()) g = m.group(2);
        const std::string u = strip(g);
        if (!u.empty()) out.push_back({u, static_cast<long>(nl_prefix[static_cast<std::size_t>(m.start())]), "css"});
    }
    return out;
}

// ---- HTML analysis --------------------------------------------------------------------------------------------------
using AttrMap = std::map<std::string, std::string>;   // first value of each attribute; a bare attribute is ""
inline const std::string* attr(const AttrMap& a, const std::string& k) {
    const auto it = a.find(k);
    return it == a.end() ? nullptr : &it->second;
}
inline std::string attr_or(const AttrMap& a, const std::string& k) { const auto* v = attr(a, k); return v ? *v : std::string(); }
inline std::optional<std::string> attr_opt(const AttrMap& a, const std::string& k) { const auto* v = attr(a, k); return v ? std::optional<std::string>(*v) : std::nullopt; }
inline bool has_aria_name(const AttrMap& a) {
    for (const char* k : {"aria-label", "aria-labelledby", "title"})
        if (!strip(attr_or(a, k)).empty()) return true;
    return false;
}

struct Ref { std::string cat, kind, raw; long line; std::optional<std::size_t> img_index; };
struct ImgInfo {
    long line = 0;
    std::optional<std::string> src, alt, width, height, srcset;
    bool hidden = false, styled_size = false;
};
struct Control { long line; std::string tag; std::optional<std::string> type; std::string id; bool named, in_label, placeholder; };
struct Collector { std::string kind; long line; bool named; std::vector<std::string> text; };

class PageParser : public htmlp::HTMLParser {
public:
    std::optional<std::string> doctype, html_lang, title, base_href;
    std::vector<AttrMap> metas;
    std::vector<Ref> refs;
    std::vector<std::pair<std::string, std::vector<long>>> ids;   // in first-seen order
    std::set<std::string> anchor_names, label_for;
    std::vector<ImgInfo> imgs;
    long icon_links = 0, h1 = 0;
    std::vector<Control> controls;
    std::vector<std::pair<std::string, long>> unnamed;
    std::vector<std::pair<std::string, long>> styles, style_attrs;   // (css text, line)

    void finish() {   // PageParser.close()
        close();
        while (!collect_.empty()) { const Collector c = collect_.back(); collect_.pop_back(); close_collector(c); }
        flush_style();
    }
    std::string text() const { return join(text_, " "); }

protected:
    void handle_decl(const std::string& decl) override {
        if (lower(decl).rfind("doctype", 0) == 0 && !doctype) doctype = decl;
    }
    void handle_starttag(const std::string& tag, const htmlp::Attrs& attrs) override {
        AttrMap a;
        for (const auto& at : attrs)
            if (!a.count(at.name)) a[at.name] = at.value.value_or("");
        const long line = getpos().first;
        if (!attr_or(a, "id").empty() && !template_) {
            const std::string ident = strip(a["id"]);
            if (!ident.empty()) {
                auto it = std::find_if(ids.begin(), ids.end(), [&](const auto& p) { return p.first == ident; });
                if (it == ids.end()) ids.push_back({ident, {line}});
                else it->second.push_back(line);
            }
        }
        if (!attr_or(a, "style").empty()) style_attrs.emplace_back(a["style"], line);

        if (tag == "svg") svg_ += 1;
        else if (tag == "html") {
            if (!html_lang) html_lang = attr_or(a, "lang");
        } else if (tag == "title") {
            if (svg_) svg_title_ = true;
            else {
                in_title_ = true;
                if (!title) title = "";
            }
        } else if (tag == "meta") metas.push_back(a);
        else if (tag == "base") {
            if (!base_href && !attr_or(a, "href").empty()) base_href = a["href"];
        } else if (tag == "link") link_tag(a, line);
        else if (tag == "script") {
            skip_ += 1;
            if (attr(a, "src")) asset("js", a["src"], line);
        } else if (tag == "style") {
            skip_ += 1;
            style_buf_ = true; style_line_ = line; style_chunks_.clear();
        } else if (tag == "template") {
            template_ += 1;
            skip_ += 1;
        } else if (tag == "img") img(a, line);
        else if (tag == "source") {
            if (!attr_or(a, "src").empty()) asset("media", a["src"], line);
            for (const auto& u : parse_srcset(attr_or(a, "srcset"))) asset("img", u, line);
        } else if (tag == "video" || tag == "audio") {
            if (!attr_or(a, "src").empty()) asset("media", a["src"], line);
            if (tag == "video" && !attr_or(a, "poster").empty()) asset("img", a["poster"], line);
        } else if (tag == "track") {
            if (!attr_or(a, "src").empty()) asset("media", a["src"], line);
        } else if (tag == "iframe" || tag == "embed" || tag == "frame") {
            if (!attr_or(a, "src").empty()) asset("frame", a["src"], line);
        } else if (tag == "object") {
            if (!attr_or(a, "data").empty()) asset("frame", a["data"], line);
        } else if (tag == "input") input(a, line);
        else if (tag == "select" || tag == "textarea") control(tag, a, line, std::nullopt);
        else if (tag == "label") {
            label_depth_ += 1;
            if (!attr_or(a, "for").empty()) label_for.insert(strip(a["for"]));
        } else if (tag == "form") {
            if (!attr_or(a, "action").empty()) refs.push_back({"form", "form", a["action"], line, std::nullopt});
        } else if (tag == "h1") h1 += 1;
        else if (tag == "a" || tag == "area") {
            if (!attr_or(a, "name").empty()) anchor_names.insert(strip(a["name"]));
            if (attr(a, "href")) {
                refs.push_back({"link", "page", a["href"], line, std::nullopt});
                if (tag == "a") collect_.push_back({"link", line, has_aria_name(a), {}});
                else if (!(!strip(attr_or(a, "alt")).empty() || has_aria_name(a))) unnamed.emplace_back("link", line);
            }
        } else if (tag == "button") collect_.push_back({"button", line, has_aria_name(a), {}});
    }
    void handle_startendtag(const std::string& tag, const htmlp::Attrs& attrs) override {
        handle_starttag(tag, attrs);
        static const std::set<std::string> closing = {"a", "button", "label", "svg", "title", "script", "style", "template"};
        if (!closing.count(tag)) return;
        handle_endtag(tag);
    }
    void handle_endtag(const std::string& tag) override {
        if (tag == "title") {
            if (svg_title_) svg_title_ = false;
            else in_title_ = false;
        } else if (tag == "svg") svg_ = std::max(0L, svg_ - 1);
        else if (tag == "script") skip_ = std::max(0L, skip_ - 1);
        else if (tag == "style") {
            skip_ = std::max(0L, skip_ - 1);
            flush_style();
        } else if (tag == "template") {
            template_ = std::max(0L, template_ - 1);
            skip_ = std::max(0L, skip_ - 1);
        } else if (tag == "label") label_depth_ = std::max(0L, label_depth_ - 1);
        else if (tag == "a" || tag == "button") {
            const std::string kind = tag == "a" ? "link" : "button";
            for (std::size_t i = collect_.size(); i-- > 0;)
                if (collect_[i].kind == kind) {
                    const Collector c = collect_[i];
                    collect_.erase(collect_.begin() + static_cast<std::ptrdiff_t>(i));
                    close_collector(c);
                    break;
                }
        }
    }
    void handle_data(const std::string& data) override {
        if (style_buf_) { style_chunks_.push_back(data); return; }
        if (in_title_) { *title += data; return; }
        if (skip_) return;
        if (!collect_.empty() && !strip(data).empty())
            for (auto& c : collect_) c.text.push_back(data);
        if (text_len_ < 400000) {
            text_.push_back(data);
            text_len_ += static_cast<long>(cplen(data));
        }
    }

private:
    bool in_title_ = false, svg_title_ = false, style_buf_ = false;
    long svg_ = 0, skip_ = 0, template_ = 0, label_depth_ = 0, style_line_ = 0, text_len_ = 0;
    std::vector<std::string> style_chunks_, text_;
    std::vector<Collector> collect_;

    void flush_style() {
        if (!style_buf_) return;
        styles.emplace_back(join(style_chunks_, ""), style_line_);
        style_buf_ = false;
        style_chunks_.clear();
    }
    void asset(const std::string& kind, const std::string& raw, long line, std::optional<std::size_t> img_index = std::nullopt) {
        refs.push_back({"asset", kind, raw, line, img_index});
    }
    void close_collector(const Collector& c) {
        if (!(c.named || !strip(join(c.text, "")).empty())) unnamed.emplace_back(c.kind, c.line);
    }
    void control(const std::string& tag, const AttrMap& a, long line, std::optional<std::string> typ) {
        controls.push_back({line, tag, std::move(typ), strip(attr_or(a, "id")), has_aria_name(a), label_depth_ > 0, !strip(attr_or(a, "placeholder")).empty()});
    }
    void link_tag(const AttrMap& a, long line) {
        std::set<std::string> rel;
        for (const auto& r : split_ws(lower(attr_or(a, "rel")))) rel.insert(r);
        auto any = [&](std::initializer_list<const char*> l) { for (const char* x : l) if (rel.count(x)) return true; return false; };
        const auto* href = attr(a, "href");
        if (href && !strip(*href).empty()) {
            if (rel.count("stylesheet")) asset("css", *href, line);
            else if (any({"icon", "apple-touch-icon", "apple-touch-icon-precomposed", "mask-icon"})) {
                asset("img", *href, line);
                if (rel.count("icon")) icon_links += 1;
            } else if (rel.count("manifest")) asset("other", *href, line);
            else if (any({"preload", "modulepreload"})) {
                static const std::map<std::string, std::string> as_kind = {{"image", "img"}, {"style", "css"}, {"script", "js"}, {"font", "font"},
                                                                           {"video", "media"}, {"audio", "media"}, {"track", "media"}};
                const auto it = as_kind.find(lower(attr_or(a, "as")));
                asset(it != as_kind.end() ? it->second : (rel.count("modulepreload") ? "js" : "other"), *href, line);
            }
        }
        for (const auto& u : parse_srcset(attr_or(a, "imagesrcset"))) asset("img", u, line);
    }
    void img(const AttrMap& a, long line) {
        const std::string style = lower(attr_or(a, "style"));
        ImgInfo info;
        info.line = line; info.src = attr_opt(a, "src"); info.alt = attr_opt(a, "alt");
        info.width = attr_opt(a, "width"); info.height = attr_opt(a, "height"); info.srcset = attr_opt(a, "srcset");
        const std::string role = lower(attr_or(a, "role"));
        info.hidden = lower(attr_or(a, "aria-hidden")) == "true" || role == "presentation" || role == "none";
        const std::string wpat = "(^|;|\\s)width\\s*:", hpat = "(^|;|\\s)height\\s*:";
        info.styled_size = style.find("aspect-ratio") != std::string::npos || (re::search(wpat, style).ok && re::search(hpat, style).ok);
        imgs.push_back(info);
        const std::size_t index = imgs.size() - 1;
        if (const auto* src = attr(a, "src")) asset("img", *src, line, index);
        for (const auto& u : parse_srcset(attr_or(a, "srcset"))) asset("img", u, line);
        if (!strip(attr_or(a, "alt")).empty())
            for (auto& c : collect_) c.named = true;
    }
    void input(const AttrMap& a, long line) {
        std::string typ = lower(strip(attr_or(a, "type")));
        if (attr_or(a, "type").empty()) typ = "text";
        if (typ == "hidden" || typ == "submit" || typ == "reset" || typ == "button") return;
        if (typ == "image") {
            if (!attr_or(a, "src").empty()) asset("img", attr_or(a, "src"), line);
            if (!(!strip(attr_or(a, "alt")).empty() || has_aria_name(a))) unnamed.emplace_back("button", line);
            return;
        }
        control("input", a, line, typ);
    }
};

struct LinkRef { std::string target; std::optional<std::string> frag; long line; std::string raw; };
struct AssetRef { std::string target, kind; long line; std::optional<std::size_t> img_index; };
struct PageAnalysis {
    std::string url, title;
    std::vector<Finding> findings;
    std::vector<LinkRef> links;
    std::vector<AssetRef> assets;
    std::set<std::string> targets;   // ids and <a name> values
    std::vector<ImgInfo> imgs;
    long icon_links = 0;
};

inline const AttrMap* meta_of(const std::vector<AttrMap>& metas, const std::string& key, const std::string& value) {
    for (const auto& m : metas)
        if (lower(strip(attr_or(m, key))) == value) return &m;
    return nullptr;
}
inline bool viewport_blocks_zoom(const std::string& content) {
    std::map<std::string, std::string> params;
    std::string part;
    auto take = [&] {
        if (const auto eq = part.find('='); eq != std::string::npos) params[lower(strip(part.substr(0, eq)))] = lower(strip(part.substr(eq + 1)));
        part.clear();
    };
    for (char c : content) {
        if (c == ',' || c == ';') take();
        else part += c;
    }
    take();
    const auto us = params.find("user-scalable");
    if (us != params.end() && (us->second == "no" || us->second == "0")) return true;
    const auto ms = params.find("maximum-scale");
    if (ms == params.end()) return false;
    try { return master::py_float(ms->second) < 2; } catch (const PyErr&) { return false; }
}

// one HTML document; no network access
inline PageAnalysis analyze(const std::string& html_text, const std::string& page_url, const std::string& header_charset,
                            const std::map<std::string, std::string>& headers, bool is_home, bool site_is_local) {
    PageParser p;
    try {
        p.feed(html_text);
        p.finish();
    } catch (const std::exception&) {   // html.parser can choke on rare malformed markup; keep what we have
    }
    PageAnalysis out;
    out.url = page_url; out.title = strip(p.title.value_or("")); out.imgs = p.imgs; out.icon_links = p.icon_links;
    for (const auto& kv : p.ids) out.targets.insert(kv.first);
    for (const auto& n : p.anchor_names) out.targets.insert(n);
    std::string base = page_url;
    if (p.base_href) {
        const Resolved r = resolve(p.base_href, page_url);
        if (r.kind == "http") base = r.url;
    }
    const bool https_page = page_url.rfind("https:", 0) == 0;
    auto add = [&](const std::string& rule, const std::string& url = "", long line = 0, const std::string& detail = "") {
        out.findings.push_back({rule, page_url, url.empty() ? page_url : url, line, detail, 0});
    };
    auto check_location = [&](const std::string& kind, const std::string& raw, const std::string& url, long line, const std::string& what) {
        if (kind == "file") { add("local-url", raw, line, what + " at line " + std::to_string(line) + " points at a file:// path"); return; }
        if (kind == "http" && !site_is_local && is_local_host(up::urlsplit(url).hostname()))
            add("local-url", url, line, what + " at line " + std::to_string(line) + " points at a local address");
    };

    // document-level checks
    if (lower(p.doctype.value_or("")).rfind("doctype html", 0) != 0) add("missing-doctype", "", 0, "first line is not <!doctype html>");
    if (strip(p.html_lang.value_or("")).empty()) add("missing-lang", "", 0, "<html> has no lang");
    if (out.title.empty()) add("missing-title", "", 0, "no <title>, or it is empty");
    const AttrMap* viewport = meta_of(p.metas, "name", "viewport");
    if (!viewport) add("missing-viewport", "", 0, "no <meta name=\"viewport\">");
    else if (viewport_blocks_zoom(attr_or(*viewport, "content"))) add("zoom-disabled", "", 0, "viewport is \"" + strip(attr_or(*viewport, "content")) + "\"");
    const AttrMap* desc = meta_of(p.metas, "name", "description");
    if (!desc || strip(attr_or(*desc, "content")).empty()) add("missing-description", "", 0, "no meta description");
    bool has_charset = !header_charset.empty() || html_text.rfind("\xEF\xBB\xBF", 0) == 0;
    for (const auto& m : p.metas) {
        if (m.count("charset")) has_charset = true;
        if (lower(attr_or(m, "http-equiv")) == "content-type" && lower(attr_or(m, "content")).find("charset=") != std::string::npos) has_charset = true;
    }
    if (!has_charset) add("missing-charset", "", 0, "no <meta charset> and the server sent no charset");
    std::vector<std::string> robots;
    for (const auto& m : p.metas) {
        const std::string name = lower(attr_or(m, "name"));
        if (name == "robots" || name == "googlebot") robots.push_back(lower(attr_or(m, "content")));
    }
    const auto xr = headers.find("x-robots-tag");
    if (join(robots, " ").find("noindex") != std::string::npos || (xr != headers.end() && lower(xr->second).find("noindex") != std::string::npos))
        add("noindex", "", 0, "robots noindex is set");
    if (p.h1 == 0) add("missing-h1", "", 0, "no <h1>");
    for (const auto& [ident, lines] : p.ids) {
        if (lines.size() < 2) continue;
        std::vector<std::string> shown;
        for (std::size_t k = 0; k < lines.size() && k < 8; ++k) shown.push_back(std::to_string(lines[k]));
        add("duplicate-id", "", lines[0], "id=\"" + ident + "\" is used " + std::to_string(lines.size()) + " times (lines " + join(shown, ", ") + ")");
    }

    // images: alt text and intrinsic size attributes
    for (const auto& im : p.imgs) {
        const Resolved r = resolve(im.src, base);
        const std::string raw_src = strip(im.src.value_or(""));
        const std::string where = r.kind == "http" ? r.url
                                  : (lower(raw_src).rfind("data:", 0) == 0 ? std::string("inline data: image")
                                                                          : (raw_src.empty() ? std::string("<img>") : cpslice(raw_src, 0, 80)));
        if (!im.alt && !im.hidden) add("img-missing-alt", where, im.line, "line " + std::to_string(im.line));
        if (!(!im.width.value_or("").empty() && !im.height.value_or("").empty()) && !im.styled_size) add("img-missing-size", where, im.line, "line " + std::to_string(im.line));
    }

    // form fields and accessible names
    for (const auto& c : p.controls) {
        const bool labeled = c.named || c.in_label || (!c.id.empty() && p.label_for.count(c.id));
        if (labeled) continue;
        const std::string what = "<" + c.tag + (c.type && !c.type->empty() ? " type=\"" + *c.type + "\"" : std::string()) + ">";
        const std::string hint = c.placeholder ? " (has a placeholder, but a placeholder is not a label)" : "";
        add("unlabeled-field", "", c.line, what + " at line " + std::to_string(c.line) + hint);
    }
    for (const auto& [kind, line] : p.unnamed)
        add(kind == "link" ? "unnamed-link" : "unnamed-button", "", line, std::string("<") + (kind == "link" ? "a" : "button") + "> at line " + std::to_string(line));

    const std::string lorem = "\\blorem\\s+ipsum\\b";
    if (re::search(lorem, p.text(), re::I).ok) add("lorem-ipsum", "", 0, "lorem ipsum found in the page text");

    // links, assets and forms
    for (const auto& ref : p.refs) {
        const Resolved r = resolve(ref.raw, base);
        if (ref.cat == "link") {
            const std::string stripped = strip(drop_tab_crlf(ref.raw));
            if (r.kind == "empty" || r.kind == "js" || stripped == "#") {
                add("placeholder-link", "", ref.line, "href=\"" + cpslice(stripped, 0, 40) + "\" at line " + std::to_string(ref.line));
                continue;
            }
            check_location(r.kind, ref.raw, r.url, ref.line, "link");
            if (r.kind == "http") out.links.push_back({r.url, r.frag, ref.line, ref.raw});
        } else if (ref.cat == "asset") {
            if (r.kind == "empty") {
                if (ref.kind == "img" && ref.img_index) add("broken-asset", "", ref.line, "<img> at line " + std::to_string(ref.line) + " has an empty src");
                continue;
            }
            check_location(r.kind, ref.raw, r.url, ref.line, "resource");
            if (r.kind != "http") continue;
            if (https_page && r.url.rfind("http:", 0) == 0) add("mixed-content", r.url, ref.line, "loaded over http at line " + std::to_string(ref.line));
            out.assets.push_back({r.url, ref.kind, ref.line, ref.img_index});
        } else if (ref.cat == "form") {
            check_location(r.kind, ref.raw, r.url, ref.line, "form action");
            if (r.kind == "http" && https_page && r.url.rfind("http:", 0) == 0) add("mixed-content", r.url, ref.line, "form submits over http at line " + std::to_string(ref.line));
        }
    }

    // CSS inside the page: <style> blocks and style="" attributes
    std::vector<std::pair<std::string, long>> blocks = p.styles;
    blocks.insert(blocks.end(), p.style_attrs.begin(), p.style_attrs.end());
    for (const auto& [css, start_line] : blocks) {
        for (const auto& cu : css_urls(css)) {
            const Resolved r = resolve(cu.url, base);
            const long line = start_line + cu.newlines;
            check_location(r.kind, cu.url, r.url, line, "CSS url()");
            if (r.kind != "http") continue;
            if (https_page && r.url.rfind("http:", 0) == 0) add("mixed-content", r.url, line, "CSS loads it over http at line " + std::to_string(line));
            out.assets.push_back({r.url, cu.kind, line, std::nullopt});
        }
    }

    // home page only: the favicon is checked later (it needs the network); og:image now
    if (is_home) {
        const AttrMap* og = meta_of(p.metas, "property", "og:image");
        if (!og) og = meta_of(p.metas, "name", "og:image");
        const std::string content = og ? strip(attr_or(*og, "content")) : std::string();
        if (content.empty()) add("missing-og-image", "", 0, "no og:image meta tag");
        else {
            const Resolved r = resolve(content, base);
            const std::string abs_pat = "^https?://";
            if (!re::match(abs_pat, content, re::I).ok) add("relative-og-image", r.url.empty() ? content : r.url, 0, "og:image is \"" + cpslice(content, 0, 80) + "\"");
            if (r.kind == "http") out.assets.push_back({r.url, "img", 0, std::nullopt});
        }
    }
    return out;
}

}  // namespace packtools::master_sitecheck
