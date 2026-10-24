"""Native Matter codecs: wire vectors, malformed payloads and bounded fuzzing."""

import json
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/libretiny_matter_protocol"
VENDOR = ROOT / "components/libretiny_matter/vendor/connectedhomeip"
ON_REQUEST = bytes.fromhex("1528002801360215370024000124010624020118350118181824ff0c18")


@pytest.fixture(scope="module")
def protocol_binary(tmp_path_factory):
    build = tmp_path_factory.mktemp("protocol-build")
    manifest = json.loads((VENDOR / "upstream.json").read_text())
    sources = [str(VENDOR / name) for name in manifest["sources"]]
    binary = build / "protocol-test"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-g",
            "-O1",
            "-Wall",
            "-Wextra",
            "-Werror",
            # Upstream intentionally leaves hook parameters unused.
            "-Wno-unused-parameter",
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer",
            "-no-pie",
            "-ffunction-sections",
            "-fdata-sections",
            "-Wl,--gc-sections",
            "-DCHIP_SYSTEM_CONFIG_USE_SOCKETS=1",
            "-DCHIP_ERROR_LOGGING=0",
            "-DCHIP_PROGRESS_LOGGING=0",
            "-DCHIP_DETAIL_LOGGING=0",
            f"-I{VENDOR / 'src'}",
            f"-I{VENDOR / 'src/include'}",
            f"-I{VENDOR / 'third_party/nlio/repo/include'}",
            f"-I{COMPONENT}",
            str(ROOT / "tests/protocol_cli.cpp"),
            str(COMPONENT / "protocol_codec.cpp"),
            str(COMPONENT / "protocol_tests.cpp"),
            *sources,
            "-o",
            str(binary),
        ],
        check=True,
        timeout=120,
    )
    return binary


def inspect(binary, payload):
    result = subprocess.run(
        [str(binary), payload.hex()], capture_output=True, text=True, timeout=5
    )
    assert result.returncode in (0, 1), result.stderr
    return json.loads(result.stdout)


def test_same_self_test_as_device(protocol_binary):
    result = subprocess.run(
        [str(protocol_binary)], capture_output=True, text=True, timeout=10
    )
    assert result.returncode == 0, result.stdout + result.stderr
    report = json.loads(result.stdout)
    assert report["failed"] == 0
    assert report["passed"] >= 70


@pytest.mark.parametrize("command", [0, 1, 2])
def test_off_on_toggle_vectors(protocol_binary, command):
    payload = bytearray(ON_REQUEST)
    payload[18] = command
    assert inspect(protocol_binary, payload) == {
        "error": 0,
        "endpoint": 1,
        "cluster": 6,
        "command": command,
        "revision": 12,
    }


@pytest.mark.parametrize("length", range(len(ON_REQUEST)))
def test_all_truncation_points(protocol_binary, length):
    assert inspect(protocol_binary, ON_REQUEST[:length])["error"] != 0


@pytest.mark.parametrize(
    "payload",
    [
        b"\x16" + ON_REQUEST[1:],  # wrong root type
        ON_REQUEST + b"\x09",  # trailing TLV outside the root
        ON_REQUEST[:10] + ON_REQUEST[13:],  # endpoint missing
        ON_REQUEST[:13] + ON_REQUEST[16:],  # cluster missing
        ON_REQUEST[:16] + ON_REQUEST[19:],  # command missing
        ON_REQUEST[:25] + ON_REQUEST[28:],  # revision missing
        ON_REQUEST[:3] + b"\x29" + ON_REQUEST[4:],  # timed invoke unsupported
        ON_REQUEST[:3] + b"\x24" + ON_REQUEST[4:],  # bool replaced by integer
        b"\xff" * 513,  # outside inspector's work limit
    ],
)
def test_malformed_or_unsupported_payload(protocol_binary, payload):
    assert inspect(protocol_binary, payload)["error"] != 0


def test_bounded_random_inputs_with_sanitizers(protocol_binary):
    subprocess.run([str(protocol_binary), "fuzz"], check=True, timeout=30)


def test_esphome_host_runs_codec_self_test(tmp_path):
    binary = (
        ROOT
        / "tests/.esphome/build/matter-protocol-host"
        / ".pioenvs/matter-protocol-host/program"
    )
    assert binary.is_file(), "Build tests/protocol-host.yaml first"
    try:
        result = subprocess.run(
            [str(binary)], cwd=tmp_path, capture_output=True, timeout=3
        )
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or b"") + (error.stderr or b"")
    assert b"Matter codec tests: passed=72 failed=0" in output, output.decode(
        errors="replace"
    )
