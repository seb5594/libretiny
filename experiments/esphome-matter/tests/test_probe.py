"""Exercise the actual C++ ESPHome host component over IPv6, not a mock server."""

import os
import secrets
import socket
import subprocess
import time
from pathlib import Path

import pytest
from tools.check_udp import destination, probe

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def server(tmp_path_factory):
    binary = Path(
        os.environ.get(
            "PROBE_BINARY",
            ROOT
            / "tests/.esphome/build/matter-probe-host"
            / ".pioenvs/matter-probe-host/program",
        )
    )
    if not binary.is_file():
        pytest.fail("Build tests/host.yaml first, or set PROBE_BINARY")
    workdir = tmp_path_factory.mktemp("host")
    with (workdir / "host.log").open("w+") as log:
        process = subprocess.Popen(
            [str(binary)], cwd=workdir, stdout=log, stderr=subprocess.STDOUT
        )
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and process.poll() is None:
                if probe("::1", count=1, timeout=0.1)["acknowledged"] == 1:
                    break
            else:
                log.seek(0)
                pytest.fail(f"Host did not start: {log.read()}")
            yield process
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def test_ipv6_round_trip(server):
    result = probe("::1", count=10, timeout=0.5)
    assert result["acknowledged"] == 10
    assert result["peers"] == ["::1"]
    assert result["matter_tested"] is False
    assert server.poll() is None


@pytest.mark.parametrize(
    "packet",
    [
        b"",
        b"LTM1",
        b"LTM1" + bytes(15),
        b"FAIL" + bytes(16),
        b"LTM1" + bytes(17),
        b"LTM1" + bytes(4096),
    ],
    ids=[
        "empty",
        "short-magic",
        "short-nonce",
        "wrong-magic",
        "extra-byte",
        "oversized",
    ],
)
def test_invalid_packets_are_not_echoed(server, packet):
    with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as sock:
        sock.settimeout(0.15)
        sock.sendto(packet, ("::1", 49152))
        with pytest.raises(TimeoutError):
            sock.recvfrom(4096)
        valid = b"LTM1" + secrets.token_bytes(16)
        sock.sendto(valid, ("::1", 49152))
        assert sock.recvfrom(4096)[0] == valid
    assert server.poll() is None


def test_no_listener_is_reported_as_failure(server):
    assert probe("::1", port=65534, count=1, timeout=0.1)["acknowledged"] == 0


def test_link_local_requires_scope():
    with pytest.raises(ValueError, match="interface"):
        destination("fe80::1", None, 49152)
    assert destination("fe80::1%3", None, 49152) == ("fe80::1", 49152, 0, 3)


@pytest.mark.parametrize("address", ["127.0.0.1", "example.com", "not-an-address"])
def test_reject_non_ipv6_literals(address):
    with pytest.raises(ValueError):
        destination(address, None, 49152)
