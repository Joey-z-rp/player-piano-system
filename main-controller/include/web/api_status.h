#ifndef API_STATUS_H
#define API_STATUS_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

class OtaUpdates;
class PianoController;
class WiFiStationModule;

// The controller's state, as GET /api/status gives it (and /api/events
// streams it): the playback, the loaded piece, its settings, the solenoids'
// heat, and the system (see writeSystem). /api/status also adds the
// ever-changing extras (uptime, free memory, queued commands).
void writeStatus(JsonObject out, PianoController &piano, WiFiStationModule &wifi, OtaUpdates &ota);

// GET /api/status
void registerStatusRoutes(AsyncWebServer &server, PianoController &piano, WiFiStationModule &wifi, OtaUpdates &ota);

#endif // API_STATUS_H
