"""Execute the actual ESPHome/FreeRTOS bridge with deterministic platform doubles."""

import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "components/libretiny_matter_server"


@pytest.fixture(scope="module")
def runtime_binary(tmp_path_factory):
    build = tmp_path_factory.mktemp("server-runtime")
    headers = [
        "FreeRTOS.h",
        "libretiny.h",
        "lwip/netif.h",
        "queue.h",
        "semphr.h",
        "task.h",
        "drv_model_pub.h",
        "irda_pub.h",
        "esphome/components/libretiny_matter_storage/matter_storage.h",
        "esphome/core/component.h",
        "esphome/core/log.h",
        "esphome/components/network/util.h",
        "esphome/components/wifi/wifi_component.h",
    ]
    for name in headers:
        path = build / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "runtime_mocks.h"\n')
    binary = build / "runtime-test"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-no-pie",
            f"-I{build}",
            f"-I{SERVER}",
            f"-I{ROOT / 'tests/server'}",
            str(SERVER / "matter_server.cpp"),
            str(ROOT / "tests/server/runtime_test.cpp"),
            "-o",
            str(binary),
        ],
        check=True,
    )
    return binary


@pytest.mark.parametrize(
    "scenario", ["network", "low_heap", "task_failure", "random", "allocator"]
)
def test_runtime(runtime_binary, scenario):
    subprocess.run([str(runtime_binary), scenario], check=True, timeout=10)
