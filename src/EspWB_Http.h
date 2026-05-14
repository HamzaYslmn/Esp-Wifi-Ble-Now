#pragma once
#include <WebServer.h>

// MARK: EspWB::Http - one-call CORS + Chrome PNA middleware for WebServer.
//
// Usage:
//   WebServer http(80);
//   EspWB::enableCors(http);          // anywhere before http.begin()
//   http.on("/message", [](){ http.send(200, "text/plain", "ok"); });
//   http.begin();
//
// Effect (uses arduino-esp32's NATIVE WebServer middleware chain):
//   * Adds Access-Control-Allow-Origin: *
//          Access-Control-Allow-Private-Network: true   (Chrome PNA / LNA)
//     to every response.
//   * Short-circuits OPTIONS preflights with 204 + the full preflight set.
//   * No request-side header collection needed; works whether or not the
//     sketch calls collectHeaders() (stock CorsMiddleware silently no-ops
//     when Origin isn't collected, which is the default).

namespace EspWB {

inline void enableCors(WebServer& s) {
  s.addMiddleware([](WebServer& server, Middleware::Callback next) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      return true;
    }
    return next();
  });
}

} // namespace EspWB
