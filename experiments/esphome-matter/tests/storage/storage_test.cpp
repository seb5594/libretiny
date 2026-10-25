extern "C" {
#include <flashdb.h>
}

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "flashdb_storage.h"

using esphome::libretiny_matter_storage::FlashDBStorage;

#include "nor_flash.h"

using namespace test_nor;

#define CHECK(condition)                                          \
  do {                                                            \
    if (!(condition)) {                                           \
      std::cerr << __LINE__ << ": " << #condition << " failed\n"; \
      std::exit(1);                                               \
    }                                                             \
  } while (false)

static bool is_owner(void *context) { return *static_cast<std::thread::id *>(context) == std::this_thread::get_id(); }

static void expect_value(chip::PersistentStorageDelegate &storage, const char *key, const std::string &expected) {
  std::array<char, FlashDBStorage::MAX_VALUE_SIZE> data{};
  uint16_t size = data.size();
  CHECK(storage.SyncGetKeyValue(key, data.data(), size) == CHIP_NO_ERROR);
  CHECK(size == expected.size());
  CHECK(memcmp(data.data(), expected.data(), size) == 0);
}

static void contract(FlashDBStorage &storage, fdb_kvdb &db) {
  // Call through the real upstream interface, including its existence helper.
  chip::PersistentStorageDelegate &delegate = storage;
  const std::string binary("\0\xff\x07\0\x08", 5);
  CHECK(delegate.SyncSetKeyValue("f/1/n", binary.data(), binary.size()) == CHIP_NO_ERROR);
  expect_value(delegate, "f/1/n", binary);
  CHECK(delegate.SyncDoesKeyExist("f/1/n"));
  uint16_t size = 0;
  CHECK(delegate.SyncGetKeyValue("f/1/n", nullptr, size) == CHIP_ERROR_BUFFER_TOO_SMALL);
  CHECK(size == 0);
  char short_buffer[3] = {};
  size = sizeof(short_buffer);
  CHECK(delegate.SyncGetKeyValue("f/1/n", short_buffer, size) == CHIP_ERROR_BUFFER_TOO_SMALL);
  CHECK(size == sizeof(short_buffer) && memcmp(short_buffer, binary.data(), size) == 0);
  CHECK(delegate.SyncSetKeyValue("empty", nullptr, 0) == CHIP_NO_ERROR);
  CHECK(delegate.SyncDoesKeyExist("empty"));
  size = 0;
  CHECK(delegate.SyncGetKeyValue("empty", nullptr, size) == CHIP_NO_ERROR && size == 0);
  CHECK(delegate.SyncDeleteKeyValue("empty") == CHIP_NO_ERROR);
  CHECK(!delegate.SyncDoesKeyExist("empty"));
  CHECK(delegate.SyncDeleteKeyValue("empty") == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
  CHECK(delegate.SyncSetKeyValue("replace", "value", 5) == CHIP_NO_ERROR);
  CHECK(delegate.SyncSetKeyValue("replace", nullptr, 0) == CHIP_NO_ERROR);
  expect_value(delegate, "replace", "");
  size = sizeof(short_buffer);
  CHECK(delegate.SyncGetKeyValue("absent", short_buffer, size) == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND);
  CHECK(size == 0);

  const std::string max_key(chip::PersistentStorageDelegate::kKeyLengthMax, 'k');
  CHECK(delegate.SyncSetKeyValue(max_key.c_str(), "x", 1) == CHIP_NO_ERROR);
  expect_value(delegate, max_key.c_str(), "x");
  for (const char *invalid : {static_cast<const char *>(nullptr), "", "012345678901234567890123456789012"}) {
    CHECK(delegate.SyncSetKeyValue(invalid, "x", 1) == CHIP_ERROR_INVALID_ARGUMENT);
    size = sizeof(short_buffer);
    CHECK(delegate.SyncGetKeyValue(invalid, short_buffer, size) == CHIP_ERROR_INVALID_ARGUMENT && size == 0);
    CHECK(delegate.SyncDeleteKeyValue(invalid) == CHIP_ERROR_INVALID_ARGUMENT);
  }
  CHECK(delegate.SyncSetKeyValue("bad", nullptr, 1) == CHIP_ERROR_INVALID_ARGUMENT);
  size = 1;
  CHECK(delegate.SyncGetKeyValue("f/1/n", nullptr, size) == CHIP_ERROR_INVALID_ARGUMENT && size == 0);
  const std::string large(FlashDBStorage::MAX_VALUE_SIZE, 'v');
  CHECK(delegate.SyncSetKeyValue("large", large.data(), large.size()) == CHIP_NO_ERROR);
  expect_value(delegate, "large", large);
  CHECK(delegate.SyncSetKeyValue("large", large.data(), large.size() + 1) == CHIP_ERROR_INVALID_ARGUMENT);
  expect_value(delegate, "large", large);

  // Existing numeric ESPHome preference keys must survive Matter writes/deletes.
  fdb_blob blob{};
  CHECK(fdb_kv_set_blob(&db, "1234", fdb_blob_make(&blob, "pref", 4)) == FDB_NO_ERR);
  CHECK(delegate.SyncSetKeyValue("1234", "matter", 6) == CHIP_NO_ERROR);
  CHECK(delegate.SyncDeleteKeyValue("1234") == CHIP_NO_ERROR);
  char pref[4] = {};
  CHECK(fdb_kv_get_blob(&db, "1234", fdb_blob_make(&blob, pref, sizeof(pref))) == 4);
  CHECK(memcmp(pref, "pref", 4) == 0);

  CHECK(delegate.SyncSetKeyValue("f/1/n", "updated", 7) == CHIP_NO_ERROR);
  expect_value(delegate, "f/1/n", "updated");
  std::thread other([&delegate]() {
    uint16_t length = 0;
    CHECK(delegate.SyncGetKeyValue("f/1/n", nullptr, length) == CHIP_ERROR_INCORRECT_STATE);
    CHECK(delegate.SyncSetKeyValue("wrong-task", "x", 1) == CHIP_ERROR_INCORRECT_STATE);
    CHECK(delegate.SyncDeleteKeyValue("f/1/n") == CHIP_ERROR_INCORRECT_STATE);
  });
  other.join();
  expect_value(delegate, "f/1/n", "updated");
}

static void errors(FlashDBStorage &storage, fdb_kvdb &db) {
  CHECK(storage.SyncSetKeyValue("keep", "old", 3) == CHIP_NO_ERROR);
  fail_writes = true;
  CHECK(storage.SyncSetKeyValue("new", "value", 5) == CHIP_ERROR_PERSISTED_STORAGE_FAILED);
  CHECK(storage.SyncSetKeyValue("keep", "changed", 7) == CHIP_ERROR_PERSISTED_STORAGE_FAILED);
  CHECK(storage.SyncDeleteKeyValue("keep") == CHIP_ERROR_PERSISTED_STORAGE_FAILED);
  fail_writes = false;
  CHECK(!storage.SyncDoesKeyExist("new"));
  expect_value(storage, "keep", "old");

  fdb_kv kv{};
  CHECK(fdb_kv_get_obj(&db, "matter/keep", &kv) != nullptr);
  fail_read_address = kv.addr.value;
  fail_read_size = 2;
  char buffer[2] = {'x', 'x'};
  uint16_t size = sizeof(buffer);
  CHECK(storage.SyncGetKeyValue("keep", buffer, size) == CHIP_ERROR_PERSISTED_STORAGE_FAILED);
  CHECK(size == 0 && buffer[0] == 0 && buffer[1] == 0);
  fail_read_address = UINT32_MAX;
  expect_value(storage, "keep", "old");

  // An entry still present in FlashDB's lookup cache must not bypass its CRC.
  CHECK(storage.SyncSetKeyValue("corrupt", "original", 8) == CHIP_NO_ERROR);
  CHECK(fdb_kv_get_obj(&db, "matter/corrupt", &kv) != nullptr);
  flash[kv.addr.value] ^= 1;
  size = sizeof(buffer);
  CHECK(storage.SyncGetKeyValue("corrupt", buffer, size) == CHIP_ERROR_INTEGRITY_CHECK_FAILED);
  CHECK(size == 0);
}

static void full(FlashDBStorage &storage) {
  const std::string value(FlashDBStorage::MAX_VALUE_SIZE, 'v');
  bool filled = false;
  for (int i = 0; i < 64; i++) {
    const auto key = "item/" + std::to_string(i);
    const auto result = storage.SyncSetKeyValue(key.c_str(), value.data(), value.size());
    if (result == CHIP_ERROR_NO_MEMORY) {
      filled = true;
      break;
    }
    CHECK(result == CHIP_NO_ERROR);
  }
  CHECK(filled);
  expect_value(storage, "item/0", value);
  CHECK(storage.SyncDeleteKeyValue("item/0") == CHIP_NO_ERROR);
  CHECK(storage.SyncSetKeyValue("reclaimed", value.data(), value.size()) == CHIP_NO_ERROR);
  expect_value(storage, "reclaimed", value);
}

int main(int argc, char **argv) {
  CHECK(argc == 3);
  const std::string mode = argv[1];
  flash.fill(0xff);
  if (mode == "reload") {
    std::ifstream image(argv[2], std::ios::binary);
    CHECK(image.read(reinterpret_cast<char *>(flash.data()), flash.size()));
  }
  fdb_kvdb db{};
  FlashDBStorage storage;
  auto owner = std::this_thread::get_id();
  CHECK(storage.SyncSetKeyValue("unbound", "x", 1) == CHIP_ERROR_INCORRECT_STATE);
  CHECK(storage.bind(&db, is_owner, &owner) == CHIP_ERROR_INCORRECT_STATE);
  CHECK(fdb_kvdb_init(&db, "esphome", "kvs", nullptr, nullptr) == FDB_NO_ERR);
  CHECK(storage.bind(&db, nullptr, nullptr) == CHIP_ERROR_INCORRECT_STATE);
  CHECK(storage.bind(&db, is_owner, &owner) == CHIP_NO_ERROR);
  CHECK(storage.bind(&db, is_owner, &owner) == CHIP_ERROR_INCORRECT_STATE);

  if (mode == "contract")
    contract(storage, db);
  else if (mode == "errors")
    errors(storage, db);
  else if (mode == "full")
    full(storage);
  else if (mode == "save") {
    CHECK(storage.SyncSetKeyValue("reboot", "persisted", 9) == CHIP_NO_ERROR);
    CHECK(storage.SyncSetKeyValue("empty", nullptr, 0) == CHIP_NO_ERROR);
    CHECK(storage.SyncSetKeyValue("deleted", "old", 3) == CHIP_NO_ERROR);
    CHECK(storage.SyncDeleteKeyValue("deleted") == CHIP_NO_ERROR);
    std::ofstream image(argv[2], std::ios::binary);
    CHECK(image.write(reinterpret_cast<const char *>(flash.data()), flash.size()));
  } else if (mode == "reload") {
    const int before = write_count;
    expect_value(storage, "reboot", "persisted");
    CHECK(storage.SyncDoesKeyExist("empty"));
    CHECK(!storage.SyncDoesKeyExist("deleted"));
    CHECK(before == write_count);
  } else {
    CHECK(false);
  }

  CHECK(fdb_kvdb_deinit(&db) == FDB_NO_ERR);
  CHECK(storage.SyncSetKeyValue("closed", "x", 1) == CHIP_ERROR_INCORRECT_STATE);
  std::cout << mode << " passed\n";
  return 0;
}
