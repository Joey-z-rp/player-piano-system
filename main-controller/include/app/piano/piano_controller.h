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
#include "app/piano/key_offsets.h"
#include "app/piano/live_player.h"
#include "app/piano/play_config.h"

class Rs485Module;
class VelocityTable;

static const size_t DRIVER_COMMAND_MAX_LENGTH = 48; // e.g. "P:87:33.25:50:22.5:150:60.25:40:37.5" is 36
static const size_t DRIVER_COMMAND_QUEUE_LENGTH = 32;


// The sustain pedal is pushed by two solenoids, driven together, with the
// press and release commands in the PlayConfig (P commands: duty:strike
// ms:follow-up duty:follow-up ms:hold duty). Starting part way through with
// the pedal down, it's pressed first and given PlayConfig::pedalSettleMs to
// go down before the first note.
static const uint8_t SUSTAIN_PEDAL_CHANNELS[] = {88, 89};

// Key presses are driven as the velocity table's DriveSettings say (see
// sendPress). Without landing times, presses use the drivers' defaults (a
// 50 ms strike, then 20% until the release).

// Volume: every note's velocity is scaled by this percent, within these
// limits. A change applies from the next play: a loaded piece is rebuilt for
// it then, so its notes stay timed to how its hammers land. Like everything
// else that shapes a piece (Dynamics), it's the loaded piece's own: each new
// piece starts from the defaults, and a library piece keeps its own.
static const uint16_t MIN_VOLUME = 10;
static const uint16_t MAX_VOLUME = 200;
static const uint16_t DEFAULT_VOLUME = 100;
// Contrast and voicing stretch velocities apart (see Dynamics), in percent,
// within these limits; each part's offset is in velocities.
static const uint16_t MIN_CONTRAST = 50;
static const uint16_t MAX_CONTRAST = 250;
static const uint16_t MIN_VOICING = 50;
static const uint16_t MAX_VOICING = 300;
static const int8_t MAX_PART_OFFSET = 20;
static const uint8_t MAX_REPEAT_BOOST = 40; // velocities a repeat can be played louder
// Speed, in percent of the written tempo, for the loaded piece only: 100 for
// each new one. Unlike the rest it changes where things are on the piece's
// timeline, so when nothing is playing the piece is rebuilt for it at once.
static const uint16_t MIN_SPEED = 50;
static const uint16_t MAX_SPEED = 150;

// Before each song, and on request, the driver boards are restarted and given
// this long to come back, so a driver stuck in a bad state can't carry it
// into a song.
static const char DRIVER_RESET_COMMAND[] = "RESET:ALL";
static const uint32_t DRIVER_RESET_WAIT_MS = 100;
// How often the solenoids' heat is estimated, and how far a hot one must
// cool (of its limit) before Play goes on.
static const uint32_t HEAT_TICK_MS = 250;
static const float HEAT_RESUME_LOAD = 0.8f;

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
  QueueFull,
  TooHot,  // a solenoid is still cooling (see heatStatus)
  Updating // an update over Wi-Fi is under way
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
  bool testing; // playing a test (playTest), not the loaded piece
  bool live;    // playing live (startLive)
  uint32_t positionMs;
  std::shared_ptr<const PianoRoll> roll; // null when nothing is loaded
};

// Owns the driver bus. A single task does every RS485 write: it plays the
// loaded roll on schedule and sends manual commands queued by web handlers.
class PianoController
{
public:
  PianoController();

  // Key presses take their duty from velocityTable; it must outlive the
  // controller, which gives it the saved per-key offsets.
  bool init(Rs485Module *rs485, VelocityTable *velocityTable);

  // Safe to call from any task.
  SubmitResult submitDriverCommand(const String &command);
  PlaybackResult loadRoll(std::shared_ptr<const PianoRoll> roll);
  // Plays the loaded piece from fromMs on its timeline (see findRollStart).
  // CONTINUE plays from the start, or where it was paused. Going on from a
  // pause rebuilds the piece if the dynamics have changed.
  // atSpeed: the speed the piece was built at when fromMs was read from it,
  // so it's moved to the same point if it's been rebuilt since (0: as it is).
  static const uint32_t CONTINUE = UINT32_MAX;
  PlaybackResult play(uint32_t fromMs = CONTINUE, uint16_t atSpeed = 0);
  PlaybackResult pause();
  // Jumps the playing or paused piece to toMs (atSpeed as for play).
  PlaybackResult seek(uint32_t toMs, uint16_t atSpeed = 0);
  PlaybackResult stop();
  // Plays a few notes built like a piece (the test page's strikes and
  // sequences), without touching the loaded piece. A test replaces one
  // still playing; Busy while the piece is playing or paused. Pause and seek
  // don't apply to it; stop does.
  PlaybackResult playTest(std::shared_ptr<const PianoRoll> roll);
  // An update over Wi-Fi: stops anything playing (every key and the pedal let
  // go) and holds off Play and tests until it ends (endUpdate, if it fails;
  // else the controller restarts).
  void beginUpdate();
  void endUpdate();
  // Stops any song and restarts every driver board, which lets every key go.
  PlaybackResult resetDrivers();
  // Presses or gradually releases the sustain pedal, as a song does.
  PlaybackResult setPedal(bool down);
  PlaybackStatus status() const;

  // Live play (live_player.h): MIDI from the Live page, played a fixed
  // delay after it was played. A session starts with every key and the
  // pedal let go, and ends (everything let go again) on stopLive, Stop, an
  // update, the heat limit, or LIVE_TIMEOUT_MS without a word from the
  // sender. Busy while a piece or test is playing or paused; Play and tests
  // are Busy while it's on. Safe to call from any task.
  enum class LiveEnd : uint8_t
  {
    None,
    Stopped,
    TimedOut,
    TooHot
  };
  struct LiveStatus
  {
    bool active = false;
    uint16_t delayMs = 0;
    uint32_t late = 0; // notes too late for the delay, played at once
    LiveEnd ended = LiveEnd::None; // why the last session ended
  };
  PlaybackResult startLive(uint16_t delayMs);
  // A MIDI message the sender got at senderMs (its clock).
  void liveMessage(uint32_t senderMs, uint8_t status, uint8_t data1, uint8_t data2);
  // The sender's clock now (its pings), which also keeps the session alive.
  void liveClock(uint32_t senderMs);
  void setLiveDelay(uint16_t delayMs);
  void stopLive();
  LiveStatus liveStatus() const;

  // The solenoids' estimated heat (HeatModel, from the play config's coil
  // settings): the one nearest its limit, and what was last done about one
  // reaching it. A piece pauses at the limit (a test stops; a key or pedal
  // held by hand is let go), and Play waits until it has cooled to
  // HEAT_RESUME_LOAD of the limit.
  enum class HeatAction : uint8_t
  {
    None,
    Paused,      // the piece
    TestStopped,
    Released,    // the channel, held by a manual command
    LiveStopped  // live play
  };
  struct HeatStatus
  {
    bool watched = false; // any coil measured
    uint8_t channel = 0;  // the hottest watched one
    float riseC = 0;
    float limitC = 0;
    float load = 0;       // riseC / limitC
    float coolS = 0;      // until it's down to HEAT_RESUME_LOAD
    HeatAction action = HeatAction::None;
    uint8_t actionChannel = 0;
  };
  HeatStatus heatStatus() const;
  // Every solenoid's estimated rise (°C; 0-87 the keys, 88-89 the pedal),
  // whether each is watched (its coil measured), and the limit.
  void heatRises(float rises[HEAT_CHANNELS], bool watched[HEAT_CHANNELS], float &limitC) const;
  // True while the loaded piece is being rebuilt (for new settings or speed).
  bool building() const;
  size_t pendingCommands() const;
  // The loaded piece's (see MIN_VOLUME). Return false if out of range.
  bool setVolume(uint16_t percent);
  uint16_t volume() const;
  bool setContrast(uint16_t percent);
  bool setVoicing(uint16_t percent);
  bool setRepeatBoost(uint8_t velocities);
  // For the loaded piece (see MidiEvent::part). Returns false if out of range.
  bool setPartOffset(uint8_t part, int8_t velocities);
  // Leaves a part out (or back in), for the loaded piece.
  bool setPartMuted(uint8_t part, bool muted);
  // For the loaded piece too. Applies at once when nothing is playing (a
  // paused position moves with it), else from the next pause or Play.
  bool setSpeed(uint16_t percent);
  // Everything a piece is shaped by, as it is now: applies from the next Play.
  Dynamics dynamics() const;
  // The loaded piece's note edits (note_edits.h; null: none). Like the
  // dynamics, new ones apply from the next Play; loading a piece takes the
  // ones it was built with.
  std::shared_ptr<const NoteEdits> noteEdits() const;
  void setNoteEdits(std::shared_ptr<const NoteEdits> edits);
  // The timing rules pieces are played with (the config page), saved on the
  // controller; they apply from the next Play. setPlayConfig reads JSON (see
  // readPlayConfig) over the current settings.
  PlayConfig playConfig() const;
  uint32_t playConfigVersion() const; // changes whenever the config does
  bool setPlayConfig(const char *json, size_t length, std::string &error);
  void resetPlayConfig();
  // Per-key velocity offsets (key_offsets.h), saved on the controller. They
  // apply at once to single presses and tests, and to pieces from the next
  // Play (playConfigVersion changes with them, so pieces are rebuilt).
  std::string keyOffsetsJson() const;
  // A key's curves: up, and its own pedal-down one (null: none, it uses up).
  void setKeyOffsets(uint8_t key, const KeyOffsets::Entry &entry);
  void clearKeyOffsets();
  void setAllKeyOffsets(const KeyOffsets &offsets); // e.g. from a backup

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
    PedalUp,
    Rebuild, // for a new speed, when nothing is playing
    PlayTest,
    LiveStart,
    LiveStop,
    LiveWake // something new to play live
  };

  struct Message
  {
    MessageType type;
    uint32_t timeMs;  // Play and Seek
    uint16_t atSpeed; // Play and Seek: see play()
    char text[DRIVER_COMMAND_MAX_LENGTH + 1];
  };

  static void taskEntry(void *arg);
  bool post(const Message &message);
  void run();
  void handleMessage(const Message &message);
  // Live play (piano_live.cpp), on the player task.
  void playDueLive(TickType_t &wait);
  void sendLiveAction(const LiveAction &action);
  void endLive(LiveEnd why);
  void wakeForLive();
  void startPlayback(uint32_t fromMs, uint16_t atSpeed);
  void resumePlayback(uint32_t fromMs, uint16_t atSpeed);
  uint32_t toLoadedSpeed(uint32_t ms, uint16_t atSpeed) const;
  void playFrom(uint32_t fromMs);
  // positionMs (if given) is moved to the same point in the rebuilt piece.
  void rebuildForDynamics(uint32_t *positionMs = nullptr);
  void rebuildIfSpeedChanged();
  void playDueEvents();
  void finishPlayback();
  void sendRollEvent(const RollEvent &event);
  void releaseAll();
  void restartDrivers();
  void sendPedal(bool down);
  void loadSettings(); // at init (piano_settings.cpp)
  void watchHeat();    // every HEAT_TICK_MS (piano_heat.cpp)
  bool tooHotToPlay() const;
  void clearHeatAction();
  void pausePlayback();
  void saveConfig(const PlayConfig &config);
  void useKeyOffsets(std::shared_ptr<const KeyOffsets> offsets); // and save them
  bool send(const char *command);
  void sendPress(const RollEvent &event); // as the roll worked it out

  Rs485Module *rs485;
  VelocityTable *velocityTable;
  std::mutex offsetsMutex; // one change to the offsets at a time
  QueueHandle_t messageQueue;

  mutable std::mutex rollMutex;
  std::shared_ptr<const PianoRoll> loadedRoll;
  std::shared_ptr<const PianoRoll> testRoll; // waiting to be played (playTest)
  std::shared_ptr<const NoteEdits> currentEdits; // with rollMutex
  mutable std::mutex liveMutex;
  LivePlayer livePlayer;   // with liveMutex
  PlayConfig liveConfig;   // the session's, with liveMutex
  std::atomic<bool> live{false};
  std::atomic<uint32_t> liveHeardMs{0};
  std::atomic<LiveEnd> liveEnded{LiveEnd::None};
  std::vector<LiveAction> liveDue; // owned by the player task
  std::atomic<bool> testing;
  std::atomic<bool> updating;
  std::atomic<PlaybackState> state;
  std::atomic<uint16_t> volumePercent;
  std::atomic<uint16_t> contrastPercent;
  std::atomic<uint16_t> voicingPercent;
  std::atomic<uint8_t> repeatBoost;
  std::atomic<int8_t> partOffsets[MAX_MIDI_PARTS];
  std::atomic<uint16_t> speedPercent;
  std::atomic<uint16_t> mutedParts;
  std::atomic<bool> rebuilding;
  mutable std::mutex configMutex;
  PlayConfig config;
  std::atomic<uint32_t> configVersion;
  std::atomic<uint32_t> playStartMs;
  std::atomic<uint32_t> pausedAtMs;

  // Owned by the player task.
  std::shared_ptr<const PianoRoll> playingRoll;
  size_t nextEvent;
  int64_t startUs;
  std::bitset<PIANO_KEY_COUNT> skipReleases; // see RollStart
  std::vector<size_t> skippedPresses;
  size_t nextSkipped;
  PlayConfig playingConfig; // the config the playing roll was built with
  mutable std::mutex heatMutex;
  HeatModel heat;
  HeatAction heatAction = HeatAction::None;
  uint8_t heatActionChannel = 0;
  uint32_t heatCheckedMs = 0;
};

#endif // PIANO_CONTROLLER_H
