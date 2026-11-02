// CPython's random.Random for the seed tools: Mersenne Twister, str seeds through SHA-512 (seed version 2), shuffle by
// _randbelow. The same code as the social group's arena port (a separate translation unit, so it is repeated here).
#include <array>
#include <cstdint>
namespace packtools::seed {
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
}  // namespace packtools::seed
