#include <cstdio>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ParkingLogger.h"
#include "ParkingSlotManager.h"
#include "TestFramework.h"
#include "TestSupport.h"

TEST(logger_writesChangeErrorAndLifecycleLines) {
    const std::string path = uniqueTempPath(".log");
    {
        ParkingLogger lg(path);
        CHECK(lg.isOpen());
        CHECK_EQ(lg.path(), path);

        ParkingEvent e;
        e.timestamp = "2026-01-01 10:00:00";
        e.slotId = 3;
        e.oldState = SlotState::FREE;
        e.newState = SlotState::OCCUPIED;
        e.source = "api";
        lg.onSlotChanged(e);
        lg.onError("something bad");
    }  // destructor writes "logging stopped" and closes the file

    const std::string text = readTextFile(path);
    CHECK(contains(text, "[INFO] logging started"));
    CHECK(contains(text, "2026-01-01 10:00:00 [INFO] slot=3 FREE -> OCCUPIED source=api"));
    CHECK(contains(text, "[ERROR] something bad"));
    CHECK(contains(text, "[INFO] logging stopped"));
    CHECK_EQ(countOccurrences(text, "\n"), 4);
    std::remove(path.c_str());
}

TEST(logger_appendsAcrossInstances) {
    const std::string path = uniqueTempPath(".log");
    { ParkingLogger a(path); }
    { ParkingLogger b(path); }
    const std::string text = readTextFile(path);
    CHECK_EQ(countOccurrences(text, "logging started"), 2);  // second run did not overwrite
    CHECK_EQ(countOccurrences(text, "logging stopped"), 2);
    std::remove(path.c_str());
}

TEST(logger_neutralisesNewlinesInMessages) {
    const std::string path = uniqueTempPath(".log");
    {
        ParkingLogger lg(path);
        lg.onError("first\nFAKE [INFO] forged line\tx");
    }
    const std::string text = readTextFile(path);
    CHECK(!contains(text, "\nFAKE"));                 // no forged line
    CHECK_EQ(countOccurrences(text, "\n"), 3);        // started, error, stopped only
    CHECK(contains(text, "first FAKE [INFO] forged line x"));
    std::remove(path.c_str());
}

TEST(logger_truncatesVeryLongMessages) {
    const std::string path = uniqueTempPath(".log");
    {
        ParkingLogger lg(path);
        lg.onError(std::string(5000, 'x'));
    }
    const std::string text = readTextFile(path);
    CHECK(text.size() < 800);
    CHECK(contains(text, "..."));
    std::remove(path.c_str());
}

TEST(logger_badPathDisablesLoggingWithoutCrashing) {
    ParkingLogger lg("/nonexistent_dir_for_smart_parking_test/x.log");
    CHECK(!lg.isOpen());
    ParkingEvent e;
    e.slotId = 1;
    lg.onSlotChanged(e);  // must be a harmless no-op
    lg.onError("ignored");
    CHECK(!lg.isOpen());
}

TEST(logger_recordsManagerEventsAndErrors) {  // AC-05
    const std::string path = uniqueTempPath(".log");
    {
        ParkingLogger lg(path);                 // declared first => destroyed last
        ParkingSlotManager m(6, &lg);
        m.updateSlot(2, SlotState::FREE, "test");
        m.updateSlot(2, SlotState::OCCUPIED, "test");
        m.updateSlot(2, SlotState::OCCUPIED, "test");   // same state: no line
        m.updateSlot(99, SlotState::FREE, "test");      // invalid slot: error line
    }
    const std::string text = readTextFile(path);
    CHECK_EQ(countOccurrences(text, "slot=2 "), 2);
    CHECK(contains(text, "slot=2 UNKNOWN -> FREE source=test"));
    CHECK(contains(text, "slot=2 FREE -> OCCUPIED source=test"));
    CHECK_EQ(countOccurrences(text, "[ERROR]"), 1);
    CHECK(contains(text, "[ERROR] invalid slot id 99"));
    std::remove(path.c_str());
}

TEST(logger_concurrentWritesKeepLinesIntact) {
    const std::string path = uniqueTempPath(".log");
    {
        ParkingLogger lg(path);
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&lg, t]() {
                for (int i = 0; i < 100; ++i) {
                    lg.onError("thread " + std::to_string(t) + " message " + std::to_string(i));
                }
            });
        }
        for (std::thread& th : threads) th.join();
    }
    const std::string text = readTextFile(path);
    CHECK_EQ(countOccurrences(text, "\n"), 402);  // 400 + started + stopped

    std::istringstream in(text);
    std::string line;
    int broken = 0;
    while (std::getline(in, line)) {
        const bool good = line.compare(0, 2, "20") == 0 &&
                          (contains(line, "[INFO]") || contains(line, "[ERROR]"));
        if (!good) ++broken;
    }
    CHECK_EQ(broken, 0);  // no interleaved or torn lines
    std::remove(path.c_str());
}
