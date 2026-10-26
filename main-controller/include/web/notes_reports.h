#ifndef NOTES_REPORTS_H
#define NOTES_REPORTS_H

#include <memory>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "app/piano/roll_builder.h"

class PianoController;

// Builds the note view's report (every note as written and as played, see
// RollReport) away from the web server's task: on its own low-priority task
// on core 1, so a long piece (a second or two) neither holds up the server
// nor keeps core 0 so busy that its watchdog restarts the controller. The
// last report is kept, so asking again for the same piece and settings
// (several pages, or the note view and its editor) builds it once.
class NotesReports
{
public:
  struct Ready
  {
    std::shared_ptr<const RollReport> report; // null: none yet, or it couldn't be built
    uint32_t leadMs = 0;
    Dynamics dynamics;
    bool failed = false; // out of memory
  };

  bool begin(PianoController &piano);
  // The loaded piece's report as it would play now (as it's playing, while it
  // is): true with it in ready if it's built, else false, with its build
  // started (or under way). An earlier piece or settings' build is dropped.
  bool get(Ready &ready);

private:
  struct Key
  {
    uint32_t rollId = 0;
    const void *source = nullptr;
    Dynamics dynamics;
    const void *edits = nullptr;
    uint32_t configVersion = 0;
    bool operator==(const Key &other) const
    {
      return rollId == other.rollId && source == other.source && dynamics == other.dynamics && edits == other.edits &&
             configVersion == other.configVersion;
    }
  };
  struct Job
  {
    Key key;
    std::shared_ptr<const std::vector<MidiEvent>> source;
    std::shared_ptr<const NoteEdits> edits;
  };

  static void taskEntry(void *arg);
  void run();

  PianoController *piano = nullptr;
  TaskHandle_t task = nullptr;
  std::mutex mutex;
  bool haveReady = false;
  Key readyKey;
  Ready ready;
  bool building = false;
  Key buildingKey;
  bool haveWanted = false;
  Job wanted; // the next to build
};

#endif // NOTES_REPORTS_H
