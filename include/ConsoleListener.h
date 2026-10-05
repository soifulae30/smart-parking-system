#ifndef CONSOLE_LISTENER_H
#define CONSOLE_LISTENER_H

#include <iostream>
#include <mutex>
#include <string>

#include "IEventListener.h"
#include "SlotState.h"

// Prints events and errors to the terminal. Several threads may report at
// the same time, so each message is built first and written in one step
// under a mutex; otherwise lines from different threads would interleave.
class ConsoleListener : public IEventListener {
public:
    void onSlotChanged(const ParkingEvent& e) override {
        const std::string line = "[EVENT] " + e.timestamp +
                                 " slot=" + std::to_string(e.slotId) + " " +
                                 toString(e.oldState) + " -> " + toString(e.newState) +
                                 " source=" + e.source + "\n";
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << line << std::flush;
    }

    void onError(const std::string& message) override {
        const std::string line = "[ERROR] " + message + "\n";
        std::lock_guard<std::mutex> lock(mutex_);
        std::cerr << line << std::flush;
    }

private:
    std::mutex mutex_;
};

#endif // CONSOLE_LISTENER_H
