#include "web/live_socket.h"
#include "app/piano/piano_controller.h"
#include <ArduinoJson.h>

LiveSocket::LiveSocket() : socket("/ws/live")
{
}

void LiveSocket::begin(AsyncWebServer &server, PianoController &piano)
{
  this->piano = &piano;
  socket.onEvent([this](AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
                        size_t length) { onEvent(client, type, arg, data, length); });
  server.addHandler(&socket);
}

static const char *endReason(PianoController::LiveEnd why)
{
  switch (why)
  {
  case PianoController::LiveEnd::TooHot:
    return "heat";
  case PianoController::LiveEnd::TimedOut:
    return "quiet";
  default:
    return "stopped";
  }
}

void LiveSocket::poll()
{
  socket.cleanupClients();
  uint32_t id = sender;
  if (id == 0 || piano->liveStatus().active)
  {
    return;
  }
  // Ended by the piano (the heat limit, Stop elsewhere, a silence).
  sender = 0;
  AsyncWebSocketClient *client = socket.client(id);
  if (client != nullptr)
  {
    std::string text = std::string("{\"stopped\":\"") + endReason(piano->liveStatus().ended) + "\"}";
    client->text(text.c_str());
  }
}

void LiveSocket::onEvent(AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t length)
{
  if (type == WS_EVT_DISCONNECT)
  {
    uint32_t id = client->id();
    if (sender.compare_exchange_strong(id, 0))
    {
      piano->stopLive(); // the page closed: let everything go
    }
    return;
  }
  if (type != WS_EVT_DATA)
  {
    return;
  }
  AwsFrameInfo *info = static_cast<AwsFrameInfo *>(arg);
  // Messages are small: whole in one frame.
  if (info->opcode == WS_TEXT && info->final && info->index == 0 && info->len == length)
  {
    onText(client, reinterpret_cast<const char *>(data), length);
  }
}

void LiveSocket::onText(AsyncWebSocketClient *client, const char *text, size_t length)
{
  JsonDocument doc;
  if (deserializeJson(doc, text, length))
  {
    client->text("{\"error\":\"not JSON\"}");
    return;
  }
  uint32_t id = client->id();
  bool playing = sender == id;
  if (!doc["start"].isNull())
  {
    uint32_t other = sender;
    if (other != 0 && other != id && socket.client(other) != nullptr)
    {
      client->text("{\"error\":\"Someone else is playing live\"}");
      return;
    }
    long delayMs = doc["start"] | (long)DEFAULT_LIVE_DELAY_MS;
    delayMs = delayMs < MIN_LIVE_DELAY_MS ? MIN_LIVE_DELAY_MS : delayMs > MAX_LIVE_DELAY_MS ? MAX_LIVE_DELAY_MS : delayMs;
    switch (piano->startLive((uint16_t)delayMs))
    {
    case PlaybackResult::Ok:
      sender = id;
      client->text(("{\"started\":" + std::to_string(delayMs) + "}").c_str());
      break;
    case PlaybackResult::TooHot:
      client->text("{\"error\":\"The solenoids are still cooling down\"}");
      break;
    case PlaybackResult::Updating:
      client->text("{\"error\":\"The piano is updating\"}");
      break;
    default:
      client->text("{\"error\":\"A piece is playing: stop it first\"}");
      break;
    }
    return;
  }
  if (!playing)
  {
    if (!doc["ping"].isNull())
    {
      client->text(("{\"pong\":" + std::to_string(doc["ping"].as<uint32_t>()) + ",\"late\":0}").c_str());
    }
    return; // not this page's session
  }
  if (doc["m"].is<JsonArray>())
  {
    for (JsonArray m : doc["m"].as<JsonArray>())
    {
      if (m.size() == 4)
      {
        piano->liveMessage(m[0].as<uint32_t>(), m[1].as<uint8_t>(), m[2].as<uint8_t>(), m[3].as<uint8_t>());
      }
    }
  }
  if (!doc["delay"].isNull())
  {
    long delayMs = doc["delay"].as<long>();
    piano->setLiveDelay((uint16_t)(delayMs < 0 ? 0 : delayMs > MAX_LIVE_DELAY_MS ? MAX_LIVE_DELAY_MS : delayMs));
  }
  if (!doc["ping"].isNull())
  {
    piano->liveClock(doc["ping"].as<uint32_t>());
    client->text(("{\"pong\":" + std::to_string(doc["ping"].as<uint32_t>()) +
                  ",\"late\":" + std::to_string(piano->liveStatus().late) + "}")
                     .c_str());
  }
  if (!doc["stop"].isNull())
  {
    sender = 0;
    piano->stopLive();
    client->text("{\"stopped\":\"stopped\"}");
  }
}
