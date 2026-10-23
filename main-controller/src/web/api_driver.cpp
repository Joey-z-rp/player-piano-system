#include "web/api_driver.h"
#include "web/api_response.h"
#include "app/piano/piano_controller.h"
#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include <AsyncJson.h>
#include <algorithm>

static const size_t MAX_TEST_NOTES = 200;
static const uint32_t MAX_TEST_MS = 120000;
static const uint8_t LOWEST_NOTE = 21; // piano key 0, A0

static float percent(uint16_t duty)
{
  return duty / 100.0f;
}

// A whole number query parameter within [low, high]; low if it's missing.
static bool readParam(AsyncWebServerRequest *request, const char *name, long low, long high, long &out)
{
  out = low;
  if (!request->hasParam(name))
  {
    return true;
  }
  const String &value = request->getParam(name)->value();
  char *end = nullptr;
  out = strtol(value.c_str(), &end, 10);
  if (value.isEmpty() || *end != '\0' || out < low || out > high)
  {
    std::string error = std::string(name) + " must be a whole number from " + std::to_string(low) + " to " +
                        std::to_string(high);
    sendError(request, 400, error.c_str());
    return false;
  }
  return true;
}

void registerDriverRoutes(AsyncWebServer &server, PianoController &piano)
{
  AsyncCallbackJsonWebHandler *commandHandler = new AsyncCallbackJsonWebHandler(
      "/api/driver/command",
      [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        String command = json["command"] | "";
        command.trim();

        switch (piano.submitDriverCommand(command))
        {
        case SubmitResult::Invalid:
          sendError(request, 400, "invalid command");
          return;
        case SubmitResult::QueueFull:
          sendError(request, 503, "command queue full");
          return;
        case SubmitResult::Queued:
          break;
        }

        JsonDocument doc;
        doc["queued"] = command;
        sendJson(request, 202, doc);
      });
  commandHandler->setMethod(HTTP_POST);
  server.addHandler(commandHandler);

  AsyncCallbackJsonWebHandler *pedalHandler = new AsyncCallbackJsonWebHandler(
      "/api/pedal",
      [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json["down"].is<bool>())
        {
          sendError(request, 400, "\"down\" must be true or false");
          return;
        }
        if (piano.setPedal(json["down"].as<bool>()) != PlaybackResult::Ok)
        {
          sendError(request, 503, "command queue full");
          return;
        }
        JsonDocument doc;
        doc["ok"] = true;
        sendJson(request, 202, doc);
      });
  pedalHandler->setMethod(HTTP_POST);
  server.addHandler(pedalHandler);

  // GET /api/driver/key?key=0-87&velocity=1-127[&pedal=1] -> how the
  // velocity table plays the key: {"key", "velocity", "pedal", "calibrated",
  // "hasPedal" (its own pedal-down entry), "command" (the press at this
  // velocity, as a piece sends it), "playedVelocity" (with the key's offset),
  // "duty", "landingMs", "coast", "hold", "clamp": {"offsetMs", "ms", "duty"}
  // or null, "duties": [duty played at velocity 1-127, offsets and all],
  // "tableDuties": [as the table has them], "pedalDuties" and
  // "tablePedalDuties" (with a pedal-down entry), "landings": [landing ms at
  // velocity 1-127 as played, for this pedal], "dutyChange" (at this
  // velocity), "offsets": {"up": [[velocity, offset], ...], "pedal": [...] or
  // null, "duty": [[velocity, duty change], ...], "dutyPedal": [...] or
  // null}}. Duties in percent.
  server.on("/api/driver/key", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    long key, velocity, pedalParam;
    if (!readParam(request, "key", 0, PIANO_KEY_COUNT - 1, key) ||
        !readParam(request, "velocity", 1, VELOCITY_LEVELS, velocity) ||
        !readParam(request, "pedal", 0, 1, pedalParam))
    {
      return;
    }
    const VelocityTable *table = piano.velocities();
    if (table == nullptr)
    {
      sendError(request, 503, "no velocity table");
      return;
    }
    bool pedal = pedalParam == 1;
    uint16_t duty = table->duty(key, velocity, pedal);
    char command[DRIVER_COMMAND_MAX_LENGTH + 1];
    table->pressCommand(key, velocity, command, sizeof(command), pedal);
    JsonDocument doc;
    doc["key"] = key;
    doc["velocity"] = velocity;
    doc["pedal"] = pedal;
    doc["calibrated"] = table->isCalibrated(key);
    doc["hasPedal"] = table->hasPedal(key);
    doc["command"] = command;
    doc["playedVelocity"] = table->playedVelocity(key, velocity, pedal);
    doc["duty"] = percent(duty);
    doc["landingMs"] = table->landingMs(key, velocity, pedal);
    doc["dutyChange"] = table->dutyChange(key, velocity, pedal) / 100.0;
    doc["coast"] = percent(table->coastDuty(key, pedal));
    doc["hold"] = percent(table->holdDuty(key, duty, pedal));
    int32_t clampMs = table->setting(key, KeySetting::ClampMs, duty, pedal);
    if (clampMs > 0)
    {
      JsonObject clamp = doc["clamp"].to<JsonObject>();
      clamp["offsetMs"] = table->setting(key, KeySetting::ClampOffsetMs, duty, pedal);
      clamp["ms"] = clampMs;
      clamp["duty"] = percent(table->setting(key, KeySetting::ClampDuty, duty, pedal));
    }
    else
    {
      doc["clamp"] = nullptr;
    }
    JsonArray duties = doc["duties"].to<JsonArray>();
    JsonArray tableDuties = doc["tableDuties"].to<JsonArray>();
    for (uint8_t v = 1; v <= VELOCITY_LEVELS; v++)
    {
      duties.add(percent(table->duty(key, v, false)));
      tableDuties.add(percent(table->tableDuty(key, v, false)));
    }
    if (table->hasPedal(key))
    {
      JsonArray pedalDuties = doc["pedalDuties"].to<JsonArray>();
      JsonArray tablePedalDuties = doc["tablePedalDuties"].to<JsonArray>();
      for (uint8_t v = 1; v <= VELOCITY_LEVELS; v++)
      {
        pedalDuties.add(percent(table->duty(key, v, true)));
        tablePedalDuties.add(percent(table->tableDuty(key, v, true)));
      }
    }
    JsonArray landings = doc["landings"].to<JsonArray>();
    for (uint8_t v = 1; v <= VELOCITY_LEVELS; v++)
    {
      landings.add(table->landingMs(key, v, pedal));
    }
    std::shared_ptr<const KeyOffsets> offsets = table->offsets();
    KeyOffsets::Entry entry = offsets != nullptr ? offsets->entry(key) : KeyOffsets::Entry();
    JsonObject keyOffsets = doc["offsets"].to<JsonObject>();
    auto addOffsets = [&](const char *name, const OffsetCurve &curve, bool has) {
      if (!has)
      {
        keyOffsets[name] = nullptr;
        return;
      }
      JsonArray points = keyOffsets[name].to<JsonArray>();
      for (uint8_t i = 0; i < curve.count; i++)
      {
        JsonArray point = points.add<JsonArray>();
        point.add(curve.points[i].velocity);
        point.add(curve.points[i].offset);
      }
    };
    auto addDuty = [&](const char *name, const DutyCurve &curve, bool has) {
      if (!has)
      {
        keyOffsets[name] = nullptr;
        return;
      }
      JsonArray points = keyOffsets[name].to<JsonArray>();
      for (uint8_t i = 0; i < curve.count; i++)
      {
        JsonArray point = points.add<JsonArray>();
        point.add(curve.points[i].velocity);
        point.add(curve.points[i].change / 100.0);
      }
    };
    addOffsets("up", entry.up, true);
    addOffsets("pedal", entry.pedal, entry.hasPedal);
    addDuty("duty", entry.dutyUp, true);
    addDuty("dutyPedal", entry.dutyPedal, entry.hasDutyPedal);
    sendJson(request, 200, doc);
  });

  // POST /api/driver/note {"key": 0-87, "velocity": 1-127, "pedal": true}
  // presses the key as a piece would at that velocity (from its pedal-down
  // entry with "pedal"), and holds it until it's released (R:key:0).
  // -> {"queued": the command}.
  AsyncCallbackJsonWebHandler *noteHandler = new AsyncCallbackJsonWebHandler(
      "/api/driver/note", [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        long key = json["key"] | -1L, velocity = json["velocity"] | -1L;
        if (key < 0 || key >= PIANO_KEY_COUNT || velocity < 1 || velocity > VELOCITY_LEVELS)
        {
          sendError(request, 400, "key must be 0-87 and velocity 1-127");
          return;
        }
        const VelocityTable *table = piano.velocities();
        if (table == nullptr)
        {
          sendError(request, 503, "no velocity table");
          return;
        }
        char command[DRIVER_COMMAND_MAX_LENGTH + 1];
        table->pressCommand(key, velocity, command, sizeof(command), json["pedal"] | false);
        if (piano.submitDriverCommand(command) != SubmitResult::Queued)
        {
          sendError(request, 503, "command queue full");
          return;
        }
        JsonDocument doc;
        doc["queued"] = command;
        sendJson(request, 202, doc);
      });
  noteHandler->setMethod(HTTP_POST);
  server.addHandler(noteHandler);

  // POST /api/test/play {"notes": [[key 0-87, velocity 1-127, on ms, off ms],
  // ...], "pedal": true} plays the notes as a piece would (velocity table,
  // landing, repeat rules and clamp, the pedal held throughout if asked),
  // leaving the loaded piece as it is. -> {"notes", "merged" (repeats too
  // soon to strike again), "durationMs", "presses": [[ms, command], ...]}.
  AsyncCallbackJsonWebHandler *testHandler = new AsyncCallbackJsonWebHandler(
      "/api/test/play", [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonArray notes = json["notes"].as<JsonArray>();
        if (notes.isNull() || notes.size() == 0 || notes.size() > MAX_TEST_NOTES)
        {
          std::string error = "notes must be a list of 1-" + std::to_string(MAX_TEST_NOTES) +
                              " [key, velocity, on ms, off ms]";
          sendError(request, 400, error.c_str());
          return;
        }
        bool pedal = json["pedal"] | false;
        PlayConfig config = piano.playConfig();
        // With the pedal, the first press (sent up to the latest landing time
        // early) comes once it has settled, so every note is a pedal-down one.
        const VelocityTable *timing = piano.velocities();
        uint32_t startMs = pedal ? config.pedalSettleMs + (timing != nullptr ? timing->maxLandingMs() : 0) + 50 : 0;
        std::vector<MidiEvent> events;
        uint32_t endMs = 0;
        for (JsonVariant note : notes)
        {
          JsonArray n = note.as<JsonArray>();
          if (n.isNull() || n.size() != 4 || !n[0].is<long>() || !n[1].is<long>() || !n[2].is<long>() ||
              !n[3].is<long>() || n[0].as<long>() < 0 || n[0].as<long>() >= PIANO_KEY_COUNT ||
              n[1].as<long>() < 1 || n[1].as<long>() > VELOCITY_LEVELS || n[2].as<long>() < 0 ||
              n[3].as<long>() <= n[2].as<long>() || n[3].as<long>() > (long)MAX_TEST_MS)
          {
            sendError(request, 400, "each note must be [key 0-87, velocity 1-127, on ms, off ms after it]");
            return;
          }
          uint8_t midi = (uint8_t)(n[0].as<long>() + LOWEST_NOTE);
          uint32_t on = startMs + n[2].as<uint32_t>(), off = startMs + n[3].as<uint32_t>();
          events.push_back({on, MidiEventType::NoteOn, midi, (uint8_t)n[1].as<long>(), 0});
          events.push_back({off, MidiEventType::NoteOff, midi, 0, 0});
          endMs = std::max(endMs, off);
        }
        if (pedal)
        {
          events.push_back({0, MidiEventType::Sustain, 0, 127, 0});
          events.push_back({endMs, MidiEventType::Sustain, 0, 0, 0});
        }
        // In time order, as the parser gives them: note-offs, then the pedal, then note-ons.
        auto order = [](MidiEventType type) { return type == MidiEventType::NoteOff ? 0 : type == MidiEventType::Sustain ? 1 : 2; };
        std::stable_sort(events.begin(), events.end(), [&](const MidiEvent &a, const MidiEvent &b) {
          return a.timeMs != b.timeMs ? a.timeMs < b.timeMs : order(a.type) < order(b.type);
        });
        std::shared_ptr<PianoRoll> roll = std::make_shared<PianoRoll>();
        roll->title = "test";
        if (!buildPianoRoll(events, *roll, piano.velocities(), Dynamics(), nullptr, &config))
        {
          sendError(request, 500, "out of memory");
          return;
        }
        PlaybackResult result = piano.playTest(roll);
        if (result != PlaybackResult::Ok)
        {
          sendError(request, result == PlaybackResult::QueueFull ? 503 : 409,
                    result == PlaybackResult::Busy     ? "a piece is playing or paused"
                    : result == PlaybackResult::TooHot ? "a solenoid is still cooling"
                    : result == PlaybackResult::Updating ? "the controller is being updated"
                                                       : "command queue full");
          return;
        }
        JsonDocument doc;
        doc["notes"] = roll->noteCount;
        doc["merged"] = roll->mergedNotes;
        doc["durationMs"] = roll->durationMs;
        JsonArray presses = doc["presses"].to<JsonArray>();
        const VelocityTable *table = piano.velocities();
        for (const RollEvent &event : roll->events)
        {
          if (event.type != RollEventType::KeyPress || table == nullptr)
          {
            continue;
          }
          char command[DRIVER_COMMAND_MAX_LENGTH + 1];
          table->strikeCommand(event.key, event.duty, event.landingMs, command, sizeof(command), event.pedal,
                               event.repeat ? &config.repeatClamp : nullptr);
          JsonArray press = presses.add<JsonArray>();
          press.add(event.timeMs);
          press.add(command);
        }
        sendJson(request, 202, doc);
      });
  testHandler->setMethod(HTTP_POST);
  server.addHandler(testHandler);

  server.on("/api/driver/reset", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    if (piano.resetDrivers() != PlaybackResult::Ok)
    {
      sendError(request, 503, "command queue full");
      return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    sendJson(request, 202, doc);
  });
}
