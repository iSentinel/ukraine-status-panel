"""Persistent semantic event history for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import time
from esphome.const import CONF_ID

CONF_COMMIT_DELAY = "commit_delay"
CONF_ON_UPDATE = "on_update"
CONF_TIME_ID = "time_id"

DEPENDENCIES = ["time"]

history_ns = cg.esphome_ns.namespace("usp_history")
HistoryStore = history_ns.class_("HistoryStore", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HistoryStore),
        cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Optional(
            CONF_COMMIT_DELAY,
            default="5s",
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(
            single=True
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the history store, clock source, and update trigger."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    clock = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(clock))
    cg.add(var.set_commit_delay(config[CONF_COMMIT_DELAY]))

    if CONF_ON_UPDATE in config:
        await automation.build_automation(
            var.get_update_trigger(),
            [],
            config[CONF_ON_UPDATE],
        )
