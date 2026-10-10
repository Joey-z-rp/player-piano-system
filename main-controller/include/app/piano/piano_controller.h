#ifndef PIANO_CONTROLLER_H
#define PIANO_CONTROLLER_H

#include <Arduino.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "app/piano/piano_roll.h"

class Rs485Module;

static const size_t DRIVER_COMMAND_MAX_LENGTH = 32;
static const size_t DRIVER_COMMAND_QUEUE_LENGTH = 32;

// Velocity 1-127 maps linearly onto this duty range.
static const uint8_t KEY_MIN_DUTY = 40;
static const uint8_t KEY_MAX_DUTY = 80;

// The sustain pedal is pushed by two solenoids, driven together. Both use
// P commands (duty:strike ms:follow-up duty:follow-up ms:hold duty):
// press is 100% for 60 ms then hold 55%; release steps down 40% -> 30% -> 0%.
static const uint8_t SUSTAIN_PEDAL_CHANNELS[] = {88, 89};
static const char SUSTAIN_PEDAL_PRESS[] = "100:30:100:30:55";
static const char SUSTAIN_PEDAL_RELEASE[] = "40:40:30:40:0";

enum class SubmitResult
{
  Queued,
  Invalid,
  QueueFull
};

enum class PlaybackResult
{
  Ok,
  Busy,
  NoRoll,
  QueueFull
};

enum class PlaybackState : uint8_t
{
  Idle,
  Playing
};

struct PlaybackStatus
{
  PlaybackState state;
  uint32_t positionMs;
  std::shared_ptr<const PianoRoll> roll; // null when nothing is loaded
};

// Owns the driver bus. A single task does every RS485 write: it plays the
// loaded roll on schedule and sends manual commands queued by web handlers.
class PianoController
{
public:
  PianoController();

  bool init(Rs485Module *rs485);

  // Safe to call from any task.
  SubmitResult submitDriverCommand(const String &command);
  PlaybackResult loadRoll(std::shared_ptr<const PianoRoll> roll);
  PlaybackResult play();
  PlaybackResult stop();
  PlaybackStatus status() const;
  size_t pendingCommands() const;

  static bool isDriverCommand(const String &command);

private:
  enum class MessageType : uint8_t
  {
    DriverCommand,
    Play,
    Stop
  };

  struct Message
  {
    MessageType type;
    char text[DRIVER_COMMAND_MAX_LENGTH + 1];
  };

  static void taskEntry(void *arg);
  bool post(const Message &message);
  void run();
  void handleMessage(const Message &message);
  void startPlayback();
  void playDueEvents();
  void finishPlayback();
  void sendRollEvent(const RollEvent &event);
  void releaseAll();
  void sendPedal(const char *params);
  bool send(const char *command);

  Rs485Module *rs485;
  QueueHandle_t messageQueue;

  mutable std::mutex rollMutex;
  std::shared_ptr<const PianoRoll> loadedRoll;
  std::atomic<PlaybackState> state;
  std::atomic<uint32_t> playStartMs;

  // Owned by the player task.
  std::shared_ptr<const PianoRoll> playingRoll;
  size_t nextEvent;
  int64_t startUs;
  bool sustainHeld;
};

#endif // PIANO_CONTROLLER_H
