#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

using TaskHandle_t = void *;
using QueueHandle_t = void *;
using SemaphoreHandle_t = void *;
using StackType_t = uint32_t;
constexpr int pdTRUE = 1;
constexpr int pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(x) (x)
#define taskENTER_CRITICAL() do {
#define taskEXIT_CRITICAL() \
  }                         \
  while (0)
#define INCLUDE_uxTaskGetStackHighWaterMark 1
inline unsigned tasks_created;
inline unsigned stack_words;
inline void (*worker_entry)(void *);
inline unsigned tasks_deleted;
inline bool create_task_ok = true;
inline uint32_t clock_ms = 10000;
inline bool online;
inline uint32_t free_heap = 100000;
inline bool random_stuck;
inline bool random_fails;
inline unsigned random_calls;
inline QueueHandle_t xQueueCreate(unsigned, size_t) { return reinterpret_cast<void *>(1); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return reinterpret_cast<void *>(1); }
inline int xQueueReceive(void *, void *, unsigned) { return 0; }
inline int xQueueSend(void *, void *, uint32_t) { return pdTRUE; }
inline int xSemaphoreTake(void *, uint32_t) { return pdTRUE; }
inline void xSemaphoreGive(void *) {}
inline void vTaskDelay(uint32_t) {}
inline void vTaskDelete(void *) { tasks_deleted++; }
inline void vTaskSuspend(void *) { std::abort(); }
inline void vPortEnterCritical() {}
inline void vPortExitCritical() {}
inline void *pvPortMalloc(size_t bytes) { return std::malloc(bytes); }
inline void vPortFree(void *ptr) { std::free(ptr); }
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 1000; }
inline int xTaskCreate(void (*entry)(void *), const char *, unsigned words, void *, unsigned, TaskHandle_t *) {
  tasks_created++;
  worker_entry = entry;
  stack_words = words;
  return create_task_ok ? pdPASS : 0;
}
inline uint32_t lt_heap_get_free() { return free_heap; }
inline uint32_t lt_heap_get_min_free() { return free_heap; }
#define IRDA_DEV_NAME "trng"
#define TRNG_CMD_GET 1
extern "C" inline int sddev_control(const char *, int, uint32_t *value) {
  *value = random_stuck ? 42 : ++random_calls;
  return random_fails ? -1 : 0;
}
struct ip4_addr_t {
  uint32_t addr;
};
struct ip6_addr_t {
  uint32_t addr[4];
};
struct netif {
  bool up = true;
  bool link = true;
  bool valid6 = true;
  ip4_addr_t ipv4{{}};
  ip6_addr_t ipv6{{0xfe80, 0, 0, 1}};
  uint8_t hwaddr[6]{0, 1, 2, 3, 4, 5};
};
inline netif fake_netif;
inline netif *netif_default = &fake_netif;
constexpr unsigned LWIP_IPV6_NUM_ADDRESSES = 1;
inline bool netif_is_up(netif *n) { return n->up; }
inline bool netif_is_link_up(netif *n) { return n->link; }
inline bool netif_ip6_addr_state(netif *n, unsigned) { return n->valid6; }
inline bool ip6_addr_isvalid(bool state) { return state; }
inline ip6_addr_t *netif_ip6_addr(netif *n, unsigned) { return &n->ipv6; }
inline ip4_addr_t *netif_ip4_addr(netif *n) { return &n->ipv4; }
inline unsigned netif_get_index(netif *) { return 1; }
struct CHIP_ERROR {
  unsigned value;
  unsigned AsInteger() const { return value; }
  bool operator==(CHIP_ERROR other) const { return value == other.value; }
  bool operator!=(CHIP_ERROR other) const { return value != other.value; }
};
constexpr CHIP_ERROR CHIP_NO_ERROR{0};
constexpr CHIP_ERROR CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND{1};
namespace esphome {
namespace setup_priority {
constexpr float AFTER_WIFI = 200;
}
inline uint32_t millis() { return clock_ms; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void mark_failed() { failed_ = true; }
  bool is_failed() const { return failed_; }

 private:
  bool failed_{};
};
namespace libretiny_matter_storage {
struct Delegate {
  CHIP_ERROR SyncGetKeyValue(const char *, uint8_t *, uint16_t &) { return CHIP_NO_ERROR; }
  CHIP_ERROR SyncSetKeyValue(const char *, const uint8_t *, uint16_t) { return CHIP_NO_ERROR; }
  CHIP_ERROR SyncDeleteKeyValue(const char *) { return CHIP_NO_ERROR; }
};
struct MatterStorage : Component {
  Delegate delegate;
  Delegate *get_delegate() { return &delegate; }
};
}  // namespace libretiny_matter_storage
namespace network {
inline bool is_connected() { return online; }
}  // namespace network
namespace wifi {
constexpr unsigned SSID_BUFFER_SIZE = 33;
struct WiFiComponent {
  void wifi_ssid_to(char *ssid) { strcpy(ssid, "runtime-test"); }
};
inline WiFiComponent fake_wifi;
inline WiFiComponent *global_wifi_component = &fake_wifi;
}  // namespace wifi
inline void esp_log_printf_(int, const char *, int, const char *, ...) {}
}  // namespace esphome
#define ESPHOME_LOG_LEVEL_ERROR 1
#define ESPHOME_LOG_LEVEL_WARN 2
#define ESPHOME_LOG_LEVEL_INFO 3
#define ESP_LOGI(...) esphome::esp_log_printf_(3, __VA_ARGS__)
#define ESP_LOGW(...) esphome::esp_log_printf_(2, __VA_ARGS__)
#define ESP_LOGE(...) esphome::esp_log_printf_(1, __VA_ARGS__)
#define ESP_LOGD(...) esphome::esp_log_printf_(4, __VA_ARGS__)
#define ESP_LOGCONFIG(...) esphome::esp_log_printf_(3, __VA_ARGS__)
#define YESNO(x) ((x) ? "YES" : "NO")
// Match the variadic ESPHome macro call, keeping arguments type-checked.
namespace esphome {
inline void esp_log_printf_(int, const char *, const char *, ...) {}
}  // namespace esphome
