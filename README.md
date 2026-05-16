# DualQuantPitch

A dual-channel granular pitch shifter for the Workshop Computer platform.

This firmware is intentionally lo-fi and texture-focused rather than transparent.
The pitch shifting is based on overlapping granular delay reads with triangle-windowed grains.

The module provides:

* Dual independent pitch-shifted outputs
* Chromatic pitch quantisation
* CV-controllable pitch offsets
* Fixed-point DSP safe for ISR/audio-rate execution
* Granular texture and overlap artefacts by design

---

# Features

## Dual granular pitch shifters

Two independent read heads scan a shared circular audio buffer.

Each voice:

* Has independent pitch control
* Uses interpolated sample reads
* Uses triangle-windowed grains
* Runs continuously at audio rate

Outputs:

* AudioOut1 = voice A
* AudioOut2 = voice B

---

# Quantisation

The quantiser now uses a simplified chromatic-only mode.

When enabled:

* Incoming pitch values are rounded to the nearest semitone
* Pitch is represented internally using Q8.8 fixed-point format
* Quantisation is lightweight and ISR-safe

Quantiser implementation:

```cpp
int32_t QuantisePitch(int32_t semitone)
{
    if (semitone >= 0)
    {
        return ((semitone + 128) >> 8) << 8;
    }
    else
    {
        return ((semitone - 128) >> 8) << 8;
    }
}
```

Switch behaviour:

* Middle = quantised chromatic pitch
* Other positions = continuous pitch

---

# Fixed-point formats

## Audio playback

Playback position uses Q16.16 fixed point.

```text
upper 16 bits = integer sample index
lower 16 bits = fractional position
```

Examples:

```text
65536  = 1.0
32768  = 0.5
131072 = 2.0
```

---

## Pitch control

Pitch values use Q8.8 semitone format.

```text
256  = 1 semitone
128  = 0.5 semitone
-256 = -1 semitone
```

This allows:

* Smooth unquantised pitch movement
* Accurate semitone snapping
* Low-cost integer arithmetic

---

# Pitch control mapping

## Knobs

```text
0     -> -12 semitones
2048  -> 0 semitones
4095  -> +12 semitones
```

Implementation:

```cpp
int32_t KnobToSemitones(int32_t value)
{
    return (((value << 8) * 24) / 4095) - (12 << 8);
}
```

---

## CV input

Approximate mapping:

```text
1V = 12 semitones
```

Implementation:

```cpp
int32_t CVToSemitones(int32_t cv)
{
    return ((cv << 8) * 72) / 2048;
}
```

---

# Granular engine

## Circular buffer

A shared circular delay buffer stores incoming audio.

```cpp
static const int32_t BUFFER_SIZE = 2048;
```

Power-of-two sizing allows fast wraparound:

```cpp
index & BUFFER_MASK
```

instead of modulo operations.

---

## Grain size

```cpp
static const int32_t GRAIN_SIZE = 512;
```

Larger grains:

* smoother
* more latency
* fewer artefacts

Smaller grains:

* rougher
* more glitchy
* more rhythmic texture

---

## Interpolation

Linear interpolation smooths movement between samples.

```cpp
sample =
    s1 +
    (((s2 - s1) * frac)
    >> FP_SHIFT);
```

---

## Grain envelope

Each grain uses a triangle envelope to reduce clicks.

```text
fade in -> peak -> fade out
```

---

# Playback ratios

Playback speed is derived from a fixed lookup table.

This avoids:

* floating point
* powf()
* expensive ISR math

Pitch lookup currently supports:

```text
-12 to +12 semitones
```

Ratios are indexed using:

```cpp
int32_t idx = (semitone >> 8) + 12;
```

---

# CV outputs

Pitch CV outputs are generated in millivolts.

```cpp
int32_t mvA =
    ((pitchA >> 8) * 1000) / 12;

int32_t mvB =
    ((pitchB >> 8) * 1000) / 12;
```

This converts Q8.8 semitone values back into integer semitone space before voltage scaling.

---

# LED behaviour

## Quantised mode

All LEDs on.

## Continuous mode

All LEDs dim.

---

# ISR safety

Everything inside `ProcessSample()` is designed for real-time ISR execution.

Avoided operations:

* dynamic allocation
* floating point
* STL containers
* blocking operations
* expensive transcendental functions

The firmware is intended to run safely at:

```text
48kHz audio rate
```

with:

```text
144MHz system clock
```

---

# Current architecture summary

## Audio format

* 16-bit audio samples
* Q16.16 playback positions
* Q8.8 pitch representation

## DSP structure

* Shared circular delay buffer
* Dual overlapping grains
* Linear interpolation
* Triangle grain windows
* Fixed lookup-table pitch ratios
* Chromatic semitone quantiser

---

# Known sonic characteristics

This is intentionally not a transparent pitch shifter.

Expected artefacts include:

* granular texture
* chorus-like motion
* smearing
* grain beating
* rhythmic overlap artefacts
* transient roughness

These artefacts are considered part of the instrument character.
