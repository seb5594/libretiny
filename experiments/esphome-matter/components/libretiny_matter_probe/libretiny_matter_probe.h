#pragma once

#include "esphome/components/socket/socket.h"
#include "esphome/core/component.h"

namespace esphome::libretiny_matter_probe {

class LibreTinyMatterProbe : public PollingComponent {
 public:
  void setup() override;
  void dump_config() override;
  void update() override;
  void loop() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void set_udp_echo(bool value) { this->udp_echo_ = value; }
  void set_port(uint16_t value) { this->port_ = value; }
  void set_interface_index(uint8_t value) { this->interface_index_ = value; }

 protected:
#if USE_NETWORK_IPV6
  bool open_socket_();
  void join_multicast_();
  std::unique_ptr<socket::Socket> socket_;
#endif
  bool udp_echo_{false};
  bool multicast_joined_{false};
  uint16_t port_{49152};
  uint8_t interface_index_{0};
  uint32_t last_attempt_ms_{0};
  uint32_t last_poll_ms_{0};
  uint32_t received_{0};
  uint32_t sent_{0};
  uint32_t send_errors_{0};
};

}  // namespace esphome::libretiny_matter_probe
