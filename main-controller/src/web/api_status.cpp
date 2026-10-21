#include "web/api_status.h"
#include "web/api_response.h"
#include "app/piano/piano_controller.h"
#include <WiFi.h>
#include <math.h>

void registerStatusRoutes(AsyncWebServer &server, PianoController &piano)
{
  server.on("/api/status", HTTP_GET, [&piano](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["rssi"] = WiFi.RSSI();
    doc["pendingCommands"] = piano.pendingCommands();
    doc["volume"] = piano.volume();
    Dynamics dynamics = piano.dynamics();
    doc["contrast"] = dynamics.contrast;
    doc["voicing"] = dynamics.voicing;
    doc["repeatBoost"] = dynamics.repeatBoost;
    doc["speed"] = dynamics.speed;

    // The solenoids' heat: the hottest watched one (key 0-87, or 88/89 the
    // pedal's), and what was last done about one reaching its limit.
    PianoController::HeatStatus heat = piano.heatStatus();
    JsonObject heatJson = doc["heat"].to<JsonObject>();
    heatJson["watched"] = heat.watched;
    if (heat.watched)
    {
      heatJson["channel"] = heat.channel;
      heatJson["riseC"] = roundf(heat.riseC * 10) / 10;
      heatJson["limitC"] = heat.limitC;
      heatJson["load"] = roundf(heat.load * 1000) / 1000;
      heatJson["resumeLoad"] = HEAT_RESUME_LOAD;
      heatJson["coolS"] = (int)ceilf(heat.coolS);
    }
    static const char *const ACTIONS[] = {"none", "paused", "testStopped", "released"};
    heatJson["action"] = ACTIONS[(int)heat.action];
    heatJson["actionChannel"] = heat.actionChannel;

    PlaybackStatus playback = piano.status();
    JsonObject playbackJson = doc["playback"].to<JsonObject>();
    playbackJson["state"] = playback.testing                          ? "testing"
                            : playback.state == PlaybackState::Playing ? "playing"
                            : playback.state == PlaybackState::Paused ? "paused"
                                                                      : "idle";
    playbackJson["positionMs"] = playback.positionMs;
    playbackJson["building"] = piano.building();
    if (playback.roll != nullptr)
    {
      JsonObject pieceJson = playbackJson["piece"].to<JsonObject>();
      pieceJson["title"] = playback.roll->title;
      pieceJson["durationMs"] = playback.roll->durationMs;
      pieceJson["notes"] = playback.roll->noteCount;
      pieceJson["id"] = playback.roll->id;                 // the same through rebuilds
      pieceJson["speed"] = playback.roll->dynamics.speed; // what its times are at
      pieceJson["leadMs"] = playback.roll->leadMs;
      pieceJson["libraryId"] = playback.roll->libraryId;
    }
    else
    {
      playbackJson["piece"] = nullptr;
    }

    sendJson(request, 200, doc);
  });
}
