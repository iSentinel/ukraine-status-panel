"""Non-blocking Starlink reachability monitor for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.const import CONF_ID

CONF_INITIAL_DELAY = "initial_delay"
CONF_ON_UPDATE = "on_update"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_CONNECT_TIMEOUT = "connect_timeout"

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["network"]

starlink_ns = cg.esphome_ns.namespace("usp_starlink")
StarlinkClient = starlink_ns.class_("StarlinkClient", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StarlinkClient),
        cv.Optional(CONF_REFRESH_INTERVAL, default="30s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_INITIAL_DELAY, default="10s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_CONNECT_TIMEOUT, default="1500ms"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the Starlink worker and its main-loop update trigger."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_refresh_interval(config[CONF_REFRESH_INTERVAL]))
    cg.add(var.set_initial_delay(config[CONF_INITIAL_DELAY]))
    cg.add(var.set_connect_timeout(config[CONF_CONNECT_TIMEOUT]))
    if CONF_ON_UPDATE in config:
        await automation.build_automation(var.get_update_trigger(), [], config[CONF_ON_UPDATE])
    esp32.include_builtin_idf_component("lwip")
    esp32.add_idf_component(name="espressif/nghttp", ref="1.68.1")
