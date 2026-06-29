// Classic CGI (RFC 3875-style) request runner.
//
// Each request re-spawns the program with CGI environment variables; its
// stdout (an optional header block + body) is captured until it exits. State
// is the program's own responsibility (it must persist to disk between spawns).
#pragma once

#include <string>

namespace hv {

struct CgiResponse {
    bool ok = false;
    int status = 200;
    std::string content_type;  // from a Content-Type: header (may be empty)
    std::string location;      // from a Location: header (redirect)
    std::string body;
    std::string error;         // populated when ok == false
};

// Run `program` (executed via `/bin/sh -c`, so arguments are allowed) for one
// request. `query` is the already-encoded query string. `body` is sent on the
// program's stdin (for POST); empty means a GET with no body.
CgiResponse run_cgi(const std::string& program, const std::string& method,
                    const std::string& path, const std::string& query,
                    const std::string& body = "");

}  // namespace hv
