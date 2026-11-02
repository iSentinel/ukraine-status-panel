from pathlib import Path
from firmware_version_guard import CURRENT_FIRMWARE_MARKER

ROOT = Path(__file__).resolve().parents[1]
CPP = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()
HDR = (ROOT / "components/lvgl/lvgl_esphome.h").read_text()
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()


def need(cond, msg):
    if not cond:
        raise SystemExit(f"FAIL: {msg}")


need(CURRENT_FIRMWARE_MARKER in YAML, "wrong firmware version")
home = YAML[YAML.index("- id: page_home") : YAML.index("- id: page_weather")]
need("bg_opa: 100%" in home, "Home root must be opaque again")
need(
    "RefreshProfile refresh_profile_" in HDR,
    "shared profiler arithmetic missing",
)
for field in (
    "handler_ms=%u",
    "pre_ms=%u",
    "render_ms=%u",
    "post_ms=%u",
    "dispatch_ms=%u",
    "layout_ms=%u",
    "prepare_ms=%u",
    "ready_seen=%s",
):
    need(field in CPP, f"profile field missing: {field}")
need("refresh_profile_.render_start(millis())" in CPP, "render event missing")
need("refresh_profile_.ready(millis())" in CPP, "ready event missing")
need("refresh_profile_.begin(millis())" in CPP, "handler start missing")
need("refresh_profile_.end(now)" in CPP, "handler end missing")
need(
    "LV_EVENT_REFR_START" in CPP and "LV_EVENT_UPDATE_LAYOUT_COMPLETED" in CPP,
    "refresh or layout diagnostic event missing",
)
need(
    "static void profile_render_start_cb(lv_event_t *event);" in HDR,
    "profile render-start declaration missing",
)
need(
    "static void profile_render_end_cb(lv_event_t *event);" in HDR,
    "profile render-end declaration missing",
)
need(
    "void LvglComponent::profile_render_start_cb" in CPP,
    "profile render-start definition missing",
)
need(
    "void LvglComponent::profile_render_end_cb" in CPP,
    "profile render-end definition missing",
)
need(
    "lv_display_add_event_cb(this->disp_, profile_render_start_cb, LV_EVENT_RENDER_START, this);"
    in CPP,
    "profile render-start callback must be registered unconditionally",
)
need(
    "lv_display_add_event_cb(this->disp_, profile_render_end_cb, LV_EVENT_REFR_READY, this);"
    in CPP,
    "profile render-end callback must be registered unconditionally",
)
need(
    "if (this->draw_start_callback_ != nullptr)" in CPP
    and "lv_display_add_event_cb(this->disp_, render_start_cb, LV_EVENT_RENDER_START, this);"
    in CPP,
    "user render-start callback lost its guard",
)
need(
    "if (this->draw_end_callback_ != nullptr || this->update_when_display_idle_)"
    in CPP
    and "lv_display_add_event_cb(this->disp_, render_end_cb, LV_EVENT_REFR_READY, this);"
    in CPP,
    "user render-end callback lost its guard",
)
user_start = CPP[
    CPP.index("void LvglComponent::render_start_cb") : CPP.index(
        "void LvglComponent::profile_render_end_cb"
    )
]
need(
    "profile_render_started_ms_" not in user_start
    and "profile_render_finished_ms_" not in user_start,
    "profiling timestamp leaked into user draw callback",
)


# These callbacks must be in the setup body, outside all option guards.
setup_start = CPP.index("void LvglComponent::setup()")
setup = CPP[setup_start:]
for callback in (
    "profile_render_start_cb",
    "profile_render_end_cb",
    "profile_refresh_start_cb",
    "profile_layout_ready_cb",
):
    call = f"lv_display_add_event_cb(this->disp_, {callback},"
    need(setup.count(call) == 1, f"{callback} registered more than once")
    prefix = setup[: setup.index(call)]
    need(
        prefix.count("{") - prefix.count("}") == 1,
        f"{callback} registration is inside a conditional block",
    )

user_end = CPP[
    CPP.index("void LvglComponent::render_end_cb") : CPP.index(
        "void LvglComponent::profile_render_end_cb"
    )
]
need(
    "profile_render_" not in user_end,
    "diagnostic timestamps must stay outside user callbacks",
)
for guard, callback in (
    ("this->draw_start_callback_ != nullptr", "render_start_cb"),
    (
        "this->draw_end_callback_ != nullptr || this->update_when_display_idle_",
        "render_end_cb",
    ),
):
    block = setup[setup.index(f"if ({guard}) {{") :]
    block = block[: block.index("}")]
    need(
        f"this->disp_, {callback}," in block,
        f"{callback} must remain inside its original option guard",
    )

print("PASS lvgl-refresh-phase-profile-static")
