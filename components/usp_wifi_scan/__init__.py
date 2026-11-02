"""Authenticated local Wi-Fi scan endpoint for Ukraine Status Panel."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import wifi
from esphome.const import CONF_ID

CODEOWNERS = ["@ukraine-status-panel"]
DEPENDENCIES = ["web_server", "wifi"]

wifi_scan_ns = cg.esphome_ns.namespace("usp_wifi_scan")
WifiScanApi = wifi_scan_ns.class_("WifiScanApi", cg.Component)

def _enable_runtime_power_save(config):
    """Allow the scan API to temporarily disable Wi-Fi power saving."""
    wifi.enable_runtime_power_save_control()
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WifiScanApi),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _enable_runtime_power_save,
)


async def to_code(config) -> None:
    """Register the authenticated scan API component."""
    wifi.request_wifi_scan_results()
    wifi.request_wifi_scan_results_listener()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
