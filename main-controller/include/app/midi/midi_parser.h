#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

enum class MidiEventType : uint8_t
{
  NoteOff,
  Sustain,
  NoteOn
};

struct MidiEvent
{
  uint32_t timeMs; // from the start of the file
  MidiEventType type;
  uint8_t note;  // MIDI note number, note events only
  uint8_t value; // velocity 1-127 for NoteOn, pedal value 0-127 for Sustain
};

// Parses a Standard MIDI File (format 0 or 1) into one timeline sorted by
// time, with tempo changes applied. All tracks and channels are merged,
// except channel 10 (drums). A note-on with velocity 0 is reported as a
// NoteOff. Events at the same time come as note-offs, sustain, then note-ons.
// Returns false and sets error if the file is malformed.
bool parseMidiFile(const uint8_t *data, size_t length, std::vector<MidiEvent> &events, std::string &error);

#endif // MIDI_PARSER_H
