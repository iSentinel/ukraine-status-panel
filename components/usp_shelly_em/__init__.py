"""Read-only Shelly Pro EM-50 monitor for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.const import CONF_ID

CONF_CONNECT_TIMEOUT = "connect_timeout"
CONF_INITIAL_DELAY = "initial_delay"
CONF_ON_UPDATE = "on_update"
CONF_REFRESH_INTERVAL = "refresh_interval"

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["network", "json"]

shelly_ns = cg.esphome_ns.namespace("usp_shelly_em")
ShellyEmClient = shelly_ns.class_("ShellyEmClient", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ShellyEmClient),
        cv.Optional(CONF_REFRESH_INTERVAL, default="10s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_INITIAL_DELAY, default="8s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_CONNECT_TIMEOUT, default="1800ms"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the Shelly worker and main-loop update trigger."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_refresh_interval(config[CONF_REFRESH_INTERVAL]))
    cg.add(var.set_initial_delay(config[CONF_INITIAL_DELAY]))
    cg.add(var.set_connect_timeout(config[CONF_CONNECT_TIMEOUT]))
    if CONF_ON_UPDATE in config:
        await automation.build_automation(
            var.get_update_trigger(), [], config[CONF_ON_UPDATE]
        )
    esp32.include_builtin_idf_component("esp_http_client")
