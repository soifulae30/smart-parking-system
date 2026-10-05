#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include <vector>

#include "SlotState.h"

// One reading: "slot N currently looks like STATE".
struct SensorReading {
    int slotId = 0;
    SlotState state = SlotState::UNKNOWN;
};

// Interface for anything that can report slot readings: the simulator
// today, a hardware-backed reader in the future. It knows nothing about
// ParkingSlotManager (NFR-09).
class SensorReader {
public:
    virtual ~SensorReader() = default;

    // Returns the current reading of every slot.
    virtual std::vector<SensorReading> readAll() = 0;
};

#endif // SENSOR_READER_H
