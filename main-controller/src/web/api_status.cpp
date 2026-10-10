#include "web/api_status.h"
#include "web/api_response.h"
#include "app/piano/piano_controller.h"
#include <WiFi.h>

void registerStatusRoutes(AsyncWebServer &server, PianoController &piano)
{
  server.on("/api/status", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["rssi"] = WiFi.RSSI();
    doc["pendingCommands"] = piano.pendingCommands();

    PlaybackStatus playback = piano.status();
    JsonObject playbackJson = doc["playback"].to<JsonObject>();
    playbackJson["state"] = playback.state == PlaybackState::Playing ? "playing" : "idle";
    playbackJson["positionMs"] = playback.positionMs;
    if (playback.roll != nullptr)
    {
      JsonObject pieceJson = playbackJson["piece"].to<JsonObject>();
      pieceJson["title"] = playback.roll->title;
      pieceJson["durationMs"] = playback.roll->durationMs;
      pieceJson["notes"] = playback.roll->noteCount;
    }
    else
    {
      playbackJson["piece"] = nullptr;
    }

    sendJson(request, 200, doc);
  });
}
