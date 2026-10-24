#ifndef LIVE_SOCKET_H
#define LIVE_SOCKET_H

#include <ESPAsyncWebServer.h>
#include <atomic>

class PianoController;

// The Live page's connection (/ws/live): a WebSocket carrying what's played
// on a MIDI keyboard to the piano (PianoController::startLive). One page
// plays at a time. Messages are JSON, from the page:
//   {"start": delay ms}      starts a session, with every key let go first
//   {"delay": delay ms}      changes the delay
//   {"m": [[ms, status, data1, data2], ...]}  MIDI messages, with when the
//                            page got each (performance.now())
//   {"ping": ms}             every 500 ms: keeps the session alive, and
//                            follows the page's clock
//   {"stop": true}
// and to it: {"started": delay ms}, {"pong": ms, "late": notes too late for
// the delay}, {"stopped": "heat", "quiet" or "stopped"} when the piano ends
// the session, {"error": text}. Closing the page ends it too.
class LiveSocket
{
public:
  LiveSocket();
  void begin(AsyncWebServer &server, PianoController &piano);
  // From loop(): tidies up, and tells the page when the piano has ended its session.
  void poll();

private:
  void onEvent(AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t length);
  void onText(AsyncWebSocketClient *client, const char *text, size_t length);

  AsyncWebSocket socket;
  PianoController *piano = nullptr;
  std::atomic<uint32_t> sender{0}; // the page playing (its client id), 0 for none
};

#endif // LIVE_SOCKET_H
