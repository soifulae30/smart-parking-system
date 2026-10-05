#ifndef I_EVENT_LISTENER_H
#define I_EVENT_LISTENER_H

#include <string>

#include "ParkingEvent.h"

// Interface (all functions pure virtual). The manager reports changes and
// errors through it without knowing who is listening. In Phase 6,
// ParkingLogger will implement it.
class IEventListener {
public:
    virtual ~IEventListener() = default;  // virtual: safe deletion via base pointer

    virtual void onSlotChanged(const ParkingEvent& event) = 0;
    virtual void onError(const std::string& message) = 0;
};

#endif // I_EVENT_LISTENER_H
