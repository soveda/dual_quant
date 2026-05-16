//
//  DualQuantPitch.cpp
//
//
//  Created by Adrian Vos on 16/05/2026.
//


#include "ComputerCard.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

class DualQuantPitch : public ComputerCard
{
public:

    // ============================================================
    // Workshop Computer granular pitch shifter
    //
    // The goal here is not perfect transparency.
    // We intentionally embrace a rough granular texture.
    //
    // Everything inside ProcessSample() must remain ISR-safe:
    // - no floats
    // - no dynamic allocation
    // - no expensive library calls
    // - no blocking operations
    //
    // ============================================================

    // ============================================================
    // Fixed-point format
    //
    // Q16.16:
    // upper 16 bits = integer
    // lower 16 bits = fraction
    //
    // Example:
    // 65536 = 1.0
    // 32768 = 0.5
    // 131072 = 2.0
    //
    // ============================================================

    static const int32_t FP_SHIFT = 16;
    static const int32_t FP_ONE   = 1 << FP_SHIFT;

    // ============================================================
    // Circular audio buffer
    //
    // Shared between both pitch shifters.
    //
    // Power-of-two size allows very fast wrapping
    // using bitmask operations instead of modulo.
    //
    // ============================================================

    static const int32_t BUFFER_SIZE = 2048;
    static const int32_t BUFFER_MASK = BUFFER_SIZE - 1;

    // ============================================================
    // Grain settings
    //
    // Larger grains:
    // - smoother
    // - more latency
    //
    // Smaller grains:
    // - rougher
    // - more glitchy
    //
    // ============================================================

    static const int32_t GRAIN_SIZE = 512;
    static const int32_t HALF_GRAIN = 256;

    // ============================================================
    // Scale definitions
    // ============================================================

    enum Scale
    {
        MAJOR,
        MINOR,
        PENTATONIC,
        MINOR_PENTATONIC,
        BLUES,
        WHOLE_TONE,
        CHROMATIC,
        NUM_SCALES
    };

    static constexpr int8_t majorScale[7] =
        {0,2,4,5,7,9,11};

    static constexpr int8_t minorScale[7] =
        {0,2,3,5,7,8,10};

    static constexpr int8_t pentScale[5] =
        {0,2,4,7,9};

    static constexpr int8_t minorPentScale[5] =
        {0,3,5,7,10};

    static constexpr int8_t bluesScale[6] =
        {0,3,5,6,7,10};

    static constexpr int8_t wholeToneScale[6] =
        {0,2,4,6,8,10};

    static constexpr int8_t chromaticScale[12] =
        {0,1,2,3,4,5,6,7,8,9,10,11};

    // ============================================================
    // Audio state
    // ============================================================

    int16_t audioBuffer[BUFFER_SIZE];

    int32_t writeHead = 0;

    int32_t readHeadA = 0;
    int32_t readHeadB = HALF_GRAIN << FP_SHIFT;

    int32_t grainPhaseA = 0;
    int32_t grainPhaseB = HALF_GRAIN;

    Scale currentScale = MAJOR;

    // ============================================================
    // Constructor
    // ============================================================

    DualQuantPitch()
    {
        // 144MHz is a good balance between:
        // - CPU headroom
        // - ADC stability
        // - reduced noise artefacts

        set_sys_clock_khz(144000, true);
    }

    // ============================================================
    // Main audio ISR
    //
    // Runs at 48kHz.
    //
    // This must complete in under ~20 microseconds.
    // ============================================================

    virtual void ProcessSample()
    {
        // ========================================================
        // Switch handling
        // ========================================================

        if (SwitchChanged())
        {
            // Rotate scale only when switch moves DOWN.
            // Prevents repeated scrolling while held.

            if (SwitchVal() == Switch::Down)
            {
                currentScale = static_cast<Scale>(
                    (currentScale + 1) % NUM_SCALES
                );
            }
        }

        bool quantised =
            (SwitchVal() == Switch::Middle);

        // ========================================================
        // Audio input
        // ========================================================

        int32_t input = AudioIn1();

        // Store incoming audio in circular delay buffer.

        audioBuffer[writeHead] = input;

        // ========================================================
        // Read controls
        // ========================================================

        int32_t mainPitch =
            KnobToSemitones(
                KnobVal(Knob::Main)
            );

        int32_t pitchA =
            mainPitch +
            KnobToSemitones(
                KnobVal(Knob::X)
            ) +
            CVToSemitones(CVIn1());

        int32_t pitchB =
            mainPitch +
            KnobToSemitones(
                KnobVal(Knob::Y)
            ) +
            CVToSemitones(CVIn2());

        // ========================================================
        // Quantisation
        // ========================================================

        if (quantised)
        {
            pitchA = QuantisePitch(pitchA);
            pitchB = QuantisePitch(pitchB);
        }

        // ========================================================
        // Convert semitones to playback ratios
        // ========================================================

        int32_t ratioA =
            SemitoneToRatio(pitchA);

        int32_t ratioB =
            SemitoneToRatio(pitchB);

        // ========================================================
        // Generate audio
        // ========================================================

        int32_t outA =
            ReadInterpolated(readHeadA,
                             grainPhaseA);

        int32_t outB =
            ReadInterpolated(readHeadB,
                             grainPhaseB);

        AudioOut1(outA);
        AudioOut2(outB);

        // ========================================================
        // Advance read heads
        // ========================================================

        readHeadA += ratioA;
        readHeadB += ratioB;

        grainPhaseA++;
        grainPhaseB++;

        // Restart grains periodically.
        // This prevents runaway drift and creates
        // the granular overlap texture.

        if (grainPhaseA >= GRAIN_SIZE)
        {
            grainPhaseA = 0;

            readHeadA =
                ((writeHead - GRAIN_SIZE)
                & BUFFER_MASK)
                << FP_SHIFT;
        }

        if (grainPhaseB >= GRAIN_SIZE)
        {
            grainPhaseB = 0;

            readHeadB =
                ((writeHead
                - GRAIN_SIZE
                - HALF_GRAIN)
                & BUFFER_MASK)
                << FP_SHIFT;
        }

        // ========================================================
        // Calibrated pitch CV outputs
        // ========================================================

        int32_t mvA =
            (pitchA * 1000) / 12;

        int32_t mvB =
            (pitchB * 1000) / 12;

        CVOut1Millivolts(mvA);
        CVOut2Millivolts(mvB);

        // ========================================================
        // LED UI
        // ========================================================

        UpdateLEDs(quantised);

        // ========================================================
        // Advance circular buffer
        // ========================================================

        writeHead =
            (writeHead + 1)
            & BUFFER_MASK;
    }

    // ============================================================
    // Granular read function
    //
    // Reads interpolated samples from the delay buffer
    // and applies a triangle window.
    //
    // The window smooths grain transitions and reduces
    // hard clicks.
    // ============================================================

    int32_t ReadInterpolated(
        int32_t readHead,
        int32_t phase)
    {
        int32_t index =
            (readHead >> FP_SHIFT)
            & BUFFER_MASK;

        int32_t frac =
            readHead & 0xFFFF;

        int32_t s1 = audioBuffer[index];

        int32_t s2 =
            audioBuffer[
                (index + 1)
                & BUFFER_MASK
            ];

        // ========================================================
        // Linear interpolation
        //
        // This smooths movement between samples.
        // ========================================================

        int32_t sample =
            s1 +
            (((s2 - s1) * frac)
            >> FP_SHIFT);

        // ========================================================
        // Triangle envelope
        //
        // Prevents abrupt grain edges.
        // ========================================================

        int32_t env;

        if (phase < HALF_GRAIN)
        {
            env = (phase * 4096) / HALF_GRAIN;
        }
        else
        {
            env =
                ((GRAIN_SIZE - phase)
                * 4096)
                / HALF_GRAIN;
        }

        return (sample * env) >> 12;
    }

    // ============================================================
    // Convert knob position to semitones
    //
    // 0      -> -12
    // 2048   -> 0
    // 4095   -> +12
    // ============================================================

    int32_t KnobToSemitones(int32_t value)
    {
        return ((value * 24) / 4095) - 12;
    }

    // ============================================================
    // CV mapping
    //
    // Approximate:
    // 1V = 12 semitones
    // ============================================================

    int32_t CVToSemitones(int32_t cv)
    {
        return (cv * 72) / 2048;
    }

    // ============================================================
    // Quantiser
    //
    // Snaps incoming semitone values to nearest
    // valid scale degree.
    //
    // This behaves like a continuous note selector
    // rather than chromatic rounding.
    // ============================================================

    int32_t QuantisePitch(int32_t semitone)
    {
        int32_t octave = semitone / 12;

        int32_t note = semitone % 12;

        if (note < 0)
        {
            note += 12;
            octave--;
        }

        const int8_t* scale;
        int32_t size;

        switch(currentScale)
        {
            case MAJOR:
                scale = majorScale;
                size = 7;
                break;

            case MINOR:
                scale = minorScale;
                size = 7;
                break;

            case PENTATONIC:
                scale = pentScale;
                size = 5;
                break;

            case MINOR_PENTATONIC:
                scale = minorPentScale;
                size = 5;
                break;

            case BLUES:
                scale = bluesScale;
                size = 6;
                break;

            case WHOLE_TONE:
                scale = wholeToneScale;
                size = 6;
                break;

            case CHROMATIC
            default:
                return semitone;
        }

        int32_t nearest = scale[0];
        int32_t bestDist = 12;

        for (int32_t i = 0; i < size; i++)
        {
            int32_t dist =
                Abs(note - scale[i]);

            // Wraparound correction.
            //
            // Example:
            // distance from 11 -> 0
            // should be 1 semitone,
            // not 11 semitones.

            if (dist > 6)
            {
                dist = 12 - dist;
            }

            if (dist < bestDist)
            {
                bestDist = dist;
                nearest = scale[i];
            }
        }

        return (octave * 12) + nearest;
    }

    // ============================================================
    // Fixed-point playback ratios
    //
    // Precomputed for speed.
    //
    // Values are intentionally approximate and
    // musically tuned rather than mathematically exact.
    //
    // Avoids expensive powf() in ISR.
    // ============================================================

    int32_t SemitoneToRatio(int32_t semitone)
    {
        static const int32_t ratios[25] =
        {
            32768,
            34716,
            36780,
            38967,
            41285,
            43740,
            46341,
            49096,
            52016,
            55109,
            58386,
            61858,
            65536,
            69433,
            73561,
            77935,
            82570,
            87480,
            92682,
            98192,
            104032,
            110218,
            116772,
            123716,
            131072
        };

        int32_t idx = semitone + 12;

        if (idx < 0) idx = 0;
        if (idx > 24) idx = 24;

        return ratios[idx];
    }

    // ============================================================
    // LED UI
    // ============================================================

    void UpdateLEDs(bool quantised)
    {
        for (int i = 0; i < 6; i++)
        {
            LedOff(i);
        }

        // Unquantised mode:
        // dim glow on all LEDs.

        if (!quantised)
        {
            for (int i = 0; i < 6; i++)
            {
                LedBrightness(i, 128);
            }

            return;
        }

        // Chromatic mode:
        // all LEDs lit.

        if (currentScale == CHROMATIC)
        {
            for (int i = 0; i < 6; i++)
            {
                LedOn(i);
            }

            return;
        }

        // Other scales:
        // one-hot LED display.

        LedOn(static_cast<int>(currentScale));
    }

    // ============================================================
    // Integer absolute value
    // ============================================================

    int32_t Abs(int32_t x)
    {
        return (x < 0) ? -x : x;
    }
};

int main()
{
    DualQuantPitch card;
    card.Run();
}
