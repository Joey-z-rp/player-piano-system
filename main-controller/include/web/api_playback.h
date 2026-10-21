#ifndef API_PLAYBACK_H
#define API_PLAYBACK_H

#include <ESPAsyncWebServer.h>

class PianoController;
class PieceLibrary;
struct PianoRoll;

// POST /api/midi?name=<file name>   raw .mid file as the body (application/octet-stream)
// POST /api/playback/play
// POST /api/playback/stop
// ...and the rest in api_playback.cpp.
void registerPlaybackRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library);
// The response to loading a piece (see api_playback.cpp).
void sendLoaded(AsyncWebServerRequest *request, const PianoRoll &roll, const char *library);

#endif // API_PLAYBACK_H
