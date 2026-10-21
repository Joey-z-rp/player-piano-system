#ifndef API_LIBRARY_H
#define API_LIBRARY_H

#include <ESPAsyncWebServer.h>

class PianoController;
class PieceLibrary;

// GET /api/library, POST /api/library/load, /rename, /delete, GET
// /api/library/file: the saved pieces (see PieceLibrary).
void registerLibraryRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library);

#endif // API_LIBRARY_H
