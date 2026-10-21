#include "firmware_info.h"
#include "firmware_version.h"
#include <Arduino.h>
#include <esp_ota_ops.h>

// The Arduino core would otherwise mark a new firmware good as soon as it
// starts: on trial, it's left to firmwareIsGood(). C linkage, as the core
// declares it (esp32-hal-misc.c): a C++ definition wouldn't replace its weak
// one, and every update would be kept at once.
extern "C" bool verifyRollbackLater()
{
  return true;
}

const char *firmwareVersion()
{
  return FIRMWARE_VERSION;
}

const char *firmwareBuilt()
{
  return FIRMWARE_BUILT;
}

const char *firmwareSlot()
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  return running != nullptr ? running->label : "?";
}

bool firmwareOnTrial()
{
  esp_ota_img_states_t state;
  const esp_partition_t *running = esp_ota_get_running_partition();
  return running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool firmwareRolledBack()
{
  return esp_ota_get_last_invalid_partition() != nullptr;
}

void firmwareIsGood()
{
  if (firmwareOnTrial())
  {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("Firmware update kept: it's on Wi-Fi with the web server up");
  }
}

void checkFirmwareTrial()
{
  static bool checked = false;
  if (checked || millis() < FIRMWARE_TRIAL_MS)
  {
    return;
  }
  checked = true;
  if (firmwareOnTrial())
  {
    Serial.println("Firmware update didn't get online in time: going back to the previous firmware");
    delay(200);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}
