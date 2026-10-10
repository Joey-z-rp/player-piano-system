#include "app/piano/piano_controller.h"
#include "rs485.h"
#include <esp_timer.h>
#include <freertos/task.h>

static const uint32_t PLAYER_TASK_STACK = 4096;
static const UBaseType_t PLAYER_TASK_PRIORITY = 3; // above loop()
static const BaseType_t PLAYER_TASK_CORE = 1;      // Wi-Fi runs on core 0

static uint8_t velocityToDuty(uint8_t velocity)
{
  if (velocity < 1)
  {
    velocity = 1;
  }
  return KEY_MIN_DUTY + (uint32_t)(velocity - 1) * (KEY_MAX_DUTY - KEY_MIN_DUTY) / 126;
}

PianoController::PianoController()
    : rs485(nullptr), messageQueue(nullptr), state(PlaybackState::Idle), playStartMs(0),
      nextEvent(0), startUs(0), sustainHeld(false)
{
}

bool PianoController::init(Rs485Module *rs485)
{
  this->rs485 = rs485;
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
  if (state == PlaybackState::Playing)
  {
    return PlaybackResult::Busy;
  }
  loadedRoll = roll;
  return PlaybackResult::Ok;
}

PlaybackResult PianoController::play()
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

PlaybackStatus PianoController::status() const
{
  PlaybackStatus result;
  result.state = state;
  result.positionMs = result.state == PlaybackState::Playing ? millis() - playStartMs : 0;
  std::lock_guard<std::mutex> lock(rollMutex);
  result.roll = loadedRoll;
  return result;
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
    if (state != PlaybackState::Playing)
    {
      startPlayback();
    }
    break;
  case MessageType::Stop:
    if (state == PlaybackState::Playing)
    {
      finishPlayback();
    }
    else
    {
      releaseAll(); // also clears keys left on by manual commands
    }
    break;
  }
}

void PianoController::startPlayback()
{
  // Take the roll and mark it playing under one lock, so loadRoll() can't
  // swap in a new roll between the two.
  {
    std::lock_guard<std::mutex> lock(rollMutex);
    if (loadedRoll == nullptr || loadedRoll->events.empty())
    {
      return;
    }
    playingRoll = loadedRoll;
    nextEvent = 0;
    startUs = esp_timer_get_time();
    playStartMs = millis();
    state = PlaybackState::Playing;
  }
  Serial.printf("Playing \"%s\" (%u events)\n", playingRoll->title.c_str(), playingRoll->events.size());
}

void PianoController::playDueEvents()
{
  const std::vector<RollEvent> &events = playingRoll->events;
  int64_t elapsedUs = esp_timer_get_time() - startUs;
  while (nextEvent < events.size() && (int64_t)events[nextEvent].timeMs * 1000 <= elapsedUs)
  {
    sendRollEvent(events[nextEvent]);
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
    snprintf(command, sizeof(command), "P:%u:%u", event.key, velocityToDuty(event.velocity));
    send(command);
    break;
  case RollEventType::KeyRelease:
    snprintf(command, sizeof(command), "R:%u:0", event.key);
    send(command);
    break;
  case RollEventType::SustainOn:
    sendPedal(SUSTAIN_PEDAL_PRESS);
    sustainHeld = true;
    break;
  case RollEventType::SustainOff:
    sendPedal(SUSTAIN_PEDAL_RELEASE);
    sustainHeld = false;
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
  if (sustainHeld)
  {
    sendPedal(SUSTAIN_PEDAL_RELEASE);
    sustainHeld = false;
  }
  else
  {
    for (uint8_t channel : SUSTAIN_PEDAL_CHANNELS)
    {
      snprintf(command, sizeof(command), "R:%u:0", channel);
      send(command);
    }
  }
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
    if (!isdigit(c) && c != ':')
    {
      return false;
    }
  }
  return true;
}
