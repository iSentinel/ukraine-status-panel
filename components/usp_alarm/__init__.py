"""Three disabled-by-default local alarm schedules using the existing clock."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import time
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "time"]
alarm_ns = cg.esphome_ns.namespace("usp_alarm")
AlarmManager = alarm_ns.class_("AlarmManager", cg.PollingComponent)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(AlarmManager),
    cv.Required("time_id"): cv.use_id(time.RealTimeClock),
    cv.Required("trusted"): cv.returning_lambda,
    cv.Optional("on_alarm"): automation.validate_automation(single=True),
    cv.Optional("on_update"): automation.validate_automation(single=True),
}).extend(cv.polling_component_schema("1s"))


async def to_code(config):
    """Connect the existing clock and independent callbacks."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    clock = await cg.get_variable(config["time_id"])
    cg.add(var.set_clock(clock))
    trusted = await cg.process_lambda(
        config["trusted"], [], return_type=cg.bool_
    )
    cg.add(var.set_trusted(trusted))
    if "on_alarm" in config:
        await automation.build_automation(
            var.get_alarm_trigger(), [(cg.uint8, "slot")], config["on_alarm"]
        )
    if "on_update" in config:
        await automation.build_automation(
            var.get_update_trigger(), [], config["on_update"]
        )
