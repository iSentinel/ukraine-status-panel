#!/usr/bin/env python3
"""Guard B-F10 network clients against large internal-heap temporaries."""

from pathlib import Path

from loguru import logger


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "components/usp_weather/weather_client.cpp").read_text()
OUTAGE = (ROOT / "components/usp_outage/outage_client.cpp").read_text()
STARLINK = (ROOT / "components/usp_starlink/starlink_grpc.cpp").read_text()


def need(condition: bool, message: str) -> None:
    """Fail with a focused contract error."""
    if not condition:
        raise AssertionError(message)


def main() -> None:
    """Verify all three B-F10 allocation-pressure fixes stay present."""
    need("struct PsramHttpBody" in WEATHER, "weather PSRAM body missing")
    need("MALLOC_CAP_SPIRAM" in WEATHER, "weather body is not PSRAM-backed")
    need(
        "json::parse_json(body, body_size)" in WEATHER,
        "weather raw-body parser missing",
    )
    need(
        "struct HttpBodyCollector {\n  std::string body;" not in WEATHER,
        "weather regressed to internal std::string body",
    )

    need(
        OUTAGE.count("esp_http_client_init(&config)") == 1,
        "outage must create one HTTP client per refresh",
    )
    need(
        "esp_http_client_set_url(client, url.c_str())" in OUTAGE,
        "outage client reuse path missing",
    )
    need(
        OUTAGE.count("this->fetch_group_json_(") == 2,
        "planned/probable requests are not both using shared client",
    )

    need(
        "nghttp2_session_client_new3" in STARLINK,
        "Starlink custom nghttp2 allocator missing",
    )
    need(
        "heap_caps_calloc" in STARLINK and "heap_caps_realloc" in STARLINK,
        "Starlink nghttp2 PSRAM callbacks incomplete",
    )
    need(
        "std::vector<uint8_t> response;" not in STARLINK,
        "Starlink large response regressed to std::vector",
    )
    need(
        "MAX_GRPC_BODY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in STARLINK,
        "Starlink response buffer is not explicitly PSRAM-backed",
    )
    logger.info(
        "NETWORK_HEAP_PRESSURE_STATIC=PASS weather=psram16k "
        "outage_http_init=1 starlink_response=psram64k nghttp2=psram"
    )


if __name__ == "__main__":
    main()
