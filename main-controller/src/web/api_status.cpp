#include "web/api_status.h"
#include "web/api_response.h"
#include "app/piano_controller.h"
#include <WiFi.h>

void registerStatusRoutes(AsyncWebServer &server, PianoController &piano)
{
  server.on("/api/status", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["rssi"] = WiFi.RSSI();
    doc["pendingCommands"] = piano.pendingCommands();
    sendJson(request, 200, doc);
  });
}
