#include "web/api_playback.h"
#include "web/api_response.h"
#include "app/piano/roll_builder.h"
#include "app/piano/piano_controller.h"

static const size_t MAX_MIDI_FILE_SIZE = 512 * 1024;

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
  case PlaybackResult::QueueFull:
    sendError(request, 503, "command queue full");
    return;
  }
}

static void handleMidiUpload(AsyncWebServerRequest *request, PianoController &piano)
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
  if (piano.status().state == PlaybackState::Playing)
  {
    sendError(request, 409, "a piece is playing");
    return;
  }

  std::vector<MidiEvent> events;
  std::string error;
  if (!parseMidiFile(static_cast<const uint8_t *>(request->_tempObject), length, events, error))
  {
    sendError(request, 400, error.c_str());
    return;
  }
  std::shared_ptr<PianoRoll> roll = std::make_shared<PianoRoll>();
  if (!buildPianoRoll(events, *roll))
  {
    sendError(request, 500, "out of memory");
    return;
  }
  if (roll->noteCount == 0)
  {
    sendError(request, 400, "no playable notes");
    return;
  }
  roll->title = request->hasParam("name") ? request->getParam("name")->value().c_str() : "untitled";

  PlaybackResult result = piano.loadRoll(roll);
  if (result != PlaybackResult::Ok)
  {
    sendPlaybackResult(request, result);
    return;
  }

  JsonDocument doc;
  doc["title"] = roll->title;
  doc["durationMs"] = roll->durationMs;
  doc["notes"] = roll->noteCount;
  doc["skippedNotes"] = roll->skippedNotes;
  doc["mergedNotes"] = roll->mergedNotes;
  sendJson(request, 200, doc);
}

// The body arrives in chunks; collect it into one buffer, which the request
// frees when it's destroyed. Oversized uploads are drained without storing.
static void collectBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (index == 0 && total > 0 && total <= MAX_MIDI_FILE_SIZE)
  {
    request->_tempObject = malloc(total);
  }
  if (request->_tempObject != nullptr)
  {
    memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);
  }
}

void registerPlaybackRoutes(AsyncWebServer &server, PianoController &piano)
{
  server.on(
      "/api/midi", HTTP_POST,
      [&piano](AsyncWebServerRequest *request) { handleMidiUpload(request, piano); },
      nullptr, collectBody);

  server.on("/api/playback/play", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    sendPlaybackResult(request, piano.play());
  });

  server.on("/api/playback/stop", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    sendPlaybackResult(request, piano.stop());
  });
}
