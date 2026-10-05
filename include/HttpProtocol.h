#ifndef HTTP_PROTOCOL_H
#define HTTP_PROTOCOL_H

#include <cstddef>
#include <string>

// Parsed request line + the one header we care about.
struct HttpHead {
    std::string method;               // "GET", "POST", ...
    std::string path;                 // target without the query string
    std::size_t contentLength = 0;    // 0 if no Content-Length header
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    std::string allow;                // value of the Allow header (for 405)
};

// Parses everything BEFORE the blank line (request line + headers, without
// the final "\r\n\r\n"). Returns false if the request is malformed.
bool parseHead(const std::string& head, HttpHead& out);

// Turns a response into the exact bytes to send on the socket.
std::string serialize(const HttpResponse& response);

const char* reasonPhrase(int status);

// Escapes a string for use inside a JSON string literal.
std::string jsonEscape(const std::string& text);

// Builds {"error":"<message>"} with the given status code.
HttpResponse makeJsonError(int status, const std::string& message);

#endif // HTTP_PROTOCOL_H
