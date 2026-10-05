# Smart Parking System: Architecture and UML

Version 2.0 (Phase 12). Matches the implemented code. Supersedes version 1.0.
Differences from the Phase 2 design: HTTP logic split into `HttpProtocol` and
`ApiHandler`; `ParkingApplication` holds members by value; added
`MultiListener`, `ConsoleListener`, `TimeUtil`, `AppConfig`, `Worker`;
the browser dashboard was not built (see REQUIREMENTS.md Amendment 1).

## 1. System architecture

```mermaid
flowchart TB
    Client["HTTP client (curl or any program)"]
    subgraph Process["smart_parking process (single Linux process)"]
        MAIN["main.cpp<br/>argument parsing, signal handler, terminal menu"]
        APP["ParkingApplication<br/>owns and wires everything"]
        subgraph Net["Network / API layer"]
            SS["StatusServer<br/>accept loop + Worker threads"]
            API["ApiHandler<br/>routing, validation, JSON"]
            HP["HttpProtocol<br/>parse, serialize, escape"]
        end
        subgraph Biz["Business logic"]
            PM["ParkingSlotManager<br/>mutex-protected state and history"]
        end
        subgraph Input["Input abstraction"]
            SR["SensorReader (interface)"]
            SIM["SimulatedSensor"]
        end
        subgraph Out["Notification and persistence"]
            ML["MultiListener"]
            CL["ConsoleListener"]
            LG["ParkingLogger"]
        end
    end
    LOG[("logs/parking.log")]
    MAIN --> APP
    Client -- "HTTP over TCP 127.0.0.1:PORT" --> SS
    SS --> API
    SS --> HP
    API --> HP
    API -- "getSnapshot / getHistory / updateSlot" --> PM
    APP -- "sensor thread: readAll()" --> SR
    SIM -. implements .-> SR
    APP -- "updateSlot(id, state, sensor)" --> PM
    PM -- "onSlotChanged / onError (after unlock)" --> ML
    ML --> CL
    ML --> LG
    LG -- "ofstream append" --> LOG
```

## 2. Use cases

```mermaid
flowchart LR
    Client["API client / operator"]
    Sim["Sensor simulator (software actor)"]
    Dev["Developer / tester"]
    subgraph System["Smart Parking System"]
        UC1(["View slots and counts"])
        UC2(["View recent history"])
        UC3(["Set a slot's simulated state"])
        UC4(["Auto-update slot from sensor reading"])
        UC5(["Record change or error in log"])
        UC6(["Use the terminal menu"])
        UC7(["Shut down gracefully"])
    end
    Client --> UC1
    Client --> UC2
    Client --> UC3
    Sim --> UC4
    Dev --> UC6
    Dev --> UC7
    UC3 -. includes .-> UC5
    UC4 -. includes .-> UC5
    UC6 -. includes .-> UC5
```

## 3. Class diagram

```mermaid
classDiagram
    class SlotState {
        <<enumeration>>
        FREE
        OCCUPIED
        UNKNOWN
    }
    class UpdateResult {
        <<enumeration>>
        OK_CHANGED
        OK_UNCHANGED
        INVALID_SLOT
    }
    class ParkingSlot {
        -int id_
        -SlotState state_
        +ParkingSlot(int id)
        +getId() int
        +getState() SlotState
        +setState(SlotState s) void
    }
    class ParkingEvent {
        +string timestamp
        +int slotId
        +SlotState oldState
        +SlotState newState
        +string source
    }
    class SlotCounts {
        +int total
        +int available
        +int occupied
        +int unknown
    }
    class Snapshot {
        +vector~ParkingSlot~ slots
        +SlotCounts counts
    }
    class IEventListener {
        <<interface>>
        +onSlotChanged(ParkingEvent e) void
        +onError(string message) void
    }
    class ParkingSlotManager {
        -vector~ParkingSlot~ slots_
        -deque~ParkingEvent~ history_
        -mutex mutex_
        -IEventListener* listener_
        +ParkingSlotManager(int slotCount, IEventListener* l)
        +updateSlot(int id, SlotState s, string source) UpdateResult
        +getSnapshot() Snapshot
        +getHistory(size_t maxItems) vector~ParkingEvent~
    }
    class SensorReading {
        +int slotId
        +SlotState state
    }
    class SensorReader {
        <<interface>>
        +readAll() vector~SensorReading~
    }
    class SimulatedSensor {
        -vector~SlotState~ current_
        -mt19937 rng_
        -bool noiseEnabled_
        -mutex mutex_
        +SimulatedSensor(int slotCount, unsigned seed)
        +readAll() vector~SensorReading~
        +vehicleArrives(int id) bool
        +vehicleLeaves(int id) bool
        +injectFault(int id) bool
        +setNoiseEnabled(bool b) void
        +isNoiseEnabled() bool
    }
    class ParkingLogger {
        -string path_
        -ofstream file_
        -mutex mutex_
        +ParkingLogger(string path)
        +isOpen() bool
        +path() string
        +onSlotChanged(ParkingEvent e) void
        +onError(string message) void
    }
    class ConsoleListener {
        -mutex mutex_
        +onSlotChanged(ParkingEvent e) void
        +onError(string message) void
    }
    class MultiListener {
        -vector~IEventListener*~ listeners_
        +add(IEventListener* l) void
        +onSlotChanged(ParkingEvent e) void
        +onError(string message) void
    }
    class HttpRequest {
        +string method
        +string path
        +string body
    }
    class HttpResponse {
        +int status
        +string contentType
        +string body
        +string allow
    }
    class ApiHandler {
        -ParkingSlotManager& manager_
        -IEventListener& events_
        -string webDir_
        +ApiHandler(ParkingSlotManager& m, IEventListener& e, string webDir)
        +handle(HttpRequest r) HttpResponse
    }
    class Worker {
        +thread thread
        +atomic~bool~ done
    }
    class StatusServer {
        -IEventListener& events_
        -ApiHandler handler_
        -int listenFd_
        -atomic~bool~ running_
        -list~Worker~ workers_
        +StatusServer(ParkingSlotManager& m, IEventListener& e, string addr, int port, string webDir)
        +start() bool
        +run(atomic~bool~* stopFlag) void
        +stop() void
        +port() int
        -handleClient(int fd) void
    }
    class AppConfig {
        +string address
        +int port
        +int slots
        +string logPath
        +string webDir
        +int sensorMs
        +unsigned seed
    }
    class ParkingApplication {
        -AppConfig config_
        -ConsoleListener console_
        -ParkingLogger logger_
        -MultiListener events_
        -ParkingSlotManager manager_
        -SimulatedSensor sensor_
        -StatusServer server_
        -thread sensorThread_
        -atomic~bool~ sensorStop_
        +ParkingApplication(AppConfig c)
        +run(atomic~bool~* stopFlag) int
        +requestShutdown() void
        +port() int
    }
    ParkingSlot --> SlotState
    ParkingSlotManager "1" *-- "many" ParkingSlot : owns
    ParkingSlotManager ..> ParkingEvent : creates
    ParkingSlotManager ..> Snapshot : returns
    ParkingSlotManager ..> UpdateResult : returns
    Snapshot *-- SlotCounts
    ParkingSlotManager o--> IEventListener : notifies
    ParkingLogger ..|> IEventListener
    ConsoleListener ..|> IEventListener
    MultiListener ..|> IEventListener
    MultiListener o--> IEventListener : forwards to
    SimulatedSensor ..|> SensorReader
    SensorReader ..> SensorReading : returns
    ApiHandler --> ParkingSlotManager : uses
    ApiHandler ..> HttpRequest : reads
    ApiHandler ..> HttpResponse : returns
    StatusServer *-- ApiHandler
    StatusServer *-- Worker
    ParkingApplication *-- AppConfig
    ParkingApplication *-- ConsoleListener
    ParkingApplication *-- ParkingLogger
    ParkingApplication *-- MultiListener
    ParkingApplication *-- ParkingSlotManager
    ParkingApplication *-- SimulatedSensor
    ParkingApplication *-- StatusServer
    ParkingApplication ..> SensorReader : reads via interface
```

Free functions not shown as classes: `toString`/`parseState` (SlotState),
`parseHead`, `serialize`, `jsonEscape`, `makeJsonError`, `reasonPhrase`
(HttpProtocol), and `currentTimestamp` (TimeUtil).

## 4. Slot state machine

```mermaid
stateDiagram-v2
    [*] --> UNKNOWN : application starts
    UNKNOWN --> FREE : reading says empty
    UNKNOWN --> OCCUPIED : reading says vehicle
    FREE --> OCCUPIED : vehicle enters
    OCCUPIED --> FREE : vehicle leaves
    FREE --> UNKNOWN : sensor fault or API sets UNKNOWN
    OCCUPIED --> UNKNOWN : sensor fault or API sets UNKNOWN
    FREE --> FREE : same-state update (accepted, no event)
    OCCUPIED --> OCCUPIED : same-state update (accepted, no event)
    UNKNOWN --> UNKNOWN : same-state update (accepted, no event)
```

## 5. Sequence: vehicle enters a slot (sensor thread)

```mermaid
sequenceDiagram
    participant App as ParkingApplication (sensor thread)
    participant Sen as SimulatedSensor (via SensorReader)
    participant Mgr as ParkingSlotManager
    participant ML as MultiListener
    participant Log as ParkingLogger
    participant File as parking.log
    App->>Sen: readAll()
    Sen-->>App: readings for every slot
    loop each reading that differs from the last one reported
        App->>Mgr: updateSlot(3, OCCUPIED, "sensor")
        Mgr->>Mgr: lock, validate id, compare state
        alt state differs
            Mgr->>Mgr: setState, push event to history, unlock
            Mgr->>ML: onSlotChanged(event)
            ML->>Log: onSlotChanged(event)
            Log->>File: append and flush one line
            Mgr-->>App: OK_CHANGED
        else same state
            Mgr->>Mgr: unlock
            Mgr-->>App: OK_UNCHANGED (no event, no log line)
        end
    end
```

## 6. Sequence: client requests parking status

```mermaid
sequenceDiagram
    participant C as HTTP client
    participant K as Linux kernel (TCP)
    participant S as StatusServer (accept thread)
    participant W as Worker thread
    participant A as ApiHandler
    participant Mgr as ParkingSlotManager
    C->>K: connect to 127.0.0.1:PORT
    K-->>S: accept() returns clientFd
    S->>W: spawn worker owning clientFd
    C->>W: GET /api/status HTTP/1.1
    W->>W: recv loop, size limit, parseHead
    W->>A: handle(request)
    A->>Mgr: getSnapshot()
    Mgr-->>A: Snapshot (copy, lock already released)
    A-->>W: HttpResponse 200 with JSON
    W->>C: send(serialized response)
    W->>K: shutdown, drain, close(clientFd)
```

## 7. Sequence: client changes a slot

```mermaid
sequenceDiagram
    participant C as HTTP client
    participant W as Worker thread
    participant A as ApiHandler
    participant Mgr as ParkingSlotManager
    participant ML as MultiListener
    C->>W: POST /api/slots/2 with {"state":"FREE"}
    W->>A: handle(request)
    A->>A: validate slot id, body format, state value
    alt input invalid
        A->>ML: onError(reason) via events
        A-->>W: 400 JSON error
    else input valid
        A->>Mgr: updateSlot(2, FREE, "api")
        alt slot does not exist
            Mgr->>ML: onError
            Mgr-->>A: INVALID_SLOT
            A-->>W: 404
        else slot exists
            Mgr->>ML: onSlotChanged (only if the state changed)
            Mgr-->>A: OK_CHANGED or OK_UNCHANGED
            A-->>W: 200 with changed true or false
        end
    end
    W->>C: send response and close
```