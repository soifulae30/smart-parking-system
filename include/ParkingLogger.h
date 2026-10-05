#ifndef PARKING_LOGGER_H
#define PARKING_LOGGER_H

#include <fstream>
#include <mutex>
#include <string>

#include "IEventListener.h"

// Writes slot changes and errors, one line each, to a text file.
// If the file cannot be opened the object is still usable: it reports the
// problem on stderr and silently skips writes, so the application keeps
// running (see isOpen()).
class ParkingLogger : public IEventListener {
public:
    explicit ParkingLogger(const std::string& path);
    ~ParkingLogger() override;

    // A logger owns an open file and a mutex, so copying makes no sense.
    ParkingLogger(const ParkingLogger&) = delete;
    ParkingLogger& operator=(const ParkingLogger&) = delete;

    bool isOpen() const;
    const std::string& path() const;

    void onSlotChanged(const ParkingEvent& event) override;
    void onError(const std::string& message) override;

private:
    void writeLine(const std::string& line);

    std::string path_;
    std::ofstream file_;
    bool writeFailureReported_ = false;
    mutable std::mutex mutex_;
};

#endif // PARKING_LOGGER_H
