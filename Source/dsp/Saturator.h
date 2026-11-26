#pragma once

#include "Biquad.h"

#include <algorithm>
#include <cmath>

namespace cf {

/** Stereo saturator with three curves (tape, tube, clip), level compensation,
    a post tilt tone control and a parallel mix. */
class Saturator
{
public:
    enum Mode { Tape = 0, Tube = 1, Clip = 2 };

    struct Params
    {
        bool  on = false;
        int   mode = Tape;
        float driveDb = 6.0f, tone = 0.0f /* -1..1 */, mix = 1.0f, outDb = 0.0f;
    };

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        paramCoef = std::exp (-1.0 / (0.02 * sr));
        dcCoef = std::exp (-2.0 * 3.14159265358979323846 * 8.0 / sr);
        reset();
    }

    void reset()
    {
        for (auto& f : tiltLo) f.clear();
        for (auto& f : tiltHi) f.clear();
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;
        onMix = target.on ? 1.0 : 0.0;
        lastTone = 1.0e9f;
        drive = std::pow (10.0, target.driveDb / 20.0);
    }

    void setParams (const Params& p) noexcept { target = p; }

    static double shape (int mode, double x) noexcept
    {
        switch (mode)
        {
            case Tube:
            {
                // asymmetric: the positive half compresses earlier, adds even harmonics
                constexpr double bias = 0.25;
                return std::tanh (x + bias) - std::tanh (bias);
            }
            case Clip:
            {
                constexpr double knee = 0.85;
                const double a = std::abs (x);
                if (a <= knee) return x;
                const double y = a < 1.0 + (1.0 - knee)
                               ? knee + (a - knee) - (a - knee) * (a - knee) / (4.0 * (1.0 - knee))
                               : 1.0;
                return x < 0.0 ? -std::min (1.0, y) : std::min (1.0, y);
            }
            default:
            {
                // tape: soft symmetric curve with a gentler onset than tanh
                return x / std::sqrt (1.0 + x * x * 0.85) * 0.95 + 0.05 * std::tanh (x);
            }
        }
    }

    void process (float* l, float* r, int num) noexcept
    {
        const double onTarget = target.on ? 1.0 : 0.0;
        if (onMix <= 0.0 && onTarget <= 0.0)
            return;

        if (target.tone != lastTone)
        {
            lastTone = target.tone;
            const double t = std::clamp ((double) target.tone, -1.0, 1.0) * 6.0;
            for (auto& f : tiltLo) f.c = biquad::lowShelf (700.0, 0.6, -t, sr);
            for (auto& f : tiltHi) f.c = biquad::highShelf (700.0, 0.6, t, sr);
        }

        const int mode = std::clamp (target.mode, 0, 2);
        const double driveTarget = std::pow (10.0, std::clamp ((double) target.driveDb, 0.0, 36.0) / 20.0);
        const double outTarget = std::pow (10.0, target.outDb / 20.0);

        for (int n = 0; n < num; ++n)
        {
            onMix = onTarget + paramCoef * (onMix - onTarget);
            drive = driveTarget + paramCoef * (drive - driveTarget);
            outGain = outTarget + paramCoef * (outGain - outTarget);
            wet = (double) target.mix + paramCoef * (wet - (double) target.mix);

            // keep a -12 dBFS signal at about the same level whatever the drive
            const double comp = 0.5 / std::max (1.0e-6, std::abs (shape (mode, 0.25 * drive)) + std::abs (shape (mode, -0.25 * drive)));

            double io[2] = { (double) l[n], (double) r[n] };
            for (int ch = 0; ch < 2; ++ch)
            {
                const double x = io[ch];
                double y = shape (mode, x * drive) * comp;

                // DC blocker (the tube curve is asymmetric)
                const double d = y - dcX[ch] + dcCoef * dcY[ch];
                dcX[ch] = y;
                dcY[ch] = d;
                y = d;

                y = tiltHi[ch].process (tiltLo[ch].process (y));
                y = x + wet * (y - x);
                y *= outGain;
                io[ch] = x + onMix * (y - x);
            }

            l[n] = (float) io[0];
            r[n] = (float) io[1];
        }

        for (auto& f : tiltLo) f.sanitise();
        for (auto& f : tiltHi) f.sanitise();
    }

private:
    Params target;
    double sr = 48000.0, paramCoef = 0.999, dcCoef = 0.999;
    double onMix = 0.0, drive = 1.0, outGain = 1.0, wet = 1.0;
    double dcX[2] {}, dcY[2] {};
    float lastTone = 1.0e9f;
    Biquad tiltLo[2], tiltHi[2];
};

} // namespace cf
