#include <array>
#include <cassert>
#include <iostream>

#include "matter_server.h"
#include "runtime_mocks.h"

extern "C" int32_t ltm_server_init(const LtmConfig *) { return 0; }
extern "C" int32_t ltm_server_poll() { return 0; }
extern "C" void ltm_server_stop() {}
extern "C" bool ltm_udp_open(const LtmConfig *) { return true; }
extern "C" void ltm_udp_close() {}
extern "C" bool ltm_platform_connected();
extern "C" int32_t ltm_platform_random(uint8_t *, size_t);
extern "C" void *ltm_platform_alloc(size_t, size_t);
extern "C" void ltm_platform_free(void *);
static_assert(sizeof(LtmConfig) == 92);
static_assert(sizeof(LtmPeer) == 24);

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  if (scenario == "random") {
    std::array<uint8_t, 33> bytes;
    bytes.fill(0xaa);
    assert(ltm_platform_random(bytes.data() + 1, 31) == 0);
    assert(bytes.front() == 0xaa && bytes.back() == 0xaa);
    assert(random_calls == 8);
    assert(bytes[1] == 1 && bytes[29] == 8);
    random_fails = true;
    assert(ltm_platform_random(bytes.data(), 1) == -1);
    random_fails = false;
    random_stuck = true;
    assert(ltm_platform_random(bytes.data(), 12) == -1);
  } else if (scenario == "allocator") {
    for (size_t align : {1, 4, 8, 16, 64}) {
      void *ptr = ltm_platform_alloc(123, align);
      assert(ptr && reinterpret_cast<uintptr_t>(ptr) % align == 0);
      memset(ptr, 0x55, 123);
      ltm_platform_free(ptr);
    }
    assert(ltm_platform_alloc(SIZE_MAX, 8) == nullptr);
  } else {
    esphome::libretiny_matter_storage::MatterStorage storage;
    esphome::libretiny_matter_server::MatterServer server;
    server.set_storage(&storage);
    server.setup();
    server.loop();
    assert(tasks_created == 0);  // No Wi-Fi yet.
    online = true;
    fake_netif.valid6 = false;
    server.loop();
    assert(tasks_created == 0);  // Wi-Fi without IPv6 must not start Matter.
    fake_netif.valid6 = true;
    clock_ms += 5000;
    if (scenario == "low_heap")
      free_heap = 10000;
    if (scenario == "task_failure")
      create_task_ok = false;
    server.loop();
    if (scenario == "low_heap") {
      assert(server.is_failed() && tasks_created == 0);
    } else if (scenario == "task_failure") {
      assert(server.is_failed() && tasks_created == 1);
    } else {
      assert(scenario == "network");
      assert(tasks_created == 1 && stack_words * sizeof(StackType_t) == 24576);
      assert(ltm_platform_connected());
      online = false;
      server.loop();
      assert(!ltm_platform_connected());
      online = true;
      clock_ms += 1000;
      server.loop();
      assert(ltm_platform_connected() && tasks_created == 1);
      fake_netif.valid6 = false;
      clock_ms += 1000;
      server.loop();
      assert(!ltm_platform_connected());
      fake_netif.valid6 = true;
      clock_ms += 1000;
      server.loop();
      assert(ltm_platform_connected());
    }
  }
  std::cout << "PASS " << scenario << '\n';
}
