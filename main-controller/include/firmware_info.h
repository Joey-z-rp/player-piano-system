#ifndef FIRMWARE_INFO_H
#define FIRMWARE_INFO_H

#include <stdint.h>

// What firmware is running (stamped by scripts/build_info.py), from which
// slot, and whether an update was rolled back to it.
//
// After an update over Wi-Fi, the new firmware is on trial: it's kept only
// once firmwareIsGood() is called (on Wi-Fi, with the web server up). If that
// hasn't happened within FIRMWARE_TRIAL_MS, or it crashes or restarts before,
// the controller goes back to the firmware it had. Firmware uploaded over USB
// isn't on trial.
static const uint32_t FIRMWARE_TRIAL_MS = 120000;

const char *firmwareVersion(); // e.g. "b15c185+changes": the commit, and whether there were uncommitted edits
const char *firmwareBuilt();   // "YYYY-MM-DD HH:MM", the build computer's time
const char *firmwareSlot();    // "app0" or "app1"
bool firmwareOnTrial();
// Whether the controller went back to this firmware because an update didn't
// pass its trial (until the next update).
bool firmwareRolledBack();

void firmwareIsGood();
// From loop(): restarts into the previous firmware once a trial runs out.
void checkFirmwareTrial();

#endif // FIRMWARE_INFO_H
