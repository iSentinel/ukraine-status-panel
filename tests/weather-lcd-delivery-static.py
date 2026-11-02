#!/usr/bin/env python3
"""Guard typed fullscreen Weather delivery without changing layout timing."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIGS = (
    ROOT / 'firmware/ukraine-status-panel.yaml',
    ROOT / 'firmware/ukraine-status-panel-public-first-flash.yaml',
    ROOT / 'firmware/ukraine-status-panel-production.yaml',
)


def selected_configs() -> tuple[Path, ...]:
    """Return an isolated override or all production projections."""
    override = os.environ.get('USP_CONFIG')
    return (Path(override),) if override else DEFAULT_CONFIGS


def script_block(text: str) -> str:
    """Return only the fullscreen Weather refresh script."""
    start = text.index('  - id: refresh_weather_fullscreen_ui')
    end = text.index('  - id: refresh_weather_forecast_ui', start)
    return text[start:end]


def target_ids() -> list[str]:
    """List the existing 24 labels whose geometry remains in YAML."""
    result = [
        'fullscreen_weather_city_label', 'fullscreen_weather_status_label',
        'fullscreen_weather_temp_label', 'fullscreen_weather_condition_label',
        'fullscreen_weather_feels_label', 'fullscreen_weather_humidity_label',
        'fullscreen_weather_wind_label', 'fullscreen_weather_updated_label',
    ]
    for day in range(4):
        result.extend([
            f'full_weather_day{day}_date', f'full_weather_day{day}_condition',
            f'full_weather_day{day}_temp', f'full_weather_day{day}_rain',
        ])
    return result


def verify(config: Path) -> None:
    """Require one typed delivery call and preserve surrounding controls."""
    text = config.read_text(encoding='utf-8')
    block = script_block(text)
    assert 'include/usp_weather_lcd_delivery.h' in text
    assert block.count('usp_weather_lcd_delivery::Targets targets{') == 1
    assert block.count('usp_weather_lcd_delivery::render(id(weather_ui_view), targets);') == 1
    assert block.count('lv_label_set_text(') == 0
    assert block.count('lv_obj_set_style_text_color(') == 0
    for name in target_ids():
        assert block.count(f'id({name})') == 1, name
    for name in ('weather_full_hourly_button', 'weather_full_daily_button',
                 'weather_full_details_button'):
        assert block.count(f'id({name})') == 1, name
    assert block.count('- delay: 1ms') == 1
    print(f'WEATHER_LCD_DELIVERY_STATIC=PASS config={config.name} targets=24 helper=1')


for selected in selected_configs():
    verify(selected)
