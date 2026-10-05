#include "StatusServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <system_error>
#include <utility>

#include "HttpProtocol.h"

namespace {

const std::size_t kMaxRequestBytes = 8 * 1024;  // NFR-04
const long kClientTimeoutMs = 5000;
const long kLingerTimeoutMs = 200;
const std::size_t kMaxDrainBytes = 64 * 1024;
const int kListenBacklog = 16;
const int kPollTimeoutMs = 200;

// RAII for a file descriptor: the destructor always closes it.
class FdGuard {
public:
    explicit FdGuard(int fd) : fd_(fd) {}
    ~FdGuard() { if (fd_ >= 0) ::close(fd_); }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
private:
    int fd_;
};

void setSocketTimeout(int fd, long ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

}  // namespace

StatusServer::StatusServer(ParkingSlotManager& manager, IEventListener& events,
                           std::string address, int port, std::string webDir)
    : events_(events),
      handler_(manager, events, std::move(webDir)),
      address_(std::move(address)),
      port_(port) {}

StatusServer::~StatusServer() {
    reapFinished(true);  // never destroy a joinable std::thread (it would terminate)
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
}

bool StatusServer::fail(const char* what) {
    const int err = errno;  // capture first: later calls may change errno
    events_.onError(std::string(what) + " on " + address_ + ":" +
                    std::to_string(port_) + ": " + std::strerror(err));
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    return false;
}

bool StatusServer::start() {
    if (port_ < 0 || port_ > 65535) {
        events_.onError("invalid port " + std::to_string(port_));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    if (inet_pton(AF_INET, address_.c_str(), &addr.sin_addr) != 1) {
        events_.onError("invalid IPv4 address '" + address_ + "'");
        return false;
    }

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) return fail("socket() failed");

    const int yes = 1;
    if (::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        return fail("setsockopt(SO_REUSEADDR) failed");
    }
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return fail("bind() failed");
    }
    if (::listen(listenFd_, kListenBacklog) < 0) {
        return fail("listen() failed");
    }

    running_ = true;
    return true;
}

int StatusServer::port() const {
    if (listenFd_ < 0) return port_;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        return ntohs(addr.sin_port);
    }
    return port_;
}

void StatusServer::stop() { running_ = false; }

// Joins workers that have finished (or ALL of them if waitForAll).
void StatusServer::reapFinished(bool waitForAll) {
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (waitForAll || (*it)->done.load()) {
            if ((*it)->thread.joinable()) (*it)->thread.join();
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
}

// Starts a worker thread that owns clientFd from now on.
void StatusServer::spawnHandler(int clientFd) {
    auto worker = std::make_unique<Worker>();
    Worker* raw = worker.get();  // stays valid: the object lives on the heap
    try {
        worker->thread = std::thread([this, clientFd, raw]() {
            try {
                handleClient(clientFd);
            } catch (const std::exception& e) {
                events_.onError(std::string("worker error: ") + e.what());
            } catch (...) {
                events_.onError("worker error: unknown exception");
            }
            raw->done = true;  // last action: tells the accept loop we can be joined
        });
    } catch (const std::system_error& e) {
        ::close(clientFd);  // no thread owns it, so we must close it
        events_.onError(std::string("cannot create thread: ") + e.what());
        return;
    }
    workers_.push_back(std::move(worker));
}

// Too many live connections: answer 503 right away and close.
void StatusServer::rejectBusy(int clientFd) {
    FdGuard guard(clientFd);
    setSocketTimeout(clientFd, kLingerTimeoutMs);
    events_.onError("too many simultaneous connections; rejecting one");
    sendAll(clientFd, serialize(makeJsonError(503, "server busy")));
}

void StatusServer::run(const std::atomic<bool>* stopFlag) {
    while (running_ && !(stopFlag != nullptr && stopFlag->load())) {
        reapFinished(false);  // join workers that already finished

        // poll() waits up to 200 ms for a connection, so we notice stop
        // requests quickly even if the signal landed on another thread.
        pollfd pfd{};
        pfd.fd = listenFd_;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, kPollTimeoutMs);
        if (ready < 0) {
            if (errno == EINTR) continue;  // a signal arrived: re-check the flag
            events_.onError(std::string("poll() failed: ") + std::strerror(errno));
            break;
        }
        if (ready == 0) continue;  // timeout, nobody connected

        const int clientFd = ::accept(listenFd_, nullptr, nullptr);
        if (clientFd < 0) {
            const int err = errno;
            if (err == EINTR || err == EAGAIN || err == ECONNABORTED) continue;
            events_.onError(std::string("accept() failed: ") + std::strerror(err));
            ::usleep(100000);  // avoid a busy loop on persistent errors (e.g. EMFILE)
            continue;
        }

        if (workers_.size() >= kMaxConnections) {
            rejectBusy(clientFd);
        } else {
            spawnHandler(clientFd);
        }
    }

    reapFinished(true);  // graceful shutdown: wait for every in-flight request
}

// Returns bytes appended; 0 = peer closed; -1 = error (already reported).
long StatusServer::receiveSome(int fd, std::string& data) {
    const bool hadData = !data.empty();
    char buffer[1024];
    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            const int err = errno;
            if (err == EINTR) continue;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                // Browsers open idle "speculative" connections; stay quiet for those.
                if (hadData) events_.onError("client timed out while sending request");
            } else {
                events_.onError(std::string("recv() failed: ") + std::strerror(err));
            }
            return -1;
        }
        data.append(buffer, static_cast<std::size_t>(n));
        return static_cast<long>(n);
    }
}

bool StatusServer::sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        // MSG_NOSIGNAL: if the client vanished, return EPIPE instead of
        // killing the whole process with SIGPIPE.
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            const int err = errno;
            if (err == EINTR) continue;
            events_.onError(std::string("send() failed: ") + std::strerror(err));
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void StatusServer::reply(int fd, const HttpResponse& response) {
    sendAll(fd, serialize(response));

    // Orderly close: tell the client we are done sending, then discard any
    // request bytes we never read. Closing a socket that still holds unread
    // data makes Linux send a reset, which can destroy our response (e.g. a 413).
    ::shutdown(fd, SHUT_WR);
    setSocketTimeout(fd, kLingerTimeoutMs);
    char scratch[1024];
    std::size_t drained = 0;
    while (drained < kMaxDrainBytes) {
        const ssize_t n = ::recv(fd, scratch, sizeof(scratch), 0);
        if (n <= 0) break;
        drained += static_cast<std::size_t>(n);
    }
}

// Runs on a worker thread. Touches only: its own locals, events_ (thread-safe
// listeners) and handler_.handle() (const; talks to the mutex-protected manager).
void StatusServer::handleClient(int clientFd) {
    FdGuard guard(clientFd);  // closes clientFd on every return path
    setSocketTimeout(clientFd, kClientTimeoutMs);

    // 1. Read until the blank line that ends the headers.
    std::string data;
    std::size_t headEnd = std::string::npos;
    while (headEnd == std::string::npos) {
        if (data.size() >= kMaxRequestBytes) {
            events_.onError("request headers exceed size limit");
            reply(clientFd, makeJsonError(413, "request too large"));
            return;
        }
        const long n = receiveSome(clientFd, data);
        if (n <= 0) return;  // closed or error (error already reported)
        headEnd = data.find("\r\n\r\n");
    }

    // 2. Parse the request line and headers.
    HttpHead head;
    if (!parseHead(data.substr(0, headEnd), head)) {
        events_.onError("malformed HTTP request");
        reply(clientFd, makeJsonError(400, "malformed request"));
        return;
    }

    // 3. Enforce the size limit BEFORE reading a large body.
    const std::size_t bodyStart = headEnd + 4;
    if (bodyStart + head.contentLength > kMaxRequestBytes) {
        events_.onError("request body exceeds size limit");
        reply(clientFd, makeJsonError(413, "request too large"));
        return;
    }

    // 4. Read the rest of the body (TCP may deliver it in pieces).
    while (data.size() < bodyStart + head.contentLength) {
        const long n = receiveSome(clientFd, data);
        if (n == 0) events_.onError("client closed connection before sending full body");
        if (n <= 0) return;
    }

    HttpRequest request;
    request.method = head.method;
    request.path = head.path;
    request.body = data.substr(bodyStart, head.contentLength);

    // 5. Route it. No mutex is held here or while sending (NFR-07).
    HttpResponse response;
    try {
        response = handler_.handle(request);
    } catch (const std::exception& e) {
        events_.onError(std::string("internal error: ") + e.what());
        response = makeJsonError(500, "internal server error");
    }
    reply(clientFd, response);
}