#include "web/api_offsets.h"
#include "web/api_response.h"
#include "app/piano/piano_controller.h"
#include <AsyncJson.h>
#include <memory>

static void sendOffsets(AsyncWebServerRequest *request, PianoController &piano, int status = 200)
{
  request->send(status, "application/json", piano.keyOffsetsJson().c_str());
}

void registerOffsetRoutes(AsyncWebServer &server, PianoController &piano)
{
  // GET /api/offsets -> {"keys": {"44": {"up": [[velocity, offset], ...],
  // "pedal": [...], "duty": [[velocity, duty change %], ...], "duty_pedal":
  // [...]}, ...}}: only keys with something set; "pedal", "duty" and
  // "duty_pedal" only where a key has them (key_offsets.h readKeyEntry).
  server.on("/api/offsets", HTTP_GET, [&piano](AsyncWebServerRequest *request) { sendOffsets(request, piano); });

  // POST /api/offsets/key {"key": 44, "up": [[64, -4]], "pedal": [[64, -2]],
  // "duty": [[64, 0], [127, 23]], "duty_pedal": [...]} sets all of one key's
  // curves: any left out or null is none ("pedal" and "duty_pedal": it uses
  // the pedal-up one with the pedal down too). -> all keys'.
  AsyncCallbackJsonWebHandler *keyHandler = new AsyncCallbackJsonWebHandler(
      "/api/offsets/key", [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        long key = json["key"] | -1L;
        if (key < 0 || key >= PIANO_KEY_COUNT)
        {
          sendError(request, 400, "key must be 0-87");
          return;
        }
        std::string body, error;
        serializeJson(json, body);
        KeyOffsets::Entry entry;
        if (!readKeyEntry(body.data(), body.size(), entry, error))
        {
          sendError(request, 400, error.c_str());
          return;
        }
        piano.setKeyOffsets((uint8_t)key, entry);
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
        std::unique_ptr<KeyOffsets> offsets(new KeyOffsets()); // ~9 KB: not on the stack
        if (!readKeyOffsets(body.data(), body.size(), *offsets, error))
        {
          sendError(request, 400, error.c_str());
          return;
        }
        piano.setAllKeyOffsets(*offsets);
        sendOffsets(request, piano);
      });
  allHandler->setMethod(HTTP_POST);
  server.addHandler(allHandler);
}
