"""PlatformIO hook: build the pinned Matter static library for Beken ARM9."""

import os
import shutil
import subprocess
from pathlib import Path

Import("env")  # noqa: F821 -- provided by SCons

scons_env = globals()["env"]
source = Path(scons_env.Dir(".").srcnode().abspath) / "rust"
cargo = shutil.which("cargo") or str(Path.home() / ".cargo/bin/cargo")
target = "thumbv5te-none-eabi"
output = Path(
    os.environ.get(
        "LTM_CARGO_TARGET_DIR", scons_env.subst("$PROJECT_BUILD_DIR") + "/matter-rust"
    )
).resolve()
subprocess.run(
    [
        cargo,
        "build",
        "--locked",
        "--release",
        "-j",
        "1",
        "--target",
        target,
        "-Z",
        "build-std=core,alloc",
        "--target-dir",
        str(output),
    ],
    cwd=source,
    check=True,
)
scons_env.Append(
    LIBS=[
        scons_env.File(str(output / target / "release/liblibretiny_matter_server.a"))
    ],
)
