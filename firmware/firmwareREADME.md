# Firmware

`BLE_detent_wall_smartknob_final.ino` is the complete sketch. 

## Dependencies

| Library | Version / source | Note |
|---|---|---|
| `arduino-esp32` core | **3.1.3 exactly** | 3.2.x ships ESP-IDF 5.4, which breaks MCPWM init for the 3-PWM driver |
| `SimpleFOC` | current release | |
| `ESP32-BLE-Keyboard` | [wakwak-koba NimBLE fork](https://github.com/wakwak-koba/ESP32-BLE-Keyboard) | The original T-vK library does not build on core 3.x |

## Required patch

Add to the top of `BleKeyboard.h` in the fork:

```cpp
#include <functional>
```

Without it the build fails on `std::function`.

## Flashing

Board: **ESP32 Dev Module**. Port: the CP2102 device. Default upload speed is fine.

## Pin assignments

See [`media/wiring.svg`](../media/wiring.svg) for the full wiring picture. Two GPIO details that
matter when reading the sketch:

- **GPIO 34 is input-only** on the ESP32 and has **no internal pull-up**. That suits the encoder,
  which drives the line actively — but it means this pin cannot be repurposed for the button.
- **GPIO 32 is configured `INPUT_PULLUP`**, so the button is active-low: the switch pulls to GND.

## Configuration

| Parameter | Value |
|---|---|
| `BLDCMotor` pole pairs | 11 |
| `voltage_power_supply` | 12 V |
| `voltage_limit` | 4 V |
| Control mode | torque / voltage |
| `zero_electric_angle` | ≈ 0.82 rad |
| Sensor direction | CW |
| Volume mode detent | 10° |
| Track mode detent | 50° |
| Detent profile | dead-zone |
| Endstops | soft, `WALL = 1.5` |
| Damping | disabled — see [`DEBUGGING.md`](../DEBUGGING.md) |

The `voltage_limit` of 4 V against a 12 V rail keeps continuous current below the motor's
1.1A rating. The knob holds position against a hand rather than spinning a load, so
thermal headroom matters more than peak torque.

## Calibration

`zero_electric_angle` (≈0.82 rad) and sensor direction (CW) are compile-time constants matched to
this specific motor. On different hardware, run the SimpleFOC calibration once and substitute the
values it reports. Persisting these to NVS to remove the boot-time calibration twitch is a
tracked next step.
