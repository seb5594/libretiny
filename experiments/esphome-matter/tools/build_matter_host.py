"""Build the native Matter server with the real FlashDB adapter on simulated NOR."""

import argparse
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "components/libretiny_matter_server"
STORAGE = ROOT / "components/libretiny_matter_storage"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--skip-rust", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    target = Path(os.environ.get("LTM_CARGO_TARGET_DIR", build / "rust-target"))
    if not args.skip_rust:
        cargo = shutil.which("cargo") or str(Path.home() / ".cargo/bin/cargo")
        subprocess.run(
            [
                cargo,
                "build",
                "--locked",
                "--release",
                "-j",
                "1",
                "--target-dir",
                str(target),
            ],
            cwd=SERVER / "rust",
            check=True,
        )
    source = Path(os.environ.get("FLASHDB_SOURCE", ROOT / ".cache/flashdb"))
    if not (source / "src/fdb_kvdb.c").is_file():
        raise SystemExit("Run tools/fetch_flashdb.py before building")
    includes = [
        f"-I{directory}"
        for directory in [
            SERVER,
            STORAGE,
            ROOT / "tests/storage",
            ROOT / "components/libretiny_matter/vendor/connectedhomeip/src",
            source / "inc",
            source / "port/fal/inc",
        ]
    ]
    objects = []
    for name in ["fdb", "fdb_utils", "fdb_kvdb"]:
        obj = build / f"{name}.o"
        subprocess.run(
            [
                "cc",
                "-std=c99",
                "-O2",
                *includes,
                "-c",
                str(source / f"src/{name}.c"),
                "-o",
                str(obj),
            ],
            check=True,
        )
        objects.append(str(obj))
    binary = build / "matter-host"
    subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pthread",
            "-DLTM_HOST",
            "-DCHIP_SYSTEM_CONFIG_USE_SOCKETS=1",
            *includes,
            str(ROOT / "tests/server/host_main.cpp"),
            str(ROOT / "tests/storage/nor_flash.cpp"),
            str(SERVER / "platform_udp.cpp"),
            str(STORAGE / "flashdb_storage.cpp"),
            *objects,
            str(target / "release/liblibretiny_matter_server.a"),
            "-ldl",
            "-lm",
            "-o",
            str(binary),
        ],
        check=True,
    )
    print(binary)


if __name__ == "__main__":
    main()
