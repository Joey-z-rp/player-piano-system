#include "app/piano/piano_controller.h"
#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include "rs485.h"
#include <esp_timer.h>
#include <freertos/task.h>

static const uint32_t PLAYER_TASK_STACK = 8192; // rebuilds a piece for new dynamics
static const UBaseType_t PLAYER_TASK_PRIORITY = 3; // above loop()
static const BaseType_t PLAYER_TASK_CORE = 1;      // Wi-Fi runs on core 0

PianoController::PianoController()
    : rs485(nullptr), velocityTable(nullptr), messageQueue(nullptr), state(PlaybackState::Idle),
      volumePercent(DEFAULT_VOLUME), contrastPercent(100), voicingPercent(100), repeatBoost(0), partOffsets(), speedPercent(100), mutedParts(0), rebuilding(false), testing(false), configVersion(1), playStartMs(0),
      pausedAtMs(0),
      nextEvent(0), startUs(0), nextSkipped(0)
{
}

bool PianoController::init(Rs485Module *rs485, VelocityTable *velocityTable)
{
  this->rs485 = rs485;
  this->velocityTable = velocityTable;
  loadSettings();
  messageQueue = xQueueCreate(DRIVER_COMMAND_QUEUE_LENGTH, sizeof(Message));
  if (messageQueue == nullptr)
  {
    return false;
  }
  return xTaskCreatePinnedToCore(taskEntry, "player", PLAYER_TASK_STACK, this, PLAYER_TASK_PRIORITY, nullptr,
                                 PLAYER_TASK_CORE) == pdPASS;
}

SubmitResult PianoController::submitDriverCommand(const String &command)
{
  if (!isDriverCommand(command))
  {
    return SubmitResult::Invalid;
  }

  Message message;
  message.type = MessageType::DriverCommand;
  strlcpy(message.text, command.c_str(), sizeof(message.text));
  return post(message) ? SubmitResult::Queued : SubmitResult::QueueFull;
}

PlaybackResult PianoController::loadRoll(std::shared_ptr<const PianoRoll> roll)
{
  std::lock_guard<std::mutex> lock(rollMutex);
  if (state != PlaybackState::Idle)
  {
    return PlaybackResult::Busy;
  }
  loadedRoll = roll;
  for (std::atomic<int8_t> &offset : partOffsets)
  {
    offset = 0; // a new piece starts with its parts as written
  }
  // A new piece starts from the defaults (piece_loader.cpp then gives a
  // library piece its own settings).
  volumePercent = DEFAULT_VOLUME;
  contrastPercent = 100;
  voicingPercent = 100;
  repeatBoost = 0;
  speedPercent = 100;
  mutedParts = 0;
  return PlaybackResult::Ok;
}

PlaybackResult PianoController::play(uint32_t fromMs, uint16_t atSpeed)
{
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    if (loadedRoll == nullptr)
    {
      return PlaybackResult::NoRoll;
    }
  }
  if (state == PlaybackState::Playing)
  {
    return PlaybackResult::Busy;
  }
  if (tooHotToPlay())
  {
    return PlaybackResult::TooHot;
  }

  Message message;
  message.type = MessageType::Play;
  message.timeMs = fromMs;
  message.atSpeed = atSpeed;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::playTest(std::shared_ptr<const PianoRoll> roll)
{
  if (state != PlaybackState::Idle && !testing)
  {
    return PlaybackResult::Busy;
  }
  if (tooHotToPlay())
  {
    return PlaybackResult::TooHot;
  }
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    testRoll = roll;
  }
  Message message;
  message.type = MessageType::PlayTest;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::pause()
{
  if (state != PlaybackState::Playing || testing)
  {
    return PlaybackResult::NotPlaying;
  }
  Message message;
  message.type = MessageType::Pause;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::seek(uint32_t toMs, uint16_t atSpeed)
{
  if (state == PlaybackState::Idle || testing)
  {
    return PlaybackResult::NotPlaying;
  }
  Message message;
  message.type = MessageType::Seek;
  message.timeMs = toMs;
  message.atSpeed = atSpeed;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::stop()
{
  Message message;
  message.type = MessageType::Stop;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::resetDrivers()
{
  Message message;
  message.type = MessageType::ResetDrivers;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::setPedal(bool down)
{
  Message message;
  message.type = down ? MessageType::PedalDown : MessageType::PedalUp;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackStatus PianoController::status() const
{
  PlaybackStatus result;
  result.state = state;
  result.testing = testing;
  // Before playStartMs while the pedal settles at a start part way through.
  int32_t positionMs = (int32_t)(millis() - playStartMs);
  switch (result.state)
  {
  case PlaybackState::Playing:
    result.positionMs = positionMs > 0 ? positionMs : 0;
    break;
  case PlaybackState::Paused:
    result.positionMs = pausedAtMs;
    break;
  default:
    result.positionMs = 0;
  }
  std::lock_guard<std::mutex> lock(rollMutex);
  result.roll = loadedRoll;
  return result;
}

bool PianoController::building() const
{
  return rebuilding;
}

const VelocityTable *PianoController::velocities() const
{
  return velocityTable;
}

size_t PianoController::pendingCommands() const
{
  return messageQueue == nullptr ? 0 : uxQueueMessagesWaiting(messageQueue);
}

bool PianoController::post(const Message &message)
{
  return messageQueue != nullptr && xQueueSend(messageQueue, &message, 0) == pdTRUE;
}

void PianoController::taskEntry(void *arg)
{
  static_cast<PianoController *>(arg)->run();
}

// Sleeps on the message queue until either a message arrives or the next
// roll event is due, so manual commands and playback share one bus owner.
void PianoController::run()
{
  for (;;)
  {
    TickType_t wait = portMAX_DELAY;
    if (state == PlaybackState::Playing)
    {
      playDueEvents();
    }
    if (state == PlaybackState::Playing)
    {
      int64_t dueUs = startUs + (int64_t)playingRoll->events[nextEvent].timeMs * 1000;
      int64_t remainingUs = dueUs - esp_timer_get_time();
      wait = remainingUs <= 0 ? 0 : pdMS_TO_TICKS((remainingUs + 999) / 1000);
    }

    if (wait > pdMS_TO_TICKS(HEAT_TICK_MS))
    {
      wait = pdMS_TO_TICKS(HEAT_TICK_MS); // the heat is watched even when idle
    }

    Message message;
    if (xQueueReceive(messageQueue, &message, wait) == pdTRUE)
    {
      handleMessage(message);
    }
    if (millis() - heatCheckedMs >= HEAT_TICK_MS)
    {
      watchHeat();
    }
  }
}

void PianoController::handleMessage(const Message &message)
{
  switch (message.type)
  {
  case MessageType::DriverCommand:
    if (send(message.text))
    {
      Serial.printf("RS485 sent: %s\n", message.text);
    }
    break;
  case MessageType::Play:
    if (state == PlaybackState::Idle)
    {
      startPlayback(message.timeMs == CONTINUE ? 0 : message.timeMs, message.atSpeed);
    }
    else if (state == PlaybackState::Paused)
    {
      bool fromPause = message.timeMs == CONTINUE;
      resumePlayback(fromPause ? (uint32_t)pausedAtMs : message.timeMs, fromPause ? 0 : message.atSpeed);
    }
    break;
  case MessageType::Pause:
    if (state == PlaybackState::Playing)
    {
      pausePlayback();
    }
    break;
  case MessageType::PlayTest:
  {
    std::shared_ptr<const PianoRoll> roll;
    {
      std::lock_guard<std::mutex> lock(rollMutex);
      roll.swap(testRoll);
    }
    if (roll == nullptr || roll->events.empty() || (state != PlaybackState::Idle && !testing))
    {
      break;
    }
    if (testing)
    {
      finishPlayback(); // the one before: every key up first
    }
    playingConfig = playConfig();
    playingRoll = roll;
    testing = true;
    state = PlaybackState::Playing;
    playFrom(0);
    break;
  }
  case MessageType::Rebuild:
    if (state != PlaybackState::Playing)
    {
      rebuildIfSpeedChanged();
    }
    break;
  case MessageType::Seek:
    if (state == PlaybackState::Playing)
    {
      releaseAll();
      playFrom(toLoadedSpeed(message.timeMs, message.atSpeed));
    }
    else if (state == PlaybackState::Paused)
    {
      pausedAtMs = toLoadedSpeed(message.timeMs, message.atSpeed);
    }
    break;
  case MessageType::Stop:
    if (state != PlaybackState::Idle)
    {
      finishPlayback();
    }
    else
    {
      releaseAll(); // also clears keys left on by manual commands
    }
    break;
  case MessageType::ResetDrivers:
    if (state != PlaybackState::Idle)
    {
      finishPlayback();
    }
    restartDrivers();
    break;
  case MessageType::PedalDown:
    sendPedal(true);
    break;
  case MessageType::PedalUp:
    sendPedal(false);
    break;
  }
}

void PianoController::pausePlayback()
{
  int64_t elapsedUs = esp_timer_get_time() - startUs;
  pausedAtMs = elapsedUs > 0 ? (uint32_t)(elapsedUs / 1000) : 0;
  releaseAll();
  state = PlaybackState::Paused;
  Serial.printf("Paused at %u ms\n", (uint32_t)pausedAtMs);
  rebuildIfSpeedChanged();
}

// The drivers come back with every output off.
void PianoController::restartDrivers()
{
  send(DRIVER_RESET_COMMAND);
  vTaskDelay(pdMS_TO_TICKS(DRIVER_RESET_WAIT_MS));
  Serial.println("Drivers restarted");
}

// Builds the loaded piece again if the dynamics or the play config have
// changed since it was built, from the MIDI it came from.
// While nothing is playing, a new speed is built in at once, so the piece's
// length, the paused position and the note view all agree with it.
void PianoController::rebuildIfSpeedChanged()
{
  std::shared_ptr<const PianoRoll> loaded;
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    loaded = loadedRoll;
  }
  if (loaded != nullptr && loaded->dynamics.speed != speedPercent)
  {
    uint32_t position = pausedAtMs;
    rebuildForDynamics(&position);
    if (state == PlaybackState::Paused)
    {
      pausedAtMs = position;
    }
  }
}

void PianoController::rebuildForDynamics(uint32_t *positionMs)
{
  std::shared_ptr<const PianoRoll> loaded;
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    loaded = loadedRoll;
  }
  Dynamics now = dynamics();
  uint32_t version = configVersion;
  if (loaded == nullptr || loaded->source == nullptr ||
      (loaded->dynamics == now && loaded->configVersion == version))
  {
    return;
  }
  PlayConfig rules = playConfig();
  std::shared_ptr<PianoRoll> rebuilt = std::make_shared<PianoRoll>();
  rebuilt->configVersion = version;
  rebuilding = true;
  bool built = buildPianoRoll(*loaded->source, *rebuilt, velocityTable, now, nullptr, &rules);
  rebuilding = false;
  if (!built)
  {
    Serial.println("Out of memory rebuilding for the new dynamics: playing as before");
    return;
  }
  rebuilt->title = loaded->title;
  rebuilt->id = loaded->id;
  rebuilt->libraryId = loaded->libraryId;
  rebuilt->source = loaded->source;
  rebuilt->partNames = loaded->partNames;
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    if (loadedRoll != loaded) // replaced by an upload meanwhile
    {
      return;
    }
    loadedRoll = rebuilt;
  }
  if (positionMs != nullptr)
  {
    *positionMs = toLoadedSpeed(*positionMs, loaded->dynamics.speed);
  }
  Serial.printf("Rebuilt \"%s\" for volume %u%%, contrast %u%%, voicing %u%%, repeats +%u, speed %u%%\n",
                rebuilt->title.c_str(), now.volume, now.contrast, now.voicing, now.repeatBoost, now.speed);
}

// A position read from the piece when it was built at atSpeed, moved to the
// same point in the loaded piece (0: already in it).
uint32_t PianoController::toLoadedSpeed(uint32_t ms, uint16_t atSpeed) const
{
  std::shared_ptr<const PianoRoll> loaded;
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    loaded = loadedRoll;
  }
  if (loaded == nullptr || atSpeed == 0 || atSpeed == loaded->dynamics.speed || ms <= loaded->leadMs)
  {
    return ms;
  }
  uint64_t intoPiece = ms - loaded->leadMs;
  return loaded->leadMs + (uint32_t)((intoPiece * atSpeed + loaded->dynamics.speed / 2) / loaded->dynamics.speed);
}

void PianoController::startPlayback(uint32_t fromMs, uint16_t atSpeed)
{
  if (atSpeed == 0)
  {
    rebuildForDynamics(&fromMs);
  }
  else
  {
    rebuildForDynamics();
    fromMs = toLoadedSpeed(fromMs, atSpeed);
  }
  playingConfig = playConfig();
  clearHeatAction();
  restartDrivers();

  // Take the roll and mark it playing under one lock, so loadRoll() can't
  // swap in a new roll between the two.
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    if (loadedRoll == nullptr || loadedRoll->events.empty())
    {
      return;
    }
    playingRoll = loadedRoll;
    state = PlaybackState::Playing;
  }
  Serial.printf("Playing \"%s\" (%u events) from %u ms\n", playingRoll->title.c_str(), playingRoll->events.size(),
                fromMs);
  playFrom(fromMs);
}

// Goes on from a pause, with the piece rebuilt if the dynamics have changed.
// Every key is already up.
void PianoController::resumePlayback(uint32_t fromMs, uint16_t atSpeed)
{
  if (atSpeed == 0)
  {
    rebuildForDynamics(&fromMs);
  }
  else
  {
    rebuildForDynamics();
    fromMs = toLoadedSpeed(fromMs, atSpeed);
  }
  playingConfig = playConfig();
  clearHeatAction();
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    if (loadedRoll == nullptr)
    {
      return;
    }
    playingRoll = loadedRoll;
    state = PlaybackState::Playing;
  }
  Serial.printf("Resuming from %u ms\n", fromMs);
  playFrom(fromMs);
}

// Starts the clock so the playing roll goes on from fromMs. Keys must all be
// up (after a driver restart or releaseAll).
void PianoController::playFrom(uint32_t fromMs)
{
  RollStart start = findRollStart(*playingRoll, fromMs, velocityTable);
  uint32_t settleMs = 0;
  if (start.pedalDown)
  {
    sendPedal(true);
    settleMs = playingConfig.pedalSettleMs;
  }
  nextEvent = start.index;
  skipReleases = start.skipReleases;
  skippedPresses = std::move(start.skippedPresses);
  nextSkipped = 0;
  startUs = esp_timer_get_time() + ((int64_t)settleMs - start.timeMs) * 1000;
  playStartMs = millis() + settleMs - start.timeMs;
}

void PianoController::playDueEvents()
{
  const std::vector<RollEvent> &events = playingRoll->events;
  int64_t elapsedUs = esp_timer_get_time() - startUs;
  while (nextEvent < events.size() && (int64_t)events[nextEvent].timeMs * 1000 <= elapsedUs)
  {
    if (nextSkipped < skippedPresses.size() && skippedPresses[nextSkipped] == nextEvent)
    {
      nextSkipped++; // sounded before where playing started
    }
    else
    {
      sendRollEvent(events[nextEvent]);
    }
    nextEvent++;
  }
  if (nextEvent >= events.size())
  {
    finishPlayback();
  }
}

// Always leaves every solenoid off, whether the roll ended or was stopped.
void PianoController::finishPlayback()
{
  releaseAll();
  bool completed = nextEvent >= playingRoll->events.size();
  Serial.printf("Playback %s\n", completed ? "finished" : "stopped");
  playingRoll.reset();
  testing = false;
  state = PlaybackState::Idle;
}

void PianoController::sendRollEvent(const RollEvent &event)
{
  char command[DRIVER_COMMAND_MAX_LENGTH + 1];
  switch (event.type)
  {
  case RollEventType::KeyPress:
    sendPress(event.key, event.velocity, event.pedal, event.repeat);
    break;
  case RollEventType::KeyRelease:
    if (skipReleases.test(event.key))
    {
      skipReleases.reset(event.key); // never pressed: playing started while it was held
      break;
    }
    snprintf(command, sizeof(command), "R:%u:0", event.key);
    send(command);
    break;
  case RollEventType::SustainOn:
    sendPedal(true);
    break;
  case RollEventType::SustainOff:
    sendPedal(false);
    break;
  }
}

void PianoController::releaseAll()
{
  char command[DRIVER_COMMAND_MAX_LENGTH + 1];
  for (uint8_t key = 0; key < PIANO_KEY_COUNT; key++)
  {
    snprintf(command, sizeof(command), "R:%u:0", key);
    send(command);
  }
  sendPedal(false);
}

void PianoController::sendPedal(bool down)
{
  PlayConfig rules = playConfig();
  const char *params = down ? rules.pedalPress : rules.pedalRelease;
  char command[DRIVER_COMMAND_MAX_LENGTH + 1];
  for (uint8_t channel : SUSTAIN_PEDAL_CHANNELS)
  {
    snprintf(command, sizeof(command), "P:%u:%s", channel, params);
    send(command);
  }
}

bool PianoController::send(const char *command)
{
  if (rs485 == nullptr || !rs485->send(command))
  {
    Serial.printf("RS485 send failed: %s\n", command);
    return false;
  }
  std::lock_guard<std::mutex> lock(heatMutex);
  heat.command(command, millis()); // what the solenoid's now doing, for its heat
  return true;
}

void PianoController::sendPress(uint8_t key, uint8_t velocity, bool pedal, bool repeat)
{
  char command[DRIVER_COMMAND_MAX_LENGTH + 1];
  velocityTable->pressCommand(key, velocity, command, sizeof(command), pedal,
                              repeat ? &playingConfig.repeatClamp : nullptr);
  send(command);
}

bool PianoController::isDriverCommand(const String &command)
{
  if (command.length() < 3 || command.length() > DRIVER_COMMAND_MAX_LENGTH)
  {
    return false;
  }
  if (!command.startsWith("P:") && !command.startsWith("R:"))
  {
    return false;
  }
  for (unsigned int i = 2; i < command.length(); i++)
  {
    char c = command.charAt(i);
    if (!isdigit(c) && c != ':' && c != '.')
    {
      return false;
    }
  }
  return true;
}
