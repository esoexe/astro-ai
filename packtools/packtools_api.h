// packtools_api.h - what the engine (workspace_server.cpp) uses from the built-in pack tools. The tools themselves are
// compiled on their own in packtools/packtools.cpp: two translation units keep the compiler's memory use in bounds
// (the engine and the pack tools together no longer fit a 2 GB build machine at -O2).
#pragma once
#include <string>
#include <cstddef>
#include <utility>
#include <vector>
namespace packtools {
// Network for helpers that read public data (news feeds, SEC, quotes). The engine plugs in its libcurl client
// (public addresses only, redirects, gzip); a build without one says so instead of failing silently.
struct HttpResp { long status = 0; std::string body, error; };
using HttpFn = HttpResp (*)(const std::string& url, const std::vector<std::string>& headers, long timeout_s, const std::string* post);   // post: nullptr = GET
HttpFn& http_hook();
// Fuller HTTP for helpers that check sites (master-build/sitecheck): any method, a byte cap, the final response's
// headers and URL after redirects, and the kind of failure. The engine plugs in libcurl here for https:// (plain
// http:// the helper speaks itself); unlike http_hook it may reach local addresses, since it checks the user's sites.
// body/has_body: a request body (duel posts JSON); follow_redirects = false returns a 30x as is, so a credential in the
// headers never travels to another host.
struct HttpExReq {
    std::string url, method = "GET"; std::vector<std::string> headers; double timeout_s = 10; std::size_t max_bytes = 0; bool insecure = false;
    std::string body; bool has_body = false, follow_redirects = true;
};
struct HttpExResp {
    long status = 0; std::string final_url, body, error;
    std::vector<std::pair<std::string, std::string>> headers;
    bool dns_fail = false, refused = false, timed_out = false, tls_untrusted = false;
};
using HttpExFn = HttpExResp (*)(const HttpExReq&);
HttpExFn& http_ex_hook();
// run a program and capture its stdout: {exit code, stdout}. The engine sets this on Windows (CreateProcess).
struct ExecResult { int code = -1; std::string out; };
using ExecFn = ExecResult (*)(const std::vector<std::string>& args);
ExecFn& exec_hook();
// What ${ASTRO_TOOL} expands to in a skill: the quoted engine path plus "tool".
std::string tool_command();
// Called first thing in main(): runs "astro-server tool ..." and returns its exit code, or -1 when this is not a tool run.
int maybe_tool(int argc, char** argv);
}  // namespace packtools
