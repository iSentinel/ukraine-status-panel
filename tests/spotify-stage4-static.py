#!/usr/bin/env python3
"""Fail closed if Spotify Stage4 loses its bounded one-worker safety contract."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMP = ROOT / "components/usp_spotify_auth"


def need(text: str, token: str, label: str) -> None:
    """Require one safety token in canonical source."""
    if token not in text:
        raise AssertionError(f"{label}: {token}")


def main() -> None:
    """Check canonical Stage4 ownership, privacy and public separation."""
    header = (COMP / "spotify_auth.h").read_text()
    source = (COMP / "spotify_auth.cpp").read_text()
    coordinator = (COMP / "stage4_coordinator.h").read_text()
    queue = (COMP / "queue_policy.h").read_text()
    public = (ROOT / "firmware/ukraine-status-panel-public-first-flash.yaml").read_text()
    for token in ("request_transfer", "request_volume", "request_seek",
                  "request_shuffle_toggle", "request_repeat", "request_queue_add"):
        need(header, token, "public adapter API")
    need(source, "dispatch_stage4_();", "single worker dispatch")
    need(source, "poll_stage4_reads_();", "single worker reads")
    need(source, "zeroize(this->stage4_body_.get(),STAGE4_QUEUE_BODY)", "body zeroize")
    need(source, "response.status==401&&this->generation()==generation", "stale 401 guard")
    need(coordinator, "provider_response", "shared Player cooldown")
    need(queue, "MAX_ITEMS=12", "bounded queue")
    assert source.count("xTaskCreatePinnedToCoreWithCaps(") == 1
    assert "usp_spotify_auth:" not in public and "stage4_transport" not in public
    print("SPOTIFY_STAGE4_STATIC=PASS bounded one_worker shared429 generation_guard public0")


if __name__ == "__main__":
    main()
