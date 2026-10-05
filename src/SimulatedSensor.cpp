#include "SimulatedSensor.h"

#include <cstddef>
#include <stdexcept>

SimulatedSensor::SimulatedSensor(int slotCount, unsigned seed)
    : rng_(seed) {
    if (slotCount < 1) {
        throw std::invalid_argument("slotCount must be at least 1");
    }
    current_.assign(static_cast<std::size_t>(slotCount), SlotState::UNKNOWN);
}

std::vector<SensorReading> SimulatedSensor::readAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (noiseEnabled_) {
        for (SlotState& state : current_) {
            int roll = percent_(rng_);
            if (state == SlotState::UNKNOWN) {
                if (roll < kRecoverPercent) {
                    // Sensor starts reporting: pick FREE or OCCUPIED.
                    state = (percent_(rng_) < 50) ? SlotState::FREE
                                                  : SlotState::OCCUPIED;
                }
            } else if (roll < kFaultPercent) {
                state = SlotState::UNKNOWN;                    // fault
            } else if (roll < kFaultPercent + kTogglePercent) {
                state = (state == SlotState::FREE) ? SlotState::OCCUPIED
                                                   : SlotState::FREE;
            }
        }
    }

    std::vector<SensorReading> readings;
    readings.reserve(current_.size());
    for (std::size_t i = 0; i < current_.size(); ++i) {
        SensorReading r;
        r.slotId = static_cast<int>(i) + 1;
        r.state = current_[i];
        readings.push_back(r);
    }
    return readings;
}

bool SimulatedSensor::vehicleArrives(int slotId) {
    return setSlot(slotId, SlotState::OCCUPIED);
}

bool SimulatedSensor::vehicleLeaves(int slotId) {
    return setSlot(slotId, SlotState::FREE);
}

bool SimulatedSensor::injectFault(int slotId) {
    return setSlot(slotId, SlotState::UNKNOWN);
}

void SimulatedSensor::setNoiseEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    noiseEnabled_ = enabled;
}

bool SimulatedSensor::isNoiseEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return noiseEnabled_;
}

bool SimulatedSensor::setSlot(int slotId, SlotState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slotId < 1 || slotId > static_cast<int>(current_.size())) {
        return false;
    }
    current_[static_cast<std::size_t>(slotId - 1)] = state;
    return true;
}
