#ifndef API_OFFSETS_H
#define API_OFFSETS_H

#include <ESPAsyncWebServer.h>

class PianoController;

// GET /api/offsets, POST /api/offsets/key, POST /api/offsets/clear: the
// per-key velocity offsets (see key_offsets.h).
void registerOffsetRoutes(AsyncWebServer &server, PianoController &piano);

#endif // API_OFFSETS_H
