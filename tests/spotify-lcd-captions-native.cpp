#include "playback_captions.h"
#include <cassert>
#include <cstring>
#include <iostream>
using usp_spotify_presentation::Mode;
struct Expected {const char *status[3];const char *hint[3];uint32_t color;};
constexpr Expected REFERENCE[] = {
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

/** Active track/episode and inactive captions retain the accepted .327 wording. */
void content_cases() {
  usp_spotify_presentation::View view{};
  for(unsigned language=0;language<256;++language) {
    auto text=usp_spotify_captions::describe(view,language);
    assert(std::strcmp(text.device,"—")==0 && std::strcmp(text.play,"Play")==0);
    assert(std::strstr(text.context.data(),"—")!=nullptr);
  }
  view.mode=Mode::PLAYING;view.playing=true;
  std::strcpy(view.title.data(),"Пісня");
  std::strcpy(view.subtitle.data(),"Artist");
  std::strcpy(view.context.data(),"Album");
  std::strcpy(view.device.data(),"Kitchen");
  auto text=usp_spotify_captions::describe(view,2);
  assert(std::strcmp(text.track,"Пісня")==0 && std::strcmp(text.artist,"Artist")==0);
  assert(std::strcmp(text.context.data(),"Album: Album")==0 && std::strcmp(text.play,"Pause")==0);
  view.item_type=usp_spotify_playback::ItemType::EPISODE;view.subtitle[0]=0;
  text=usp_spotify_captions::describe(view,2);
  assert(std::strcmp(text.artist,"Podcast episode")==0 && std::strcmp(text.context.data(),"Show: Album")==0);
  view.mode=Mode::DISCONNECTED;
  text=usp_spotify_captions::describe(view,2);
  assert(std::strcmp(text.track,"Spotify is not connected")==0);
  assert(std::strcmp(text.play,"Play")==0);
  assert(std::strcmp(text.device,"—")==0);
}
int main() {
  content_cases();
  for(unsigned mode=0;mode<9;++mode)for(unsigned lang=0;lang<256;++lang) {
    const auto text=usp_spotify_captions::status(static_cast<Mode>(mode),lang);
    const unsigned expected_language=lang<3?lang:0;
    assert(std::strcmp(text.status,REFERENCE[mode].status[expected_language])==0);
    assert(std::strcmp(text.hint,REFERENCE[mode].hint[expected_language])==0);
    assert(text.color==REFERENCE[mode].color);
  }
  const auto invalid=usp_spotify_captions::status(static_cast<Mode>(255),2);
  assert(std::strcmp(invalid.status,REFERENCE[0].status[2])==0);
  std::cout<<"SPOTIFY_CAPTION_PREFLIGHT=PASS modes9 languages256 exact327_text_colors invalid_mode no_provider_device\n";
}
