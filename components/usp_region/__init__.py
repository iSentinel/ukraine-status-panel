"""Local selected-region preferences, independent of any provider."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

region_ns = cg.esphome_ns.namespace("usp_region")
RegionSettings = region_ns.class_("RegionSettings", cg.Component)
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RegionSettings),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Register local preferences without networking dependencies."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
