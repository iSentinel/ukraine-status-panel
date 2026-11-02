#!/usr/bin/env python3
"""Validate authenticated Wi-Fi scan UI/backend integration."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
YAML = (ROOT / "firmware/ukraine-status-panel.yaml").read_text()
PUBLIC = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
JS = (ROOT / "firmware/webui/usp-web-ui.src.js").read_text()
CPP = (ROOT / "components/usp_wifi_scan/wifi_scan_api.cpp").read_text()
PYCOMP = (ROOT / "components/usp_wifi_scan/__init__.py").read_text()
WEB_IDF = (ROOT / "components/web_server_idf/web_server_idf.cpp").read_text()


def need(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


need("usp_wifi_scan:" in YAML, "production scan component missing")
need("/usp/network/scan" in CPP, "scan endpoint missing")
HEADER = (ROOT / "components/usp_wifi_scan/wifi_scan_api.h").read_text()
need("esp_wifi_scan_start" in CPP and "WiFiScanResultsListener" in HEADER,
     "ESPHome scan listener integration missing")
need("WifiScanState::SCANNING" in CPP and "WifiScanState::READY" in CPP,
     "non-blocking scan state machine missing")
need("WifiScanState::START_PENDING" in CPP and "SCAN_DEFER_MS" in HEADER,
     "HTTP response is not decoupled from radio scan start")
need("config.coex_background_scan = true" in CPP and
     "config.home_chan_dwell_time = 150" in CPP,
     "connected-station home-channel dwell protection missing")
need("config.scan_time.active.min = 15" in CPP and
     "config.scan_time.active.max = 40" in CPP,
     "balanced active scan dwell contract missing")
need("wifi.enable_runtime_power_save_control()" in PYCOMP,
     "runtime Wi-Fi power-save control is not enabled")
need("request_high_performance()" in CPP and
     "release_high_performance()" in CPP and
     "release_high_performance_();" in CPP,
     "scan high-performance acquire/release contract missing")
need("config.channel = 0" not in CPP and "scan_channel_" not in CPP,
     "rejected channel-sliced scan state machine returned")
need("request->method() == HTTP_POST" in CPP and '202, "scanning"' in CPP,
     "POST start / 202 scan contract missing")
need("xSemaphoreTake(this->scan_done_" not in CPP and "pdMS_TO_TICKS(15000)" not in CPP,
     "HTTP handler still blocks waiting for Wi-Fi scan completion")
need("httpd_resp_send(*request, this->response_.get(), length);\n  this->unlock_();" in CPP,
     "shared Wi-Fi JSON buffer is not protected through HTTP send")
need('fetch("/usp/network/scan",{method:"POST"})' in JS,
     "browser does not start scan asynchronously")
need('q.status===202' in JS and 'd.state!=="ready"' in JS,
     "browser scan polling contract missing")
need("esp_wifi_scan_get_ap_records" not in CPP, "driver scan records are consumed twice")
need("add_scan_results_listener(this)" in CPP, "ESPHome scan listener registration missing")
need("config.show_hidden = true" in CPP, "hidden network detection missing")
need("add_handler(this)" in CPP, "authenticated middleware registration missing")
need("case 202:" in WEB_IDF and "202 Accepted" in WEB_IDF,
     "web_server_idf does not preserve HTTP 202 Accepted")
need("case 405:" in WEB_IDF and "405 Method Not Allowed" in WEB_IDF,
     "web_server_idf does not preserve HTTP 405")
need("case 503:" in WEB_IDF and "503 Service Unavailable" in WEB_IDF,
     "web_server_idf does not preserve HTTP 503")
need("add_handler_without_auth" not in CPP, "scan endpoint bypasses web auth")
need("DEPENDENCIES = [\"web_server\", \"wifi\"]" in PYCOMP,
     "component dependency contract missing")
need('fetch("/usp/network/scan"' in JS, "browser scan request missing")
need('entity.name === "Network SSID"' in JS, "raw SSID card is not suppressed")
need("prompt(t.hiddenWifi" in JS, "hidden-SSID manual fallback missing")
need("n.rssi" in JS and "n.secure" in JS,
     "signal/security metadata missing")
need("usp_wifi_scan" not in PUBLIC and "/usp/network/scan" not in PUBLIC,
     "public first-flash leaked private Wi-Fi scan API")
print("WEB_WIFI_SCAN_STATIC=PASS auth=1 esp_idf=1 selection=1 hidden_fallback=1 public_isolated=1")
