"""Run real Matter TLV and Interaction Model codec tests on LibreTiny devices."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

AUTO_LOAD = ["libretiny_matter"]
CONF_RUN_ON_BOOT = "run_on_boot"
protocol_ns = cg.esphome_ns.namespace("libretiny_matter_protocol")
MatterProtocolTest = protocol_ns.class_("MatterProtocolTest", cg.Component)


def _validate(config):
    if not (CORE.is_libretiny or CORE.is_host):
        raise cv.Invalid("Matter codec tests require LibreTiny or host")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MatterProtocolTest),
            cv.Optional(CONF_RUN_ON_BOOT, default=True): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_run_on_boot(config[CONF_RUN_ON_BOOT]))
