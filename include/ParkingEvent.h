#ifndef PARKING_EVENT_H
#define PARKING_EVENT_H

#include <string>

#include "SlotState.h"

// A record of one state change. Stored in history and sent to listeners.
struct ParkingEvent {
    std::string timestamp;  // "YYYY-MM-DD HH:MM:SS" (local time)
    int slotId = 0;
    SlotState oldState = SlotState::UNKNOWN;
    SlotState newState = SlotState::UNKNOWN;
    std::string source;     // who caused it: "sensor", "dashboard", "terminal"
};

#endif // PARKING_EVENT_H
