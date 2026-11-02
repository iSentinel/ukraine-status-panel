#pragma once

#include <cstddef>
#include <cstdint>

#include "esphome/components/image/image.h"

namespace usp_map_touch {

/** Describe one visible map mask and its selection target. */
struct RegionMask {
  uint16_t region_id;
  int16_t x;
  int16_t y;
  int16_t center_x;
  int16_t center_y;
  const image::Image *mask;
};

/** Store localized names for one selectable region. */
struct RegionName {
  uint16_t region_id;
  const char *ua;
  const char *ru;
  const char *en;
};

inline uint32_t distance_squared(
    int x, int y, int center_x, int center_y) {
  const int32_t dx = x - center_x;
  const int32_t dy = y - center_y;
  return static_cast<uint32_t>(dx * dx + dy * dy);
}

/**
 * Return the selectable region at a map-local point.
 *
 * The visible binary masks are the source of truth. Adjacent masks can share
 * a few border pixels, so ties are resolved by the nearest mask interior
 * center instead of table order.
 */
inline int32_t hit_test_region(
    int x, int y, const RegionMask *regions, size_t count) {
  int32_t selected = -1;
  uint32_t best_distance = UINT32_MAX;

  for (size_t index = 0; index < count; index++) {
    const auto &region = regions[index];
    const int local_x = x - region.x;
    const int local_y = y - region.y;
    if (region.mask == nullptr ||
        !region.mask->get_pixel(local_x, local_y).is_on()) {
      continue;
    }

    const uint32_t distance = distance_squared(
        x, y, region.center_x, region.center_y);
    if (distance < best_distance) {
      best_distance = distance;
      selected = region.region_id;
    }
  }

  return selected;
}

inline constexpr RegionName REGION_NAMES[] = {
    {3, "Хмельницька обл.", "Хмельницкая обл.", "Khmelnytskyi"},
    {4, "Вінницька обл.", "Винницкая обл.", "Vinnytsia"},
    {5, "Рівненська обл.", "Ровненская обл.", "Rivne"},
    {8, "Волинська обл.", "Волынская обл.", "Volyn"},
    {9, "Дніпропетровська обл.", "Днепропетровская обл.", "Dnipropetrovsk"},
    {10, "Житомирська обл.", "Житомирская обл.", "Zhytomyr"},
    {11, "Закарпатська обл.", "Закарпатская обл.", "Zakarpattia"},
    {12, "Запорізька обл.", "Запорожская обл.", "Zaporizhzhia"},
    {13, "Ів.-Франківська обл.", "Ив.-Франковская обл.", "Ivano-Frankivsk"},
    {14, "Київська обл.", "Киевская обл.", "Kyiv Oblast"},
    {15, "Кіровоградська обл.", "Кировоградская обл.", "Kirovohrad"},
    {16, "Луганська обл.", "Луганская обл.", "Luhansk"},
    {17, "Миколаївська обл.", "Николаевская обл.", "Mykolaiv"},
    {18, "Одеська обл.", "Одесская обл.", "Odesa"},
    {19, "Полтавська обл.", "Полтавская обл.", "Poltava"},
    {20, "Сумська обл.", "Сумская обл.", "Sumy"},
    {21, "Тернопільська обл.", "Тернопольская обл.", "Ternopil"},
    {22, "Харківська обл.", "Харьковская обл.", "Kharkiv"},
    {23, "Херсонська обл.", "Херсонская обл.", "Kherson"},
    {24, "Черкаська обл.", "Черкасская обл.", "Cherkasy"},
    {25, "Чернігівська обл.", "Черниговская обл.", "Chernihiv"},
    {26, "Чернівецька обл.", "Черновицкая обл.", "Chernivtsi"},
    {27, "Львівська обл.", "Львовская обл.", "Lviv"},
    {28, "Донецька обл.", "Донецкая обл.", "Donetsk"},
    {31, "м. Київ", "г. Киев", "Kyiv City"},
    {9999, "АР Крим", "АР Крым", "Crimea"},
};

/** Return a localized name for a selectable region. */
inline const char *region_name(uint16_t region_id, uint8_t language) {
  for (const auto &entry : REGION_NAMES) {
    if (entry.region_id != region_id) {
      continue;
    }
    if (language == 1) {
      return entry.ru;
    }
    if (language == 2) {
      return entry.en;
    }
    return entry.ua;
  }

  if (language == 1) {
    return "Неизвестный регион";
  }
  if (language == 2) {
    return "Unknown region";
  }
  return "Невідомий регіон";
}

}  // namespace usp_map_touch
