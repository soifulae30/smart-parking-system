# Demonstration Script and Defence Notes

## Before you start
```bash
make clean && make && make test    # expect 0 warnings and 63 passed
```

## Live demo (about 7 minutes)
1. **Show the layers** (docs/ARCHITECTURE.md diagram 1): sensor → manager → logger / server.
2. **Run the scripted demo:** `make demo`. It starts a real server, calls every endpoint including error cases, stops it with SIGTERM and shows the log.
3. **Point out in the output:**
   - all slots start UNKNOWN and `available` is 0 (FR-03, FR-08)
   - the same state twice gives `"changed":false` and no log line (FR-09)
   - 400 / 404 / 405 / 413 responses and the state staying unchanged
   - exit code 0 and `logging stopped` after SIGTERM (FR-24)
   - `source=sensor` lines in the second run (the sensor thread works)
4. **Show threads:** start the server, run `ls /proc/$(pgrep -n smart_parking)/task` and `ls -l /proc/$(pgrep -n smart_parking)/fd`.
5. **Show the tests:** `./build/run_tests manager_` and `make test`.
6. **Show the honest limits:** README "Deviations" and "Known limitations".

## Questions to prepare for
| Question | Answer in one line |
|---|---|
| Why is the sensor behind an interface? | So a hardware reader can replace the simulator without changing the manager or server (NFR-09) |
| Is SimulatedSensor a driver? | No. It is a user-space class. A real driver would be a kernel module exposing a /dev file; explained, not implemented |
| Why unlock before notifying the listener? | A slow log write must never block other threads, and a re-entrant listener must not deadlock (tested) |
| Why one thread per connection? | Simple and enough locally; capped at 64; a thread pool or epoll would scale better |
| What is a race condition here? | Two threads passing the "state changed?" check at once, producing duplicate events or corrupting the deque |
| What does `accept()` return? | A new file descriptor for that client; the listening fd stays open |
| What happens on kill -9? | No destructors run, no `logging stopped`, but the kernel still frees the port |
| Why no dashboard? | Project is C++ only; the JSON API provides the same data (Amendment 1) |
| What is not verified? | Browser behaviour (none exists), NFR-10 response time, automated SIGINT delivery |
