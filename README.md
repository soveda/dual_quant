# dual quantised pitch shifter for MTMWS computer
Vibe code assissted first try at a programme card for the Music Thing Modular Workshop Computer
# Dual Quant Pitch

Dual quantised granular pitch shifter for the Music Thing Modular Workshop Computer.

This card creates two independently pitch-shifted versions of the same incoming audio signal while also generating matching calibrated 1V/oct CV outputs.

The design embraces the Workshop Computer philosophy:
- small
- immediate
- playable
- imperfect in musical ways

The pitch shifting is intentionally grainy and characterful rather than transparent.

---

# Controls

| Control | Function |
|---|---|
| Main Knob | Global transpose for both outputs (-12 to +12 semitones) |
| X Knob | Output 1 pitch |
| Y Knob | Output 2 pitch |
| CV In 1 | Additional modulation for Output 1 |
| CV In 2 | Additional modulation for Output 2 |

---

# Switch

| Position | Function |
|---|---|
| UP | Unquantised |
| MIDDLE | Quantised |
| DOWN | Advance scale |

Scale changes occur once per switch press.

---

# Scales

- Major
- Minor
- Pentatonic
- Minor Pentatonic
- Blues
- Whole Tone
- Chromatic

---

# LEDs

| LED | Scale |
|---|---|
| 0 | Major |
| 1 | Minor |
| 2 | Pentatonic |
| 3 | Minor Pentatonic |
| 4 | Blues |
| 5 | Whole Tone |
| All LEDs | Chromatic |

In unquantised mode all LEDs glow dimly.

---

# Inputs / Outputs

| Jack | Function |
|---|---|
| Audio/CV In 1 | Audio input |
| Audio Out 1 | Pitch shifted voice A |
| Audio Out 2 | Pitch shifted voice B |
| CV Out 1 | 1V/oct pitch CV for voice A |
| CV Out 2 | 1V/oct pitch CV for voice B |

---

# DSP Notes

This card uses:
- shared circular audio buffer
- dual granular read heads
- fixed-point DSP
- integer interpolation
- crossfaded grains

No floating point math is used inside the audio interrupt.

The result is CPU-efficient and stable on the RP2040 while preserving a rough hardware texture.

---

# Building

Requires:
- Pico SDK
- ComputerCard library
- RP2040 toolchain

Recommended:
- 144MHz clock
- copy_to_ram binary type

---

# Known Behaviour

This is not a transparent studio pitch shifter.

Large shifts intentionally:
- become grainier
- smear transients
- develop chorus-like movement

These artefacts are treated as musical character rather than defects.

---

# Credits

Created for the Music Thing Modular Workshop Computer.

AI-assisted implementation with human supervision and tuning.
