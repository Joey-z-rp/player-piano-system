#include "web/web_server.h"
#include "web/api_config.h"
#include "web/api_driver.h"
#include "web/api_dynamics.h"
#include "web/api_edits.h"
#include "web/api_piece.h"
#include "web/api_library.h"
#include "web/api_offsets.h"
#include "web/api_playback.h"
#include "web/api_response.h"
#include "web/api_status.h"
#include "web/api_system.h"
#include "wifi_station.h"
#include <LittleFS.h>

static const uint32_t STREAM_CHECK_MS = 100;
static const uint32_t STREAM_MIN_MS = 200;
static const uint32_t STREAM_HEARTBEAT_MS = 3000;

WebServerModule::WebServerModule() : server(80), events("/api/events")
{
}

std::string WebServerModule::statusJson()
{
  JsonDocument doc;
  writeStatus(doc.to<JsonObject>(), *piano, *wifi, *ota);
  std::string json;
  serializeJson(doc, json);
  return json;
}

void WebServerModule::poll()
{
  uint32_t now = millis();
  if (piano == nullptr || now - lastCheckMs < STREAM_CHECK_MS)
  {
    return;
  }
  lastCheckMs = now;
  live.poll();
  if (events.count() == 0)
  {
    return;
  }
  std::string json = statusJson();
  bool changed = json != lastSent;
  if ((changed && now - lastSentMs >= STREAM_MIN_MS) || now - lastSentMs >= STREAM_HEARTBEAT_MS)
  {
    events.send(json.c_str(), "status");
    lastSent = json;
    lastSentMs = now;
  }
}

bool WebServerModule::begin(PianoController &piano, PieceLibrary &library, WiFiStationModule &wifi, OtaUpdates &ota)
{
  this->piano = &piano;
  this->wifi = &wifi;
  this->ota = &ota;
  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS mount failed");
    return false;
  }

  registerConfigRoutes(server, piano);
  registerOffsetRoutes(server, piano);
  registerDriverRoutes(server, piano);
  registerPlaybackRoutes(server, piano, library);
  registerEditRoutes(server, piano, library); // before the library and piece routes
  registerLibraryRoutes(server, piano, library);
  registerPieceRoutes(server, piano);
  registerDynamicsRoutes(server, piano, library);
  registerStatusRoutes(server, piano, wifi, ota);
  registerSystemRoutes(server, wifi, ota);
  live.begin(server, piano);

  // The live status: each page gets it at once on connecting, then as it changes.
  events.onConnect([this](AsyncEventSourceClient *client) { client->send(statusJson().c_str(), "status", 0, 2000); });
  server.addHandler(&events);

  // The pages: Play is the start, and the old pages' addresses lead to where
  // they went.
  auto redirect = [this](const char *from, const char *to) {
    server.on(from, HTTP_GET, [to](AsyncWebServerRequest *request) { request->redirect(to); });
  };
  redirect("/", "/play/");
  redirect("/test", "/keys/");
  redirect("/config", "/settings/#rules");
  redirect("/wifi", "/settings/#wifi");
  redirect("/backup", "/settings/#backup");

  // Each page is a folder under data/www/ with its own index.html,
  // e.g. data/www/test/index.html is served at /test.
  server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

  server.onNotFound([&wifi](AsyncWebServerRequest *request) {
    if (request->url().startsWith("/api/"))
    {
      sendError(request, 404, "not found");
      return;
    }
    if (wifi.inSetup())
    {
      // On the setup network, phones checking for the internet (and anything
      // else) get the Wi-Fi page.
      request->redirect("http://" + wifi.getIPAddress().toString() + "/settings/#wifi");
      return;
    }
    request->send(404, "text/plain", "not found");
  });

  server.begin();
  Serial.println("Web server listening on port 80");
  return true;
}
