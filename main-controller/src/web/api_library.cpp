#include "web/api_library.h"
#include "web/api_playback.h"
#include "web/api_response.h"
#include "web/piece_loader.h"
#include "app/library/piece_library.h"
#include "app/midi/midi_parser.h"
#include "app/piano/piano_controller.h"
#include <AsyncJson.h>

// The piece a request names in its "id", or false (having said so) if
// there's no such piece.
static bool findPiece(AsyncWebServerRequest *request, PieceLibrary &library, const char *id, PieceInfo &piece)
{
  if (id == nullptr || !library.find(id, piece))
  {
    sendError(request, 404, "no such piece in the library");
    return false;
  }
  return true;
}

void registerLibraryRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library)
{
  // POST /api/library/load {"id"} loads a saved piece, with its own settings.
  AsyncCallbackJsonWebHandler *loadHandler = new AsyncCallbackJsonWebHandler(
      "/api/library/load", [&piano, &library](AsyncWebServerRequest *request, JsonVariant &json) {
        PieceInfo piece;
        if (!findPiece(request, library, json["id"], piece))
        {
          return;
        }
        std::vector<uint8_t> data;
        if (!library.read(piece.id, data))
        {
          sendError(request, 500, "couldn't read the piece's file");
          return;
        }
        std::shared_ptr<PianoRoll> roll;
        int status;
        std::string error;
        if (!loadPiece(piano, data.data(), data.size(), piece.name, &piece, roll, status, error))
        {
          sendError(request, status, error.c_str());
          return;
        }
        sendLoaded(request, *roll, nullptr);
      });
  loadHandler->setMethod(HTTP_POST);
  server.addHandler(loadHandler);

  // POST /api/library/add?name=<name> with a MIDI file as the body saves it
  // in the library without loading it (restoring a backup). -> {"library":
  // "saved", "already", "full" or "failed", "piece": {...}}.
  server.on(
      "/api/library/add", HTTP_POST,
      [&library](AsyncWebServerRequest *request) {
        size_t length = request->contentLength();
        if (length == 0 || length > MAX_MIDI_FILE_SIZE || request->_tempObject == nullptr)
        {
          sendError(request, length > MAX_MIDI_FILE_SIZE ? 413 : 400, length == 0 ? "empty file" : "file too large");
          return;
        }
        const uint8_t *data = static_cast<const uint8_t *>(request->_tempObject);
        std::vector<MidiEvent> events;
        std::string error;
        if (!parseMidiFile(data, length, events, error))
        {
          sendError(request, 400, error.c_str());
          return;
        }
        PieceInfo piece;
        piece.name = request->hasParam("name") ? request->getParam("name")->value().c_str() : "untitled";
        describePiece(events, piece);
        static const char *const RESULTS[] = {"saved", "already", "full", "failed"};
        PieceLibrary::SaveResult result =
            library.available() ? library.save(data, length, piece) : PieceLibrary::SaveResult::Failed;
        std::string body = std::string("{\"library\":\"") + RESULTS[(int)result] + "\",\"piece\":" + pieceJson(piece) + "}";
        request->send(result == PieceLibrary::SaveResult::Failed ? 500 : 200, "application/json", body.c_str());
      },
      nullptr, collectMidiBody);

  // POST /api/library/details {"id", "name", "added", "lastPlayed", "volume",
  // "contrast", "voicing", "repeatBoost", "speed", "parts", "muted"}: a saved
  // piece's details as a backup had them.
  AsyncCallbackJsonWebHandler *detailsHandler = new AsyncCallbackJsonWebHandler(
      "/api/library/details", [&library](AsyncWebServerRequest *request, JsonVariant &json) {
        PieceInfo piece;
        if (!findPiece(request, library, json["id"], piece))
        {
          return;
        }
        piece.name = json["name"] | piece.name.c_str();
        piece.added = json["added"] | piece.added;
        piece.lastPlayed = json["lastPlayed"] | piece.lastPlayed;
        // Out of range: loading the piece uses the default for it instead.
        piece.dynamics.volume = json["volume"] | piece.dynamics.volume;
        piece.dynamics.contrast = json["contrast"] | piece.dynamics.contrast;
        piece.dynamics.voicing = json["voicing"] | piece.dynamics.voicing;
        piece.dynamics.repeatBoost = json["repeatBoost"] | piece.dynamics.repeatBoost;
        piece.dynamics.speed = json["speed"] | piece.dynamics.speed;
        if (json["parts"].is<JsonArray>())
        {
          piece.parts.clear();
          for (JsonVariant offset : json["parts"].as<JsonArray>())
          {
            long o = offset | 0L;
            piece.parts.push_back((int8_t)(o < -MAX_PART_OFFSET ? -MAX_PART_OFFSET : o > MAX_PART_OFFSET ? MAX_PART_OFFSET : o));
          }
        }
        piece.dynamics.mutedParts = json["muted"] | piece.dynamics.mutedParts;
        if (!library.restore(piece))
        {
          sendError(request, 500, "couldn't save");
          return;
        }
        library.find(piece.id, piece);
        request->send(200, "application/json", pieceJson(piece).c_str());
      });
  detailsHandler->setMethod(HTTP_POST);
  server.addHandler(detailsHandler);

  // POST /api/library/rename {"id", "name"} -> the piece.
  AsyncCallbackJsonWebHandler *renameHandler = new AsyncCallbackJsonWebHandler(
      "/api/library/rename", [&library](AsyncWebServerRequest *request, JsonVariant &json) {
        PieceInfo piece;
        const char *name = json["name"];
        if (!findPiece(request, library, json["id"], piece))
        {
          return;
        }
        if (name == nullptr || !library.rename(piece.id, name))
        {
          sendError(request, name == nullptr ? 400 : 500, name == nullptr ? "name is required" : "couldn't save");
          return;
        }
        library.find(piece.id, piece);
        request->send(200, "application/json", pieceJson(piece).c_str());
      });
  renameHandler->setMethod(HTTP_POST);
  server.addHandler(renameHandler);

  // POST /api/library/delete {"id"}. A loaded piece stays loaded.
  AsyncCallbackJsonWebHandler *deleteHandler = new AsyncCallbackJsonWebHandler(
      "/api/library/delete", [&library](AsyncWebServerRequest *request, JsonVariant &json) {
        PieceInfo piece;
        if (!findPiece(request, library, json["id"], piece))
        {
          return;
        }
        library.remove(piece.id);
        JsonDocument doc;
        doc["ok"] = true;
        sendJson(request, 200, doc);
      });
  deleteHandler->setMethod(HTTP_POST);
  server.addHandler(deleteHandler);

  // GET /api/library/file?id= -> the piece's MIDI file, named after it.
  server.on("/api/library/file", HTTP_GET, [&library](AsyncWebServerRequest *request) {
    PieceInfo piece;
    if (!request->hasParam("id") || !findPiece(request, library, request->getParam("id")->value().c_str(), piece))
    {
      if (!request->hasParam("id"))
      {
        sendError(request, 400, "id is required");
      }
      return;
    }
    // The name as a file name: an ASCII fallback, and the real one (RFC 5987).
    std::string ascii, encoded;
    for (unsigned char c : piece.name)
    {
      ascii += (c >= 0x20 && c < 0x7f && c != '"' && c != '\\' && c != '/') ? (char)c : '_';
      char hex[4];
      bool plain = isalnum(c) || strchr("-._~ ", c) != nullptr;
      snprintf(hex, sizeof(hex), "%%%02X", c);
      encoded += c == ' ' ? std::string("%20") : plain ? std::string(1, (char)c) : std::string(hex);
    }
    AsyncWebServerResponse *response =
        request->beginResponse(library.files(), PieceLibrary::path(piece.id).c_str(), "audio/midi");
    std::string disposition = "attachment; filename=\"" + ascii + ".mid\"; filename*=UTF-8''" + encoded + ".mid";
    response->addHeader("Content-Disposition", disposition.c_str());
    request->send(response);
  });

  // Last: it would also answer /api/library/... (the server matches what follows a "/").
  // GET /api/library -> {"available", "usedBytes", "totalBytes", "pieces": [{"id",
  // "name", "bytes", "durationMs", "notes", "added", "lastPlayed" (Unix
  // seconds, 0 if unknown), "speed", "parts"}, ...]}.
  server.on("/api/library", HTTP_GET, [&library](AsyncWebServerRequest *request) {
    std::string json = libraryIndexJson(library.list());
    char head[96];
    snprintf(head, sizeof(head), "{\"available\":%s,\"usedBytes\":%u,\"totalBytes\":%u,",
             library.available() ? "true" : "false", (unsigned)library.usedBytes(), (unsigned)library.totalBytes());
    std::string body = head + json.substr(1); // its "pieces" after these
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", body.c_str());
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
}
