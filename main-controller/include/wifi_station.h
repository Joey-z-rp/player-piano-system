#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include <Arduino.h>
#include <WiFi.h>

class WiFiStationModule
{
public:
  WiFiStationModule();
  ~WiFiStationModule();

  // Initialize WiFi Station with credentials
  bool init(const char *ssid, const char *password);

  // Initialize WiFi Station with default credentials
  bool init();

  // Check if connected to WiFi
  bool isConnected() const;

  // Get the IP address
  IPAddress getIPAddress() const;

  // Get WiFi signal strength
  int getRSSI() const;

  // Get SSID of connected network
  String getSSID() const;

  // Disconnect from WiFi
  void disconnect();

  // Set WiFi credentials
  void setCredentials(const char *ssid, const char *password);

private:
  bool connected;
  String wifiSSID;
  String wifiPassword;

  // Default WiFi credentials (change these to your network)
  static const char *DEFAULT_SSID;
  static const char *DEFAULT_PASSWORD;

  // Connection timeout in milliseconds
  static const unsigned long CONNECTION_TIMEOUT;
};

#endif // WIFI_STATION_H
