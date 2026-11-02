# LVGL bootstrap progress regression
Run lvgl-bootstrap-codegen.py with the QA Python environment. It invokes the
real ESPHome generator for a small nested ESP32 display fixture, checks emitted
watchdog progress and limits object creations between progress calls.
It generates C++ only; no image is compiled, GPIO operated or device contacted.
Old codegen produces0 progress calls and fails; patched codegen produces34 for
18 creations, max gap1. Actual full staged graph990->2 and ESP target syntax
evidence are in artifacts/m7b-342-lvgl-bootstrap-preflight-20261025.
The watchdog timeout is unchanged. These source tests do not establish real
boot time or hardware acceptance; UART460800 and real device checks remain.
