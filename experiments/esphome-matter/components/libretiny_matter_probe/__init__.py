"""Measure prerequisites for a future LibreTiny Matter port, without a Matter stack."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import socket
from esphome.const import CONF_ENABLE_IPV6, CONF_ID, CONF_PORT
from esphome.core import CORE

AUTO_LOAD = ["network", "socket"]
CONF_UDP_ECHO = "udp_echo"
CONF_INTERFACE_INDEX = "interface_index"

probe_ns = cg.esphome_ns.namespace("libretiny_matter_probe")
LibreTinyMatterProbe = probe_ns.class_("LibreTinyMatterProbe", cg.PollingComponent)


def _validate(config):
    if not (CORE.is_libretiny or CORE.is_host):
        raise cv.Invalid(
            "This probe requires LibreTiny (or host for development tests)"
        )
    if config[CONF_UDP_ECHO]:
        socket.consume_sockets(1, "libretiny_matter_probe", socket.SocketType.UDP)(
            config
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LibreTinyMatterProbe),
            cv.Optional(CONF_UDP_ECHO, default=False): cv.boolean,
            cv.Optional(CONF_PORT, default=49152): cv.int_range(min=49152, max=65535),
            cv.Optional(CONF_INTERFACE_INDEX, default=0): cv.uint8_t,
        }
    ).extend(cv.polling_component_schema("30s")),
    _validate,
)


def _final_validate(config):
    full = fv.full_config.get()
    if not CORE.is_host and "wifi" not in full:
        raise cv.Invalid("The LibreTiny probe requires wifi")
    if config[CONF_UDP_ECHO] and not full.get("network", {}).get(CONF_ENABLE_IPV6):
        raise cv.Invalid("udp_echo requires network: enable_ipv6: true")


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_udp_echo(config[CONF_UDP_ECHO]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_interface_index(config[CONF_INTERFACE_INDEX]))
