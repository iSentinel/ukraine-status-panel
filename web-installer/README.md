# ESP Web Tools installer

**UA:** Підключіть Waveshare ESP32-S3-Touch-LCD-7B через USB, відкрийте `index.html` через HTTPS-сайт GitHub Pages і натисніть кнопку встановлення. Після прошивання налаштуйте Wi-Fi через Improv Serial або тимчасову Setup AP.

**RU:** Подключите Waveshare ESP32-S3-Touch-LCD-7B по USB, откройте `index.html` на HTTPS-сайте GitHub Pages и нажмите кнопку установки. После прошивки настройте Wi-Fi через Improv Serial или временную Setup AP.

**EN:** Connect the Waveshare ESP32-S3-Touch-LCD-7B over USB, open `index.html` from the HTTPS GitHub Pages site, and use the install button. Configure Wi-Fi with Improv Serial or the temporary Setup AP after flashing.

`manifest.json` is tracked. `firmware.factory.bin` and `SHA256SUMS` are generated release/Pages assets and are intentionally not committed.

The flashing UI runtime is vendored locally at `vendor/esp-web-tools/10.4.0/`; the installer does not execute it from a CDN. `PROVENANCE.json` records the exact upstream package tarball and SHA256 plus per-file hashes, while `LICENSE` and `THIRD_PARTY_LICENSES.json` retain the upstream notices. Refresh it only with `scripts/vendor-web-installer-runtime.py`; `tests/web-installer-runtime-static.py` verifies the pinned byte hashes and complete local module closure.

`release-status.json` is the publication gate for the exact manifest version. A factory image may be built for CI, but GitHub Pages / tagged public release publication remains blocked until blank or spare hardware completes USB first-flash + provisioning acceptance and `public_first_flash_hardware_validated` is set to `true` for that same version.

**Provisioning timeout / таймаут / тайм-аут:** Setup/Improv is available for 15 minutes after boot. If Wi-Fi is not configured in time, power-cycle the panel and retry. / Setup/Improv доступен 15 минут после загрузки; если не успели настроить Wi-Fi, перезапитайте панель. / Setup/Improv доступний 15 хвилин після завантаження; якщо Wi-Fi не налаштовано вчасно, перезапустіть живлення панелі.

**Public profile security / Безопасность / Безпека:** Public first-flash is USB
provisioning only: Native API and OTA upload are disabled. To install a private
production profile, flash it over USB. No shared network update password or API
key is embedded. / Публичный профиль не открывает Native API и OTA; переход на
личную конфигурацию выполняется через USB. / Публічний профіль не відкриває
Native API та OTA; перехід на власну конфігурацію виконується через USB.
