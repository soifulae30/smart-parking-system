# HTTP API Reference

Base URL: `http://127.0.0.1:8080` (configurable with `--addr` and `--port`).
One request per TCP connection (`Connection: close`). Responses are JSON
(`Content-Type: application/json`), except `GET /` (HTML).
Limits: request (headers plus body) up to 8 KiB, 5 s client timeout,
at most 64 simultaneous connections. Slot IDs start at 1.

## GET /api/status
Current counts and all slots, taken as one consistent snapshot.
```json
{"total":6,"available":1,"occupied":0,"unknown":5,"slots":[{"id":1,"state":"FREE"},{"id":2,"state":"UNKNOWN"}]}
```
`available` counts FREE slots only. Always `available + occupied + unknown = total`.

## GET /api/history
Up to 50 most recent state changes, newest first (in memory, lost on restart).
```json
{"events":[{"timestamp":"2026-10-04 10:15:02","slot":1,"old":"UNKNOWN","new":"FREE","source":"api"}]}
```
`source` is `api`, `sensor` or `terminal`. An empty history is `{"events":[]}`.

## POST /api/slots/{id}
Sets a slot's simulated state. The request must include `Content-Length`
(curl `-d` does this). Body: exactly `{"state":"FREE"}`, `{"state":"OCCUPIED"}`
or `{"state":"UNKNOWN"}` (case-insensitive value, whitespace allowed, no other keys).
```json
{"slot":1,"state":"FREE","changed":true}
```
`changed` is `false` when the slot already had that state (no history event, no log line).

## GET /
Serves `web/index.html` (a plain static page). Also served: `/style.css` and `/app.js`
only if those files exist in the web directory; they don't in this project (404).

## Errors
Body is always `{"error":"<message>"}`.

| Status | When |
|---|---|
| 400 | Malformed request; slot id not a whole number (1 to 6 digits); body not `{"state":"..."}`; state not FREE/OCCUPIED/UNKNOWN |
| 404 | Unknown path; slot id outside 1..N; a static file is missing |
| 405 | Known path, wrong method. `Allow` header lists the accepted method |
| 413 | Request over 8 KiB (headers or declared body) |
| 500 | Unexpected internal exception |
| 503 | More than 64 simultaneous connections |

## curl examples
```bash
curl -i http://127.0.0.1:8080/api/status
curl -i -X POST -d '{"state":"OCCUPIED"}' http://127.0.0.1:8080/api/slots/3
curl -s http://127.0.0.1:8080/api/history
curl -i -X POST -d '{"state":"BANANA"}' http://127.0.0.1:8080/api/slots/1   # 400
curl -i -X POST -d '{"state":"FREE"}' http://127.0.0.1:8080/api/slots/999   # 404
curl -i http://127.0.0.1:8080/api/slots/1                                   # 405, Allow: POST
```

## Concurrency note
Sensor updates and API updates use the same `updateSlot` call; the latest wins.
Start the server with `--sensor-ms 0` for fully deterministic API behaviour.
