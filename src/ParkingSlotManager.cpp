#include "ParkingSlotManager.h"

#include <stdexcept>

#include "TimeUtil.h"

ParkingSlotManager::ParkingSlotManager(int slotCount, IEventListener* listener)
    : listener_(listener) {
    if (slotCount < 1) {
        throw std::invalid_argument("slotCount must be at least 1");
    }
    for (int id = 1; id <= slotCount; ++id) {
        slots_.emplace_back(id);
    }
}

UpdateResult ParkingSlotManager::updateSlot(int id, SlotState state,
                                            const std::string& source) {
    ParkingEvent event;
    bool invalid = false;

    {   // ---- critical section: lock held only inside these braces ----
        std::lock_guard<std::mutex> lock(mutex_);

        if (id < 1 || id > static_cast<int>(slots_.size())) {
            invalid = true;
        } else {
            ParkingSlot& slot = slots_[static_cast<std::size_t>(id - 1)];
            if (slot.getState() == state) {
                return UpdateResult::OK_UNCHANGED;  // lock released automatically
            }
            event.timestamp = currentTimestamp();
            event.slotId = id;
            event.oldState = slot.getState();
            event.newState = state;
            event.source = source;

            slot.setState(state);
            history_.push_back(event);
            if (history_.size() > kMaxHistory) {
                history_.pop_front();
            }
        }
    }   // ---- lock released here ----

    // Notify AFTER unlocking so a slow listener cannot block other threads.
    if (invalid) {
        if (listener_ != nullptr) {
            listener_->onError("invalid slot id " + std::to_string(id) +
                               " (source: " + source + ")");
        }
        return UpdateResult::INVALID_SLOT;
    }

    if (listener_ != nullptr) {
        listener_->onSlotChanged(event);
    }
    return UpdateResult::OK_CHANGED;
}

Snapshot ParkingSlotManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Snapshot snap;
    snap.slots = slots_;  // copy while locked
    snap.counts.total = static_cast<int>(slots_.size());
    for (const ParkingSlot& slot : slots_) {
        switch (slot.getState()) {
            case SlotState::FREE:     ++snap.counts.available; break;
            case SlotState::OCCUPIED: ++snap.counts.occupied;  break;
            case SlotState::UNKNOWN:  ++snap.counts.unknown;   break;
        }
    }
    return snap;  // copy leaves; caller no longer touches shared data
}

std::vector<ParkingEvent> ParkingSlotManager::getHistory(std::size_t maxItems) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ParkingEvent> result;
    for (auto it = history_.rbegin(); it != history_.rend() && result.size() < maxItems; ++it) {
        result.push_back(*it);  // rbegin = newest first
    }
    return result;
}