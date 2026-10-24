#!/usr/bin/env python3
"""Check the probe's IPv6 transport; this does not test Matter commissioning."""

import argparse
import ipaddress
import json
import secrets
import socket
import statistics
import time

MAGIC = b"LTM1"
PACKET_SIZE = 20


def destination(address: str, interface: str | None, port: int) -> tuple:
    """Resolve a numeric IPv6 address and optional local interface scope."""
    ip = ipaddress.IPv6Address(address)
    scope = interface or ip.scope_id
    scope_id = 0
    if scope:
        scope_id = int(scope) if scope.isdecimal() else socket.if_nametoindex(scope)
    if (ip.is_link_local or ip.is_multicast) and not scope_id:
        raise ValueError(
            "Link-local/multicast destinations require --interface or %interface"
        )
    if not 49152 <= port <= 65535:
        raise ValueError("Port must be in the probe range 49152..65535")
    return (str(ip).split("%", 1)[0], port, 0, scope_id)


def probe(
    address: str,
    *,
    interface: str | None = None,
    port: int = 49152,
    count: int = 5,
    timeout: float = 1.0,
) -> dict:
    if not 1 <= count <= 1000 or not 0 < timeout <= 60:
        raise ValueError("count must be 1..1000 and timeout must be in (0, 60] seconds")
    target = destination(address, interface, port)
    multicast = ipaddress.IPv6Address(target[0]).is_multicast
    timings = []
    peers = set()
    acknowledged = 0
    with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as sock:
        if multicast:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF, target[3])
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_HOPS, 1)
        for _ in range(count):
            packet = MAGIC + secrets.token_bytes(PACKET_SIZE - len(MAGIC))
            started = time.monotonic()
            deadline = started + timeout
            sock.sendto(packet, target)
            received = False
            responders = set()
            while (remaining := deadline - time.monotonic()) > 0:
                sock.settimeout(remaining)
                try:
                    data, peer = sock.recvfrom(PACKET_SIZE + 1)
                except TimeoutError:
                    break
                if data != packet or peer[1] != port:
                    continue
                peer_ip = ipaddress.IPv6Address(peer[0])
                if not multicast and peer_ip != ipaddress.IPv6Address(target[0]):
                    continue
                peer_key = (peer[0], peer[3])
                if peer_key in responders:
                    continue
                responders.add(peer_key)
                peers.add(f"{peer[0]}%{peer[3]}" if peer[3] else peer[0])
                timings.append((time.monotonic() - started) * 1000)
                received = True
                if not multicast:
                    break
            acknowledged += received
    return {
        "target": address,
        "transport": "ipv6_multicast" if multicast else "ipv6_unicast",
        "sent": count,
        "acknowledged": acknowledged,
        "peers": sorted(peers),
        "rtt_ms_median": round(statistics.median(timings), 3) if timings else None,
        "rtt_ms_max": round(max(timings), 3) if timings else None,
        "matter_tested": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "address", help="IPv6 literal, optionally followed by %%interface"
    )
    parser.add_argument("--interface", help="Local interface name or numeric index")
    parser.add_argument("--port", type=int, default=49152)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=1.0)
    args = parser.parse_args()
    try:
        result = probe(**vars(args))
    except (ValueError, OSError) as error:
        parser.exit(2, f"Probe failed: {error}\n")
    print(json.dumps(result, indent=2))
    return 0 if result["acknowledged"] == result["sent"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
