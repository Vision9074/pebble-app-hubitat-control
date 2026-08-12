// Shared colours and font choices. Aplite and Diorite fall back to black on
// white, so every accent has a monochrome twin.

#pragma once

#include <pebble.h>

#define THEME_BACKGROUND GColorBlack
#define THEME_TEXT GColorWhite
#define THEME_DIM PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define THEME_ACCENT PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorWhite)
#define THEME_ON PBL_IF_COLOR_ELSE(GColorYellow, GColorWhite)
#define THEME_OFF PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)
#define THEME_ALERT PBL_IF_COLOR_ELSE(GColorMelon, GColorWhite)

#define THEME_MENU_BG PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite)
#define THEME_MENU_FG PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
#define THEME_MENU_SEL_BG PBL_IF_COLOR_ELSE(GColorPictonBlue, GColorBlack)
#define THEME_MENU_SEL_FG PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite)

// Screen sizes in play: aplite, basalt, diorite and flint are 144x168; chalk is
// 180x180 and emery 200x228; gabbro is 260x260 and carries a bigger face again.
// Keyed off the actual bounds rather than the platform, so a new size slots in.
typedef enum {
  ScreenSmall = 0,
  ScreenMedium,
  ScreenLarge
} ScreenTier;

static inline ScreenTier theme_tier(GRect bounds) {
  if (bounds.size.w >= 220)
    return ScreenLarge;
  if (bounds.size.w >= 180)
    return ScreenMedium;
  return ScreenSmall;
}
