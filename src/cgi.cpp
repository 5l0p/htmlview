#include "cgi.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace hv {

namespace {

void set_cgi_env(const std::string& method, const std::string& path,
                 const std::string& query, size_t content_length) {
    setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    setenv("SERVER_PROTOCOL", "HTTP/1.0", 1);
    setenv("SERVER_SOFTWARE", "htmlview", 1);
    setenv("REQUEST_METHOD", method.c_str(), 1);
    setenv("SCRIPT_NAME", path.c_str(), 1);
    setenv("PATH_INFO", path.c_str(), 1);
    setenv("QUERY_STRING", query.c_str(), 1);
    std::string uri = query.empty() ? path : path + "?" + query;
    setenv("REQUEST_URI", uri.c_str(), 1);
    setenv("CONTENT_LENGTH", std::to_string(content_length).c_str(), 1);
    if (content_length > 0)
        setenv("CONTENT_TYPE", "application/x-www-form-urlencoded", 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Split a CGI response into a header block and body. Lenient: if the output
// doesn't begin with a header-looking line, the whole thing is the body.
void parse_response(const std::string& out, CgiResponse& r) {
    // Find the header/body separator (blank line).
    size_t sep = std::string::npos, skip = 0;
    if (size_t p = out.find("\r\n\r\n"); p != std::string::npos) { sep = p; skip = 4; }
    if (size_t p = out.find("\n\n"); p != std::string::npos && (sep == std::string::npos || p < sep)) {
        sep = p; skip = 2;
    }

    // Decide whether the leading block is actually headers: first line must
    // look like "Field: value" and not start a tag.
    bool has_headers = false;
    if (sep != std::string::npos) {
        size_t nl = out.find('\n');
        std::string first = out.substr(0, nl == std::string::npos ? sep : std::min(nl, sep));
        size_t colon = first.find(':');
        if (colon != std::string::npos && first.find('<') == std::string::npos) {
            // Field name must be token-ish.
            has_headers = !first.empty() && first[0] != '<';
        }
    }

    if (!has_headers) {
        r.body = out;
        return;
    }

    std::string header_block = out.substr(0, sep);
    r.body = out.substr(sep + skip);

    size_t i = 0;
    while (i < header_block.size()) {
        size_t eol = header_block.find('\n', i);
        std::string line = trim(header_block.substr(i, eol - i));
        i = (eol == std::string::npos) ? header_block.size() : eol + 1;
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, colon)));
        std::string val = trim(line.substr(colon + 1));
        if (key == "content-type") r.content_type = val;
        else if (key == "location") { r.location = val; if (r.status == 200) r.status = 302; }
        else if (key == "status") r.status = std::atoi(val.c_str());
    }
}

}  // namespace

CgiResponse run_cgi(const std::string& program, const std::string& method,
                    const std::string& path, const std::string& query,
                    const std::string& body) {
    CgiResponse r;

    int outp[2];
    if (pipe(outp) != 0) { r.error = "pipe() failed"; return r; }
    bool use_stdin = !body.empty();
    int inp[2] = {-1, -1};
    if (use_stdin && pipe(inp) != 0) {
        close(outp[0]); close(outp[1]);
        r.error = "pipe() failed";
        return r;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(outp[0]); close(outp[1]);
        if (use_stdin) { close(inp[0]); close(inp[1]); }
        r.error = "fork() failed";
        return r;
    }

    if (pid == 0) {  // child
        dup2(outp[1], STDOUT_FILENO);
        close(outp[0]); close(outp[1]);
        if (use_stdin) {
            dup2(inp[0], STDIN_FILENO);
            close(inp[0]); close(inp[1]);
        }
        set_cgi_env(method, path, query, body.size());
        execl("/bin/sh", "sh", "-c", program.c_str(), (char*)nullptr);
        _exit(127);  // exec failed
    }

    // parent
    close(outp[1]);
    if (use_stdin) {
        close(inp[0]);
        ssize_t off = 0;
        while (off < (ssize_t)body.size()) {
            ssize_t w = write(inp[1], body.data() + off, body.size() - off);
            if (w <= 0) break;
            off += w;
        }
        close(inp[1]);
    }

    std::string out;
    char buf[8192];
    ssize_t n;
    while ((n = read(outp[0], buf, sizeof buf)) > 0) out.append(buf, (size_t)n);
    close(outp[0]);

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 127) {
        r.error = "failed to exec program";
        return r;
    }

    parse_response(out, r);
    r.ok = true;
    return r;
}

}  // namespace hv
