#ifndef PIANO_CONTROLLER_H
#define PIANO_CONTROLLER_H

#include <Arduino.h>
#include <atomic>
#include <bitset>
#include <memory>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>
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
// Starting part way through with the pedal down, it's pressed first and
// given this long to go down before the first note.
static const uint32_t PEDAL_SETTLE_MS = 100;

// Key presses are driven as the velocity table's DriveSettings say (see
// sendPress). Without landing times, presses use the drivers' defaults (a
// 50 ms strike, then 20% until the release).

// Volume: every note's velocity is scaled by this percent, within these
// limits. A change applies from the next play: a loaded piece is rebuilt for
// it then, so its notes stay timed to how its hammers land.
static const uint16_t MIN_VOLUME = 10;
static const uint16_t MAX_VOLUME = 200;
static const uint16_t DEFAULT_VOLUME = 100;
// Contrast and voicing stretch velocities apart (see Dynamics), in percent,
// within these limits; saved like the volume. Each part's offset (in
// velocities) is for the loaded piece only, and starts at 0 for each new one.
static const uint16_t MIN_CONTRAST = 50;
static const uint16_t MAX_CONTRAST = 250;
static const uint16_t MIN_VOICING = 50;
static const uint16_t MAX_VOICING = 300;
static const int8_t MAX_PART_OFFSET = 20;

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
  NotPlaying,
  QueueFull
};

enum class PlaybackState : uint8_t
{
  Idle,
  Playing,
  Paused // every key up; Play goes on from there
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
  // Plays the loaded piece from fromMs on its timeline (see findRollStart).
  // CONTINUE plays from the start, or where it was paused. Going on from a
  // pause rebuilds the piece if the dynamics have changed.
  static const uint32_t CONTINUE = UINT32_MAX;
  PlaybackResult play(uint32_t fromMs = CONTINUE);
  PlaybackResult pause();
  // Jumps the playing or paused piece to toMs.
  PlaybackResult seek(uint32_t toMs);
  PlaybackResult stop();
  // Stops any song and restarts every driver board, which lets every key go.
  PlaybackResult resetDrivers();
  // Presses or gradually releases the sustain pedal, as a song does.
  PlaybackResult setPedal(bool down);
  PlaybackStatus status() const;
  size_t pendingCommands() const;
  // Saved, so it survives a restart. Returns false if out of range.
  bool setVolume(uint16_t percent);
  uint16_t volume() const;
  // Saved too. Return false if out of range.
  bool setContrast(uint16_t percent);
  bool setVoicing(uint16_t percent);
  // For the loaded piece (see MidiEvent::part). Returns false if out of range.
  bool setPartOffset(uint8_t part, int8_t velocities);
  // Everything a piece is shaped by, as it is now: applies from the next Play.
  Dynamics dynamics() const;
  // For building rolls timed to how this piano's hammers land.
  const VelocityTable *velocities() const;

  static bool isDriverCommand(const String &command);

private:
  enum class MessageType : uint8_t
  {
    DriverCommand,
    Play,
    Pause,
    Seek,
    Stop,
    ResetDrivers,
    PedalDown,
    PedalUp
  };

  struct Message
  {
    MessageType type;
    uint32_t timeMs; // Play and Seek
    char text[DRIVER_COMMAND_MAX_LENGTH + 1];
  };

  static void taskEntry(void *arg);
  bool post(const Message &message);
  void run();
  void handleMessage(const Message &message);
  void startPlayback(uint32_t fromMs);
  void resumePlayback(uint32_t fromMs);
  void playFrom(uint32_t fromMs);
  void rebuildForDynamics();
  void playDueEvents();
  void finishPlayback();
  void sendRollEvent(const RollEvent &event);
  void releaseAll();
  void restartDrivers();
  void sendPedal(const char *params);
  bool send(const char *command);
  void sendPress(uint8_t key, uint8_t velocity, bool pedal);

  Rs485Module *rs485;
  const VelocityTable *velocityTable;
  QueueHandle_t messageQueue;

  mutable std::mutex rollMutex;
  std::shared_ptr<const PianoRoll> loadedRoll;
  std::atomic<PlaybackState> state;
  std::atomic<uint16_t> volumePercent;
  std::atomic<uint16_t> contrastPercent;
  std::atomic<uint16_t> voicingPercent;
  std::atomic<int8_t> partOffsets[MAX_MIDI_PARTS];
  std::atomic<uint32_t> playStartMs;
  std::atomic<uint32_t> pausedAtMs;

  // Owned by the player task.
  std::shared_ptr<const PianoRoll> playingRoll;
  size_t nextEvent;
  int64_t startUs;
  std::bitset<PIANO_KEY_COUNT> skipReleases; // see RollStart
  std::vector<size_t> skippedPresses;
  size_t nextSkipped;
};

#endif // PIANO_CONTROLLER_H
