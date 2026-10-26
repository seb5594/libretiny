"""Validate server configuration through the real ESPHome pipeline."""

import hashlib
import json
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize(
    ("change", "expected"),
    [
        ({"passcode": 11111111}, "forbids this setup passcode"),
        ({"passcode": 12345678}, "forbids this setup passcode"),
        ({"passcode": 0}, "at least 1"),
        ({"discriminator": 4096}, "at most 4095"),
        ({"commissioning_seconds": 179}, "at least 180"),
        ({"ipv6": False}, "requires network.enable_ipv6"),
        ({"mdns": False}, "Set mdns.disabled: true"),
        ({"board": "generic-bk7231t-qfn32-tuya"}, "supports BK7231N and BK7238"),
        ({}, None),
    ],
)
def test_server_config(tmp_path, change, expected):
    config = {
        "esphome": {"name": "matter-config-test"},
        "bk72xx": {"board": change.get("board", "generic-bk7231n-qfn32-tuya")},
        "wifi": {"ssid": "test", "password": "testpassword"},
        "network": {"enable_ipv6": change.get("ipv6", True)},
        "mdns": {"disabled": change.get("mdns", True)},
        "external_components": [
            {
                "source": {"type": "local", "path": str(ROOT / "components")},
                "components": [
                    "libretiny_matter",
                    "libretiny_matter_storage",
                    "libretiny_matter_server",
                ],
            }
        ],
        "libretiny_matter_server": {
            "passcode": change.get("passcode", 20202021),
            "discriminator": change.get("discriminator", 3840),
            "commissioning_seconds": change.get("commissioning_seconds", 300),
        },
    }
    path = tmp_path / "server.yaml"
    path.write_text(yaml.safe_dump(config))
    result = subprocess.run(
        [sys.executable, "-m", "esphome", "config", str(path)],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if expected:
        assert result.returncode != 0
        assert expected in result.stdout + result.stderr
    else:
        assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize(
    ("manifest", "directory"),
    [
        ("upstream.json", "embassy-sync"),
        ("codegen-upstream.json", "rs-matter-codegen"),
    ],
)
def test_vendor_integrity(manifest, directory):
    vendor = ROOT / "components/libretiny_matter_server/rust/vendor"
    info = json.loads((vendor / manifest).read_text())
    checksums = info["original_sha256"] | info.get("license_sha256", {})
    for name, checksum in checksums.items():
        path = vendor / directory / name
        assert path.is_file(), name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if name not in info["changed"]:
            assert actual == checksum, name
        else:
            assert actual != checksum, f"Unnecessary patch exception: {name}"


def test_public_attestation_root():
    path = ROOT / "tests/server/paa/Chip-Test-PAA-FFF1-Cert.der"
    assert hashlib.sha256(path.read_bytes()).hexdigest() == (
        "369eef40616645e5bd3e1bc9db70815d6cf93e9a611973ac20c9cd14c8d60c37"
    )
