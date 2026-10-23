#include "app/piano/note_edits.h"
#include "app/piano/piano_roll.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <stdio.h>

static bool before(const NoteEdit &a, uint32_t atMs, uint8_t key)
{
  return a.atMs != atMs ? a.atMs < atMs : a.key < key;
}

const NoteEdit *NoteEdits::find(uint8_t key, uint32_t atMs) const
{
  auto at = std::lower_bound(edits.begin(), edits.end(), std::make_pair(atMs, key),
                             [](const NoteEdit &edit, const std::pair<uint32_t, uint8_t> &note)
                             { return before(edit, note.first, note.second); });
  return at != edits.end() && at->atMs == atMs && at->key == key ? &*at : nullptr;
}

const PedalEdit *NoteEdits::findPedal(uint32_t atMs) const
{
  auto at = std::lower_bound(pedal.begin(), pedal.end(), atMs,
                             [](const PedalEdit &edit, uint32_t ms) { return edit.atMs < ms; });
  return at != pedal.end() && at->atMs == atMs ? &*at : nullptr;
}

static bool readPedal(JsonVariant value, std::vector<PedalEdit> &out, std::string &error)
{
  JsonArray list = value.as<JsonArray>();
  if (list.isNull() || list.size() > MAX_PEDAL_EDITS)
  {
    error = "pedal edits must be a list of up to " + std::to_string(MAX_PEDAL_EDITS) +
            " [at ms, down ms, up ms, removed]";
    return false;
  }
  auto shift = [](JsonVariant v)
  { return v.is<long>() && v.as<long>() >= -MAX_PEDAL_SHIFT_MS && v.as<long>() <= MAX_PEDAL_SHIFT_MS; };
  std::vector<PedalEdit> read;
  for (JsonVariant item : list)
  {
    JsonArray e = item.as<JsonArray>();
    if (e.isNull() || e.size() != 4 || !e[0].is<uint32_t>() || !shift(e[1]) || !shift(e[2]) || !e[3].is<long>() ||
        e[3].as<long>() < 0 || e[3].as<long>() > 1)
    {
      error = "each pedal edit must be [at ms, down ms -" + std::to_string(MAX_PEDAL_SHIFT_MS) + " to " +
              std::to_string(MAX_PEDAL_SHIFT_MS) + ", up ms likewise, removed 0 or 1]";
      return false;
    }
    read.push_back({e[0].as<uint32_t>(), (int16_t)e[1].as<long>(), (int16_t)e[2].as<long>(), e[3].as<long>() == 1});
  }
  std::stable_sort(read.begin(), read.end(), [](const PedalEdit &a, const PedalEdit &b) { return a.atMs < b.atMs; });
  out.clear();
  for (const PedalEdit &edit : read)
  {
    if (!out.empty() && out.back().atMs == edit.atMs)
    {
      out.pop_back();
    }
    if (edit.downMs != 0 || edit.upMs != 0 || edit.removed)
    {
      out.push_back(edit);
    }
  }
  return true;
}

bool readNoteEdits(const char *json, size_t length, NoteEdits &edits, std::string &error)
{
  JsonDocument doc;
  if (deserializeJson(doc, json, length) || !doc.is<JsonObject>())
  {
    error = "edits must be {\"notes\": [...], \"pedal\": [...]}";
    return false;
  }
  NoteEdits read;
  if (!doc["pedal"].isNull() && !readPedal(doc["pedal"], read.pedal, error))
  {
    return false;
  }
  JsonArray list = doc["notes"].as<JsonArray>();
  if (!doc["notes"].isNull() && list.isNull())
  {
    error = "notes must be a list of [key, at ms, velocity, length ms, shift ms]";
    return false;
  }
  if (list.size() > MAX_NOTE_EDITS)
  {
    error = "a piece can have up to " + std::to_string(MAX_NOTE_EDITS) + " edited notes";
    return false;
  }
  auto whole = [](JsonVariant value, long low, long high)
  { return value.is<long>() && value.as<long>() >= low && value.as<long>() <= high; };
  read.edits.reserve(list.size());
  for (JsonVariant item : list)
  {
    JsonArray e = item.as<JsonArray>();
    if (e.isNull() || e.size() != 5 || !whole(e[0], 0, PIANO_KEY_COUNT - 1) || !e[1].is<uint32_t>() ||
        !whole(e[2], -MAX_EDIT_VELOCITY, MAX_EDIT_VELOCITY) || !whole(e[3], -MAX_EDIT_LENGTH_MS, MAX_EDIT_LENGTH_MS) ||
        !whole(e[4], -MAX_EDIT_SHIFT_MS, MAX_EDIT_SHIFT_MS))
    {
      error = "each edit must be [key 0-87, at ms, velocity -" + std::to_string(MAX_EDIT_VELOCITY) + " to " +
              std::to_string(MAX_EDIT_VELOCITY) + ", length ms -" + std::to_string(MAX_EDIT_LENGTH_MS) + " to " +
              std::to_string(MAX_EDIT_LENGTH_MS) + ", shift ms -" + std::to_string(MAX_EDIT_SHIFT_MS) + " to " +
              std::to_string(MAX_EDIT_SHIFT_MS) + "]";
      return false;
    }
    NoteEdit edit = {e[1].as<uint32_t>(), (uint8_t)e[0].as<long>(), (int8_t)e[2].as<long>(),
                     (int16_t)e[3].as<long>(), (int16_t)e[4].as<long>()};
    read.edits.push_back(edit);
  }
  // In order, the last of any note's, and none that change nothing.
  std::stable_sort(read.edits.begin(), read.edits.end(),
                   [](const NoteEdit &a, const NoteEdit &b) { return before(a, b.atMs, b.key); });
  std::vector<NoteEdit> kept;
  for (const NoteEdit &edit : read.edits)
  {
    if (!kept.empty() && kept.back().atMs == edit.atMs && kept.back().key == edit.key)
    {
      kept.pop_back();
    }
    kept.push_back(edit);
  }
  kept.erase(std::remove_if(kept.begin(), kept.end(), [](const NoteEdit &edit)
                            { return edit.velocity == 0 && edit.lengthMs == 0 && edit.shiftMs == 0; }),
             kept.end());
  read.edits = std::move(kept);
  edits = std::move(read);
  return true;
}

std::string noteEditsJson(const NoteEdits &edits)
{
  std::string out = "{\"notes\":[";
  char text[64];
  for (size_t i = 0; i < edits.edits.size(); i++)
  {
    const NoteEdit &e = edits.edits[i];
    snprintf(text, sizeof(text), "%s[%u,%lu,%d,%d,%d]", i ? "," : "", e.key, (unsigned long)e.atMs, e.velocity,
             e.lengthMs, e.shiftMs);
    out += text;
  }
  out += "],\"pedal\":[";
  for (size_t i = 0; i < edits.pedal.size(); i++)
  {
    const PedalEdit &e = edits.pedal[i];
    snprintf(text, sizeof(text), "%s[%lu,%d,%d,%d]", i ? "," : "", (unsigned long)e.atMs, e.downMs, e.upMs,
             e.removed ? 1 : 0);
    out += text;
  }
  return out + "]}";
}
