#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <string>
#include "web/live_socket.h"
#include "web/notes_reports.h"

class PianoController;
class OtaUpdates;
class PieceLibrary;
class WiFiStationModule;

// Owns the HTTP server. Each feature area registers its own routes
// (see web/api_*.h); static UI files are served from LittleFS.
class WebServerModule
{
public:
  WebServerModule();

  bool begin(PianoController &piano, PieceLibrary &library, WiFiStationModule &wifi, OtaUpdates &ota);
  // From loop(): streams the status to the pages listening (/api/events).
  void poll();

private:
  AsyncWebServer server;
  // GET /api/events: the status (as /api/status gives it) as server-sent
  // "status" events, whenever it changes (at most every STREAM_MIN_MS) and at
  // least every STREAM_HEARTBEAT_MS, so the pages don't poll.
  AsyncEventSource events;
  LiveSocket live; // the Live page's connection (/ws/live)
  NotesReports notesReports; // the note view's, built in the background
  PianoController *piano = nullptr;
  WiFiStationModule *wifi = nullptr;
  OtaUpdates *ota = nullptr;
  std::string lastSent;
  uint32_t lastSentMs = 0;
  uint32_t lastCheckMs = 0;
  std::string statusJson();
};

#endif // WEB_SERVER_H
