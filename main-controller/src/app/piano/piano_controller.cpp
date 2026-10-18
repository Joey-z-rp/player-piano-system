#include "app/piano/piano_controller.h"
#include "app/piano/roll_builder.h"
#include "app/piano/velocity_table.h"
#include <Preferences.h>
#include "rs485.h"
#include <esp_timer.h>
#include <freertos/task.h>

static const uint32_t PLAYER_TASK_STACK = 8192; // rebuilds a piece for new dynamics
static const char SETTINGS_NAMESPACE[] = "piano";
static const char VOLUME_KEY[] = "volume";
static const char CONTRAST_KEY[] = "contrast";
static const char VOICING_KEY[] = "voicing";
static const UBaseType_t PLAYER_TASK_PRIORITY = 3; // above loop()
static const BaseType_t PLAYER_TASK_CORE = 1;      // Wi-Fi runs on core 0

PianoController::PianoController()
    : rs485(nullptr), velocityTable(nullptr), messageQueue(nullptr), state(PlaybackState::Idle),
      volumePercent(DEFAULT_VOLUME), contrastPercent(100), voicingPercent(100), partOffsets(), playStartMs(0),
      pausedAtMs(0),
      nextEvent(0), startUs(0), nextSkipped(0)
{
}

bool PianoController::init(Rs485Module *rs485, const VelocityTable *velocityTable)
{
  this->rs485 = rs485;
  this->velocityTable = velocityTable;
  Preferences settings;
  if (settings.begin(SETTINGS_NAMESPACE, true))
  {
    uint16_t saved = settings.getUShort(VOLUME_KEY, DEFAULT_VOLUME);
    volumePercent = (saved >= MIN_VOLUME && saved <= MAX_VOLUME) ? saved : DEFAULT_VOLUME;
    saved = settings.getUShort(CONTRAST_KEY, 100);
    contrastPercent = (saved >= MIN_CONTRAST && saved <= MAX_CONTRAST) ? saved : 100;
    saved = settings.getUShort(VOICING_KEY, 100);
    voicingPercent = (saved >= MIN_VOICING && saved <= MAX_VOICING) ? saved : 100;
    settings.end();
  }
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
  return PlaybackResult::Ok;
}

PlaybackResult PianoController::play(uint32_t fromMs)
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

  Message message;
  message.type = MessageType::Play;
  message.timeMs = fromMs;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::pause()
{
  if (state != PlaybackState::Playing)
  {
    return PlaybackResult::NotPlaying;
  }
  Message message;
  message.type = MessageType::Pause;
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

PlaybackResult PianoController::seek(uint32_t toMs)
{
  if (state == PlaybackState::Idle)
  {
    return PlaybackResult::NotPlaying;
  }
  Message message;
  message.type = MessageType::Seek;
  message.timeMs = toMs;
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

const VelocityTable *PianoController::velocities() const
{
  return velocityTable;
}

bool PianoController::setVolume(uint16_t percent)
{
  if (percent < MIN_VOLUME || percent > MAX_VOLUME)
  {
    return false;
  }
  volumePercent = percent;
  Preferences settings;
  if (settings.begin(SETTINGS_NAMESPACE, false))
  {
    settings.putUShort(VOLUME_KEY, percent);
    settings.end();
  }
  return true;
}

uint16_t PianoController::volume() const
{
  return volumePercent;
}

static void saveSetting(const char *key, uint16_t value)
{
  Preferences settings;
  if (settings.begin(SETTINGS_NAMESPACE, false))
  {
    settings.putUShort(key, value);
    settings.end();
  }
}

bool PianoController::setContrast(uint16_t percent)
{
  if (percent < MIN_CONTRAST || percent > MAX_CONTRAST)
  {
    return false;
  }
  contrastPercent = percent;
  saveSetting(CONTRAST_KEY, percent);
  return true;
}

bool PianoController::setVoicing(uint16_t percent)
{
  if (percent < MIN_VOICING || percent > MAX_VOICING)
  {
    return false;
  }
  voicingPercent = percent;
  saveSetting(VOICING_KEY, percent);
  return true;
}

bool PianoController::setPartOffset(uint8_t part, int8_t velocities)
{
  if (part >= MAX_MIDI_PARTS || velocities < -MAX_PART_OFFSET || velocities > MAX_PART_OFFSET)
  {
    return false;
  }
  partOffsets[part] = velocities;
  return true;
}

Dynamics PianoController::dynamics() const
{
  Dynamics now;
  now.volume = volumePercent;
  now.contrast = contrastPercent;
  now.voicing = voicingPercent;
  for (uint8_t part = 0; part < MAX_MIDI_PARTS; part++)
  {
    now.partOffset[part] = partOffsets[part];
  }
  return now;
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

    Message message;
    if (xQueueReceive(messageQueue, &message, wait) == pdTRUE)
    {
      handleMessage(message);
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
      startPlayback(message.timeMs == CONTINUE ? 0 : message.timeMs);
    }
    else if (state == PlaybackState::Paused)
    {
      resumePlayback(message.timeMs == CONTINUE ? (uint32_t)pausedAtMs : message.timeMs);
    }
    break;
  case MessageType::Pause:
    if (state == PlaybackState::Playing)
    {
      int64_t elapsedUs = esp_timer_get_time() - startUs;
      pausedAtMs = elapsedUs > 0 ? (uint32_t)(elapsedUs / 1000) : 0;
      releaseAll();
      state = PlaybackState::Paused;
      Serial.printf("Paused at %u ms\n", (uint32_t)pausedAtMs);
    }
    break;
  case MessageType::Seek:
    if (state == PlaybackState::Playing)
    {
      releaseAll();
      playFrom(message.timeMs);
    }
    else if (state == PlaybackState::Paused)
    {
      pausedAtMs = message.timeMs;
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
    sendPedal(SUSTAIN_PEDAL_PRESS);
    break;
  case MessageType::PedalUp:
    sendPedal(SUSTAIN_PEDAL_RELEASE);
    break;
  }
}

// The drivers come back with every output off.
void PianoController::restartDrivers()
{
  send(DRIVER_RESET_COMMAND);
  vTaskDelay(pdMS_TO_TICKS(DRIVER_RESET_WAIT_MS));
  Serial.println("Drivers restarted");
}

// Builds the loaded piece again if the dynamics have changed since it was
// built, from the MIDI it came from.
void PianoController::rebuildForDynamics()
{
  std::shared_ptr<const PianoRoll> loaded;
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    loaded = loadedRoll;
  }
  Dynamics now = dynamics();
  if (loaded == nullptr || loaded->source == nullptr || loaded->dynamics == now)
  {
    return;
  }
  std::shared_ptr<PianoRoll> rebuilt = std::make_shared<PianoRoll>();
  if (!buildPianoRoll(*loaded->source, *rebuilt, velocityTable, now))
  {
    Serial.println("Out of memory rebuilding for the new dynamics: playing as before");
    return;
  }
  rebuilt->title = loaded->title;
  rebuilt->source = loaded->source;
  rebuilt->partNames = loaded->partNames;
  std::lock_guard<std::mutex> lock(rollMutex);
  if (loadedRoll == loaded) // not replaced by an upload meanwhile
  {
    loadedRoll = rebuilt;
  }
  Serial.printf("Rebuilt \"%s\" for volume %u%%, contrast %u%%, voicing %u%%\n", rebuilt->title.c_str(), now.volume,
                now.contrast, now.voicing);
}

void PianoController::startPlayback(uint32_t fromMs)
{
  rebuildForDynamics();
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
void PianoController::resumePlayback(uint32_t fromMs)
{
  rebuildForDynamics();
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
    sendPedal(SUSTAIN_PEDAL_PRESS);
    settleMs = PEDAL_SETTLE_MS;
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
  state = PlaybackState::Idle;
}

void PianoController::sendRollEvent(const RollEvent &event)
{
  char command[DRIVER_COMMAND_MAX_LENGTH + 1];
  switch (event.type)
  {
  case RollEventType::KeyPress:
    sendPress(event.key, event.velocity, event.pedal);
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
    sendPedal(SUSTAIN_PEDAL_PRESS);
    break;
  case RollEventType::SustainOff:
    sendPedal(SUSTAIN_PEDAL_RELEASE);
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
  sendPedal(SUSTAIN_PEDAL_RELEASE);
}

void PianoController::sendPedal(const char *params)
{
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
  return true;
}

void PianoController::sendPress(uint8_t key, uint8_t velocity, bool pedal)
{
  char command[DRIVER_COMMAND_MAX_LENGTH + 1];
  velocityTable->pressCommand(key, velocity, command, sizeof(command), pedal);
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
