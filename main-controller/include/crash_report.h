#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// Why the controller last restarted, and the last crash it recorded. A crash
// (or a watchdog) saves a core dump to the "coredump" partition, which stays
// until the next crash replaces it or it's cleared: through restarts and
// updates. Its summary says which task crashed, how and where (program
// addresses), and which firmware it was (its ELF's SHA-256): tools/crash_report.py
// matches that to a saved build (firmware-builds/) and turns the addresses
// into code lines.
struct CrashInfo
{
  bool present = false;
  std::string task;         // e.g. "async_tcp"
  uint32_t pc = 0;          // where it stopped
  uint32_t cause = 0;       // Xtensa exception cause
  uint32_t address = 0;     // the address it faulted on, for a load or store
  std::vector<uint32_t> backtrace; // program addresses, innermost first
  bool backtraceCorrupted = false;
  std::string firmwareSha;  // the crashed firmware's ELF SHA-256, first 16 hex digits
  bool thisFirmware = false; // the same build as is running now
  size_t bytes = 0;         // the core dump's size
  std::string reason;       // the restart it caused (if known): see restartReason
};

// Call once at start: notes why this start happened and reads the crash
// record. A start after a crash keeps its reason (saved in NVS), so it's
// still known after later restarts.
void beginCrashReport();
// This start's reason: "power", "software" (a restart or update), "crash",
// "watchdog" (a task or interrupt held up too long), "brownout" (the supply
// dipped), "deep sleep", "external" or "unknown".
const char *restartReason();
const CrashInfo &lastCrash();
// Where the core dump is in flash, for downloading it; false if there's none.
bool crashImage(size_t &address, size_t &size);
// Forgets the recorded crash.
bool clearCrash();
// "LoadProhibited" and so on, for an Xtensa exception cause.
const char *exceptionName(uint32_t cause);

#endif // CRASH_REPORT_H
