#include "matter_storage.h"

#include <FreeRTOS.h>
#include <task.h>

#include "esphome/components/libretiny/preferences.h"
#include "esphome/core/log.h"

namespace esphome::libretiny_matter_storage {

static const char *const TAG = "libretiny_matter_storage";

void MatterStorage::setup() {
  auto *preferences = libretiny::get_preferences();
  auto is_owner_task = [](void *owner) { return xTaskGetCurrentTaskHandle() == owner; };
  auto error = this->storage_.bind(&preferences->db, is_owner_task, xTaskGetCurrentTaskHandle());
  if (error != CHIP_NO_ERROR) {
    ESP_LOGE(TAG, "Cannot attach Matter storage to ESPHome preferences (error=%" PRIu32 ")", error.AsInteger());
    this->mark_failed();
  }
}

void MatterStorage::dump_config() {
  ESP_LOGCONFIG(TAG, "Matter persistent storage (experimental):");
  ESP_LOGCONFIG(TAG, "  Shared FlashDB / prefix: matter/ / maximum value: %u bytes", FlashDBStorage::MAX_VALUE_SIZE);
  ESP_LOGCONFIG(TAG, "  ESPHome main task only; no Matter server or commissioning yet");
}

}  // namespace esphome::libretiny_matter_storage
