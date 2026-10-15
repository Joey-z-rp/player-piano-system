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
class VelocityTable;

static const size_t DRIVER_COMMAND_MAX_LENGTH = 48; // e.g. "P:87:33.25:50:22.5:150:60.25:40:37.5" is 36
static const size_t DRIVER_COMMAND_QUEUE_LENGTH = 32;


// The sustain pedal is pushed by two solenoids, driven together. Both use
// P commands (duty:strike ms:follow-up duty:follow-up ms:hold duty):
// press is 100% for 60 ms then hold 55%; release steps down 40% -> 30% -> 0%.
static const uint8_t SUSTAIN_PEDAL_CHANNELS[] = {88, 89};
static const char SUSTAIN_PEDAL_PRESS[] = "100:30:100:30:55";
static const char SUSTAIN_PEDAL_RELEASE[] = "40:40:30:40:0";

// Key presses are driven as the velocity table's DriveSettings say (see
// sendPress). Without landing times, presses use the drivers' defaults (a
// 50 ms strike, then 20% until the release).

// Before each song, and on request, the driver boards are restarted and given
// this long to come back, so a driver stuck in a bad state can't carry it
// into a song.
static const char DRIVER_RESET_COMMAND[] = "RESET:ALL";
static const uint32_t DRIVER_RESET_WAIT_MS = 100;

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

  // Key presses take their duty from velocityTable; it must outlive the controller.
  bool init(Rs485Module *rs485, const VelocityTable *velocityTable);

  // Safe to call from any task.
  SubmitResult submitDriverCommand(const String &command);
  PlaybackResult loadRoll(std::shared_ptr<const PianoRoll> roll);
  PlaybackResult play();
  PlaybackResult stop();
  // Stops any song and restarts every driver board, which lets every key go.
  PlaybackResult resetDrivers();
  // Presses or gradually releases the sustain pedal, as a song does.
  PlaybackResult setPedal(bool down);
  PlaybackStatus status() const;
  size_t pendingCommands() const;
  // For building rolls timed to how this piano's hammers land.
  const VelocityTable *velocities() const;

  static bool isDriverCommand(const String &command);

private:
  enum class MessageType : uint8_t
  {
    DriverCommand,
    Play,
    Stop,
    ResetDrivers,
    PedalDown,
    PedalUp
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
  void restartDrivers();
  void sendPedal(const char *params);
  bool send(const char *command);
  void sendPress(uint8_t key, uint8_t velocity);

  Rs485Module *rs485;
  const VelocityTable *velocityTable;
  QueueHandle_t messageQueue;

  mutable std::mutex rollMutex;
  std::shared_ptr<const PianoRoll> loadedRoll;
  std::atomic<PlaybackState> state;
  std::atomic<uint32_t> playStartMs;

  // Owned by the player task.
  std::shared_ptr<const PianoRoll> playingRoll;
  size_t nextEvent;
  int64_t startUs;
};

#endif // PIANO_CONTROLLER_H
