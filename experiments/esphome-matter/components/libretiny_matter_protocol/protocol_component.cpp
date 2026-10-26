#include "protocol_component.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "protocol_codec.h"

namespace esphome::libretiny_matter_protocol {

static const char *const TAG = "libretiny_matter_protocol";

void MatterProtocolTest::setup() {
  if (this->run_on_boot_)
    this->run_tests();
}

void MatterProtocolTest::dump_config() { ESP_LOGCONFIG(TAG, "Native Matter codec tests; no Matter server or pairing"); }

void MatterProtocolTest::run_tests() {
  const uint32_t started = millis();
  const auto result = run_protocol_tests();
  ESP_LOGI(TAG, "Matter codec tests: passed=%u failed=%u duration=%" PRIu32 " ms", result.passed, result.failed,
           millis() - started);
  if (result.failed != 0) {
    ESP_LOGE(TAG, "First failing test: %s", result.first_failure);
    this->status_set_warning();
  } else {
    this->status_clear_warning();
  }
}

}  // namespace esphome::libretiny_matter_protocol
