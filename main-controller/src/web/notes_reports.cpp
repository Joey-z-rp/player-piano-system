#include "web/notes_reports.h"
#include "app/piano/note_edits.h"
#include "app/piano/piano_controller.h"
#include <new>

// Below the player (3) and level with loop(), which it shares core 1 with:
// playing always goes first, and the status keeps flowing.
static const UBaseType_t BUILDER_PRIORITY = 1;
static const uint32_t BUILDER_STACK = 8192;
static const BaseType_t BUILDER_CORE = 1;

bool NotesReports::begin(PianoController &piano)
{
  this->piano = &piano;
  return xTaskCreatePinnedToCore(taskEntry, "notes", BUILDER_STACK, this, BUILDER_PRIORITY, &task, BUILDER_CORE) == pdPASS;
}

bool NotesReports::get(Ready &out)
{
  PlaybackStatus playback = piano->status();
  std::shared_ptr<const PianoRoll> loaded = playback.roll;
  if (loaded == nullptr || loaded->source == nullptr)
  {
    return false;
  }
  // While playing, what it's playing with; new settings apply from the next Play.
  bool playing = playback.state == PlaybackState::Playing;
  Job job;
  job.source = loaded->source;
  job.edits = playing ? loaded->edits : piano->noteEdits();
  job.key.rollId = loaded->id;
  job.key.source = loaded->source.get();
  job.key.dynamics = playing ? loaded->dynamics : piano->dynamics();
  job.key.edits = job.edits.get();
  job.key.configVersion = piano->playConfigVersion();

  std::lock_guard<std::mutex> lock(mutex);
  if (haveReady && readyKey == job.key)
  {
    out = ready;
    return true;
  }
  if (!(building && buildingKey == job.key))
  {
    wanted = job;
    haveWanted = true;
    xTaskNotifyGive(task);
  }
  return false;
}

void NotesReports::taskEntry(void *arg)
{
  static_cast<NotesReports *>(arg)->run();
}

void NotesReports::run()
{
  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;)
    {
      Job job;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (!haveWanted)
        {
          building = false;
          break;
        }
        job = wanted;
        haveWanted = false;
        wanted = Job();
        building = true;
        buildingKey = job.key;
      }
      Ready built;
      built.dynamics = job.key.dynamics;
      std::shared_ptr<RollReport> report(new (std::nothrow) RollReport());
      PianoRoll roll;
      PlayConfig rules = piano->playConfig();
      if (report != nullptr &&
          buildPianoRoll(*job.source, roll, piano->velocities(), job.key.dynamics, report.get(), &rules, job.edits.get()))
      {
        built.report = report;
        built.leadMs = roll.leadMs;
      }
      else
      {
        built.failed = true;
      }
      std::lock_guard<std::mutex> lock(mutex);
      ready = built;
      readyKey = job.key;
      haveReady = true;
    }
  }
}
