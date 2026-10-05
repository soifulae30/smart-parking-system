#ifndef PARKING_APPLICATION_H
#define PARKING_APPLICATION_H

#include <atomic>
#include <random>
#include <string>
#include <thread>

#include "ConsoleListener.h"
#include "MultiListener.h"
#include "ParkingLogger.h"
#include "ParkingSlotManager.h"
#include "SimulatedSensor.h"
#include "StatusServer.h"

struct AppConfig {
    std::string address = "127.0.0.1";   // loopback only by default (NFR-03)
    int port = 8080;
    int slots = 6;
    std::string logPath = "logs/parking.log";
    std::string webDir = "web";
    int sensorMs = 2000;                 // poll interval; 0 disables the sensor thread
    unsigned seed = std::random_device{}();
};

// Creates every component, connects them, runs the sensor thread and the
// HTTP server, and shuts everything down in the right order.
class ParkingApplication {
public:
    explicit ParkingApplication(const AppConfig& config);
    ~ParkingApplication();

    ParkingApplication(const ParkingApplication&) = delete;
    ParkingApplication& operator=(const ParkingApplication&) = delete;

    // Starts the server and the sensor thread, then serves until *stopFlag
    // becomes true or requestShutdown() is called. Returns a process exit code.
    int run(const std::atomic<bool>* stopFlag);

    // Thread-safe: asks run() to return.
    void requestShutdown();

    // The port actually bound (valid after run() started the server).
    int port() const;

private:
    void sensorLoop();
    void stopSensor();

    // MEMBER ORDER MATTERS. Members are constructed top to bottom and
    // destroyed bottom to top. Everything a member uses is declared above it.
    AppConfig config_;
    ConsoleListener console_;
    ParkingLogger logger_;
    MultiListener events_;
    ParkingSlotManager manager_;
    SimulatedSensor sensor_;
    StatusServer server_;
    std::thread sensorThread_;
    std::atomic<bool> sensorStop_{false};
};

#endif // PARKING_APPLICATION_H
