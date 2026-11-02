"""Execute actual Home cache methods with controlled display/DMA boundaries."""

import argparse
import subprocess
import tempfile
from pathlib import Path

from loguru import logger

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define YESNO(x) ((x) ? "YES" : "NO")
struct lv_obj_t { bool hidden{true}; };
struct lv_display_t { lv_obj_t *active{nullptr}; };
struct lv_event_t { void *user{nullptr}; };
constexpr int LV_OBJ_FLAG_HIDDEN=1;
static uint32_t micros() { static uint32_t tick=0; return ++tick; }
static uint32_t millis() { return micros()/1000; }
static void *lv_event_get_user_data(lv_event_t *event) { return event->user; }
static lv_obj_t *lv_display_get_screen_active(lv_display_t *d) { return d->active; }
static bool lv_obj_has_flag(lv_obj_t *o,int) { return o->hidden; }
static int releases=0;
static void lv_display_flush_ready(lv_display_t *) { ++releases; }
namespace esphome::mipi_rgb {
struct MipiRgb {
  uint16_t a[4]{1,2,3,4}, b[4]{}, c[4]{};
  bool buffers{true}, complete{true}, submit{true};
  int presents{0};
  bool get_native_frame_buffers(void **x,void **y,void **z) {
    *x=a;*y=b;*z=c;return buffers;
  }
  bool wait_native_frame_complete() { return complete; }
  bool present_native_frame(void *,int,int,int,int) { ++presents;return submit; }
};
}
namespace esphome::lvgl {
struct Profile { void ready(uint32_t) {} void render_start(uint32_t) {} };
class LvglComponent {
 public:
  lv_display_t *disp_{nullptr};
  std::vector<void *> displays_;
  bool paused_{false},qa_frame_cache_hold_{false},qa_native_resident_hold_{false};
  void *last_presented_frame_{nullptr};
  bool last_presented_frame_complete_{true};
  void *home_native_frame_cache_{nullptr};
  size_t home_native_frame_cache_bytes_{0};
  bool home_native_frame_cache_valid_{false};
  uint32_t home_native_frame_cache_generation_{0};
  uint64_t home_native_frame_data_key_{0};
  uint64_t home_native_frame_cache_data_key_{0};
  lv_obj_t *home_cache_root_{nullptr},*home_cache_menu_{nullptr},*home_cache_alarm_{nullptr};
  bool home_render_context_clean_{false},home_last_render_context_clean_{false};
  bool home_render_pending_{false};
  Profile refresh_profile_;
  uint16_t get_width() const { return 2; }
  uint16_t get_height() const { return 2; }
  bool refresh_home_native_frame_cache();
  bool present_home_native_frame_cache();
  void set_home_native_frame_data_key(uint64_t key) {
    if (home_native_frame_data_key_ == key) return;
    home_native_frame_data_key_ = key;
    home_native_frame_cache_valid_ = false;
  }
  void set_home_native_frame_context(lv_obj_t *,lv_obj_t *,lv_obj_t *);
  bool home_cache_context_clean_(bool capture) const;
  void home_cache_render_started_();
  void home_cache_render_ready_();
  void *home_cache_native_buffer_(esphome::mipi_rgb::MipiRgb *);
  bool home_cache_wait_frame_(esphome::mipi_rgb::MipiRgb *);
  bool home_cache_present_buffer_(esphome::mipi_rgb::MipiRgb *,uint32_t);
  static void profile_render_start_cb(lv_event_t *);
  static void profile_render_end_cb(lv_event_t *);
};
"""
CASES = r"""
}
using namespace esphome::lvgl;
struct Fixture {
  esphome::mipi_rgb::MipiRgb rgb;
  lv_obj_t home,other,menu,alarm;
  lv_display_t display{&home};
  LvglComponent comp;
  Fixture() {
    comp.disp_=&display;comp.displays_={&rgb};comp.last_presented_frame_=rgb.a;
#ifdef HAS_CONTEXT
    comp.set_home_native_frame_context(&home,&menu,&alarm);
#endif
  }
  void render(lv_obj_t *root) {
    display.active=root;
    comp.last_presented_frame_=rgb.a;
    lv_event_t event{&comp};
    LvglComponent::profile_render_start_cb(&event);
    LvglComponent::profile_render_end_cb(&event);
  }
};
static void capture_context() {
  Fixture f;
  f.render(&f.home);
  lv_event_t idle{&f.comp};
  LvglComponent::profile_render_end_cb(&idle);
  assert(f.comp.refresh_home_native_frame_cache());
  assert(std::memcmp(f.rgb.a,f.rgb.c,sizeof(f.rgb.a))==0);
  f.menu.hidden=false;
  f.render(&f.home);
  assert(!f.comp.refresh_home_native_frame_cache());
  f.menu.hidden=true;
  assert(!f.comp.refresh_home_native_frame_cache());
  f.render(&f.other);
  assert(!f.comp.refresh_home_native_frame_cache());
  f.render(&f.home);
  assert(f.comp.refresh_home_native_frame_cache());
  f.alarm.hidden=false;
  f.render(&f.home);
  assert(!f.comp.refresh_home_native_frame_cache());
  f.alarm.hidden=true;
  assert(!f.comp.refresh_home_native_frame_cache());
  f.render(&f.home);
  assert(f.comp.refresh_home_native_frame_cache());
}
static void presentation_context() {
  Fixture f;
  f.render(&f.home);
  assert(f.comp.refresh_home_native_frame_cache());
  f.display.active=&f.other;
  f.menu.hidden=false;
  assert(!f.comp.present_home_native_frame_cache());
  f.menu.hidden=true;
  f.alarm.hidden=false;
  assert(!f.comp.present_home_native_frame_cache());
  assert(f.rgb.presents==0);
  f.alarm.hidden=true;
  assert(f.comp.present_home_native_frame_cache());
  assert(f.rgb.presents==1);
  assert(f.comp.last_presented_frame_==f.rgb.c);
}
static void same_key_render_preserves_cache() {
  Fixture f;
  f.comp.set_home_native_frame_data_key(0x100000001ULL);
  f.render(&f.home);
  assert(f.comp.refresh_home_native_frame_cache());
  assert(f.rgb.c[0] == 1);
  // A clean render with the same data/minute key must not destroy the fast
  // resident frame; only a changed data key or C-D2 context may reject it.
  f.rgb.a[0] = 9;
  f.render(&f.home);
  assert(f.comp.present_home_native_frame_cache());
  assert(f.rgb.presents == 1);
}

static void data_key_freshness() {
  Fixture f;
  f.comp.set_home_native_frame_data_key(0x100000001ULL);
  f.render(&f.home);
  assert(f.comp.refresh_home_native_frame_cache());
  assert(f.comp.present_home_native_frame_cache());
  const int presented = f.rgb.presents;
  f.comp.set_home_native_frame_data_key(0x200000001ULL);
  assert(!f.comp.present_home_native_frame_cache());
  assert(f.rgb.presents == presented);
  f.render(&f.home);
  assert(f.comp.refresh_home_native_frame_cache());
  assert(f.comp.present_home_native_frame_cache());
}

static void dma_and_readiness() {
  Fixture f;
  f.render(&f.home);
  const int initial=releases;
  f.comp.last_presented_frame_complete_=false;
  f.rgb.complete=false;
  assert(!f.comp.refresh_home_native_frame_cache());
  assert(releases==initial);
  f.rgb.complete=true;
  assert(f.comp.refresh_home_native_frame_cache());
  assert(releases==initial+1);
  assert(f.comp.refresh_home_native_frame_cache());
  assert(releases==initial+1);
  f.comp.last_presented_frame_complete_=false;
  f.rgb.complete=false;
  assert(!f.comp.present_home_native_frame_cache());
  assert(f.rgb.presents==0 && releases==initial+1);
  f.rgb.complete=true;f.rgb.submit=false;
  assert(!f.comp.present_home_native_frame_cache());
  assert(releases==initial+2 && f.rgb.presents==1);
  assert(f.comp.last_presented_frame_==f.rgb.a);
  f.comp.paused_=true;
  assert(!f.comp.refresh_home_native_frame_cache());
  f.comp.paused_=false;f.comp.qa_frame_cache_hold_=true;
  assert(!f.comp.refresh_home_native_frame_cache());
  f.comp.qa_frame_cache_hold_=false;f.comp.qa_native_resident_hold_=true;
  assert(!f.comp.refresh_home_native_frame_cache());
  f.comp.qa_native_resident_hold_=false;f.rgb.buffers=false;
  assert(!f.comp.refresh_home_native_frame_cache());
}
static void transitions_during_render() {
  Fixture f;
  lv_event_t event{&f.comp};
  LvglComponent::profile_render_start_cb(&event);
  f.menu.hidden=false;
  LvglComponent::profile_render_end_cb(&event);
  f.menu.hidden=true;
  assert(!f.comp.refresh_home_native_frame_cache());
  f.render(&f.home);
  f.display.active=&f.other;
  assert(!f.comp.refresh_home_native_frame_cache());
  f.display.active=&f.home;
  f.comp.home_cache_root_=nullptr;
  assert(!f.comp.refresh_home_native_frame_cache());
  assert(!f.comp.present_home_native_frame_cache());
}
int main() {
  capture_context();
  presentation_context();
  same_key_render_preserves_cache();
  data_key_freshness();
  dma_and_readiness();
  transitions_during_render();
  std::cout<<"HOME_CACHE_CONTEXT=PASS actual methods capture/present overlays "
             "render_transition DMA_ownership exactly_once_flush ASAN_UBSAN"<<std::endl;
}
"""


def method(source, name):
    """Extract an exact implementation with its existing return type."""
    position = source.index("LvglComponent::" + name + "(")
    start = source.rfind("\n", 0, position) + 1
    opened = source.index("{", position)
    depth, end = 1, opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def native_source():
    """Execute production methods; display and DMA are explicit test doubles."""
    source = (ROOT / "components/lvgl/lvgl_esphome.cpp").read_text()
    names = [
        "refresh_home_native_frame_cache",
        "present_home_native_frame_cache",
        "profile_render_start_cb",
        "profile_render_end_cb",
    ]
    optional = [
        "set_home_native_frame_context",
        "home_cache_context_clean_",
        "home_cache_render_started_",
        "home_cache_render_ready_",
        "home_cache_native_buffer_",
        "home_cache_wait_frame_",
        "home_cache_present_buffer_",
    ]
    names += [
        name for name in optional if "LvglComponent::" + name + "(" in source
    ]
    flag = "#define HAS_CONTEXT\n" if optional[0] in names else ""
    return (
        flag
        + FIXTURE
        + "".join(method(source, name) for name in names)
        + CASES
    )


def main():
    """Run server-only cache checks with ASAN/UBSAN; no device access."""
    argparse.ArgumentParser(description=__doc__).parse_args()
    with tempfile.TemporaryDirectory(prefix="usp-home-context-") as name:
        work = Path(name)
        source, binary = work / "test.cpp", work / "test"
        source.write_text(native_source())
        subprocess.run(
            [
                "g++",
                "-std=c++20",
                "-g",
                "-fsanitize=address,undefined",
                "-fno-pie",
                "-no-pie",
                str(source),
                "-o",
                str(binary),
            ],
            check=True,
        )
        result = subprocess.run([str(binary)], check=False)
        logger.info("Actual Home cache exit: {}", result.returncode)
        raise SystemExit(0 if result.returncode == 0 else 1)


if __name__ == "__main__":
    main()
