#include "input.h"
#include "gadget_config.h"

static const uint32_t DEBOUNCE_MS = 30;
static const uint32_t LONG_PRESS_MS = 1000;

// ---------------- Rotary encoder ----------------
// Both encoder pins interrupt on every change; the table turns the last two
// readings into -1/0/+1 and ignores contact bounce. A KY-040 makes 4 changes per click.

static const int8_t STEP_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
static volatile uint8_t encState = 0;
static volatile int32_t encTicks = 0;

static void IRAM_ATTR encoderChanged() {
  encState = ((encState << 2) | (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN)) & 0x0F;
  encTicks += STEP_TABLE[encState];
}

int knobTakeSteps() {
  noInterrupts();
  int32_t ticks = encTicks;
  int steps = ticks / 4;
  encTicks = ticks - steps * 4;     // keep a half-turned click for next time
  interrupts();
  return ENC_REVERSE ? -steps : steps;
}

// ---------------- Buttons (polled) ----------------

struct Button {
  uint8_t  pin;
  bool     activeHigh;
  bool     down;                    // debounced state
  bool     lastRaw;
  uint32_t changedAt, downAt;
  bool     longSent;
};

static Button touchBtn = {TOUCH_BTN_PIN, true, false, false, 0, 0, false};  // TTP223 goes HIGH when touched
static Button knobBtn  = {ENC_SW_PIN, false, false, false, 0, 0, false};    // encoder switch pulls LOW
static TouchPress touchEvent = PRESS_NONE;
static bool knobPressed = false;

// Updates a button; returns +1 on press, -1 on release, 0 otherwise
static int poll(Button &b) {
  bool raw = digitalRead(b.pin) == (b.activeHigh ? HIGH : LOW);
  if (raw != b.lastRaw) { b.lastRaw = raw; b.changedAt = millis(); }
  if (raw == b.down || millis() - b.changedAt < DEBOUNCE_MS) return 0;
  b.down = raw;
  if (raw) { b.downAt = millis(); b.longSent = false; return 1; }
  return -1;
}

void inputBegin() {
  pinMode(TOUCH_BTN_PIN, INPUT);              // TTP223 drives the pin itself
  pinMode(ENC_SW_PIN, INPUT_PULLUP);
  pinMode(ENC_CLK_PIN, INPUT_PULLUP);
  pinMode(ENC_DT_PIN, INPUT_PULLUP);
  encState = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), encoderChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT_PIN), encoderChanged, CHANGE);
  // Knob switch should read HIGH (1) when not pressed; 0 here means a wiring problem
  Serial.printf("Knob switch pin %d reads %d at start (1 = OK)\n", ENC_SW_PIN, digitalRead(ENC_SW_PIN));
}

void inputService() {
  int t = poll(touchBtn);
  if (touchBtn.down && !touchBtn.longSent && millis() - touchBtn.downAt >= LONG_PRESS_MS) {
    touchBtn.longSent = true;
    touchEvent = PRESS_LONG;                  // fires while still held
  } else if (t < 0 && !touchBtn.longSent) {
    touchEvent = PRESS_TAP;                   // short touch, on release
  }
  static bool lastSwRaw = true;
  bool swRaw = digitalRead(ENC_SW_PIN);
  if (swRaw != lastSwRaw) {                  // diagnostics: every raw change of the knob switch
    lastSwRaw = swRaw;
    Serial.printf("Knob switch raw: %s\n", swRaw ? "released" : "pressed");
  }
  if (poll(knobBtn) > 0) knobPressed = true;
}

TouchPress touchTake() {
  TouchPress e = touchEvent;
  touchEvent = PRESS_NONE;
  return e;
}

bool knobTakePress() {
  bool p = knobPressed;
  knobPressed = false;
  return p;
}
