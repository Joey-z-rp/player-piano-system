#ifndef API_DRIVER_H
#define API_DRIVER_H

#include <ESPAsyncWebServer.h>

class PianoController;

// POST /api/driver/command  {"command": "P:20:60"}
void registerDriverRoutes(AsyncWebServer &server, PianoController &piano);

#endif // API_DRIVER_H
