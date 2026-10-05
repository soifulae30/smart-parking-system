#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "ConsoleListener.h"
#include "IEventListener.h"
#include "MultiListener.h"
#include "ParkingApplication.h"
#include "ParkingLogger.h"
#include "ParkingSlotManager.h"
#include "SensorReader.h"
#include "SimulatedSensor.h"
#include "SlotState.h"

namespace {

const int kDefaultSlots = 6;
const int kMinSlots = 4;    // FR-01
const int kMaxSlots = 100;
const char* const kDefaultLogPath = "logs/parking.log";

// Set from the signal handler, read by the server thread. A lock-free atomic
// is safe in a handler AND safe to read from any thread (unlike a plain
// volatile variable once several threads exist).
std::atomic<bool> gStopRequested{false};
static_assert(std::atomic<bool>::is_always_lock_free,
              "atomic<bool> must be lock-free to be used in a signal handler");

void onSignal(int) { gStopRequested.store(true); }

// Parses a whole string as an int. Rejects "12abc", "", "3 4", overflow.
bool parseInt(const std::string& text, int& out) {
    std::istringstream in(text);
    in >> out;
    if (in.fail()) return false;
    char extra;
    if (in >> extra) return false;  // leftover non-space characters
    return true;
}

void printUsage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " [slots " << kMinSlots << ".." << kMaxSlots
              << "] [seed] [logfile]          terminal menu\n"
              << "  " << program << " serve [--port N] [--addr IPv4] [--slots N]\n"
              << "          [--log FILE] [--web DIR] [--sensor-ms N] [--seed N]   HTTP server\n"
              << "          (--sensor-ms 0 turns the sensor thread off)\n";
}

// ---------------------------------------------------------------- terminal

// Prompts for a slot id. Returns false on bad input or end of input.
bool askSlotId(IEventListener& events, int& id) {
    std::string line;
    std::cout << "Slot id: ";
    if (!std::getline(std::cin, line)) return false;
    if (!parseInt(line, id)) {
        events.onError("slot id must be a whole number");
        return false;
    }
    return true;
}

void printStatus(const ParkingSlotManager& manager) {
    Snapshot snap = manager.getSnapshot();
    std::cout << "\n=== Parking Status ===\n";
    for (const ParkingSlot& slot : snap.slots) {
        std::cout << "Slot " << slot.getId() << ": " << toString(slot.getState()) << '\n';
    }
    std::cout << "Total: " << snap.counts.total
              << " | Available: " << snap.counts.available
              << " | Occupied: " << snap.counts.occupied
              << " | Unknown: " << snap.counts.unknown << '\n';
}

void printHistory(const ParkingSlotManager& manager) {
    std::vector<ParkingEvent> events = manager.getHistory(10);
    std::cout << "\n=== Recent Events (newest first) ===\n";
    if (events.empty()) {
        std::cout << "(none)\n";
    }
    for (const ParkingEvent& e : events) {
        std::cout << e.timestamp << " slot=" << e.slotId << " "
                  << toString(e.oldState) << " -> " << toString(e.newState)
                  << " source=" << e.source << '\n';
    }
}

// Manual override: bypasses the sensor and goes straight to the manager.
void setSlotInteractively(ParkingSlotManager& manager, IEventListener& events) {
    int id = 0;
    if (!askSlotId(events, id)) return;

    std::string line;
    std::cout << "State (FREE / OCCUPIED / UNKNOWN): ";
    if (!std::getline(std::cin, line)) return;
    SlotState state = SlotState::UNKNOWN;
    if (!parseState(line, state)) {
        events.onError("invalid state '" + line + "'");
        return;
    }

    UpdateResult result = manager.updateSlot(id, state, "terminal");
    if (result == UpdateResult::OK_UNCHANGED) {
        std::cout << "(no change: slot " << id << " is already " << toString(state) << ")\n";
    }
    // OK_CHANGED and INVALID_SLOT are reported by the listeners.
}

// The glue between the sensor layer and the business layer.
// It sees only the SensorReader INTERFACE, not SimulatedSensor.
void pollAndApply(SensorReader& reader, ParkingSlotManager& manager) {
    std::vector<SensorReading> readings = reader.readAll();

    std::cout << "Sensor reported:";
    for (const SensorReading& r : readings) {
        std::cout << ' ' << r.slotId << ':' << toString(r.state);
    }
    std::cout << '\n';

    int changes = 0;
    for (const SensorReading& r : readings) {
        UpdateResult result = manager.updateSlot(r.slotId, r.state, "sensor");
        if (result == UpdateResult::OK_CHANGED) ++changes;
    }
    std::cout << "Poll complete: " << changes << " change(s)\n";
}

void printMenu() {
    std::cout << "\n--- Smart Parking (terminal) ---\n"
              << "1) Show status\n"
              << "2) Set a slot's state (manual override)\n"
              << "3) Show recent events\n"
              << "4) Simulate: vehicle arrives in a slot\n"
              << "5) Simulate: vehicle leaves a slot\n"
              << "6) Simulate: sensor fault in a slot\n"
              << "7) Poll sensor now\n"
              << "8) Toggle random sensor noise\n"
              << "0) Quit\n"
              << "Choice: ";
}

int runTerminal(int argc, char* argv[]) {
    int slotCount = kDefaultSlots;
    unsigned seed = std::random_device{}();
    std::string logPath = kDefaultLogPath;

    if (argc > 4) {
        printUsage(argv[0]);
        return 1;
    }
    if (argc > 1) {
        if (!parseInt(argv[1], slotCount) || slotCount < kMinSlots || slotCount > kMaxSlots) {
            printUsage(argv[0]);
            return 1;
        }
    }
    if (argc > 2) {
        int seedValue = 0;
        if (!parseInt(argv[2], seedValue) || seedValue < 0) {
            printUsage(argv[0]);
            return 1;
        }
        seed = static_cast<unsigned>(seedValue);
    }
    if (argc > 3) {
        logPath = argv[3];
    }

    // Declaration order matters: destruction runs in REVERSE order.
    ConsoleListener console;
    ParkingLogger logger(logPath);
    if (!logger.isOpen()) {
        std::cerr << "[WARN] file logging is disabled; continuing without it\n";
    }
    MultiListener events;
    events.add(&console);
    events.add(&logger);

    ParkingSlotManager manager(slotCount, &events);

    SimulatedSensor sim(slotCount, seed);  // concrete type: manual controls
    SensorReader& reader = sim;            // abstract view: what the glue sees

    std::cout << "Smart Parking started with " << slotCount << " slots.\n";
    if (logger.isOpen()) {
        std::cout << "Logging to " << logger.path() << '\n';
    }

    std::string line;
    while (true) {
        printMenu();
        if (!std::getline(std::cin, line)) break;  // Ctrl+D / end of input

        int choice = -1;
        if (!parseInt(line, choice)) {
            events.onError("menu input is not a number: '" + line + "'");
            continue;
        }
        if (choice == 0) {
            break;
        } else if (choice == 1) {
            printStatus(manager);
        } else if (choice == 2) {
            setSlotInteractively(manager, events);
        } else if (choice == 3) {
            printHistory(manager);
        } else if (choice == 4 || choice == 5 || choice == 6) {
            int id = 0;
            if (!askSlotId(events, id)) continue;
            bool ok = false;
            if (choice == 4) ok = sim.vehicleArrives(id);
            else if (choice == 5) ok = sim.vehicleLeaves(id);
            else ok = sim.injectFault(id);
            if (!ok) {
                events.onError("sensor: no such slot " + std::to_string(id));
            } else {
                std::cout << "(sensor changed; the manager learns about it on the next poll)\n";
            }
        } else if (choice == 7) {
            pollAndApply(reader, manager);
        } else if (choice == 8) {
            sim.setNoiseEnabled(!sim.isNoiseEnabled());
            std::cout << "Sensor noise is now " << (sim.isNoiseEnabled() ? "ON" : "OFF") << '\n';
        } else {
            events.onError("unknown menu option " + std::to_string(choice));
        }
    }

    std::cout << "Goodbye.\n";
    return 0;
}

// ------------------------------------------------------------------ server

// Options come in "--key value" pairs after the word "serve".
bool parseServeOptions(int argc, char* argv[], AppConfig& o) {
    for (int i = 2; i < argc; i += 2) {
        const std::string key = argv[i];
        if (i + 1 >= argc) {
            std::cerr << "[ERROR] option " << key << " needs a value\n";
            return false;
        }
        const std::string value = argv[i + 1];

        if (key == "--port") {
            if (!parseInt(value, o.port) || o.port < 1 || o.port > 65535) {
                std::cerr << "[ERROR] --port must be 1..65535\n";
                return false;
            }
        } else if (key == "--slots") {
            if (!parseInt(value, o.slots) || o.slots < kMinSlots || o.slots > kMaxSlots) {
                std::cerr << "[ERROR] --slots must be " << kMinSlots << ".." << kMaxSlots << '\n';
                return false;
            }
        } else if (key == "--sensor-ms") {
            if (!parseInt(value, o.sensorMs) || o.sensorMs < 0 || o.sensorMs > 60000) {
                std::cerr << "[ERROR] --sensor-ms must be 0..60000\n";
                return false;
            }
        } else if (key == "--seed") {
            int seedValue = 0;
            if (!parseInt(value, seedValue) || seedValue < 0) {
                std::cerr << "[ERROR] --seed must be a non-negative whole number\n";
                return false;
            }
            o.seed = static_cast<unsigned>(seedValue);
        } else if (key == "--addr") {
            o.address = value;
        } else if (key == "--log") {
            o.logPath = value;
        } else if (key == "--web") {
            o.webDir = value;
        } else {
            std::cerr << "[ERROR] unknown option " << key << '\n';
            return false;
        }
    }
    return true;
}

int runServer(const AppConfig& config) {
    // SIGINT = Ctrl+C, SIGTERM = `kill`. No SA_RESTART: we WANT blocking
    // calls to be interrupted so loops notice the flag promptly.
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    try {
        ParkingApplication app(config);
        return app.run(&gStopRequested);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] fatal: " << e.what() << '\n';
        return 1;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "serve") {
        AppConfig config;
        if (!parseServeOptions(argc, argv, config)) {
            printUsage(argv[0]);
            return 1;
        }
        return runServer(config);
    }
    return runTerminal(argc, argv);
}