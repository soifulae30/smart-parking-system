#ifndef API_HANDLER_H
#define API_HANDLER_H

#include <string>

#include "HttpProtocol.h"
#include "IEventListener.h"
#include "ParkingSlotManager.h"

struct StaticFile;  // defined in ApiHandler.cpp

// Maps an HTTP request to a response. Knows the API, knows nothing about
// sockets, so it can be tested by calling handle() with plain strings.
class ApiHandler {
public:
    // 'webDir' is the folder holding index.html, style.css, app.js.
    // manager and events must outlive this object.
    ApiHandler(ParkingSlotManager& manager, IEventListener& events, std::string webDir);

    HttpResponse handle(const HttpRequest& request) const;

private:
    HttpResponse statusResponse() const;
    HttpResponse historyResponse() const;
    HttpResponse slotUpdateResponse(const HttpRequest& request) const;
    HttpResponse serveFile(const StaticFile& file) const;

    ParkingSlotManager& manager_;
    IEventListener& events_;
    std::string webDir_;
};

#endif // API_HANDLER_H
