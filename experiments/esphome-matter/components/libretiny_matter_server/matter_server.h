#pragma once

#include "esphome/components/libretiny_matter_storage/matter_storage.h"
#include "esphome/core/component.h"
#include "platform.h"

namespace esphome::libretiny_matter_server {

class MatterServer : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void set_storage(libretiny_matter_storage::MatterStorage *storage) { this->storage_ = storage; }
  void set_passcode(uint32_t value) { this->config_.passcode = value; }
  void set_discriminator(uint16_t value) { this->config_.discriminator = value; }
  void set_commissioning_seconds(uint16_t value) { this->config_.commissioning_seconds = value; }
  bool get_on_off() const { return this->on_off_; }

 protected:
  bool start_();
  bool read_network_(LtmConfig &config);
  libretiny_matter_storage::MatterStorage *storage_{};
  LtmConfig config_{};
  uint32_t last_attempt_{};
  uint32_t last_status_{};
  uint32_t last_network_check_{};
  bool started_{};
  bool on_off_{};
};
}  // namespace esphome::libretiny_matter_server
