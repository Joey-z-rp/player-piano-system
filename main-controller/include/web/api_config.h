#ifndef API_CONFIG_H
#define API_CONFIG_H

#include <ESPAsyncWebServer.h>

class PianoController;

// GET /api/config, POST /api/config, POST /api/config/reset: the timing rules
// pieces are played with (see PlayConfig).
void registerConfigRoutes(AsyncWebServer &server, PianoController &piano);

#endif // API_CONFIG_H
