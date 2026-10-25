#include "matter_server.h"

#include <FreeRTOS.h>
#include <libretiny.h>
#include <lwip/netif.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include <drv_model_pub.h>
#include <irda_pub.h>
}

#include "esphome/components/network/util.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/log.h"

namespace {
const char *const TAG = "libretiny_matter_server";
enum class Operation : uint8_t { LOAD, STORE, REMOVE };
struct StorageRequest {
  Operation operation;
  uint16_t key;
  uint8_t *data;
  size_t size;
  int32_t result;
};
QueueHandle_t requests;
SemaphoreHandle_t completed;
TaskHandle_t worker;
bool worker_failed;
bool worker_on_off;
bool connected;
uint32_t poll_max_ms;
uint32_t minimum_stack_bytes;
LtmConfig network_config{};
uint32_t network_generation;

void run_server(void *) {
  bool active = false;
  uint32_t generation = 0;
  unsigned failures = 0;
  uint32_t active_since = 0;
  uint32_t last_stack_check = 0;
  for (;;) {
    bool online;
    uint32_t current_generation;
    LtmConfig config{};
    taskENTER_CRITICAL();
    online = connected;
    config = network_config;
    current_generation = network_generation;
    taskEXIT_CRITICAL();
    if (active && (!online || generation != current_generation)) {
      ltm_server_stop();
      ltm_udp_close();
      active = false;
      ESP_LOGI(TAG, "Network changed; Matter sessions closed, stored fabrics retained");
    }
    if (!online) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!active) {
      if (!ltm_udp_open(&config) || ltm_server_init(&config) != 0) {
        ltm_udp_close();
        ESP_LOGE(TAG, "Matter initialization failed");
        if (++failures >= 3)
          break;
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }
      generation = current_generation;
      active = true;
      active_since = esphome::millis();
    }
    const uint32_t start = esphome::millis();
    if (start - active_since >= 30000)
      failures = 0;
    const int result = ltm_server_poll();
#if INCLUDE_uxTaskGetStackHighWaterMark
    if (start - last_stack_check >= 30000) {
      last_stack_check = start;
      const uint32_t free_stack = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
      taskENTER_CRITICAL();
      minimum_stack_bytes = free_stack;
      taskEXIT_CRITICAL();
    }
#endif
    const uint32_t elapsed = esphome::millis() - start;
    taskENTER_CRITICAL();
    poll_max_ms = std::max(poll_max_ms, elapsed);
    taskEXIT_CRITICAL();
    if (result != 0) {
      ltm_server_stop();
      ltm_udp_close();
      active = false;
      if (++failures >= 3)
        break;
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
    vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
  }
  taskENTER_CRITICAL();
  worker_failed = true;
  taskEXIT_CRITICAL();
  vTaskDelete(nullptr);
}

int32_t storage_call(Operation operation, uint16_t key, uint8_t *data, size_t size) {
  StorageRequest request{operation, key, data, size, -1};
  auto *pointer = &request;
  if (xQueueSend(requests, &pointer, portMAX_DELAY) != pdTRUE) {
    return -1;
  }
  // The pointer remains valid until main acknowledges completion. A timeout must
  // never return while main could still access the worker's stack or buffer.
  while (xSemaphoreTake(completed, pdMS_TO_TICKS(1000)) != pdTRUE) {
    ESP_LOGW(TAG, "Waiting for main task to finish Matter storage operation");
  }
  return request.result;
}
}  // namespace

namespace esphome::libretiny_matter_server {
void MatterServer::setup() {
  if (this->storage_ == nullptr || this->storage_->is_failed()) {
    this->mark_failed();
    return;
  }
  requests = xQueueCreate(1, sizeof(StorageRequest *));
  completed = xSemaphoreCreateBinary();
  if (requests == nullptr || completed == nullptr) {
    ESP_LOGE(TAG, "Cannot allocate Matter storage synchronization objects");
    this->mark_failed();
  }
  this->last_attempt_ = millis() - 5000;
}

bool MatterServer::read_network_(LtmConfig &config) {
  // Use a connected STA interface and its real scope index for link-local IPv6.
  netif *iface = netif_default;
  if (iface == nullptr || !netif_is_up(iface) || !netif_is_link_up(iface)) {
    return false;
  }
  bool has_ipv6 = false;
  for (unsigned i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
    if (ip6_addr_isvalid(netif_ip6_addr_state(iface, i))) {
      memcpy(config.ipv6, netif_ip6_addr(iface, i)->addr, 16);
      has_ipv6 = true;
      break;
    }
  }
  if (!has_ipv6) {
    return false;
  }
  memcpy(config.ipv4, &netif_ip4_addr(iface)->addr, 4);
  config.interface = netif_get_index(iface);
  memcpy(config.mac, iface->hwaddr, 6);
  snprintf(reinterpret_cast<char *>(config.hostname), sizeof(config.hostname), "lt%02x%02x%02x%02x%02x%02x",
           iface->hwaddr[0], iface->hwaddr[1], iface->hwaddr[2], iface->hwaddr[3], iface->hwaddr[4], iface->hwaddr[5]);
  char ssid[wifi::SSID_BUFFER_SIZE];
  wifi::global_wifi_component->wifi_ssid_to(ssid);
  config.ssid_len = strnlen(ssid, sizeof(config.ssid));
  memset(config.ssid, 0, sizeof(config.ssid));
  memcpy(config.ssid, ssid, config.ssid_len);
  return true;
}

bool MatterServer::start_() {
  if (!this->read_network_(this->config_)) {
    ESP_LOGW(TAG, "Waiting for usable IPv6 address before starting Matter");
    return false;
  }
  taskENTER_CRITICAL();
  network_config = this->config_;
  ++network_generation;
  connected = true;
  taskEXIT_CRITICAL();
  constexpr uint32_t STACK_BYTES = 24 * 1024;
  if (lt_heap_get_free() < STACK_BYTES + 16 * 1024) {
    ESP_LOGE(TAG, "Insufficient free heap for Matter task: %u bytes", lt_heap_get_free());
    this->mark_failed();
    return false;
  }
  ESP_LOGI(TAG, "Starting Matter: heap=%u stack=%u interface=%u", lt_heap_get_free(), STACK_BYTES,
           this->config_.interface);
  if (xTaskCreate(run_server, "matter", STACK_BYTES / sizeof(StackType_t), nullptr, 1, &worker) != pdPASS) {
    ESP_LOGE(TAG, "Cannot allocate Matter task stack");
    this->mark_failed();
    return false;
  }
  return true;
}

void MatterServer::loop() {
  const bool is_connected = network::is_connected();
  bool failed;
  uint32_t slowest;
  uint32_t stack_free;
  taskENTER_CRITICAL();
  if (!is_connected)
    connected = false;
  failed = worker_failed;
  this->on_off_ = worker_on_off;
  slowest = poll_max_ms;
  stack_free = minimum_stack_bytes;
  taskEXIT_CRITICAL();
  if (failed) {
    ESP_LOGE(TAG, "Matter task stopped; ESPHome remains active");
    this->mark_failed();
    return;
  }
  StorageRequest *request{};
  if (xQueueReceive(requests, &request, 0) == pdTRUE) {
    auto *storage = this->storage_->get_delegate();
    char key[12];
    snprintf(key, sizeof(key), "rs/%04x", request->key);
    CHIP_ERROR error;
    uint16_t size = std::min<size_t>(request->size, UINT16_MAX);
    switch (request->operation) {
      case Operation::LOAD:
        error = storage->SyncGetKeyValue(key, request->data, size);
        request->result = error == CHIP_NO_ERROR ? size : -1;
        break;
      case Operation::STORE:
        error = storage->SyncSetKeyValue(key, request->data, size);
        request->result = error == CHIP_NO_ERROR ? 0 : -1;
        break;
      case Operation::REMOVE:
        error = storage->SyncDeleteKeyValue(key);
        request->result = error == CHIP_NO_ERROR ? 0 : -1;
        break;
    }
    if (error == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) {
      request->result = -2;
    } else if (error != CHIP_NO_ERROR) {
      ESP_LOGE(TAG, "Matter storage operation=%u key=%04x failed (error=%u)", static_cast<unsigned>(request->operation),
               request->key, error.AsInteger());
    } else {
      ESP_LOGD(TAG, "Matter storage operation=%u key=%04x bytes=%u", static_cast<unsigned>(request->operation),
               request->key, size);
    }
    xSemaphoreGive(completed);
  }
  const auto now = millis();
  if (!this->started_ && is_connected && now - this->last_attempt_ >= 5000) {
    this->last_attempt_ = now;
    this->started_ = this->start_();
  }
  if (this->started_ && is_connected && now - this->last_network_check_ >= 1000) {
    this->last_network_check_ = now;
    auto config = this->config_;
    const bool ready = this->read_network_(config);
    taskENTER_CRITICAL();
    connected = ready;
    if (ready && memcmp(&config, &network_config, sizeof(config)) != 0) {
      network_config = config;
      ++network_generation;
    }
    taskEXIT_CRITICAL();
  }
  if (this->started_ && now - this->last_status_ >= 30000) {
    this->last_status_ = now;
    ESP_LOGI(TAG, "Matter status: connected=%s on_off=%s heap=%u minimum_heap=%u slowest_poll_ms=%u stack_min_free=%u",
             YESNO(connected), YESNO(this->on_off_), lt_heap_get_free(), lt_heap_get_min_free(), slowest, stack_free);
  }
}

void MatterServer::dump_config() {
  ESP_LOGCONFIG(TAG, "Native Matter development server (test attestation credentials)");
  ESP_LOGCONFIG(TAG, "  Endpoint 1: virtual On/Off light; no GPIO access");
  ESP_LOGCONFIG(TAG, "  Commissioning: on-network, discriminator=%u, window=%u s", this->config_.discriminator,
                this->config_.commissioning_seconds);
}
}  // namespace esphome::libretiny_matter_server

extern "C" uint64_t ltm_platform_now_ms() {
  static uint32_t previous;
  static uint64_t elapsed;
  const uint32_t now = esphome::millis();
  elapsed += uint32_t(now - previous);
  previous = now;
  return elapsed;
}
extern "C" int32_t ltm_platform_random(uint8_t *data, size_t len) {
  // BDK 3.0.33's byte API fills at most four bytes; use the checked TRNG
  // device operation directly, four bytes at a time, on both SDK versions.
  static uint32_t previous;
  static unsigned repeats;
  static bool initialized;
  while (len != 0) {
    uint32_t word{};
    if (sddev_control(IRDA_DEV_NAME, TRNG_CMD_GET, &word) != 0) {
      return -1;
    }
    // Fail closed on a stuck peripheral. This is a continuity check, not a
    // claim of entropy certification. Three identical 32-bit outputs trigger it.
    repeats = initialized && word == previous ? repeats + 1 : 0;
    previous = word;
    initialized = true;
    if (repeats >= 2) {
      return -1;
    }
    const size_t count = std::min(len, sizeof(word));
    memcpy(data, &word, count);
    data += count;
    len -= count;
  }
  return 0;
}
extern "C" int32_t ltm_platform_load(uint16_t key, uint8_t *data, size_t capacity) {
  return storage_call(Operation::LOAD, key, data, capacity);
}
extern "C" int32_t ltm_platform_store(uint16_t key, const uint8_t *data, size_t len) {
  return storage_call(Operation::STORE, key, const_cast<uint8_t *>(data), len);
}
extern "C" int32_t ltm_platform_remove(uint16_t key) { return storage_call(Operation::REMOVE, key, nullptr, 0); }
extern "C" void ltm_platform_log(uint8_t level, const uint8_t *data, size_t len) {
  const int priority = level == 1   ? ESPHOME_LOG_LEVEL_ERROR
                       : level == 2 ? ESPHOME_LOG_LEVEL_WARN
                                    : ESPHOME_LOG_LEVEL_INFO;
  esphome::esp_log_printf_(priority, TAG, __LINE__, "%.*s", static_cast<int>(len),
                           reinterpret_cast<const char *>(data));
}
extern "C" void ltm_platform_switch(bool on) {
  taskENTER_CRITICAL();
  worker_on_off = on;
  taskEXIT_CRITICAL();
}
extern "C" bool ltm_platform_connected() {
  bool result;
  taskENTER_CRITICAL();
  result = connected;
  taskEXIT_CRITICAL();
  return result;
}
extern "C" void *ltm_platform_alloc(size_t size, size_t align) {
  align = std::max(align, sizeof(void *));
  if (size > SIZE_MAX - align - sizeof(void *)) {
    return nullptr;
  }
  void *base = pvPortMalloc(size + align - 1 + sizeof(void *));
  if (base == nullptr) {
    return nullptr;
  }
  const uintptr_t address = (reinterpret_cast<uintptr_t>(base) + sizeof(void *) + align - 1) & ~(align - 1);
  auto *result = reinterpret_cast<void **>(address);
  result[-1] = base;
  return result;
}
extern "C" void ltm_platform_free(void *pointer) { vPortFree(static_cast<void **>(pointer)[-1]); }
extern "C" void ltm_platform_enter_critical() { vPortEnterCritical(); }
extern "C" void ltm_platform_exit_critical() { vPortExitCritical(); }
extern "C" [[noreturn]] void ltm_platform_fatal() {
  taskENTER_CRITICAL();
  worker_failed = true;
  taskEXIT_CRITICAL();
  for (;;) {
    vTaskSuspend(nullptr);
  }
}
