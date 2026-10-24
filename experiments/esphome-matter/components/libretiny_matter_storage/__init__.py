"""Native CHIP PersistentStorageDelegate backed by LibreTiny's shared FlashDB."""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

storage_ns = cg.esphome_ns.namespace("libretiny_matter_storage")
MatterStorage = storage_ns.class_("MatterStorage", cg.Component)


def _validate(config):
    if CORE.target_platform != "bk72xx":
        raise cv.Invalid("Matter storage currently requires LibreTiny BK72xx")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(MatterStorage)}).extend(
        cv.COMPONENT_SCHEMA
    ),
    _validate,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    interface = Path(__file__).parent / "vendor/connectedhomeip"
    cg.add_library(
        "MatterStorageInterface", None, repository=f"file://{interface.resolve()}"
    )
    cg.add_build_flag("-DCHIP_SYSTEM_CONFIG_USE_LWIP=1")
