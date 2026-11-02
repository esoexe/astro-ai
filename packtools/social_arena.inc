// arena-skill (Jake Schincariol, MIT), ported to C++: the tournament state machine behind /arena.
//   arena/bracket   plan, init, next, prompts, show, save, check, pairings, collect, record, advance, status, winner, card
// Same cards, same seeds, same pairings and the same arena.json as bracket.py: random.Random(str) seeds through
// SHA-512 exactly like CPython, and every shuffle, random() and bye follows the original's order of calls. Like the
// earlier Astro Node.js port it adds `show` and `save`, for A.S.T.R.O AI's built-in sub-agent, which cannot open
// files. Group 6. ArenaError is "error: <message>", exit 2; other Python exceptions stay uncaught (exit 1).
#include <array>
#include <cmath>
#include <ctime>
#include <random>
#include <set>

namespace packtools::arena {
using namespace pyrt;
namespace pm = packtools::master;

struct ArenaError : std::runtime_error { using std::runtime_error::runtime_error; };

// ------------------------------------------------------------------ hashlib.sha512 (FIPS 180-4)
inline std::array<uint8_t, 64> sha512(const std::string& msg) {
    static const uint64_t K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL,
        0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL, 0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL, 0x983e5152ee66dfabULL,
        0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL,
        0x53380d139d95b3dfULL, 0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL, 0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL,
        0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL, 0xca273eceea26619cULL,
        0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL, 0x1b710b35131c471bULL, 0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
    uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                     0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    std::string m = msg;
    const uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 128 != 112) m += '\0';
    for (int k = 0; k < 8; ++k) m += '\0';   // the high 64 bits of the 128-bit length
    for (int k = 7; k >= 0; --k) m += static_cast<char>((bits >> (8 * k)) & 0xff);
    const auto rotr = [](uint64_t x, int n) { return (x >> n) | (x << (64 - n)); };
    for (std::size_t off = 0; off < m.size(); off += 128) {
        uint64_t w[80];
        for (int t = 0; t < 16; ++t) {
            uint64_t v = 0;
            for (int k = 0; k < 8; ++k) v = (v << 8) | static_cast<uint8_t>(m[off + static_cast<std::size_t>(t * 8 + k)]);
            w[t] = v;
        }
        for (int t = 16; t < 80; ++t) {
            const uint64_t s0 = rotr(w[t - 15], 1) ^ rotr(w[t - 15], 8) ^ (w[t - 15] >> 7);
            const uint64_t s1 = rotr(w[t - 2], 19) ^ rotr(w[t - 2], 61) ^ (w[t - 2] >> 6);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 80; ++t) {
            const uint64_t S1 = rotr(e, 14) ^ rotr(e, 18) ^ rotr(e, 41);
            const uint64_t ch = (e & f) ^ (~e & g);
            const uint64_t t1 = hh + S1 + ch + K[t] + w[t];
            const uint64_t S0 = rotr(a, 28) ^ rotr(a, 34) ^ rotr(a, 39);
            const uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint64_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<uint8_t, 64> out{};
    for (int k = 0; k < 8; ++k)
        for (int j = 0; j < 8; ++j) out[static_cast<std::size_t>(k * 8 + j)] = static_cast<uint8_t>(h[k] >> (56 - 8 * j));
    return out;
}
inline std::string sha512_hex(const std::string& s) {
    static const char* hx = "0123456789abcdef";
    std::string o;
    for (uint8_t b : sha512(s)) { o += hx[b >> 4]; o += hx[b & 15]; }
    return o;
}

// ------------------------------------------------------------------ random.Random(str): CPython's Mersenne Twister
struct PyRandom {
    static constexpr int N = 624, M = 397;
    uint32_t mt[N]; int mti = N + 1;
    void init_genrand(uint32_t s) {
        mt[0] = s;
        for (mti = 1; mti < N; mti++) mt[mti] = 1812433253U * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + static_cast<uint32_t>(mti);
    }
    void init_by_array(const std::vector<uint32_t>& key) {
        init_genrand(19650218U);
        int i = 1; std::size_t j = 0;
        for (int k = std::max<int>(N, static_cast<int>(key.size())); k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<uint32_t>(j);
            i++; j++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
            if (j >= key.size()) j = 0;
        }
        for (int k = N - 1; k; k--) {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) - static_cast<uint32_t>(i);
            i++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
        }
        mt[0] = 0x80000000U;
        mti = N;
    }
    // random.seed(a) for a str, version 2: int.from_bytes(a.encode() + sha512(a.encode()).digest(), "big"), whose
    // 32-bit words go to init_by_array least significant first (CPython drops the zero words above the top bit)
    explicit PyRandom(const std::string& seed) {
        std::string bytes = seed;
        for (uint8_t b : sha512(seed)) bytes += static_cast<char>(b);
        std::vector<uint32_t> key;
        for (std::size_t end = bytes.size(); end > 0;) {
            const std::size_t beg = end >= 4 ? end - 4 : 0;
            uint32_t w = 0;
            for (std::size_t k = beg; k < end; ++k) w = (w << 8) | static_cast<uint8_t>(bytes[k]);
            key.push_back(w);
            end = beg;
        }
        while (key.size() > 1 && key.back() == 0) key.pop_back();
        init_by_array(key);
    }
    uint32_t genrand() {
        static const uint32_t mag01[2] = {0x0U, 0x9908b0dfU};
        uint32_t y;
        if (mti >= N) {
            int kk = 0;
            for (; kk < N - M; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            for (; kk < N - 1; kk++) { y = (mt[kk] & 0x80000000U) | (mt[kk + 1] & 0x7fffffffU); mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U]; }
            y = (mt[N - 1] & 0x80000000U) | (mt[0] & 0x7fffffffU);
            mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11); y ^= (y << 7) & 0x9d2c5680U; y ^= (y << 15) & 0xefc60000U; y ^= (y >> 18);
        return y;
    }
    double random() { const uint32_t a = genrand() >> 5, b = genrand() >> 6; return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0); }
    uint64_t getrandbits(int k) {
        if (k <= 0) return 0;
        if (k <= 32) return genrand() >> (32 - k);
        const uint64_t lo = genrand();
        const uint64_t hi = genrand() >> (64 - k);
        return lo | (hi << 32);
    }
    uint64_t randbelow(uint64_t n) {
        int k = 0;
        for (uint64_t t = n; t; t >>= 1) k++;
        uint64_t r = getrandbits(k);
        while (r >= n) r = getrandbits(k);
        return r;
    }
    template <class T> void shuffle(std::vector<T>& x) {
        for (std::size_t i = x.size(); i-- > 1;) { const std::size_t j = static_cast<std::size_t>(randbelow(i + 1)); std::swap(x[i], x[j]); }
    }
};

// ------------------------------------------------------------------ small Python helpers
constexpr long long DEFAULT_AGENTS = 100, QUICK_AGENTS = 16, DEFAULT_WAVE = 10, CALLS_PER_MATCH = 5;
inline const char* ROOT = ".arena";
inline const char* LATEST = "LATEST";
inline const char* STATE_FILE = "arena.json";
inline const std::vector<std::string> PHASES = {"spawn", "attack", "defend", "judge", "final"};
inline const std::vector<std::string> TEMPLATES = {"competitor", "attacker", "defender", "judge", "final"};
inline const std::vector<std::pair<std::string, int>> WEIGHTS = {{"correctness", 30}, {"completeness", 25}, {"specificity", 15}, {"robustness", 20}, {"clarity", 10}};
constexpr std::size_t SHOW_LIMIT = 24000;   // characters pasted per file by show

inline std::string i2s(long long v) { return std::to_string(v); }
inline std::string key_repr(const std::string& k) { return str_repr(k); }
inline Val item(const Val& o, const std::string& k) {   // o[k]
    if (o.t != Val::DICT) {
        if (o.t == Val::LIST) throw PyErr("TypeError", "list indices must be integers or slices, not str");
        throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    }
    const Val* p = o.d->get(k);
    if (!p) throw PyErr("KeyError", key_repr(k));
    return *p;
}
inline Val item(const Val& o, std::size_t i) {
    if (o.t != Val::LIST) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object is not subscriptable");
    if (i >= o.l->size()) throw PyErr("IndexError", "list index out of range");
    return (*o.l)[i];
}
inline std::string sitem(const Val& o, const std::string& k) { return pystr(item(o, k)); }   // str(o[k])
inline void put(const Val& o, const std::string& k, Val v) { if (o.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(o) + "' object does not support item assignment"); o.d->set(k, std::move(v)); }
inline List lst(const Val& v) {   // a copy: the elements share their dicts with the state
    if (v.t != Val::LIST) throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
    return *v.l;
}
inline std::size_t len(const Val& v) {
    if (v.t == Val::LIST || v.t == Val::DICT) return v.size();
    if (v.t == Val::STR) return cplen(v.s);
    throw PyErr("TypeError", std::string("object of type '") + type_name(v) + "' has no len()");
}
inline std::vector<std::string> keys(const Val& d) {
    std::vector<std::string> o;
    if (d.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(d) + "' object has no attribute 'items'");
    for (const auto& kv : d.d->items) o.push_back(kv.first);
    return o;
}
inline bool py_eq(const Val& a, const Val& b) {   // a == b
    if (a.t == Val::LIST || b.t == Val::LIST) {
        if (a.t != b.t || a.l->size() != b.l->size()) return false;
        for (std::size_t k = 0; k < a.l->size(); ++k) if (!py_eq((*a.l)[k], (*b.l)[k])) return false;
        return true;
    }
    if (a.t == Val::DICT || b.t == Val::DICT) {
        if (a.t != b.t || a.d->items.size() != b.d->items.size()) return false;
        for (const auto& [k, v] : a.d->items) { const Val* w = b.d->get(k); if (!w || !py_eq(v, *w)) return false; }
        return true;
    }
    return pm::key_eq(a, b);
}
inline long long as_int(const Val& v) {   // an int field of arena.json
    if (v.t == Val::INT && !v.big()) return v.i;
    if (v.t == Val::BOOL) return v.b ? 1 : 0;
    if (v.t == Val::FLOAT && std::isfinite(v.f)) return static_cast<long long>(v.f);
    throw PyErr("TypeError", std::string("'") + type_name(v) + "' object cannot be interpreted as an integer");
}
inline bool is_digits(const std::string& s) {   // str.isdigit()
    if (s.empty()) return false;
    for (uint32_t c : ucase_detail::u32(s)) if (!uprop::digit(c) && !uprop::decimal(c)) return false;
    return true;
}
inline std::string int_key(const std::string& s) {   // int(s) as a canonical decimal string, for ==
    for (uint32_t c : ucase_detail::u32(s)) if (!uprop::decimal(c)) throw PyErr("ValueError", "invalid literal for int() with base 10: " + str_repr(s));
    std::string d = pm::ascii_digits(s);
    std::size_t k = 0; while (k + 1 < d.size() && d[k] == '0') k++;
    return d.substr(k);
}
inline std::string lstrip_chars(const std::string& s, char c) { std::size_t k = 0; while (k < s.size() && s[k] == c) k++; return s.substr(k); }
inline std::string rstrip_nl(std::string s) { while (!s.empty() && s.back() == '\n') s.pop_back(); return s; }
inline std::string last_split(const std::string& s, const std::string& sep) { const std::size_t k = s.rfind(sep); return k == std::string::npos ? s : s.substr(k + sep.size()); }
inline std::string fmt_val(const Val& x) { return x.is_none() ? "?" : fmt_g(x.num()); }   // "?" if x is None else "%g" % x
inline long long ceil_div(long long a, long long b) { return static_cast<long long>(std::ceil(static_cast<double>(a) / static_cast<double>(b))); }   // math.ceil(a / b)
inline std::string stamp(const char* f) {   // time.strftime(f), local time
    std::time_t t = std::time(nullptr); std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[64]; std::strftime(b, sizeof b, f, &tm); return b;
}
inline std::string abspath(const std::string& p) {   // os.path.abspath
    std::error_code ec;
    const std::filesystem::path a = std::filesystem::absolute(fspath(p.empty() ? std::string(".") : p), ec);
    const auto u = a.lexically_normal().generic_u8string();
    std::string s(u.begin(), u.end());
#ifdef _WIN32
    s = replace_all(s, "/", "\\");
    while (s.size() > 3 && s.back() == '\\') s.pop_back();
#else
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#endif
    return s;
}
inline std::string join(std::initializer_list<std::string> parts) { std::string o; for (const auto& p : parts) o = o.empty() ? p : path_join(o, p); return o; }
inline bool has_output(const std::string& p) {   // os.path.isfile(p) and os.path.getsize(p) > 0
    std::error_code ec;
    const auto f = fspath(p);
    if (!std::filesystem::is_regular_file(f, ec)) return false;
    const auto n = std::filesystem::file_size(f, ec);
    return !ec && n > 0;
}
inline std::string read_utf8(const std::string& p, bool replace = false) { return read_text_utf8(p, replace); }   // open(p, encoding="utf-8"[, errors="replace"]).read()
inline std::string skill_path(const std::string& name) { return skill_file(name); }
// the command line that runs this tool again: what ${ASTRO_TOOL} expands to in the skill, then the tool id
inline std::string cmd(std::initializer_list<std::string> parts) {
    std::string c = tool_command();
    if (!skill_dir_ref().empty()) c += " --skill-dir \"" + abspath(skill_dir_ref()) + "\"";
    c += " arena/bracket";
    for (const auto& p : parts) c += " " + p;
    return c;
}

// ------------------------------------------------------------------ the cards
inline Val load_strategies() {
    const Val data = loads(read_utf8(skill_path("strategies.json")));
    for (const std::string& key : std::vector<std::string>{"reasoning", "workflows", "strategies"}) {
        if (data.t != Val::DICT) throw PyErr("AttributeError", std::string("'") + type_name(data) + "' object has no attribute 'get'");
        const Val raw = get(data, key);
        const Val items = truthy(raw) ? raw : Val::list();
        if (!truthy(items)) throw ArenaError("strategies.json has no '" + key + "' entries");
        List ids;
        for (const Val& it : lst(items)) ids.push_back(item(it, "id"));
        for (const Val& x : ids) pm::need_hashable(x);
        for (std::size_t a = 0; a < ids.size(); ++a)
            for (std::size_t b = a + 1; b < ids.size(); ++b)
                if (pm::key_eq(ids[a], ids[b])) throw ArenaError("strategies.json has a duplicate id in '" + key + "'");
        for (const Val& it : lst(items))
            if (!truthy(get(it, "name")) || !truthy(get(it, "how"))) throw ArenaError("strategies.json entry '" + pystr(item(it, "id")) + "' needs a name and a how");
    }
    return data;
}
inline long long combo_count(const Val& data) {
    return static_cast<long long>(len(item(data, "reasoning")) * len(item(data, "workflows")) * len(item(data, "strategies")));
}

// Properly edge-colour a bipartite (multi)graph with k colours (Konig, by alternating-path swaps), then balance the
// colour classes (de Werra) until any two differ by at most one.
inline std::vector<int> edge_colour(const std::vector<std::pair<int, int>>& edges, int k) {
    std::vector<int> colour(edges.size(), -1);
    std::array<std::map<int, std::map<int, std::size_t>>, 2> at;
    const auto slot = [&](int side, int node) -> std::map<int, std::size_t>& { return at[static_cast<std::size_t>(side)][node]; };
    const auto end_of = [&](std::size_t e, int side) { return side == 0 ? edges[e].first : edges[e].second; };
    const auto put_c = [&](std::size_t e, int c) { colour[e] = c; slot(0, edges[e].first)[c] = e; slot(1, edges[e].second)[c] = e; };
    const auto take = [&](std::size_t e) { slot(0, edges[e].first).erase(colour[e]); slot(1, edges[e].second).erase(colour[e]); };
    const auto walk = [&](int side, int node, int first, int other) {
        std::vector<std::size_t> path; int c = first;
        for (;;) {
            auto& s = slot(side, node);
            const auto it = s.find(c);
            if (it == s.end() || std::find(path.begin(), path.end(), it->second) != path.end()) return path;
            const std::size_t e = it->second;
            path.push_back(e);
            node = end_of(e, 1 - side);
            side = 1 - side;
            c = c == first ? other : first;
        }
    };
    const auto swap_path = [&](const std::vector<std::size_t>& path, int a, int b) {
        std::map<std::size_t, int> nw;
        for (std::size_t e : path) nw[e] = colour[e] == a ? b : a;
        for (std::size_t e : path) take(e);
        for (std::size_t e : path) put_c(e, nw[e]);
    };
    for (std::size_t e = 0; e < edges.size(); ++e) {
        const int u = edges[e].first, v = edges[e].second;
        std::vector<int> free_u; std::set<int> free_v;
        for (int c = 0; c < k; ++c) { if (!slot(0, u).count(c)) free_u.push_back(c); if (!slot(1, v).count(c)) free_v.insert(c); }
        int c = -1;
        for (int x : free_u) if (free_v.count(x)) { c = x; break; }
        if (c < 0) {
            if (free_u.empty() || free_v.empty()) throw PyErr("IndexError", "list index out of range");
            const int a = free_u[0], b = *free_v.begin();
            swap_path(walk(1, v, a, b), a, b);
            c = a;
        }
        put_c(e, c);
    }
    for (;;) {
        std::vector<long long> count(static_cast<std::size_t>(k), 0);
        for (int c : colour) count[static_cast<std::size_t>(c)]++;
        int hi = 0, lo = 0;
        for (int c = 1; c < k; ++c) {
            if (count[static_cast<std::size_t>(c)] > count[static_cast<std::size_t>(hi)]) hi = c;   // max by (count, -c): the first of equal counts
            if (count[static_cast<std::size_t>(c)] < count[static_cast<std::size_t>(lo)]) lo = c;
        }
        if (count[static_cast<std::size_t>(hi)] - count[static_cast<std::size_t>(lo)] <= 1) return colour;
        bool swapped = false;
        for (int side = 0; side < 2 && !swapped; ++side) {
            std::vector<int> nodes;
            for (const auto& kv : at[static_cast<std::size_t>(side)]) nodes.push_back(kv.first);   // std::map: sorted
            for (int node : nodes) {
                auto& s = at[static_cast<std::size_t>(side)][node];
                if (s.count(hi) && !s.count(lo)) {
                    const auto path = walk(side, node, hi, lo);
                    long long nh = 0, nl = 0;
                    for (std::size_t e : path) { if (colour[e] == hi) nh++; if (colour[e] == lo) nl++; }
                    if (nh > nl) { swap_path(path, hi, lo); swapped = true; break; }
                }
            }
        }
        if (!swapped) throw ArenaError("could not balance the strategy cards");
    }
}

struct Triple { int r, w, s; };
inline std::vector<Triple> deal(long long n, const std::string& seed, const Val& data) {
    const int R = static_cast<int>(len(item(data, "reasoning"))), W = static_cast<int>(len(item(data, "workflows"))), S = static_cast<int>(len(item(data, "strategies")));
    const long long total = static_cast<long long>(R) * W * S;
    if (n < 1 || n > total) throw ArenaError("can deal between 1 and " + i2s(total) + " cards, not " + i2s(n));
    PyRandom rng("arena-deal:" + seed);
    std::vector<int> pr(static_cast<std::size_t>(R)), pw(static_cast<std::size_t>(W)), ps(static_cast<std::size_t>(S));
    for (int k = 0; k < R; ++k) pr[static_cast<std::size_t>(k)] = k;
    for (int k = 0; k < W; ++k) pw[static_cast<std::size_t>(k)] = k;
    for (int k = 0; k < S; ++k) ps[static_cast<std::size_t>(k)] = k;
    rng.shuffle(pr); rng.shuffle(pw); rng.shuffle(ps);
    const long long lcm = static_cast<long long>(R) * W / std::gcd(R, W);
    std::vector<std::pair<int, int>> cells;
    for (long long i = 0; i < n; ++i) cells.emplace_back(static_cast<int>(i % R), static_cast<int>((i + i / lcm) % W));
    long long deg_r = 0, deg_w = 0;
    for (int x = 0; x < R; ++x) { long long c = 0; for (const auto& cw : cells) if (cw.first == x) c++; deg_r = std::max(deg_r, c); }
    for (int x = 0; x < W; ++x) { long long c = 0; for (const auto& cw : cells) if (cw.second == x) c++; deg_w = std::max(deg_w, c); }
    std::vector<Triple> triples;
    if (deg_r <= S && deg_w <= S) {
        std::vector<long long> order(static_cast<std::size_t>(n));
        for (long long i = 0; i < n; ++i) order[static_cast<std::size_t>(i)] = i;
        rng.shuffle(order);
        std::vector<std::pair<int, int>> e;
        for (long long i : order) e.push_back(cells[static_cast<std::size_t>(i)]);
        const auto colours = edge_colour(e, S);
        std::vector<int> strategy_of(static_cast<std::size_t>(n));
        for (std::size_t j = 0; j < order.size(); ++j) strategy_of[static_cast<std::size_t>(order[j])] = colours[j];
        for (std::size_t i = 0; i < cells.size(); ++i) triples.push_back({cells[i].first, cells[i].second, strategy_of[i]});
    } else {   // past that size a repeat pair is unavoidable: stay balanced and never repeat a card
        std::vector<long long> count(static_cast<std::size_t>(S), 0);
        std::map<std::pair<int, int>, long long> pairs_rs, pairs_ws;
        std::set<std::array<int, 3>> used;
        for (const auto& [r, w] : cells) {
            const long long low = *std::min_element(count.begin(), count.end());
            int best = -1; std::array<long long, 4> bk{};
            for (int c = 0; c < S; ++c) {
                if (used.count({r, w, c})) continue;
                const auto rs = pairs_rs.find({r, c}); const auto ws = pairs_ws.find({w, c});
                const std::array<long long, 4> key = {count[static_cast<std::size_t>(c)] - low, rs == pairs_rs.end() ? 0 : rs->second, ws == pairs_ws.end() ? 0 : ws->second, c};
                if (best < 0 || key < bk) { best = c; bk = key; }
            }
            if (best < 0) throw PyErr("ValueError", "min() arg is an empty sequence");
            used.insert({r, w, best});
            count[static_cast<std::size_t>(best)]++;
            pairs_rs[{r, best}]++;
            pairs_ws[{w, best}]++;
            triples.push_back({r, w, best});
        }
    }
    std::vector<Triple> dealt;
    for (const auto& t : triples) dealt.push_back({pr[static_cast<std::size_t>(t.r)], pw[static_cast<std::size_t>(t.w)], ps[static_cast<std::size_t>(t.s)]});
    rng.shuffle(dealt);
    return dealt;
}
inline std::vector<std::string> agent_ids(long long n) {
    const int width = std::max<int>(3, static_cast<int>(i2s(n).size()));
    std::vector<std::string> o;
    for (long long i = 1; i <= n; ++i) { std::string d = i2s(i); if (static_cast<int>(d.size()) < width) d = std::string(static_cast<std::size_t>(width) - d.size(), '0') + d; o.push_back("a" + d); }
    return o;
}
inline std::vector<long long> bracket_sizes(long long n) {
    std::vector<long long> s = {n};
    while (s.back() > 1) s.push_back((s.back() + 1) / 2);
    return s;
}
inline std::string sizes_line(long long n) { std::vector<std::string> v; for (long long x : bracket_sizes(n)) v.push_back(i2s(x)); return pyrt::join(v, " -> "); }

// ------------------------------------------------------------------ paths
inline std::string rdir(long long rnd) { return "r" + i2s(rnd); }
inline std::string spawn_out(const std::string& d, const std::string& aid) { return join({d, "r0", aid + ".md"}); }
inline std::string attack_out(const std::string& d, long long rnd, const std::string& mid, const std::string& attacker) { return join({d, rdir(rnd), mid + "." + attacker + ".attack.md"}); }
inline std::string defense_out(const std::string& d, long long rnd, const std::string& mid, const std::string& aid) { return join({d, rdir(rnd), mid + "." + aid + ".defense.md"}); }
inline std::string revised_out(const std::string& d, long long rnd, const std::string& mid, const std::string& aid) { return join({d, rdir(rnd), mid + "." + aid + ".solution.md"}); }
inline std::string verdict_out(const std::string& d, long long rnd, const std::string& mid) { return join({d, rdir(rnd), mid + ".verdict.json"}); }
inline std::string final_out(const std::string& d) { return join({d, "final.verdict.json"}); }
inline std::string prompt_path(const std::string& d, std::optional<long long> rnd, const std::string& job_id) { return join({d, "prompts", rnd ? rdir(*rnd) : std::string("final"), job_id + ".md"}); }

// ------------------------------------------------------------------ the bracket
inline Val copy_dict(const Val& v) {   // dict(v)
    if (v.t != Val::DICT) throw PyErr("TypeError", std::string("'") + type_name(v) + "' object is not iterable");
    Val o = Val::dict(); o.d->items = v.d->items; return o;
}
inline Val sdict(std::initializer_list<std::pair<std::string, Val>> kv) { Val o = Val::dict(); for (const auto& [k, v] : kv) o.d->set(k, v); return o; }
inline Val strs(const std::vector<std::string>& v) { Val o = Val::list(); for (const auto& s : v) o.l->push_back(Val::str(s)); return o; }

inline std::pair<Val, Val> pair_round(const Val& state, long long rnd, const std::vector<std::string>& alive) {
    const Val agents = item(state, "agents");
    PyRandom rng("arena-pair:" + pystr(item(state, "seed")) + ":" + i2s(rnd));
    std::vector<std::string> pool = alive;
    std::sort(pool.begin(), pool.end());
    rng.shuffle(pool);
    Val bye;
    if (pool.size() % 2) {
        double fewest = 0; bool first = true;
        for (const auto& a : pool) { const double b = item(item(agents, a), "byes").num(); if (first || b < fewest) fewest = b; first = false; }
        for (std::size_t k = 0; k < pool.size(); ++k)
            if (item(item(agents, pool[k]), "byes").num() == fewest) { bye = Val::str(pool[k]); pool.erase(pool.begin() + static_cast<long>(k)); break; }
    }
    const int width = std::max<int>(2, static_cast<int>(i2s(static_cast<long long>(pool.size() / 2)).size()));
    Val matches = Val::list();
    while (!pool.empty()) {
        std::string a = pool.front(); pool.erase(pool.begin());
        const Val mode = item(item(item(item(agents, a), "card"), "reasoning"), "id");
        std::size_t j = 0;
        for (std::size_t k = 0; k < pool.size(); ++k)
            if (!py_eq(item(item(item(item(agents, pool[k]), "card"), "reasoning"), "id"), mode)) { j = k; break; }
        std::string b = pool[j]; pool.erase(pool.begin() + static_cast<long>(j));
        if (rng.random() < 0.5) std::swap(a, b);
        std::string num = i2s(static_cast<long long>(matches.l->size() + 1));
        if (static_cast<int>(num.size()) < width) num = std::string(static_cast<std::size_t>(width) - num.size(), '0') + num;
        matches.l->push_back(sdict({{"id", Val::str("r" + i2s(rnd) + "-m" + num)}, {"a", Val::str(a)}, {"b", Val::str(b)},
                                    {"winner", Val()}, {"loser", Val()}, {"reason", Val()}, {"survived", Val::list()}, {"scores", Val()}, {"source", Val()}}));
    }
    return {matches, bye};
}
inline void open_round(const Val& state, std::vector<std::string> alive) {   // open the next round, or crown the champion
    std::sort(alive.begin(), alive.end());
    if (alive.size() == 1) {
        put(state, "champion", Val::str(alive[0]));
        if (truthy(get(state, "has_baseline")) && !truthy(get(state, "final"))) {
            PyRandom rng("arena-final:" + pystr(item(state, "seed")));
            const std::string x = rng.random() < 0.5 ? "champion" : "baseline";
            put(state, "final", sdict({{"X", Val::str(x)}, {"Y", Val::str(x == "champion" ? "baseline" : "champion")}, {"result", Val()}}));
        }
        return;
    }
    const long long rnd = static_cast<long long>(len(item(state, "rounds"))) + 1;
    auto [matches, bye] = pair_round(state, rnd, alive);
    item(state, "rounds").l->push_back(sdict({{"n", Val::integer(rnd)}, {"alive", strs(alive)}, {"matches", matches}, {"bye", bye}, {"closed", Val::boolean(false)}}));
}
inline Val new_state(long long n, const Val& seed, const Val& data, const std::string& run_dir0, long long wave, bool has_baseline) {
    const std::string run_dir = abspath(run_dir0);
    const auto ids = agent_ids(n);
    const auto dealt = deal(n, pystr(seed), data);
    Val agents = Val::dict();
    for (std::size_t k = 0; k < ids.size(); ++k) {
        const Triple& t = dealt[k];
        const Val card = sdict({{"reasoning", copy_dict(item(item(data, "reasoning"), static_cast<std::size_t>(t.r)))},
                                {"workflow", copy_dict(item(item(data, "workflows"), static_cast<std::size_t>(t.w)))},
                                {"strategy", copy_dict(item(item(data, "strategies"), static_cast<std::size_t>(t.s)))}});
        agents.d->set(ids[k], sdict({{"card", card}, {"solution", Val::str(spawn_out(run_dir, ids[k]))}, {"alive", Val::boolean(true)},
                                     {"eliminated_in", Val()}, {"eliminated_by", Val()}, {"byes", Val::integer(0)}}));
    }
    const Val state = sdict({{"version", Val::integer(1)}, {"created", Val::str(stamp("%Y-%m-%d %H:%M:%S"))}, {"seed", seed}, {"agents_n", Val::integer(n)},
                             {"cards_available", Val::integer(combo_count(data))}, {"wave", Val::integer(wave)}, {"dir", Val::str(run_dir)},
                             {"has_baseline", Val::boolean(has_baseline)}, {"agents", agents}, {"rounds", Val::list()}, {"champion", Val()}, {"final", Val()}});
    open_round(state, ids);
    return state;
}
inline std::optional<Val> current_round(const Val& state) {
    if (truthy(item(state, "champion")) || !truthy(item(state, "rounds"))) return std::nullopt;
    const Val rd = lst(item(state, "rounds")).back();
    if (truthy(item(rd, "closed"))) return std::nullopt;
    return rd;
}
inline std::string no_round(const Val& state) { return std::string("no round is open") + (truthy(item(state, "champion")) ? " (the arena has a champion)" : ""); }
inline std::string norm_agent(const Val& state, const Val& raw0) {
    const std::string raw = lower(strip(pystr(raw0)));
    const Val agents = item(state, "agents");
    if (agents.d->has(raw)) return raw;
    const std::string digits = lstrip_chars(raw, 'a');
    if (is_digits(digits)) {
        const std::string want = int_key(digits);
        for (const auto& aid : keys(agents)) if (int_key(cpslice(aid, 1)) == want) return aid;
    }
    throw ArenaError("no agent called '" + raw + "'");
}
inline std::pair<Val, Val> find_match(const Val& state, const std::string& mid) {
    const auto rd = current_round(state);
    if (!rd) throw ArenaError(no_round(state));
    const std::string raw = lower(strip(mid));
    for (const Val& m : lst(item(*rd, "matches"))) if (sitem(m, "id") == raw) return {*rd, m};
    const std::string tail = lstrip_chars(last_split(raw, "-m"), 'm');
    if (is_digits(tail)) {
        const std::string want = int_key(tail);
        for (const Val& m : lst(item(*rd, "matches"))) if (int_key(last_split(sitem(m, "id"), "-m")) == want) return {*rd, m};
    }
    throw ArenaError("no match '" + mid + "' in round " + pystr(item(*rd, "n")) + " (run pairings)");
}
inline std::pair<Val, Val> record(const Val& state, const std::string& mid, const Val& winner0, const Val& reason, const Val& survived, const Val& scores, const std::string& source) {
    const auto [rd, m] = find_match(state, mid);
    const std::string winner = norm_agent(state, winner0);
    const std::string a = sitem(m, "a"), b = sitem(m, "b");
    if (winner != a && winner != b) throw ArenaError(winner + " is not in " + sitem(m, "id") + " (" + a + " vs " + b + ")");
    const Val previous = item(m, "winner");
    put(m, "winner", Val::str(winner));
    put(m, "loser", Val::str(winner == a ? b : a));
    put(m, "reason", truthy(reason) ? reason : Val());
    Val sv = Val::list(); if (truthy(survived)) for (const Val& x : lst(survived)) sv.l->push_back(x);
    put(m, "survived", sv);
    put(m, "scores", scores);
    put(m, "source", Val::str(source));
    return {m, previous};
}
inline std::vector<std::string> advance(const Val& state) {
    const auto rd = current_round(state);
    if (!rd) throw ArenaError(no_round(state));
    std::vector<std::string> unrecorded;
    for (const Val& m : lst(item(*rd, "matches"))) if (!truthy(item(m, "winner"))) unrecorded.push_back(sitem(m, "id"));
    if (!unrecorded.empty())
        throw ArenaError("round " + pystr(item(*rd, "n")) + " has " + i2s(static_cast<long long>(unrecorded.size())) + " unrecorded match(es): " + pyrt::join(unrecorded, ", "));
    const Val agents = item(state, "agents");
    const std::string d = sitem(state, "dir");
    const long long n = as_int(item(*rd, "n"));
    std::vector<std::string> survivors;
    for (const Val& m : lst(item(*rd, "matches"))) {
        const Val loser = item(agents, sitem(m, "loser"));
        put(loser, "alive", Val::boolean(false));
        put(loser, "eliminated_in", item(*rd, "n"));
        put(loser, "eliminated_by", item(m, "winner"));
        for (const std::string& aid : {sitem(m, "a"), sitem(m, "b")}) {
            const std::string rev = revised_out(d, n, sitem(m, "id"), aid);
            if (has_output(rev)) put(item(agents, aid), "solution", Val::str(rev));
        }
        survivors.push_back(sitem(m, "winner"));
    }
    if (truthy(item(*rd, "bye"))) {
        const Val ag = item(agents, sitem(*rd, "bye"));
        const Val by = item(ag, "byes");
        put(ag, "byes", by.t == Val::FLOAT ? Val::flt(by.f + 1) : Val::integer(as_int(by) + 1));
        survivors.push_back(sitem(*rd, "bye"));
    }
    put(*rd, "closed", Val::boolean(true));
    open_round(state, survivors);
    return survivors;
}
inline long long rounds_played(const Val& state) { long long c = 0; for (const Val& rd : lst(item(state, "rounds"))) if (truthy(item(rd, "closed"))) c++; return c; }
inline std::vector<std::string> alive_ids(const Val& state) {
    std::vector<std::string> o;
    const Val agents = item(state, "agents");
    for (const auto& [a, v] : agents.d->items) if (truthy(item(v, "alive"))) o.push_back(a);
    std::sort(o.begin(), o.end());
    return o;
}

// ------------------------------------------------------------------ verdicts
inline Val weighted_total(const Val& scores) {   // 0 to 100 from five 0 to 10 scores, weighted as in rubric.md; None if any is missing
    if (scores.t != Val::DICT) return Val();
    double total = 0.0;
    for (const auto& [key, weight] : WEIGHTS) {
        const Val v = get(scores, key);
        if (v.t == Val::BOOL) return Val();
        double x;
        if (v.t == Val::INT || v.t == Val::FLOAT) x = v.num();
        else if (v.t == Val::STR) { try { x = pm::py_float(v.s); } catch (const PyErr& e) { if (e.type != "ValueError") throw; return Val(); } }
        else return Val();   // float(None), float([]): TypeError
        const double lo = x < 10.0 ? x : 10.0;   // min(10.0, v)
        const double cl = lo > 0.0 ? lo : 0.0;   // max(0.0, ...)
        total += cl * weight;
    }
    return Val::flt(round_n(total / 10.0, 2));
}
inline bool truthy_word(const Val& v) {
    if (v.t == Val::STR) { const std::string s = lower(strip(v.s)); return s == "true" || s == "yes" || s == "1"; }
    return truthy(v);
}
inline Val extract_json(const std::string& text0) {
    const std::string text = strip(text0);
    try { return loads(text); }
    catch (const PyErr& e) { if (e.type != "json.decoder.JSONDecodeError") throw; }
    const std::size_t i = text.find('{'), j = text.rfind('}');
    if (i != std::string::npos && j != std::string::npos && j > i) {
        try { return loads(text.substr(i, j - i + 1)); }
        catch (const PyErr& e) { if (e.type != "json.decoder.JSONDecodeError") throw; return Val(); }
    }
    return Val();
}
inline Val lower_keys(const Val& o) {   // {str(k).strip().lower(): v for k, v in o.items()}
    Val out = Val::dict();
    for (const auto& [k, v] : o.d->items) out.d->set(lower(strip(k)), v);
    return out;
}
inline bool word_in(const std::string& id, const std::string& text) {   // re.search(r"\b%s\b" % re.escape(id), text)
    const std::string pat = "\\b" + re::escape(id) + "\\b";
    return static_cast<bool>(re::search(pat, text));
}
struct Decision { std::string winner; Val totals; std::string note; };
inline Decision decide(const Val& verdict, const std::string& a, const std::string& b) {
    if (verdict.t != Val::DICT) throw ArenaError("verdict is not a JSON object");
    const Val raw_scores = truthy(get(verdict, "scores")) ? get(verdict, "scores") : Val::dict();
    const Val scores = raw_scores.t == Val::DICT ? lower_keys(raw_scores) : Val::dict();
    const Val sa = get(scores, a), sb = get(scores, b);
    const Val w = get(verdict, "winner");
    const std::string pick_raw = lower(strip(truthy(w) ? pystr(w) : std::string()));
    const bool in_a = word_in(a, pick_raw), in_b = word_in(b, pick_raw);
    const std::string pick = (in_a && !in_b) ? a : (in_b && !in_a) ? b : std::string();
    const Val ta = weighted_total(sa), tb = weighted_total(sb);
    Val totals = Val::dict(); totals.d->set(a, ta); totals.d->set(b, tb);
    if (ta.is_none() || tb.is_none()) {
        if (!pick.empty()) return {pick, totals, "scores incomplete, used the judge's pick"};
        throw ArenaError("verdict has neither complete scores nor a clear winner");
    }
    const bool fa = truthy_word(get(sa, "fatal")), fb = truthy_word(get(sb, "fatal"));
    std::string winner;
    if (fa != fb) winner = fa ? b : a;
    else if (ta.f != tb.f) winner = ta.f > tb.f ? a : b;
    else {
        const Val st0 = get(verdict, "standing");
        const Val st = st0.t == Val::DICT ? lower_keys(st0) : Val::dict();
        const Val la = get(st, a), lb = get(st, b);
        const long long na = la.t == Val::LIST ? static_cast<long long>(la.l->size()) : -1;   // -1: None
        const long long nb = lb.t == Val::LIST ? static_cast<long long>(lb.l->size()) : -1;
        const auto corr = [](const Val& s) { const Val c = s.d->at("correctness", Val::integer(0)); return c.t == Val::STR ? pm::py_float(c.s) : c.num(); };
        const double ca = corr(sa), cb = corr(sb);
        if (na >= 0 && nb >= 0 && na != nb) winner = na < nb ? a : b;
        else if (ca != cb) winner = ca > cb ? a : b;
        else if (!pick.empty()) winner = pick;
        else throw ArenaError("exact tie and the judge did not pick a winner");
    }
    std::string note;
    if (!pick.empty() && pick != winner) note = "judge picked " + pick + " but its own scores favour " + winner + ", so the scores win";
    return {winner, totals, note};
}
struct Result { std::string id, what, note; };
inline Val str_list(const Val& v, std::size_t n) {   // [str(x) for x in v][:n] if isinstance(v, list) else []
    Val o = Val::list();
    if (v.t != Val::LIST) return o;
    for (const Val& x : *v.l) { if (o.l->size() >= n) break; o.l->push_back(Val::str(pystr(x))); }
    return o;
}
inline std::vector<Result> collect(const Val& state) {
    const std::string d = sitem(state, "dir");
    std::vector<Result> results;
    if (truthy(item(state, "champion"))) {
        const Val fin = get(state, "final");
        if (!truthy(fin) || truthy(get(fin, "result"))) return results;
        const std::string path = final_out(d);
        if (!has_output(path)) return {{"final", "missing", ""}};
        Val v = extract_json(read_utf8(path, true));
        Decision dc;
        try {
            if (v.t == Val::DICT && get(v, "scores").t == Val::DICT) { v = copy_dict(v); v.d->set("scores", lower_keys(get(v, "scores"))); }
            dc = decide(v, "x", "y");
        } catch (const ArenaError& e) {
            os_replace(path, path + ".unreadable");
            return {{"final", "unreadable", e.what()}};
        }
        const Val better = item(fin, upper(dc.winner));
        const Val reason = get(v, "reason");
        put(fin, "result", sdict({{"better", better},
                                  {"champion_total", get(dc.totals, sitem(fin, "X") == "champion" ? "x" : "y")},
                                  {"baseline_total", get(dc.totals, sitem(fin, "X") == "baseline" ? "x" : "y")},
                                  {"reason", Val::str(cpslice(truthy(reason) ? pystr(reason) : std::string(), 0, 400))},
                                  {"fixed", str_list(truthy(get(v, "fixed")) ? get(v, "fixed") : Val::list(), 10)}}));
        return {{"final", "recorded", strip(pystr(better) + " is better. " + dc.note)}};
    }
    const auto rd = current_round(state);
    if (!rd) return results;
    const long long n = as_int(item(*rd, "n"));
    for (const Val& m : lst(item(*rd, "matches"))) {
        if (truthy(item(m, "winner"))) continue;
        const std::string mid = sitem(m, "id");
        const std::string path = verdict_out(d, n, mid);
        if (!has_output(path)) { results.push_back({mid, "missing", ""}); continue; }
        const Val v = extract_json(read_utf8(path, true));
        Decision dc;
        try { dc = decide(v, sitem(m, "a"), sitem(m, "b")); }
        catch (const ArenaError& e) { os_replace(path, path + ".unreadable"); results.push_back({mid, "unreadable", e.what()}); continue; }
        const Val reason = get(v, "reason");
        record(state, mid, Val::str(dc.winner), Val::str(cpslice(truthy(reason) ? pystr(reason) : std::string(), 0, 400)), str_list(get(v, "survived"), 10), dc.totals, "judge");
        const std::string loser = sitem(m, "loser");
        const std::string line = dc.winner + " beat " + loser + ", " + fmt_val(get(dc.totals, dc.winner)) + " to " + fmt_val(get(dc.totals, loser));
        results.push_back({mid, "recorded", line + (dc.note.empty() ? std::string() : ". " + dc.note)});
    }
    return results;
}

// ------------------------------------------------------------------ jobs and prompts
struct Job {
    std::string id, kind; std::optional<long long> round; std::string agent, opponent, match, a, b;
    std::vector<std::string> outputs; std::string prompt;
};
inline std::vector<Job> phase_jobs(const Val& state, const std::string& phase) {
    if (std::find(PHASES.begin(), PHASES.end(), phase) == PHASES.end()) throw ArenaError("unknown phase '" + phase + "' (one of: " + pyrt::join(PHASES, ", ") + ")");
    const std::string d = sitem(state, "dir");
    std::vector<Job> jobs;
    if (phase == "spawn") {
        auto ids = keys(item(state, "agents"));
        std::sort(ids.begin(), ids.end());
        for (const auto& aid : ids) { Job j; j.id = aid + ".spawn"; j.kind = "competitor"; j.round = 0; j.agent = aid; j.outputs = {spawn_out(d, aid)}; jobs.push_back(j); }
    } else if (phase == "final") {
        const Val fin = get(state, "final");
        if (truthy(item(state, "champion")) && truthy(fin) && !truthy(get(fin, "result"))) { Job j; j.id = "final.judge"; j.kind = "final"; j.outputs = {final_out(d)}; jobs.push_back(j); }
    } else {
        const auto rd = current_round(state);
        if (!rd) return {};
        const long long n = as_int(item(*rd, "n"));
        for (const Val& m : lst(item(*rd, "matches"))) {
            if (truthy(item(m, "winner"))) continue;
            const std::string a = sitem(m, "a"), b = sitem(m, "b"), mid = sitem(m, "id");
            if (phase == "attack" || phase == "defend") {
                for (const auto& [me, opp] : std::vector<std::pair<std::string, std::string>>{{a, b}, {b, a}}) {
                    Job j; j.round = n; j.agent = me; j.opponent = opp; j.match = mid;
                    if (phase == "attack") { j.id = mid + "." + me + ".attack"; j.kind = "attacker"; j.outputs = {attack_out(d, n, mid, me)}; }
                    else { j.id = mid + "." + me + ".defend"; j.kind = "defender"; j.outputs = {defense_out(d, n, mid, me), revised_out(d, n, mid, me)}; }
                    jobs.push_back(j);
                }
            } else {
                Job j; j.id = mid + ".judge"; j.kind = "judge"; j.round = n; j.match = mid; j.a = a; j.b = b; j.outputs = {verdict_out(d, n, mid)};
                jobs.push_back(j);
            }
        }
    }
    for (auto& j : jobs) j.prompt = prompt_path(d, j.round, j.id);
    return jobs;
}
inline bool job_done(const Job& j) { for (const auto& p : j.outputs) if (!has_output(p)) return false; return true; }
inline std::vector<Job> missing_jobs(const std::vector<Job>& jobs) { std::vector<Job> o; for (const auto& j : jobs) if (!job_done(j)) o.push_back(j); return o; }

inline std::map<std::string, std::string> load_templates() {
    const std::string text = read_utf8(skill_path("SKILL.md"));
    static const std::string pat = R"(<!-- template:([a-z]+) -->\s*```text\n(.*?)\n```\s*<!-- /template:\1 -->)";
    const re::Regex* rx = &re::compile(pat, re::S);
    std::map<std::string, std::string> found;
    for (const auto& m : rx->finditer(text)) found[m.group(1)] = m.group(2);
    std::vector<std::string> missing;
    for (const auto& t : TEMPLATES) if (!found.count(t)) missing.push_back(t);
    if (!missing.empty()) throw ArenaError("SKILL.md is missing prompt templates: " + pyrt::join(missing, ", "));
    return found;
}
using Values = std::map<std::string, std::string>;
inline std::string fill(const std::string& tmpl, const Values& values) {   // one pass: a task with {{braces}} of its own stays as written
    static const std::string pat = R"(\{\{(\w+)\}\})";
    const re::Regex* rx = &re::compile(pat);
    std::set<std::string> missing;
    for (const auto& m : rx->finditer(tmpl)) if (!values.count(m.group(1))) missing.insert(m.group(1));
    if (!missing.empty()) throw ArenaError("no value for placeholder(s): " + pyrt::join(std::vector<std::string>(missing.begin(), missing.end()), ", "));
    return rx->sub_fn([&](const re::Match& m) { return values.at(m.group(1)); }, tmpl);
}
inline std::string read_task(const Val& state) { return rstrip_nl(read_utf8(join({sitem(state, "dir"), "task.md"}))); }
inline void card_values(Values& v, const Val& card) {
    v["reasoning_name"] = sitem(item(card, "reasoning"), "name"); v["reasoning_how"] = sitem(item(card, "reasoning"), "how");
    v["workflow_name"] = sitem(item(card, "workflow"), "name"); v["workflow_how"] = sitem(item(card, "workflow"), "how");
    v["strategy_name"] = sitem(item(card, "strategy"), "name"); v["strategy_how"] = sitem(item(card, "strategy"), "how");
}
inline std::string render_job(const Val& state, const Job& job, const std::map<std::string, std::string>& templates, const std::string& task) {
    const std::string d = sitem(state, "dir");
    const Val agents = item(state, "agents");
    Values v = {{"task", task}, {"arena_dir", d}, {"rubric", join({d, "rubric.md"})}, {"n", sitem(state, "agents_n")}};
    const std::string rnd = job.round ? i2s(*job.round) : "None";
    if (job.kind == "competitor") {
        card_values(v, item(item(agents, job.agent), "card"));
        v["baseline_note"] = truthy(get(state, "has_baseline"))
            ? "The user already got an answer to this task and was NOT satisfied with it. It is at " + join({d, "baseline.md"}) +
              ". Read it first and work out exactly why it fell short. Then beat it. Do not just polish it: the winner of this arena is compared with that answer at the end."
            : "There is no earlier answer to beat. Start from the task.";
        v["agent"] = job.agent; v["out"] = job.outputs[0];
    } else if (job.kind == "attacker") {
        card_values(v, item(item(agents, job.agent), "card"));
        v["agent"] = job.agent; v["target"] = job.opponent; v["match"] = job.match; v["round"] = rnd;
        v["own_solution"] = sitem(item(agents, job.agent), "solution"); v["target_solution"] = sitem(item(agents, job.opponent), "solution");
        v["out"] = job.outputs[0];
    } else if (job.kind == "defender") {
        card_values(v, item(item(agents, job.agent), "card"));
        v["agent"] = job.agent; v["attacker"] = job.opponent; v["match"] = job.match; v["round"] = rnd;
        v["own_solution"] = sitem(item(agents, job.agent), "solution");
        v["attacks"] = attack_out(d, *job.round, job.match, job.opponent);
        v["defense_out"] = job.outputs[0]; v["solution_out"] = job.outputs[1];
    } else if (job.kind == "judge") {
        const long long n = *job.round;
        v["match"] = job.match; v["round"] = rnd; v["first"] = job.a; v["second"] = job.b;
        v["first_solution"] = revised_out(d, n, job.match, job.a); v["second_solution"] = revised_out(d, n, job.match, job.b);
        v["first_attacks"] = attack_out(d, n, job.match, job.b); v["second_attacks"] = attack_out(d, n, job.match, job.a);
        v["first_defense"] = defense_out(d, n, job.match, job.a); v["second_defense"] = defense_out(d, n, job.match, job.b);
        v["out"] = job.outputs[0];
    } else if (job.kind == "final") {
        v["rounds"] = i2s(rounds_played(state));
        v["x_solution"] = join({d, "final", "X.md"}); v["y_solution"] = join({d, "final", "Y.md"});
        v["out"] = job.outputs[0];
    }
    return fill(templates.at(job.kind), v);
}
inline std::vector<Job> write_prompts(const Val& state, const std::string& phase) {
    const auto jobs = phase_jobs(state, phase);
    if (jobs.empty()) return jobs;
    const auto templates = load_templates();
    const std::string task = read_task(state);
    for (const auto& j : jobs) {
        for (const auto& p : j.outputs) makedirs(dirname(p));
        makedirs(dirname(j.prompt));
        write_text(j.prompt, render_job(state, j, templates, task) + "\n");
    }
    const std::string d = sitem(state, "dir");
    if (phase == "spawn") for (const auto& aid : keys(item(state, "agents"))) makedirs(join({d, "scratch", aid}));
    if (phase == "final") {   // blind copies: the final judge sees X and Y, never which one is the champion
        const Val fin = item(state, "final");
        const std::string champ_sol = sitem(item(item(state, "agents"), sitem(state, "champion")), "solution");
        const std::string base = join({d, "baseline.md"});
        makedirs(join({d, "final"}));
        for (const std::string& label : std::vector<std::string>{"X", "Y"}) {
            const std::string src = sitem(fin, label) == "champion" ? champ_sol : base;
            write_text(join({d, "final", label + ".md"}), read_utf8(src, true));
        }
    }
    return jobs;
}
struct NextAction { std::string phase; std::vector<Job> left, jobs; };
inline NextAction next_action(const Val& state) {
    const auto spawn = phase_jobs(state, "spawn");
    auto left = missing_jobs(spawn);
    if (!left.empty()) return {"spawn", left, spawn};
    if (truthy(item(state, "champion"))) {
        const Val fin = get(state, "final");
        if (truthy(fin) && !truthy(get(fin, "result"))) {
            const auto jobs = phase_jobs(state, "final");
            left = missing_jobs(jobs);
            return left.empty() ? NextAction{"collect", {}, jobs} : NextAction{"final", left, jobs};
        }
        return {"done", {}, {}};
    }
    for (const std::string& phase : std::vector<std::string>{"attack", "defend", "judge"}) {
        const auto jobs = phase_jobs(state, phase);
        left = missing_jobs(jobs);
        if (!left.empty()) return {phase, left, jobs};
    }
    const auto rd = current_round(state);
    if (!rd) throw PyErr("TypeError", "'NoneType' object is not subscriptable");
    for (const Val& m : lst(item(*rd, "matches"))) if (!truthy(item(m, "winner"))) return {"collect", {}, {}};
    return {"advance", {}, {}};
}

// ------------------------------------------------------------------ plan
struct PlanRow { long long round, alive, matches, bye, calls, waves; };
inline std::vector<PlanRow> plan_rows(long long n, long long wave) {
    std::vector<PlanRow> rows;
    const auto sizes = bracket_sizes(n);
    for (std::size_t k = 0; k + 1 < sizes.size(); ++k) {
        const long long alive = sizes[k], m = alive / 2;
        rows.push_back({static_cast<long long>(k + 1), alive, m, alive % 2, CALLS_PER_MATCH * m, 2 * ceil_div(2 * m, wave) + ceil_div(m, wave)});
    }
    return rows;
}
inline Val plan_totals(long long n, long long wave) {
    const auto rows = plan_rows(n, wave);
    long long calls = n, waves = ceil_div(n, wave);
    Val rl = Val::list();
    for (const auto& r : rows) {
        calls += r.calls; waves += r.waves;
        rl.l->push_back(sdict({{"round", Val::integer(r.round)}, {"alive", Val::integer(r.alive)}, {"matches", Val::integer(r.matches)}, {"bye", Val::integer(r.bye)},
                               {"calls", Val::integer(r.calls)}, {"waves", Val::integer(r.waves)}}));
    }
    return sdict({{"agents", Val::integer(n)}, {"rounds", Val::integer(static_cast<long long>(rows.size()))}, {"calls", Val::integer(calls)}, {"waves", Val::integer(waves)}, {"rows", rl}});
}

// ------------------------------------------------------------------ state io
inline std::string run_dir_from(const Val& a) {
    if (!S(a, "dir").empty()) return abspath(S(a, "dir"));
    const std::string latest = join({ROOT, LATEST});
    if (is_file(latest)) return strip(read_utf8(latest));
    throw ArenaError("no arena here. Run init first, or pass --dir");
}
inline Val load_state(const Val& a) {
    const std::string d = run_dir_from(a);
    const std::string p = join({d, STATE_FILE});
    if (!is_file(p)) throw ArenaError("no " + std::string(STATE_FILE) + " in " + d);
    return loads(read_utf8(p));
}
inline DumpOpts indent1() { DumpOpts o; o.indent = 1; return o; }
inline void save_state(const Val& state) {
    const std::string p = join({sitem(state, "dir"), STATE_FILE});
    const std::string tmp = p + ".tmp";
    write_text(tmp, dumps(state, indent1()) + "\n");
    os_replace(tmp, p);
}
inline std::string card_line(const Val& card) {
    return sitem(item(card, "reasoning"), "name") + " + " + sitem(item(card, "workflow"), "name") + " + " + sitem(item(card, "strategy"), "name");
}

// ------------------------------------------------------------------ show / save (A.S.T.R.O AI's tool-less sub-agents)
inline std::pair<std::string, Job> find_job(const Val& state, const std::string& raw) {
    const std::string id = lower(strip(raw));
    for (const auto& phase : PHASES) for (const auto& j : phase_jobs(state, phase)) if (lower(j.id) == id) return {phase, j};
    throw ArenaError("no open job called '" + raw + "'. " + cmd({"next"}) + " shows what is open");
}
inline void run_files(const std::string& dir, const std::string& top, std::vector<std::string>& out) {
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> ents;
    for (std::filesystem::directory_iterator it(fspath(dir), ec), e; !ec && it != e; it.increment(ec)) ents.push_back(*it);
    for (const auto& ent : ents) {
        const auto u = ent.path().filename().u8string();
        const std::string name(u.begin(), u.end());
        const std::string p = path_join(dir, name);
        std::error_code e2;
        if (ent.is_directory(e2)) { if ((name != "prompts" && name != "scratch") || dir != top) run_files(p, top, out); }
        else if (ent.is_regular_file(e2)) out.push_back(p);
    }
}
inline std::string rstrip_one_nl(std::string s) { if (!s.empty() && s.back() == '\n') s.pop_back(); return s; }
inline std::string show_job(const Val& state, const std::string& raw) {
    const Job job = find_job(state, raw).second;
    const std::string brief = is_file(job.prompt) ? rstrip_one_nl(read_utf8(job.prompt, true)) : render_job(state, job, load_templates(), read_task(state));
    std::vector<std::string> files;
    run_files(sitem(state, "dir"), sitem(state, "dir"), files);
    std::vector<std::pair<std::size_t, std::string>> named;
    for (const auto& p : files) {
        if (std::find(job.outputs.begin(), job.outputs.end(), p) != job.outputs.end() || p == job.prompt) continue;
        const std::size_t at = brief.find(p);
        if (at == std::string::npos || !has_output(p)) continue;
        named.emplace_back(at, p);
    }
    std::stable_sort(named.begin(), named.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<std::string> parts = {brief, "", "---", "HOW TO ANSWER IN THIS SETTING", "",
        "You cannot open or write files here. The files this brief tells you to read are pasted below, each under its full path; treat them as those files. Where the brief says to keep notes in a scratch folder, keep them in your head. This replaces the brief's last step: do not reply with a DONE line, your reply is the file itself."};
    if (job.kind == "defender")
        parts.push_back("Reply in two parts, in this order, each starting with its marker on a line of its own:\n=== DEFENSE ===\n(what the brief says to write to " + job.outputs[0] +
                        ")\n=== SOLUTION ===\n(the complete revised solution the brief says to write to " + job.outputs[1] + ")\nNothing before the first marker. The orchestrator saves each part to its file.");
    else parts.push_back("Reply with exactly what the brief says to write to " + job.outputs[0] + ", and nothing else. The orchestrator saves your reply to that file.");
    for (const auto& np : named) {
        const std::string& p = np.second;
        const std::string t = read_utf8(p, true);
        const std::size_t n = cplen(t);
        parts.push_back("");
        parts.push_back("===== FILE " + p + " =====");
        parts.push_back(n > SHOW_LIMIT ? cpslice(t, 0, static_cast<long>(SHOW_LIMIT)) + "\n[... cut here: the file is " + i2s(static_cast<long long>(n)) + " characters, the first " + i2s(static_cast<long long>(SHOW_LIMIT)) + " are shown ...]"
                                       : rstrip_one_nl(t));
        parts.push_back("===== END " + basename(p) + " =====");
    }
    return pyrt::join(parts, "\n");
}
inline bool js_space(uint32_t c) {   // JavaScript \s
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}
inline std::string rstrip_ws(const std::string& s) { auto v = ucase_detail::u32(s); while (!v.empty() && js_space(v.back())) v.pop_back(); std::string o; for (uint32_t c : v) o += utf8(c); return o; }
inline std::string lstrip_nl(const std::string& s) { std::size_t k = 0; while (k < s.size() && s[k] == '\n') k++; return s.substr(k); }
inline std::string read_stdin_all() {
    std::string o; char b[65536]; std::size_t n;
    while ((n = std::fread(b, 1, sizeof b, stdin)) > 0) o.append(b, n);
    return o;
}
inline std::string universal_nl(const std::string& s) { std::string o; for (std::size_t k = 0; k < s.size(); ++k) { if (s[k] == '\r') { o += '\n'; if (k + 1 < s.size() && s[k + 1] == '\n') k++; } else o += s[k]; } return o; }
inline Job save_reply(const Val& state, const std::string& raw, const std::string& reply_file) {
    const Job job = find_job(state, raw).second;
    std::string text = reply_file == "-" ? universal_nl(decode_utf8_replace(read_stdin_all())) : read_utf8(reply_file, true);
    text = rstrip_ws(lstrip_nl(text));
    if (text.empty()) throw ArenaError("the reply is empty. Re-run that job");
    std::vector<std::string> bodies;
    if (job.kind == "defender") {
        static const std::string pat = R"(^[ \t]*={2,}[ \t]*DEFENSE[ \t]*={2,}[ \t]*\n([\s\S]*?)^[ \t]*={2,}[ \t]*(?:REVISED[ \t]+)?SOLUTION[ \t]*={2,}[ \t]*(?:\n|$)([\s\S]*)$)";
        const re::Regex* rx = &re::compile(pat, re::I | re::M);
        const re::Match m = rx->search(text);
        if (!m || strip(m.group(1)).empty() || strip(m.group(2)).empty())
            throw ArenaError("a defender reply needs both parts: a line \"=== DEFENSE ===\", the defense, a line \"=== SOLUTION ===\", the revised solution. Re-run that job with the brief from show");
        bodies = {rstrip_ws(m.group(1)), rstrip_ws(lstrip_nl(m.group(2)))};
    } else bodies = {text};
    for (std::size_t i = 0; i < job.outputs.size() && i < bodies.size(); ++i) { makedirs(dirname(job.outputs[i])); write_text(job.outputs[i], bodies[i] + "\n"); }
    return job;
}

// ------------------------------------------------------------------ commands
inline long long agents_arg(const Val& a) {
    if (B(a, "quick") && !get(a, "agents").is_none()) throw ArenaError("pass --quick or --agents, not both");
    if (B(a, "quick")) return QUICK_AGENTS;
    return get(a, "agents").is_none() ? DEFAULT_AGENTS : as_int(get(a, "agents"));
}
inline std::string rj(long long v, int w) { return rjust(i2s(v), w); }
inline std::string rj(const std::string& v, int w) { return rjust(v, w); }

inline int cmd_plan(const Val& a) {
    const long long n = agents_arg(a);
    if (n < 1) throw ArenaError("--agents must be at least 1");
    const long long wave = as_int(get(a, "wave"));
    if (wave == 0) throw ArenaError("--wave must not be 0");
    const Val t = plan_totals(n, wave);
    if (B(a, "json")) { print(dumps(t, indent1())); return 0; }
    print(i2s(n) + " agents, " + pystr(item(t, "rounds")) + " rounds, waves of " + i2s(wave));
    print("");
    print("  round  alive  matches  bye  sub-agent calls  waves");
    print("  spawn  " + rj(n, 5) + "  " + rj("-", 7) + "  " + rj("-", 3) + "  " + rj(n, 15) + "  " + rj(ceil_div(n, wave), 5));
    for (const Val& r : lst(item(t, "rows")))
        print("  " + rj(as_int(item(r, "round")), 5) + "  " + rj(as_int(item(r, "alive")), 5) + "  " + rj(as_int(item(r, "matches")), 7) + "  " +
              rj(as_int(item(r, "bye")) ? "yes" : "-", 3) + "  " + rj(as_int(item(r, "calls")), 15) + "  " + rj(as_int(item(r, "waves")), 5));
    print("  total                     " + rj(as_int(item(t, "calls")), 15) + "  " + rj(as_int(item(t, "waves")), 5));
    print("");
    print("  alive per round: " + sizes_line(n));
    print("  plus 1 call for the final check when there is a rejected answer to beat");
    return 0;
}
inline int cmd_init(const Val& a) {
    const long long n = agents_arg(a);
    const long long wave = as_int(get(a, "wave"));
    if (wave < 1) throw ArenaError("--wave must be at least 1");
    const Val data = load_strategies();
    if (n < 1 || n > combo_count(data)) throw ArenaError("--agents must be between 1 and " + i2s(combo_count(data)) + " (the number of distinct cards)");
    std::string task = !S(a, "task_file").empty() ? read_utf8(S(a, "task_file")) : S(a, "task");
    task = strip(task);
    if (task.empty()) throw ArenaError("the task is empty. Pass --task-file or --task");
    std::string baseline;
    const bool has_base_file = !S(a, "baseline_file").empty();
    if (has_base_file) {
        baseline = strip(read_utf8(S(a, "baseline_file")));
        if (baseline.empty()) throw ArenaError("the baseline file is empty");
    }
    Val seed = get(a, "seed");
    if (seed.is_none()) { std::random_device rd; seed = Val::integer(1 + static_cast<long long>(rd() % 999999U)); }   // SystemRandom().randrange(1, 1000000)
    const bool default_dir = S(a, "dir").empty();
    const std::string d = abspath(default_dir ? join({ROOT, "run-" + stamp("%Y%m%d-%H%M%S") + "-s" + pystr(seed)}) : S(a, "dir"));
    if (exists(join({d, STATE_FILE}))) throw ArenaError(d + " already has an arena in it. Pick another --dir");
    makedirs(d);
    write_text(join({d, "task.md"}), task + "\n");
    if (!baseline.empty()) write_text(join({d, "baseline.md"}), baseline + "\n");
    // the run gets its own frozen copy of the rubric, inside the working directory, so judges can read it
    write_text(join({d, "rubric.md"}), read_utf8(skill_path("rubric.md")));
    const Val state = new_state(n, seed, data, d, wave, !baseline.empty());
    save_state(state);
    if (default_dir) { makedirs(ROOT); write_text(join({ROOT, LATEST}), d + "\n"); }
    const Val t = plan_totals(n, wave);
    const long long extra = baseline.empty() ? 0 : 1;
    print("arena ready: " + d);
    print("seed " + pystr(seed) + ". " + i2s(n) + " agents, " + i2s(n) + " distinct cards dealt from " + i2s(combo_count(data)) + ", no repeats.");
    print(pystr(item(t, "rounds")) + " rounds (" + sizes_line(n) + "). " + i2s(as_int(item(t, "calls")) + extra) + " sub-agent calls in " + i2s(as_int(item(t, "waves")) + extra) +
          " waves of " + i2s(wave) + (extra ? ", including the final check" : "") + ".");
    print("next: " + cmd({"next"}));
    return 0;
}
inline int cmd_next(const Val& a) {
    const Val state = load_state(a);
    const NextAction na = next_action(state);
    const auto rd = current_round(state);
    const long long total_rounds = static_cast<long long>(bracket_sizes(as_int(item(state, "agents_n"))).size()) - 1;
    if (na.phase == "spawn") print("spawn: " + sitem(state, "agents_n") + " competitors, then " + i2s(total_rounds) + " round(s)");
    else if (rd) print("round " + sitem(*rd, "n") + " of " + i2s(total_rounds) + ", " + i2s(static_cast<long long>(len(item(*rd, "alive")))) + " alive");
    else if (truthy(item(state, "champion"))) print("champion: " + sitem(state, "champion") + " after " + i2s(rounds_played(state)) + " round(s)");
    if (std::find(PHASES.begin(), PHASES.end(), na.phase) != PHASES.end()) {
        print("NEXT: " + na.phase + ". " + i2s(static_cast<long long>(na.left.size())) + " of " + i2s(static_cast<long long>(na.jobs.size())) + " job(s) to run.");
        print("  1. " + cmd({"prompts", na.phase}));
        print("  2. launch the jobs it lists, one wave of " + sitem(state, "wave") + " at a time");
        print("  3. " + cmd({"next"}));
    } else if (na.phase == "collect") {
        print("NEXT: collect. Every verdict for this step is on disk.");
        print("  " + cmd({"collect"}));
    } else if (na.phase == "advance") {
        print("NEXT: advance. Every match in round " + sitem(*rd, "n") + " has a winner.");
        print("  " + cmd({"advance"}));
    } else print("DONE. " + cmd({"winner"}));
    return 0;
}
inline int cmd_prompts(const Val& a) {
    const Val state = load_state(a);
    const std::string phase = S(a, "phase");
    const auto jobs = write_prompts(state, phase);
    if (jobs.empty()) { print("nothing to run for " + phase + " right now. " + cmd({"next"})); return 0; }
    const auto left = missing_jobs(jobs);
    const long long w = as_int(item(state, "wave"));
    const auto rd = current_round(state);
    const std::string where = phase == "spawn" ? "round 0" : phase == "final" ? "final" : "round " + sitem(*rd, "n");
    std::vector<std::vector<Job>> waves;
    for (std::size_t i = 0; i < left.size(); i += static_cast<std::size_t>(w))
        waves.emplace_back(left.begin() + static_cast<long>(i), left.begin() + static_cast<long>(std::min(left.size(), i + static_cast<std::size_t>(w))));
    print(where + ", " + phase + ": " + i2s(static_cast<long long>(jobs.size())) + " job(s), " + i2s(static_cast<long long>(left.size())) + " still to run, " +
          i2s(static_cast<long long>(waves.size())) + " wave(s) of up to " + i2s(w));
    for (std::size_t k = 0; k < waves.size(); ++k) {
        print("wave " + i2s(static_cast<long long>(k + 1)));
        for (const auto& j : waves[k]) print("  " + ljust(j.id, 26) + " " + j.prompt);
    }
    if (!left.empty()) {
        print("");
        print("Each job is one task tool call, description \"arena <job id>\".");
        print("  A sub-agent that can read and write files (a CLI agent such as \"claude\" or \"opencode\"): prompt \"Read <prompt path> and follow it exactly. It is your whole brief.\"");
        print("  The built-in sub-agent cannot open files: run " + cmd({"show", "<job id>"}) + ", give its whole output as the prompt, write the reply to a file, then run " + cmd({"save", "<job id>", "<file>"}) + ".");
        print("A wave is a batch of jobs. Finish every job in it before the next wave.");
    }
    print("then: " + cmd({"next"}));
    return 0;
}
inline int cmd_show(const Val& a) { print(show_job(load_state(a), S(a, "job_id"))); return 0; }
inline int cmd_save(const Val& a) {
    const Val state = load_state(a);
    const Job job = save_reply(state, S(a, "job_id"), S(a, "reply_file"));
    print("saved " + job.id + ": " + pyrt::join(job.outputs, ", "));
    print("then: " + cmd({"next"}));
    return 0;
}
inline int cmd_check(const Val& a) {
    const Val state = load_state(a);
    const std::string phase = S(a, "phase");
    const auto jobs = phase_jobs(state, phase);
    const auto left = missing_jobs(jobs);
    print(phase + ": " + i2s(static_cast<long long>(jobs.size() - left.size())) + " of " + i2s(static_cast<long long>(jobs.size())) + " job(s) done");
    for (const auto& j : left) {
        std::vector<std::string> gone;
        for (const auto& p : j.outputs) if (!has_output(p)) gone.push_back(p);
        print("  missing " + ljust(j.id, 26) + " " + pyrt::join(gone, ", "));
    }
    return left.empty() ? 0 : 1;
}
inline int cmd_pairings(const Val& a) {
    const Val state = load_state(a);
    const auto rd = current_round(state);
    if (!rd) {
        if (truthy(item(state, "champion"))) { print("No matches left. " + sitem(state, "champion") + " is the last one standing."); return 0; }
        throw ArenaError("no round is open");
    }
    if (B(a, "json")) { print(dumps(*rd, indent1())); return 0; }
    const Val agents = item(state, "agents");
    const Val bye = item(*rd, "bye");
    print("round " + sitem(*rd, "n") + ": " + i2s(static_cast<long long>(len(item(*rd, "alive")))) + " alive, " + i2s(static_cast<long long>(len(item(*rd, "matches")))) +
          " match(es), bye: " + (truthy(bye) ? pystr(bye) : std::string("none")));
    for (const Val& m : lst(item(*rd, "matches"))) {
        const std::string ra = sitem(item(item(item(agents, sitem(m, "a")), "card"), "reasoning"), "name");
        const std::string rb = sitem(item(item(item(agents, sitem(m, "b")), "card"), "reasoning"), "name");
        const std::string res = truthy(item(m, "winner")) ? "winner " + sitem(m, "winner") : "open";
        print("  " + sitem(m, "id") + "  " + sitem(m, "a") + " vs " + sitem(m, "b") + "  (" + ra + " vs " + rb + ")  " + res);
    }
    if (truthy(bye)) print("  bye     " + pystr(bye) + " goes through without a match");
    return 0;
}
inline int cmd_record(const Val& a) {
    const Val state = load_state(a);
    const auto [m, previous] = record(state, S(a, "match_id"), get(a, "winner_id"), get(a, "reason"), get(a, "survived"), Val(), "manual");
    save_state(state);
    const std::string extra = truthy(previous) && pystr(previous) != sitem(m, "winner") ? " (was " + pystr(previous) + ")" : "";
    print(sitem(m, "id") + ": " + sitem(m, "winner") + " beat " + sitem(m, "loser") + extra);
    return 0;
}
inline int cmd_collect(const Val& a) {
    const Val state = load_state(a);
    const auto results = collect(state);
    save_state(state);
    if (results.empty()) { print("nothing to collect. " + cmd({"next"})); return 0; }
    long long bad = 0;
    for (const auto& r : results) {
        if (r.what != "recorded") bad++;
        print("  " + ljust(r.id, 10) + " " + ljust(r.what, 10) + " " + r.note);
    }
    if (const auto rd = current_round(state)) {
        long long done = 0;
        for (const Val& m : lst(item(*rd, "matches"))) if (truthy(item(m, "winner"))) done++;
        print("round " + sitem(*rd, "n") + ": " + i2s(done) + " of " + i2s(static_cast<long long>(len(item(*rd, "matches")))) + " match(es) recorded");
    }
    if (bad) { print(i2s(bad) + " verdict(s) missing or unreadable. Re-run those judges: " + cmd({"next"})); return 1; }
    print("then: " + cmd({"next"}));
    return 0;
}
inline int cmd_advance(const Val& a) {
    const Val state = load_state(a);
    const auto rd = current_round(state);
    const std::string closing = rd ? sitem(*rd, "n") : "None";
    const auto survivors = advance(state);
    save_state(state);
    print("round " + closing + " closed: " + i2s(static_cast<long long>(survivors.size())) + " through, " + i2s(static_cast<long long>(len(item(*rd, "matches")))) + " eliminated");
    if (truthy(item(state, "champion"))) print("CHAMPION: " + sitem(state, "champion") + ". " + cmd({"next"}));
    else {
        const Val nr = lst(item(state, "rounds")).back();
        const Val bye = item(nr, "bye");
        print("round " + sitem(nr, "n") + " open: " + i2s(static_cast<long long>(len(item(nr, "matches")))) + " match(es)" + (truthy(bye) ? ", bye " + pystr(bye) : std::string()) + ". " + cmd({"next"}));
    }
    return 0;
}
inline Val status_rows(const Val& state) {
    Val rows = Val::list();
    for (const Val& rd : lst(item(state, "rounds"))) {
        long long recorded = 0;
        for (const Val& m : lst(item(rd, "matches"))) if (truthy(item(m, "winner"))) recorded++;
        const long long nm = static_cast<long long>(len(item(rd, "matches")));
        rows.l->push_back(sdict({{"round", item(rd, "n")}, {"alive", Val::integer(static_cast<long long>(len(item(rd, "alive"))))}, {"matches", Val::integer(nm)},
                                 {"bye", item(rd, "bye")}, {"eliminated", Val::integer(recorded)},
                                 {"state", Val::str(truthy(item(rd, "closed")) ? std::string("closed") : "open, " + i2s(recorded) + " of " + i2s(nm) + " recorded")}}));
    }
    return rows;
}
inline int cmd_status(const Val& a) {
    const Val state = load_state(a);
    const Val rows = status_rows(state);
    const auto alive = alive_ids(state);
    const long long n = as_int(item(state, "agents_n")), na = static_cast<long long>(alive.size());
    if (B(a, "json")) {
        print(dumps(sdict({{"dir", item(state, "dir")}, {"seed", item(state, "seed")}, {"agents", item(state, "agents_n")}, {"alive", Val::integer(na)},
                           {"eliminated", Val::integer(n - na)}, {"champion", item(state, "champion")}, {"rounds", rows}}), indent1()));
        return 0;
    }
    print("arena " + sitem(state, "dir"));
    print("seed " + sitem(state, "seed") + ", " + sitem(state, "agents_n") + " agents, waves of " + sitem(state, "wave") + ", " + sitem(state, "cards_available") + " distinct cards available");
    print("  round  alive  matches  bye     eliminated  state");
    for (const Val& r : lst(rows))
        print("  " + rj(sitem(r, "round"), 5) + "  " + rj(sitem(r, "alive"), 5) + "  " + rj(sitem(r, "matches"), 7) + "  " +
              ljust(truthy(item(r, "bye")) ? sitem(r, "bye") : std::string("-"), 6) + "  " + rj(sitem(r, "eliminated"), 10) + "  " + sitem(r, "state"));
    print("now: " + i2s(na) + " alive, " + i2s(n - na) + " eliminated" + (truthy(item(state, "champion")) ? ", champion " + sitem(state, "champion") : std::string()));
    return 0;
}
inline Val winner_report(const Val& state) {
    const Val champ = item(state, "champion");
    if (!truthy(champ)) return Val();
    const std::string c = pystr(champ);
    const Val agent = item(item(state, "agents"), c);
    Val path = Val::list(), survived = Val::list();
    long long won = 0;
    for (const Val& rd : lst(item(state, "rounds"))) {
        if (py_eq(item(rd, "bye"), champ)) path.l->push_back(sdict({{"round", item(rd, "n")}, {"bye", Val::boolean(true)}}));
        for (const Val& m : lst(item(rd, "matches"))) {
            if (sitem(m, "a") != c && sitem(m, "b") != c) continue;
            const std::string opp = sitem(m, "a") == c ? sitem(m, "b") : sitem(m, "a");
            const Val sc = truthy(get(m, "scores")) ? get(m, "scores") : Val::dict();
            const Val sv = truthy(get(m, "survived")) ? get(m, "survived") : Val::list();
            path.l->push_back(sdict({{"round", item(rd, "n")}, {"bye", Val::boolean(false)}, {"beat", Val::str(opp)}, {"match", item(m, "id")},
                                     {"score", get(sc, c)}, {"opponent_score", get(sc, opp)}, {"reason", get(m, "reason")}, {"survived", sv}}));
            for (const Val& x : lst(sv)) survived.l->push_back(x);
            won++;
        }
    }
    const Val fin = truthy(get(state, "final")) ? get(state, "final") : Val::dict();
    return sdict({{"champion", champ}, {"card", item(agent, "card")}, {"card_line", Val::str(card_line(item(agent, "card")))}, {"solution", item(agent, "solution")},
                  {"rounds", Val::integer(rounds_played(state))}, {"agents", item(state, "agents_n")}, {"matches_won", Val::integer(won)}, {"path", path},
                  {"attacks_survived", survived}, {"final", get(fin, "result")}});
}
inline int cmd_winner(const Val& a) {
    const Val state = load_state(a);
    const Val rep = winner_report(state);
    if (rep.is_none()) {
        const auto rd = current_round(state);
        print("no winner yet: round " + (rd ? sitem(*rd, "n") : std::string("?")) + ", " + i2s(static_cast<long long>(alive_ids(state).size())) + " alive. " + cmd({"next"}));
        return 1;
    }
    if (B(a, "json")) { print(dumps(rep, indent1())); return 0; }
    print("CHAMPION  " + sitem(rep, "champion"));
    print("card      " + sitem(rep, "card_line"));
    print("solution  " + sitem(rep, "solution"));
    print("rounds    " + sitem(rep, "rounds") + " played, " + sitem(rep, "agents") + " agents in, 1 left, " + sitem(rep, "matches_won") + " match(es) won");
    for (const Val& p : lst(item(rep, "path"))) {
        if (truthy(item(p, "bye"))) { print("  round " + sitem(p, "round") + "  bye"); continue; }
        const Val reason = item(p, "reason");
        print("  round " + sitem(p, "round") + "  beat " + sitem(p, "beat") + ", " + fmt_val(item(p, "score")) + " to " + fmt_val(item(p, "opponent_score")) + ". " + (truthy(reason) ? pystr(reason) : std::string()));
        for (const Val& s : lst(item(p, "survived"))) print("           survived: " + pystr(s));
    }
    const Val fin = item(rep, "final");
    if (truthy(fin)) {
        const std::string verdict = sitem(fin, "better") == "champion" ? "the winner beats it" : "the answer you rejected scored higher. Say so";
        print("vs the answer you rejected: winner " + fmt_val(item(fin, "champion_total")) + ", rejected " + fmt_val(item(fin, "baseline_total")) + ", " + verdict + ". " + sitem(fin, "reason"));
    }
    return 0;
}
inline int cmd_card(const Val& a) {
    const Val state = load_state(a);
    const std::string aid = norm_agent(state, get(a, "agent_id"));
    const Val ag = item(item(state, "agents"), aid);
    const Val c = item(ag, "card");
    print(aid + "  " + (truthy(item(ag, "alive")) ? std::string("alive") : "eliminated in round " + sitem(ag, "eliminated_in") + " by " + sitem(ag, "eliminated_by")));
    for (const std::string& part : std::vector<std::string>{"reasoning", "workflow", "strategy"}) print("  " + ljust(part, 9) + " " + sitem(item(c, part), "name") + ". " + sitem(item(c, part), "how"));
    print("  solution  " + sitem(ag, "solution"));
    return 0;
}

inline int main(const std::vector<std::string>& argv) {
    Parser p("bracket", "Tournament state for /arena.");
    p.subcommands("cmd", true);
    const auto dir = [](Parser& s) { s.opt({"--dir"}).help("the arena run directory (default: the one in .arena/LATEST)"); };
    { Parser& s = p.sub("plan"); s.help_line = "rounds, sub-agent calls and waves for N agents";
      s.opt({"--agents"}).integer(); s.opt({"--quick"}).store_true(); s.opt({"--wave"}).integer().def(DEFAULT_WAVE); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("init"); s.help_line = "deal the cards and write arena.json"; dir(s);
      s.opt({"--agents"}).integer().help("number of competitors (default 100)");
      s.opt({"--quick"}).store_true().help("16 competitors");
      s.opt({"--seed"}).integer().help("fixes the cards and the pairings (default: random, recorded)");
      s.opt({"--task-file"}).help("the task, word for word, as every competitor will get it");
      s.opt({"--task"}).help("the task as a string, instead of --task-file");
      s.opt({"--baseline-file"}).help("the answer the user was not satisfied with");
      s.opt({"--wave"}).integer().def(DEFAULT_WAVE).help("parallel sub-agents per wave (default 10)"); }
    { Parser& s = p.sub("next"); s.help_line = "what to do now"; dir(s); }
    { Parser& s = p.sub("prompts"); s.help_line = "write the briefs for a phase and list the jobs left"; dir(s); s.pos("phase").choices(PHASES); }
    { Parser& s = p.sub("check"); s.help_line = "list the jobs whose outputs are missing"; dir(s); s.pos("phase").choices(PHASES); }
    { Parser& s = p.sub("show"); s.help_line = "one brief with the files it names pasted in (for a sub-agent that cannot read files)"; dir(s); s.pos("job_id"); }
    { Parser& s = p.sub("save"); s.help_line = "store a sub-agent's reply as that job's output file(s)"; dir(s); s.pos("job_id"); s.pos("reply_file"); }
    { Parser& s = p.sub("pairings"); s.help_line = "this round's matches, including the bye"; dir(s); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("record"); s.help_line = "record a match result by hand"; dir(s); s.pos("match_id"); s.pos("winner_id");
      s.opt({"--reason"}); s.opt({"--survived"}).append().help("an attack the winner survived (repeatable)"); }
    { Parser& s = p.sub("collect"); s.help_line = "record every verdict the judges wrote"; dir(s); }
    { Parser& s = p.sub("advance"); s.help_line = "close the round and pair the survivors"; dir(s); }
    { Parser& s = p.sub("status"); s.help_line = "alive and eliminated, per round"; dir(s); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("winner"); s.help_line = "the survivor and how it got there"; dir(s); s.opt({"--json"}).store_true(); }
    { Parser& s = p.sub("card"); s.help_line = "one competitor's strategy card"; dir(s); s.pos("agent_id"); }
    const Val a = parse_args(p, argv);
    const std::string c = S(a, "cmd");
    try {
        if (c == "plan") return cmd_plan(a);
        if (c == "init") return cmd_init(a);
        if (c == "next") return cmd_next(a);
        if (c == "prompts") return cmd_prompts(a);
        if (c == "show") return cmd_show(a);
        if (c == "save") return cmd_save(a);
        if (c == "check") return cmd_check(a);
        if (c == "pairings") return cmd_pairings(a);
        if (c == "record") return cmd_record(a);
        if (c == "collect") return cmd_collect(a);
        if (c == "advance") return cmd_advance(a);
        if (c == "status") return cmd_status(a);
        if (c == "winner") return cmd_winner(a);
        return cmd_card(a);
    } catch (const ArenaError& e) {
        flush();
        eprint(std::string("error: ") + e.what());
        return 2;
    }
}
PACKTOOL("arena/bracket", main);
}  // namespace packtools::arena
