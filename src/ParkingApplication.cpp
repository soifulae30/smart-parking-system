#include "ParkingApplication.h"

#include <chrono>
#include <exception>
#include <iostream>
#include <map>

ParkingApplication::ParkingApplication(const AppConfig& config)
    : config_(config),
      logger_(config.logPath),
      manager_(config.slots, &events_),   // stores only the pointer; events_ is filled below
      sensor_(config.slots, config.seed),
      server_(manager_, events_, config.address, config.port, config.webDir) {
    events_.add(&console_);
    events_.add(&logger_);
    if (!logger_.isOpen()) {
        std::cerr << "[WARN] file logging is disabled; continuing without it\n";
    }
}

ParkingApplication::~ParkingApplication() {
    stopSensor();  // a joinable std::thread must never be destroyed
}

int ParkingApplication::port() const { return server_.port(); }

void ParkingApplication::requestShutdown() { server_.stop(); }

int ParkingApplication::run(const std::atomic<bool>* stopFlag) {
    if (!server_.start()) {
        return 1;  // reason already printed and logged
    }

    if (config_.address != "127.0.0.1") {
        std::cerr << "[WARN] listening on " << config_.address
                  << ": other machines may be able to connect, and there is no authentication\n";
    }
    std::cout << "Smart Parking server: http://" << config_.address << ':' << server_.port()
              << "/  (" << config_.slots << " slots, sensor ";
    if (config_.sensorMs > 0) std::cout << "every " << config_.sensorMs << " ms";
    else std::cout << "off";
    std::cout << ", Ctrl+C to stop)" << std::endl;

    if (config_.sensorMs > 0) {
        try {
            sensorThread_ = std::thread(&ParkingApplication::sensorLoop, this);
        } catch (const std::exception& e) {
            console_.onError(std::string("cannot start sensor thread: ") + e.what());
            return 1;
        }
    }

    server_.run(stopFlag);  // blocks here; returns after joining all workers

    stopSensor();
    std::cout << "\nShutting down..." << std::endl;
    return 0;
}

void ParkingApplication::stopSensor() {
    sensorStop_ = true;
    if (sensorThread_.joinable()) {
        sensorThread_.join();
    }
}

// Runs on the sensor thread. It sees the sensor only through SensorReader and
// the business layer only through updateSlot(): the two never meet directly.
void ParkingApplication::sensorLoop() {
    try {
        SensorReader& reader = sensor_;
        std::map<int, SlotState> lastReported;  // private to this thread: no lock needed

        while (!sensorStop_) {
            for (const SensorReading& r : reader.readAll()) {
                auto it = lastReported.find(r.slotId);
                if (it != lastReported.end() && it->second == r.state) {
                    continue;  // sensor says nothing new about this slot
                }
                lastReported[r.slotId] = r.state;
                manager_.updateSlot(r.slotId, r.state, "sensor");
            }
            // Sleep in 50 ms slices so shutdown does not wait a full interval.
            for (int waited = 0; waited < config_.sensorMs && !sensorStop_; waited += 50) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    } catch (const std::exception& e) {
        console_.onError(std::string("sensor thread stopped: ") + e.what());
    }
}
