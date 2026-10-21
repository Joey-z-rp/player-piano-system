#include "web/api_dynamics.h"
#include "web/api_response.h"
#include "app/library/piece_library.h"
#include "app/piano/piano_controller.h"
#include <AsyncJson.h>
#include <algorithm>

static void sendDynamics(AsyncWebServerRequest *request, PianoController &piano)
{
  Dynamics dynamics = piano.dynamics();
  std::shared_ptr<const PianoRoll> roll = piano.status().roll;
  JsonDocument doc;
  doc["volume"] = dynamics.volume;
  doc["contrast"] = dynamics.contrast;
  doc["voicing"] = dynamics.voicing;
  doc["repeatBoost"] = dynamics.repeatBoost;
  doc["speed"] = dynamics.speed;
  JsonArray muted = doc["muted"].to<JsonArray>();
  for (size_t i = 0; roll != nullptr && i < roll->partNames.size(); i++)
  {
    muted.add((dynamics.mutedParts >> i) & 1 ? true : false);
  }
  JsonArray parts = doc["parts"].to<JsonArray>();
  for (size_t i = 0; roll != nullptr && i < roll->partNames.size(); i++)
  {
    parts.add(dynamics.partOffset[i]);
  }
  sendJson(request, 200, doc);
}

// A whole number within [low, high], if given.
static bool readSetting(JsonVariant value, const char *name, long low, long high, long &out, std::string &error)
{
  if (!value.is<long>() || value.as<long>() < low || value.as<long>() > high)
  {
    error = std::string(name) + " must be a whole number from " + std::to_string(low) + " to " + std::to_string(high);
    return false;
  }
  out = value.as<long>();
  return true;
}

// Checks everything given before setting any of it.
static bool applyDynamics(JsonVariant json, PianoController &piano, std::string &error)
{
  long volume = -1, contrast = -1, voicing = -1, boost = -1, speed = -1;
  if (!json["volume"].isNull() && !readSetting(json["volume"], "volume", MIN_VOLUME, MAX_VOLUME, volume, error))
  {
    return false;
  }
  if (!json["contrast"].isNull() &&
      !readSetting(json["contrast"], "contrast", MIN_CONTRAST, MAX_CONTRAST, contrast, error))
  {
    return false;
  }
  if (!json["voicing"].isNull() && !readSetting(json["voicing"], "voicing", MIN_VOICING, MAX_VOICING, voicing, error))
  {
    return false;
  }
  if (!json["repeatBoost"].isNull() &&
      !readSetting(json["repeatBoost"], "repeatBoost", 0, MAX_REPEAT_BOOST, boost, error))
  {
    return false;
  }
  if (!json["speed"].isNull() && !readSetting(json["speed"], "speed", MIN_SPEED, MAX_SPEED, speed, error))
  {
    return false;
  }
  std::vector<long> offsets;
  JsonVariant parts = json["parts"];
  if (!parts.isNull())
  {
    if (!parts.is<JsonArray>() || parts.size() > MAX_MIDI_PARTS)
    {
      error = "parts must be a list of up to " + std::to_string(MAX_MIDI_PARTS) + " offsets";
      return false;
    }
    for (JsonVariant part : parts.as<JsonArray>())
    {
      long offset;
      if (!readSetting(part, "each part's offset", -MAX_PART_OFFSET, MAX_PART_OFFSET, offset, error))
      {
        return false;
      }
      offsets.push_back(offset);
    }
  }
  std::vector<bool> muted;
  JsonVariant mutedJson = json["muted"];
  if (!mutedJson.isNull())
  {
    if (!mutedJson.is<JsonArray>() || mutedJson.size() > MAX_MIDI_PARTS)
    {
      error = "muted must be a list of up to " + std::to_string(MAX_MIDI_PARTS) + " true or false";
      return false;
    }
    for (JsonVariant part : mutedJson.as<JsonArray>())
    {
      if (!part.is<bool>())
      {
        error = "muted must be a list of true or false";
        return false;
      }
      muted.push_back(part.as<bool>());
    }
  }
  if (volume >= 0)
  {
    piano.setVolume((uint16_t)volume);
  }
  if (speed >= 0)
  {
    piano.setSpeed((uint16_t)speed);
  }
  if (contrast >= 0)
  {
    piano.setContrast((uint16_t)contrast);
  }
  if (voicing >= 0)
  {
    piano.setVoicing((uint16_t)voicing);
  }
  if (boost >= 0)
  {
    piano.setRepeatBoost((uint8_t)boost);
  }
  for (size_t i = 0; i < offsets.size(); i++)
  {
    piano.setPartOffset((uint8_t)i, (int8_t)offsets[i]);
  }
  for (size_t i = 0; i < muted.size(); i++)
  {
    piano.setPartMuted((uint8_t)i, muted[i]);
  }
  return true;
}

void registerDynamicsRoutes(AsyncWebServer &server, PianoController &piano, PieceLibrary &library)
{
  // GET /api/dynamics -> {"volume", "contrast", "voicing", "repeatBoost", "speed", "muted": [true if
  // left out, for each of the loaded piece's parts], "parts":
  // [offset of each of the loaded piece's parts]}; POST any of them to set them, e.g.
  // {"contrast": 130} or {"parts": [8, -8]}. They apply from the next Play,
  // except a new speed applies at once when nothing is playing.
  server.on("/api/dynamics", HTTP_GET, [&piano](AsyncWebServerRequest *request) { sendDynamics(request, piano); });
  AsyncCallbackJsonWebHandler *dynamicsHandler = new AsyncCallbackJsonWebHandler(
      "/api/dynamics", [&piano, &library](AsyncWebServerRequest *request, JsonVariant &json) {
        std::string error;
        if (!applyDynamics(json, piano, error))
        {
          sendError(request, 400, error.c_str());
          return;
        }
        // A library piece remembers them all.
        std::shared_ptr<const PianoRoll> roll = piano.status().roll;
        if (roll != nullptr && !roll->libraryId.empty())
        {
          library.saveSettings(roll->libraryId, piano.dynamics(), roll->partNames.size());
        }
        sendDynamics(request, piano);
      });
  dynamicsHandler->setMethod(HTTP_POST);
  server.addHandler(dynamicsHandler);
}
