#include "web/api_edits.h"
#include "web/api_response.h"
#include "app/library/piece_library.h"
#include "app/piano/note_edits.h"
#include "app/piano/piano_controller.h"
#include <AsyncJson.h>
#include <memory>

// Reads the request's "notes" and "pedal" (see readNoteEdits).
static bool readEdits(AsyncWebServerRequest *request, JsonVariant json, std::shared_ptr<NoteEdits> &edits)
{
  std::string body, error;
  serializeJson(json, body);
  edits = std::make_shared<NoteEdits>();
  if (!readNoteEdits(body.data(), body.size(), *edits, error))
  {
    sendError(request, 400, error.c_str());
    return false;
  }
  return true;
}

// {"saved", "notes", "pedal"}.
static void sendEdits(AsyncWebServerRequest *request, const std::shared_ptr<const NoteEdits> &edits, bool saved)
{
  std::string json = noteEditsJson(edits != nullptr ? *edits : NoteEdits());
  json = std::string("{\"saved\":") + (saved ? "true," : "false,") + json.substr(1);
  request->send(200, "application/json", json.c_str());
}

void registerEditRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library)
{
  // GET /api/piece/edits -> the loaded piece's edits: {"notes": [[key, at
  // ms, velocity, length ms, shift ms], ...], "pedal": [[at ms, down ms, up
  // ms, removed], ...], "saved": whether they're kept with it in the
  // library} (note_edits.h).
  server.on("/api/piece/edits", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    std::shared_ptr<const PianoRoll> roll = piano.status().roll;
    if (roll == nullptr)
    {
      sendError(request, 404, "no piece loaded");
      return;
    }
    sendEdits(request, piano.noteEdits(), !roll->libraryId.empty());
  });

  // POST /api/piece/edits {"notes": [...], "pedal": [...]} replaces them
  // (from the next Play), and keeps them with the piece in the library. -> as GET.
  AsyncCallbackJsonWebHandler *pieceHandler = new AsyncCallbackJsonWebHandler(
      "/api/piece/edits", [&piano, &library](AsyncWebServerRequest *request, JsonVariant &json) {
        std::shared_ptr<const PianoRoll> roll = piano.status().roll;
        if (roll == nullptr)
        {
          sendError(request, 404, "no piece loaded");
          return;
        }
        std::shared_ptr<NoteEdits> edits;
        if (!readEdits(request, json, edits))
        {
          return;
        }
        piano.setNoteEdits(edits);
        bool saved = !roll->libraryId.empty() && library.saveEdits(roll->libraryId, *edits);
        sendEdits(request, piano.noteEdits(), saved);
      });
  pieceHandler->setMethod(HTTP_POST);
  server.addHandler(pieceHandler);

  // GET /api/library/edits?id= -> a library piece's, as above.
  server.on("/api/library/edits", HTTP_GET, [&library](AsyncWebServerRequest *request) {
    PieceInfo piece;
    if (!request->hasParam("id") || !library.find(request->getParam("id")->value().c_str(), piece))
    {
      sendError(request, 404, "no such piece in the library");
      return;
    }
    sendEdits(request, library.readEdits(piece.id), true);
  });

  // POST /api/library/edits {"id", "notes": [...], "pedal": [...]} replaces a library
  // piece's (restoring a backup); the loaded piece's too, if it's that one.
  AsyncCallbackJsonWebHandler *libraryHandler = new AsyncCallbackJsonWebHandler(
      "/api/library/edits", [&piano, &library](AsyncWebServerRequest *request, JsonVariant &json) {
        PieceInfo piece;
        const char *id = json["id"];
        if (id == nullptr || !library.find(id, piece))
        {
          sendError(request, 404, "no such piece in the library");
          return;
        }
        std::shared_ptr<NoteEdits> edits;
        if (!readEdits(request, json, edits))
        {
          return;
        }
        if (!library.saveEdits(piece.id, *edits))
        {
          sendError(request, 500, "couldn't save the edits");
          return;
        }
        std::shared_ptr<const PianoRoll> roll = piano.status().roll;
        if (roll != nullptr && roll->libraryId == piece.id)
        {
          piano.setNoteEdits(edits);
        }
        sendEdits(request, edits, true);
      });
  libraryHandler->setMethod(HTTP_POST);
  server.addHandler(libraryHandler);
}
