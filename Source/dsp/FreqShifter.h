#pragma once

#include <cmath>

namespace cf {

class FreqShifter {
public:
    FreqShifter() { updateSmoothingCoeff(); reset(); }

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 44100.0;
        updateSmoothingCoeff();
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            chans[c].resetState();
            chans[c].phase = 0.0;
        }
        sShift = tShift; sSpread = tSpread; sFb = tFb; sMix = tMix;
    }

    // shiftHz: -5000..5000 (positive = shift up).
    // stereoSpreadHz: 0..200. Left = shiftHz + spread/2, right = shiftHz - spread/2.
    // feedback: 0..0.95. mix: 0..1 dry/wet.
    void setParams (float shiftHz, float stereoSpreadHz, float feedback, float mix)
    {
        tShift  = clampD (finiteOr (static_cast<double> (shiftHz), 0.0), -5000.0, 5000.0);
        tSpread = clampD (finiteOr (static_cast<double> (stereoSpreadHz), 0.0), 0.0, 200.0);
        tFb     = clampD (finiteOr (static_cast<double> (feedback), 0.0), 0.0, 0.95);
        tMix    = clampD (finiteOr (static_cast<double> (mix), 0.0), 0.0, 1.0);
        if (! paramsSet)
        {
            paramsSet = true;
            sShift = tShift; sSpread = tSpread; sFb = tFb; sMix = tMix;
        }
    }

    void process (float* left, float* right, int numSamples)
    {
        if (numSamples <= 0) return;
        float* bufs[2] = { left, right };
        const double twoPiOverSr = kTwoPi / sr;

        for (int n = 0; n < numSamples; ++n)
        {
            sShift  += smCoeff * (tShift  - sShift);
            sSpread += smCoeff * (tSpread - sSpread);
            sFb     += smCoeff * (tFb     - sFb);
            sMix    += smCoeff * (tMix    - sMix);
            sShift  = flushD (sShift);
            sSpread = flushD (sSpread);
            sFb     = flushD (sFb);
            sMix    = flushD (sMix);

            const double halfSpread = 0.5 * sSpread;
            const double hz[2] = { sShift + halfSpread, sShift - halfSpread };

            for (int c = 0; c < 2; ++c)
            {
                float* buf = bufs[c];
                if (buf == nullptr) continue;
                if (c == 1 && right == left) continue;

                Channel& ch = chans[c];
                double dry = static_cast<double> (buf[n]);
                if (! std::isfinite (dry)) dry = 0.0;

                const double in = dry + sFb * softClip (ch.fbSample);
                const double wet = ch.shift (in);
                double out = wet;

                if (! std::isfinite (out) || std::fabs (out) > 1.0e6)
                {
                    ch.resetState();
                    out = 0.0;
                }
                ch.fbSample = flushD (out);

                double ph = ch.phase + hz[c] * twoPiOverSr;
                if (ph >= kTwoPi) ph -= kTwoPi;
                else if (ph < 0.0) ph += kTwoPi;
                if (! (ph >= 0.0 && ph < kTwoPi)) ph = 0.0;
                ch.phase = ph;

                const double y = dry + sMix * (out - dry);
                buf[n] = static_cast<float> (std::isfinite (y) ? y : 0.0);
            }
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kTwoPi = 2.0 * kPi;

    struct Section
    {
        double a = 0.0, x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
        inline double tick (double x)
        {
            double y = a * (x + y2) - x2;
            if (std::fabs (y) < 1.0e-25) y = 0.0;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void clear() { x1 = x2 = y1 = y2 = 0.0; }
    };

    struct Channel
    {
        Section A[4], B[4];
        double aDelay = 0.0;
        double fbSample = 0.0;
        double phase = 0.0;

        Channel()
        {
            static const double ca[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
            static const double cb[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
            // Niemitalo lists a; the section coefficient is a^2
            for (int i = 0; i < 4; ++i) { A[i].a = ca[i] * ca[i]; B[i].a = cb[i] * cb[i]; }
        }

        void resetState()
        {
            for (int i = 0; i < 4; ++i) { A[i].clear(); B[i].clear(); }
            aDelay = 0.0;
            fbSample = 0.0;
        }

        inline double shift (double x)
        {
            double a = x, b = x;
            for (int i = 0; i < 4; ++i) { a = A[i].tick (a); b = B[i].tick (b); }
            const double I = aDelay;
            aDelay = a;
            const double Q = b;
            return I * std::cos (phase) + Q * std::sin (phase);
        }
    };

    static inline double softClip (double x)
    {
        // smooth saturator, ~linear for small x, asymptote at +-1.5
        return 1.5 * std::tanh (x / 1.5);
    }
    static inline double flushD (double v) { return (std::fabs (v) < 1.0e-25) ? 0.0 : v; }
    static inline double clampD (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static inline double finiteOr (double v, double d) { return std::isfinite (v) ? v : d; }

    void updateSmoothingCoeff()
    {
        smCoeff = 1.0 - std::exp (-1.0 / (0.02 * sr));
    }

    double sr = 44100.0;
    double smCoeff = 0.0;
    bool paramsSet = false;

    double tShift = 0.0, tSpread = 0.0, tFb = 0.0, tMix = 1.0;
    double sShift = 0.0, sSpread = 0.0, sFb = 0.0, sMix = 1.0;

    Channel chans[2];
};

} // namespace cf
