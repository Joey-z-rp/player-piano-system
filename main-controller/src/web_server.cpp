#include "web_server.h"
#include <Arduino.h>

WebServerModule::WebServerModule() : server(80)
{
}

void WebServerModule::begin()
{
  server.on("/", HTTP_GET, [this]() {
    server.send(200, "text/plain", "player piano ready");
  });

  server.on("/midi", HTTP_POST, [this]() {
    String body = server.arg("plain");
    Serial.printf("MIDI upload: %u bytes\n", body.length());
    server.send(200, "text/plain", "received");
  });

  server.begin();
  Serial.println("Web server listening on port 80");
}

void WebServerModule::handle()
{
  server.handleClient();
}
