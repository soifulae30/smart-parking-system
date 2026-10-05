#ifndef SIMULATED_SENSOR_H
#define SIMULATED_SENSOR_H

#include <mutex>
#include <random>
#include <vector>

#include "SensorReader.h"
#include "SlotState.h"

// Software stand-in for physical parking sensors. NOT a device driver:
// it uses no hardware and no kernel code.
class SimulatedSensor : public SensorReader {
public:
    // Slots 1..slotCount, all UNKNOWN until they "report".
    // Throws std::invalid_argument if slotCount < 1.
    explicit SimulatedSensor(int slotCount,
                             unsigned seed = std::random_device{}());

    // Applies random behaviour (if noise is enabled), then returns the
    // reading of every slot.
    std::vector<SensorReading> readAll() override;

    // Manual controls. Each returns false if slotId does not exist.
    bool vehicleArrives(int slotId);   // slot becomes OCCUPIED
    bool vehicleLeaves(int slotId);    // slot becomes FREE
    bool injectFault(int slotId);      // slot becomes UNKNOWN (sensor fault)

    void setNoiseEnabled(bool enabled);
    bool isNoiseEnabled() const;

private:
    bool setSlot(int slotId, SlotState state);

    // Per poll, per slot, in percent (0..100).
    static constexpr int kTogglePercent  = 15;  // FREE <-> OCCUPIED
    static constexpr int kFaultPercent   = 3;   // FREE/OCCUPIED -> UNKNOWN
    static constexpr int kRecoverPercent = 50;  // UNKNOWN -> FREE/OCCUPIED

    std::vector<SlotState> current_;  // index 0 = slot 1
    std::mt19937 rng_;
    std::uniform_int_distribution<int> percent_{0, 99};
    bool noiseEnabled_ = true;
    mutable std::mutex mutex_;
};

#endif // SIMULATED_SENSOR_H
