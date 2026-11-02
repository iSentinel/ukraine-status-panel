"""Bounded week/month/year room climate aggregation backend."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]

room_aggregate_ns = cg.esphome_ns.namespace("usp_room_aggregate_history")
RoomAggregateHistory = room_aggregate_ns.class_(
    "RoomAggregateHistory", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RoomAggregateHistory),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the bounded aggregate-history component."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
