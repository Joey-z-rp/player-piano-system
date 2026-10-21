#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <vector>

// Joins the Wi-Fi network saved on the controller (its Wi-Fi page, or over
// USB: WIFI "network" "password"), else the one in secrets.ini, if any. If
// there's none, or it can't be joined, it starts its own open network,
// SETUP_NETWORK, whose every web address leads to the Wi-Fi page; it keeps
// trying the saved one meanwhile.
class WiFiStationModule
{
public:
  static constexpr const char *SETUP_NETWORK = "Piano setup";
  static constexpr const char *HOSTNAME = "piano"; // piano.local

  struct Network
  {
    String ssid;
    int rssi;
    bool secure;
  };

  WiFiStationModule();

  void begin();
  void poll(); // from loop()

  bool inSetup() const; // running SETUP_NETWORK
  bool isConnected() const;
  IPAddress getIPAddress() const;
  int getRSSI() const;
  String getSSID() const; // the network joined, or SETUP_NETWORK

  // The network it joins at start: saved on the controller, else secrets.ini's.
  String networkToJoin() const;
  bool hasSaved() const;
  bool hasFallback() const; // secrets.ini gave one
  // Saved, to join from the next start. False if the network name is empty
  // or either is too long.
  bool saveNetwork(const String &ssid, const String &password);
  // On the setup network, the next one to join (secrets.ini's) is tried at
  // once; joined, it's from the next start.
  void forgetNetwork();

  // Networks in range: false while a scan is under way (one is started if
  // none is).
  bool scan(std::vector<Network> &networks);

private:
  bool join(const String &ssid, const String &password, uint32_t timeoutMs);
  void startSetup();
  bool loadSaved(String &ssid, String &password) const;

  DNSServer dns;
  bool setup;
  volatile bool tryNow; // set by forgetNetwork (a web request), read by poll()
  uint32_t lastTryMs;
};

#endif // WIFI_STATION_H
