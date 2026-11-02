"""Configure password inputs whose public state remains empty."""

import esphome.automation as automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text
from esphome.const import (
    CONF_MAX_LENGTH,
    CONF_MIN_LENGTH,
    CONF_MODE,
    CONF_RESTORE_VALUE,
    CONF_SET_ACTION,
)

namespace = cg.esphome_ns.namespace("usp_secret_text")
SecretText = namespace.class_("SecretText", text.Text, cg.Component)
CONFIG_SCHEMA = (
    text.text_schema(SecretText, mode="PASSWORD")
    .extend(
        {
            cv.Required(CONF_MODE): cv.enum(
                {"PASSWORD": text.TextMode.TEXT_MODE_PASSWORD}, upper=True
            ),
            cv.Optional(CONF_MIN_LENGTH, default=0): cv.int_range(min=0, max=0),
            cv.Optional(CONF_MAX_LENGTH, default=64): cv.int_range(min=64, max=64),
            cv.Optional(CONF_RESTORE_VALUE, default=False): cv.boolean,
            cv.Optional(CONF_SET_ACTION): automation.validate_automation(single=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    """Keep ordinary text setters while withholding secret state."""
    variable = await text.new_text(
        config,
        min_length=config[CONF_MIN_LENGTH],
        max_length=config[CONF_MAX_LENGTH],
        pattern=None,
    )
    await cg.register_component(variable, config)
    cg.add(variable.set_restore_value(config[CONF_RESTORE_VALUE]))
    if CONF_SET_ACTION in config:
        await automation.build_automation(
            variable.get_set_trigger(), [(cg.std_string, "x")], config[CONF_SET_ACTION]
        )
