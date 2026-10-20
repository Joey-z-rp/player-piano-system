#include "web/api_config.h"
#include "web/api_response.h"
#include "app/piano/piano_controller.h"
#include <AsyncJson.h>

// {"config": {...the settings now...}, "defaults": {...}} (see playConfigJson).
static void sendConfig(AsyncWebServerRequest *request, PianoController &piano)
{
  std::string body = "{\"config\":" + playConfigJson(piano.playConfig()) +
                     ",\"defaults\":" + playConfigJson(PlayConfig()) + "}";
  request->send(200, "application/json", body.c_str());
}

void registerConfigRoutes(AsyncWebServer &server, PianoController &piano)
{
  // GET /api/config -> the settings now and their defaults.
  server.on("/api/config", HTTP_GET, [&piano](AsyncWebServerRequest *request) { sendConfig(request, piano); });

  // POST /api/config/reset puts every setting back to its default.
  // Registered before /api/config, which would also match it.
  server.on("/api/config/reset", HTTP_POST, [&piano](AsyncWebServerRequest *request) {
    piano.resetPlayConfig();
    sendConfig(request, piano);
  });

  // POST /api/config with any of the settings, e.g. {"min_note_ms": 15},
  // checks and saves them; they apply from the next Play.
  AsyncCallbackJsonWebHandler *handler =
      new AsyncCallbackJsonWebHandler("/api/config", [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        std::string body;
        serializeJson(json, body);
        std::string error;
        if (!piano.setPlayConfig(body.data(), body.size(), error))
        {
          sendError(request, 400, error.c_str());
          return;
        }
        sendConfig(request, piano);
      });
  handler->setMethod(HTTP_POST);
  server.addHandler(handler);
}
