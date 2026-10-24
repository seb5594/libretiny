"""Check invalid configurations through ESPHome's real validation pipeline."""

import subprocess
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize(
    ("ipv6", "port", "expected"),
    [
        (False, 49152, "udp_echo requires network: enable_ipv6: true"),
        (True, 5540, "at least 49152"),
        (True, 65536, "at most 65535"),
    ],
)
def test_reject_invalid_config(tmp_path, ipv6, port, expected):
    config = yaml.safe_load((ROOT / "tests/host.yaml").read_text())
    config["external_components"][0]["source"]["path"] = str(ROOT / "components")
    config["network"]["enable_ipv6"] = ipv6
    config["libretiny_matter_probe"]["port"] = port
    path = tmp_path / "invalid.yaml"
    path.write_text(yaml.safe_dump(config))
    result = subprocess.run(
        [sys.executable, "-m", "esphome", "config", str(path)],
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert result.returncode != 0
    assert expected in result.stdout + result.stderr
