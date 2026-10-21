#ifndef API_SYSTEM_H
#define API_SYSTEM_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

class OtaUpdates;
class WiFiStationModule;

// GET /api/system: the firmware (version, slot, rollback), any update under
// way, and the Wi-Fi. GET /api/wifi/scan, POST /api/wifi, POST
// /api/wifi/forget: the network the controller joins.
void registerSystemRoutes(AsyncWebServer &server, WiFiStationModule &wifi, OtaUpdates &ota);
// The system's state into out: "firmware", "update" and "wifi", as GET
// /api/system gives them.
void writeSystem(JsonObject out, WiFiStationModule &wifi, OtaUpdates &ota);
// From loop(): restarts the controller once a request has asked for it.
void restartIfAsked();

#endif // API_SYSTEM_H
