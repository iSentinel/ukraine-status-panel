#!/usr/bin/env python3
"""Require flash-safe Spotify task ownership and bounded secret workspaces."""
import argparse
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]


def check_task(component):
    """Require internal stack allocation while keeping large buffers external."""
    source = (component / "spotify_auth.cpp").read_text()
    header = (component / "spotify_auth.h").read_text()
    start = source.index("bool SpotifyAuthApi::start_worker_()")
    end = source.index("\nnamespace {", start)
    task = source[start:end]
    assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in task
    assert "MALLOC_CAP_SPIRAM" not in task
    assert "RAMUniquePtr<usp_spotify_session::Workspace>" in header
    assert "ExternalRAMAllocator<usp_spotify_session::Workspace>" in source
    assert "this->session_vault_.set_workspace(" in source
    assert "RAMUniquePtr<usp_spotify_stage4_binding::ReadWorkspace>" in header
    assert "ReadWorkspace>::ALLOC_EXTERNAL" in source
    assert "usp_spotify_queue::parse_into(" in source
    queue_read = source.split("void SpotifyAuthApi::poll_queue_(", 1)[1]
    queue_read = queue_read.split(
        "void SpotifyAuthApi::poll_stage4_reads_", 1
    )[0]
    assert "lock.unlock()" not in queue_read
    assert "Queue queue{}" not in queue_read


def check_storage(component):
    """Prevent shared preference queue calls, erasure and unguarded flash calls."""
    source = (component / "session_vault_idf.h").read_text()
    for token in (
        "SESSION_NAMESPACE",
        '"usp_spotify"',
        '"session_v1"',
        '"local_key_v1"',
        "nvs_open(",
        "nvs_get_blob(",
        "nvs_set_blob(",
        "nvs_commit(",
        "esp_ptr_internal(&marker)",
        "can_access_()",
    ):
        assert token in source, token
    for token in (
        "global_preferences",
        "make_preference",
        "nvs_flash_erase",
        "nvs_erase_all",
        "esp_efuse_write_key",
    ):
        assert token not in source, token
    vault = (component / "session_vault.h").read_text()
    assert "WorkspaceUse" in vault and "Workspace *workspace_" in vault
    for token in (
        "Plaintext next",
        "Record tombstone",
        "Record record{};\n    ",
        "Session next",
    ):
        assert token not in vault
    engine = (component / "token_worker_engine.h").read_text()
    assert (
        "Session restore_session_" in engine
        and "Session session;" not in engine
    )


def main():
    """Static contracts supplement native and actual ESP32 stack measurements."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    component = ROOT / "components/usp_spotify_auth"
    check_task(component)
    check_storage(component)
    logger.info(
        "SPOTIFY_FLASH_STACK_STATIC=PASS internal_task PSRAM_data private_NVS no_erase"
    )


if __name__ == "__main__":
    main()
