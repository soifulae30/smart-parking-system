#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "ParkingSlotManager.h"
#include "TestFramework.h"
#include "TestSupport.h"

namespace {
const SlotState kAllStates[] = {SlotState::FREE, SlotState::OCCUPIED, SlotState::UNKNOWN};
}

TEST(manager_initialStateAllUnknown) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    const Snapshot s = m.getSnapshot();

    CHECK_EQ(static_cast<int>(s.slots.size()), 6);
    CHECK_EQ(s.counts.total, 6);
    CHECK_EQ(s.counts.unknown, 6);
    CHECK_EQ(s.counts.available, 0);
    CHECK_EQ(s.counts.occupied, 0);
    for (std::size_t i = 0; i < s.slots.size(); ++i) {
        CHECK_EQ(s.slots[i].getId(), static_cast<int>(i) + 1);
        CHECK(s.slots[i].getState() == SlotState::UNKNOWN);
    }
    CHECK(m.getHistory(10).empty());
    CHECK(l.events().empty());
}

TEST(manager_availableCountsOnlyFree) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    m.updateSlot(1, SlotState::FREE, "test");
    m.updateSlot(2, SlotState::FREE, "test");
    m.updateSlot(3, SlotState::OCCUPIED, "test");
    // slots 4, 5, 6 stay UNKNOWN

    const Snapshot s = m.getSnapshot();
    CHECK_EQ(s.counts.available, 2);
    CHECK_EQ(s.counts.occupied, 1);
    CHECK_EQ(s.counts.unknown, 3);
    CHECK_EQ(s.counts.available + s.counts.occupied + s.counts.unknown, s.counts.total);
}

TEST(manager_unknownIsNeverAvailable) {  // FR-08
    RecordingListener l;
    ParkingSlotManager m(4, &l);
    m.updateSlot(1, SlotState::FREE, "test");
    CHECK_EQ(m.getSnapshot().counts.available, 1);
    m.updateSlot(1, SlotState::UNKNOWN, "test");  // sensor fault
    CHECK_EQ(m.getSnapshot().counts.available, 0);
    CHECK_EQ(m.getSnapshot().counts.unknown, 4);
}

TEST(manager_validUpdateRecordsEvent) {
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    CHECK(m.updateSlot(3, SlotState::OCCUPIED, "api") == UpdateResult::OK_CHANGED);

    const std::vector<ParkingEvent> ev = l.events();
    CHECK_EQ(static_cast<int>(ev.size()), 1);
    CHECK_EQ(ev[0].slotId, 3);
    CHECK(ev[0].oldState == SlotState::UNKNOWN);
    CHECK(ev[0].newState == SlotState::OCCUPIED);
    CHECK_EQ(ev[0].source, "api");
    CHECK_EQ(static_cast<int>(ev[0].timestamp.size()), 19);  // "YYYY-MM-DD HH:MM:SS"
    CHECK_EQ(static_cast<int>(m.getHistory(10).size()), 1);
}

TEST(manager_sameStateIsAcceptedWithoutEvent) {  // FR-09, AC-06
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    m.updateSlot(1, SlotState::FREE, "test");
    CHECK(m.updateSlot(1, SlotState::FREE, "test") == UpdateResult::OK_UNCHANGED);
    CHECK(m.updateSlot(1, SlotState::FREE, "test") == UpdateResult::OK_UNCHANGED);
    CHECK_EQ(static_cast<int>(l.events().size()), 1);
    CHECK_EQ(static_cast<int>(m.getHistory(10).size()), 1);
    // UNKNOWN -> UNKNOWN is also "unchanged"
    CHECK(m.updateSlot(2, SlotState::UNKNOWN, "test") == UpdateResult::OK_UNCHANGED);
}

TEST(manager_invalidSlotIdsRejected) {  // FR-10
    RecordingListener l;
    ParkingSlotManager m(6, &l);
    const int badIds[] = {0, -1, 7, 1000000, std::numeric_limits<int>::min(),
                          std::numeric_limits<int>::max()};
    for (int id : badIds) {
        CHECK(m.updateSlot(id, SlotState::FREE, "test") == UpdateResult::INVALID_SLOT);
    }
    CHECK_EQ(m.getSnapshot().counts.unknown, 6);  // nothing changed
    CHECK(l.events().empty());
    CHECK_EQ(static_cast<int>(l.errors().size()), 6);  // each rejection reported
    CHECK(l.anyErrorContains("invalid slot id"));
    CHECK(m.getHistory(10).empty());
}

TEST(manager_allNineTransitions) {  // the state-machine diagram as a test matrix
    for (SlotState from : kAllStates) {
        for (SlotState to : kAllStates) {
            RecordingListener l;
            ParkingSlotManager m(4, &l);
            m.updateSlot(2, from, "setup");  // slots start UNKNOWN
            const std::size_t before = l.events().size();

            const UpdateResult expected =
                (from == to) ? UpdateResult::OK_UNCHANGED : UpdateResult::OK_CHANGED;
            CHECK(m.updateSlot(2, to, "test") == expected);
            CHECK(m.getSnapshot().slots[1].getState() == to);
            CHECK_EQ(static_cast<int>(l.events().size() - before), (from == to) ? 0 : 1);
        }
    }
}

TEST(manager_repeatedUpdates) {
    RecordingListener l;
    ParkingSlotManager m(4, &l);
    for (int i = 0; i < 10; ++i) {
        m.updateSlot(1, (i % 2 == 0) ? SlotState::FREE : SlotState::OCCUPIED, "test");
        m.updateSlot(1, (i % 2 == 0) ? SlotState::FREE : SlotState::OCCUPIED, "test");  // duplicate
    }
    CHECK_EQ(static_cast<int>(l.events().size()), 10);  // duplicates produced no events
    CHECK(m.getSnapshot().slots[0].getState() == SlotState::OCCUPIED);
}

TEST(manager_historyIsNewestFirstAndBounded) {
    RecordingListener l;
    ParkingSlotManager m(4, &l);
    for (int i = 0; i < 120; ++i) {
        m.updateSlot(1, (i % 2 == 0) ? SlotState::FREE : SlotState::OCCUPIED, "test");
    }
    CHECK_EQ(static_cast<int>(l.events().size()), 120);

    const std::vector<ParkingEvent> h = m.getHistory(1000);
    CHECK_EQ(static_cast<int>(h.size()), 50);              // capped at kMaxHistory
    CHECK(h[0].newState == SlotState::OCCUPIED);           // i = 119 (odd): newest
    CHECK(h[1].newState == SlotState::FREE);               // i = 118
    CHECK_EQ(static_cast<int>(m.getHistory(3).size()), 3);
    CHECK(m.getHistory(0).empty());
}

TEST(manager_zeroSlotsThrows) {
    bool thrown = false;
    try {
        ParkingSlotManager m(0, nullptr);
        (void)m;
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    CHECK(thrown);
}

TEST(manager_worksWithoutListener) {
    ParkingSlotManager m(4, nullptr);
    CHECK(m.updateSlot(1, SlotState::FREE, "test") == UpdateResult::OK_CHANGED);
    CHECK(m.updateSlot(99, SlotState::FREE, "test") == UpdateResult::INVALID_SLOT);
    CHECK_EQ(m.getSnapshot().counts.available, 1);
}

TEST(manager_notifiesListenerOutsideItsLock) {  // NFR-07
    ReentrantListener l;
    ParkingSlotManager m(4, &l);
    l.manager = &m;
    // If updateSlot still held its mutex while notifying, the listener's call
    // to getSnapshot() would block forever and this test would hang.
    CHECK(m.updateSlot(1, SlotState::FREE, "test") == UpdateResult::OK_CHANGED);
    CHECK_EQ(l.callbacks.load(), 1);
    CHECK_EQ(l.totalSeen.load(), 4);
}
