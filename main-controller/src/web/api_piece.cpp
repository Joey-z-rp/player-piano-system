#include "web/api_piece.h"
#include "web/api_response.h"
#include "app/midi/midi_parser.h"
#include "app/piano/piano_controller.h"
#include "app/piano/roll_builder.h"
#include <AsyncJson.h>

// Writes a RollReport as JSON a piece at a time, for a chunked response, so
// a long piece never needs the whole text in memory.
class ReportWriter
{
public:
  ReportWriter(std::shared_ptr<const RollReport> report, uint32_t leadMs, const Dynamics &dynamics)
      : report(std::move(report)), leadMs(leadMs), dynamics(dynamics)
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
    char text[128];
    switch (stage)
    {
    case 0:
      snprintf(text, sizeof(text), "{\"leadMs\":%u,\"volume\":%u,\"contrast\":%u,\"voicing\":%u,\"repeatBoost\":%u,"
               "\"speed\":%u,\"notes\":[", leadMs, dynamics.volume, dynamics.contrast, dynamics.voicing,
               dynamics.repeatBoost, dynamics.speed);
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
        snprintf(text, sizeof(text), "%s[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]", index ? "," : "", note.key, note.onMs,
                 note.writtenOffMs, note.playedOnMs, note.playedOffMs, note.writtenVelocity, note.playedVelocity,
                 note.changes, note.mergedIntoMs, note.part);
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
  Dynamics dynamics;
  int stage = 0;
  size_t index = 0;
  std::string pending;
  size_t sent = 0;
};

void registerPieceRoutes(AsyncWebServer &server, PianoController &piano)
{
  // GET /api/piece/notes -> every note of the loaded piece as written and as
  // it's played (at the volume it's playing at, else the current one), to
  // show what the roll builder changed: {"leadMs", "volume", "notes": [[key
  // 0-87, on ms, written off ms, played on ms, played off ms (both 0 if
  // merged), written velocity, played velocity (0 if merged), NoteChange
  // flags, merged: on ms of the strike it's held in, part], ...], "writtenPedal":
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
    // While playing, the dynamics it's playing with; new ones apply from the
    // next Play (or going on from a pause).
    Dynamics dynamics = playback.state == PlaybackState::Playing ? loaded->dynamics : piano.dynamics();
    PlayConfig rules = piano.playConfig();
    if (!buildPianoRoll(*loaded->source, roll, piano.velocities(), dynamics, report.get(), &rules))
    {
      sendError(request, 500, "out of memory");
      return;
    }
    std::shared_ptr<ReportWriter> writer = std::make_shared<ReportWriter>(report, roll.leadMs, dynamics);
    AsyncWebServerResponse *response = request->beginChunkedResponse(
        "application/json", [writer](uint8_t *buffer, size_t maxLen, size_t) { return writer->write(buffer, maxLen); });
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  // GET /api/piece -> the loaded piece's notes by velocity as written, so the
  // play page can show what a volume does to them: {"title", "notes",
  // "velocities": [count at 1, count at 2, ... count at 127], "parts": [{"name",
  // "notes", "low", "high", "velocity"}, ...]}, or 404.
  server.on("/api/piece", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    std::shared_ptr<const PianoRoll> roll = piano.status().roll;
    if (roll == nullptr || roll->source == nullptr)
    {
      sendError(request, 404, "no piece loaded");
      return;
    }
    uint32_t counts[127] = {};
    struct Part
    {
      uint32_t notes = 0;
      uint8_t low = 255, high = 0;
      uint32_t velocities[127] = {};
    };
    std::vector<Part> parts(roll->partNames.size());
    for (const MidiEvent &event : *roll->source)
    {
      // Notes on the 88 keys (A0-C8), as the roll builder plays them.
      if (event.type == MidiEventType::NoteOn && event.note >= 21 && event.note <= 108 && event.value >= 1 &&
          event.value <= 127)
      {
        counts[event.value - 1]++;
        if (event.part < parts.size())
        {
          Part &part = parts[event.part];
          part.notes++;
          part.low = std::min(part.low, event.note);
          part.high = std::max(part.high, event.note);
          part.velocities[event.value - 1]++;
        }
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
    // Each part of the file with notes on the keys: its name, how many, its
    // lowest and highest MIDI note, and its median velocity.
    JsonArray partList = doc["parts"].to<JsonArray>();
    for (size_t i = 0; i < parts.size(); i++)
    {
      JsonObject part = partList.add<JsonObject>();
      part["name"] = roll->partNames[i];
      part["notes"] = parts[i].notes;
      if (parts[i].notes)
      {
        part["low"] = parts[i].low;
        part["high"] = parts[i].high;
        uint32_t seen = 0;
        for (int v = 0; v < 127; v++)
        {
          if ((seen += parts[i].velocities[v]) * 2 >= parts[i].notes)
          {
            part["velocity"] = v + 1;
            break;
          }
        }
      }
    }
    sendJson(request, 200, doc);
  });
}
