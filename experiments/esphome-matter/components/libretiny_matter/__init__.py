"""Shared, pinned upstream Matter code for the LibreTiny port experiments."""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    source = Path(__file__).parent / "vendor/connectedhomeip"
    # Track source updates locally instead of retaining PlatformIO's copied cache.
    cg.add_library(
        "LibreTinyMatterCore", None, repository=f"symlink://{source.resolve()}"
    )
    for directory in ["third_party/nlio/repo/include"]:
        cg.add_build_flag(f'-I"{(source / directory).resolve()}"')
    backend = "SOCKETS" if CORE.is_host else "LWIP"
    cg.add_build_flag(f"-DCHIP_SYSTEM_CONFIG_USE_{backend}=1")
    for level in ["ERROR", "PROGRESS", "DETAIL"]:
        cg.add_build_flag(f"-DCHIP_{level}_LOGGING=0")
