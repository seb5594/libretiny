#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {
struct LtmConfig {
  uint32_t passcode;
  uint16_t discriminator;
  uint16_t commissioning_seconds;
  uint8_t ipv4[4];
  uint8_t ipv6[16];
  uint32_t interface;
  uint8_t hostname[16];
  uint8_t ssid[32];
  uint8_t ssid_len;
  uint8_t mac[8];
  uint8_t log_setup_codes;
};
struct LtmPeer {
  uint8_t ip[16];
  uint32_t scope;
  uint16_t port;
  uint8_t ipv4;
};

int32_t ltm_server_init(const LtmConfig *config);
int32_t ltm_server_poll();
void ltm_server_stop();
bool ltm_udp_open(const LtmConfig *config);
void ltm_udp_close();
int32_t ltm_platform_send(uint8_t socket, const uint8_t *data, size_t len, const LtmPeer *peer);
int32_t ltm_platform_recv(uint8_t socket, uint8_t *data, size_t len, LtmPeer *peer);
int32_t ltm_platform_available(uint8_t socket);
int32_t ltm_platform_multicast(uint8_t socket, const uint8_t *ip, bool ipv4, bool join);
void ltm_platform_log(uint8_t level, const uint8_t *data, size_t len);
}
