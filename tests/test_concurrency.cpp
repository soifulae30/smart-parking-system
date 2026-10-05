#include <atomic>
#include <random>
#include <thread>
#include <vector>

#include "ParkingSlotManager.h"
#include "TestFramework.h"
#include "TestSupport.h"

namespace {
SlotState pick(std::mt19937& rng) {
    switch (rng() % 3) {
        case 0:  return SlotState::FREE;
        case 1:  return SlotState::OCCUPIED;
        default: return SlotState::UNKNOWN;
    }
}
}  // namespace

TEST(concurrency_manyWritersKeepEveryInvariant) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    std::atomic<int> changed{0};
    std::atomic<int> invalid{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&m, &changed, &invalid, t]() {
            std::mt19937 rng(static_cast<unsigned>(t) + 1);
            for (int i = 0; i < 5000; ++i) {
                const int id = (i % 50 == 0) ? 0 : static_cast<int>(rng() % 6) + 1;  // some invalid
                const UpdateResult r = m.updateSlot(id, pick(rng), "thread");
                if (r == UpdateResult::OK_CHANGED) ++changed;
                if (r == UpdateResult::INVALID_SLOT) ++invalid;
            }
        });
    }
    for (std::thread& th : threads) th.join();

    const Snapshot s = m.getSnapshot();
    CHECK_EQ(s.counts.available + s.counts.occupied + s.counts.unknown, s.counts.total);
    CHECK_EQ(static_cast<int>(l.events().size()), changed.load());   // one event per real change
    CHECK_EQ(static_cast<int>(l.errors().size()), invalid.load());   // one error per rejection
    CHECK_EQ(static_cast<int>(m.getHistory(1000).size()), 50);       // bounded, not corrupted
    CHECK(changed.load() > 0);
    CHECK(invalid.load() > 0);
}

TEST(concurrency_readersAlwaysSeeConsistentSnapshots) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    std::atomic<int> writersFinished{0};
    std::atomic<int> violations{0};
    std::atomic<long> snapshotsTaken{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {  // writers
        threads.emplace_back([&m, &writersFinished, t]() {
            std::mt19937 rng(static_cast<unsigned>(t) + 100);
            for (int i = 0; i < 3000; ++i) {
                m.updateSlot(static_cast<int>(rng() % 6) + 1, pick(rng), "writer");
            }
            ++writersFinished;
        });
    }
    for (int t = 0; t < 4; ++t) {  // readers
        threads.emplace_back([&m, &writersFinished, &violations, &snapshotsTaken]() {
            while (writersFinished.load() < 4) {
                const Snapshot s = m.getSnapshot();
                ++snapshotsTaken;
                int f = 0, o = 0, u = 0;
                for (const ParkingSlot& slot : s.slots) {
                    if (slot.getState() == SlotState::FREE) ++f;
                    else if (slot.getState() == SlotState::OCCUPIED) ++o;
                    else ++u;
                }
                // counts must describe exactly the slots that were copied with them
                if (f != s.counts.available || o != s.counts.occupied || u != s.counts.unknown ||
                    s.counts.total != 6 || static_cast<int>(s.slots.size()) != 6) {
                    ++violations;
                }
            }
        });
    }
    for (std::thread& th : threads) th.join();

    CHECK_EQ(violations.load(), 0);
    CHECK(snapshotsTaken.load() > 0);
}

TEST(concurrency_reentrantListenerNeverDeadlocks) {
    ReentrantListener l;
    ParkingSlotManager m(6, &l);
    l.manager = &m;
    std::atomic<int> changed{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&m, &changed, t]() {
            std::mt19937 rng(static_cast<unsigned>(t) + 7);
            for (int i = 0; i < 500; ++i) {
                if (m.updateSlot(static_cast<int>(rng() % 6) + 1, pick(rng), "t") ==
                    UpdateResult::OK_CHANGED) {
                    ++changed;
                }
            }
        });
    }
    for (std::thread& th : threads) th.join();  // a deadlock would hang here
    CHECK_EQ(l.callbacks.load(), changed.load());
    CHECK_EQ(l.totalSeen.load(), 6);
}
