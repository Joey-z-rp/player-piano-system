#include "web/api_response.h"

void sendJson(AsyncWebServerRequest *request, int status, const JsonDocument &doc)
{
  String body;
  serializeJson(doc, body);
  request->send(status, "application/json", body);
}

void sendError(AsyncWebServerRequest *request, int status, const char *message)
{
  JsonDocument doc;
  doc["error"] = message;
  sendJson(request, status, doc);
}
