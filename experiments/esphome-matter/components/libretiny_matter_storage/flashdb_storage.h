#pragma once

#include <flashdb.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>

namespace esphome::libretiny_matter_storage {

// The database remains owned by ESPHome. All access must run on its main task;
// FlashDB's per-operation locks alone would not protect lookup + blob read.
class FlashDBStorage final : public chip::PersistentStorageDelegate {
 public:
  using ContextCheck = bool (*)(void *);
  static constexpr uint16_t MAX_VALUE_SIZE = 2048;

  CHIP_ERROR bind(fdb_kvdb_t db, ContextCheck context_check, void *context);
  CHIP_ERROR SyncGetKeyValue(const char *key, void *buffer, uint16_t &size) override;
  CHIP_ERROR SyncSetKeyValue(const char *key, const void *value, uint16_t size) override;
  CHIP_ERROR SyncDeleteKeyValue(const char *key) override;

 private:
  static constexpr char PREFIX[] = "matter/";
  static constexpr size_t KEY_BUFFER_SIZE = sizeof(PREFIX) + kKeyLengthMax;
  static_assert(KEY_BUFFER_SIZE <= FDB_KV_NAME_MAX, "FlashDB key limit cannot hold Matter keys");

  CHIP_ERROR make_key_(const char *key, char *output) const;
  bool ready_() const;
  static CHIP_ERROR storage_error_(fdb_err_t error);

  fdb_kvdb_t db_{nullptr};
  ContextCheck context_check_{nullptr};
  void *context_{nullptr};
};

}  // namespace esphome::libretiny_matter_storage
