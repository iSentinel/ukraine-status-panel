"""Private Spotify authorization endpoint with HMAC-sealed refresh-session worker."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ID

DEPENDENCIES = ["web_server", "lvgl"]
auth_ns = cg.esphome_ns.namespace("usp_spotify_auth")
SpotifyAuthApi = auth_ns.class_("SpotifyAuthApi", cg.Component)


def validate_application_client_id(value):
    """Accept only Spotify public application identifiers."""
    value = cv.string_strict(value)
    valid = 1 <= len(value) <= 128 and value.isascii() and value.isalnum()
    if not valid:
        raise cv.Invalid(
            "Spotify Client ID must be 1..128 ASCII alphanumeric characters"
        )
    return value


def validate_application_redirect(value):
    """Keep the registered return address HTTPS-only and JSON-safe."""
    value = cv.string_strict(value)
    unsafe = any(ch.isspace() or ch in '\\?#"' for ch in value)
    valid = 9 <= len(value) <= 256 and value.startswith("https://")
    if not valid or unsafe:
        raise cv.Invalid("Spotify redirect URI must be a safe HTTPS URL")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SpotifyAuthApi),
        cv.Required("application_client_id"): validate_application_client_id,
        cv.Required("application_redirect_uri"): validate_application_redirect,
        cv.Required("session_key_id"): cv.int_range(min=0, max=5),
        cv.Optional("language"): cv.returning_lambda,
        cv.Optional("artwork_presentation"): cv.returning_lambda,
    }
).extend(cv.COMPONENT_SCHEMA)


def validate_authenticated(config):
    """Fail closed unless the generated web server has nonempty credentials."""
    server = fv.full_config.get().get("web_server", {})
    auth = server.get("auth", {})
    if not auth.get("username") or not auth.get("password"):
        raise cv.Invalid(
            "Spotify authorization requires private web authentication"
        )
    return config


FINAL_VALIDATE_SCHEMA = validate_authenticated


async def to_code(config):
    """Register a handler behind the existing Digest and Origin middleware."""
    variable = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(variable, config)
    cg.add(variable.set_application_client_id(config["application_client_id"]))
    cg.add(
        variable.set_application_redirect_uri(
            config["application_redirect_uri"]
        )
    )
    cg.add(variable.set_session_key_id(config["session_key_id"]))
    if "language" in config:
        callback = await cg.process_lambda(
            config["language"], [], return_type=cg.uint8
        )
        cg.add(variable.set_language(callback))

    if "artwork_presentation" in config:
        presenter = await cg.process_lambda(
            config["artwork_presentation"], [], return_type=cg.void
        )
        cg.add(variable.set_artwork_presenter(presenter))
