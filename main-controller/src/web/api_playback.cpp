#include "web/api_playback.h"
#include "web/api_response.h"
#include "app/piano/roll_builder.h"
#include "app/piano/piano_controller.h"
#include "app/library/piece_library.h"
#include "app/midi/midi_parser.h"
#include "web/piece_loader.h"
#include <AsyncJson.h>


static void sendPlaybackResult(AsyncWebServerRequest *request, PlaybackResult result)
{
  switch (result)
  {
  case PlaybackResult::Ok:
  {
    JsonDocument doc;
    doc["ok"] = true;
    sendJson(request, 202, doc);
    return;
  }
  case PlaybackResult::Busy:
    sendError(request, 409, "a piece is playing");
    return;
  case PlaybackResult::NoRoll:
    sendError(request, 409, "no piece loaded");
    return;
  case PlaybackResult::NotPlaying:
    sendError(request, 409, "nothing is playing");
    return;
  case PlaybackResult::QueueFull:
    sendError(request, 503, "command queue full");
    return;
  case PlaybackResult::TooHot:
    sendError(request, 409, "a solenoid is still cooling: see /api/status's heat");
    return;
  case PlaybackResult::Updating:
    sendError(request, 409, "the controller is being updated");
    return;
  }
}

// Reads a time on the loaded piece's timeline from the query parameter name,
// 0 if it's missing, and the speed the piece was at when it was read from
// "speed" (0 if missing: as it is now). Sends an error and returns false if
// either isn't a whole number in range (the time within the piece).
static bool readPieceTime(AsyncWebServerRequest *request, PianoController &piano, const char *name, uint32_t &timeMs,
                          uint16_t &atSpeed)
{
  timeMs = 0;
  atSpeed = 0;
  if (!request->hasParam(name))
  {
    return true;
  }
  std::shared_ptr<const PianoRoll> roll = piano.status().roll;
  if (roll == nullptr)
  {
    sendPlaybackResult(request, PlaybackResult::NoRoll);
    return false;
  }
  if (request->hasParam("speed"))
  {
    long speed = request->getParam("speed")->value().toInt();
    if (speed < MIN_SPEED || speed > MAX_SPEED)
    {
      char error[64];
      snprintf(error, sizeof(error), "speed must be a whole number from %u to %u", MIN_SPEED, MAX_SPEED);
      sendError(request, 400, error);
      return false;
    }
    atSpeed = (uint16_t)speed;
  }
  // The piece's length at that speed.
  uint32_t durationMs = roll->durationMs;
  if (atSpeed != 0 && atSpeed != roll->dynamics.speed && durationMs > roll->leadMs)
  {
    durationMs = roll->leadMs + (uint32_t)(((uint64_t)(durationMs - roll->leadMs) * roll->dynamics.speed + atSpeed - 1) /
                                           atSpeed);
  }
  const String &value = request->getParam(name)->value();
  char *end = nullptr;
  unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (value.isEmpty() || *end != '\0' || value[0] == '-' || parsed > durationMs)
  {
    char error[80];
    snprintf(error, sizeof(error), "%s must be a whole number of ms from 0 to %u", name, durationMs);
    sendError(request, 400, error);
    return false;
  }
  timeMs = parsed;
  return true;
}

// What a load tells the page: the piece, what the roll builder changed, and
// (for an upload) what happened in the library: "saved", "already" (the same
// file was there: loaded as saved, with its name and settings), "full",
// "unavailable" (no library partition) or "failed".
void sendLoaded(AsyncWebServerRequest *request, const PianoRoll &roll, const char *library)
{
  JsonDocument doc;
  doc["title"] = roll.title;
  doc["durationMs"] = roll.durationMs;
  doc["notes"] = roll.noteCount;
  doc["skippedNotes"] = roll.skippedNotes;
  doc["mergedNotes"] = roll.mergedNotes;
  doc["duplicateNotes"] = roll.duplicateNotes;
  doc["libraryId"] = roll.libraryId;
  if (library != nullptr)
  {
    doc["library"] = library;
  }
  sendJson(request, 200, doc);
}

// Loads the uploaded file, and saves it in the library unless it's there
// already (in which case it's loaded with its saved name and settings).
static void handleMidiUpload(AsyncWebServerRequest *request, PianoController &piano, PieceLibrary &library)
{
  size_t length = request->contentLength();
  if (length == 0)
  {
    sendError(request, 400, "empty file");
    return;
  }
  if (length > MAX_MIDI_FILE_SIZE)
  {
    sendError(request, 413, "file too large");
    return;
  }
  if (request->_tempObject == nullptr)
  {
    sendError(request, 500, "out of memory");
    return;
  }
  const uint8_t *data = static_cast<const uint8_t *>(request->_tempObject);
  std::string fileName = request->hasParam("name") ? request->getParam("name")->value().c_str() : "";
  PieceInfo piece;
  bool known = library.find(pieceId(data, length), piece);
  if (!known)
  {
    piece.id = pieceId(data, length);
    piece.name = pieceNameFromFile(fileName);
  }
  std::shared_ptr<PianoRoll> roll;
  int status;
  std::string error;
  if (!loadPiece(piano, data, length, piece.name, &piece, roll, status, error))
  {
    sendError(request, status, error.c_str());
    return;
  }
  const char *saved = "already";
  if (!known)
  {
    describePiece(*roll->source, piece);
    switch (library.available() ? library.save(data, length, piece) : PieceLibrary::SaveResult::Failed)
    {
    case PieceLibrary::SaveResult::Saved:
      saved = "saved";
      break;
    case PieceLibrary::SaveResult::AlreadySaved:
      saved = "already";
      break;
    case PieceLibrary::SaveResult::Full:
      saved = "full";
      break;
    case PieceLibrary::SaveResult::Failed:
      saved = library.available() ? "failed" : "unavailable";
      break;
    }
  }
  sendLoaded(request, *roll, saved);
}

void registerPlaybackRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library)
{
  server.on(
      "/api/midi", HTTP_POST,
      [&piano, &library](AsyncWebServerRequest *request) { handleMidiUpload(request, piano, library); },
      nullptr, collectMidiBody);

  // POST /api/playback/play[?from=ms[&speed=percent]] plays the loaded piece:
  // from that point on its timeline (the positionMs of /api/status) if given,
  // else from the start, or where it was paused. speed: the piece's speed
  // when the point was read (its "speed" in /api/status), so it's the same
  // point in the music if the piece has been rebuilt for another since.
  server.on("/api/playback/play", HTTP_POST, [&piano, &library](AsyncWebServerRequest *request) {
    uint32_t fromMs;
    uint16_t atSpeed;
    if (readPieceTime(request, piano, "from", fromMs, atSpeed))
    {
      PlaybackResult result = piano.play(request->hasParam("from") ? fromMs : PianoController::CONTINUE, atSpeed);
      std::shared_ptr<const PianoRoll> roll = piano.status().roll;
      if (result == PlaybackResult::Ok && roll != nullptr && !roll->libraryId.empty())
      {
        library.markPlayed(roll->libraryId);
      }
      sendPlaybackResult(request, result);
    }
  });

  server.on("/api/playback/pause", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    sendPlaybackResult(request, piano.pause());
  });

  // POST /api/playback/seek?to=ms[&speed=percent] jumps the playing or paused
  // piece to that point (speed as for play).
  server.on("/api/playback/seek", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    uint32_t toMs;
    uint16_t atSpeed;
    if (!request->hasParam("to"))
    {
      sendError(request, 400, "to is required");
    }
    else if (readPieceTime(request, piano, "to", toMs, atSpeed))
    {
      sendPlaybackResult(request, piano.seek(toMs, atSpeed));
    }
  });

  server.on("/api/playback/stop", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    sendPlaybackResult(request, piano.stop());
  });
}
