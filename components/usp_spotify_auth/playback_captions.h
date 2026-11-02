#pragma once
#include "playback_status.h"
namespace usp_spotify_captions {
/** Borrowed strings are consumed immediately while their source View is alive. */
struct Content {
  Text info{};
  const char *track{nullptr}, *artist{nullptr}, *device{nullptr};
  const char *art{nullptr}, *no_art{nullptr}, *art_hint{nullptr};
  const char *volume_caption{nullptr}, *device_caption{nullptr};
  const char *previous{nullptr}, *play{nullptr}, *next{nullptr};
  std::array<char,192> context{};
};
inline const char *pick(unsigned lang,const char *uk,const char *ru,const char *en) {
  return lang==1?ru:lang==2?en:uk;
}

/** Fixed labels do not depend on playback metadata and never allocate memory. */
inline void fixed(Content &text,unsigned lang,bool playing) {
  text.art=pick(lang,"ОБКЛАДИНКА","ОБЛОЖКА","ALBUM ART");
  text.no_art=pick(lang,"Немає обкладинки","Нет обложки","No artwork");
  text.art_hint=pick(lang,"Завантаження обкладинки вимкнено","Загрузка обложки отключена","Artwork download disabled");
  text.volume_caption=pick(lang,"Гучність","Громкость","Volume");
  text.device_caption=pick(lang,"Пристрій","Устройство","Device");
  text.previous=pick(lang,"Назад","Назад","Prev");
  text.next=pick(lang,"Далі","Далее","Next");
  text.play=playing?pick(lang,"Пауза","Пауза","Pause"):"Play";
}

/** Format only the current view; disconnected states cannot borrow old metadata. */
inline Content describe(const usp_spotify_presentation::View &view,unsigned lang) {
  using usp_spotify_presentation::Mode;
  Content text{};
  text.info=status(view.mode,lang);
  const bool active=view.mode==Mode::PLAYING || view.mode==Mode::PAUSED;
  text.track=active && view.title[0]?view.title.data():
      view.mode==Mode::NO_PLAYER?
      pick(lang,"Немає активного відтворення","Нет активного воспроизведения","No active playback"):
      pick(lang,"Spotify не підключено","Spotify не подключён","Spotify is not connected");
  const bool episode=view.item_type==usp_spotify_playback::ItemType::EPISODE;
  text.artist=active && view.subtitle[0]?view.subtitle.data():
      active && episode?pick(lang,"Епізод подкасту","Эпизод подкаста","Podcast episode"):text.info.hint;
  const char *prefix=episode?pick(lang,"Шоу","Шоу","Show"):pick(lang,"Альбом","Альбом","Album");
  std::snprintf(text.context.data(),text.context.size(),"%s: %s",
                prefix,active && view.context[0]?view.context.data():"—");
  text.device=active && view.device[0]?view.device.data():"—";
  fixed(text,lang,active && view.playing);
  return text;
}
static_assert(sizeof(Content)<=384,"LCD captions must remain bounded");
}  // namespace usp_spotify_captions
