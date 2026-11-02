// daytrade yahoo - the chart reader shared by the day-trader ports (entry, exit, scanner, timemachine): Yahoo chart
// JSON over https, query2 first and query1 when query2 answers 429 or 5xx, one retry on the same host after a pause on
// a 429, Yahoo's own error JSON on a 404. Same behaviour as each script's yahoo_get (day-trader-skill, Jake
// Schincariol, MIT). E is the calling tool's error type.
namespace packtools::daytrade_yahoo {
using namespace pyrt;
static const char* UA = "Mozilla/5.0 (compatible; day-trader-skill/2.0; +https://github.com/Jakeschincariol/day-trader-skill)";
inline void pause(double sec) { if (sec > 0) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(sec * 1000))); }
template <class E> std::string get(const std::string& path, double backoff = 5.0, int timeout = 20) {
    std::string last = "no answer";
    for (const char* host : {"query2", "query1"}) {
        const std::string url = std::string("https://") + host + ".finance.yahoo.com" + path;
        const std::string bare = split(url, "?")[0];
        for (int attempt = 0; attempt < 2; ++attempt) {
            const HttpResp r = http_get(url, {std::string("User-Agent: ") + UA, "Accept: application/json"}, timeout);
            if (r.status == 0 || (r.status < 400 && !r.error.empty())) {
                last = "could not connect: " + (r.error.empty() ? std::string("no response") : r.error);
                break;
            }
            if (r.status < 400) return decode_utf8_replace(r.body);
            const long code = r.status;
            if (code == 404) {
                if (!r.body.empty()) return decode_utf8_replace(r.body);
                throw E("Yahoo has no chart at " + bare + " (HTTP 404)");
            }
            last = "HTTP " + std::to_string(code);
            if (code == 429 && attempt == 0) {
                pause(backoff);
                continue;
            }
            if (code == 429 || code >= 500) break;
            throw E("Yahoo answered HTTP " + std::to_string(code) + " for " + bare);
        }
    }
    throw E("Yahoo did not answer (" + last + " on query2 and query1): wait a minute and try again");
}
// open(path) or gzip.open(path, "rt") for a .gz, both with universal newlines
inline std::string read_text_any(const std::string& path) {
    if (!ends(path, ".gz")) return read_text(path);
    const std::string t = zip::gunzip(read_bytes(path));
    std::string o; o.reserve(t.size());
    for (std::size_t k = 0; k < t.size(); ++k) {
        if (t[k] != '\r') { o += t[k]; continue; }
        o += '\n';
        if (k + 1 < t.size() && t[k + 1] == '\n') k++;
    }
    return decode_utf8_replace(o);
}
}  // namespace packtools::daytrade_yahoo
