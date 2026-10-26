#include "web/api_system.h"
#include "web/api_response.h"
#include "crash_report.h"
#include "firmware_info.h"
#include "ota_updates.h"
#include "wifi_station.h"
#include <AsyncJson.h>
#include <algorithm>
#include <esp_spi_flash.h>

static volatile uint32_t restartAtMs = 0; // 0: not asked

void restartIfAsked()
{
  if (restartAtMs != 0 && (int32_t)(millis() - restartAtMs) >= 0)
  {
    ESP.restart();
  }
}

// The system's state: also in /api/status (see api_status.cpp).
void writeSystem(JsonObject out, WiFiStationModule &wifi, OtaUpdates &ota)
{
  JsonObject firmware = out["firmware"].to<JsonObject>();
  firmware["version"] = firmwareVersion();
  firmware["built"] = firmwareBuilt();
  firmware["slot"] = firmwareSlot();
  firmware["onTrial"] = firmwareOnTrial();
  firmware["rolledBack"] = firmwareRolledBack();
  JsonObject restart = out["restart"].to<JsonObject>();
  restart["reason"] = restartReason();
  const CrashInfo &crash = lastCrash();
  restart["lastUnexpected"] = crash.reason; // "" if none known
  if (crash.present)
  {
    JsonObject c = out["crash"].to<JsonObject>();
    char hex[12];
    c["task"] = crash.task;
    snprintf(hex, sizeof(hex), "0x%08x", (unsigned)crash.pc);
    c["pc"] = hex;
    c["cause"] = exceptionName(crash.cause);
    snprintf(hex, sizeof(hex), "0x%08x", (unsigned)crash.address);
    c["address"] = hex;
    JsonArray backtrace = c["backtrace"].to<JsonArray>();
    for (uint32_t pc : crash.backtrace)
    {
      snprintf(hex, sizeof(hex), "0x%08x", (unsigned)pc);
      backtrace.add(hex);
    }
    c["backtraceCorrupted"] = crash.backtraceCorrupted;
    c["firmware"] = crash.firmwareSha;
    c["thisFirmware"] = crash.thisFirmware;
    c["bytes"] = crash.bytes;
  }
  else
  {
    out["crash"] = nullptr;
  }
  JsonObject update = out["update"].to<JsonObject>();
  update["kind"] = ota.updating() == OtaUpdates::Kind::Firmware ? "firmware"
                   : ota.updating() == OtaUpdates::Kind::Files  ? "files"
                                                                 : "none";
  update["percent"] = ota.percent();
  JsonObject net = out["wifi"].to<JsonObject>();
  net["setup"] = wifi.inSetup();
  net["network"] = wifi.getSSID();
  net["ip"] = wifi.getIPAddress().toString();
  net["rssi"] = wifi.getRSSI();
  net["joins"] = wifi.networkToJoin();
  net["saved"] = wifi.hasSaved();
  net["fallback"] = wifi.hasFallback();
}

void registerSystemRoutes(AsyncWebServer &server, WiFiStationModule &wifi, OtaUpdates &ota)
{
  // Before /api/system, which would also answer these.
  // GET /api/system/crash -> the last crash's core dump, as stored (for
  // tools/crash_report.py or espcoredump); 404 if there's none.
  server.on("/api/system/crash", HTTP_GET, [](AsyncWebServerRequest *request) {
    size_t address = 0, size = 0;
    if (!crashImage(address, size))
    {
      sendError(request, 404, "no crash recorded");
      return;
    }
    AsyncWebServerResponse *response = request->beginResponse(
        "application/octet-stream", size, [address, size](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
          size_t length = std::min(maxLen, size - index);
          return spi_flash_read(address + index, buffer, length) == ESP_OK ? length : 0;
        });
    std::string name = "attachment; filename=\"crash-" + lastCrash().firmwareSha + ".bin\"";
    response->addHeader("Content-Disposition", name.c_str());
    request->send(response);
  });

  // POST /api/system/crash/clear forgets it.
  server.on("/api/system/crash/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!clearCrash())
    {
      sendError(request, 500, "couldn't clear it");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/system", HTTP_GET, [&wifi, &ota](AsyncWebServerRequest *request) {
    JsonDocument doc;
    writeSystem(doc.to<JsonObject>(), wifi, ota);
    sendJson(request, 200, doc);
  });

  // GET /api/wifi/scan -> {"scanning": true} while it looks, then
  // {"networks": [{"ssid", "rssi", "secure"}, ...]}, strongest first.
  server.on("/api/wifi/scan", HTTP_GET, [&wifi](AsyncWebServerRequest *request) {
    std::vector<WiFiStationModule::Network> networks;
    JsonDocument doc;
    if (!wifi.scan(networks))
    {
      doc["scanning"] = true;
    }
    else
    {
      std::sort(networks.begin(), networks.end(),
                [](const WiFiStationModule::Network &a, const WiFiStationModule::Network &b) { return a.rssi > b.rssi; });
      JsonArray list = doc["networks"].to<JsonArray>();
      for (const WiFiStationModule::Network &n : networks)
      {
        JsonObject item = list.add<JsonObject>();
        item["ssid"] = n.ssid;
        item["rssi"] = n.rssi;
        item["secure"] = n.secure;
      }
    }
    sendJson(request, 200, doc);
  });

  // POST /api/wifi/forget: the saved network goes; secrets.ini's (if any) is
  // joined instead: at once on the setup network, else from the next start.
  // -> {"ok", "joins": the network now joined at start, "tryingNow"}.
  server.on("/api/wifi/forget", HTTP_POST, [&wifi](AsyncWebServerRequest *request) {
    wifi.forgetNetwork();
    JsonDocument doc;
    doc["ok"] = true;
    doc["joins"] = wifi.networkToJoin();
    doc["tryingNow"] = wifi.inSetup() && wifi.networkToJoin().length() > 0;
    sendJson(request, 200, doc);
  });

  // POST /api/wifi {"ssid", "password"} saves the network and restarts to
  // join it (from the setup network, the phone then rejoins its own).
  AsyncCallbackJsonWebHandler *saveHandler = new AsyncCallbackJsonWebHandler(
      "/api/wifi", [&wifi](AsyncWebServerRequest *request, JsonVariant &json) {
        String ssid = json["ssid"] | "", password = json["password"] | "";
        if (!wifi.saveNetwork(ssid, password))
        {
          sendError(request, 400, "the network's name must be 1-32 characters and its password up to 63");
          return;
        }
        JsonDocument doc;
        doc["ok"] = true;
        doc["restarting"] = true;
        sendJson(request, 200, doc);
        restartAtMs = millis() + 1500; // once the reply is out
      });
  saveHandler->setMethod(HTTP_POST);
  server.addHandler(saveHandler); // last: it'd also answer /api/wifi/...
}
