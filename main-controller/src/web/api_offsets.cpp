#include "web/api_offsets.h"
#include "web/api_response.h"
#include "app/piano/piano_controller.h"
#include <AsyncJson.h>

static void sendOffsets(AsyncWebServerRequest *request, PianoController &piano, int status = 200)
{
  request->send(status, "application/json", piano.keyOffsetsJson().c_str());
}

// Reads one of a key's curves from the request's JSON.
static bool readCurve(JsonVariant value, OffsetCurve &curve, std::string &error)
{
  std::string json;
  serializeJson(value, json);
  return readOffsetCurve(json.data(), json.size(), curve, error);
}

void registerOffsetRoutes(AsyncWebServer &server, PianoController &piano)
{
  // GET /api/offsets -> {"keys": {"44": {"up": [[velocity, offset], ...],
  // "pedal": [...]}, ...}}: only keys with offsets, "pedal" only where a key
  // has its own pedal-down curve.
  server.on("/api/offsets", HTTP_GET, [&piano](AsyncWebServerRequest *request) { sendOffsets(request, piano); });

  // POST /api/offsets/key {"key": 44, "up": [[64, -4]], "pedal": [[64, -2]]}
  // sets one key's curves: "up" ([] for none), and its own "pedal" one (left
  // out or null: none, it uses "up" with the pedal down too). -> all of them.
  AsyncCallbackJsonWebHandler *keyHandler = new AsyncCallbackJsonWebHandler(
      "/api/offsets/key", [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        long key = json["key"] | -1L;
        if (key < 0 || key >= PIANO_KEY_COUNT)
        {
          sendError(request, 400, "key must be 0-87");
          return;
        }
        OffsetCurve up, pedal;
        std::string error;
        bool hasPedal = !json["pedal"].isNull();
        if ((!json["up"].isNull() && !readCurve(json["up"], up, error)) ||
            (hasPedal && !readCurve(json["pedal"], pedal, error)))
        {
          sendError(request, 400, error.c_str());
          return;
        }
        piano.setKeyOffsets((uint8_t)key, up, hasPedal ? &pedal : nullptr);
        sendOffsets(request, piano);
      });
  keyHandler->setMethod(HTTP_POST);
  server.addHandler(keyHandler);

  // POST /api/offsets/clear removes every key's offsets.
  server.on("/api/offsets/clear", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    piano.clearKeyOffsets();
    sendOffsets(request, piano);
  });

  // Last: it would also answer /api/offsets/... (the server matches what follows a "/").
  // POST /api/offsets {"keys": {...}} replaces every key's offsets (as GET
  // gives them: a backup), all or nothing.
  AsyncCallbackJsonWebHandler *allHandler = new AsyncCallbackJsonWebHandler(
      "/api/offsets", [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        std::string body, error;
        serializeJson(json, body);
        KeyOffsets offsets;
        if (!readKeyOffsets(body.data(), body.size(), offsets, error))
        {
          sendError(request, 400, error.c_str());
          return;
        }
        piano.setAllKeyOffsets(offsets);
        sendOffsets(request, piano);
      });
  allHandler->setMethod(HTTP_POST);
  server.addHandler(allHandler);
}
