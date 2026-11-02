#pragma once
#include "control_view.h"

namespace usp_spotify_controls {
/** Short messages describe submission and observation without claiming playback changed. */
inline const char *hint(const ControlView &view,unsigned language,const char *fallback) {
  const unsigned lang=language<3?language:0;
  static constexpr const char *MESSAGES[][3]={
    {"Надсилання команди…","Отправка команды…","Sending command…"},
    {"Оновлення стану…","Обновление состояния…","Updating playback state…"},
    {"Відповідь не підтверджено · перевірте стан","Ответ не подтверждён · проверьте состояние","Reply unconfirmed · check playback state"},
    {"Команду не надіслано · натисніть знову","Команда не отправлена · нажмите снова","Command not sent · tap again"},
    {"Spotify відхилив команду","Spotify отклонил команду","Spotify rejected this command"},
    {"Пристрій недоступний · відкрийте Spotify","Устройство недоступно · откройте Spotify","Device unavailable · open Spotify"},
    {"Spotify просить зачекати","Spotify просит подождать","Spotify requested a retry delay"},
    {"Керування активним пристроєм Spotify","Управление активным устройством Spotify","Control the active Spotify device"},
  };
  if(view.phase!=Phase::IDLE)return MESSAGES[0][lang];
  if(view.outcome==Outcome::UNCERTAIN)return MESSAGES[2][lang];
  if(view.outcome==Outcome::EXPIRED || view.outcome==Outcome::NOT_SENT)
    return MESSAGES[3][lang];
  if(view.outcome==Outcome::FORBIDDEN)return MESSAGES[4][lang];
  if(view.outcome==Outcome::UNAVAILABLE)return MESSAGES[5][lang];
  if(view.allowed_mask)return MESSAGES[7][lang];
  if(view.outcome==Outcome::RATE_LIMITED)return MESSAGES[6][lang];
  if(view.outcome==Outcome::ACCEPTED)return MESSAGES[1][lang];
  return fallback;
}
}  // namespace usp_spotify_controls
