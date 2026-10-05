#ifndef MULTI_LISTENER_H
#define MULTI_LISTENER_H

#include <string>
#include <vector>

#include "IEventListener.h"

// Forwards every notification to several listeners (console + file, ...).
// Call add() only during start-up, before other threads exist; after that
// the list is read-only, so no mutex is needed.
// The listeners are not owned and must outlive this object.
class MultiListener : public IEventListener {
public:
    void add(IEventListener* listener) {
        if (listener != nullptr) listeners_.push_back(listener);
    }

    void onSlotChanged(const ParkingEvent& event) override {
        for (IEventListener* l : listeners_) l->onSlotChanged(event);
    }

    void onError(const std::string& message) override {
        for (IEventListener* l : listeners_) l->onError(message);
    }

private:
    std::vector<IEventListener*> listeners_;
};

#endif // MULTI_LISTENER_H
