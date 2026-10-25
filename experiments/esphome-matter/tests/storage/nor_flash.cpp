#include "nor_flash.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

namespace test_nor {
// Test only the hardware boundary: erase to 0xff, program 1 -> 0, and retain
// a flash image between separate processes. Never mock the CHIP interface/DB.
std::array<uint8_t, 32768> flash;
static const fal_flash_dev device = {"test", 0, flash.size(), 4096, {}, 8};
static const fal_partition partition = {0, "kvs", "test", 0, flash.size(), 0};
bool fail_writes = false;
uint32_t fail_read_address = UINT32_MAX;
size_t fail_read_size = 0;
int write_count = 0;

#define CHECK(condition)                                          \
  do {                                                            \
    if (!(condition)) {                                           \
      std::cerr << __LINE__ << ": " << #condition << " failed\n"; \
      std::exit(1);                                               \
    }                                                             \
  } while (false)

static std::string image_path;
void persist_image() {
  if (image_path.empty())
    return;
  std::ofstream image(image_path, std::ios::binary | std::ios::trunc);
  image.write(reinterpret_cast<const char *>(flash.data()), flash.size());
  image.close();
  CHECK(image);
}
void open_image(const std::string &path) {
  image_path = path;
  flash.fill(0xff);
  std::ifstream image(path, std::ios::binary);
  if (image)
    CHECK(image.read(reinterpret_cast<char *>(flash.data()), flash.size()));
}
}  // namespace test_nor

using namespace test_nor;

extern "C" {
int fal_init() { return 1; }
const fal_flash_dev *fal_flash_device_find(const char *name) { return strcmp(name, "test") == 0 ? &device : nullptr; }
const fal_partition *fal_partition_find(const char *name) { return strcmp(name, "kvs") == 0 ? &partition : nullptr; }

int fal_partition_read(const fal_partition *, uint32_t addr, uint8_t *buf, size_t size) {
  CHECK(addr <= flash.size() && size <= flash.size() - addr);
  if (addr == fail_read_address && size == fail_read_size)
    return -1;
  memcpy(buf, flash.data() + addr, size);
  return static_cast<int>(size);
}

int fal_partition_write(const fal_partition *, uint32_t addr, const uint8_t *buf, size_t size) {
  CHECK(addr <= flash.size() && size <= flash.size() - addr);
  if (fail_writes)
    return -1;
  for (size_t i = 0; i < size; i++) {
    CHECK((flash[addr + i] & buf[i]) == buf[i]);
    flash[addr + i] = buf[i];
  }
  persist_image();
  ++write_count;
  return static_cast<int>(size);
}

int fal_partition_erase(const fal_partition *, uint32_t addr, size_t size) {
  CHECK(addr % 4096 == 0 && size % 4096 == 0);
  CHECK(addr <= flash.size() && size <= flash.size() - addr);
  if (fail_writes)
    return -1;
  std::fill_n(flash.data() + addr, size, 0xff);
  persist_image();
  return static_cast<int>(size);
}
}
