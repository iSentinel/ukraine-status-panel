"""Yasno outage schedule transport for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32, time
from esphome.const import CONF_ID, CONF_TIME_ID

CONF_INITIAL_DELAY = "initial_delay"
CONF_ON_UPDATE = "on_update"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_STALE_AFTER = "stale_after"

DEPENDENCIES = ["esp32", "time"]
AUTO_LOAD = ["json", "network"]

outage_ns = cg.esphome_ns.namespace("usp_outage")
OutageClient = outage_ns.class_("OutageClient", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OutageClient),
        cv.Required(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.Optional(
            CONF_REFRESH_INTERVAL,
            default="15min",
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_STALE_AFTER,
            default="45min",
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_INITIAL_DELAY,
            default="18s",
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(
            single=True
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config) -> None:
    """Register the independent outage worker and main-loop update trigger."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    clock = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time_source(clock))
    cg.add(var.set_refresh_interval(config[CONF_REFRESH_INTERVAL]))
    cg.add(var.set_stale_after(config[CONF_STALE_AFTER]))
    cg.add(var.set_initial_delay(config[CONF_INITIAL_DELAY]))

    if CONF_ON_UPDATE in config:
        await automation.build_automation(
            var.get_update_trigger(),
            [],
            config[CONF_ON_UPDATE],
        )

    esp32.require_certificate_bundle()
    esp32.include_builtin_idf_component("esp_http_client")
    esp32.include_builtin_idf_component("esp-tls")
