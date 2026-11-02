#pragma once
#include "lvgl.h"
#include <cstdlib>

namespace usp_button_label_layout {

struct Position {
  int error_x{}, error_y{};
  bool valid{}, inside{};
};

struct Audit {
  unsigned total{}, offcenter{}, outside{};
};

/** Compare the label and button centers in actual display coordinates. */
inline Position inspect(lv_obj_t *button) {
  Position result;
  if (button == nullptr || lv_obj_get_child_count(button) == 0) return result;
  auto *label = lv_obj_get_child(button, 0);
  if (!lv_obj_check_type(label, &lv_label_class)) return result;
  lv_area_t outer{}, inner{};
  lv_obj_get_coords(button, &outer);
  lv_obj_get_coords(label, &inner);
  result.error_x = inner.x1 + inner.x2 - outer.x1 - outer.x2;
  result.error_y = inner.y1 + inner.y2 - outer.y1 - outer.y2;
  result.inside = inner.x1 >= outer.x1 && inner.x2 <= outer.x2 &&
                  inner.y1 >= outer.y1 && inner.y2 <= outer.y2;
  result.valid = true;
  return result;
}

/** Inspect visible buttons without changing any labels or user settings. */
inline void collect(lv_obj_t *root, Audit &result) {
  if (root == nullptr || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_obj_check_type(root, &lv_button_class)) {
    const auto position = inspect(root);
    if (position.valid) {
      ++result.total;
      if (std::abs(position.error_x) > 1 || std::abs(position.error_y) > 1)
        ++result.offcenter;
      if (!position.inside) ++result.outside;
    }
  }
  for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index)
    collect(lv_obj_get_child(root, index), result);
}

/** Resolve pending layout before collecting read-only geometry evidence. */
inline Audit audit(lv_obj_t *root) {
  Audit result;
  if (root != nullptr) lv_obj_update_layout(root);
  collect(root, result);
  return result;
}

}  // namespace usp_button_label_layout
