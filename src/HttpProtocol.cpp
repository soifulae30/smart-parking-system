#include "HttpProtocol.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    std::size_t begin = s.find_first_not_of(" \t");
    if (begin == std::string::npos) return "";
    std::size_t end = s.find_last_not_of(" \t");
    return s.substr(begin, end - begin + 1);
}

std::string toLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

}  // namespace

bool parseHead(const std::string& head, HttpHead& out) {
    const std::size_t lineEnd = head.find("\r\n");
    const std::string requestLine = head.substr(0, lineEnd);  // npos = whole string

    // "METHOD SP target SP HTTP/1.x"
    const std::size_t s1 = requestLine.find(' ');
    if (s1 == std::string::npos) return false;
    const std::size_t s2 = requestLine.find(' ', s1 + 1);
    if (s2 == std::string::npos) return false;

    const std::string method = requestLine.substr(0, s1);
    std::string target = requestLine.substr(s1 + 1, s2 - s1 - 1);
    const std::string version = requestLine.substr(s2 + 1);

    if (method.empty() || target.empty()) return false;
    for (char c : method) {
        if (!std::isupper(static_cast<unsigned char>(c))) return false;
    }
    if (version.size() != 8 || version.compare(0, 7, "HTTP/1.") != 0) return false;
    if (target[0] != '/') return false;

    const std::size_t query = target.find('?');
    if (query != std::string::npos) target.erase(query);  // ignore ?a=b

    // Header lines
    std::size_t contentLength = 0;
    std::size_t pos = lineEnd;
    while (pos != std::string::npos) {
        pos += 2;                                   // skip the "\r\n"
        if (pos >= head.size()) break;
        const std::size_t next = head.find("\r\n", pos);
        const std::string line =
            head.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        pos = next;
        if (line.empty()) continue;

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) return false;  // header without ':'
        if (toLower(trim(line.substr(0, colon))) == "content-length") {
            const std::string value = trim(line.substr(colon + 1));
            if (value.empty() || value.size() > 9) return false;
            for (char c : value) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            }
            contentLength = std::stoul(value);
        }
    }

    out.method = method;
    out.path = target;
    out.contentLength = contentLength;
    return true;
}

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

std::string serialize(const HttpResponse& r) {
    std::ostringstream out;
    out << "HTTP/1.1 " << r.status << ' ' << reasonPhrase(r.status) << "\r\n"
        << "Content-Type: " << r.contentType << "\r\n"
        << "Content-Length: " << r.body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Cache-Control: no-store\r\n"
        << "X-Content-Type-Options: nosniff\r\n";
    if (!r.allow.empty()) {
        out << "Allow: " << r.allow << "\r\n";
    }
    out << "\r\n" << r.body;
    return out.str();
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

HttpResponse makeJsonError(int status, const std::string& message) {
    HttpResponse r;
    r.status = status;
    r.body = "{\"error\":\"" + jsonEscape(message) + "\"}";
    return r;
}
