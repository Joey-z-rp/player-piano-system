#include "web/web_server.h"
#include "web/api_config.h"
#include "web/api_driver.h"
#include "web/api_dynamics.h"
#include "web/api_piece.h"
#include "web/api_library.h"
#include "web/api_offsets.h"
#include "web/api_playback.h"
#include "web/api_response.h"
#include "web/api_status.h"
#include "web/api_system.h"
#include "wifi_station.h"
#include <LittleFS.h>

WebServerModule::WebServerModule() : server(80)
{
}

bool WebServerModule::begin(PianoController &piano, PieceLibrary &library, WiFiStationModule &wifi, OtaUpdates &ota)
{
  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS mount failed");
    return false;
  }

  registerConfigRoutes(server, piano);
  registerOffsetRoutes(server, piano);
  registerDriverRoutes(server, piano);
  registerPlaybackRoutes(server, piano, library);
  registerLibraryRoutes(server, piano, library);
  registerPieceRoutes(server, piano);
  registerDynamicsRoutes(server, piano, library);
  registerStatusRoutes(server, piano, wifi, ota);
  registerSystemRoutes(server, wifi, ota);

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
      request->redirect("http://" + wifi.getIPAddress().toString() + "/wifi/");
      return;
    }
    request->send(404, "text/plain", "not found");
  });

  server.begin();
  Serial.println("Web server listening on port 80");
  return true;
}
