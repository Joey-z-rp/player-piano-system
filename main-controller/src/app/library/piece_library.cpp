#include "app/library/piece_library.h"
#include <algorithm>
#include <time.h>

static const char INDEX_PATH[] = "/index.json";
static const char INDEX_TEMP_PATH[] = "/index.tmp";
static const char PIECES_DIR[] = "/pieces";
static const char EDITS_DIR[] = "/edits";
static const size_t FREE_MARGIN = 32 * 1024; // left free for the index and the file system's own use

uint32_t unixTimeNow()
{
  time_t now = time(nullptr);
  return now > 1600000000 ? (uint32_t)now : 0; // before 2020: not set yet
}

PieceLibrary::PieceLibrary() : mounted(false)
{
}

bool PieceLibrary::begin()
{
  std::lock_guard<std::mutex> lock(mutex);
  mounted = fs.begin(true, "/library", 5, "library");
  if (!mounted)
  {
    Serial.println("Piece library: no \"library\" partition (upload once with partitions.csv)");
    return false;
  }
  fs.mkdir(PIECES_DIR);
  fs.mkdir(EDITS_DIR);
  File file = fs.open(INDEX_PATH, "r");
  if (file)
  {
    std::string json(file.size(), '\0');
    file.read(reinterpret_cast<uint8_t *>(&json[0]), json.size());
    file.close();
    if (!readLibraryIndex(json.data(), json.size(), pieces))
    {
      Serial.println("Piece library: index unreadable, starting a new one");
    }
  }
  // Only pieces whose files are there.
  std::vector<PieceInfo> present;
  for (const PieceInfo &piece : pieces)
  {
    if (fs.exists(path(piece.id).c_str()))
    {
      present.push_back(piece);
    }
  }
  pieces = std::move(present);
  removeStrays();
  Serial.printf("Piece library: %u pieces, %u of %u KB used\n", (unsigned)pieces.size(),
                (unsigned)(fs.usedBytes() / 1024), (unsigned)(fs.totalBytes() / 1024));
  return true;
}

// Says what's on the partition besides the pieces, and deletes piece files
// the index doesn't list (a save cut short): nothing could show or play them.
void PieceLibrary::removeStrays()
{
  size_t strays = 0, strayBytes = 0, other = 0;
  File dir = fs.open(PIECES_DIR);
  std::vector<std::string> remove;
  for (File file = dir.openNextFile(); file; file = dir.openNextFile())
  {
    std::string name = file.name(); // without the directory
    std::string id = name.size() == 20 && name.substr(16) == ".mid" ? name.substr(0, 16) : "";
    if (id.empty() || entry(id) == nullptr)
    {
      remove.push_back(std::string(PIECES_DIR) + "/" + name);
      strayBytes += file.size();
    }
    file.close();
  }
  dir.close();
  // Edits of pieces no longer there.
  File edits = fs.open(EDITS_DIR);
  for (File file = edits.openNextFile(); file; file = edits.openNextFile())
  {
    std::string name = file.name();
    std::string id = name.size() == 21 && name.substr(16) == ".json" ? name.substr(0, 16) : "";
    if (id.empty() || entry(id) == nullptr)
    {
      remove.push_back(std::string(EDITS_DIR) + "/" + name);
      strayBytes += file.size();
    }
    file.close();
  }
  edits.close();
  for (const std::string &stray : remove)
  {
    fs.remove(stray.c_str());
    strays++;
  }
  File root = fs.open("/");
  for (File file = root.openNextFile(); file; file = root.openNextFile())
  {
    std::string name = file.name();
    if (name != "pieces" && name != "edits" && name != "index.json")
    {
      Serial.printf("Piece library: unexpected %s \"%s\" (%u bytes)\n", file.isDirectory() ? "folder" : "file",
                    name.c_str(), (unsigned)file.size());
      other++;
    }
    file.close();
  }
  root.close();
  if (strays > 0)
  {
    Serial.printf("Piece library: deleted %u files of pieces the index didn't list (%u KB)\n", (unsigned)strays,
                  (unsigned)(strayBytes / 1024));
  }
}

bool PieceLibrary::available() const
{
  return mounted;
}

std::string PieceLibrary::path(const std::string &id)
{
  return std::string(PIECES_DIR) + "/" + id + ".mid";
}

std::string PieceLibrary::editsPath(const std::string &id)
{
  return std::string(EDITS_DIR) + "/" + id + ".json";
}

std::vector<PieceInfo> PieceLibrary::list() const
{
  std::lock_guard<std::mutex> lock(mutex);
  return pieces;
}

bool PieceLibrary::find(const std::string &id, PieceInfo &piece) const
{
  std::lock_guard<std::mutex> lock(mutex);
  for (const PieceInfo &p : pieces)
  {
    if (p.id == id)
    {
      piece = p;
      return true;
    }
  }
  return false;
}

PieceInfo *PieceLibrary::entry(const std::string &id)
{
  for (PieceInfo &p : pieces)
  {
    if (p.id == id)
    {
      return &p;
    }
  }
  return nullptr;
}

bool PieceLibrary::writeIndex()
{
  std::string json = libraryIndexJson(pieces);
  File file = fs.open(INDEX_TEMP_PATH, "w");
  if (!file)
  {
    return false;
  }
  bool written = file.write(reinterpret_cast<const uint8_t *>(json.data()), json.size()) == json.size();
  file.close();
  return written && fs.rename(INDEX_TEMP_PATH, INDEX_PATH);
}

PieceLibrary::SaveResult PieceLibrary::save(const uint8_t *data, size_t length, PieceInfo &piece)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (!mounted)
  {
    return SaveResult::Failed;
  }
  piece.id = pieceId(data, length);
  if (PieceInfo *existing = entry(piece.id))
  {
    piece = *existing;
    return SaveResult::AlreadySaved;
  }
  if (fs.totalBytes() < fs.usedBytes() + length + FREE_MARGIN)
  {
    return SaveResult::Full;
  }
  std::string filePath = path(piece.id);
  File file = fs.open(filePath.c_str(), "w");
  if (!file)
  {
    return SaveResult::Failed;
  }
  bool written = file.write(data, length) == length;
  file.close();
  if (!written)
  {
    fs.remove(filePath.c_str());
    return SaveResult::Failed;
  }
  piece.name = cleanPieceName(piece.name);
  piece.bytes = length;
  piece.added = unixTimeNow();
  piece.lastPlayed = 0;
  piece.dynamics = Dynamics();
  piece.parts.clear();
  pieces.push_back(piece);
  if (!writeIndex())
  {
    pieces.pop_back();
    fs.remove(filePath.c_str());
    return SaveResult::Failed;
  }
  return SaveResult::Saved;
}

bool PieceLibrary::read(const std::string &id, std::vector<uint8_t> &data) const
{
  std::lock_guard<std::mutex> lock(mutex);
  File file = const_cast<fs::LittleFSFS &>(fs).open(path(id).c_str(), "r");
  if (!file)
  {
    return false;
  }
  data.resize(file.size());
  bool read = file.read(data.data(), data.size()) == data.size();
  file.close();
  return read;
}

bool PieceLibrary::rename(const std::string &id, const std::string &name)
{
  std::lock_guard<std::mutex> lock(mutex);
  PieceInfo *piece = entry(id);
  if (piece == nullptr)
  {
    return false;
  }
  piece->name = cleanPieceName(name);
  return writeIndex();
}

bool PieceLibrary::remove(const std::string &id)
{
  std::lock_guard<std::mutex> lock(mutex);
  for (size_t i = 0; i < pieces.size(); i++)
  {
    if (pieces[i].id == id)
    {
      pieces.erase(pieces.begin() + i);
      writeIndex();
      fs.remove(path(id).c_str());
      fs.remove(editsPath(id).c_str());
      return true;
    }
  }
  return false;
}

bool PieceLibrary::restore(const PieceInfo &info)
{
  std::lock_guard<std::mutex> lock(mutex);
  PieceInfo *piece = entry(info.id);
  if (piece == nullptr)
  {
    return false;
  }
  piece->name = cleanPieceName(info.name);
  piece->added = info.added;
  piece->lastPlayed = info.lastPlayed;
  piece->dynamics = info.dynamics;
  piece->parts = info.parts;
  return writeIndex();
}

void PieceLibrary::markPlayed(const std::string &id)
{
  std::lock_guard<std::mutex> lock(mutex);
  PieceInfo *piece = entry(id);
  if (piece != nullptr)
  {
    piece->lastPlayed = unixTimeNow();
    writeIndex();
  }
}

void PieceLibrary::saveSettings(const std::string &id, const Dynamics &dynamics, size_t parts)
{
  std::lock_guard<std::mutex> lock(mutex);
  PieceInfo *piece = entry(id);
  std::vector<int8_t> offsets(dynamics.partOffset, dynamics.partOffset + std::min(parts, (size_t)MAX_MIDI_PARTS));
  Dynamics kept = dynamics; // the part offsets are kept in parts
  std::fill(kept.partOffset, kept.partOffset + MAX_MIDI_PARTS, 0);
  if (piece != nullptr && (!(piece->dynamics == kept) || piece->parts != offsets))
  {
    piece->dynamics = kept;
    piece->parts = offsets;
    writeIndex();
  }
}

size_t PieceLibrary::usedBytes() const
{
  std::lock_guard<std::mutex> lock(mutex);
  return mounted ? const_cast<fs::LittleFSFS &>(fs).usedBytes() : 0;
}

size_t PieceLibrary::totalBytes() const
{
  std::lock_guard<std::mutex> lock(mutex);
  return mounted ? const_cast<fs::LittleFSFS &>(fs).totalBytes() : 0;
}

fs::FS &PieceLibrary::files()
{
  return fs;
}
