#pragma once

#include "esphome/core/component.h"

namespace esphome::libretiny_matter_protocol {

class MatterProtocolTest : public Component {
 public:
  void setup() override;
  void dump_config() override;
  void run_tests();
  void set_run_on_boot(bool enabled) { this->run_on_boot_ = enabled; }
  float get_setup_priority() const override { return setup_priority::LATE; }

 private:
  bool run_on_boot_{true};
};

}  // namespace esphome::libretiny_matter_protocol
