#include "flashdb_storage.h"

#include <algorithm>
#include <cstring>

namespace esphome::libretiny_matter_storage {

CHIP_ERROR FlashDBStorage::bind(fdb_kvdb_t db, ContextCheck context_check, void *context) {
  if (this->db_ != nullptr)
    return CHIP_ERROR_INCORRECT_STATE;
  // A 2 KiB value plus key/record headers fits in LibreTiny's 4 KiB sectors.
  if (db == nullptr || !db->parent.init_ok || db->parent.sec_size < 4096 || context_check == nullptr ||
      !context_check(context))
    return CHIP_ERROR_INCORRECT_STATE;
  this->db_ = db;
  this->context_check_ = context_check;
  this->context_ = context;
  return CHIP_NO_ERROR;
}

bool FlashDBStorage::ready_() const {
  // Check the task before touching the shared database, including after reset.
  return this->db_ != nullptr && this->context_check_(this->context_) && this->db_->parent.init_ok;
}

CHIP_ERROR FlashDBStorage::make_key_(const char *key, char *output) const {
  if (key == nullptr)
    return CHIP_ERROR_INVALID_ARGUMENT;
  const size_t length = strnlen(key, kKeyLengthMax + 1);
  if (length == 0 || length > kKeyLengthMax)
    return CHIP_ERROR_INVALID_ARGUMENT;
  memcpy(output, PREFIX, sizeof(PREFIX) - 1);
  memcpy(output + sizeof(PREFIX) - 1, key, length + 1);
  return CHIP_NO_ERROR;
}

CHIP_ERROR FlashDBStorage::storage_error_(fdb_err_t error) {
  if (error == FDB_NO_ERR)
    return CHIP_NO_ERROR;
  if (error == FDB_SAVED_FULL)
    return CHIP_ERROR_NO_MEMORY;
  return CHIP_ERROR_PERSISTED_STORAGE_FAILED;
}

CHIP_ERROR FlashDBStorage::SyncGetKeyValue(const char *key, void *buffer, uint16_t &size) {
  const uint16_t capacity = size;
  size = 0;
  if (!this->ready_())
    return CHIP_ERROR_INCORRECT_STATE;
  if (buffer == nullptr && capacity != 0)
    return CHIP_ERROR_INVALID_ARGUMENT;
  char name[KEY_BUFFER_SIZE];
  auto error = this->make_key_(key, name);
  if (error != CHIP_NO_ERROR)
    return error;

  struct fdb_kv kv{};
  if (fdb_kv_get_obj(this->db_, name, &kv) == nullptr)
    return CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
  if (!kv.crc_is_ok)
    return CHIP_ERROR_INTEGRITY_CHECK_FAILED;
  if (kv.value_len > MAX_VALUE_SIZE)
    return CHIP_ERROR_PERSISTED_STORAGE_FAILED;

  const auto read_size = std::min<uint16_t>(capacity, kv.value_len);
  if (read_size != 0) {
    struct fdb_blob blob{};
    fdb_blob_make(&blob, buffer, read_size);
    fdb_kv_to_blob(&kv, &blob);
    if (fdb_blob_read(&this->db_->parent, &blob) != read_size) {
      // Do not expose a partially read credential as a successful value.
      memset(buffer, 0, read_size);
      return CHIP_ERROR_PERSISTED_STORAGE_FAILED;
    }
  }
  size = read_size;
  return capacity < kv.value_len ? CHIP_ERROR_BUFFER_TOO_SMALL : CHIP_NO_ERROR;
}

CHIP_ERROR FlashDBStorage::SyncSetKeyValue(const char *key, const void *value, uint16_t size) {
  if (!this->ready_())
    return CHIP_ERROR_INCORRECT_STATE;
  if ((value == nullptr && size != 0) || size > MAX_VALUE_SIZE)
    return CHIP_ERROR_INVALID_ARGUMENT;
  char name[KEY_BUFFER_SIZE];
  auto error = this->make_key_(key, name);
  if (error != CHIP_NO_ERROR)
    return error;

  // In FlashDB a null buffer means delete, even for a zero-length blob.
  const uint8_t empty = 0;
  struct fdb_blob blob{};
  fdb_blob_make(&blob, size == 0 ? &empty : value, size);
  return storage_error_(fdb_kv_set_blob(this->db_, name, &blob));
}

CHIP_ERROR FlashDBStorage::SyncDeleteKeyValue(const char *key) {
  if (!this->ready_())
    return CHIP_ERROR_INCORRECT_STATE;
  char name[KEY_BUFFER_SIZE];
  auto error = this->make_key_(key, name);
  if (error != CHIP_NO_ERROR)
    return error;
  struct fdb_kv kv{};
  if (fdb_kv_get_obj(this->db_, name, &kv) == nullptr)
    return CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
  return storage_error_(fdb_kv_del(this->db_, name));
}

}  // namespace esphome::libretiny_matter_storage
