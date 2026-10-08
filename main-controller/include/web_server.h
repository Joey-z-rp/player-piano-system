#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>

class WebServerModule
{
public:
  WebServerModule();

  void begin();
  void handle();

private:
  WebServer server;
};

#endif // WEB_SERVER_H
