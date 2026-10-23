#ifndef PIECE_LOADER_H
#define PIECE_LOADER_H

#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "app/library/library_index.h"
#include "app/piano/piano_roll.h"

class AsyncWebServerRequest;
class PianoController;
struct MidiEvent;

static const size_t MAX_MIDI_FILE_SIZE = 512 * 1024;

// For an upload's body handler: collects the body (a MIDI file) into one
// buffer in request->_tempObject, which the request frees; one over
// MAX_MIDI_FILE_SIZE is drained without storing.
void collectMidiBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
// What the library lists for a piece (durationMs, notes), from its events.
void describePiece(const std::vector<MidiEvent> &events, PieceInfo &piece);

// Builds a piece from a MIDI file and loads it to play, with the settings it
// was last played with if it's in the library (saved; else its written
// speed and parts) and its note edits (null: none). On failure, status is
// the HTTP status and error why.
bool loadPiece(PianoController &piano, const uint8_t *data, size_t length, const std::string &title,
               const PieceInfo *saved, std::shared_ptr<const NoteEdits> edits, std::shared_ptr<PianoRoll> &roll,
               int &status, std::string &error);

#endif // PIECE_LOADER_H
