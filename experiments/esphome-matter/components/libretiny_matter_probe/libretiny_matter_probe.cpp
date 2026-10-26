#include "libretiny_matter_probe.h"

#include <cerrno>
#include <cinttypes>
#include <cstring>

#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_LIBRETINY
#include <libretiny.h>
#include <lwip/opt.h>
#endif

namespace esphome::libretiny_matter_probe {

static const char *const TAG = "libretiny_matter_probe";
static constexpr uint32_t RETRY_INTERVAL_MS = 5000;
static constexpr uint32_t POLL_INTERVAL_MS = 20;
static constexpr size_t PACKET_SIZE = 20;
static constexpr char MAGIC[] = "LTM1";

void LibreTinyMatterProbe::setup() {
  this->last_attempt_ms_ = millis() - RETRY_INTERVAL_MS;
  if (!this->udp_echo_)
    this->disable_loop();
}

void LibreTinyMatterProbe::dump_config() {
  ESP_LOGCONFIG(TAG, "LibreTiny Matter prerequisite probe (NO Matter server)");
  ESP_LOGCONFIG(TAG, "  IPv6 compiled: %s; UDP echo: %s; port: %u", YESNO(USE_NETWORK_IPV6), YESNO(this->udp_echo_),
                this->port_);
#ifdef USE_LIBRETINY
  ESP_LOGCONFIG(TAG, "  Board: %s; chip: %s; LibreTiny: %s", lt_get_board_code(), lt_cpu_get_model_name(),
                lt_get_version());
  ESP_LOGCONFIG(TAG, "  Flash: %" PRIu32 "; RAM (reported total, not free heap): %" PRIu32, lt_flash_get_size(),
                lt_ram_get_size());
  ESP_LOGCONFIG(TAG, "  lwIP: IPv6=%d MLD6=%d sockets=%d", LWIP_IPV6, LWIP_IPV6_MLD, LWIP_SOCKET);
#endif
  LOG_UPDATE_INTERVAL(this);
}

void LibreTinyMatterProbe::update() {
#ifdef USE_LIBRETINY
  ESP_LOGI(TAG, "heap_free=%" PRIu32 " heap_min=%" PRIu32, lt_heap_get_free(), lt_heap_get_min_free());
  const uint32_t max_block = lt_heap_get_max_alloc();
  if (max_block != 0)
    ESP_LOGI(TAG, "heap_max_block=%" PRIu32, max_block);
  else
    ESP_LOGI(TAG, "heap_max_block=unavailable (LibreTiny API returned zero)");
#endif
  unsigned ipv6_count = 0;
  for (const auto &address : network::get_ip_addresses()) {
    if (!address.is_set())
      continue;
    char text[network::IP_ADDRESS_BUFFER_SIZE];
    ESP_LOGI(TAG, "address=%s", address.str_to(text));
    ipv6_count += address.is_ip6();
  }
  ESP_LOGI(TAG, "connected=%s ipv6_addresses=%u multicast_joined=%s rx=%" PRIu32 " tx=%" PRIu32 " send_errors=%" PRIu32,
           YESNO(network::is_connected()), ipv6_count, YESNO(this->multicast_joined_), this->received_, this->sent_,
           this->send_errors_);
}

#if USE_NETWORK_IPV6
bool LibreTinyMatterProbe::open_socket_() {
  auto sock = socket::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (!sock) {
    ESP_LOGW(TAG, "IPv6 UDP socket creation failed: errno=%d", errno);
    return false;
  }
  sockaddr_in6 local{};
  local.sin6_family = AF_INET6;
  local.sin6_port = htons(this->port_);
  if (sock->setblocking(false) != 0 || sock->bind(reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
    ESP_LOGW(TAG, "IPv6 UDP setup failed: errno=%d", errno);
    return false;
  }
  this->socket_ = std::move(sock);
  ESP_LOGI(TAG, "IPv6 UDP echo listening on port %u", this->port_);
  return true;
}

void LibreTinyMatterProbe::join_multicast_() {
  // RFC 4727 link-local experimental group. This is not Matter discovery.
  ipv6_mreq request{};
  const uint8_t group[16] = {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x14};
  std::memcpy(&request.ipv6mr_multiaddr, group, sizeof(group));
  request.ipv6mr_interface = this->interface_index_;
  this->multicast_joined_ = this->socket_->setsockopt(IPPROTO_IPV6, IPV6_JOIN_GROUP, &request, sizeof(request)) == 0;
  if (!this->multicast_joined_)
    ESP_LOGW(TAG, "IPv6 multicast join failed: errno=%d (unicast remains available)", errno);
}
#endif

void LibreTinyMatterProbe::loop() {
#if USE_NETWORK_IPV6
  const uint32_t now = millis();
  if (now - this->last_poll_ms_ < POLL_INTERVAL_MS)
    return;
  this->last_poll_ms_ = now;
  if (!network::is_connected()) {
    this->on_shutdown();
    return;
  }
  if ((!this->socket_ || !this->multicast_joined_) && now - this->last_attempt_ms_ >= RETRY_INTERVAL_MS) {
    this->last_attempt_ms_ = now;
    if (!this->socket_ && !this->open_socket_())
      return;
    if (!this->multicast_joined_)
      this->join_multicast_();
  }
  if (!this->socket_)
    return;

  // One extra byte distinguishes an oversized datagram from a valid packet.
  uint8_t packet[PACKET_SIZE + 1];
  sockaddr_in6 peer{};
  socklen_t peer_size = sizeof(peer);
  const ssize_t count =
      this->socket_->recvfrom(packet, sizeof(packet), reinterpret_cast<sockaddr *>(&peer), &peer_size);
  if (count < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      ESP_LOGW(TAG, "IPv6 UDP receive failed: errno=%d; reopening", errno);
      this->on_shutdown();
      this->last_attempt_ms_ = now;
    }
    return;
  }
  // Accept only our fixed-size nonce protocol, with no amplification.
  if (count != PACKET_SIZE || std::memcmp(packet, MAGIC, sizeof(MAGIC) - 1) != 0 || peer.sin6_family != AF_INET6)
    return;
  this->received_++;
  if (this->socket_->sendto(packet, PACKET_SIZE, 0, reinterpret_cast<sockaddr *>(&peer), peer_size) == PACKET_SIZE)
    this->sent_++;
  else
    this->send_errors_++;
#endif
}

void LibreTinyMatterProbe::on_shutdown() {
#if USE_NETWORK_IPV6
  this->socket_.reset();
#endif
  this->multicast_joined_ = false;
}

}  // namespace esphome::libretiny_matter_probe
