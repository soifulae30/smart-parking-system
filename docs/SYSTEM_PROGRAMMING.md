# Smart Parking: Linux System Programming and Driver Concepts

Version 1.0 (Phase 11). Describes what the implemented software actually uses
and what is only explained conceptually. Observations were made by the
student on their own machine; fill in the "Observed" lines in section 5.

## 1. Implemented and used
| Topic | Where | Evidence |
|---|---|---|
| Process, PID, address space | whole program | /proc/PID/status |
| Threads (clone3) | sensor thread, one worker per connection | /proc/PID/task, strace -f |
| File descriptors | log file, listening socket, client sockets | /proc/PID/fd |
| File I/O syscalls | ParkingLogger (openat O_APPEND, write, close) | strace |
| Socket syscalls | StatusServer (socket..close) | strace, ss |
| Signals | SIGINT/SIGTERM via sigaction; MSG_NOSIGNAL; EINTR loops | kill, SigCgt |
| Synchronization | std::mutex (futex under contention), std::atomic | strace -f |
| errno handling | fail(), receiveSome(), sendAll(), ParkingLogger | log lines |

## 2. Not used in this project
fork/exec, pipes/FIFOs, message queues, shared memory, semaphores, mmap, ioctl,
epoll/select. Threads share memory directly; clients use TCP.

## 3. Not implemented (conceptual only, outside the software-only scope)
- Kernel modules (insmod/rmmod/lsmod, module_init/module_exit)
- Character-device drivers (file_operations, cdev, major/minor, copy_to_user)
- Interrupts, DMA, GPIO, device tree

SimulatedSensor is an ordinary user-space C++ class. It is NOT a device
driver and was not tested against any hardware. A future DeviceSensorReader
(implementing SensorReader and reading a /dev node) would replace it without
changes to the manager or server. That class does not exist.

## 4. Why the simulator is a legitimate substitute
It reproduces the interface the application needs (a list of slot readings,
including UNKNOWN for a fault) so every layer above it can be built and tested.
It does not reproduce timing, electrical noise, bus errors or driver failures.

## 5. Observations (fill in)
- Threads while idle (sensor on): ___   during a request: ___
- fd numbers: log file ___, listening socket ___
- Syscalls seen for one curl request: ___
- SigCgt value: ___
- kill -TERM: exit code ___, last log line ___
- kill -KILL: exit code ___, last log line ___
