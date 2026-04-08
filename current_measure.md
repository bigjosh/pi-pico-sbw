# Target Current Measurement

After programming an MSP430, I want to check the power draw to make sure there
are no shorts on the PCB or any other power problems. 

We do this by measuring the voltage drop across an external burden resistor using the Pico's ADC — no dedicated current sense IC needed.

## How It Works

A 100kΩ external resistor connects a separate GPIO (GP21, the "drive" pin)
to the VCC pin (GP27, the "sense" pin). During normal operation, GP27
drives the target directly. For current measurement, GP21 drives high
through the resistor while GP27 switches to ADC input. The voltage drop
across the resistor tells us the current.

### Why not the internal pull-up?

The original approach used the Pico's internal pull-up resistor on GP27 as
the sense element. This worked in principle but was inaccurate in practice.
I am not Exactly sure why, but the resistor seemed to be nonlinear.

The external resistor approach solves this by keeping the ADC pin (GP27)
connected to the low-impedance target VCC rail. The high-impedance resistor
is on a separate pin (GP21) that the ADC never touches. The ADC sees a
source impedance of just the target rail's output impedance (very low),
so the sample cap settles correctly.

The external resistor has proven to be surprisingly accurate.


### Circuit

```
3.3V ──[GPIO GP21 drive high]──[100kΩ]──┬── GP27 (ADC reads here)
                                        │
                                      [Target VCC]
                                        │
                                       GND
```

### Measurement cycle

1. Program and verify the target normally (GP27 driven by GPIO)
2. Float the clock and data pins to eliminate leakage current paths
3. Drive GP21 high (powers target through 100kΩ resistor)
4. Switch GP27 to ADC input (no pull-up)
5. Wait for the RC circuit to settle (~500ms)
6. Sample the ADC over the measurement window (1 sample/ms),
   tracking total, min, and max
7. Compute `I = (VCC - V_measured) / R_ext`
8. Restore GP27 to GPIO drive high (full power to target)
9. Float GP21

### Parameters

**1. External resistance** — 100kΩ, 1% tolerance. This is a known, stable
value unlike the internal pull-up which varies between chips and with
temperature.

**2. VCC voltage** — measured at 3.32V on our board. Used in the current
calculation. Adjust for your specific board.

**3. Settle time** — after switching from GPIO drive to resistor power,
the decoupling cap needs time to reach steady state. With 100kΩ and ~1µF
decoupling, τ ≈ 100ms, so we wait 500ms (~5τ).

**4. Sample window** — determines how many samples to average. A 500ms window captures a representative slice.

### Sensitivity

At 100kΩ external resistance:

| Target current | Voltage drop | ADC counts (of 65535) |
|----------------|-------------|----------------------|
| 0.5 µA | 50 mV | ~988 |
| 1 µA | 100 mV | ~1976 |
| 2 µA | 200 mV | ~3953 |
| 5 µA | 500 mV | ~9882 |
| 10 µA | 1.0V | ~19765 |
| 33 µA | 3.3V | saturated (target browns out) |

The practical measurement range is roughly 0.5–25 µA. Below 0.5 µA the
signal is small but detectable with averaging. Above ~25 µA the voltage
drops too much for the target to operate reliably.

## Hardware Setup

Connect a 100kΩ resistor (1% tolerance) between:
- GP21 (Pico pin 27) — the drive pin
- GP27 (Pico pin 32) — the sense/VCC pin


## Files

| File | Purpose |
|------|---------|
| `target_power.py` | `TargetPower` and `TargetPowerWithDetect` classes for power control and measurement |
| `simple-current-test/main.py` | Standalone test bench for current measurement validation |
| `current_measure.md` | This document |
