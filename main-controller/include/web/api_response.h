#ifndef API_RESPONSE_H
#define API_RESPONSE_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

void sendJson(AsyncWebServerRequest *request, int status, const JsonDocument &doc);
void sendError(AsyncWebServerRequest *request, int status, const char *message);

#endif // API_RESPONSE_H
