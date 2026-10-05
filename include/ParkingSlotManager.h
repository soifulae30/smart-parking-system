#ifndef PARKING_SLOT_MANAGER_H
#define PARKING_SLOT_MANAGER_H

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "IEventListener.h"
#include "ParkingEvent.h"
#include "ParkingSlot.h"
#include "SlotState.h"

// Counts derived from the slot states (FR-07). Only FREE is "available" (FR-08).
struct SlotCounts {
    int total = 0;
    int available = 0;
    int occupied = 0;
    int unknown = 0;
};

// A consistent copy of all slots and counts, taken under one lock.
struct Snapshot {
    std::vector<ParkingSlot> slots;
    SlotCounts counts;
};

enum class UpdateResult {
    OK_CHANGED,    // state changed, event recorded
    OK_UNCHANGED,  // same state as before: accepted, no event (FR-09)
    INVALID_SLOT   // slot id does not exist (FR-10)
};

class ParkingSlotManager {
public:
    // Creates slots with IDs 1..slotCount, all UNKNOWN.
    // 'listener' may be nullptr. The manager does not own it.
    // Throws std::invalid_argument if slotCount < 1.
    explicit ParkingSlotManager(int slotCount, IEventListener* listener = nullptr);

    // The single door through which every state change passes.
    UpdateResult updateSlot(int id, SlotState state, const std::string& source);

    // Copy of the current state. Safe to use after the lock is released.
    Snapshot getSnapshot() const;

    // Up to 'maxItems' most recent events, NEWEST FIRST.
    std::vector<ParkingEvent> getHistory(std::size_t maxItems) const;

private:
    static constexpr std::size_t kMaxHistory = 50;

    std::vector<ParkingSlot> slots_;
    std::deque<ParkingEvent> history_;
    mutable std::mutex mutex_;
    IEventListener* listener_;
};

#endif // PARKING_SLOT_MANAGER_H
