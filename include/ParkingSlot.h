#ifndef PARKING_SLOT_H
#define PARKING_SLOT_H

#include "SlotState.h"

// One parking slot: an ID and a state. A new slot starts UNKNOWN (FR-03).
class ParkingSlot {
public:
    explicit ParkingSlot(int id);

    int getId() const;
    SlotState getState() const;
    void setState(SlotState state);

private:
    int id_;
    SlotState state_;
};

#endif // PARKING_SLOT_H
