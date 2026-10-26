#include "crash_report.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

static const char *startReason = "unknown";
static CrashInfo crash;

static const char *reasonName(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "power";
  case ESP_RST_SW:
    return "software";
  case ESP_RST_PANIC:
    return "crash";
  case ESP_RST_INT_WDT:
  case ESP_RST_TASK_WDT:
  case ESP_RST_WDT:
    return "watchdog";
  case ESP_RST_BROWNOUT:
    return "brownout";
  case ESP_RST_DEEPSLEEP:
    return "deep sleep";
  case ESP_RST_EXT:
    return "external";
  default:
    return "unknown";
  }
}

const char *exceptionName(uint32_t cause)
{
  switch (cause)
  {
  case 0:
    return "IllegalInstruction";
  case 2:
    return "InstructionFetchError";
  case 3:
    return "LoadStoreError";
  case 6:
    return "IntegerDivideByZero";
  case 9:
    return "LoadStoreAlignment";
  case 20:
    return "InstFetchProhibited";
  case 28:
    return "LoadProhibited";
  case 29:
    return "StoreProhibited";
  default:
    return "";
  }
}

void beginCrashReport()
{
  esp_reset_reason_t reason = esp_reset_reason();
  startReason = reasonName(reason);
  Preferences saved;
  bool crashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
                 reason == ESP_RST_WDT || reason == ESP_RST_BROWNOUT;
  if (saved.begin("crash", false))
  {
    if (crashed)
    {
      saved.putString("reason", startReason);
    }
    crash.reason = saved.getString("reason", "").c_str();
    saved.end();
  }
  Serial.printf("Started after: %s\n", startReason);

  size_t address = 0, size = 0;
  if (esp_core_dump_image_get(&address, &size) != ESP_OK || esp_core_dump_image_check() != ESP_OK)
  {
    return;
  }
  esp_core_dump_summary_t *summary = static_cast<esp_core_dump_summary_t *>(malloc(sizeof(esp_core_dump_summary_t)));
  if (summary == nullptr)
  {
    return;
  }
  if (esp_core_dump_get_summary(summary) == ESP_OK)
  {
    crash.present = true;
    crash.bytes = size;
    crash.task = std::string(summary->exc_task, strnlen(summary->exc_task, sizeof(summary->exc_task)));
    crash.pc = summary->exc_pc;
    crash.cause = summary->ex_info.exc_cause;
    crash.address = summary->ex_info.exc_vaddr;
    for (uint32_t i = 0; i < summary->exc_bt_info.depth && i < 16; i++)
    {
      crash.backtrace.push_back(summary->exc_bt_info.bt[i]);
    }
    crash.backtraceCorrupted = summary->exc_bt_info.corrupted;
    crash.firmwareSha = std::string(reinterpret_cast<const char *>(summary->app_elf_sha256),
                                    strnlen(reinterpret_cast<const char *>(summary->app_elf_sha256), APP_ELF_SHA256_SZ));
    char running[APP_ELF_SHA256_SZ] = {};
    esp_ota_get_app_elf_sha256(running, sizeof(running));
    crash.thisFirmware = crash.firmwareSha == running;
    Serial.printf("Last crash recorded: task %s at 0x%08x (%s), firmware %s\n", crash.task.c_str(), crash.pc,
                  exceptionName(crash.cause), crash.firmwareSha.c_str());
  }
  free(summary);
}

const char *restartReason()
{
  return startReason;
}

const CrashInfo &lastCrash()
{
  return crash;
}

bool crashImage(size_t &address, size_t &size)
{
  return crash.present && esp_core_dump_image_get(&address, &size) == ESP_OK;
}

bool clearCrash()
{
  if (esp_core_dump_image_erase() != ESP_OK)
  {
    return false;
  }
  crash = CrashInfo();
  Preferences saved;
  if (saved.begin("crash", false))
  {
    saved.remove("reason");
    saved.end();
  }
  return true;
}
