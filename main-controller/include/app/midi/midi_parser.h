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

// Notes are told apart by the part of the file they come from: each track
// and channel with notes, numbered from 0 in the order they first play, up
// to this many (later ones share the last).
static const uint8_t MAX_MIDI_PARTS = 16;

struct MidiEvent
{
  uint32_t timeMs; // from the start of the file
  MidiEventType type;
  uint8_t note;  // MIDI note number, note events only
  uint8_t value; // velocity 1-127 for NoteOn, pedal value 0-127 for Sustain
  uint8_t part;  // note events only: see MAX_MIDI_PARTS
};

// Parses a Standard MIDI File (format 0 or 1) into one timeline sorted by
// time, with tempo changes applied. All tracks and channels are merged,
// except channel 10 (drums); each note says which part it's from, and
// partNames (if given) gets each part's name: its track's name, else "Track
// n", with the channel when a track has notes on more than one. A note-on
// with velocity 0 is reported as a NoteOff. Events at the same time come as
// note-offs, sustain, then note-ons. Returns false and sets error if the
// file is malformed.
bool parseMidiFile(const uint8_t *data, size_t length, std::vector<MidiEvent> &events, std::string &error,
                   std::vector<std::string> *partNames = nullptr);

#endif // MIDI_PARSER_H
