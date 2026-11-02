"""NEPTUN threat summary transport for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.const import CONF_ID

CONF_IDLE_REFRESH_INTERVAL = "idle_refresh_interval"
CONF_INITIAL_DELAY = "initial_delay"
CONF_ON_UPDATE = "on_update"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_STALE_AFTER = "stale_after"

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["network"]

neptun_ns = cg.esphome_ns.namespace("usp_neptun")
NeptunClient = neptun_ns.class_("NeptunClient", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NeptunClient),
        cv.Optional(CONF_REFRESH_INTERVAL, default="20s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_IDLE_REFRESH_INTERVAL, default="45s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STALE_AFTER, default="120s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_INITIAL_DELAY, default="24s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_UPDATE):
            automation.validate_automation(single=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the independent NEPTUN REST worker."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_refresh_interval(config[CONF_REFRESH_INTERVAL]))
    cg.add(var.set_idle_refresh_interval(config[CONF_IDLE_REFRESH_INTERVAL]))
    cg.add(var.set_stale_after(config[CONF_STALE_AFTER]))
    cg.add(var.set_initial_delay(config[CONF_INITIAL_DELAY]))

    if CONF_ON_UPDATE in config:
        await automation.build_automation(
            var.get_update_trigger(), [], config[CONF_ON_UPDATE]
        )

    esp32.require_certificate_bundle()
    esp32.include_builtin_idf_component("esp_http_client")
    esp32.include_builtin_idf_component("esp-tls")
