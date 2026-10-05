#ifndef STATUS_SERVER_H
#define STATUS_SERVER_H

#include <atomic>
#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <thread>

#include "ApiHandler.h"
#include "IEventListener.h"
#include "ParkingSlotManager.h"

// One connection handler thread plus a flag it sets when it has finished,
// so the accept loop knows when it is safe to join().
struct Worker {
    std::thread thread;
    std::atomic<bool> done{false};
};

// TCP/HTTP server on POSIX sockets. The thread that calls run() accepts
// connections; each connection is handled on its own worker thread.
class StatusServer {
public:
    // 'address' must be an IPv4 address such as "127.0.0.1".
    // 'port' 0 asks the OS for a free port (see port()).
    StatusServer(ParkingSlotManager& manager, IEventListener& events,
                 std::string address, int port, std::string webDir);
    ~StatusServer();

    StatusServer(const StatusServer&) = delete;
    StatusServer& operator=(const StatusServer&) = delete;

    // socket + setsockopt + bind + listen. Returns false (after reporting
    // the reason through the listener) on failure.
    bool start();

    // Accept loop. Returns when stop() is called or *stopFlag becomes true
    // (stopFlag may be nullptr). Joins every worker thread before returning.
    void run(const std::atomic<bool>* stopFlag);

    // Thread-safe: may be called from any thread.
    void stop();

    // The port actually bound (useful when 0 was requested).
    int port() const;

private:
    static constexpr std::size_t kMaxConnections = 64;

    void spawnHandler(int clientFd);
    void rejectBusy(int clientFd);
    void reapFinished(bool waitForAll);

    void handleClient(int clientFd);
    long receiveSome(int fd, std::string& data);
    bool sendAll(int fd, const std::string& data);
    void reply(int fd, const HttpResponse& response);
    bool fail(const char* what);

    IEventListener& events_;
    ApiHandler handler_;
    std::string address_;
    int port_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};

    // Touched ONLY by the thread that runs run(), so it needs no mutex.
    std::list<std::unique_ptr<Worker>> workers_;
};

#endif // STATUS_SERVER_H