// Watching the solenoids' heat (see HeatModel and PianoController::heatStatus):
// estimated every HEAT_TICK_MS on the player task, from the commands sent.
#include "app/piano/piano_controller.h"

void PianoController::watchHeat()
{
  uint32_t now = millis();
  heatCheckedMs = now;
  uint8_t hot = HEAT_CHANNELS;
  {
    std::lock_guard<std::mutex> lock(heatMutex);
    heat.configure(playConfig().heat); // as the config page last set it
    heat.update(now);
    // The first watched channel at its limit that's still being driven.
    for (uint8_t channel = 0; channel < HEAT_CHANNELS && hot == HEAT_CHANNELS; channel++)
    {
      if (heat.load(channel) >= 1 && heat.on(channel, now))
      {
        hot = channel;
      }
    }
  }
  if (hot == HEAT_CHANNELS)
  {
    return;
  }
  HeatAction action;
  if (state == PlaybackState::Playing && !testing)
  {
    pausePlayback(); // lets every key and the pedal go
    action = HeatAction::Paused;
  }
  else if (testing)
  {
    finishPlayback();
    action = HeatAction::TestStopped;
  }
  else
  {
    // Held by a manual command: let it go (the pedal's two together).
    if (hot >= FIRST_PEDAL_CHANNEL)
    {
      sendPedal(false);
    }
    else
    {
      char command[DRIVER_COMMAND_MAX_LENGTH + 1];
      snprintf(command, sizeof(command), "R:%u:0", hot);
      send(command);
    }
    action = HeatAction::Released;
  }
  std::lock_guard<std::mutex> lock(heatMutex);
  heatAction = action;
  heatActionChannel = hot;
  Serial.printf("Solenoid %u at its heat limit (%.0f °C rise): %s\n", hot, heat.riseC(hot),
                action == HeatAction::Paused        ? "paused the piece"
                : action == HeatAction::TestStopped ? "stopped the test"
                                                     : "released it");
}

bool PianoController::tooHotToPlay() const
{
  std::lock_guard<std::mutex> lock(heatMutex);
  uint8_t hottest = heat.hottest();
  return hottest < HEAT_CHANNELS && heat.load(hottest) >= HEAT_RESUME_LOAD;
}

PianoController::HeatStatus PianoController::heatStatus() const
{
  std::lock_guard<std::mutex> lock(heatMutex);
  HeatStatus status;
  status.action = heatAction;
  status.actionChannel = heatActionChannel;
  uint8_t hottest = heat.hottest();
  if (hottest == HEAT_CHANNELS)
  {
    return status;
  }
  status.watched = true;
  status.channel = hottest;
  status.riseC = heat.riseC(hottest);
  status.limitC = heat.settings().limitC;
  status.load = heat.load(hottest);
  status.coolS = heat.secondsToCool(hottest, HEAT_RESUME_LOAD);
  return status;
}

void PianoController::clearHeatAction()
{
  std::lock_guard<std::mutex> lock(heatMutex);
  heatAction = HeatAction::None;
}
