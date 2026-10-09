#ifndef PIANO_CONTROLLER_H
#define PIANO_CONTROLLER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Rs485Module;

static const size_t DRIVER_COMMAND_MAX_LENGTH = 32;
static const size_t DRIVER_COMMAND_QUEUE_LENGTH = 32;

enum class SubmitResult
{
  Queued,
  Invalid,
  QueueFull
};

// Owns the driver bus. Web handlers run on the async_tcp task and only
// enqueue commands; process() runs on the main loop and does the sending.
class PianoController
{
public:
  PianoController();

  bool init(Rs485Module *rs485);

  // Safe to call from any task.
  SubmitResult submitDriverCommand(const String &command);
  size_t pendingCommands() const;

  // Call from loop().
  void process();

  static bool isDriverCommand(const String &command);

private:
  struct DriverCommand
  {
    char text[DRIVER_COMMAND_MAX_LENGTH + 1];
  };

  Rs485Module *rs485;
  QueueHandle_t commandQueue;
};

#endif // PIANO_CONTROLLER_H
