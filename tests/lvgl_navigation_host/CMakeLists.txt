cmake_minimum_required(VERSION 3.16)
project(usp_navigation_native LANGUAGES C CXX)
option(USP_SANITIZE "Enable host memory and undefined-behavior checks" ON)
if(USP_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
add_subdirectory("${USP_LVGL_ROOT}" lvgl)
add_executable(navigation navigation.cpp)
target_include_directories(navigation PRIVATE
  "${USP_NAVIGATION_HEADER_DIR}"
  "${USP_PROJECT_ROOT}/include"
  shims)
target_compile_features(navigation PRIVATE cxx_std_17)
target_link_libraries(navigation PRIVATE lvgl m)

add_executable(clock_map_mirror clock_map_mirror.cpp)
target_include_directories(clock_map_mirror PRIVATE
  "${USP_PROJECT_ROOT}/include" shims)
target_compile_features(clock_map_mirror PRIVATE cxx_std_17)
target_link_libraries(clock_map_mirror PRIVATE lvgl m)

add_executable(transition_probe transition_probe.cpp)
target_include_directories(transition_probe PRIVATE
  "${USP_PROJECT_ROOT}/include" shims)
target_compile_features(transition_probe PRIVATE cxx_std_17)
target_link_libraries(transition_probe PRIVATE lvgl m)

add_executable(display_boot display_boot.cpp)
target_compile_features(display_boot PRIVATE cxx_std_17)
target_link_libraries(display_boot PRIVATE lvgl m)

add_executable(alarm_days alarm_days.cpp)
target_include_directories(alarm_days PRIVATE "${USP_PROJECT_ROOT}/include")
target_compile_features(alarm_days PRIVATE cxx_std_20)
target_compile_options(alarm_days PRIVATE -Werror=deprecated-enum-enum-conversion)
target_link_libraries(alarm_days PRIVATE lvgl m)

add_executable(theme_integrity theme_integrity.cpp)
target_compile_features(theme_integrity PRIVATE cxx_std_20)
target_link_libraries(theme_integrity PRIVATE lvgl m)

add_executable(button_alignment button_alignment.cpp)
target_include_directories(button_alignment PRIVATE "${USP_PROJECT_ROOT}/include")
target_compile_features(button_alignment PRIVATE cxx_std_20)
target_link_libraries(button_alignment PRIVATE lvgl m)

if(USP_WEATHER_TOUCH_FIXTURE_DIR)
  add_executable(weather_touch weather_touch.cpp)
  target_include_directories(weather_touch PRIVATE
    "${USP_PROJECT_ROOT}/include" "${USP_WEATHER_TOUCH_FIXTURE_DIR}" shims)
  target_compile_features(weather_touch PRIVATE cxx_std_17)
  target_link_libraries(weather_touch PRIVATE lvgl m)
endif()

if(USP_WEATHER_WRAP_FIXTURE_DIR)
  add_executable(weather_wrap weather_wrap.cpp)
  target_include_directories(weather_wrap PRIVATE "${USP_WEATHER_WRAP_FIXTURE_DIR}")
  target_compile_features(weather_wrap PRIVATE cxx_std_17)
  target_link_libraries(weather_wrap PRIVATE lvgl m)
endif()

if(USP_WEATHER_FORECAST_WRAP_FIXTURE_DIR)
  add_executable(weather_forecast_wrap weather_forecast_wrap.cpp)
  target_include_directories(weather_forecast_wrap PRIVATE "${USP_WEATHER_FORECAST_WRAP_FIXTURE_DIR}")
  target_compile_features(weather_forecast_wrap PRIVATE cxx_std_17)
  target_link_libraries(weather_forecast_wrap PRIVATE lvgl m)
endif()

if(USP_POWER_TOUCH_FIXTURE_DIR)
  add_executable(power_touch power_touch.cpp)
  target_include_directories(power_touch PRIVATE
    "${USP_PROJECT_ROOT}/include" "${USP_POWER_TOUCH_FIXTURE_DIR}" shims)
  target_compile_features(power_touch PRIVATE cxx_std_17)
  target_link_libraries(power_touch PRIVATE lvgl m)
endif()

if(DEFINED USP_SETTINGS_TOUCH_FIXTURE_DIR)
  add_executable(settings_touch settings_touch.cpp)
  target_include_directories(settings_touch PRIVATE
    "${USP_PROJECT_ROOT}/include" "${USP_SETTINGS_TOUCH_FIXTURE_DIR}" shims)
  target_compile_features(settings_touch PRIVATE cxx_std_17)
  target_link_libraries(settings_touch PRIVATE lvgl m)
endif()

add_executable(touch_fault touch_fault.cpp)
target_include_directories(touch_fault PRIVATE "${USP_NAVIGATION_HEADER_DIR}" "${USP_PROJECT_ROOT}/include" shims)
target_compile_features(touch_fault PRIVATE cxx_std_17)
target_link_libraries(touch_fault PRIVATE lvgl m)

add_executable(wifi_icon wifi_icon.cpp)
target_include_directories(wifi_icon PRIVATE "${USP_PROJECT_ROOT}/include")
target_compile_features(wifi_icon PRIVATE cxx_std_17)
target_link_libraries(wifi_icon PRIVATE lvgl m)

add_executable(settings_sections settings_sections.cpp)
target_include_directories(settings_sections PRIVATE "${USP_PROJECT_ROOT}/include" shims)
target_compile_features(settings_sections PRIVATE cxx_std_17)
target_link_libraries(settings_sections PRIVATE lvgl m)
