"""Open-Meteo weather transport for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.const import CONF_ID

CONF_INITIAL_DELAY = "initial_delay"
CONF_ON_SEARCH_RESULTS = "on_search_results"
CONF_ON_UPDATE = "on_update"
CONF_REFRESH_INTERVAL = "refresh_interval"
CONF_STALE_AFTER = "stale_after"

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["json", "network"]

weather_ns = cg.esphome_ns.namespace("usp_weather")
WeatherClient = weather_ns.class_("WeatherClient", cg.Component)


def bounded_interval(value):
    """Keep timers within the safe half-period of the 32-bit clock."""
    interval = cv.positive_time_period_milliseconds(value)
    if interval.total_milliseconds >= 0x80000000:
        raise cv.Invalid(
            "Weather intervals must be shorter than 2147483648 ms"
        )
    return interval


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WeatherClient),
        cv.Optional(
            CONF_REFRESH_INTERVAL,
            default="15min",
        ): bounded_interval,
        cv.Optional(
            CONF_STALE_AFTER,
            default="45min",
        ): bounded_interval,
        cv.Optional(
            CONF_INITIAL_DELAY,
            default="12s",
        ): bounded_interval,
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(
            single=True
        ),
        cv.Optional(CONF_ON_SEARCH_RESULTS): automation.validate_automation(
            single=True
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def build_trigger(var, config, key, getter) -> None:
    """Build one no-argument automation trigger when it is configured."""
    if key not in config:
        return
    await automation.build_automation(
        getter(var),
        [],
        config[key],
    )


async def to_code(config) -> None:
    """Register the independent weather worker and its main-loop triggers."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_refresh_interval(config[CONF_REFRESH_INTERVAL]))
    cg.add(var.set_stale_after(config[CONF_STALE_AFTER]))
    cg.add(var.set_initial_delay(config[CONF_INITIAL_DELAY]))

    await build_trigger(
        var, config, CONF_ON_UPDATE, lambda item: item.get_update_trigger()
    )
    await build_trigger(
        var,
        config,
        CONF_ON_SEARCH_RESULTS,
        lambda item: item.get_search_trigger(),
    )

    esp32.require_certificate_bundle()
    esp32.include_builtin_idf_component("esp_http_client")
    esp32.include_builtin_idf_component("esp-tls")
