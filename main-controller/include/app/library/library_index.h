#ifndef LIBRARY_INDEX_H
#define LIBRARY_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "app/piano/piano_roll.h"

static const size_t MAX_PIECE_NAME = 80; // characters (bytes of UTF-8)

// A piece saved in the library (see PieceLibrary), with what the list shows
// and the settings it was last played with.
struct PieceInfo
{
  std::string id;          // from its contents (pieceId): the same file is saved once
  std::string name;
  uint32_t bytes = 0;
  uint32_t durationMs = 0; // as written
  uint32_t notes = 0;      // on the 88 keys
  uint32_t added = 0;      // Unix seconds; 0 if the clock wasn't set
  uint32_t lastPlayed = 0; // likewise; 0 if never
  // Its own settings, as it was last played with (see Dynamics): partOffset
  // only for the parts it has, in parts.
  Dynamics dynamics;
  std::vector<int8_t> parts;
};

// The id for a file's contents: 16 hex digits (FNV-1a, 64 bits).
std::string pieceId(const uint8_t *data, size_t length);

// A name as the library keeps it: control characters dropped, spaces
// trimmed, at most MAX_PIECE_NAME bytes (not splitting a UTF-8 character);
// "untitled" if nothing's left.
std::string cleanPieceName(const std::string &name);
// A piece's name from its file's: without ".mid" or ".midi".
std::string pieceNameFromFile(const std::string &fileName);

// The index file: {"pieces": [{"id", "name", "bytes", "durationMs", "notes",
// "added", "lastPlayed", "speed", "parts": [...]}, ...]}. Reading skips
// entries it can't use; false only if it isn't an index at all.
bool readLibraryIndex(const char *json, size_t length, std::vector<PieceInfo> &pieces);
std::string libraryIndexJson(const std::vector<PieceInfo> &pieces);
// One piece, in the same form.
std::string pieceJson(const PieceInfo &piece);

#endif // LIBRARY_INDEX_H
