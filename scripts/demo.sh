#!/usr/bin/env bash
# Scripted demonstration. Starts a real server (sensor off), exercises the API,
# stops it with SIGTERM, then runs a short sensor-on session. Uses logs/demo.log.
set -u
cd "$(dirname "$0")/.." || exit 1

PORT="${PORT:-8089}"
LOG=logs/demo.log
CONSOLE=logs/demo_console.log
BIN=./build/smart_parking
BASE="http://127.0.0.1:$PORT"

[ -x "$BIN" ] || { echo "Build first: make"; exit 1; }
command -v curl >/dev/null || { echo "curl is required (sudo apt install curl)"; exit 1; }

step() { printf '\n=== %s ===\n' "$*"; }
code() { curl -s -o /dev/null -w "HTTP %{http_code}\n" "$@"; }

wait_up() {   # wait up to 3 s for the server to answer
    for _ in $(seq 1 30); do
        curl -s -o /dev/null "$BASE/api/status" && return 0
        kill -0 "$PID" 2>/dev/null || return 1
        sleep 0.1
    done
    return 1
}

rm -f "$LOG" "$CONSOLE"

step "1. Start server (sensor off, port $PORT)"
"$BIN" serve --port "$PORT" --sensor-ms 0 --log "$LOG" --web web >"$CONSOLE" 2>&1 &
PID=$!
trap 'kill "$PID" 2>/dev/null' EXIT
wait_up || { echo "Server did not start. Console output:"; cat "$CONSOLE"; exit 1; }
echo "server pid $PID"

step "2. Initial status (all UNKNOWN, available 0)"
curl -s "$BASE/api/status"; echo

step "3. Vehicle enters slot 1, slot 2 becomes free"
curl -s -X POST -d '{"state":"OCCUPIED"}' "$BASE/api/slots/1"; echo
curl -s -X POST -d '{"state":"FREE"}' "$BASE/api/slots/2"; echo
curl -s "$BASE/api/status"; echo

step "4. Same state again (changed:false, no log line)"
curl -s -X POST -d '{"state":"OCCUPIED"}' "$BASE/api/slots/1"; echo

step "5. Sensor fault: slot 2 becomes UNKNOWN (not counted as available)"
curl -s -X POST -d '{"state":"UNKNOWN"}' "$BASE/api/slots/2"; echo
curl -s "$BASE/api/status"; echo

step "6. History (newest first)"
curl -s "$BASE/api/history"; echo

step "7. Error cases (state must stay unchanged)"
echo -n "bad state:   "; code -X POST -d '{"state":"BANANA"}' "$BASE/api/slots/1"
echo -n "no such slot: "; code -X POST -d '{"state":"FREE"}' "$BASE/api/slots/999"
echo -n "bad slot id: "; code -X POST -d '{"state":"FREE"}' "$BASE/api/slots/abc"
echo -n "wrong method: "; code "$BASE/api/slots/1"
echo -n "unknown path: "; code "$BASE/nothing"
echo -n "too large:    "
head -c 20000 /dev/zero | tr '\0' 'a' | curl -s -o /dev/null -w "HTTP %{http_code}\n" \
    -X POST --data-binary @- "$BASE/api/slots/1"
curl -s "$BASE/api/status"; echo

step "8. Graceful shutdown with SIGTERM"
kill -TERM "$PID"
wait "$PID"; echo "exit code: $?"
echo -n "port closed: "
curl -s -o /dev/null "$BASE/api/status" && echo "NO (still answering)" || echo "yes"

step "9. Second run: sensor thread on (poll every 200 ms, seed 42)"
"$BIN" serve --port "$PORT" --sensor-ms 200 --seed 42 --log "$LOG" --web web >>"$CONSOLE" 2>&1 &
PID=$!
wait_up || { echo "second start failed"; cat "$CONSOLE"; exit 1; }
sleep 2
curl -s "$BASE/api/status"; echo
kill -TERM "$PID"
wait "$PID"; echo "exit code: $?"

step "10. Log file ($LOG)"
cat "$LOG"
echo
echo "Copy it into the docs with:  cp $LOG docs/SAMPLE_LOG.txt"
