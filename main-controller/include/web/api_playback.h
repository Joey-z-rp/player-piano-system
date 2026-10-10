#ifndef API_PLAYBACK_H
#define API_PLAYBACK_H

#include <ESPAsyncWebServer.h>

class PianoController;

// POST /api/midi?name=<title>   raw .mid file as the body (application/octet-stream)
// POST /api/playback/play
// POST /api/playback/stop
void registerPlaybackRoutes(AsyncWebServer &server, PianoController &piano);

#endif // API_PLAYBACK_H
