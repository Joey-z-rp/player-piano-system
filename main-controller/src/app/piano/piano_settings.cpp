// The controller's settings: the play config and the per-key offsets, kept on
// the "settings" partition so they survive restarts and uploadfs; and the
// loaded piece's own (volume, contrast, voicing, repeat boost, speed, part
// offsets and mutes), which a library piece keeps in the library instead.
#include "app/piano/piano_controller.h"
#include "app/piano/velocity_table.h"
#include <Preferences.h>

static const char SETTINGS_NAMESPACE[] = "piano";
static const char CONFIG_KEY[] = "playConfig";
static const char OFFSETS_KEY[] = "keyOffsets";
// Their own partition (partitions.csv), with room for a full set of key
// offsets to be rewritten; nvs keeps Wi-Fi's and the radio's data. Settings
// left in nvs by older firmware aren't read: they start from the defaults.
static const char SETTINGS_PARTITION[] = "settings";

static bool openSettings(Preferences &settings, bool readOnly)
{
  return settings.begin(SETTINGS_NAMESPACE, readOnly, SETTINGS_PARTITION);
}

void PianoController::loadSettings()
{
  Preferences settings;
  if (openSettings(settings, true))
  {
    String savedConfig = settings.getString(CONFIG_KEY, "");
    std::string savedOffsets(settings.getBytesLength(OFFSETS_KEY), '\0');
    if (!savedOffsets.empty())
    {
      settings.getBytes(OFFSETS_KEY, &savedOffsets[0], savedOffsets.size());
    }
    settings.end();
    std::string offsetsError;
    std::shared_ptr<KeyOffsets> offsets = std::make_shared<KeyOffsets>();
    if (!savedOffsets.empty())
    {
      if (readKeyOffsets(savedOffsets.data(), savedOffsets.size(), *offsets, offsetsError))
      {
        velocityTable->setOffsets(offsets);
      }
      else
      {
        Serial.printf("Saved key offsets not used (%s)\n", offsetsError.c_str());
      }
    }
    std::string error;
    if (savedConfig.length() > 0 && !readPlayConfig(savedConfig.c_str(), savedConfig.length(), config, error))
    {
      Serial.printf("Saved play config not used (%s): using the defaults\n", error.c_str());
    }
  }
}

bool PianoController::setVolume(uint16_t percent)
{
  if (percent < MIN_VOLUME || percent > MAX_VOLUME)
  {
    return false;
  }
  volumePercent = percent;
  return true;
}

uint16_t PianoController::volume() const
{
  return volumePercent;
}

bool PianoController::setContrast(uint16_t percent)
{
  if (percent < MIN_CONTRAST || percent > MAX_CONTRAST)
  {
    return false;
  }
  contrastPercent = percent;
  return true;
}

bool PianoController::setVoicing(uint16_t percent)
{
  if (percent < MIN_VOICING || percent > MAX_VOICING)
  {
    return false;
  }
  voicingPercent = percent;
  return true;
}

bool PianoController::setRepeatBoost(uint8_t velocities)
{
  if (velocities > MAX_REPEAT_BOOST)
  {
    return false;
  }
  repeatBoost = velocities;
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

bool PianoController::setPartMuted(uint8_t part, bool muted)
{
  if (part >= MAX_MIDI_PARTS)
  {
    return false;
  }
  uint16_t bit = 1u << part;
  mutedParts = muted ? (mutedParts | bit) : (mutedParts & ~bit);
  return true;
}

bool PianoController::setSpeed(uint16_t percent)
{
  if (percent < MIN_SPEED || percent > MAX_SPEED)
  {
    return false;
  }
  if (speedPercent.exchange(percent) != percent && state != PlaybackState::Playing)
  {
    Message message;
    message.type = MessageType::Rebuild;
    message.text[0] = '\0';
    post(message); // if the queue's full, it's rebuilt at the next Play instead
  }
  return true;
}

PlayConfig PianoController::playConfig() const
{
  std::lock_guard<std::mutex> lock(configMutex);
  return config;
}

uint32_t PianoController::playConfigVersion() const
{
  return configVersion;
}

void PianoController::saveConfig(const PlayConfig &saved)
{
  Preferences settings;
  if (openSettings(settings, false))
  {
    settings.putString(CONFIG_KEY, playConfigJson(saved).c_str());
    settings.end();
  }
}

bool PianoController::setPlayConfig(const char *json, size_t length, std::string &error)
{
  std::lock_guard<std::mutex> lock(configMutex);
  PlayConfig next = config;
  if (!readPlayConfig(json, length, next, error))
  {
    return false;
  }
  if (next != config)
  {
    config = next;
    configVersion++;
    saveConfig(next);
  }
  return true;
}

void PianoController::resetPlayConfig()
{
  std::lock_guard<std::mutex> lock(configMutex);
  config = PlayConfig();
  configVersion++;
  Preferences settings;
  if (openSettings(settings, false))
  {
    settings.remove(CONFIG_KEY);
    settings.end();
  }
}

std::string PianoController::keyOffsetsJson() const
{
  std::shared_ptr<const KeyOffsets> offsets = velocityTable->offsets();
  return offsets == nullptr ? "{\"keys\":{}}" : ::keyOffsetsJson(*offsets);
}

void PianoController::setKeyOffsets(uint8_t key, const KeyOffsets::Entry &entry)
{
  if (key >= PIANO_KEY_COUNT)
  {
    return;
  }
  std::lock_guard<std::mutex> lock(offsetsMutex);
  std::shared_ptr<const KeyOffsets> now = velocityTable->offsets();
  std::shared_ptr<KeyOffsets> next = now != nullptr ? std::make_shared<KeyOffsets>(*now) : std::make_shared<KeyOffsets>();
  next->set(key, entry);
  useKeyOffsets(next);
}

void PianoController::setAllKeyOffsets(const KeyOffsets &offsets)
{
  std::lock_guard<std::mutex> lock(offsetsMutex);
  useKeyOffsets(std::make_shared<KeyOffsets>(offsets));
}

void PianoController::clearKeyOffsets()
{
  std::lock_guard<std::mutex> lock(offsetsMutex);
  useKeyOffsets(nullptr);
}

void PianoController::useKeyOffsets(std::shared_ptr<const KeyOffsets> offsets)
{
  if (offsets != nullptr && offsets->empty())
  {
    offsets = nullptr;
  }
  velocityTable->setOffsets(offsets);
  configVersion++; // pieces are rebuilt for them
  Preferences settings;
  if (openSettings(settings, false))
  {
    if (offsets == nullptr)
    {
      settings.remove(OFFSETS_KEY);
    }
    else
    {
      std::string json = ::keyOffsetsJson(*offsets);
      settings.putBytes(OFFSETS_KEY, json.data(), json.size());
    }
    settings.end();
  }
}

Dynamics PianoController::dynamics() const
{
  Dynamics now;
  now.volume = volumePercent;
  now.contrast = contrastPercent;
  now.voicing = voicingPercent;
  now.repeatBoost = repeatBoost;
  now.speed = speedPercent;
  now.mutedParts = mutedParts;
  for (uint8_t part = 0; part < MAX_MIDI_PARTS; part++)
  {
    now.partOffset[part] = partOffsets[part];
  }
  return now;
}
