# Smart Parking System

A software-only smart parking simulator in C++17 for Linux. Simulated sensors
update parking-slot states, changes are logged to a text file, and a small
HTTP server built directly on POSIX sockets exposes a JSON API.

No database, cloud service, internet connection or hardware is needed.

## Features
- 4 to 100 slots (default 6), states FREE / OCCUPIED / UNKNOWN. UNKNOWN is never counted as available.
- Simulated sensor with random arrivals, departures and faults (seedable for repeatable runs).
- Thread-safe slot manager: sensor thread and client threads share one mutex-protected state.
- Occupancy changes and errors written to a local log file.
- HTTP API: `GET /api/status`, `GET /api/history`, `POST /api/slots/{id}`, `GET /`.
- Terminal menu mode for manual testing.
- Graceful shutdown on SIGINT / SIGTERM.
- 63 automated tests (unit, integration, system, concurrency), plus sanitizer builds.

## Requirements
Ubuntu (including WSL2), g++ with C++17, make, git, gdb (optional). `curl` for the demo.

## Build
```bash
make            # builds build/smart_parking
make test       # builds and runs the test suite
make test-asan  # tests with AddressSanitizer + UBSan
make test-tsan  # tests with ThreadSanitizer
make demo       # scripted demonstration against a real server
make check      # delivery check (files, docs); `make check` runs the full build and tests too
make clean
```

## Run
Server mode (listens on 127.0.0.1:8080 by default):
```bash
./build/smart_parking serve [--port N] [--addr IPv4] [--slots N] [--log FILE]
                            [--web DIR] [--sensor-ms N] [--seed N]
```
`--sensor-ms 0` turns the sensor thread off (useful for predictable API tests).
Stop with Ctrl+C or `kill -TERM`.

Terminal mode:
```bash
./build/smart_parking [slots] [seed] [logfile]
```

Quick check (second terminal):
```bash
curl -s http://127.0.0.1:8080/api/status
curl -s -X POST -d '{"state":"FREE"}' http://127.0.0.1:8080/api/slots/1
curl -s http://127.0.0.1:8080/api/history
```
Full API reference: `docs/API.md`.

## Project layout
```
include/  headers            src/    sources (main.cpp is only the entry point)
tests/    test suite         web/    static page served at GET /
docs/     requirements, architecture, API, system programming, demo
scripts/  demo.sh, check_delivery.sh      logs/   runtime log (parking.log)
```

## Documentation
| File | Content |
|---|---|
| docs/REQUIREMENTS.md | Requirements, acceptance criteria, Amendment 1 and traceability |
| docs/ARCHITECTURE.md | Layers and Mermaid UML diagrams |
| docs/API.md | HTTP API reference |
| docs/SYSTEM_PROGRAMMING.md | Linux system programming and driver concepts |
| docs/DEMO.md | Demonstration script and defence notes |
| docs/SAMPLE_LOG.txt | Log produced by `make demo` |

## Deviations from the original brief
- **No browser dashboard.** The project is C++ only (see REQUIREMENTS.md, Amendment 1).
  The same information is available through the JSON API. `GET /` serves a plain static page listing the endpoints.
- **No kernel driver.** `SimulatedSensor` is a user-space class, not a device driver.
  A real driver was explained conceptually (docs/SYSTEM_PROGRAMMING.md) and not implemented.

## Known limitations
- State and history are in memory only. History keeps the newest 50 events. Slot states reset on restart; the log file is the permanent record.
- No authentication or encryption. Loopback (127.0.0.1) by default; other addresses print a warning.
- Minimal HTTP/1.1: one request per connection (no keep-alive), 8 KiB request limit, IPv4 only, request bodies must be `{"state":"..."}` (a small purpose-built parser, not general JSON).
- One thread per connection with a cap of 64; extra connections get 503.
- The sensor and the API both write slots; the latest update wins. Use `--sensor-ms 0` for deterministic API use.
- Log file is flushed to the kernel on every line but not `fsync`ed, and is never rotated.
- Some tests are timing-based; they use generous bounds but could rarely fail on a heavily loaded machine.
- Real SIGINT/SIGTERM delivery is verified manually (see docs/SYSTEM_PROGRAMMING.md and `make demo`). The automated tests use the stop flag the handler sets.
- Response-time target NFR-10 was not measured.
- Developed and tested on Ubuntu under WSL2 only.