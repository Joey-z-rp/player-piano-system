#ifndef API_DYNAMICS_H
#define API_DYNAMICS_H

#include <ESPAsyncWebServer.h>

class PianoController;
class PieceLibrary;

// GET/POST /api/dynamics: the loaded piece's volume, contrast, voicing,
// repeat boost, speed, part offsets and mutes (remembered for a library piece).
void registerDynamicsRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library);

#endif // API_DYNAMICS_H
