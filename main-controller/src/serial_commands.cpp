#include "serial_commands.h"
#include "app/piano/piano_controller.h"
#include "wifi_station.h"

SerialCommands::SerialCommands() : piano(nullptr), wifi(nullptr), length(0), overflow(false)
{
}

void SerialCommands::begin(PianoController *piano, WiFiStationModule *wifi)
{
  this->piano = piano;
  this->wifi = wifi;
}

// Reads a "quoted" word (\" for a quote inside), or a plain one up to a space.
static bool readWord(const String &text, size_t &at, String &word)
{
  while (at < text.length() && text[at] == ' ')
  {
    at++;
  }
  word = "";
  if (at >= text.length())
  {
    return false;
  }
  if (text[at] != '"')
  {
    while (at < text.length() && text[at] != ' ')
    {
      word += text[at++];
    }
    return true;
  }
  for (at++; at < text.length(); at++)
  {
    if (text[at] == '\\' && at + 1 < text.length())
    {
      word += text[++at];
    }
    else if (text[at] == '"')
    {
      at++;
      return true;
    }
    else
    {
      word += text[at];
    }
  }
  return false; // no closing quote
}

void SerialCommands::handleWifi(const String &arguments)
{
  if (wifi == nullptr)
  {
    Serial.println("#ERR not ready");
    return;
  }
  if (arguments.length() == 0)
  {
    Serial.printf("#WIFI on \"%s\" (%s), joins \"%s\" (%s)\n", wifi->getSSID().c_str(),
                  wifi->getIPAddress().toString().c_str(), wifi->networkToJoin().c_str(),
                  wifi->hasSaved() ? "saved" : wifi->hasFallback() ? "secrets.ini" : "none");
    Serial.println("#OK");
    return;
  }
  if (arguments == "FORGET")
  {
    wifi->forgetNetwork();
    Serial.println("#OK");
    return;
  }
  size_t at = 0;
  String ssid, password, rest;
  bool named = readWord(arguments, at, ssid);
  rest = arguments.substring(at);
  rest.trim();
  bool passwordRead = rest.length() == 0 || readWord(arguments, at, password); // none, for an open network
  if (!named || !passwordRead || !wifi->saveNetwork(ssid, password))
  {
    Serial.println("#ERR use: WIFI \"network\" \"password\" (a name of 1-32 characters, a password up to 63)");
    return;
  }
  Serial.printf("Saved Wi-Fi \"%s\": restarting to join it\n", ssid.c_str());
  Serial.println("#OK");
  Serial.flush();
  delay(200);
  ESP.restart();
}

void SerialCommands::poll()
{
  while (Serial.available() > 0)
  {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (overflow)
      {
        Serial.println("#ERR line too long");
      }
      else if (length > 0)
      {
        line[length] = '\0';
        handleLine();
      }
      length = 0;
      overflow = false;
      continue;
    }
    if (length < sizeof(line) - 1)
    {
      line[length++] = c;
    }
    else
    {
      overflow = true;
    }
  }
}

void SerialCommands::handleLine()
{
  if (piano == nullptr)
  {
    Serial.println("#ERR not ready");
    return;
  }

  String command(line);
  command.trim();

  if (command == "STOP")
  {
    Serial.println(piano->stop() == PlaybackResult::Ok ? "#OK" : "#ERR queue full");
    return;
  }
  if (command == "WIFI" || command.startsWith("WIFI "))
  {
    handleWifi(command.length() > 5 ? command.substring(5) : String());
    return;
  }
  if (command == "CONFIG")
  {
    // The play config, for the tools: one line, then #OK.
    Serial.printf("#CONFIG %s\n", playConfigJson(piano->playConfig()).c_str());
    Serial.println("#OK");
    return;
  }
  if (command == "OFFSETS")
  {
    // The per-key velocity offsets, for the tools: one line, then #OK.
    Serial.printf("#OFFSETS %s\n", piano->keyOffsetsJson().c_str());
    Serial.println("#OK");
    return;
  }
  if (command == "RESET")
  {
    Serial.println(piano->resetDrivers() == PlaybackResult::Ok ? "#OK" : "#ERR queue full");
    return;
  }

  switch (piano->submitDriverCommand(command))
  {
  case SubmitResult::Queued:
    Serial.println("#OK");
    break;
  case SubmitResult::Invalid:
    Serial.println("#ERR invalid command");
    break;
  case SubmitResult::QueueFull:
    Serial.println("#ERR queue full");
    break;
  }
}
