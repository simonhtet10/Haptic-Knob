# Debugging log

The problems that actually cost time, and how each was diagnosed. Ordered roughly by how long
they took to find rather than by when they appeared.

---

## Toolchain

### MCPWM initialization fails on arduino-esp32 3.2.x

**Symptom:** the SimpleFOCMini 3-PWM driver failed to initialize; the motor never energized.

**Cause:** core 3.2.x ships ESP-IDF 5.4, which reworked the MCPWM peripheral driver. SimpleFOC's
ESP32 3-PWM path did not initialize against it.

**Fix:** pinned `arduino-esp32` to **3.1.3** in Boards Manager.

**Takeaway:** on ESP32 the board-support version is part of the design, not an incidental detail.
It is pinned in the build notes for that reason.

---

### BLE keyboard library would not build

**Symptom:** the original T-vK `ESP32-BLE-Keyboard` failed to compile against core 3.x.

**Fix:** switched to the wakwak-koba NimBLE fork — then hit a second failure, on `std::function`,
resolved by adding a missing `#include <functional>` at the top of `BleKeyboard.h`.

**Takeaway:** two independent faults stacked behind one symptom ("BLE won't build"). Fixing the
first only revealed the second, which is worth remembering before concluding that a fix didn't work.

---

## Language and tooling gotchas

### `'X' was not declared in this scope` — hit twice

**Symptom:** an enum used in a function signature reported as undeclared, even though it was
visibly declared earlier in the file.

**Cause:** the Arduino IDE auto-generates function prototypes and inserts them near the top of the
sketch — *above* type declarations that appear later. The generated prototype then references a
type the compiler hasn't seen.

**Fix:** moved all enum declarations above the first function definition.

**Takeaway:** the Arduino preprocessor is not a plain C++ compiler. Any type appearing in a
function signature must be declared before the first function in the file.

---

### "Bluetooth device not found"

**Cause:** not a BLE bug. A pre-BLE revision of the sketch was on the board — the binary contained
no BLE code at all.

**Takeaway:** confirm what is actually running before debugging why it isn't working. A build-ID
string printed on boot would have caught this immediately, and is worth adding to any project with
multiple sketch revisions in flight.

---

### Encoder `getAngle()` "not looping"

**Symptom:** the reported angle kept increasing past 2π instead of wrapping.

**Resolution:** correct behavior, not a bug. `getAngle()` is cumulative — it counts total rotation
including full turns, which is what a multi-turn control loop needs.
`getMechanicalAngle()` is the one that wraps to 0–2π.

**Takeaway:** check the API contract before treating unexpected output as a fault.

---

## Hardware

### Unlabeled motor phase leads

**Problem:** the motor's phase connections are bare surface pads with no markings or documentation.

**Method:** measured resistance across pad pairs with a DMM. Three pads read ~6.8 Ω pairwise —
consistent with the datasheet's 6.34 Ω phase-to-phase plus lead and probe resistance — identifying
them as the windings. In a wye-connected 3-phase motor any pad triplet reading equal pairwise
resistance is the phase set, and phase *order* only sets rotation direction, which is corrected in
firmware.

**Fix:** soldered directly to the pads, with hot glue for strain relief.

---

## Control

### Damping made oscillation worse

**Symptom:** adding the standard `−DAMP · velocity` term to the detent torque increased ringing
rather than settling it.

**Diagnosis:** velocity is derived by differentiating the PWM-interface angle signal, which updates
at roughly 1 kHz and is quantized. Differentiation amplifies that quantization noise, so the
damping term injected noise-driven torque faster than it dissipated energy.

**Fix:** removed damping entirely; kept endstop walls soft so they don't excite the same ringing.

**Takeaway:** this is a sensor-bandwidth limit, not a gain-tuning problem. No value of `DAMP`
fixes it — an SPI encoder would. Worth recognizing early, because time spent sweeping a gain
against a noise-limited signal is time wasted.

---

### Mid-gap "dent" in coarse detent mode

**Symptom:** in 50° track mode, a spurious catch appeared *between* detent centers.

**Cause:** a pure sinusoidal torque profile has its maximum restoring force at the midpoint
between centers. At 10° spacing this is imperceptible; at 50° it becomes a distinct false detent.

**Fix:** dead-zone profile — restoring torque applies only within a catch zone around each center,
zero in between.

---

### Double-counted track skips

**Symptom:** one detent of rotation sometimes emitted two Next Track events.

**Cause:** coarse detents overshoot and settle back. Naive index-change detection fired once on
the overshoot and again on the bounce-back.

**Fix:** directional hysteresis — a new detent index is committed only after traveling 60% of a
step past the currently committed detent. One event per detent, no early trigger, no double-count.

---

## Cross-cutting

Three of the hardest problems here (**MCPWM init**, **BLE build failure**, **"Bluetooth not
found"**) had nothing to do with motor control. In an embedded project the toolchain is a
first-class source of bugs, and the useful early question is usually *"is the thing I think is
running actually running?"* before *"is my algorithm right?"*
