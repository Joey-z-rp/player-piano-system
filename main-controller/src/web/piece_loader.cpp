#include "web/piece_loader.h"
#include "app/midi/midi_parser.h"
#include "app/piano/piano_controller.h"
#include "app/piano/roll_builder.h"
#include <ESPAsyncWebServer.h>
#include <algorithm>

void collectMidiBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (index == 0 && total > 0 && total <= MAX_MIDI_FILE_SIZE)
  {
    request->_tempObject = malloc(total);
  }
  if (request->_tempObject != nullptr)
  {
    memcpy(static_cast<uint8_t *>(request->_tempObject) + index, data, len);
  }
}

void describePiece(const std::vector<MidiEvent> &events, PieceInfo &piece)
{
  piece.durationMs = events.empty() ? 0 : events.back().timeMs;
  piece.notes = 0;
  for (const MidiEvent &event : events)
  {
    piece.notes += event.type == MidiEventType::NoteOn && event.note >= 21 && event.note <= 108; // on the 88 keys
  }
}

bool loadPiece(PianoController &piano, const uint8_t *data, size_t length, const std::string &title,
               const PieceInfo *saved, std::shared_ptr<const NoteEdits> edits, std::shared_ptr<PianoRoll> &roll,
               int &status, std::string &error)
{
  if (piano.status().state != PlaybackState::Idle)
  {
    status = 409;
    error = "a piece is playing";
    return false;
  }
  std::vector<MidiEvent> events;
  std::vector<std::string> partNames;
  if (!parseMidiFile(data, length, events, error, &partNames))
  {
    status = 400;
    return false;
  }
  roll = std::make_shared<PianoRoll>();
  roll->partNames = std::move(partNames);
  // Its own settings, as last played (anything out of range: the default),
  // else the defaults.
  Dynamics dynamics;
  if (saved != nullptr)
  {
    const Dynamics &own = saved->dynamics;
    auto within = [](uint16_t value, uint16_t low, uint16_t high, uint16_t otherwise)
    { return value >= low && value <= high ? value : otherwise; };
    dynamics.volume = within(own.volume, MIN_VOLUME, MAX_VOLUME, DEFAULT_VOLUME);
    dynamics.contrast = within(own.contrast, MIN_CONTRAST, MAX_CONTRAST, 100);
    dynamics.voicing = within(own.voicing, MIN_VOICING, MAX_VOICING, 100);
    dynamics.repeatBoost = (uint8_t)within(own.repeatBoost, 0, MAX_REPEAT_BOOST, 0);
    dynamics.speed = within(own.speed, MIN_SPEED, MAX_SPEED, 100);
    for (size_t i = 0; i < saved->parts.size() && i < roll->partNames.size(); i++)
    {
      dynamics.partOffset[i] = std::max<int8_t>(-MAX_PART_OFFSET, std::min<int8_t>(MAX_PART_OFFSET, saved->parts[i]));
    }
    // Only the parts it has, and never all of them.
    uint16_t parts = roll->partNames.size() >= 16 ? 0xFFFF : (uint16_t)((1u << roll->partNames.size()) - 1);
    dynamics.mutedParts = (own.mutedParts & parts) == parts ? 0 : own.mutedParts & parts;
  }
  PlayConfig rules = piano.playConfig();
  roll->configVersion = piano.playConfigVersion();
  roll->edits = edits;
  if (!buildPianoRoll(events, *roll, piano.velocities(), dynamics, nullptr, &rules, edits.get()))
  {
    status = 500;
    error = "out of memory";
    return false;
  }
  // Kept, so Play can build it again if the settings change.
  roll->source = std::make_shared<const std::vector<MidiEvent>>(std::move(events));
  if (roll->noteCount == 0)
  {
    status = 400;
    error = "no playable notes";
    return false;
  }
  roll->title = title;
  roll->libraryId = saved != nullptr ? saved->id : "";
  static uint32_t loads = 0;
  roll->id = ++loads;
  if (piano.loadRoll(roll) != PlaybackResult::Ok)
  {
    status = 409;
    error = "a piece is playing";
    return false;
  }
  // loadRoll starts a piece from the defaults: these are its own.
  piano.setVolume(dynamics.volume);
  piano.setContrast(dynamics.contrast);
  piano.setVoicing(dynamics.voicing);
  piano.setRepeatBoost(dynamics.repeatBoost);
  piano.setSpeed(dynamics.speed);
  for (size_t i = 0; i < roll->partNames.size() && i < MAX_MIDI_PARTS; i++)
  {
    piano.setPartOffset((uint8_t)i, dynamics.partOffset[i]);
    piano.setPartMuted((uint8_t)i, (dynamics.mutedParts >> i) & 1);
  }
  return true;
}
