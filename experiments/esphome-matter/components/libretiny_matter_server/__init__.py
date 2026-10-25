"""Native Project CHIP rs-matter server on the LibreTiny network platform."""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import socket
from esphome.components.libretiny import get_libretiny_family
from esphome.const import CONF_ID
from esphome.core import CORE

AUTO_LOAD = ["libretiny_matter_storage", "network"]
DEPENDENCIES = ["wifi"]
server_ns = cg.esphome_ns.namespace("libretiny_matter_server")
MatterServer = server_ns.class_("MatterServer", cg.Component)


def _passcode(value):
    value = cv.int_range(min=1, max=99999998)(value)
    if value in {12345678, 87654321, *(int(str(n) * 8) for n in range(1, 10))}:
        raise cv.Invalid("Matter forbids this setup passcode")
    return value


def _validate(config):
    if CORE.target_platform != "bk72xx" or get_libretiny_family() not in {
        "BK7231N",
        "BK7238",
    }:
        raise cv.Invalid(
            "The native Matter server currently supports BK7231N and BK7238"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MatterServer),
            cv.Required("passcode"): _passcode,
            cv.Required("discriminator"): cv.int_range(min=0, max=4095),
            cv.Optional("commissioning_seconds", default=300): cv.int_range(
                min=180, max=900
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate,
    socket.consume_sockets(2, "libretiny_matter_server", socket.SocketType.UDP),
)


def _final_validate(config):
    full = fv.full_config.get()
    if not full.get("network", {}).get("enable_ipv6"):
        raise cv.Invalid("Matter requires network.enable_ipv6: true")
    if not full.get("mdns", {}).get("disabled"):
        raise cv.Invalid(
            "Set mdns.disabled: true; the Matter stack provides its own mDNS responder"
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    storage = await cg.get_variable(CORE.config["libretiny_matter_storage"][CONF_ID])
    cg.add(var.set_storage(storage))
    cg.add(var.set_passcode(config["passcode"]))
    cg.add(var.set_discriminator(config["discriminator"]))
    cg.add(var.set_commissioning_seconds(config["commissioning_seconds"]))
    cg.add_build_flag("-DINCLUDE_uxTaskGetStackHighWaterMark=1")
    script = Path(__file__).parent / "build_rust.py"
    cg.add_platformio_option("extra_scripts", [f"pre:{script.resolve()}"])
