#ifndef PIECE_LIBRARY_H
#define PIECE_LIBRARY_H

#include <LittleFS.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "app/library/library_index.h"
#include "app/piano/note_edits.h"

// The pieces uploaded to the play page, kept on their own flash partition
// ("library" in partitions.csv) so uploadfs never touches them: each file as
// /pieces/<id>.mid, an index of their names, sizes and remembered settings,
// and each one's note edits (if it has any) as /edits/<id>.json. Every call
// is safe from any task.
class PieceLibrary
{
public:
  PieceLibrary();

  // Mounts the partition, formatting it the first time. False if it can't
  // (an old partition table): the library is then empty and saves nothing.
  bool begin();
  bool available() const;

  std::vector<PieceInfo> list() const;
  bool find(const std::string &id, PieceInfo &piece) const;

  enum class SaveResult
  {
    Saved,
    AlreadySaved, // the same contents: piece is the one already there
    Full,
    Failed
  };
  // Saves a file (its id from its contents), unless it's already there.
  // piece is filled in with name, durationMs and notes on the way in.
  SaveResult save(const uint8_t *data, size_t length, PieceInfo &piece);
  // Reads a piece's file.
  bool read(const std::string &id, std::vector<uint8_t> &data) const;
  bool rename(const std::string &id, const std::string &name);
  bool remove(const std::string &id);
  void markPlayed(const std::string &id);
  // A saved piece's details as a backup had them: name, dates and settings.
  bool restore(const PieceInfo &info);
  // Its settings, to play it with next time: the dynamics, with partOffset
  // for its first `parts` parts.
  void saveSettings(const std::string &id, const Dynamics &dynamics, size_t parts);
  // Its note edits (piece_edits.cpp): null if it has none (or isn't saved).
  std::shared_ptr<const NoteEdits> readEdits(const std::string &id) const;
  // Replaces them (none: the file goes). False if the piece isn't saved or
  // they couldn't be written.
  bool saveEdits(const std::string &id, const NoteEdits &edits);

  size_t usedBytes() const;
  size_t totalBytes() const;
  // For serving a piece's file.
  fs::FS &files();
  static std::string path(const std::string &id);
  static std::string editsPath(const std::string &id);

private:
  bool writeIndex(); // with the mutex held
  void removeStrays(); // at begin
  PieceInfo *entry(const std::string &id);

  fs::LittleFSFS fs;
  bool mounted;
  mutable std::mutex mutex;
  std::vector<PieceInfo> pieces;
};

// Unix seconds now, or 0 if the clock hasn't been set (no time server yet).
uint32_t unixTimeNow();

#endif // PIECE_LIBRARY_H
