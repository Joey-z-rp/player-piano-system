#ifndef API_PIECE_H
#define API_PIECE_H

#include <ESPAsyncWebServer.h>

class PianoController;

// GET /api/piece and /api/piece/notes: the loaded piece's parts and
// velocities, and every note as written and as played.
void registerPieceRoutes(AsyncWebServer &server, PianoController &piano);

#endif // API_PIECE_H
