#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "ParkingApplication.h"
#include "TestFramework.h"
#include "TestSupport.h"

namespace {

AppConfig makeConfig(int sensorMs) {
    AppConfig cfg;
    cfg.port = freePort();
    cfg.slots = 6;
    cfg.logPath = uniqueTempPath(".log");
    cfg.webDir = "nonexistent_web_dir";
    cfg.sensorMs = sensorMs;
    cfg.seed = 42;
    return cfg;
}

}  // namespace

TEST(app_runsServesAndShutsDownGracefully) {  // FR-06, FR-24, AC-10
    const AppConfig cfg = makeConfig(50);
    std::atomic<bool> stop{false};
    int exitCode = -1;
    {
        ParkingApplication app(cfg);
        std::thread runner([&]() { exitCode = app.run(&stop); });

        const bool up = waitForServer(cfg.port, 3000);
        CHECK(up);
        if (up) {
            CHECK_EQ(statusCodeOf(httpExchange(cfg.port, makeGet("/api/status"))), 200);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));  // ~14 sensor polls
        }
        stop = true;
        runner.join();
    }
    CHECK_EQ(exitCode, 0);

    const std::string log = readTextFile(cfg.logPath);
    CHECK(contains(log, "logging started"));
    CHECK(contains(log, "source=sensor"));        // the sensor thread really drove updates
    CHECK(contains(log, "logging stopped"));      // clean shutdown reached the logger
    CHECK(connectLoopback(cfg.port) < 0);         // port released
    std::remove(cfg.logPath.c_str());
}

TEST(app_sensorThreadCanBeDisabled) {
    const AppConfig cfg = makeConfig(0);
    std::atomic<bool> stop{false};
    int exitCode = -1;
    {
        ParkingApplication app(cfg);
        std::thread runner([&]() { exitCode = app.run(&stop); });
        CHECK(waitForServer(cfg.port, 3000));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const std::string status = bodyOf(httpExchange(cfg.port, makeGet("/api/status")));
        CHECK_EQ(jsonInt(status, "unknown"), 6);  // nobody changed anything
        stop = true;
        runner.join();
    }
    CHECK_EQ(exitCode, 0);
    CHECK(!contains(readTextFile(cfg.logPath), "source=sensor"));
    std::remove(cfg.logPath.c_str());
}

TEST(app_portInUseReturnsErrorAndLogsIt) {  // FR-13, socket error handling
    ListeningSocket occupied;
    AppConfig cfg = makeConfig(0);
    cfg.port = occupied.port;
    std::atomic<bool> stop{false};
    int exitCode = -1;
    {
        ParkingApplication app(cfg);
        exitCode = app.run(&stop);  // returns immediately: bind fails
    }
    CHECK_EQ(exitCode, 1);
    const std::string log = readTextFile(cfg.logPath);
    CHECK(contains(log, "[ERROR] bind() failed"));
    CHECK(contains(log, "logging stopped"));
    std::remove(cfg.logPath.c_str());
}

TEST(app_requestShutdownEndsRun) {
    const AppConfig cfg = makeConfig(100);
    int exitCode = -1;
    {
        ParkingApplication app(cfg);
        std::thread runner([&]() { exitCode = app.run(nullptr); });
        CHECK(waitForServer(cfg.port, 3000));
        app.requestShutdown();
        runner.join();
    }
    CHECK_EQ(exitCode, 0);
    CHECK(contains(readTextFile(cfg.logPath), "logging stopped"));
    std::remove(cfg.logPath.c_str());
}

TEST(app_clientsSeeConsistentCountsWhileSensorRuns) {  // AC-09 (system level)
    const AppConfig cfg = makeConfig(20);  // fast sensor: constant competing writes
    std::atomic<bool> stop{false};
    std::atomic<int> okCount{0};
    std::atomic<int> bad{0};
    int exitCode = -1;
    {
        ParkingApplication app(cfg);
        std::thread runner([&]() { exitCode = app.run(&stop); });

        if (waitForServer(cfg.port, 3000)) {
            std::vector<std::thread> clients;
            for (int t = 0; t < 6; ++t) {
                clients.emplace_back([&cfg, &okCount, &bad, t]() {
                    for (int i = 0; i < 20; ++i) {
                        const std::string path = "/api/slots/" + std::to_string(i % 6 + 1);
                        const char* state = (t % 2 == 0) ? "FREE" : "OCCUPIED";
                        httpExchange(cfg.port,
                                     makePost(path, std::string("{\"state\":\"") + state + "\"}"));
                        const std::string resp = httpExchange(cfg.port, makeGet("/api/status"));
                        if (statusCodeOf(resp) != 200) { ++bad; continue; }
                        ++okCount;
                        const std::string body = bodyOf(resp);
                        const int sum = jsonInt(body, "available") + jsonInt(body, "occupied") +
                                        jsonInt(body, "unknown");
                        if (jsonInt(body, "total") != 6 || sum != 6) ++bad;
                    }
                });
            }
            for (std::thread& c : clients) c.join();
        } else {
            CHECK(false);
        }
        stop = true;
        runner.join();
    }
    CHECK_EQ(exitCode, 0);
    CHECK_EQ(okCount.load(), 120);
    CHECK_EQ(bad.load(), 0);
    std::remove(cfg.logPath.c_str());
}
