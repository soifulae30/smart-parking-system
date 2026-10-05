#!/usr/bin/env bash
# Delivery check: required files present, diagram count, and (with --full)
# a clean build plus the complete test suite.
cd "$(dirname "$0")/.." || exit 1

missing=0
need() {
    if [ -e "$1" ]; then echo "ok       $1"; else echo "MISSING  $1"; missing=$((missing + 1)); fi
}

FILES="
Makefile README.md .gitignore logs/.gitkeep web/index.html
docs/REQUIREMENTS.md docs/ARCHITECTURE.md docs/API.md docs/SYSTEM_PROGRAMMING.md docs/DEMO.md docs/SAMPLE_LOG.txt
scripts/demo.sh scripts/check_delivery.sh
include/ApiHandler.h include/ConsoleListener.h include/HttpProtocol.h include/IEventListener.h
include/MultiListener.h include/ParkingApplication.h include/ParkingEvent.h include/ParkingLogger.h
include/ParkingSlot.h include/ParkingSlotManager.h include/SensorReader.h include/SimulatedSensor.h
include/SlotState.h include/StatusServer.h include/TimeUtil.h
src/ApiHandler.cpp src/HttpProtocol.cpp src/ParkingApplication.cpp src/ParkingLogger.cpp
src/ParkingSlot.cpp src/ParkingSlotManager.cpp src/SimulatedSensor.cpp src/SlotState.cpp
src/StatusServer.cpp src/TimeUtil.cpp src/main.cpp
tests/TestFramework.h tests/TestSupport.h tests/test_main.cpp tests/test_application.cpp
tests/test_concurrency.cpp tests/test_http.cpp tests/test_logger.cpp tests/test_sensor.cpp
tests/test_server.cpp tests/test_slot_manager.cpp tests/test_slot_state.cpp
"
for f in $FILES; do need "$f"; done

echo
diagrams=$(grep -c '^```mermaid' docs/ARCHITECTURE.md 2>/dev/null || echo 0)
if [ "$diagrams" -eq 7 ]; then echo "ok       7 Mermaid diagrams in ARCHITECTURE.md"
else echo "PROBLEM  ARCHITECTURE.md has $diagrams Mermaid diagrams (expected 7)"; missing=$((missing + 1)); fi

if grep -q "Amendment 1" docs/REQUIREMENTS.md 2>/dev/null; then echo "ok       REQUIREMENTS.md has Amendment 1"
else echo "PROBLEM  REQUIREMENTS.md lacks Amendment 1"; missing=$((missing + 1)); fi

if [ "${1:-}" = "--full" ]; then
    echo; echo "--- full build and test ---"
    make clean && make && make test || { echo "BUILD OR TESTS FAILED"; exit 1; }
fi

echo; echo "problems: $missing"
[ "$missing" -eq 0 ]
