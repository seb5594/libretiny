// POSIX adapter for exercising the exact firmware Rust library and UDP bridge.
#include <arpa/inet.h>
#include <sys/random.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "flashdb_storage.h"
#include "nor_flash.h"
#include "platform.h"

namespace {
std::filesystem::path directory;
std::recursive_mutex critical;
auto started = std::chrono::steady_clock::now();
volatile std::sig_atomic_t restart_requested;
fdb_kvdb database{};
esphome::libretiny_matter_storage::FlashDBStorage storage;
std::thread::id owner;

void format_key(uint16_t key, char *output, size_t size) { snprintf(output, size, "rs/%04x", key); }
}  // namespace

extern "C" uint64_t ltm_platform_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}
extern "C" int32_t ltm_platform_random(uint8_t *data, size_t len) {
  if (std::getenv("LTM_TEST_FAIL_ENTROPY") != nullptr)
    return -1;
  while (len != 0) {
    const auto count = getrandom(data, len, 0);
    if (count <= 0) {
      return -1;
    }
    data += count;
    len -= count;
  }
  return 0;
}
extern "C" int32_t ltm_platform_load(uint16_t key, uint8_t *data, size_t capacity) {
  char name[12];
  format_key(key, name, sizeof(name));
  uint16_t size = std::min<size_t>(capacity, UINT16_MAX);
  const auto error = storage.SyncGetKeyValue(name, data, size);
  if (error == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND)
    return -2;
  return error == CHIP_NO_ERROR ? size : -1;
}
extern "C" int32_t ltm_platform_store(uint16_t key, const uint8_t *data, size_t len) {
  if (len > storage.MAX_VALUE_SIZE)
    return -1;
  char name[12];
  format_key(key, name, sizeof(name));
  test_nor::fail_writes = std::getenv("LTM_TEST_FAIL_FABRIC_WRITE") != nullptr && key > 0 && key < 0x100;
  const auto error = storage.SyncSetKeyValue(name, data, len);
  test_nor::fail_writes = false;
  fprintf(stderr, "STORE key=%04x bytes=%zu error=%u\n", key, len, error.AsInteger());
  return error == CHIP_NO_ERROR ? 0 : -1;
}
extern "C" int32_t ltm_platform_remove(uint16_t key) {
  char name[12];
  format_key(key, name, sizeof(name));
  const auto error = storage.SyncDeleteKeyValue(name);
  return error == CHIP_NO_ERROR || error == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND ? 0 : -1;
}
extern "C" void ltm_platform_log(uint8_t level, const uint8_t *data, size_t len) {
  fprintf(stderr, "[%llu][%u] %.*s\n", static_cast<unsigned long long>(ltm_platform_now_ms()), level,
          static_cast<int>(len), reinterpret_cast<const char *>(data));
}
extern "C" void ltm_platform_switch(bool on) { fprintf(stderr, "SWITCH %s\n", on ? "ON" : "OFF"); }
extern "C" bool ltm_platform_connected() { return true; }
extern "C" void *ltm_platform_alloc(size_t size, size_t align) {
  void *pointer{};
  return posix_memalign(&pointer, std::max(align, sizeof(void *)), size) == 0 ? pointer : nullptr;
}
extern "C" void ltm_platform_free(void *pointer) { free(pointer); }
extern "C" void ltm_platform_enter_critical() { critical.lock(); }
extern "C" void ltm_platform_exit_critical() { critical.unlock(); }
extern "C" [[noreturn]] void ltm_platform_fatal() { std::abort(); }
extern "C" void rust_eh_personality() { std::abort(); }

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: matter-host STORAGE_DIRECTORY IPV4_ADDRESS\n");
    return 2;
  }
  directory = argv[1];
  std::filesystem::create_directories(directory);
  test_nor::open_image((directory / "flash.bin").string());
  if (fdb_kvdb_init(&database, "esphome", "kvs", nullptr, nullptr) != FDB_NO_ERR)
    return 1;
  owner = std::this_thread::get_id();
  auto is_owner = [](void *context) { return *static_cast<std::thread::id *>(context) == std::this_thread::get_id(); };
  if (storage.bind(&database, is_owner, &owner) != CHIP_NO_ERROR)
    return 1;
  LtmConfig config{};
  config.passcode = 20202021;
  config.discriminator = 3840;
  config.commissioning_seconds = 300;
  memcpy(config.hostname, "lt-host-test", 13);
  memcpy(config.ssid, "host-test", 9);
  config.ssid_len = 9;
  inet_pton(AF_INET, argv[2], config.ipv4);
  inet_pton(AF_INET6, "::1", config.ipv6);
  if (ltm_server_poll() != -1 || ltm_server_init(nullptr) != -1)
    return 1;
  for (const auto code : {0U, 11111111U, 12345678U, 87654321U, 99999999U}) {
    auto invalid = config;
    invalid.passcode = code;
    if (ltm_server_init(&invalid) != -1)
      return 1;
  }
  std::signal(SIGUSR1, [](int) { restart_requested = 1; });
  if (!ltm_udp_open(&config) || ltm_server_init(&config) != 0) {
    perror("server initialization");
    return 1;
  }
  for (;;) {
    if (restart_requested) {
      restart_requested = 0;
      ltm_server_stop();
      ltm_udp_close();
      if (!ltm_udp_open(&config) || ltm_server_init(&config) != 0) {
        return 1;
      }
      fprintf(stderr, "WARM_RESTART\n");
    }
    if (ltm_server_poll() != 0) {
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
