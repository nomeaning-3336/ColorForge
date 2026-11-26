#pragma once

#include "Biquad.h"

#include <algorithm>
#include <cmath>

namespace cf {

/** Master stereo stage: mid/side width, plus an optional mono low end
    (Linkwitz-Riley 4th-order split; the low band is summed to mono). */
class StereoImager
{
public:
    struct Params
    {
        bool  on = true;
        float width = 1.0f;          // 0 = mono, 1 = unchanged, 2 = extra wide
        bool  monoBass = false;
        float monoFreq = 120.0f;
        float balance = 0.0f;        // -1 = left, +1 = right
    };

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        paramCoef = std::exp (-1.0 / (0.02 * sr));
        lastFreq = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& ch : lp) for (auto& f : ch) f.clear();
        for (auto& ch : hp) for (auto& f : ch) f.clear();
        width = target.on ? target.width : 1.0;
        mono = (target.on && target.monoBass) ? 1.0 : 0.0;
        bal = target.on ? target.balance : 0.0;
    }

    void setParams (const Params& p) noexcept  { target = p; }

    void process (float* l, float* r, int num) noexcept
    {
        const double wT = target.on ? std::clamp ((double) target.width, 0.0, 2.0) : 1.0;
        const double mT = (target.on && target.monoBass) ? 1.0 : 0.0;
        const double bT = target.on ? std::clamp ((double) target.balance, -1.0, 1.0) : 0.0;

        if (std::abs (width - 1.0) < 1.0e-6 && wT == 1.0 && mono <= 0.0 && mT <= 0.0
            && std::abs (bal) < 1.0e-6 && bT == 0.0)
            return;

        if (target.monoFreq != lastFreq)
        {
            lastFreq = target.monoFreq;
            const auto lc = biquad::lowPass (std::clamp ((double) lastFreq, 20.0, 1000.0), 0.70710678, sr);
            const auto hc = biquad::highPass (std::clamp ((double) lastFreq, 20.0, 1000.0), 0.70710678, sr);
            for (auto& ch : lp) for (auto& f : ch) f.c = lc;
            for (auto& ch : hp) for (auto& f : ch) f.c = hc;
        }

        for (int n = 0; n < num; ++n)
        {
            width = wT + paramCoef * (width - wT);
            mono  = mT + paramCoef * (mono - mT);
            bal   = bT + paramCoef * (bal - bT);

            double xl = l[n], xr = r[n];

            if (mono > 1.0e-5)
            {
                const double lol = lp[0][1].process (lp[0][0].process (xl));
                const double lor = lp[1][1].process (lp[1][0].process (xr));
                const double hil = hp[0][1].process (hp[0][0].process (xl));
                const double hir = hp[1][1].process (hp[1][0].process (xr));
                const double lm = 0.5 * (lol + lor);
                const double outLoL = lol + mono * (lm - lol);
                const double outLoR = lor + mono * (lm - lor);
                // blend back towards the unsplit signal as mono fades out
                xl = xl + mono * ((outLoL + hil) - xl);
                xr = xr + mono * ((outLoR + hir) - xr);
            }

            const double mid = 0.5 * (xl + xr);
            const double side = 0.5 * (xl - xr) * width;
            xl = mid + side;
            xr = mid - side;

            // constant-power-ish balance: only attenuate the opposite side
            if (bal > 0.0) xl *= 1.0 - bal;
            else           xr *= 1.0 + bal;

            l[n] = (float) xl;
            r[n] = (float) xr;
        }

        for (auto& ch : lp) for (auto& f : ch) f.sanitise();
        for (auto& ch : hp) for (auto& f : ch) f.sanitise();
    }

private:
    Params target;
    double sr = 48000.0, paramCoef = 0.999, width = 1.0, mono = 0.0, bal = 0.0;
    float lastFreq = -1.0f;
    Biquad lp[2][2], hp[2][2];
};

} // namespace cf
