#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

// Helpers shared by the test files: a recording listener, temp files, and a
// minimal TCP/HTTP CLIENT (socket, connect, send, recv, close) used to talk
// to the real server.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "IEventListener.h"
#include "ParkingSlotManager.h"

// Remembers everything it is told, so tests can inspect it afterwards.
class RecordingListener : public IEventListener {
public:
    void onSlotChanged(const ParkingEvent& e) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(e);
    }
    void onError(const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        errors_.push_back(message);
    }
    std::vector<ParkingEvent> events() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }
    std::vector<std::string> errors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return errors_;
    }
    bool anyErrorContains(const std::string& text) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::string& e : errors_) {
            if (e.find(text) != std::string::npos) return true;
        }
        return false;
    }
private:
    mutable std::mutex mutex_;
    std::vector<ParkingEvent> events_;
    std::vector<std::string> errors_;
};

// Calls back into the manager from inside the notification. This only works
// if the manager released its mutex BEFORE notifying (NFR-07); otherwise the
// test deadlocks (and `timeout` in the Makefile ends it).
class ReentrantListener : public IEventListener {
public:
    ParkingSlotManager* manager = nullptr;
    std::atomic<int> callbacks{0};
    std::atomic<int> totalSeen{0};

    void onSlotChanged(const ParkingEvent&) override {
        if (manager != nullptr) {
            totalSeen = manager->getSnapshot().counts.total;
        }
        ++callbacks;
    }
    void onError(const std::string&) override {}
};

inline std::string uniqueTempPath(const std::string& suffix) {
    static std::atomic<int> counter{0};
    const std::string name = "smart_parking_test_" + std::to_string(::getpid()) + "_" +
                             std::to_string(counter++) + suffix;
    return (std::filesystem::temp_directory_path() / name).string();
}

inline std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

inline bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

inline int countOccurrences(const std::string& text, const std::string& needle) {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// ------------------------------------------------------------ TCP client

// Returns a connected socket to 127.0.0.1:port, or -1.
inline int connectLoopback(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    timeval tv{};
    tv.tv_sec = 3;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

inline void sendBytes(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;  // server may close early (e.g. after a 413): fine
        sent += static_cast<std::size_t>(n);
    }
}

inline std::string readUntilClose(int fd) {
    std::string out;
    char buffer[2048];
    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        out.append(buffer, static_cast<std::size_t>(n));
    }
    return out;
}

// One complete request/response over a fresh connection.
// Returns "" if the connection could not be made.
inline std::string httpExchange(int port, const std::string& rawRequest) {
    const int fd = connectLoopback(port);
    if (fd < 0) return "";
    sendBytes(fd, rawRequest);
    const std::string response = readUntilClose(fd);
    ::close(fd);
    return response;
}

inline std::string makeGet(const std::string& path) {
    return "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
}

inline std::string makePost(const std::string& path, const std::string& body) {
    return "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
}

inline int statusCodeOf(const std::string& response) {
    if (response.size() < 12 || response.compare(0, 5, "HTTP/") != 0) return -1;
    return std::atoi(response.c_str() + 9);  // "HTTP/1.1 " is 9 characters
}

inline std::string bodyOf(const std::string& response) {
    const std::size_t pos = response.find("\r\n\r\n");
    return pos == std::string::npos ? "" : response.substr(pos + 4);
}

// Extracts the integer after "key": in a flat JSON text, or -1.
inline int jsonInt(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\":";
    const std::size_t pos = json.find(pattern);
    if (pos == std::string::npos) return -1;
    return std::atoi(json.c_str() + pos + pattern.size());
}

// A listening socket that just holds a port, to provoke "address in use".
struct ListeningSocket {
    int fd = -1;
    int port = 0;
    ListeningSocket() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0;  // let the OS choose
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (fd >= 0 && ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
            ::listen(fd, 4) == 0) {
            socklen_t len = sizeof(addr);
            ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
            port = ntohs(addr.sin_port);
        }
    }
    ~ListeningSocket() { if (fd >= 0) ::close(fd); }
    ListeningSocket(const ListeningSocket&) = delete;
    ListeningSocket& operator=(const ListeningSocket&) = delete;
};

// A port that was free a moment ago (tiny race; fine for tests).
inline int freePort() {
    ListeningSocket s;
    return s.port;
}

inline bool waitForServer(int port, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        const int fd = connectLoopback(port);
        if (fd >= 0) {
            ::close(fd);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

#endif // TEST_SUPPORT_H
