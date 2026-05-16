// ============================================================
// Dual Quant Pitch
//
// Workshop Computer program card for:
//
// - Dual pitch-shifted outputs
// - Independent pitch control
// - Quantised interval selection
// - CV-controlled pitch offsets
// - Proper overlapping-grain pitch shifting
//
// Designed for:
// Music Thing Modular Workshop Computer
// ComputerCard v0.3.0+
//
// ============================================================

#include "ComputerCard.h"

#include <math.h>
#include <stdint.h>

class DualQuantPitch : public ComputerCard
{
public:

    // ========================================================
    // Audio buffer configuration
    // ========================================================

    static const int32_t BUFFER_SIZE = 2048;
    static const int32_t BUFFER_MASK = BUFFER_SIZE - 1;

    // ========================================================
    // Larger grains improve pitch clarity and reduce
    // dry-signal bleed.
    // ========================================================

    static const int32_t GRAIN_SIZE = 1024;
    static const int32_t HALF_GRAIN = 512;

    // ========================================================
    // Fixed-point precision
    // ========================================================

    static const int32_t FP_SHIFT = 16;
    static const int32_t FP_ONE = 1 << FP_SHIFT;

    // ========================================================
    // Circular audio buffer
    // ========================================================

    int16_t audioBuffer[BUFFER_SIZE];

    volatile int32_t writeHead = 0;

    // ========================================================
    // Hann window lookup table
    //
    // 4096 = unity gain
    // ========================================================

    int16_t hannWindow[GRAIN_SIZE];

    // ========================================================
    // Grain structures
    // ========================================================

    struct Grain
    {
        int32_t readHead;
        int32_t phase;
    };

    struct Voice
    {
        Grain grain1;
        Grain grain2;
    };

    Voice voiceA;
    Voice voiceB;

    // ========================================================
    // Scale selection
    // ========================================================

    int32_t currentScale = 0;

    // ========================================================
    // Scale definitions
    // ========================================================

    static const int32_t SCALE_COUNT = 6;

    static const int32_t majorScale[7];
    static const int32_t minorScale[7];
    static const int32_t pentScale[5];
    static const int32_t minorPentScale[5];
    static const int32_t bluesScale[6];
    static const int32_t wholeToneScale[6];

    // ========================================================
    // Constructor
    // ========================================================

    DualQuantPitch()
    {
        // ====================================================
        // Build Hann window lookup table.
        //
        // Done once at startup.
        // Float usage here is acceptable because this
        // does NOT run in the audio ISR.
        // ====================================================

        for (int32_t i = 0; i < GRAIN_SIZE; i++)
        {
            float phase =
                (2.0f * 3.14159265f * i)
                / (GRAIN_SIZE - 1);

            float w =
                0.5f * (1.0f - cosf(phase));

            hannWindow[i] =
                static_cast<int16_t>(w * 4096.0f);
        }

        // ====================================================
        // Voice A
        // ====================================================

        voiceA.grain1.phase = 0;
        voiceA.grain2.phase = HALF_GRAIN;

        voiceA.grain1.readHead = 0;
        voiceA.grain2.readHead =
            HALF_GRAIN << FP_SHIFT;

        // ====================================================
        // Voice B
        //
        // Offset read heads to decorrelate outputs.
        // ====================================================

        voiceB.grain1.phase = 0;
        voiceB.grain2.phase = HALF_GRAIN;

        voiceB.grain1.readHead =
            (BUFFER_SIZE / 3)
            << FP_SHIFT;

        voiceB.grain2.readHead =
            ((BUFFER_SIZE / 3)
            + HALF_GRAIN)
            << FP_SHIFT;
    }

    // ========================================================
    // Convert semitone offset into playback ratio
    //
    // Ratio stored as Q16 fixed-point.
    // ========================================================

    int32_t SemitoneToRatio(
        int32_t semitone)
    {
        float ratio =
            powf(
                2.0f,
                semitone / 12.0f
            );

        return
            static_cast<int32_t>(
                ratio * FP_ONE
            );
    }

    // ========================================================
    // Quantise semitone value to scale
    //
    // Behaves like a continuous note selector snapped
    // to nearest scale degree.
    // ========================================================

    int32_t QuantiseSemitone(
        int32_t semitone)
    {
        const int32_t* scale = majorScale;
        int32_t scaleLength = 7;

        switch (currentScale)
        {
            case 0:
                scale = majorScale;
                scaleLength = 7;
                break;

            case 1:
                scale = minorScale;
                scaleLength = 7;
                break;

            case 2:
                scale = pentScale;
                scaleLength = 5;
                break;

            case 3:
                scale = minorPentScale;
                scaleLength = 5;
                break;

            case 4:
                scale = bluesScale;
                scaleLength = 6;
                break;

            case 5:
                scale = wholeToneScale;
                scaleLength = 6;
                break;
        }

        int32_t octave =
            semitone / 12;

        int32_t note =
            semitone % 12;

        if (note < 0)
        {
            note += 12;
            octave--;
        }

        int32_t best =
            scale[0];

        int32_t bestDistance =
            99;

        for (int32_t i = 0;
             i < scaleLength;
             i++)
        {
            int32_t d =
                abs(note - scale[i]);

            if (d < bestDistance)
            {
                bestDistance = d;
                best = scale[i];
            }
        }

        return
            (octave * 12)
            + best;
    }

    // ========================================================
    // Convert knob/CV pair into semitone range
    //
    // -12 semitones fully CCW
    // +12 semitones fully CW
    // ========================================================

    int32_t PitchControl(
        int32_t knob,
        int32_t cv)
    {
        int32_t summed =
            knob + cv;

        if (summed < 0)
            summed = 0;

        if (summed > 4095)
            summed = 4095;

        return
            ((summed - 2048) * 24)
            / 4096;
    }

    // ========================================================
    // Render one grain
    // ========================================================

    int32_t RenderGrain(
        Grain& grain,
        int32_t ratio)
    {
        // ====================================================
        // Read interpolated sample
        // ====================================================

        int32_t index =
            (grain.readHead >> FP_SHIFT)
            & BUFFER_MASK;

        int32_t frac =
            grain.readHead
            & 0xFFFF;

        int32_t s1 =
            audioBuffer[index];

        int32_t s2 =
            audioBuffer[
                (index + 1)
                & BUFFER_MASK
            ];

        int32_t sample =
            s1 +
            (((s2 - s1)
            * frac)
            >> FP_SHIFT);

        // ====================================================
        // Apply Hann envelope
        // ====================================================

        int32_t env =
            hannWindow[grain.phase];

        sample =
            (sample * env)
            >> 12;

        // ====================================================
        // Advance playback
        // ====================================================

        grain.readHead += ratio;
        grain.phase++;

        // ====================================================
        // Restart grain when complete
        // ====================================================

        if (grain.phase >= GRAIN_SIZE)
        {
            grain.phase = 0;

            grain.readHead =
                ((writeHead
                - GRAIN_SIZE
                - HALF_GRAIN)
                & BUFFER_MASK)
                << FP_SHIFT;
        }

        return sample;
    }

    // ========================================================
    // Render one complete voice
    // ========================================================

    int32_t RenderVoice(
        Voice& voice,
        int32_t ratio)
    {
        int32_t out = 0;

        out +=
            RenderGrain(
                voice.grain1,
                ratio
            );

        out +=
            RenderGrain(
                voice.grain2,
                ratio
            );

        // Prevent clipping.

        out >>= 1;

        return out;
    }

    // ========================================================
    // LED scale display
    // ========================================================

    void UpdateLEDs(
        bool quantised)
    {
        for (int32_t i = 0;
             i < 6;
             i++)
        {
            LedOff(i);
        }

        // ====================================================
        // Top-left LED lights when quantiser enabled.
        // ====================================================

        LedOn(0, quantised);

        // ====================================================
        // Remaining LEDs show scale index.
        // ====================================================

        for (int32_t i = 0;
             i < currentScale;
             i++)
        {
            LedBrightness(
                i + 1,
                4095
            );
        }
    }

    // ========================================================
    // Main audio ISR
    //
    // MUST remain fast and deterministic.
    // ========================================================

    virtual void ProcessSample()
    {
        // ====================================================
        // Read incoming audio
        // ====================================================

        int32_t input =
            AudioIn1();

        // ====================================================
        // Store into circular delay buffer
        // ====================================================

        audioBuffer[writeHead] =
            input;

        writeHead =
            (writeHead + 1)
            & BUFFER_MASK;

        // ====================================================
        // Read controls
        // ====================================================

        int32_t mainTranspose =
            PitchControl(
                KnobVal(Knob::Main),
                2048
            );

        int32_t pitchA =
            PitchControl(
                KnobVal(Knob::X),
                CVIn1() + 2048
            );

        int32_t pitchB =
            PitchControl(
                KnobVal(Knob::Y),
                CVIn2() + 2048
            );

        pitchA += mainTranspose;
        pitchB += mainTranspose;

        // ====================================================
        // Switch handling
        //
        // Up     = unquantised
        // Middle = quantised
        // Down   = rotate scales
        // ====================================================

        bool quantised = false;

        int32_t sw =
            SwitchVal();

        if (sw == Switch::Middle)
        {
            quantised = true;
        }

        if (sw == Switch::Down
            && SwitchChanged())
        {
            currentScale++;

            if (currentScale
                >= SCALE_COUNT)
            {
                currentScale = 0;
            }
        }

        // ====================================================
        // Quantise pitch if enabled
        // ====================================================

        if (quantised)
        {
            pitchA =
                QuantiseSemitone(
                    pitchA
                );

            pitchB =
                QuantiseSemitone(
                    pitchB
                );
        }

        // ====================================================
        // Convert to playback ratios
        // ====================================================

        int32_t ratioA =
            SemitoneToRatio(
                pitchA
            );

        int32_t ratioB =
            SemitoneToRatio(
                pitchB
            );

        // ====================================================
        // Render pitch-shifted outputs
        // ====================================================

        int32_t outA =
            RenderVoice(
                voiceA,
                ratioA
            );

        int32_t outB =
            RenderVoice(
                voiceB,
                ratioB
            );

        // ====================================================
        // Audio outputs
        // ====================================================

        AudioOut1(outA);
        AudioOut2(outB);

        // ====================================================
        // CV outputs
        //
        // Proper calibrated 1V/oct behaviour.
        //
        // 1 semitone = 83.333mV
        // ====================================================

        int32_t mvA =
            pitchA * 83;

        int32_t mvB =
            pitchB * 83;

        CVOut1Millivolts(mvA);
        CVOut2Millivolts(mvB);

        // ====================================================
        // LED UI
        // ====================================================

        UpdateLEDs(quantised);
    }
};

// ============================================================
// Scale definitions
// ============================================================

const int32_t
DualQuantPitch::majorScale[7] =
{
    0, 2, 4, 5, 7, 9, 11
};

const int32_t
DualQuantPitch::minorScale[7] =
{
    0, 2, 3, 5, 7, 8, 10
};

const int32_t
DualQuantPitch::pentScale[5] =
{
    0, 2, 4, 7, 9
};

const int32_t
DualQuantPitch::minorPentScale[5] =
{
    0, 3, 5, 7, 10
};

const int32_t
DualQuantPitch::bluesScale[6] =
{
    0, 3, 5, 6, 7, 10
};

const int32_t
DualQuantPitch::wholeToneScale[6] =
{
    0, 2, 4, 6, 8, 10
};

// ============================================================
// Main entry point
// ============================================================

int main()
{
    set_sys_clock_khz(144000, true);

    DualQuantPitch card;

    card.Run();
}
