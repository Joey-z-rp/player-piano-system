#include "web/api_playback.h"
#include "web/api_response.h"
#include "app/piano/roll_builder.h"
#include "app/piano/piano_controller.h"
#include <AsyncJson.h>

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
  case PlaybackResult::NotPlaying:
    sendError(request, 409, "nothing is playing");
    return;
  case PlaybackResult::QueueFull:
    sendError(request, 503, "command queue full");
    return;
  }
}

// Reads a time on the loaded piece's timeline from the query parameter name,
// 0 if it's missing. Sends an error and returns false if it isn't a whole
// number of ms within the piece.
static bool readPieceTime(AsyncWebServerRequest *request, PianoController &piano, const char *name, uint32_t &timeMs)
{
  timeMs = 0;
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
  const String &value = request->getParam(name)->value();
  char *end = nullptr;
  unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (value.isEmpty() || *end != '\0' || value[0] == '-' || parsed > roll->durationMs)
  {
    char error[80];
    snprintf(error, sizeof(error), "%s must be a whole number of ms from 0 to %u", name, roll->durationMs);
    sendError(request, 400, error);
    return false;
  }
  timeMs = parsed;
  return true;
}

// Writes a RollReport as JSON a piece at a time, for a chunked response, so
// a long piece never needs the whole text in memory.
class ReportWriter
{
public:
  ReportWriter(std::shared_ptr<const RollReport> report, uint32_t leadMs, uint16_t volume)
      : report(std::move(report)), leadMs(leadMs), volume(volume)
  {
  }

  size_t write(uint8_t *buffer, size_t maxLen)
  {
    while (pending.size() - sent < maxLen && more())
    {
    }
    size_t length = std::min(maxLen, pending.size() - sent);
    memcpy(buffer, pending.data() + sent, length);
    sent += length;
    if (sent == pending.size())
    {
      pending.clear();
      sent = 0;
    }
    return length;
  }

private:
  // Adds the next part to pending; false once everything is out.
  bool more()
  {
    char text[96];
    switch (stage)
    {
    case 0:
      snprintf(text, sizeof(text), "{\"leadMs\":%u,\"volume\":%u,\"notes\":[", leadMs, volume);
      stage++;
      break;
    case 1:
      if (index == report->notes.size())
      {
        snprintf(text, sizeof(text), "],\"writtenPedal\":[");
        stage++;
        index = 0;
        break;
      }
      {
        const NoteReport &note = report->notes[index];
        snprintf(text, sizeof(text), "%s[%u,%u,%u,%u,%u,%u,%u,%u,%u]", index ? "," : "", note.key, note.onMs,
                 note.writtenOffMs, note.playedOnMs, note.playedOffMs, note.writtenVelocity, note.playedVelocity,
                 note.changes, note.mergedIntoMs);
        index++;
      }
      break;
    case 2:
    case 3:
    {
      const std::vector<PedalSpan> &spans = stage == 2 ? report->writtenPedal : report->playedPedal;
      if (index == spans.size())
      {
        snprintf(text, sizeof(text), stage == 2 ? "],\"playedPedal\":[" : "]}");
        stage++;
        index = 0;
        break;
      }
      snprintf(text, sizeof(text), "%s[%u,%u]", index ? "," : "", spans[index].downMs, spans[index].upMs);
      index++;
      break;
    }
    default:
      return false;
    }
    pending += text;
    return true;
  }

  std::shared_ptr<const RollReport> report;
  uint32_t leadMs;
  uint16_t volume;
  int stage = 0;
  size_t index = 0;
  std::string pending;
  size_t sent = 0;
};

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
  if (piano.status().state != PlaybackState::Idle)
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
  if (!buildPianoRoll(events, *roll, piano.velocities(), piano.volume()))
  {
    sendError(request, 500, "out of memory");
    return;
  }
  // Kept, so Play can build it again if the volume changes.
  roll->source = std::make_shared<const std::vector<MidiEvent>>(std::move(events));
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
  doc["duplicateNotes"] = roll->duplicateNotes;
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

  // POST /api/playback/play[?from=ms] plays the loaded piece: from that
  // point on its timeline (the positionMs of /api/status) if given, else from
  // the start, or where it was paused.
  server.on("/api/playback/play", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    uint32_t fromMs;
    if (readPieceTime(request, piano, "from", fromMs))
    {
      sendPlaybackResult(request, piano.play(request->hasParam("from") ? fromMs : PianoController::CONTINUE));
    }
  });

  server.on("/api/playback/pause", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    sendPlaybackResult(request, piano.pause());
  });

  // POST /api/playback/seek?to=ms jumps the playing or paused piece to that point.
  server.on("/api/playback/seek", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    uint32_t toMs;
    if (!request->hasParam("to"))
    {
      sendError(request, 400, "to is required");
    }
    else if (readPieceTime(request, piano, "to", toMs))
    {
      sendPlaybackResult(request, piano.seek(toMs));
    }
  });

  server.on("/api/playback/stop", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    sendPlaybackResult(request, piano.stop());
  });

  // GET /api/piece/notes -> every note of the loaded piece as written and as
  // it's played (at the volume it's playing at, else the current one), to
  // show what the roll builder changed: {"leadMs", "volume", "notes": [[key
  // 0-87, on ms, written off ms, played on ms, played off ms (both 0 if
  // merged), written velocity, played velocity (0 if merged), NoteChange
  // flags, merged: on ms of the strike it's held in], ...], "writtenPedal":
  // [[down ms, up ms], ...], "playedPedal": [...]}. Times are on the MIDI's timeline; the playback
  // position is lead ms later. Registered before /api/piece, which would
  // also match it.
  server.on("/api/piece/notes", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    PlaybackStatus playback = piano.status();
    std::shared_ptr<const PianoRoll> loaded = playback.roll;
    if (loaded == nullptr || loaded->source == nullptr)
    {
      sendError(request, 404, "no piece loaded");
      return;
    }
    PianoRoll roll;
    std::shared_ptr<RollReport> report = std::make_shared<RollReport>();
    // While playing, the volume it's playing at; a new one applies from the
    // next Play (or going on from a pause).
    uint16_t volume = playback.state == PlaybackState::Playing ? loaded->volume : piano.volume();
    if (!buildPianoRoll(*loaded->source, roll, piano.velocities(), volume, report.get()))
    {
      sendError(request, 500, "out of memory");
      return;
    }
    std::shared_ptr<ReportWriter> writer = std::make_shared<ReportWriter>(report, roll.leadMs, volume);
    AsyncWebServerResponse *response = request->beginChunkedResponse(
        "application/json", [writer](uint8_t *buffer, size_t maxLen, size_t) { return writer->write(buffer, maxLen); });
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  // GET /api/piece -> the loaded piece's notes by velocity as written, so the
  // play page can show what a volume does to them: {"title", "notes",
  // "velocities": [count at 1, count at 2, ... count at 127]}, or 404.
  server.on("/api/piece", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    std::shared_ptr<const PianoRoll> roll = piano.status().roll;
    if (roll == nullptr || roll->source == nullptr)
    {
      sendError(request, 404, "no piece loaded");
      return;
    }
    uint32_t counts[127] = {};
    for (const MidiEvent &event : *roll->source)
    {
      // Notes on the 88 keys (A0-C8), as the roll builder plays them.
      if (event.type == MidiEventType::NoteOn && event.note >= 21 && event.note <= 108 && event.value >= 1 &&
          event.value <= 127)
      {
        counts[event.value - 1]++;
      }
    }
    JsonDocument doc;
    doc["title"] = roll->title;
    doc["notes"] = roll->noteCount;
    JsonArray velocities = doc["velocities"].to<JsonArray>();
    for (uint32_t count : counts)
    {
      velocities.add(count);
    }
    sendJson(request, 200, doc);
  });

  // GET /api/volume -> {"volume": 100}; POST {"volume": 80} sets it (applies from the next play).
  server.on("/api/volume", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["volume"] = piano.volume();
    sendJson(request, 200, doc);
  });
  AsyncCallbackJsonWebHandler *volumeHandler = new AsyncCallbackJsonWebHandler(
      "/api/volume", [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonVariant volume = json["volume"];
        long percent = volume.is<long>() ? volume.as<long>() : -1;
        if (percent < MIN_VOLUME || percent > MAX_VOLUME || !piano.setVolume((uint16_t)percent))
        {
          char error[64];
          snprintf(error, sizeof(error), "volume must be a whole number from %u to %u", MIN_VOLUME, MAX_VOLUME);
          sendError(request, 400, error);
          return;
        }
        JsonDocument doc;
        doc["volume"] = piano.volume();
        sendJson(request, 200, doc);
      });
  volumeHandler->setMethod(HTTP_POST);
  server.addHandler(volumeHandler);
}
