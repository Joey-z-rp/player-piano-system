#ifndef API_STATUS_H
#define API_STATUS_H

#include <ESPAsyncWebServer.h>

class PianoController;

// GET /api/status
void registerStatusRoutes(AsyncWebServer &server, PianoController &piano);

#endif // API_STATUS_H
