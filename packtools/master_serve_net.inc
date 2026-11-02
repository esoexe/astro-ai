// master_serve_net.inc - the pieces of master-build/serve below the request handler: sockets, MIME types, URL and
// path handling, Range parsing. A port of serve.py (master-skills, MIT) and of the parts of Python's http.server,
// urllib.parse, posixpath and mimetypes that serve.py relies on, so requests resolve exactly as they did there.
#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#endif
#include <atomic>
#include <csignal>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace packtools::master_serve {
using namespace pyrt;
using namespace packtools::master;

// ---- sockets ------------------------------------------------------------------------------------------------------
#if defined(_WIN32)
using sock_t = SOCKET;
inline const sock_t BAD_SOCK = INVALID_SOCKET;
inline void sock_close(sock_t s) { closesocket(s); }
inline int sock_errno() { return WSAGetLastError(); }
inline std::string errno_text(int e) {   // str(OSError) on Windows
    char buf[512] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(e), 0, buf, sizeof buf, nullptr);
    std::string m = buf; while (!m.empty() && (m.back() == '\n' || m.back() == '\r' || m.back() == ' ')) m.pop_back();
    return "[WinError " + std::to_string(e) + "] " + m;
}
inline int poll_one(sock_t s, short events, int ms) { WSAPOLLFD p{}; p.fd = s; p.events = events; return WSAPoll(&p, 1, ms); }
inline void set_blocking(sock_t s, bool on) { u_long nb = on ? 0 : 1; ioctlsocket(s, FIONBIO, &nb); }
inline bool in_progress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
using sock_t = int;
inline const sock_t BAD_SOCK = -1;
inline void sock_close(sock_t s) { ::close(s); }
inline int sock_errno() { return errno; }
inline std::string errno_text(int e) { return "[Errno " + std::to_string(e) + "] " + std::strerror(e); }   // str(OSError)
inline int poll_one(sock_t s, short events, int ms) { pollfd p{}; p.fd = s; p.events = events; return ::poll(&p, 1, ms); }
inline void set_blocking(sock_t s, bool on) { const int f = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, on ? (f & ~O_NONBLOCK) : (f | O_NONBLOCK)); }
inline bool in_progress(int e) { return e == EINPROGRESS; }
#endif
#if defined(MSG_NOSIGNAL)
inline constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
inline constexpr int SEND_FLAGS = 0;
#endif
inline std::string gai_text(int rc) {   // str(socket.gaierror)
#if defined(_WIN32)
    return "[Errno " + std::to_string(rc) + "] getaddrinfo failed";
#else
    return "[Errno " + std::to_string(rc) + "] " + gai_strerror(rc);
#endif
}
inline bool send_all(sock_t s, const char* p, std::size_t n) {
    while (n > 0) {
        const int chunk = static_cast<int>(n > 65536 ? 65536 : n);
        const auto k = ::send(s, p, chunk, SEND_FLAGS);
        if (k <= 0) return false;
        p += k; n -= static_cast<std::size_t>(k);
    }
    return true;
}
inline bool send_all(sock_t s, const std::string& d) { return send_all(s, d.data(), d.size()); }

// _port_in_use: socket.create_connection((probe, port), timeout=0.25) succeeds
inline bool port_in_use(const std::string& host, int port) {
    const std::string probe = (host.empty() || host == "0.0.0.0") ? "127.0.0.1" : host;
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(probe.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return false;
    bool open = false;
    for (addrinfo* a = res; a && !open; a = a->ai_next) {
        const sock_t s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == BAD_SOCK) continue;
        set_blocking(s, false);
        if (::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) open = true;
        else if (in_progress(sock_errno()) && poll_one(s, POLLOUT, 250) > 0) {
            int err = 0; socklen_t len = sizeof err;
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == 0 && err == 0) open = true;
        }
        sock_close(s);
    }
    freeaddrinfo(res);
    return open;
}

// ThreadingHTTPServer((host, port)): an AF_INET socket with SO_REUSEADDR, bound and listening (queue 5).
// Returns the socket, or BAD_SOCK with err set to str(the OSError).
inline sock_t bind_listen(const std::string& host, int port, std::string& err, int& bound) {
    sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(static_cast<uint16_t>(port));
    if (host.empty()) sa.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) != 1) {
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
        if (rc != 0 || !res) { err = gai_text(rc); return BAD_SOCK; }
        sa.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    const sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCK) { err = errno_text(sock_errno()); return BAD_SOCK; }
    int one = 1;
#if defined(_WIN32)
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&one), sizeof one);
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0 || ::listen(s, 5) != 0) {
        err = errno_text(sock_errno()); sock_close(s); return BAD_SOCK;
    }
    sockaddr_in got{}; socklen_t len = sizeof got;
    getsockname(s, reinterpret_cast<sockaddr*>(&got), &len);
    bound = ntohs(got.sin_port);
    return s;
}

// lan_ip: the address a UDP socket "connected" to 10.255.255.255 would use (sends nothing)
inline std::string lan_ip() {
    const sock_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCK) return "";
    sockaddr_in sa{}; sa.sin_family = AF_INET; sa.sin_port = htons(1);
    inet_pton(AF_INET, "10.255.255.255", &sa.sin_addr);
    std::string ip;
    if (::connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa) == 0) {
        sockaddr_in me{}; socklen_t len = sizeof me;
        char buf[INET_ADDRSTRLEN] = {0};
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0 && inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf)) ip = buf;
    }
    sock_close(s);
    return starts(ip, "127.") ? "" : ip;
}

// ---- times --------------------------------------------------------------------------------------------------------
inline std::tm tm_of(std::time_t t, bool utc) {
    std::tm tm{};
#if defined(_WIN32)
    if (utc) gmtime_s(&tm, &t); else localtime_s(&tm, &t);
#else
    if (utc) gmtime_r(&t, &tm); else localtime_r(&t, &tm);
#endif
    return tm;
}
inline const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
inline std::string http_date(std::time_t t) {   // email.utils.formatdate(t, usegmt=True)
    static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const std::tm tm = tm_of(t, true);
    char b[64]; std::snprintf(b, sizeof b, "%s, %02d %s %04d %02d:%02d:%02d GMT", days[tm.tm_wday], tm.tm_mday, kMonths[tm.tm_mon], tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}
inline std::string log_date(std::time_t t) {   // BaseHTTPRequestHandler.log_date_time_string (local time)
    const std::tm tm = tm_of(t, false);
    char b[64]; std::snprintf(b, sizeof b, "%02d/%3s/%04d %02d:%02d:%02d", tm.tm_mday, kMonths[tm.tm_mon], tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}
inline bool file_mtime(const std::string& p, std::time_t& out) {
#if defined(_WIN32)
    struct _stat64 st{};
    if (_wstat64(fspath(p).c_str(), &st) != 0) return false;
#else
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return false;
#endif
    out = static_cast<std::time_t>(st.st_mtime);
    return true;
}

// ---- MIME types ---------------------------------------------------------------------------------------------------
// serve.py's TYPES table, then SimpleHTTPRequestHandler.extensions_map, then mimetypes. Python's mimetypes also reads
// the system's mime.types files, so its answers vary by machine; this uses the table built into the module.
inline const char* const kServeTypes =
    ".html text/html\n.htm text/html\n.css text/css\n.js text/javascript\n.mjs text/javascript\n.cjs text/javascript\n"
    ".json application/json\n.map application/json\n.webmanifest application/manifest+json\n.wasm application/wasm\n"
    ".svg image/svg+xml\n.png image/png\n.jpg image/jpeg\n.jpeg image/jpeg\n.gif image/gif\n.webp image/webp\n"
    ".avif image/avif\n.ico image/x-icon\n.woff font/woff\n.woff2 font/woff2\n.ttf font/ttf\n.otf font/otf\n"
    ".mp4 video/mp4\n.m4v video/mp4\n.webm video/webm\n.mov video/quicktime\n.mp3 audio/mpeg\n.m4a audio/mp4\n"
    ".wav audio/wav\n.ogg audio/ogg\n.vtt text/vtt\n.txt text/plain\n.xml application/xml\n.pdf application/pdf\n"
    ".zip application/zip\n";
inline const char* const kStdTypes =
    ".js text/javascript\n.mjs text/javascript\n.json application/json\n.webmanifest application/manifest+json\n"
    ".doc application/msword\n.dot application/msword\n.wiz application/msword\n.nq application/n-quads\n"
    ".nt application/n-triples\n.bin application/octet-stream\n.a application/octet-stream\n.dll application/octet-stream\n"
    ".exe application/octet-stream\n.o application/octet-stream\n.obj application/octet-stream\n.so application/octet-stream\n"
    ".oda application/oda\n.ogx application/ogg\n.pdf application/pdf\n.p7c application/pkcs7-mime\n.ps application/postscript\n"
    ".ai application/postscript\n.eps application/postscript\n.trig application/trig\n.m3u application/vnd.apple.mpegurl\n"
    ".m3u8 application/vnd.apple.mpegurl\n.xls application/vnd.ms-excel\n.xlb application/vnd.ms-excel\n"
    ".eot application/vnd.ms-fontobject\n.ppt application/vnd.ms-powerpoint\n.pot application/vnd.ms-powerpoint\n"
    ".ppa application/vnd.ms-powerpoint\n.pps application/vnd.ms-powerpoint\n.pwz application/vnd.ms-powerpoint\n"
    ".odg application/vnd.oasis.opendocument.graphics\n.odp application/vnd.oasis.opendocument.presentation\n"
    ".ods application/vnd.oasis.opendocument.spreadsheet\n.odt application/vnd.oasis.opendocument.text\n"
    ".pptx application/vnd.openxmlformats-officedocument.presentationml.presentation\n"
    ".xlsx application/vnd.openxmlformats-officedocument.spreadsheetml.sheet\n"
    ".docx application/vnd.openxmlformats-officedocument.wordprocessingml.document\n.rar application/vnd.rar\n"
    ".wasm application/wasm\n.7z application/x-7z-compressed\n.bcpio application/x-bcpio\n.cpio application/x-cpio\n"
    ".csh application/x-csh\n.deb application/x-debian-package\n.dvi application/x-dvi\n.gtar application/x-gtar\n"
    ".hdf application/x-hdf\n.h5 application/x-hdf5\n.latex application/x-latex\n.mif application/x-mif\n"
    ".cdf application/x-netcdf\n.nc application/x-netcdf\n.p12 application/x-pkcs12\n.php application/x-httpd-php\n"
    ".pfx application/x-pkcs12\n.ram application/x-pn-realaudio\n.pyc application/x-python-code\n"
    ".pyo application/x-python-code\n.rpm application/x-rpm\n.sh application/x-sh\n.shar application/x-shar\n"
    ".swf application/x-shockwave-flash\n.sv4cpio application/x-sv4cpio\n.sv4crc application/x-sv4crc\n"
    ".tar application/x-tar\n.tcl application/x-tcl\n.tex application/x-tex\n.texi application/x-texinfo\n"
    ".texinfo application/x-texinfo\n.roff application/x-troff\n.t application/x-troff\n.tr application/x-troff\n"
    ".man application/x-troff-man\n.me application/x-troff-me\n.ms application/x-troff-ms\n.ustar application/x-ustar\n"
    ".src application/x-wais-source\n.xsl application/xml\n.rdf application/xml\n.wsdl application/xml\n"
    ".xpdl application/xml\n.yaml application/yaml\n.yml application/yaml\n.zip application/zip\n.3gp audio/3gpp\n"
    ".3gpp audio/3gpp\n.3g2 audio/3gpp2\n.3gpp2 audio/3gpp2\n.aac audio/aac\n.adts audio/aac\n.loas audio/aac\n"
    ".ass audio/aac\n.au audio/basic\n.snd audio/basic\n.flac audio/flac\n.mka audio/matroska\n.m4a audio/mp4\n"
    ".mp3 audio/mpeg\n.mp2 audio/mpeg\n.ogg audio/ogg\n.opus audio/opus\n.aif audio/x-aiff\n.aifc audio/x-aiff\n"
    ".aiff audio/x-aiff\n.ra audio/x-pn-realaudio\n.wav audio/vnd.wave\n.otf font/otf\n.ttf font/ttf\n.weba audio/webm\n"
    ".woff font/woff\n.woff2 font/woff2\n.avif image/avif\n.bmp image/bmp\n.emf image/emf\n.fits image/fits\n"
    ".g3 image/g3fax\n.gif image/gif\n.ief image/ief\n.jp2 image/jp2\n.jpg image/jpeg\n.jpe image/jpeg\n"
    ".jpeg image/jpeg\n.jpm image/jpm\n.jpx image/jpx\n.heic image/heic\n.heif image/heif\n.png image/png\n"
    ".svg image/svg+xml\n.t38 image/t38\n.tiff image/tiff\n.tif image/tiff\n.tfx image/tiff-fx\n"
    ".ico image/vnd.microsoft.icon\n.webp image/webp\n.wmf image/wmf\n.ras image/x-cmu-raster\n"
    ".pnm image/x-portable-anymap\n.pbm image/x-portable-bitmap\n.pgm image/x-portable-graymap\n"
    ".ppm image/x-portable-pixmap\n.rgb image/x-rgb\n.xbm image/x-xbitmap\n.xpm image/x-xpixmap\n"
    ".xwd image/x-xwindowdump\n.eml message/rfc822\n.mht message/rfc822\n.mhtml message/rfc822\n.nws message/rfc822\n"
    ".gltf model/gltf+json\n.glb model/gltf-binary\n.stl model/stl\n.css text/css\n.csv text/csv\n.html text/html\n"
    ".htm text/html\n.md text/markdown\n.markdown text/markdown\n.n3 text/n3\n.txt text/plain\n.bat text/plain\n"
    ".c text/plain\n.h text/plain\n.ksh text/plain\n.pl text/plain\n.srt text/plain\n.rtx text/richtext\n.rtf text/rtf\n"
    ".tsv text/tab-separated-values\n.vtt text/vtt\n.py text/x-python\n.rst text/x-rst\n.etx text/x-setext\n"
    ".sgm text/x-sgml\n.sgml text/x-sgml\n.vcf text/x-vcard\n.xml text/xml\n.mkv video/matroska\n"
    ".mk3d video/matroska-3d\n.mp4 video/mp4\n.mpeg video/mpeg\n.m1v video/mpeg\n.mpa video/mpeg\n.mpe video/mpeg\n"
    ".mpg video/mpeg\n.ogv video/ogg\n.mov video/quicktime\n.qt video/quicktime\n.webm video/webm\n.avi video/vnd.avi\n"
    ".m4v video/x-m4v\n.wmv video/x-ms-wmv\n.movie video/x-sgi-movie\n";
inline std::map<std::string, std::string> type_table(const char* text) {
    std::map<std::string, std::string> m;
    for (const auto& line : split(text, "\n")) {
        const auto sp = line.find(' ');
        if (sp != std::string::npos) m.emplace(line.substr(0, sp), line.substr(sp + 1));
    }
    return m;
}
inline std::pair<std::string, std::string> posix_splitext(const std::string& p) {
    const std::size_t sep = p.rfind('/'), dot = p.rfind('.');
    const std::size_t base = sep == std::string::npos ? 0 : sep + 1;
    if (dot != std::string::npos && dot >= base)
        for (std::size_t k = base; k < dot; ++k)
            if (p[k] != '.') return {p.substr(0, dot), p.substr(dot)};
    return {p, ""};
}
inline std::string guess_type(const std::string& path) {
    static const auto serve_types = type_table(kServeTypes), std_types = type_table(kStdTypes);
    const std::string ext = lower(splitext(path).second);
    if (auto it = serve_types.find(ext); it != serve_types.end()) return it->second;
    // SimpleHTTPRequestHandler.guess_type
    static const std::map<std::string, std::string> ext_map = {{".gz", "application/gzip"}, {".Z", "application/octet-stream"}, {".bz2", "application/x-bzip2"}, {".xz", "application/x-xz"}};
    const std::string raw = posix_splitext(path).second;
    if (auto it = ext_map.find(raw); it != ext_map.end()) return it->second;
    if (auto it = ext_map.find(lower(raw)); it != ext_map.end()) return it->second;
    // mimetypes.guess_file_type: suffix_map, then strip one encoding suffix, then the types table
    static const std::map<std::string, std::string> suffix_map = {{".svgz", ".svg.gz"}, {".tgz", ".tar.gz"}, {".taz", ".tar.gz"}, {".tz", ".tar.gz"}, {".tbz2", ".tar.bz2"}, {".txz", ".tar.xz"}};
    static const std::map<std::string, std::string> enc_map = {{".gz", "gzip"}, {".Z", "compress"}, {".bz2", "bzip2"}, {".xz", "xz"}, {".br", "br"}};
    auto [base, e] = posix_splitext(path);
    for (;;) {
        auto it = suffix_map.find(e);
        if (it == suffix_map.end()) it = suffix_map.find(lower(e));
        if (it == suffix_map.end()) break;
        std::tie(base, e) = posix_splitext(base + it->second);
    }
    if (enc_map.count(e) || enc_map.count(lower(e))) std::tie(base, e) = posix_splitext(base);
    if (auto it = std_types.find(e); it != std_types.end()) return it->second;
    if (auto it = std_types.find(lower(e)); it != std_types.end()) return it->second;
    return "application/octet-stream";
}

// ---- URLs and paths -----------------------------------------------------------------------------------------------
inline std::string latin1_to_utf8(const std::string& b) {   // str(raw, 'iso-8859-1')
    std::string o;
    for (unsigned char c : b) {
        if (c < 0x80) o += static_cast<char>(c);
        else { o += static_cast<char>(0xC0 | (c >> 6)); o += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return o;
}
struct Split { std::string scheme, netloc, path, query, fragment; };
inline Split urlsplit(std::string url) {   // urllib.parse.urlsplit (3.13)
    std::size_t a = 0;
    while (a < url.size() && static_cast<unsigned char>(url[a]) <= ' ') a++;
    url = url.substr(a);
    std::string clean;
    for (char c : url)
        if (c != '\t' && c != '\r' && c != '\n') clean += c;
    url = clean;
    Split r;
    const std::size_t i = url.find(':');
    if (i != std::string::npos && i > 0 && std::isalpha(static_cast<unsigned char>(url[0]))) {
        bool ok = true;
        for (std::size_t k = 0; k < i; ++k) {
            const unsigned char c = static_cast<unsigned char>(url[k]);
            if (!(std::isalnum(c) || c == '+' || c == '-' || c == '.')) { ok = false; break; }
        }
        if (ok) { r.scheme = lower(url.substr(0, i)); url = url.substr(i + 1); }
    }
    if (url.compare(0, 2, "//") == 0) {
        const std::size_t end = url.find_first_of("/?#", 2);
        r.netloc = url.substr(2, end == std::string::npos ? std::string::npos : end - 2);
        url = end == std::string::npos ? "" : url.substr(end);
        const bool lb = r.netloc.find('[') != std::string::npos, rb = r.netloc.find(']') != std::string::npos;
        if (lb != rb) throw PyErr("ValueError", "Invalid IPv6 URL");
    }
    if (const auto h = url.find('#'); h != std::string::npos) { r.fragment = url.substr(h + 1); url = url.substr(0, h); }
    if (const auto q = url.find('?'); q != std::string::npos) { r.query = url.substr(q + 1); url = url.substr(0, q); }
    r.path = url;
    return r;
}
inline std::string urlunsplit_path(std::string url, const std::string& query) {   // urlunsplit(("", "", path, query, ""))
    if (url.compare(0, 2, "//") == 0) url = "//" + url;
    if (!query.empty()) url += "?" + query;
    return url;
}
inline std::string normpath(const std::string& path) {   // posixpath.normpath
    if (path.empty()) return ".";
    int initial = path[0] == '/' ? 1 : 0;
    if (initial && path.compare(0, 2, "//") == 0 && path.compare(0, 3, "///") != 0) initial = 2;
    std::vector<std::string> comps;
    for (const auto& c : split(path, "/")) {
        if (c.empty() || c == ".") continue;
        if (c != ".." || (!initial && comps.empty()) || (!comps.empty() && comps.back() == "..")) comps.push_back(c);
        else if (!comps.empty()) comps.pop_back();
    }
    const std::string out = std::string(static_cast<std::size_t>(initial), '/') + join(comps, "/");
    return out.empty() ? "." : out;
}
inline int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// a decoded %-run is valid UTF-8, also allowing encoded surrogates (errors='surrogatepass')
inline bool utf8_surrogatepass_ok(const std::string& b) {
    std::size_t i = 0;
    while (i < b.size()) {
        const unsigned char c = static_cast<unsigned char>(b[i]);
        const std::size_t n = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : 9;
        if (n == 9 || i + n >= b.size() + (n == 0 ? 1 : 0)) return false;
        for (std::size_t k = 1; k <= n; ++k) {
            const unsigned char d = static_cast<unsigned char>(b[i + k]);
            unsigned char lo = 0x80, hi = 0xBF;   /* 0xED keeps 0xA0..0xBF: surrogates pass */
            if (k == 1 && c == 0xE0) lo = 0xA0;
            if (k == 1 && c == 0xF0) lo = 0x90;
            if (k == 1 && c == 0xF4) hi = 0x8F;
            if (d < lo || d > hi) return false;
        }
        i += n + 1;
    }
    return true;
}
// urllib.parse.unquote(path, errors='surrogatepass'), falling back to errors='replace'
inline std::string unquote(const std::string& s) {
    if (s.find('%') == std::string::npos) return s;
    for (int pass = 0; pass < 2; ++pass) {
        std::string out, run; bool bad = false;
        auto flush = [&] {
            if (run.empty()) return;
            if (pass == 0 && !utf8_surrogatepass_ok(run)) bad = true;
            out += pass == 0 ? run : decode_utf8_replace(run);
            run.clear();
        };
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
                run += static_cast<char>(hexval(s[i + 1]) * 16 + hexval(s[i + 2])); i += 2;
            } else { flush(); out += s[i]; }
        }
        flush();
        if (!bad) return out;
    }
    return s;
}
// SimpleHTTPRequestHandler.translate_path
inline std::string translate_path(const std::string& directory, std::string path) {
    path = path.substr(0, path.find('?'));
    path = path.substr(0, path.find('#'));
    const std::string rs = [&] { auto v = cps(path); while (!v.empty() && py_space(cp_of(v.back()))) v.pop_back(); std::string o; for (const auto& c : v) o += c; return o; }();
    const bool trailing = !rs.empty() && rs.back() == '/';
    path = normpath(unquote(path));
    std::string out = directory;
    for (const auto& w : split(path, "/")) {
        if (w.empty() || w == "." || w == "..") continue;
#if defined(_WIN32)
        if (w.find('\\') != std::string::npos || w.find(':') != std::string::npos) continue;   // ntpath.dirname(word)
#endif
        out = (!out.empty() && (out.back() == '/' || out.back() == '\\')) ? out + w : out + "/" + w;
    }
    if (trailing) out += "/";
    return out;
}
inline std::string path_join2(const std::string& a, const std::string& b) {   // os.path.join(a, b), b relative
    return (!a.empty() && (a.back() == '/' || a.back() == '\\')) ? a + b : a + "/" + b;
}

// ---- Range --------------------------------------------------------------------------------------------------------
// int(s) for a header field: whitespace, a sign, ASCII digits with single underscores between them; saturates
inline bool header_int(const std::string& text, long long& out) {
    const std::string s = strip(text);
    std::size_t i = 0; bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    if (i >= s.size()) return false;
    long long v = 0; bool prev_digit = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') { v = v > 400000000000000000LL ? 4000000000000000000LL : v * 10 + (c - '0'); prev_digit = true; }
        else if (c == '_' && prev_digit && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') prev_digit = false;
        else return false;
    }
    out = neg ? -v : v;
    return true;
}
enum class RangeKind { Full, Unsatisfiable, Part };
struct Range { RangeKind kind = RangeKind::Full; long long start = 0, end = 0; };
inline Range parse_range(const std::string& header, long long size) {   // serve.parse_range
    Range r;
    const std::string h = strip(header);
    if (lower(h).compare(0, 6, "bytes=") != 0 || h.find(',') != std::string::npos) return r;
    const std::string spec = strip(h.substr(6));
    const auto dash = spec.find('-');
    if (dash == std::string::npos) return r;
    const std::string first = spec.substr(0, dash), last = spec.substr(dash + 1);
    long long start = 0, end = 0;
    if (first.empty()) {
        long long n = 0;
        if (!header_int(last, n)) return r;
        if (n <= 0) { r.kind = RangeKind::Unsatisfiable; return r; }
        start = size - n > 0 ? size - n : 0; end = size - 1;
    } else {
        if (!header_int(first, start)) return r;
        if (last.empty()) end = size - 1;
        else if (!header_int(last, end)) return r;
        end = end < size - 1 ? end : size - 1;
    }
    if (start < 0 || start > end || start >= size) { r.kind = RangeKind::Unsatisfiable; return r; }
    r.kind = RangeKind::Part; r.start = start; r.end = end;
    return r;
}
}  // namespace packtools::master_serve
