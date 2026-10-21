#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>

class PianoController;
class PieceLibrary;

// Owns the HTTP server. Each feature area registers its own routes
// (see web/api_*.h); static UI files are served from LittleFS.
class WebServerModule
{
public:
  WebServerModule();

  bool begin(PianoController &piano, PieceLibrary &library);

private:
  AsyncWebServer server;
};

#endif // WEB_SERVER_H
