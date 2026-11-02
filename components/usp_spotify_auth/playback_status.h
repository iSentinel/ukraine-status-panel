#pragma once
#include "playback_presentation.h"
namespace usp_spotify_captions {
struct Text {const char *status; const char *hint; uint32_t color;};
struct Row {const char *status[3]; const char *hint[3]; uint32_t color;};
/** Pick exactly the current LCD strings; language fallback is Ukrainian. */
inline Text status(usp_spotify_presentation::Mode mode, unsigned language) {
  static constexpr Row ROWS[] = {
      {{"НЕ ПІДКЛЮЧЕНО", "НЕ ПОДКЛЮЧЕНО", "NOT CONNECTED"}, {"Підключення через Web UI · OAuth PKCE", "Подключение через Web UI · OAuth PKCE", "Connect in Web UI · OAuth PKCE"}, 0xFACC15},
      {{"ПІДКЛЮЧЕННЯ", "ПОДКЛЮЧЕНИЕ", "CONNECTING"}, {"OAuth PKCE · обмін токена", "OAuth PKCE · обмен токена", "OAuth PKCE · token exchange"}, 0x38BDF8},
      {{"ПОТРІБЕН ВХІД", "ТРЕБУЕТСЯ ВХОД", "SIGN-IN REQUIRED"}, {"Повторіть вхід через Web UI", "Повторите вход через Web UI", "Reconnect from Web UI"}, 0xFB923C},
      {{"НЕМАЄ PLAYER", "НЕТ PLAYER", "NO ACTIVE PLAYER"}, {"Запустіть Spotify на будь-якому пристрої", "Запустите Spotify на любом устройстве", "Start Spotify on any device"}, 0x91A8B2},
      {{"ВІДТВОРЕННЯ", "ВОСПРОИЗВЕДЕНИЕ", "PLAYING"}, {"Лише перегляд · керування вимкнено", "Только просмотр · управление отключено", "Read-only · controls disabled"}, 0x4ADE80},
      {{"ПАУЗА", "ПАУЗА", "PAUSED"}, {"Лише перегляд · керування вимкнено", "Только просмотр · управление отключено", "Read-only · controls disabled"}, 0x38BDF8},
      {{"НЕМАЄ ДОСТУПУ", "НЕТ ДОСТУПА", "FORBIDDEN"}, {"Перевірте доступ акаунта Spotify", "Проверьте доступ аккаунта Spotify", "Check Spotify account access"}, 0xF87171},
      {{"ОЧІКУВАННЯ", "ОЖИДАНИЕ", "RATE LIMITED"}, {"Spotify попросив зачекати", "Spotify попросил подождать", "Spotify requested a retry delay"}, 0xFACC15},
      {{"ТИМЧАСОВА ПОМИЛКА", "ВРЕМЕННАЯ ОШИБКА", "TEMPORARY ERROR"}, {"Повтор автоматично · без керування", "Повтор автоматически · без управления", "Automatic retry · controls disabled"}, 0xFACC15},
  };
  const unsigned index = static_cast<unsigned>(mode);
  const auto &row = ROWS[index < 9 ? index : 0];
  const unsigned lang = language < 3 ? language : 0;
  return {row.status[lang], row.hint[lang], row.color};
}
}  // namespace usp_spotify_captions
