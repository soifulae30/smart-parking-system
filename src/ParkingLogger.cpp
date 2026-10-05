#include "ParkingLogger.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>

#include "SlotState.h"
#include "TimeUtil.h"

namespace {

const std::size_t kMaxMessageLength = 500;

// Replaces control characters (newline, tab, ...) with spaces and truncates.
// Prevents one message from forging extra log lines.
std::string sanitize(const std::string& text) {
    std::string out;
    for (char c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        out += (u < 0x20 || u == 0x7F) ? ' ' : c;
        if (out.size() >= kMaxMessageLength) {
            out += "...";
            break;
        }
    }
    return out;
}

}  // namespace

ParkingLogger::ParkingLogger(const std::string& path) : path_(path) {
    // ios::app = append mode (O_APPEND): never overwrite earlier runs.
    file_.open(path_, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[ERROR] cannot open log file '" << path_ << "': "
                  << std::strerror(errno) << '\n';
        return;
    }
    writeLine(currentTimestamp() + " [INFO] logging started");
}

ParkingLogger::~ParkingLogger() {
    writeLine(currentTimestamp() + " [INFO] logging stopped");
    if (file_.is_open()) {
        file_.close();  // would happen automatically; explicit for clarity
    }
}

bool ParkingLogger::isOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_.is_open();
}

const std::string& ParkingLogger::path() const { return path_; }

void ParkingLogger::onSlotChanged(const ParkingEvent& e) {
    writeLine(e.timestamp + " [INFO] slot=" + std::to_string(e.slotId) + " " +
              toString(e.oldState) + " -> " + toString(e.newState) +
              " source=" + sanitize(e.source));
}

void ParkingLogger::onError(const std::string& message) {
    writeLine(currentTimestamp() + " [ERROR] " + sanitize(message));
}

void ParkingLogger::writeLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) {
        return;  // logging disabled; the open failure was already reported
    }
    file_ << line << '\n';
    file_.flush();  // hand the text to the kernel now
    if (file_.fail()) {
        if (!writeFailureReported_) {  // report once, not on every line
            std::cerr << "[ERROR] failed writing to log file '" << path_ << "'\n";
            writeFailureReported_ = true;
        }
        file_.clear();  // reset error flags so a later write can retry
    }
}
