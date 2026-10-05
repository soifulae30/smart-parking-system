#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "ApiHandler.h"
#include "HttpProtocol.h"
#include "ParkingSlotManager.h"
#include "TestFramework.h"
#include "TestSupport.h"

namespace {
HttpRequest req(const std::string& method, const std::string& path,
                const std::string& body = "") {
    HttpRequest r;
    r.method = method;
    r.path = path;
    r.body = body;
    return r;
}
}  // namespace

// ----------------------------------------------------------- HttpProtocol

TEST(http_parseHeadValid) {
    HttpHead h;
    CHECK(parseHead("GET /api/status HTTP/1.1\r\nHost: x\r\nAccept: */*", h));
    CHECK_EQ(h.method, "GET");
    CHECK_EQ(h.path, "/api/status");
    CHECK_EQ(h.contentLength, static_cast<std::size_t>(0));

    CHECK(parseHead("POST /api/slots/3 HTTP/1.1\r\nContent-Length: 16", h));
    CHECK_EQ(h.method, "POST");
    CHECK_EQ(h.contentLength, static_cast<std::size_t>(16));

    CHECK(parseHead("GET / HTTP/1.0", h));  // no headers at all
    CHECK_EQ(h.path, "/");
}

TEST(http_parseHeadQueryStringAndHeaderCase) {
    HttpHead h;
    CHECK(parseHead("GET /api/status?x=1&y=2 HTTP/1.1", h));
    CHECK_EQ(h.path, "/api/status");

    CHECK(parseHead("POST /x HTTP/1.1\r\ncontent-LENGTH:   5  ", h));
    CHECK_EQ(h.contentLength, static_cast<std::size_t>(5));
}

TEST(http_parseHeadMalformedRejected) {
    HttpHead h;
    CHECK(!parseHead("garbage", h));
    CHECK(!parseHead("", h));
    CHECK(!parseHead("GET /x", h));                         // no version
    CHECK(!parseHead("get /x HTTP/1.1", h));                // lowercase method
    CHECK(!parseHead("GET x HTTP/1.1", h));                 // target must start with /
    CHECK(!parseHead("GET /x HTTP/2.0", h));                // unsupported version
    CHECK(!parseHead("GET /x HTTP/1.1\r\nBadHeader", h));   // header without ':'
    CHECK(!parseHead("POST /x HTTP/1.1\r\nContent-Length: abc", h));
    CHECK(!parseHead("POST /x HTTP/1.1\r\nContent-Length: -5", h));
    CHECK(!parseHead("POST /x HTTP/1.1\r\nContent-Length: 99999999999", h));  // too many digits
    CHECK(!parseHead("POST /x HTTP/1.1\r\nContent-Length:", h));
}

TEST(http_serializeProducesValidResponse) {
    HttpResponse r;
    r.body = "{}";
    const std::string s = serialize(r);
    CHECK(s.compare(0, 17, "HTTP/1.1 200 OK\r\n") == 0);
    CHECK(contains(s, "Content-Type: application/json\r\n"));
    CHECK(contains(s, "Content-Length: 2\r\n"));
    CHECK(contains(s, "Connection: close\r\n"));
    CHECK(s.size() >= 6 && s.compare(s.size() - 6, 6, "\r\n\r\n{}") == 0);

    HttpResponse e = makeJsonError(405, "nope");
    e.allow = "GET";
    const std::string s2 = serialize(e);
    CHECK(contains(s2, "HTTP/1.1 405 Method Not Allowed\r\n"));
    CHECK(contains(s2, "Allow: GET\r\n"));

    CHECK_EQ(std::string(reasonPhrase(404)), "Not Found");
    CHECK_EQ(std::string(reasonPhrase(413)), "Payload Too Large");
    CHECK_EQ(std::string(reasonPhrase(503)), "Service Unavailable");
}

TEST(http_jsonEscape) {
    CHECK_EQ(jsonEscape("plain"), "plain");
    CHECK_EQ(jsonEscape("a\"b"), "a\\\"b");
    CHECK_EQ(jsonEscape("a\\b"), "a\\\\b");
    CHECK_EQ(jsonEscape("a\nb\tc"), "a\\nb\\tc");
    CHECK_EQ(jsonEscape(std::string(1, '\x01')), "\\u0001");
}

TEST(http_makeJsonErrorBuildsValidBody) {
    const HttpResponse r = makeJsonError(400, "bad \"x\"");
    CHECK_EQ(r.status, 400);
    CHECK_EQ(r.body, "{\"error\":\"bad \\\"x\\\"\"}");
    CHECK_EQ(r.contentType, "application/json");
}

// ------------------------------------------------------------- ApiHandler

TEST(api_statusInitialExactJson) {  // AC-01
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    ApiHandler h(m, l, "nonexistent_web_dir");

    std::string expected = "{\"total\":6,\"available\":0,\"occupied\":0,\"unknown\":6,\"slots\":[";
    for (int i = 1; i <= 6; ++i) {
        if (i > 1) expected += ",";
        expected += "{\"id\":" + std::to_string(i) + ",\"state\":\"UNKNOWN\"}";
    }
    expected += "]}";

    const HttpResponse r = h.handle(req("GET", "/api/status"));
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.contentType, "application/json");
    CHECK_EQ(r.body, expected);
}

TEST(api_postValidUpdatesChangeState) {  // AC-02, FR-17
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    ApiHandler h(m, l, "nonexistent_web_dir");

    HttpResponse r = h.handle(req("POST", "/api/slots/1", "{\"state\":\"FREE\"}"));
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, "{\"slot\":1,\"state\":\"FREE\",\"changed\":true}");
    CHECK(m.getSnapshot().slots[0].getState() == SlotState::FREE);
    CHECK_EQ(m.getSnapshot().counts.available, 1);
    CHECK_EQ(m.getSnapshot().counts.unknown, 5);

    r = h.handle(req("POST", "/api/slots/1", "{\"state\":\"FREE\"}"));  // same again
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, "{\"slot\":1,\"state\":\"FREE\",\"changed\":false}");
    CHECK_EQ(static_cast<int>(l.events().size()), 1);

    r = h.handle(req("POST", "/api/slots/1", "{\"state\":\"occupied\"}"));  // lowercase ok
    CHECK_EQ(r.body, "{\"slot\":1,\"state\":\"OCCUPIED\",\"changed\":true}");

    r = h.handle(req("POST", "/api/slots/2", "  { \"state\" : \"FREE\" }  "));  // whitespace ok
    CHECK_EQ(r.status, 200);
    CHECK_EQ(l.events().back().source, "api");
}

TEST(api_postBadInputRejectedAndStateUnchanged) {  // AC-03, AC-04
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    ApiHandler h(m, l, "nonexistent_web_dir");

    const std::string good = "{\"state\":\"FREE\"}";
    const std::vector<std::pair<std::string, std::string>> bad400 = {
        {"/api/slots/1", "{\"state\":\"BANANA\"}"},
        {"/api/slots/1", ""},
        {"/api/slots/1", "{}"},
        {"/api/slots/1", "{\"state\":\"FREE\",\"x\":1}"},   // extra key
        {"/api/slots/1", "{\"state\":FREE}"},               // unquoted value
        {"/api/slots/1", "{\"state\":\"FREE\"} trailing"},
        {"/api/slots/1", "{\"state\":\"A\\nB\"}"},          // escape sequence refused
        {"/api/slots/1", "{\"state\":\"THIS_STATE_NAME_IS_TOO_LONG\"}"},
        {"/api/slots/abc", good},
        {"/api/slots/-1", good},
        {"/api/slots/1x", good},
        {"/api/slots/", good},
        {"/api/slots/1234567", good},                       // too many digits
    };
    for (const auto& item : bad400) {
        const HttpResponse r = h.handle(req("POST", item.first, item.second));
        CHECK_EQ(r.status, 400);
        CHECK(contains(r.body, "\"error\""));
    }
    CHECK_EQ(static_cast<int>(l.errors().size()), static_cast<int>(bad400.size()));  // FR-13

    CHECK_EQ(h.handle(req("POST", "/api/slots/999", good)).status, 404);
    CHECK_EQ(h.handle(req("POST", "/api/slots/999999", good)).status, 404);
    CHECK_EQ(h.handle(req("POST", "/api/slots/0", good)).status, 404);

    CHECK_EQ(m.getSnapshot().counts.unknown, 6);  // nothing ever changed
    CHECK(l.events().empty());
}

TEST(api_wrongMethodGets405WithAllowHeader) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    ApiHandler h(m, l, "nonexistent_web_dir");

    struct Case { const char* method; const char* path; const char* allow; };
    const Case cases[] = {
        {"GET", "/api/slots/1", "POST"},
        {"POST", "/api/status", "GET"},
        {"DELETE", "/api/history", "GET"},
        {"POST", "/", "GET"},
        {"PUT", "/app.js", "GET"},
    };
    for (const Case& c : cases) {
        const HttpResponse r = h.handle(req(c.method, c.path));
        CHECK_EQ(r.status, 405);
        CHECK_EQ(r.allow, c.allow);
    }
}

TEST(api_unknownPathsGet404) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    ApiHandler h(m, l, "nonexistent_web_dir");

    const char* paths[] = {"/nothing", "/api", "/api/status/", "/api/slots",
                           "/../../etc/passwd", "/index.html", "/api/STATUS"};
    for (const char* p : paths) {
        CHECK_EQ(h.handle(req("GET", p)).status, 404);
    }
}

TEST(api_historyNewestFirst) {  // FR-16
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    ApiHandler h(m, l, "nonexistent_web_dir");

    CHECK_EQ(h.handle(req("GET", "/api/history")).body, "{\"events\":[]}");

    h.handle(req("POST", "/api/slots/1", "{\"state\":\"FREE\"}"));
    h.handle(req("POST", "/api/slots/1", "{\"state\":\"OCCUPIED\"}"));

    const HttpResponse r = h.handle(req("GET", "/api/history"));
    CHECK_EQ(r.status, 200);
    CHECK(r.body.find("{\"events\":[{\"timestamp\"") == 0);
    const std::size_t newer = r.body.find("\"new\":\"OCCUPIED\"");
    const std::size_t older = r.body.find("\"new\":\"FREE\"");
    CHECK(newer != std::string::npos);
    CHECK(older != std::string::npos);
    CHECK(newer < older);  // newest event listed first
    CHECK(contains(r.body, "\"source\":\"api\""));
}

TEST(api_staticFileServing) {
    const std::string dir = uniqueTempPath("_web");
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir + "/index.html");
        out << "hello";
    }

    RecordingListener l;
    ParkingSlotManager m(6, &l);
    ApiHandler h(m, l, dir);

    const HttpResponse ok = h.handle(req("GET", "/"));
    CHECK_EQ(ok.status, 200);
    CHECK_EQ(ok.contentType, "text/html; charset=utf-8");
    CHECK_EQ(ok.body, "hello");

    CHECK_EQ(h.handle(req("GET", "/style.css")).status, 404);  // file not present
    CHECK(l.anyErrorContains("static file not found"));

    std::filesystem::remove_all(dir);
}
