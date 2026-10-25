#include "ui.h"
#include "tones.h"

TFT_eSPI tft;

const Rect BACK_BTN = {4, 4, 92, 36};

void drawButton(const Rect &r, const char *label, uint16_t bg, uint16_t fg, uint8_t font) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, bg);
  tft.setTextColor(fg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, font);
}

void drawTopBar(const char *title, uint16_t color) {
  tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, color);
  drawButton(BACK_BTN, "< Back", COL_KEY, TFT_WHITE, 2);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(title, 110, TOPBAR_H / 2, 4);
}

void showMessage(const char *line1, const char *line2) {
  tft.fillScreen(COL_BG);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(line1, SCREEN_W / 2, line2 ? 140 : 160, 4);
  if (line2) {
    tft.setTextColor(COL_DIM);
    tft.drawString(line2, SCREEN_W / 2, 185, 2);
  }
}

void clickSound() { audioPlayNote(1500, 25); }
