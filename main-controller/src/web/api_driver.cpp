#include "web/api_driver.h"
#include "web/api_response.h"
#include "app/piano_controller.h"
#include <AsyncJson.h>

void registerDriverRoutes(AsyncWebServer &server, PianoController &piano)
{
  AsyncCallbackJsonWebHandler *commandHandler = new AsyncCallbackJsonWebHandler(
      "/api/driver/command",
      [&piano](AsyncWebServerRequest *request, JsonVariant &json) {
        String command = json["command"] | "";
        command.trim();

        switch (piano.submitDriverCommand(command))
        {
        case SubmitResult::Invalid:
          sendError(request, 400, "invalid command");
          return;
        case SubmitResult::QueueFull:
          sendError(request, 503, "command queue full");
          return;
        case SubmitResult::Queued:
          break;
        }

        JsonDocument doc;
        doc["queued"] = command;
        sendJson(request, 202, doc);
      });
  commandHandler->setMethod(HTTP_POST);
  server.addHandler(commandHandler);
}
