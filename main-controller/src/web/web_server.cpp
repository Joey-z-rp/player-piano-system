#include "web/web_server.h"
#include "web/api_driver.h"
#include "web/api_response.h"
#include "web/api_status.h"
#include <LittleFS.h>

WebServerModule::WebServerModule() : server(80)
{
}

bool WebServerModule::begin(PianoController &piano)
{
  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS mount failed");
    return false;
  }

  registerDriverRoutes(server, piano);
  registerStatusRoutes(server, piano);

  // Each page is a folder under data/www/ with its own index.html,
  // e.g. data/www/test/index.html is served at /test.
  server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->url().startsWith("/api/"))
    {
      sendError(request, 404, "not found");
      return;
    }
    request->send(404, "text/plain", "not found");
  });

  server.begin();
  Serial.println("Web server listening on port 80");
  return true;
}
