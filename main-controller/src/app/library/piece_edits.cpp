// The library's note edits (see PieceLibrary::readEdits), one file per piece.
#include "app/library/piece_library.h"

std::shared_ptr<const NoteEdits> PieceLibrary::readEdits(const std::string &id) const
{
  std::lock_guard<std::mutex> lock(mutex);
  if (!mounted)
  {
    return nullptr;
  }
  File file = const_cast<fs::LittleFSFS &>(fs).open(editsPath(id).c_str(), "r");
  if (!file)
  {
    return nullptr;
  }
  std::string json(file.size(), '\0');
  bool read = file.read(reinterpret_cast<uint8_t *>(&json[0]), json.size()) == json.size();
  file.close();
  std::shared_ptr<NoteEdits> edits = std::make_shared<NoteEdits>();
  std::string error;
  if (!read || !readNoteEdits(json.data(), json.size(), *edits, error))
  {
    Serial.printf("Piece library: note edits of %s unreadable (%s)\n", id.c_str(), error.c_str());
    return nullptr;
  }
  return edits->empty() ? nullptr : edits;
}

bool PieceLibrary::saveEdits(const std::string &id, const NoteEdits &edits)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (!mounted || entry(id) == nullptr)
  {
    return false;
  }
  std::string filePath = editsPath(id);
  if (edits.empty())
  {
    fs.remove(filePath.c_str());
    return true;
  }
  std::string json = noteEditsJson(edits);
  std::string tempPath = filePath + ".tmp";
  File file = fs.open(tempPath.c_str(), "w");
  if (!file)
  {
    return false;
  }
  bool written = file.write(reinterpret_cast<const uint8_t *>(json.data()), json.size()) == json.size();
  file.close();
  if (!written)
  {
    fs.remove(tempPath.c_str());
    return false;
  }
  fs.remove(filePath.c_str());
  return fs.rename(tempPath.c_str(), filePath.c_str());
}
