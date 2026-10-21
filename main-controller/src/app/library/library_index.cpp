#include "app/library/library_index.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

std::string pieceId(const uint8_t *data, size_t length)
{
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < length; i++)
  {
    hash ^= data[i];
    hash *= 0x100000001b3ULL;
  }
  char text[17];
  snprintf(text, sizeof(text), "%08lx%08lx", (unsigned long)(hash >> 32), (unsigned long)(hash & 0xffffffffUL));
  return text;
}

std::string cleanPieceName(const std::string &name)
{
  std::string out;
  for (unsigned char c : name)
  {
    if (c >= 0x20 && c != 0x7f)
    {
      out += (char)c;
    }
  }
  size_t start = out.find_first_not_of(' ');
  if (start == std::string::npos)
  {
    return "untitled";
  }
  out = out.substr(start, out.find_last_not_of(' ') - start + 1);
  if (out.size() > MAX_PIECE_NAME)
  {
    size_t cut = MAX_PIECE_NAME;
    while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80)
    {
      cut--; // not inside a UTF-8 character
    }
    out = out.substr(0, cut);
    out = out.substr(0, out.find_last_not_of(' ') + 1);
  }
  return out;
}

std::string pieceNameFromFile(const std::string &fileName)
{
  std::string name = fileName;
  for (const char *extension : {".midi", ".mid"})
  {
    size_t length = strlen(extension);
    if (name.size() >= length)
    {
      std::string end = name.substr(name.size() - length);
      for (char &c : end)
      {
        c = (char)tolower((unsigned char)c);
      }
      if (end == extension)
      {
        name = name.substr(0, name.size() - length);
        break;
      }
    }
  }
  return cleanPieceName(name);
}

static void writePiece(JsonObject out, const PieceInfo &piece)
{
  out["id"] = piece.id;
  out["name"] = piece.name;
  out["bytes"] = piece.bytes;
  out["durationMs"] = piece.durationMs;
  out["notes"] = piece.notes;
  out["added"] = piece.added;
  out["lastPlayed"] = piece.lastPlayed;
  out["volume"] = piece.dynamics.volume;
  out["contrast"] = piece.dynamics.contrast;
  out["voicing"] = piece.dynamics.voicing;
  out["repeatBoost"] = piece.dynamics.repeatBoost;
  out["speed"] = piece.dynamics.speed;
  out["muted"] = piece.dynamics.mutedParts;
  JsonArray parts = out["parts"].to<JsonArray>();
  for (int8_t offset : piece.parts)
  {
    parts.add(offset);
  }
}

bool readLibraryIndex(const char *json, size_t length, std::vector<PieceInfo> &pieces)
{
  JsonDocument doc;
  if (deserializeJson(doc, json, length) || !doc["pieces"].is<JsonArray>())
  {
    return false;
  }
  std::vector<PieceInfo> read;
  for (JsonObject entry : doc["pieces"].as<JsonArray>())
  {
    const char *id = entry["id"];
    if (id == nullptr || strlen(id) != 16)
    {
      continue;
    }
    PieceInfo piece;
    piece.id = id;
    piece.name = cleanPieceName(entry["name"] | "untitled");
    piece.bytes = entry["bytes"] | 0u;
    piece.durationMs = entry["durationMs"] | 0u;
    piece.notes = entry["notes"] | 0u;
    piece.added = entry["added"] | 0u;
    piece.lastPlayed = entry["lastPlayed"] | 0u;
    piece.dynamics.volume = entry["volume"] | 100u;
    piece.dynamics.contrast = entry["contrast"] | 100u;
    piece.dynamics.voicing = entry["voicing"] | 100u;
    piece.dynamics.repeatBoost = entry["repeatBoost"] | 0u;
    piece.dynamics.speed = entry["speed"] | 100u;
    piece.dynamics.mutedParts = entry["muted"] | 0u;
    for (JsonVariant offset : entry["parts"].as<JsonArray>())
    {
      piece.parts.push_back((int8_t)(offset | 0));
    }
    read.push_back(piece);
  }
  pieces = std::move(read);
  return true;
}

std::string libraryIndexJson(const std::vector<PieceInfo> &pieces)
{
  JsonDocument doc;
  JsonArray list = doc["pieces"].to<JsonArray>();
  for (const PieceInfo &piece : pieces)
  {
    writePiece(list.add<JsonObject>(), piece);
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string pieceJson(const PieceInfo &piece)
{
  JsonDocument doc;
  writePiece(doc.to<JsonObject>(), piece);
  std::string out;
  serializeJson(doc, out);
  return out;
}
