#pragma once

#include "esphome/core/component.h"
#include "flashdb_storage.h"

namespace esphome::libretiny_matter_storage {

class MatterStorage : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  chip::PersistentStorageDelegate *get_delegate() { return &this->storage_; }

 private:
  FlashDBStorage storage_;
};

}  // namespace esphome::libretiny_matter_storage
