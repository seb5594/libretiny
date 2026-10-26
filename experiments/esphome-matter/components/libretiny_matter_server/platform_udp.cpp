#include <algorithm>
#include <cerrno>
#include <cstring>

#include "platform.h"

#ifdef LTM_HOST
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <lwip/sockets.h>
#endif

namespace {
int sockets[2] = {-1, -1};
uint32_t interface_index;
in_addr ipv4_interface;
constexpr int32_t AGAIN = -11;

int32_t socket_result(ssize_t result) {
  if (result >= 0) {
    return result;
  }
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ENOBUFS ? AGAIN : -1;
}
}  // namespace

extern "C" bool ltm_udp_open(const LtmConfig *config) {
  interface_index = config->interface;
  memcpy(&ipv4_interface, config->ipv4, sizeof(ipv4_interface));
  for (unsigned i = 0; i < 2; i++) {
    sockets[i] = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sockets[i] < 0) {
      ltm_udp_close();
      return false;
    }
    const int enabled = 1;
    const int disabled = 0;
    const int ttl = 255;
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(i == 0 ? 5540 : 5353);
    bool ok = setsockopt(sockets[i], IPPROTO_IPV6, IPV6_V6ONLY, &disabled, sizeof(disabled)) == 0;
    if (i == 1) {
      ok &= setsockopt(sockets[i], SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == 0;
      ok &= setsockopt(sockets[i], IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) == 0;
#ifdef LTM_HOST
      ok &= setsockopt(sockets[i], IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof(ttl)) == 0;
#endif
      // lwIP uses the same UDP PCB multicast_ttl for IPv4 and IPv6.
      ok &= setsockopt(sockets[i], IPPROTO_IP, IP_MULTICAST_IF, &ipv4_interface, sizeof(ipv4_interface)) == 0;
    }
    ok &= fcntl(sockets[i], F_SETFL, O_NONBLOCK) == 0;
    ok &= bind(sockets[i], reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0;
    if (!ok) {
      ltm_udp_close();
      return false;
    }
  }
  const uint8_t multicast4[] = {224, 0, 0, 251};
  const uint8_t multicast6[] = {0xff, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xfb};
  if (ltm_platform_multicast(1, multicast4, true, true) != 0 ||
      (interface_index != 0 && ltm_platform_multicast(1, multicast6, false, true) != 0)) {
    ltm_udp_close();
    return false;
  }
  return true;
}

extern "C" void ltm_udp_close() {
  for (auto &fd : sockets) {
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }
}

extern "C" int32_t ltm_platform_send(uint8_t index, const uint8_t *data, size_t len, const LtmPeer *peer) {
  if (index > 1 || sockets[index] < 0 || len > 1583) {
    return -1;
  }
  sockaddr_in6 addr{};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(peer->port);
  addr.sin6_scope_id = peer->scope;
  if (peer->ipv4) {
    addr.sin6_addr.s6_addr[10] = 0xff;
    addr.sin6_addr.s6_addr[11] = 0xff;
    memcpy(addr.sin6_addr.s6_addr + 12, peer->ip, 4);
  } else {
    memcpy(&addr.sin6_addr, peer->ip, 16);
  }
  return socket_result(sendto(sockets[index], data, len, 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)));
}

extern "C" int32_t ltm_platform_recv(uint8_t index, uint8_t *data, size_t len, LtmPeer *peer) {
  if (index > 1 || sockets[index] < 0) {
    return -1;
  }
  // One extra byte detects oversized datagrams even on lwIP versions without MSG_TRUNC.
  uint8_t packet[1584];
  sockaddr_in6 addr{};
  socklen_t addr_len = sizeof(addr);
  const auto count =
      recvfrom(sockets[index], packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&addr), &addr_len);
  if (count < 0) {
    return socket_result(count);
  }
  if (static_cast<size_t>(count) > std::min(len, sizeof(packet) - 1) || addr.sin6_family != AF_INET6) {
    return AGAIN;
  }
  memcpy(data, packet, count);
  *peer = {};
  peer->port = ntohs(addr.sin6_port);
  peer->scope = addr.sin6_scope_id;
  if (IN6_IS_ADDR_V4MAPPED(&addr.sin6_addr)) {
    peer->ipv4 = 1;
    memcpy(peer->ip, addr.sin6_addr.s6_addr + 12, 4);
  } else {
    memcpy(peer->ip, &addr.sin6_addr, 16);
  }
  return count;
}

extern "C" int32_t ltm_platform_available(uint8_t index) {
  if (index > 1 || sockets[index] < 0) {
    return -1;
  }
  uint8_t byte;
  const auto result = recv(sockets[index], &byte, sizeof(byte), MSG_PEEK);
  if (result >= 0) {
    return 1;
  }
  return socket_result(result) == AGAIN ? 0 : -1;
}

extern "C" int32_t ltm_platform_multicast(uint8_t index, const uint8_t *ip, bool ipv4, bool join) {
  if (index > 1 || sockets[index] < 0) {
    return -1;
  }
  if (ipv4) {
    ip_mreq request{};
    memcpy(&request.imr_multiaddr, ip, 4);
    request.imr_interface = ipv4_interface;
    return setsockopt(sockets[index], IPPROTO_IP, join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, &request,
                      sizeof(request));
  }
  ipv6_mreq request{};
  memcpy(&request.ipv6mr_multiaddr, ip, 16);
  request.ipv6mr_interface = interface_index;
  return setsockopt(sockets[index], IPPROTO_IPV6, join ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP, &request, sizeof(request));
}
