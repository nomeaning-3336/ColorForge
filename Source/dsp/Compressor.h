#pragma once

#include "Biquad.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace cf {

/** Stereo-linked feed-forward bus compressor.
    Peak/RMS blend detector with a sidechain high-pass, soft-knee gain computer,
    log-domain attack / release, auto makeup and parallel mix. */
class Compressor
{
public:
    struct Params
    {
        bool  on = false;
        float thresholdDb = -18.0f, ratio = 2.0f, attackMs = 10.0f, releaseMs = 150.0f;
        float kneeDb = 6.0f, makeupDb = 0.0f, mix = 1.0f, scHpfHz = 20.0f;
        bool  autoMakeup = true;
    };

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        paramCoef = std::exp (-1.0 / (0.02 * sr));
        rmsCoef = std::exp (-1.0 / (0.005 * sr));
        reset();
    }

    void reset()
    {
        for (auto& f : scHpf) f.clear();
        envDb = 0.0;
        rms = 0.0;
        onMix = target.on ? 1.0 : 0.0;
        makeupLin = 1.0;
        lastHpf = -1.0;
        grDb.store (0.0f, std::memory_order_relaxed);
    }

    void setParams (const Params& p) noexcept { target = p; }

    /** Current gain reduction in dB (positive number), for metering. */
    std::atomic<float> grDb { 0.0f };

    void process (float* l, float* r, int num) noexcept
    {
        const double onTarget = target.on ? 1.0 : 0.0;

        if (onMix <= 0.0 && onTarget <= 0.0)
        {
            grDb.store (0.0f, std::memory_order_relaxed);
            envDb = 0.0;
            return;
        }

        const double thr   = target.thresholdDb;
        const double ratio = std::max (1.0, (double) target.ratio);
        const double knee  = std::max (0.0, (double) target.kneeDb);
        const double slope = 1.0 - 1.0 / ratio;
        const double atk   = std::exp (-1.0 / (std::max (0.05, (double) target.attackMs) * 0.001 * sr));
        const double rel   = std::exp (-1.0 / (std::max (5.0, (double) target.releaseMs) * 0.001 * sr));

        // Auto makeup restores roughly half the reduction a signal at 0 dBFS would get.
        double mkDb = target.makeupDb;
        if (target.autoMakeup)
            mkDb += -thr * slope * 0.5;
        const double mkTarget = std::pow (10.0, mkDb / 20.0);

        if (std::abs (target.scHpfHz - lastHpf) > 0.01)
        {
            lastHpf = target.scHpfHz;
            const auto c = biquad::highPass (std::max (10.0, lastHpf), 0.7071, sr);
            for (auto& f : scHpf) f.c = c;
        }
        const bool useHpf = target.scHpfHz > 20.5f;

        double maxGr = 0.0;

        for (int n = 0; n < num; ++n)
        {
            onMix = onTarget + paramCoef * (onMix - onTarget);
            makeupLin = mkTarget + paramCoef * (makeupLin - mkTarget);
            wetMix = (double) target.mix + paramCoef * (wetMix - (double) target.mix);

            double sl = l[n], sr2 = r[n];
            if (useHpf)
            {
                sl = scHpf[0].process (sl);
                sr2 = scHpf[1].process (sr2);
            }

            // detector: average of peak and short RMS, linked across channels
            const double pk = std::max (std::abs (sl), std::abs (sr2));
            rms = rmsCoef * rms + (1.0 - rmsCoef) * pk * pk;
            const double level = 0.5 * (pk + std::sqrt (rms));
            const double inDb = 20.0 * std::log10 (std::max (level, 1.0e-9));

            // soft-knee static curve: gain reduction in dB (>= 0)
            double over = inDb - thr;
            double gr;
            if (knee > 0.0 && std::abs (over) <= knee * 0.5)
            {
                const double t = over + knee * 0.5;
                gr = slope * t * t / (2.0 * knee);
            }
            else
            {
                gr = over > 0.0 ? slope * over : 0.0;
            }

            // ballistics on the reduction amount
            const double c = gr > envDb ? atk : rel;
            envDb = gr + c * (envDb - gr);
            maxGr = std::max (maxGr, envDb);

            const double g = std::pow (10.0, -envDb / 20.0) * makeupLin;
            const double wet = wetMix;
            const double gain = (1.0 - wet) + wet * g;
            const double applied = 1.0 + onMix * (gain - 1.0);

            l[n] = (float) (l[n] * applied);
            r[n] = (float) (r[n] * applied);
        }

        for (auto& f : scHpf) f.sanitise();
        grDb.store ((float) (maxGr * onMix), std::memory_order_relaxed);
    }

    /** Static curve, for the editor's transfer graph. Returns output dB for an input dB. */
    static double staticCurve (double inDb, double thr, double ratio, double knee) noexcept
    {
        const double slope = 1.0 - 1.0 / std::max (1.0, ratio);
        const double over = inDb - thr;
        if (knee > 0.0 && std::abs (over) <= knee * 0.5)
        {
            const double t = over + knee * 0.5;
            return inDb - slope * t * t / (2.0 * knee);
        }
        return over > 0.0 ? inDb - slope * over : inDb;
    }

private:
    Params target;
    double sr = 48000.0, paramCoef = 0.999, rmsCoef = 0.99;
    double envDb = 0.0, rms = 0.0, onMix = 0.0, makeupLin = 1.0, wetMix = 1.0, lastHpf = -1.0;
    Biquad scHpf[2];
};

} // namespace cf
