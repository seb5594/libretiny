"""Compile the real CHIP delegate and FlashDB against a NOR flash test device."""

import hashlib
import json
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/libretiny_matter_storage"
VENDOR = ROOT / "components/libretiny_matter/vendor/connectedhomeip"


def test_upstream_headers_are_unmodified():
    manifest = json.loads((VENDOR / "upstream.json").read_text())
    for name, digest in manifest["files"].items():
        assert hashlib.sha256((VENDOR / name).read_bytes()).hexdigest() == digest


@pytest.fixture(scope="module")
def storage_binary(tmp_path_factory):
    source = Path(os.environ.get("FLASHDB_SOURCE", ROOT / ".cache/flashdb"))
    if not (source / "src/fdb_kvdb.c").is_file():
        pytest.fail("Run tools/fetch_flashdb.py first, or set FLASHDB_SOURCE")
    build = tmp_path_factory.mktemp("storage-build")
    includes = [
        f"-I{ROOT / 'tests/storage'}",
        f"-I{source / 'inc'}",
        f"-I{source / 'port/fal/inc'}",
        f"-I{COMPONENT}",
        f"-I{VENDOR / 'src'}",
    ]
    objects = []
    for name in ["fdb", "fdb_utils", "fdb_kvdb"]:
        obj = build / f"{name}.o"
        subprocess.run(
            [
                "cc",
                "-std=c99",
                "-g",
                *includes,
                "-c",
                str(source / f"src/{name}.c"),
                "-o",
                str(obj),
            ],
            check=True,
            timeout=60,
        )
        objects.append(str(obj))
    binary = build / "storage-test"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-g",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pthread",
            "-DCHIP_SYSTEM_CONFIG_USE_SOCKETS=1",
            *includes,
            str(ROOT / "tests/storage/storage_test.cpp"),
            str(ROOT / "tests/storage/nor_flash.cpp"),
            str(COMPONENT / "flashdb_storage.cpp"),
            *objects,
            "-o",
            str(binary),
        ],
        check=True,
        timeout=60,
    )
    return binary


@pytest.mark.parametrize("mode", ["contract", "errors", "full"])
def test_storage_contract(storage_binary, tmp_path, mode):
    subprocess.run(
        [str(storage_binary), mode, str(tmp_path / "flash.bin")], check=True, timeout=10
    )


def test_storage_survives_process_restart(storage_binary, tmp_path):
    image = str(tmp_path / "flash.bin")
    subprocess.run([str(storage_binary), "save", image], check=True, timeout=10)
    subprocess.run([str(storage_binary), "reload", image], check=True, timeout=10)
