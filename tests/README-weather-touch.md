# Weather LCD touch checks

weather-touch-layout.py reads the actual canonical YAML using a reference
loader that does not resolve secrets. It checks22 weather controls >=48x48,
parent/display containment, pair separation and reflowed labels against
the room/forecast buttons. Boot-hidden cards remain in scope: hidden at boot
does not mean permanently unused.

weather-touch-lvgl-native.py builds real LVGL9.5 with ASAN/UBSAN and the
production gesture controller. Rectangles and parent bounds come from the
same YAML gate. Both navigation modes exercise220 center/edge taps and44
points just outside; fullscreen also checks22 button drags cancel clicks
without navigation, and22 background swipes navigate. Test callbacks only
count hit dispatch; real firmware actions are not invoked. Font rendering,
GT911 hardware and a person's visual/touch acceptance remain separate.

Run the layout check with:
  /root/config/.qa-venv/bin/python3 tests/weather-touch-layout.py

For native checks set USP_LVGL_ROOT to the project's pinned LVGL source and run:
  /root/config/.qa-venv/bin/python3 tests/weather-touch-lvgl-native.py

Use --help for a fresh saved build directory and explicit LVGL path. Existing
evidence directories are refused. Both checks are included in the full
offline regression; live panel tests are disabled there.

The .296 change keeps22 actions, labels, fonts and colors byte-identical to
accepted .295 and all non-LVGL/non-version configuration equal. Its release
evidence includes this equivalence and the original small-target failure.
No synthetic weather, touch, threats or settings are sent to the panel.
