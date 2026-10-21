#ifndef API_STATUS_H
#define API_STATUS_H

#include <ESPAsyncWebServer.h>

class PianoController;

// GET /api/status
class OtaUpdates;
class WiFiStationModule;

void registerStatusRoutes(AsyncWebServer &server, PianoController &piano, WiFiStationModule &wifi, OtaUpdates &ota);

#endif // API_STATUS_H
