# Haptic Smart Knob

**A field-oriented-control BLDC drive with software-defined haptics and BLE HID media control.**

Turning the knob adjusts system volume or skips tracks. A tactile button plays/pauses and toggles
modes — and each mode reprograms the *feel* of the knob: fine 10° detents for volume, coarse 50°
detents with soft endstops for track skipping. Different torque field per mode with no hardware difference.

<!-- TODO: add demo GIF here — this is the single highest-impact asset on the page.
![Demo](media/demo.gif)
-->

---

## Why a knob

The knob serves as an intuitive way to control the motor stack underneath.

Detents don't come from a mechanical spring and ball — there is no mechanical detent anywhere in
this build. The knob is a 3-phase brushless motor running closed-loop field-oriented control, and
every "click" you feel is a torque command computed in firmware from the rotor angle:

```
AS5048A encoder ──► Clarke / Park ──► torque command ──► inverse Park ──► SVPWM ──► DRV8313 ──► motor
       (rotor angle θ)                (detent profile)                    (~25 kHz)
```

That is the same architecture as an EV traction inverter or a robot joint actuator, at
milliwatt scale — rotor-referenced current control with sinusoidal commutation. The haptics are
just an application layer written on top of a torque-controlled axis: change the torque-vs-angle
function and you change the physical sensation, with no mechanical change at all.

---

## Demo

<!-- TODO: embed the captioned demo video, or link it.
The demo shows: mode toggle → the same knob producing a visibly different rotation-per-track
response, plus detent snap-back at the endstops.
-->

<!-- TODO: 2–3 captioned stills from the video -->

---

## How it works

### Torque field → feel

Each mode defines a periodic torque function of rotor angle. The controller runs in
voltage-torque mode (the SimpleFOCMini has no current sense), so the commanded quantity is a
q-axis voltage proportional to desired torque.

| Mode | Detent spacing | Endstops | HID output |
|---|---|---|---|
| **Volume** | 10° | none | Volume Up / Volume Down |
| **Track** | 50° | soft walls (`WALL = 1.5`) | Next Track / Previous Track |

Two refinements make it feel right rather than merely work:

- **Dead-zone detent profile.** Restoring torque is applied only inside a catch zone around each
  detent center, and the field is free in between. Without this, wide (50°) detents develop a
  spurious "dent" in the middle of the gap, because a pure sinusoidal torque profile has its
  strongest restoring force halfway between centers.
- **Directional hysteresis on event emission.** A detent index is only committed after the knob
  travels 60% of a step past the detent it is currently in. Coarse detents overshoot and settle,
  which was double-counting track skips — one skip on the overshoot, one on the bounce-back. The
  hysteresis is put in place to yield exactly one HID event per detent, with no early trigger and no bounce-back
  double-count.

### Two-core split

| Core | Task |
|---|---|
| **1** | FOC loop + haptic torque computation |
| **0** | BLE HID stack |

The two are joined by a **zero-timeout FreeRTOS queue**. If the BLE side blocks — a stalled
notification, a host that stopped acknowledging — the haptics never stall, because the producer
never waits on the queue. It's acceptable to drop a media event but a hitch in the torque loop is
immediately felt in the hand.

---

## Key design decisions

These were the real engineering calls in the build.

**1. Integrated-encoder motor, to delete the biggest mechanical risk.**
The dominant failure mode in DIY FOC builds is the air-gap alignment between the diametric-magnet and the encoder. The
ROB-27478 ships with the AS5048A factory-aligned to the rotor. This ensures that there's no human error in this part of the build.

**2. The encoder was PWM, not SPI — and that shaped the whole control design.**
The AS5048A supports both, but this unit only breaks out the PWM output. Using `MagneticSensorPWM`
caps angle updates at roughly 1 kHz and makes the differentiated velocity estimate noisy. That
single interface constraint drove decision 3.

**3. Damping was removed after measurement, not after taste.**
A `−DAMP · velocity` term is the textbook way to settle a haptic detent. Here it made the knob
oscillate *worse*. The velocity signal is a finite difference of a ~1 kHz quantized angle, so the
damping term was injecting noise-driven torque faster than it removed energy. Endstops were kept
soft for the same reason. This is an interface-bandwidth limitation, not a tuning failure — the fix
is an SPI encoder, not a different gain.

**4. Undocumented motor wiring, reverse-engineered with a DMM.**
The phase leads terminate in bare surface pads with no markings. Measuring ~6.8 Ω between three
pads identified them as the phase windings (datasheet: 6.34 Ω phase-to-phase; the excess is lead
and probe resistance). The phase wires were soldered directly to the surface pads.

**5. BLE HID over the Spotify Web API, deliberately.**
HID gives roughly 15 ms end-to-end latency versus 100–300 ms for a cloud round-trip, and needs no
OAuth, TLS, or WiFi provisioning. For a device whose entire value is that it feels physically
connected to the audio, latency wins. The documented cost: HID is write-only, so there is **no
state readback and therefore no true volume endstops** — the firmware cannot know where the host's
volume actually is. See [Known limitations](#known-limitations).

---

## Characterization

Measured on an Analog Discovery 2.

### Phase PWM — switching frequency

![Phase PWM switching](media/phase_pwm_switching.png)

Two inverter phase outputs, 10 µs/div, 5 V/div. Measured switching frequency
**24.998 kHz and 25.009 kHz** on the two channels — matching the configured ~25 kHz PWM carrier,
above the audible band so the drive is silent. Both phases swing the full 12 V rail with clean
edges and no visible shoot-through notch, and the two channels sit at visibly different duty
cycles at this instant, which is the commutation acting on the two phases independently.

<!-- TODO: phase envelope figure — see analysis/README.md for the recapture procedure.
![Sinusoidal commutation](media/phase_pwm_envelope.png)
-->

<!-- TODO: current → torque figure (1 Ω shunt; τ = 0.08 N·m/A × I)
![Current and torque](media/current_torque.png)
-->

<!-- TODO: step response (serial-logged) -->

---

## Hardware

| Block | Part | Notes |
|---|---|---|
| Motor | SparkFun ROB-27478 (**DM3505**) gimbal, 12 V | 11 pole pairs |
| Encoder | **AS5048A**, PWM output | On-motor, factory-aligned |
| Driver | SimpleFOCMini (**DRV8313**) | 8–30 V, 2.5 A/phase, no current sense |
| MCU | ESP32-WROOM 30-pin DevKitC (CP2102, USB-C) | Dual-core, BLE |
| Button | 6×6 mm tactile | Short press = play/pause, long = mode toggle |
| Power | 12 V adapter → driver; ESP32 via USB | |

**DM3505 motor specs:** 12 V / 1.1 A nominal, 1.9 A stall, phase-to-phase R = 6.34 Ω,
L = 1.08 mH, torque constant 0.08 N·m/A, 11 pole pairs.

Full pin map: [`hardware/pinmap.md`](hardware/pinmap.md) · Bill of materials:
[`hardware/BOM.csv`](hardware/BOM.csv)

<!-- TODO: build photo — breadboard overview, and a close-up of the soldered phase pads
![Build](media/build_photos/breadboard.jpg)
-->

---

## Build and flash notes

Three non-obvious requirements — each one cost real debugging time. Full log in
[`DEBUGGING.md`](DEBUGGING.md).

1. **Pin `arduino-esp32` to `3.1.3`.** Core 3.2.x ships ESP-IDF 5.4, whose MCPWM initialization
   fails with SimpleFOC's 3-PWM driver configuration. Set this in Boards Manager before building.
2. **Use the [wakwak-koba NimBLE fork](https://github.com/wakwak-koba/ESP32-BLE-Keyboard) of
   `ESP32-BLE-Keyboard`.** The original T-vK library does not build against core 3.x.
3. **Patch the fork:** add `#include <functional>` to the top of `BleKeyboard.h`. Without it the
   build fails on `std::function`.

Then set the board to *ESP32 Dev Module*, select the CP2102 port, and flash
[`firmware/`](firmware/).

**Calibration values are currently compile-time constants** (`zero_electric_angle ≈ 0.82`,
sensor direction CW). If you rebuild on different hardware, run the FOC calibration once and
substitute your own values.

---

## Repo layout

```
haptic-smart-knob/
├── README.md
├── DEBUGGING.md            # the debugging log
├── firmware/               # the Arduino sketch
├── hardware/
│   ├── BOM.csv
│   └── pinmap.md
├── media/                  # demo GIF/video, AD2 figures, build photos
└── analysis/
    ├── plot_phase_pwm.py   # AD2 CSV → clean figures
    └── README.md           # capture settings + recapture procedure
```

---

## Known limitations

Stated plainly, because knowing where a design stops is part of the design.

- **No true volume endstops.** BLE HID is write-only. The firmware sends Volume Up/Down keycodes but has no knowledge of      the host's actual volume level, so it cannot place a hard wall at 0% or 100%. Track
  mode has real endstops because its range is defined locally, not by the host.
- **Velocity feedback is not usable for control.** The PWM encoder interface (~1 kHz, quantized)
  makes differentiated velocity too noisy for damping. Detent settling is therefore underdamped
  compared with an SPI-encoder build.
- **No current sensing.** The DRV8313 carrier provides none, so the drive runs open-loop in
  current: torque is commanded as a q-axis *voltage*, and the actual torque varies with winding
  temperature and back-EMF.
- **Breadboard construction**, with the ESP32 powered separately over USB.

## Next steps

- [ ] Persist `zero_electric_angle` and `sensor_direction` to NVS, eliminating the boot-time
      calibration twitch
- [ ] Current/torque characterization via 1 Ω shunt (τ = 0.08 N·m/A × I)
- [ ] Step response capture
- [ ] Migrate from breadboard to perfboard
- [ ] 3D-printed enclosure
- [ ] Onboard buck converter for standalone 5 V, removing the USB tether

---


MIT — see [LICENSE](LICENSE).
