#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${PYTHON_BIN:-}" ]]; then
  for candidate in \
    "$ROOT/.qa-venv/bin/python3" \
    "$ROOT/../.qa-venv/bin/python3"; do
    if [[ -x "$candidate" ]]; then
      PYTHON_BIN="$candidate"
      break
    fi
  done
  PYTHON_BIN="${PYTHON_BIN:-python3}"
fi
export USP_CONFIG="${USP_CONFIG:-$ROOT/firmware/ukraine-status-panel.yaml}"
export USP_ADDRESS="${USP_ADDRESS:-ukraine-status-panel.local}"
cd "$ROOT/tests"
run() {
  printf '\n==> %s\n' "$1"
  "$PYTHON_BIN" "$1"
}

SETTLE_S="${USP_REGRESSION_SETTLE_S:-4}"
RUN_LIVE="${USP_RUN_LIVE:-0}"
run_live() {
  if [[ "$RUN_LIVE" != "1" ]]; then
    printf '\n==> %s [SKIP: set USP_RUN_LIVE=1 for physical-device QA]\n' "$1"
    return 0
  fi
  run "$1"
  sleep "$SETTLE_S"
}
run_live_no_settle() {
  if [[ "$RUN_LIVE" != "1" ]]; then
    printf '\n==> %s [SKIP: set USP_RUN_LIVE=1 for physical-device QA]\n' "$1"
    return 0
  fi
  run "$1"
}
run periodic-ui-gate-static.py
run wifi-icon-static.py
run battery-support-static.py
run battery-model-native.py
run lvgl-bootstrap-codegen.py
run lvgl-2026-10-host-debug-static.py
run palette-redraw-path-static.py
run map-render-batching-static.py
run dashboard-cooperative-refresh-static.py
run alert-render-refresh-gate-static.py
run lvgl-startup-profile-static.py
run lvgl-native-resolution-static.py
run lvgl-refresh-phase-profile-static.py
run lvgl-refresh-profile-native.py
run lvgl-native-copy-policy-native.py
run lvgl-allocation-recovery-native.py
run lvgl-direct-sync-copy-native.py
run lvgl-native-copy-dma-static.py
run qa-dma-timeout-lifetime-native.py
run hardware-probe-journal-test.py
run progress-state-native.py
run checkpoint-writer-native.py
run work-checkpoint-native.py
run lvgl-startup-handoff-static.py
run lvgl-home-card-handoff-static.py
run lvgl-first-paint-sync-static.py
run lvgl-home-transparent-first-paint-static.py
run lvgl-home-frame-cache-static.py
run lvgl-home-frame-resident-static.py
run lvgl-home-fast-first-paint-static.py
run home-cache-freshness-static.py
run home-text-delivery-static.py
run weather-lcd-delivery-static.py
run indoor-lcd-delivery-static.py
run indoor-lcd-delivery-native.py
run rgb-display-stability-static.py
run rgb-cache-line-config.py
run rgb-timeout-recovery-static.py
run lvgl-display-failure-static.py
run dashboard-map-single-region-refresh-static.py
run shared-settings-backend-static.py
run hardware-options-static.py
run optional-sht4x-state-model.py
run optional-sht4x-reprobe-native.py
run optional-sht4x-conversion-native.py
run optional-sht4x-reprobe-static.py
run weather-indoor-static.py
run room-history-model.py
run room-history-native.py
run room-history-time-order-native.py
run room-history-static.py
run room-aggregate-history-native.py
run room-cursor-migration-native.py
run room-clock-recovery-native.py
run room-clock-recovery-static.py
run nvs-startup-preserve-static.py
run nvs-startup-preserve-native.py
run room-aggregate-history-static.py
run history-persistence-native.py
run history-subgroup-native.py
run history-date-layout-static.py
run room-chart-model.py
run semantic-production-native.py
run room-page-static.py
run weather-no-scroll-static.py
run weather-card-style-static.py
run weather-touch-layout.py
run weather-touch-lvgl-native.py
run power-touch-layout.py
run power-touch-lvgl-native.py
run settings-touch-layout.py
run settings-touch-lvgl-native.py
run ui-touch-diagnostic-static.py
run lcd-navigation-static.py
run navigation-text-catalog-native.py
run footer-navigation-text-catalog-native.py
run fullscreen-context-text-catalog-native.py
run settings-text-catalog-native.py
run history-text-catalog-native.py
run diagnostics-text-catalog-native.py
run diagnostics-runtime-text-native.py
run optional-hardware-text-native.py
run indoor-entry-text-native.py
run weather-page-text-native.py
run map-page-text-catalog-native.py
run power-page-text-catalog-native.py
run home-page-text-catalog-native.py
run sound-controls-text-catalog-native.py
run alarm-controls-text-catalog-native.py
run room-ui-text-catalog-native.py
run clock-ui-text-catalog-native.py
run source-status-text-catalog-native.py
run room-summary-text-native.py
run spotify-page-static.py
run spotify-pkce-static.py
run spotify-pkce-native.py
run spotify-auth-return-static.py
run spotify-auth-return-native.py
run spotify-auth-adapter-static.py
run spotify-auth-adapter-native.py
run spotify-session-vault-native.py
run spotify-session-lifecycle-native.py
run spotify-session-coordinator-native.py
run spotify-session-persistence-static.py
run spotify-local-session-key-native.py
run spotify-flash-stack-static.py
run spotify-flash-stack-native.py
run spotify-token-worker-native.py
run spotify-token-worker-static.py
run spotify-playback-model-native.py
run spotify-playback-model-static.py
run spotify-player-transport-native.py
run spotify-player-transport-static.py
run spotify-playback-worker-native.py
run spotify-playback-worker-static.py
run spotify-playback-presentation-native.py
run spotify-lcd-helper-native.py
run spotify-controls-static.py
run spotify-stage4-native.py
run spotify-stage4-static.py
run spotify-stage4-lcd-static.py
run spotify-artwork-plan-native.py
run spotify-artwork-transport-native.py
run spotify-artwork-frames-native.py
run spotify-artwork-decoder-native.py
run spotify-artwork-decoder-static.py
run spotify-artwork-cache-native.py
run spotify-artwork-lcd-native.py
run spotify-playback-presentation-static.py
run spotify-auth-page-static.py
run lcd-map-expand-static.py
run lcd-fullscreen-navigation-static.py
run swipe-navigation-native.py
run touch-event-time-native.py
run gt911-fault-native.py
run touch-event-time-static.py
run i18n-glyph-coverage-static.py
run clock-presentation-native.py
run header-time-validity-static.py
run home-header-layout.py
run home-power-text-layout.py
run home2-primary-state-static.py
run alarm-schedule-native.py
run alarm-kyiv-timezone-native.py
run alarm-controls-static.py
run alarm-audio-policy-native.py
run alarm-switch-boot-native.py
run clock-page-static.py
run screen-transition-static.py
run stripe-bandwidth-static.py
run stripe-bandwidth-native.py
run project-header-closure-static.py
run neptun-primary-static.py
run neptun-scanner-native.py
run neptun-threat-body-limit-native.py
run neptun-threat-body-cap-native.py
run region-preferences-native.py
run neptun-only-static.py
run neptun-refresh-native.py
run starlink-gps-native.py
run shelly-em50-static.py
run outage-calendar-native.py
run stale-wrap-native.py
run outage-schema-native.py
run outage-group-scanner-native.py
run outage-yasno-schema-static.py
run home-cache-context-native.py
run neptun-threat-points-native.py
run neptun-threat-icons-static.py
run alert-source-policy-native.py
if [[ -n "${USP_LVGL_ROOT:-}" ]]; then
  run lcd-navigation-lvgl-native.py
else
  printf '\n==> LVGL pointer host check [SKIP: set USP_LVGL_ROOT to real LVGL sources]\n'
fi
run weather-integrity-native.py
run weather-snapshot-native.py
run weather-current-native.py
run weather-time-context-native.py
run weather-view-native.py
run weather-current-static.py
run weather-refresh-gate-native.py
run weather-refresh-gate-static.py
run weather-forecast-ui-static.py
run weather-details-static.py
run weather-details-native.py
run weather-icons-static.py
run weather-conditions-native.py
run weather-time-native.py
run weather-geocoder-native.py
run weather-geocoder-static.py
run weather-snapshot-static.py
run web-ui-safe-surface-static.py
run web-security-debug-static.py
run web-r07-semantics-static.py
run local-password-recovery-native.py
run web-credential-ownership-native.py
run web-origin-policy-native.py
run web-status-diagnostics-static.py
run web-ui-custom-static.py
run spotify-web-status-static.py
run web-sse-psram-buffer-static.py
run json-serialization-buffer-psram-static.py
run interactive-preference-sync-static.py
run web-ui-build-static.py
run web-ui-size-budget-native.py
run web-viewport-static.py
run web-ui-static-source-native.py
run web-safe-config-static.py
run web-safe-restore-static.py
run web-reset-flows-static.py
run ota-readiness-static.py
run boot-health-policy-host.py
run ota-compat-contract-static.py
run ota-signature-static.py
run ota-trust-status-static.py
run ota-device-verifier-static.py
run ota-version-policy-host.py
run ota-esp32-rng-static.py
run ota-device-manifest-static.py
run ota-device-image-static.py
run ota-image-stream-model.py
run ota-candidate-session-static.py
run ota-candidate-session-model.py
run ota-web-preflight-static.py
run ota-web-session-static.py
run ota-session-expiry-static.py
run ota-guard-concurrency-native.py
run ota-guard-concurrency-static.py
run ota-rate-limit-static.py
run ota-rate-limit-model.py
run ota-commit-arm-static.py
run ota-commit-arm-model.py
run ota-writer-transaction-static.py
run ota-writer-transaction-model.py
run ota-writer-web-static.py
run ota-writer-web-model.py
run ota-writer-recovery-static.py
run ota-writer-recovery-model.py
run ota-flash-backend-static.py
run ota-write-pipeline-static.py
run ota-write-pipeline-model.py
run ota-post-write-validator-static.py
run ota-post-write-validator-model.py
run ota-activation-policy-static.py
run ota-activation-policy-model.py
run ota-activation-ticket-static.py
run ota-activation-ticket-model.py
run ota-boot-activation-backend-static.py
run ota-boot-activation-backend-model.py
run ota-activation-coordinator-static.py
run ota-activation-coordinator-model.py
run ota-rollback-health-policy-static.py
run ota-rollback-health-policy-model.py
run ota-rollback-confirm-backend-static.py
run ota-rollback-confirm-backend-model.py
run ota-rollback-confirm-coordinator-static.py
run ota-rollback-confirm-coordinator-model.py
run web-config-roundtrip-static.py
run web-integrations-static.py
run web-location-static.py
run web-wifi-scan-static.py
run wifi-scan-loop-native.py
run wifi-scan-results-native.py
run wifi-scan-power-native.py
run web-ssid-bidi-static.py
run web-address-privacy-static.py
run web-network-apply-static.py
run secret-text-native.py
run secret-text-contract-static.py
run network-https-gate-static.py
run http-retry-after-native.py
run http-retry-after-static.py
run network-gate-priority-native.py
run network-heap-pressure-static.py
run web-ui-ux-static.py
run web-ui-r10-coverage-static.py
run web-layout-static.py
run design-system-m3-static.py
run design-system-operate-static.py
run design-polish-v2-style-static.py
run docs-baseline-consistency-static.py
run captive-portal-no-ota-static.py
run production-api-profile-static.py
run public-history-mutation-surface-static.py
run production-build-guard-static.py
run source-release-portability.py
run live-probe-version-safety-static.py
run static-test-version-safety.py
run firmware-version-change-native.py
run regression-live-gate-static.py
run ci-regression-dependency.py
run ci-release-integrity-native.py
run confirmation-native.py
run review-safety-config.py
run digest-auth-state-native.py
run digest-session-native.py
run digest-auth-request-native.py
run web-auth-rejection-native.py
run digest-auth-replay-static.py
run button-text-generator-exec.py
run ../scripts/validate-web-manifest.py
run web-first-run-static.py
run public-release-metadata-static.py
run public-release-image-native.py
run public-candidate-artifact-native.py
run web-installer-design-static.py
run web-installer-runtime-static.py
run fullscreen-fast-assets-equivalence.py
run fullscreen-tiled-assets-static.py
run fullscreen-tiled-outline-equivalence.py
run fullscreen-cache-equivalence.py
run fullscreen-cold-native.py
run fullscreen-cold-static.py
run dashboard-map-cache-static.py
run dashboard-map-dirty-area-native.py
run dashboard-map-partial-invalidation-static.py
run alert-region-batch-native.py
run alert-region-worker-static.py
run map-color-shared-static.py
run map-color-shared-native.py
run map-render-primitives-shared-native.py
run map-run-span-shared-native.py
run map-present-gate-native.py
run map-present-gate-static.py
run map-cold-fragment-shared-native.py
run map-run-painter-shared-native.py
run dashboard-home-localization-gate-static.py
run dashboard-home-render-mask-static.py
run dashboard-cache-equivalence.py
run analyze_map_geometry_current.py
run room-period-chart-native.py
run room-period-text-native.py
run room-period-ui-static.py
run restart-confirm-text-catalog-native.py
run regression-critical-registration-static.py

run_live shared-settings-live-probe.py
run_live web-ui-hardware-probe.py
run_live web-config-persistence-live-probe.py
run_live web-ui-custom-live-probe.py
run_live web-wifi-scan-live-probe.py
run_live web-sse-memory-live-probe.py
run_live web-status-diagnostics-live-probe.py
run_live optional-hardware-live-probe.py
run_live r48_ui_page_smoke.py
run_live ui-regression-live-v2.py
run_live semantic-region-status-live-probe.py
run_live region-threat-ui-live-probe.py
run_live palette-live-probe.py
run_live neptun-status-palette-live-probe.py
run_live_no_settle r48_runtime_stability.py
if [[ "${USP_RUN_ALERT_CACHE:-0}" == "1" ]]; then
  run_live dashboard-alert-cache-live-probe.py
  run_live_no_settle r48_runtime_stability.py
fi
if [[ "${USP_RUN_BROWSER:-0}" == "1" ]]; then
  if [[ -z "${PLAYWRIGHT_BROWSERS_PATH:-}" ]] &&       [[ -d "$ROOT/../.qa-tools/playwright" ]]; then
    export PLAYWRIGHT_BROWSERS_PATH="$ROOT/../.qa-tools/playwright"
  fi
  "${NODE_BIN:-$ROOT/../.qa-tools/node/bin/node}" "$ROOT/tests/spotify-auth-return-browser.cjs"
  "${NODE_BIN:-$ROOT/../.qa-tools/node/bin/node}" "$ROOT/tests/spotify-auth-page-browser.cjs"
  for spotify_source in firmware/webui/usp-web-ui.src.js firmware/webui/usp-web-ui.js; do
    "${NODE_BIN:-$ROOT/../.qa-tools/node/bin/node}" "$ROOT/tests/spotify-web-status-browser.cjs" --script "$ROOT/$spotify_source"
  done
  bash "$ROOT/scripts/test-web-forms.sh"
  bash "$ROOT/scripts/test-web-layout.sh"
  PLAYWRIGHT_BROWSERS_PATH="${PLAYWRIGHT_BROWSERS_PATH:-$ROOT/../.qa-tools/playwright}" "${NODE_BIN:-$ROOT/../.qa-tools/node/bin/node}" "$ROOT/tests/web-installer-design-browser.cjs"
  PORTAL_NODE="$(command -v node || true)"
  if [[ -z "$PORTAL_NODE" ]]; then
    PORTAL_NODE="$ROOT/../.qa-tools/node/bin/node"
  fi
  "$PORTAL_NODE" "$ROOT/tests/captive-portal-ui-browser.cjs"
else
  printf '\n==> Web form browser checks [SKIP: set USP_RUN_BROWSER=1]\n'
fi
printf '\nRegression suite: PASS\n'
