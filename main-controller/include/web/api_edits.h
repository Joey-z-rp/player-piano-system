#ifndef API_EDITS_H
#define API_EDITS_H

#include <ESPAsyncWebServer.h>

class PianoController;
class PieceLibrary;

// Note edits (note_edits.h): the loaded piece's, from the note view
// (/api/piece/edits), and any library piece's, for backups
// (/api/library/edits). Registered before the library and piece routes,
// which would also match these.
void registerEditRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library);

#endif // API_EDITS_H
