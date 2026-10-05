# Smart Parking System: Requirements Document

Version: 1.0 | Status: Draft (Phase 1)

## 1. Problem Statement
A parking facility needs to know, at any moment, which slots are free,
occupied, or of unknown status, and to present that information to users.
Physical sensors are not available for this project, so sensor input is
simulated in software. The system must run locally on Linux with no
database, cloud service, or internet connection.

## 2. Objectives
- O1: Model a parking facility with 4 or more slots.
- O2: Keep a single, correct, thread-safe view of all slot states.
- O3: Accept simulated sensor readings and manual overrides.
- O4: Expose state through a local HTTP API and a browser dashboard.
- O5: Record every occupancy change and every error in a text log.
- O6: Demonstrate C++17, POSIX sockets, threads, Make, Git, GDB, and testing.

## 3. Scope
In scope: simulated sensors, slot manager, logger, HTTP server (POSIX
sockets), HTML/CSS/JS dashboard, tests, Makefile, documentation, UML.
Out of scope: physical hardware, kernel drivers, databases, cloud,
containers, authentication, HTTPS, multi-process deployment.

## 4. Definitions
- Slot: one parking space with an integer ID and a state.
- FREE: sensor reports no vehicle.
- OCCUPIED: sensor reports a vehicle.
- UNKNOWN: no valid reading (initial state, or sensor fault).
- Available count: number of FREE slots only.

## 5. Functional Requirements
| ID | Requirement |
|----|-------------|
| FR-01 | The system shall manage at least 4 parking slots (default 6, configurable). |
| FR-02 | Each slot shall have an integer ID and exactly one state: FREE, OCCUPIED, or UNKNOWN. |
| FR-03 | All slots shall start in the UNKNOWN state until a first valid reading arrives. |
| FR-04 | The system shall display all slots and their states. |
| FR-05 | The system shall simulate a vehicle entering (-> OCCUPIED) and leaving (-> FREE). |
| FR-06 | A simulated sensor reading that differs from a slot's current state shall update that slot automatically. |
| FR-07 | The system shall compute total, available, occupied, and unknown counts. |
| FR-08 | UNKNOWN slots shall NOT be counted as available. |
| FR-09 | Updating a slot to its current state shall be accepted but shall not create a history event or a change log line. |
| FR-10 | Updating a nonexistent slot ID shall be rejected with an error. |
| FR-11 | Updating a slot to an invalid state string shall be rejected with an error. |
| FR-12 | Every state change shall be written to a local log file with timestamp, slot ID, old state, and new state. |
| FR-13 | Errors (invalid input, file errors, socket errors) shall be written to the log file. |
| FR-14 | The server shall provide GET / returning the HTML dashboard. |
| FR-15 | The server shall provide GET /api/status returning slot states and counts as JSON. |
| FR-16 | The server shall provide GET /api/history returning recent occupancy events as JSON. |
| FR-17 | The server shall provide POST /api/slots/{id} that sets a slot's simulated state. |
| FR-18 | The dashboard shall show total, available, occupied, and unknown counts. |
| FR-19 | The dashboard shall show one card/row per slot with a control to change its simulated state. |
| FR-20 | The dashboard shall show recent occupancy changes. |
| FR-21 | The dashboard shall show an error message if the API is unreachable. |
| FR-22 | Dashboard actions shall change the C++ application's state, not only the page text. |
| FR-23 | The system shall support concurrent sensor updates and client requests without data races. |
| FR-24 | The application shall shut down gracefully on SIGINT or SIGTERM (stop threads, close sockets, flush and close log). |
| FR-25 | The server shall return correct HTTP status codes (200, 400, 404, 405, 413, 500 as applicable). |

## 6. Non-Functional Requirements
| ID | Requirement |
|----|-------------|
| NFR-01 | Language: C++17, compiled with g++, built with Make. |
| NFR-02 | Platform: Linux (Ubuntu, including WSL2). |
| NFR-03 | The server shall bind to 127.0.0.1 by default; address and port are configurable. |
| NFR-04 | Request size shall be limited (default 8 KiB) to prevent memory abuse. |
| NFR-05 | No database, cloud, Node.js, or frontend framework dependency. |
| NFR-06 | The code shall compile without warnings under -Wall -Wextra. |
| NFR-07 | Shared state shall be protected by std::mutex; no mutex shall be held during socket writes or file writes. |
| NFR-08 | Resources (sockets, files, threads) shall be managed with RAII; no leaks. |
| NFR-09 | The sensor layer shall be replaceable without modifying business logic. |
| NFR-10 | A status request should respond within 100 ms under normal local load. |
| NFR-11 | Documentation shall match the final implementation. |

## 7. Assumptions
- A1: One instance of the application runs at a time.
- A2: Users access the dashboard from the same machine.
- A3: The log file directory is writable by the current user.
- A4: Sensor-simulator changes and dashboard overrides both go through the
      same manager update method; the most recent update wins.
- A5: Recent history is kept in memory (bounded, newest N events) and is
      lost on restart. The log file is the permanent record.

## 8. Limitations
- No persistence of slot state across restarts.
- No authentication or encryption; local use only.
- HTTP server is minimal: no keep-alive, no chunked encoding, no TLS.
- No real hardware or kernel driver; the sensor is a software simulator only.

## 9. Acceptance Criteria
| ID | Criterion | Covers |
|----|-----------|--------|
| AC-01 | After startup, GET /api/status reports total = N, unknown = N, available = 0. | FR-01, 03, 07, 08 |
| AC-02 | After POST /api/slots/1 {"state":"FREE"}, available = 1 and unknown = N-1. | FR-07, 08, 17 |
| AC-03 | POST to /api/slots/999 returns 404 and the state is unchanged. | FR-10 |
| AC-04 | POST with state "BANANA" returns 400 and the state is unchanged. | FR-11 |
| AC-05 | Setting a slot to OCCUPIED then FREE creates two history events and two log lines. | FR-09, 12, 16 |
| AC-06 | Setting a slot to its current state creates no new event. | FR-09 |
| AC-07 | Clicking a dashboard control changes the value later returned by GET /api/status. | FR-19, 22 |
| AC-08 | Stopping the server makes the dashboard show an error message. | FR-21 |
| AC-09 | 50 concurrent curl updates plus a running simulator finish with consistent counts (occupied + free + unknown = total). | FR-23 |
| AC-10 | Ctrl+C exits with status 0, closes the port, and leaves a complete log. | FR-24 |
| AC-11 | A request larger than the limit gets 413. | NFR-04 |
| AC-12 | `make test` runs all automated tests and they pass. | NFR-01, 06 |

## 10. Feature Checklist
- [ ] FR-01..04 Slot model and display
- [ ] FR-05..06 Simulated sensor
- [ ] FR-07..11 Counts and validation
- [ ] FR-12..13 Logging
- [ ] FR-14..17, 25 HTTP server and API
- [ ] FR-18..22 Dashboard
- [ ] FR-23..24 Concurrency and shutdown
- [ ] Tests, Makefile, README, UML, demo script

## 11. Risks
| Risk | Mitigation |
|------|------------|
| Deadlock or data race | Single mutex in the manager; copy snapshots out before I/O |
| Port already in use | Configurable port; clear error message |
| Log file not writable | Report on stderr; keep running without file logging |
| Malformed HTTP input | Strict parsing, size limits, 400 responses |

cat >> docs/REQUIREMENTS.md << 'EOF'

## 12. Amendment 1 (Phase 12): C++-only delivery

Reason: the project must use C++ only, so the HTML/CSS/JavaScript browser
dashboard was not built. [Add the source of this instruction here, e.g.
"trainer instruction dated ..." or the capstone brief wording.]
Phase 8 was skipped. The JSON API and the C++ test suite replace it.

### 12.1 Changed requirements
| ID | Original | Amended status |
|----|----------|----------------|
| FR-04 | Display all slots | Satisfied by terminal menu option 1 and `GET /api/status` |
| FR-14 | `GET /` returns the HTML dashboard | `GET /` returns a plain static HTML page (no scripts) listing the endpoints |
| FR-18 | Dashboard shows counts | NOT IMPLEMENTED (no dashboard). Counts are in `GET /api/status` |
| FR-19 | Per-slot card with a control | NOT IMPLEMENTED. Slots are changed with `POST /api/slots/{id}` |
| FR-20 | Dashboard shows recent changes | NOT IMPLEMENTED. Available in `GET /api/history` |
| FR-21 | Dashboard shows error if API unreachable | NOT IMPLEMENTED. Clients get a connection error when the server is stopped |
| FR-22 | Dashboard actions change C++ state | Reworded: an API request changes the live C++ slot manager |
| AC-07 | Dashboard click changes `/api/status` | Replaced: `POST /api/slots/{id}` changes the value later returned by `GET /api/status` |
| AC-08 | Dashboard shows error when server stops | Replaced: after shutdown the port refuses connections |
| FR-25 | Codes 200, 400, 404, 405, 413, 500 | Also 503 (more than 64 simultaneous connections) |

### 12.2 Added behaviour
| ID | Requirement |
|----|-------------|
| FR-26 | A terminal menu mode allows manual operation without the server |
| FR-27 | A sensor thread polls the sensor and reports only slots whose reading changed |
| FR-28 | Updates through the API are recorded with source "api"; sensor updates "sensor"; terminal "terminal" |

### 12.3 Traceability and verification status
"Auto" = covered by `make test` (test names in docs/API.md and tests/). "Manual" = verified by running commands.
Confirm each row against your own runs before the evaluation.

| Requirement | Status | Evidence |
|---|---|---|
| FR-01..03 slots, states, UNKNOWN start | Verified (auto) | manager_initialStateAllUnknown, api_statusInitialExactJson |
| FR-05, FR-06 simulate and auto-update | Verified (auto, manual) | sensor_*, app_runsServesAndShutsDownGracefully, terminal menu |
| FR-07, FR-08 counts, UNKNOWN excluded | Verified (auto) | manager_availableCountsOnlyFree, manager_unknownIsNeverAvailable |
| FR-09 same-state update, no event | Verified (auto) | manager_sameStateIsAcceptedWithoutEvent, api_postValidUpdatesChangeState |
| FR-10, FR-11 invalid slot / state | Verified (auto) | manager_invalidSlotIdsRejected, api_postBadInput..., server_rejectsInvalid... |
| FR-12, FR-13 logging | Verified (auto) | logger_*, app_portInUseReturnsErrorAndLogsIt |
| FR-14..17, FR-25 HTTP and API | Verified (auto, manual curl) | http_*, api_*, server_* |
| FR-18..21 dashboard | NOT IMPLEMENTED | see 12.1 |
| FR-22 API changes live state | Verified (auto) | server_postChangesTheRealApplicationState |
| FR-23 concurrency | Verified (auto, TSan run optional) | concurrency_*, server_manyConcurrent..., app_clientsSee... |
| FR-24 graceful shutdown | Verified (auto via stop flag; manual signal) | app_*, Phase 11 experiment 4 |
| NFR-01..03 | Verified | build flags, default 127.0.0.1 |
| NFR-04 size limit | Verified (auto) | server_oversizedBody..., server_oversizedHeaders... |
| NFR-05 no DB, cloud, frameworks | Met by design | no such dependency in the Makefile or code |
| NFR-06 no warnings | Verified (build output) | `make` and `make test` |
| NFR-07 no lock during I/O | Verified (auto) | manager_notifiesListenerOutsideItsLock, concurrency_reentrant... |
| NFR-08 RAII | By design, ASan/TSan runs | FdGuard, ofstream, lock_guard, joined threads |
| NFR-09 replaceable sensor | Verified (auto) | sensor_readingsFlowThroughInterfaceIntoManager |
| NFR-10 100 ms response | NOT MEASURED | tests assert loose bounds only |
| NFR-11 docs match code | Checked at delivery | `make check` |
| AC-01..06 | Verified (auto) | api_* and server_* tests |
| AC-07, AC-08 | Replaced | see 12.1 |
| AC-09 concurrent consistency | Verified (auto) | server_manyConcurrentClientsStayConsistent, app_clientsSee... |
| AC-10 Ctrl+C exit 0, log complete | Verified (auto via flag; manual signal) | app_*, Phase 11 experiment 4 |
| AC-11 oversized request 413 | Verified (auto) | server_oversizedBodyGets413AndServerSurvives |
| AC-12 `make test` passes | Verified | 63 of 63 |
EOF
