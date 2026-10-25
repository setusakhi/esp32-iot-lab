// Calculator: + - x / with percent, sign change and delete
#include "apps.h"
#include <math.h>

static const char *KEYS[4][5] = {
  {"7",   "8", "9", "/", "C"},
  {"4",   "5", "6", "x", "DEL"},
  {"1",   "2", "3", "-", "%"},
  {"+/-", "0", ".", "+", "="},
};

static const Rect DISPLAY_BOX = {6, 48, 468, 54};

static String disp;        // number on screen
static String expr;        // small line above, e.g. "12 +"
static double acc;
static char   op;
static bool   newEntry;
static bool   err;
static int    pressedR = -1, pressedC = -1;

static Rect keyRect(int r, int c) { return {(int16_t)(6 + c * 94), (int16_t)(108 + r * 53), 90, 49}; }

static uint16_t keyColor(int r, int c) {
  const char *k = KEYS[r][c];
  if (!strcmp(k, "C"))   return TFT_RED;
  if (!strcmp(k, "="))   return 0x256C;          // green
  if (c == 3)            return TFT_ORANGE;      // operators
  if (c == 4 || r == 3 && c == 0) return 0x6B4D; // DEL, %, +/-
  return COL_KEY;
}

static void drawKey(int r, int c, bool pressed) {
  uint16_t bg = pressed ? TFT_WHITE : keyColor(r, c);
  uint16_t fg = pressed ? TFT_BLACK : TFT_WHITE;
  drawButton(keyRect(r, c), KEYS[r][c], bg, fg, 4);
}

static void drawDisplay() {
  tft.fillRoundRect(DISPLAY_BOX.x, DISPLAY_BOX.y, DISPLAY_BOX.w, DISPLAY_BOX.h, 8, COL_PANEL);
  tft.setTextPadding(0);
  tft.setTextColor(COL_DIM);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(expr, 14, 52, 2);

  // Font 6 has big digits only; use font 4 for "Error" or very long numbers
  bool bigOk = !err && tft.textWidth(disp, 6) < 440;
  tft.setTextColor(err ? TFT_RED : TFT_WHITE);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(disp, 464, bigOk ? 80 : 84, bigOk ? 6 : 4);
}

static void clearAll() {
  disp = "0"; expr = ""; acc = 0; op = 0; newEntry = true; err = false;
}

static String fmt(double v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.10g", v);
  if (strchr(buf, 'e')) snprintf(buf, sizeof(buf), "%.8f", v);
  String s = buf;
  if (s.indexOf('.') >= 0) {                  // trim trailing zeros
    while (s.endsWith("0")) s.remove(s.length() - 1);
    if (s.endsWith(".")) s.remove(s.length() - 1);
  }
  if (s == "-0") s = "0";
  return s;
}

static void showResult(double v) {
  if (isnan(v) || isinf(v) || fabs(v) >= 1e12) { err = true; disp = "Error"; return; }
  disp = fmt(v);
}

static double apply(double a, char o, double b) {
  switch (o) {
    case '+': return a + b;
    case '-': return a - b;
    case 'x': return a * b;
    case '/': return b == 0 ? NAN : a / b;
  }
  return b;
}

static void pressKey(const char *k) {
  if (!strcmp(k, "C")) { clearAll(); return; }
  if (err) clearAll();

  bool isDigit = (k[0] >= '0' && k[0] <= '9' && k[1] == 0);
  if (isDigit || !strcmp(k, ".")) {
    if (newEntry) { disp = isDigit ? String(k) : String("0."); newEntry = false; return; }
    if (disp.length() >= 12) return;
    if (!strcmp(k, ".")) { if (disp.indexOf('.') < 0) disp += "."; return; }
    if (disp == "0") disp = k; else disp += k;
    return;
  }
  if (!strcmp(k, "DEL")) {
    if (newEntry) return;
    disp.remove(disp.length() - 1);
    if (disp.length() == 0 || disp == "-") disp = "0";
    return;
  }
  if (!strcmp(k, "+/-")) {
    if (disp == "0") return;
    disp = disp.startsWith("-") ? disp.substring(1) : "-" + disp;
    return;
  }
  if (!strcmp(k, "%")) { showResult(disp.toDouble() / 100.0); newEntry = true; return; }

  if (!strcmp(k, "=")) {
    if (!op) return;
    double b = disp.toDouble();
    expr = fmt(acc) + " " + op + " " + disp + " =";
    showResult(apply(acc, op, b));
    op = 0;
    newEntry = true;
    return;
  }

  // + - x /
  char o = k[0];
  double v = disp.toDouble();
  if (op && !newEntry) { showResult(apply(acc, op, v)); if (err) return; acc = disp.toDouble(); }
  else if (!op)        acc = v;
  op = o;
  expr = fmt(acc) + " " + op;
  newEntry = true;
}

static void calcEnter() {
  drawTopBar("Calculator");
  clearAll();
  drawDisplay();
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 5; c++) drawKey(r, c, false);
}

static void calcTouch(TouchEvent ev, int16_t x, int16_t y) {
  if (ev == TOUCH_DOWN) {
    if (BACK_BTN.contains(x, y)) { clickSound(); goToApp(&menuApp); return; }
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 5; c++)
        if (keyRect(r, c).contains(x, y)) {
          clickSound();
          drawKey(r, c, true);
          pressedR = r; pressedC = c;
          pressKey(KEYS[r][c]);
          drawDisplay();
          return;
        }
  } else if (ev == TOUCH_UP && pressedR >= 0) {
    drawKey(pressedR, pressedC, false);
    pressedR = pressedC = -1;
  }
}

App calcApp = {calcEnter, nullptr, nullptr, calcTouch};
