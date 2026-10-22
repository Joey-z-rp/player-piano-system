#include "wifi_station.h"
#include <ESPmDNS.h>
#include <Preferences.h>

static const uint32_t JOIN_TIMEOUT_MS = 20000;
static const uint32_t RETRY_MS = 120000; // in setup, how often the saved network is tried again
static const char NAMESPACE[] = "wifi";
static const char PARTITION[] = "settings"; // the controller's settings (partitions.csv)

#ifdef WIFI_SSID
static const char FALLBACK_SSID[] = WIFI_SSID; // from secrets.ini
#else
static const char FALLBACK_SSID[] = "";
#endif
#ifdef WIFI_PASSWORD
static const char FALLBACK_PASSWORD[] = WIFI_PASSWORD;
#else
static const char FALLBACK_PASSWORD[] = "";
#endif

WiFiStationModule::WiFiStationModule() : setup(false), tryNow(false), lastTryMs(0)
{
}

// Answers for piano.local on the network (multicast DNS), and says there are
// web pages here. Started once; it follows the controller onto networks it
// joins later.
static void startNameService()
{
  static bool started = false;
  if (!started && MDNS.begin(WiFiStationModule::HOSTNAME))
  {
    MDNS.addService("http", "tcp", 80);
    started = true;
  }
}

bool WiFiStationModule::loadSaved(String &ssid, String &password) const
{
  Preferences saved;
  if (!saved.begin(NAMESPACE, true, PARTITION))
  {
    return false;
  }
  ssid = saved.getString("ssid", "");
  password = saved.getString("password", "");
  saved.end();
  return ssid.length() > 0;
}

bool WiFiStationModule::hasSaved() const
{
  String ssid, password;
  return loadSaved(ssid, password);
}

bool WiFiStationModule::hasFallback() const
{
  return FALLBACK_SSID[0] != '\0';
}

String WiFiStationModule::networkToJoin() const
{
  String ssid, password;
  return loadSaved(ssid, password) ? ssid : String(FALLBACK_SSID);
}

bool WiFiStationModule::saveNetwork(const String &ssid, const String &password)
{
  if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63)
  {
    return false;
  }
  Preferences saved;
  if (!saved.begin(NAMESPACE, false, PARTITION))
  {
    return false;
  }
  saved.putString("ssid", ssid);
  saved.putString("password", password);
  saved.end();
  return true;
}

void WiFiStationModule::forgetNetwork()
{
  Preferences saved;
  if (saved.begin(NAMESPACE, false, PARTITION))
  {
    saved.clear();
    saved.end();
  }
  tryNow = setup; // poll() tries the next one at once
}

bool WiFiStationModule::join(const String &ssid, const String &password, uint32_t timeoutMs)
{
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to Wi-Fi: %s", ssid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void WiFiStationModule::begin()
{
  WiFi.persistent(false); // the network is kept here, not by the Wi-Fi library
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  String ssid, password;
  if (!loadSaved(ssid, password))
  {
    ssid = FALLBACK_SSID;
    password = FALLBACK_PASSWORD;
  }
  if (ssid.length() > 0 && join(ssid, password, JOIN_TIMEOUT_MS))
  {
    Serial.printf("Wi-Fi: %s, %s (%d dBm)\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    startNameService();
    return;
  }
  Serial.println(ssid.length() > 0 ? "Wi-Fi: couldn't join it" : "Wi-Fi: no network saved");
  startSetup();
}

void WiFiStationModule::startSetup()
{
  setup = true;
  lastTryMs = millis();
  WiFi.mode(WIFI_AP_STA); // the station side scans, and keeps trying the saved network
  WiFi.softAP(SETUP_NETWORK);
  dns.start(53, "*", WiFi.softAPIP()); // every name leads here, so phones open the setup page
  Serial.printf("Wi-Fi: started \"%s\": join it and open http://%s/settings/#wifi\n", SETUP_NETWORK,
                WiFi.softAPIP().toString().c_str());
  startNameService();
}

void WiFiStationModule::poll()
{
  if (!setup)
  {
    return;
  }
  dns.processNextRequest();
  if (WiFi.status() == WL_CONNECTED)
  {
    // The saved network came back: no more setup network.
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    setup = false;
    Serial.printf("Wi-Fi: joined %s, %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    return;
  }
  String ssid = networkToJoin();
  // Every RETRY_MS, but not while a phone is on the setup network (trying
  // makes it drop for a moment), unless it's just been asked for.
  if (ssid.length() > 0 && (tryNow || (millis() - lastTryMs > RETRY_MS && WiFi.softAPgetStationNum() == 0)))
  {
    lastTryMs = millis();
    tryNow = false;
    String saved, password;
    if (!loadSaved(saved, password))
    {
      password = FALLBACK_PASSWORD;
    }
    WiFi.begin(ssid.c_str(), password.c_str()); // in the background; checked above
  }
}

bool WiFiStationModule::inSetup() const
{
  return setup;
}

bool WiFiStationModule::isConnected() const
{
  return WiFi.status() == WL_CONNECTED;
}

IPAddress WiFiStationModule::getIPAddress() const
{
  return setup ? WiFi.softAPIP() : WiFi.localIP();
}

int WiFiStationModule::getRSSI() const
{
  return isConnected() ? WiFi.RSSI() : 0;
}

String WiFiStationModule::getSSID() const
{
  return setup ? String(SETUP_NETWORK) : WiFi.SSID();
}

bool WiFiStationModule::scan(std::vector<Network> &networks)
{
  int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING)
  {
    return false;
  }
  if (found < 0)
  {
    WiFi.scanNetworks(true); // in the background
    return false;
  }
  networks.clear();
  for (int16_t i = 0; i < found; i++)
  {
    String ssid = WiFi.SSID(i);
    bool seen = false;
    for (Network &n : networks)
    {
      if (n.ssid == ssid)
      {
        n.rssi = max(n.rssi, (int)WiFi.RSSI(i)); // the strongest of its access points
        seen = true;
      }
    }
    if (!seen && ssid.length() > 0)
    {
      networks.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
  }
  WiFi.scanDelete(); // the next asks for a fresh scan
  return true;
}
