// LVGL 8.3 configuration for the Weather & Pain Station.
// Anything not set here takes LVGL's default (lv_conf_internal.h).
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Colour: RGB565, no byte swap (matches the ESP32-S3 RGB panel driver) */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0

/* Memory: use the C heap, which spills large blocks into PSRAM */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_MEMCPY_MEMSET_STD 1

/* Timing */
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 20
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF 130

/* Drawing */
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_LAYER_SIMPLE_BUF_SIZE (64 * 1024)

/* Logging / debug off in production */
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/* Fonts: our Atkinson Hyperlegible fonts are compiled in from src/fonts.
 * Montserrat 48 is only used for the check-mark symbol on the pain buttons. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_USE_FONT_COMPRESSED 0

/* Text */
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_LABEL_LONG_TXT_HINT 0

/* Widgets used */
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 0
#define LV_USE_CHART 1
#define LV_USE_KEYBOARD 1
#define LV_USE_LIST 1
#define LV_USE_SPINNER 1
#define LV_USE_MSGBOX 0
#define LV_USE_CALENDAR 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_LED 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_ANIMIMG 0

/* Themes: plain default light theme; all styling is explicit */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 0
#define LV_USE_THEME_BASIC 0
#define LV_USE_THEME_MONO 0

/* Layouts */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* Demos/examples off */
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0

#endif /* LV_CONF_H */
#endif
