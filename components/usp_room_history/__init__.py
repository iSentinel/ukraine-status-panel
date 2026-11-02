"""Bounded 24-hour room climate history for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]

room_history_ns = cg.esphome_ns.namespace("usp_room_history")
RoomHistory = room_history_ns.class_("RoomHistory", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RoomHistory),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the room history worker."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
