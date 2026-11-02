"""Device-side OTA trust verification foundation for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@ukraine-status-panel"]
DEPENDENCIES = ["web_server"]

ota_guard_ns = cg.esphome_ns.namespace("usp_ota_guard")
OtaGuard = ota_guard_ns.class_("OtaGuard", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OtaGuard),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the verifier and ensure the existing libsodium dependency."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add_library("esphome/libsodium", "1.10021.11")
