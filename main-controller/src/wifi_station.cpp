#include "wifi_station.h"

// Default WiFi credentials - CHANGE THESE TO YOUR NETWORK
const char *WiFiStationModule::DEFAULT_SSID = "YOUR_WIFI_SSID";
const char *WiFiStationModule::DEFAULT_PASSWORD = "YOUR_WIFI_PASSWORD";
const unsigned long WiFiStationModule::CONNECTION_TIMEOUT = 20000; // 20 seconds

WiFiStationModule::WiFiStationModule() : connected(false)
{
  wifiSSID = DEFAULT_SSID;
  wifiPassword = DEFAULT_PASSWORD;
}

WiFiStationModule::~WiFiStationModule()
{
  disconnect();
}

bool WiFiStationModule::init()
{
  return init(DEFAULT_SSID, DEFAULT_PASSWORD);
}

bool WiFiStationModule::init(const char *ssid, const char *password)
{
  if (connected)
  {
    Serial.println("WiFi already connected!");
    return true;
  }

  // Set credentials
  setCredentials(ssid, password);

  // Disable WiFi mode first
  WiFi.mode(WIFI_OFF);
  delay(100);

  // Set WiFi mode to station
  WiFi.mode(WIFI_STA);
  delay(100);

  // Begin WiFi connection
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  Serial.printf("Connecting to WiFi: %s", wifiSSID.c_str());

  // Wait for connection with timeout
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < CONNECTION_TIMEOUT)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    connected = true;
    Serial.println();
    Serial.println("WiFi connected successfully!");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal Strength: %d dBm\n", WiFi.RSSI());
    Serial.printf("MAC Address: %s\n", WiFi.macAddress().c_str());
    return true;
  }
  else
  {
    connected = false;
    Serial.println();
    Serial.println("WiFi connection failed!");
    Serial.println("Please check your credentials and network availability.");
    return false;
  }
}

bool WiFiStationModule::isConnected() const
{
  return connected && (WiFi.status() == WL_CONNECTED);
}

IPAddress WiFiStationModule::getIPAddress() const
{
  if (isConnected())
  {
    return WiFi.localIP();
  }
  return IPAddress(0, 0, 0, 0);
}

int WiFiStationModule::getRSSI() const
{
  if (isConnected())
  {
    return WiFi.RSSI();
  }
  return 0;
}

String WiFiStationModule::getSSID() const
{
  if (isConnected())
  {
    return WiFi.SSID();
  }
  return "";
}

void WiFiStationModule::disconnect()
{
  if (connected)
  {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    connected = false;
    Serial.println("WiFi disconnected");
  }
}

void WiFiStationModule::setCredentials(const char *ssid, const char *password)
{
  if (ssid != nullptr)
  {
    wifiSSID = String(ssid);
  }

  if (password != nullptr)
  {
    wifiPassword = String(password);
  }
}
