// Live play (see PianoController::startLive and live_player.h): the sender's
// messages are scheduled as they arrive (any task); the player task sends
// what's due.
#include "app/piano/piano_controller.h"
#include "app/piano/velocity_table.h"

// Without a message or ping for this long, the sender's gone: everything is
// let go. The Live page pings every 500 ms.
static const uint32_t LIVE_TIMEOUT_MS = 2000;

PlaybackResult PianoController::startLive(uint16_t delayMs)
{
  if (state != PlaybackState::Idle)
  {
    return PlaybackResult::Busy;
  }
  if (updating)
  {
    return PlaybackResult::Updating;
  }
  if (tooHotToPlay())
  {
    return PlaybackResult::TooHot;
  }
  {
    std::lock_guard<std::mutex> lock(liveMutex);
    liveConfig = playConfig();
    livePlayer.start(delayMs, liveConfig, velocityTable);
  }
  liveHeardMs = millis();
  liveEnded = LiveEnd::None;
  live = true;
  Message message;
  message.type = MessageType::LiveStart; // every key up first
  message.text[0] = '\0';
  return post(message) ? PlaybackResult::Ok : PlaybackResult::QueueFull;
}

void PianoController::liveMessage(uint32_t senderMs, uint8_t status, uint8_t data1, uint8_t data2)
{
  if (!live)
  {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(liveMutex);
    livePlayer.message(senderMs, millis(), status, data1, data2);
  }
  liveHeardMs = millis();
  wakeForLive();
}

void PianoController::liveClock(uint32_t senderMs)
{
  if (!live)
  {
    return;
  }
  std::lock_guard<std::mutex> lock(liveMutex);
  livePlayer.clock(senderMs, millis());
  liveHeardMs = millis();
}

void PianoController::setLiveDelay(uint16_t delayMs)
{
  std::lock_guard<std::mutex> lock(liveMutex);
  livePlayer.setDelay(delayMs);
}

void PianoController::stopLive()
{
  Message message;
  message.type = MessageType::LiveStop;
  message.text[0] = '\0';
  post(message);
}

PianoController::LiveStatus PianoController::liveStatus() const
{
  LiveStatus status;
  std::lock_guard<std::mutex> lock(liveMutex);
  status.active = live;
  status.delayMs = livePlayer.delay();
  status.late = livePlayer.late();
  status.ended = liveEnded;
  return status;
}

// The player task sleeps until the next thing due: a new message may be
// due sooner, so it's woken (a message in the queue does that).
void PianoController::wakeForLive()
{
  if (messageQueue != nullptr && uxQueueMessagesWaiting(messageQueue) == 0)
  {
    Message message;
    message.type = MessageType::LiveWake;
    message.text[0] = '\0';
    post(message);
  }
}

// Sends what's due, and shortens wait to when the next is.
void PianoController::playDueLive(TickType_t &wait)
{
  if (millis() - liveHeardMs > LIVE_TIMEOUT_MS)
  {
    Serial.println("Live: nothing from the sender, letting everything go");
    endLive(LiveEnd::TimedOut);
    return;
  }
  uint32_t nextMs = 0;
  bool more;
  liveDue.clear();
  {
    std::lock_guard<std::mutex> lock(liveMutex);
    livePlayer.takeDue(millis(), liveDue);
    more = livePlayer.nextDue(nextMs);
  }
  for (const LiveAction &action : liveDue)
  {
    sendLiveAction(action);
  }
  if (more)
  {
    int32_t remaining = (int32_t)(nextMs - millis());
    TickType_t until = remaining <= 0 ? 0 : pdMS_TO_TICKS(remaining);
    wait = until < wait ? until : wait;
  }
}

void PianoController::sendLiveAction(const LiveAction &action)
{
  char command[DRIVER_COMMAND_MAX_LENGTH + 1];
  switch (action.type)
  {
  case LiveAction::Type::Press:
    velocityTable->strikeCommand(action.key, action.duty, action.landingMs, command, sizeof(command), action.pedal,
                                 action.repeat ? &liveConfig.repeatClamp : nullptr);
    send(command);
    break;
  case LiveAction::Type::Release:
    snprintf(command, sizeof(command), "R:%u:0", action.key);
    send(command);
    break;
  case LiveAction::Type::PedalDown:
    sendPedal(true);
    break;
  case LiveAction::Type::PedalUp:
    sendPedal(false);
    break;
  }
}

void PianoController::endLive(LiveEnd why)
{
  {
    std::lock_guard<std::mutex> lock(liveMutex);
    livePlayer.stop();
    liveEnded = why;
    live = false;
  }
  releaseAll();
  Serial.printf("Live play ended (%s)\n", why == LiveEnd::Stopped    ? "stopped"
                                          : why == LiveEnd::TimedOut ? "the sender went quiet"
                                                                     : "heat limit");
}
