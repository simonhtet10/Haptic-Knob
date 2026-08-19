/* ============================================================
   Haptic Smart Knob - Final
   - FOC drive: SparkFun ROB-27478 (DM3505), 11 pole pairs
   - Encoder: AS5048A PWM output on GPIO 34
   - Driver: SimpleFOCMini (DRV8313) on 25/26/27/14
   - BLE HID media control (NimBLE fork), dual mode + button
   - Two-core: FOC/haptics on core 1, BLE on core 0
   ============================================================ */

#include <SimpleFOC.h>
#include <BleKeyboard.h>
enum Evt : uint8_t { EVT_VOL_UP, EVT_VOL_DOWN, EVT_NEXT, EVT_PREV, EVT_PLAYPAUSE };
QueueHandle_t evtQ;
inline void emit(Evt e) { xQueueSend(evtQ, &e, 0); }   // 0 = never block the FOC loop

// Encoder 
MagneticSensorPWM sensor = MagneticSensorPWM(34, 3, 928);
void doPWM() { sensor.handlePWM(); }
//motor
BLDCMotor      motor  = BLDCMotor(11);
BLDCDriver3PWM driver = BLDCDriver3PWM(25, 26, 27, 14);

//BLE setup
BleKeyboard kb("SmartKnob", "DIY", 100);

const uint8_t BTN = 32;

// sets the modes
enum { MODE_VOL = 0, MODE_TRK = 1 };
int knobMode = MODE_VOL;

// Detent values
const float VOL_STEP     = 10.0 * PI / 180.0;   // fine detents
const float TRK_STEP     = 50.0 * PI / 180.0;   // coarse detents
const float VOL_STRENGTH = 1.5;
const float TRK_STRENGTH = 2.5;

// detent catch zone
const float VOL_ZONE = 5.0 * PI / 180.0;
const float TRK_ZONE = 8.0 * PI / 180.0;

inline float stepFor(int m)     { return m == MODE_VOL ? VOL_STEP     : TRK_STEP; }
inline float strengthFor(int m) { return m == MODE_VOL ? VOL_STRENGTH : TRK_STRENGTH; }
inline float zoneFor(int m)     { return m == MODE_VOL ? VOL_ZONE     : TRK_ZONE; }

// ---- Soft endstops (walls) ----
// Kept SOFT and with NO damping on purpose: the AS5048A PWM velocity estimate
// is too noisy to damp with (adding damping made it oscillate violently). A soft wall + low voltage_limit stays stable within the PWM encoder's bandwidth.
const bool  WALLS_ON = true;
const float A_MIN = -2.0 * PI;    // lower endstop (~ -360 deg from start)
const float A_MAX = 2.0 * PI;    // upper endstop (~ +360 deg from start)
const float WALL  = 1.5;          // soft: raise slowly if you want a harder wall- too high reverts back to violent shaking

// ---- Direction: set to true to reverse which way is "up/next" ----
const bool REVERSE_DIR = true;

// ---- Button state machine ----
bool     btnPrev   = HIGH;
uint32_t btnDownAt = 0;
bool     longFired = false;

// ---- Detent tracking / haptic bump ----
int lastIdx = 0;
int bump    = 0;

//logic is separated into two cores due to the timing periods being orders of magnitude different between BLE HID and the FOC loop

//Bluetooth logic on core 0
void bleTask(void *) {
  Evt e;
  for (;;) {
    while (xQueueReceive(evtQ, &e, 0) == pdTRUE) {
      if (!kb.isConnected()) continue;
      switch (e) {
        case EVT_VOL_UP:    kb.write(KEY_MEDIA_VOLUME_UP);      break;
        case EVT_VOL_DOWN:  kb.write(KEY_MEDIA_VOLUME_DOWN);    break;
        case EVT_NEXT:      kb.write(KEY_MEDIA_NEXT_TRACK);     break;
        case EVT_PREV:      kb.write(KEY_MEDIA_PREVIOUS_TRACK); break;
        case EVT_PLAYPAUSE: kb.write(KEY_MEDIA_PLAY_PAUSE);     break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Button logic on core 1
void serviceButton() {
  bool b = digitalRead(BTN);
  uint32_t now = millis();

  if (btnPrev == HIGH && b == LOW) { btnDownAt = now; longFired = false; }

  // long press on button toggles mode
  if (b == LOW && !longFired && now - btnDownAt > 500) {
    knobMode ^= 1;
    lastIdx = lroundf(motor.shaftAngle() / stepFor(knobMode));  // reset index for new spacing
    bump = 16;                                                  // haptic confirmation
    longFired = true;
  }
  // short press controls play and pause
  if (btnPrev == LOW && b == HIGH) {
    if (!longFired && now - btnDownAt > 30) emit(EVT_PLAYPAUSE);
  }
  btnPrev = b;
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN, INPUT_PULLUP);

  sensor.init();
  sensor.enableInterrupt(doPWM);
  motor.linkSensor(&sensor);

  driver.voltage_power_supply = 12; // 12V supply
  driver.init();
  motor.linkDriver(&driver);

  motor.controller        = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::voltage;   // DRV8313 has no current sensing
  motor.voltage_limit     = 4;

  motor.init();
  motor.initFOC();                       // aligns sensor, finds electrical zero

  lastIdx = lroundf(motor.shaftAngle() / stepFor(knobMode));

  // start BLE + queue AFTER FOC calibration
  evtQ = xQueueCreate(32, sizeof(Evt));
  kb.begin();
  xTaskCreatePinnedToCore(bleTask, "ble", 4096, NULL, 1, NULL, 0);   // core 0

  Serial.println("Ready. Pair 'SmartKnob' via Bluetooth.");
}

// Motor loop is on core 1
void loop() {
  motor.loopFOC();
  serviceButton();

  float a    = motor.shaftAngle();
  float step = stepFor(knobMode);

  // dead-zone detent: spring only near the center, free between detents
  float err  = roundf(a / step) * step - a;      // signed distance to nearest center
  float zone = zoneFor(knobMode);
  float torque;
  if (fabs(err) < zone) torque = strengthFor(knobMode) * err;
  else                  torque = 0;

  // soft endstops: adds a push when you're past the wall (no damping)
  if (WALLS_ON) {
    if (a > A_MAX) torque += WALL * (A_MAX - a);
    if (a < A_MIN) torque += WALL * (A_MIN - a);
  }

  // haptic confirmation bump on mode switch
  if (bump > 0) { torque += (bump & 2) ? 1.5f : -1.5f; bump--; }

  // ---- detent crossing with directional hysteresis ----
  /** Count ONE event per detent, only after committing well past the boundary,
   and immune to settle-wobble. We track the "committed" detent index and
   only advance it when the shaft passes a threshold placed HYST beyond the
   midpoint in the direction of travel. This fixes both early-skip (event
   firing before a full detent) and bounce-back double-counting. **/
  float pos = a / step;                       // position in detent units
  // how far past the committed detent, in fractions of a step:
  float delta = pos - lastIdx;
  const float HYST = 0.80f;                    // must travel 80% of a step to commit
  if (delta > HYST) {
    lastIdx += 1;
    bool up = true;  if (REVERSE_DIR) up = !up;
    if (knobMode == MODE_VOL) emit(up ? EVT_VOL_UP : EVT_VOL_DOWN);
    else                      emit(up ? EVT_NEXT   : EVT_PREV);
  } else if (delta < -HYST) {
    lastIdx -= 1;
    bool up = false; if (REVERSE_DIR) up = !up;
    if (knobMode == MODE_VOL) emit(up ? EVT_VOL_UP : EVT_VOL_DOWN);
    else                      emit(up ? EVT_NEXT   : EVT_PREV);
  }

  motor.move(torque);
}
