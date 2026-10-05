#include "ApiHandler.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

#include "SlotState.h"

struct StaticFile {
    const char* urlPath;
    const char* fileName;
    const char* contentType;
};

namespace {

const std::size_t kHistoryLimit = 50;
const std::string kSlotsPrefix = "/api/slots/";

// Only these files can ever be served: no path traversal is possible.
const StaticFile kStaticFiles[] = {
    {"/",          "index.html", "text/html; charset=utf-8"},
    {"/style.css", "style.css",  "text/css; charset=utf-8"},
    {"/app.js",    "app.js",     "application/javascript; charset=utf-8"},
};

HttpResponse methodNotAllowed(const std::string& allowed) {
    HttpResponse r = makeJsonError(405, "method not allowed");
    r.allow = allowed;
    return r;
}

// Accepts 1 to 6 digits only ("3"), rejects "", "abc", "-1", "3x", " 3".
bool parseSlotId(const std::string& text, int& id) {
    if (text.empty() || text.size() > 6) return false;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    id = std::stoi(text);
    return true;
}

// Accepts exactly {"state":"<text>"} with optional whitespace. Rejects extra
// keys, escapes, control characters and values over 16 characters.
bool parseStateBody(const std::string& body, std::string& value) {
    std::size_t i = 0;
    auto skip = [&]() {
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
    };
    auto expect = [&](char c) {
        if (i < body.size() && body[i] == c) { ++i; return true; }
        return false;
    };

    skip();
    if (!expect('{')) return false;
    skip();
    if (body.compare(i, 7, "\"state\"") != 0) return false;
    i += 7;
    skip();
    if (!expect(':')) return false;
    skip();
    if (!expect('"')) return false;

    const std::size_t start = i;
    while (i < body.size() && body[i] != '"') {
        const unsigned char c = static_cast<unsigned char>(body[i]);
        if (c == '\\' || c < 0x20) return false;
        ++i;
    }
    if (i >= body.size()) return false;  // no closing quote
    value = body.substr(start, i - start);
    if (value.size() > 16) return false;
    ++i;  // closing quote

    skip();
    if (!expect('}')) return false;
    skip();
    return i == body.size();  // nothing after the object
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

}  // namespace

ApiHandler::ApiHandler(ParkingSlotManager& manager, IEventListener& events,
                       std::string webDir)
    : manager_(manager), events_(events), webDir_(std::move(webDir)) {}

HttpResponse ApiHandler::handle(const HttpRequest& req) const {
    for (const StaticFile& file : kStaticFiles) {
        if (req.path == file.urlPath) {
            if (req.method != "GET") return methodNotAllowed("GET");
            return serveFile(file);
        }
    }
    if (req.path == "/api/status") {
        if (req.method != "GET") return methodNotAllowed("GET");
        return statusResponse();
    }
    if (req.path == "/api/history") {
        if (req.method != "GET") return methodNotAllowed("GET");
        return historyResponse();
    }
    if (req.path.compare(0, kSlotsPrefix.size(), kSlotsPrefix) == 0) {
        if (req.method != "POST") return methodNotAllowed("POST");
        return slotUpdateResponse(req);
    }
    return makeJsonError(404, "not found");
}

HttpResponse ApiHandler::statusResponse() const {
    const Snapshot snap = manager_.getSnapshot();  // one consistent copy; lock already released

    std::ostringstream out;
    out << "{\"total\":" << snap.counts.total
        << ",\"available\":" << snap.counts.available
        << ",\"occupied\":" << snap.counts.occupied
        << ",\"unknown\":" << snap.counts.unknown
        << ",\"slots\":[";
    for (std::size_t i = 0; i < snap.slots.size(); ++i) {
        if (i > 0) out << ',';
        out << "{\"id\":" << snap.slots[i].getId()
            << ",\"state\":\"" << toString(snap.slots[i].getState()) << "\"}";
    }
    out << "]}";

    HttpResponse r;
    r.body = out.str();
    return r;
}

HttpResponse ApiHandler::historyResponse() const {
    const std::vector<ParkingEvent> events = manager_.getHistory(kHistoryLimit);

    std::ostringstream out;
    out << "{\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        const ParkingEvent& e = events[i];
        if (i > 0) out << ',';
        out << "{\"timestamp\":\"" << jsonEscape(e.timestamp)
            << "\",\"slot\":" << e.slotId
            << ",\"old\":\"" << toString(e.oldState)
            << "\",\"new\":\"" << toString(e.newState)
            << "\",\"source\":\"" << jsonEscape(e.source) << "\"}";
    }
    out << "]}";

    HttpResponse r;
    r.body = out.str();
    return r;
}

HttpResponse ApiHandler::slotUpdateResponse(const HttpRequest& req) const {
    int id = 0;
    if (!parseSlotId(req.path.substr(kSlotsPrefix.size()), id)) {
        events_.onError("rejected request: slot id in URL is not a valid number");
        return makeJsonError(400, "slot id must be a whole number");
    }

    std::string stateText;
    if (!parseStateBody(req.body, stateText)) {
        events_.onError("rejected request: body is not {\"state\":\"...\"}");
        return makeJsonError(400, "body must be {\"state\":\"FREE|OCCUPIED|UNKNOWN\"}");
    }

    SlotState state = SlotState::UNKNOWN;
    if (!parseState(stateText, state)) {
        events_.onError("rejected request: invalid state '" + stateText + "'");
        return makeJsonError(400, "state must be FREE, OCCUPIED or UNKNOWN");
    }

    const UpdateResult result = manager_.updateSlot(id, state, "api");
    if (result == UpdateResult::INVALID_SLOT) {
        // The manager already reported this through the listener.
        return makeJsonError(404, "no such slot");
    }

    HttpResponse r;
    r.body = "{\"slot\":" + std::to_string(id) + ",\"state\":\"" + toString(state) +
             "\",\"changed\":" + (result == UpdateResult::OK_CHANGED ? "true" : "false") + "}";
    return r;
}

HttpResponse ApiHandler::serveFile(const StaticFile& file) const {
    std::string content;
    if (!readFile(webDir_ + "/" + file.fileName, content)) {
        events_.onError(std::string("static file not found: ") + webDir_ + "/" + file.fileName);
        return makeJsonError(404, "file not found");
    }
    HttpResponse r;
    r.contentType = file.contentType;
    r.body = std::move(content);
    return r;
}
