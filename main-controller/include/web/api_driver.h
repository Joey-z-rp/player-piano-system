#ifndef API_DRIVER_H
#define API_DRIVER_H

#include <ESPAsyncWebServer.h>

class PianoController;

// POST /api/driver/command  {"command": "P:20:60"}
// POST /api/driver/reset    restarts every driver board (stops any song)
// POST /api/pedal           {"down": true}  press the sustain pedal, as a song does
//                           {"down": false} release it gradually
void registerDriverRoutes(AsyncWebServer &server, PianoController &piano);

#endif // API_DRIVER_H
