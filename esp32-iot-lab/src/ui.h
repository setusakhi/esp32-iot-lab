#pragma once
#include <TFT_eSPI.h>

// Shared screen helpers used by every app.

extern TFT_eSPI tft;

#define SCREEN_W  480
#define SCREEN_H  320
#define TOPBAR_H  44

// Colours (RGB565)
#define COL_BG      TFT_BLACK
#define COL_TEXT    TFT_WHITE
#define COL_DIM     0x8410   // grey text
#define COL_PANEL   0x18E3   // dark panel
#define COL_KEY     0x4208   // dark grey button
#define COL_TOPBAR  0x10E7   // deep blue

struct Rect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

enum TouchEvent { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };

extern const Rect BACK_BTN;

void drawButton(const Rect &r, const char *label, uint16_t bg,
                uint16_t fg = TFT_WHITE, uint8_t font = 4);
void drawTopBar(const char *title, uint16_t color = COL_TOPBAR);   // includes "< Back"
void showMessage(const char *line1, const char *line2 = nullptr);  // full-screen message
void clickSound();
