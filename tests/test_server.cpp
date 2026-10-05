#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ParkingSlotManager.h"
#include "StatusServer.h"
#include "TestFramework.h"
#include "TestSupport.h"

namespace {

// A real server on an OS-chosen port, running its accept loop on a thread.
struct ServerFixture {
    RecordingListener events;
    ParkingSlotManager manager;
    StatusServer server;
    std::atomic<bool> stop{false};
    std::thread thread;
    bool started;

    ServerFixture()
        : manager(6, &events),
          server(manager, events, "127.0.0.1", 0, "nonexistent_web_dir"),
          started(server.start()) {
        if (started) {
            thread = std::thread([this]() { server.run(&stop); });
        }
    }
    ~ServerFixture() {
        stop = true;
        if (thread.joinable()) thread.join();
    }
    int port() const { return server.port(); }
};

}  // namespace

TEST(server_startsOnFreePort) {
    ServerFixture fx;
    CHECK(fx.started);
    CHECK(fx.port() > 0);
    CHECK(waitForServer(fx.port(), 2000));
}

TEST(server_statusEndpointOverRealSocket) {  // AC-01, FR-15
    ServerFixture fx;
    const std::string resp = httpExchange(fx.port(), makeGet("/api/status"));
    CHECK_EQ(statusCodeOf(resp), 200);
    CHECK(contains(resp, "Content-Type: application/json"));
    const std::string body = bodyOf(resp);
    CHECK_EQ(jsonInt(body, "total"), 6);
    CHECK_EQ(jsonInt(body, "available"), 0);
    CHECK_EQ(jsonInt(body, "unknown"), 6);
}

TEST(server_postChangesTheRealApplicationState) {  // FR-22, AC-02, replaces AC-07
    ServerFixture fx;
    const std::string post = httpExchange(fx.port(), makePost("/api/slots/2", "{\"state\":\"FREE\"}"));
    CHECK_EQ(statusCodeOf(post), 200);

    // The C++ object in this process really changed:
    CHECK(fx.manager.getSnapshot().slots[1].getState() == SlotState::FREE);
    CHECK_EQ(fx.manager.getSnapshot().counts.available, 1);

    // And a later request sees it:
    const std::string status = bodyOf(httpExchange(fx.port(), makeGet("/api/status")));
    CHECK_EQ(jsonInt(status, "available"), 1);
    CHECK_EQ(jsonInt(status, "unknown"), 5);

    const std::string history = bodyOf(httpExchange(fx.port(), makeGet("/api/history")));
    CHECK(contains(history, "\"slot\":2"));
    CHECK(contains(history, "\"source\":\"api\""));
}

TEST(server_rejectsInvalidRequestsAndKeepsState) {  // AC-03, AC-04
    ServerFixture fx;
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makePost("/api/slots/999", "{\"state\":\"FREE\"}"))), 404);
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makePost("/api/slots/1", "{\"state\":\"BANANA\"}"))), 400);
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makePost("/api/slots/abc", "{\"state\":\"FREE\"}"))), 400);
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makePost("/api/slots/1", ""))), 400);
    CHECK_EQ(fx.manager.getSnapshot().counts.unknown, 6);
    CHECK(fx.events.events().empty());
}

TEST(server_wrongMethodAndUnknownPath) {
    ServerFixture fx;
    const std::string r405 = httpExchange(fx.port(), makeGet("/api/slots/1"));
    CHECK_EQ(statusCodeOf(r405), 405);
    CHECK(contains(r405, "Allow: POST"));
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makeGet("/nothing"))), 404);
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makeGet("/"))), 404);  // no web dir in this fixture
}

TEST(server_oversizedBodyGets413AndServerSurvives) {  // AC-11, NFR-04
    ServerFixture fx;
    const std::string big(20000, 'a');
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makePost("/api/slots/1", big))), 413);
    CHECK(fx.events.anyErrorContains("size limit"));
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makeGet("/api/status"))), 200);  // still alive
}

TEST(server_oversizedHeadersGet413) {
    ServerFixture fx;
    // 10000 bytes and never a blank line: the server must stop reading at the limit.
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), std::string(10000, 'a'))), 413);
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makeGet("/api/status"))), 200);
}

TEST(server_malformedRequestsGet400AndServerSurvives) {
    ServerFixture fx;
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), "garbage\r\n\r\n")), 400);
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), "GET / HTTP/1.1\r\nBadHeader\r\n\r\n")), 400);
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), "POST /api/slots/1 HTTP/1.1\r\nContent-Length: abc\r\n\r\n")), 400);
    CHECK(fx.events.anyErrorContains("malformed"));
    CHECK_EQ(statusCodeOf(httpExchange(fx.port(), makeGet("/api/status"))), 200);
}

TEST(server_reassemblesRequestSplitAcrossTcpSegments) {
    ServerFixture fx;
    const int fd = connectLoopback(fx.port());
    CHECK(fd >= 0);
    if (fd < 0) return;

    const std::string full = makePost("/api/slots/1", "{\"state\":\"FREE\"}");
    const std::size_t cut = 25;  // in the middle of the request line/headers
    sendBytes(fd, full.substr(0, cut));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // force a separate recv()
    sendBytes(fd, full.substr(cut));
    const std::string resp = readUntilClose(fd);
    ::close(fd);

    CHECK_EQ(statusCodeOf(resp), 200);
    CHECK(fx.manager.getSnapshot().slots[0].getState() == SlotState::FREE);
}

TEST(server_idleClientDoesNotBlockOthers) {  // Phase 9: thread per connection
    ServerFixture fx;
    const int idle = connectLoopback(fx.port());  // connects, sends nothing
    CHECK(idle >= 0);

    const auto start = std::chrono::steady_clock::now();
    const std::string resp = httpExchange(fx.port(), makeGet("/api/status"));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();

    CHECK_EQ(statusCodeOf(resp), 200);
    CHECK(ms < 1500);  // would take about 5000 ms with a single-threaded server
    if (idle >= 0) ::close(idle);
}

TEST(server_manyConcurrentClientsStayConsistent) {  // AC-09 (server level)
    ServerFixture fx;
    std::atomic<int> okCount{0};
    std::atomic<int> inconsistent{0};
    std::vector<std::thread> clients;

    for (int t = 0; t < 20; ++t) {
        clients.emplace_back([&fx, &okCount, &inconsistent, t]() {
            const char* state = (t % 2 == 0) ? "FREE" : "OCCUPIED";
            for (int i = 0; i < 5; ++i) {
                const std::string path = "/api/slots/" + std::to_string((t + i) % 6 + 1);
                const std::string post = httpExchange(
                    fx.port(), makePost(path, std::string("{\"state\":\"") + state + "\"}"));
                const std::string get = httpExchange(fx.port(), makeGet("/api/status"));
                if (statusCodeOf(post) == 200 && statusCodeOf(get) == 200) ++okCount;
                const std::string body = bodyOf(get);
                const int sum = jsonInt(body, "available") + jsonInt(body, "occupied") +
                                jsonInt(body, "unknown");
                if (sum != jsonInt(body, "total")) ++inconsistent;
            }
        });
    }
    for (std::thread& c : clients) c.join();

    CHECK_EQ(okCount.load(), 100);
    CHECK_EQ(inconsistent.load(), 0);
    const Snapshot s = fx.manager.getSnapshot();
    CHECK_EQ(s.counts.available + s.counts.occupied + s.counts.unknown, s.counts.total);
    CHECK(fx.events.errors().empty());
}

TEST(server_portIsReleasedAfterShutdown) {  // AC-08 equivalent, AC-10
    int port = 0;
    std::chrono::steady_clock::time_point before;
    {
        auto fx = std::make_unique<ServerFixture>();
        port = fx->port();
        CHECK_EQ(statusCodeOf(httpExchange(port, makeGet("/api/status"))), 200);
        before = std::chrono::steady_clock::now();
        fx.reset();  // stop flag, join all threads, close the listening socket
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - before).count();
    CHECK(ms < 3000);  // graceful shutdown is prompt without idle clients
    CHECK(connectLoopback(port) < 0);  // nobody is listening any more: clients get an error
}

TEST(server_runReturnsWhenStopFlagIsSet) {
    RecordingListener events;
    ParkingSlotManager manager(4, &events);
    StatusServer server(manager, events, "127.0.0.1", 0, "nonexistent_web_dir");
    CHECK(server.start());

    std::atomic<bool> stop{false};
    std::thread t([&]() { server.run(&stop); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto start = std::chrono::steady_clock::now();
    stop = true;
    t.join();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
    CHECK(ms < 1500);  // the 200 ms poll() timeout bounds the reaction time
}

TEST(server_startupErrorsAreReported) {
    RecordingListener events;
    ParkingSlotManager manager(4, &events);

    {   // port already in use
        ListeningSocket occupied;
        CHECK(occupied.port > 0);
        StatusServer s(manager, events, "127.0.0.1", occupied.port, "web");
        CHECK(!s.start());
        CHECK(events.anyErrorContains("bind() failed"));
    }
    {   // invalid address
        StatusServer s(manager, events, "999.1.1.1", 8080, "web");
        CHECK(!s.start());
        CHECK(events.anyErrorContains("invalid IPv4 address"));
    }
    {   // invalid port
        StatusServer s(manager, events, "127.0.0.1", 70000, "web");
        CHECK(!s.start());
        CHECK(events.anyErrorContains("invalid port"));
    }
}
