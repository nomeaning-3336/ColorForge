#pragma once

#include "TruePeak.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace cf {

/** Lookahead brickwall limiter with true-peak detection.
    Gain = sliding minimum of the required gain over the lookahead window,
    released with a one-pole, then box-averaged over the same window so the
    gain is already down when a peak arrives. Latency is constant whatever the
    mode, so hosts never see it change. */
class Limiter
{
public:
    enum Mode { Clean = 0, Clip = 1 };

    struct Params
    {
        bool  on = true;
        int   mode = Clean;
        float gainDb = 0.0f, ceilingDb = -1.0f, releaseMs = 100.0f;
    };

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        look = std::max (8, (int) std::lround (0.0015 * sr));
        delay = look - 1 + TruePeak::kDelay;

        minVal.assign ((size_t) look + 2, 1.0);
        minIdx.assign ((size_t) look + 2, 0);
        avgBuf.assign ((size_t) look, 1.0);
        for (auto& d : sigBuf)
            d.assign ((size_t) delay + 1, 0.0);

        paramCoef = std::exp (-1.0 / (0.02 * sr));
        reset();
    }

    void reset()
    {
        for (auto& t : tp) t.reset();
        std::fill (avgBuf.begin(), avgBuf.end(), 1.0);
        for (auto& d : sigBuf) std::fill (d.begin(), d.end(), 0.0);
        head = tail = 0;
        sampleIndex = 0;
        avgSum = (double) look;
        avgPos = sigPos = 0;
        relEnv = 1.0;
        inGain = std::pow (10.0, target.gainDb / 20.0);
        grDb.store (0.0f, std::memory_order_relaxed);
    }

    void setParams (const Params& p) noexcept   { target = p; }
    int getLatencySamples() const noexcept     { return delay; }

    std::atomic<float> grDb { 0.0f };

    void process (float* l, float* r, int num) noexcept
    {
        const double gainTarget = target.on ? std::pow (10.0, target.gainDb / 20.0) : 1.0;
        const double ceil = std::pow (10.0, std::min (0.0f, target.ceilingDb) / 20.0);
        const double rel = std::exp (-1.0 / (std::max (1.0, (double) target.releaseMs) * 0.001 * sr));
        const int mode = target.on ? std::clamp (target.mode, 0, 1) : -1;
        double minGain = 1.0;

        for (int n = 0; n < num; ++n)
        {
            inGain = gainTarget + paramCoef * (inGain - gainTarget);
            const double xl = l[n] * inGain, xr = r[n] * inGain;

            // ---- detector (true peak, kDelay samples behind the input)
            const double pk = std::max (tp[0].push (xl), tp[1].push (xr));
            const double req = pk > ceil ? ceil / pk : 1.0;

            // sliding minimum over the last `look` values (monotonic deque)
            const int cap = look + 2;
            while (head != tail && minVal[(size_t) ((tail + cap - 1) % cap)] >= req)
                tail = (tail + cap - 1) % cap;
            minVal[(size_t) tail] = req;
            minIdx[(size_t) tail] = sampleIndex;
            tail = (tail + 1) % cap;
            while (minIdx[(size_t) head] <= sampleIndex - look)
                head = (head + 1) % cap;
            const double m = minVal[(size_t) head];
            ++sampleIndex;

            // release, then box average
            relEnv = m < relEnv ? m : m + rel * (relEnv - m);
            avgSum += relEnv - avgBuf[(size_t) avgPos];
            avgBuf[(size_t) avgPos] = relEnv;
            if (++avgPos >= look)
            {
                avgPos = 0;
                double s = 0.0;                 // refresh the running sum to stop drift
                for (double v : avgBuf) s += v;
                avgSum = s;
            }
            const double g = std::min (1.0, avgSum / (double) look);

            // ---- delayed signal
            const int rd = (sigPos + 1) % (delay + 1);
            sigBuf[0][(size_t) sigPos] = xl;
            sigBuf[1][(size_t) sigPos] = xr;
            double yl = sigBuf[0][(size_t) rd];
            double yr = sigBuf[1][(size_t) rd];
            sigPos = rd;

            if (mode == Clean)
            {
                yl = std::clamp (yl * g, -ceil, ceil);
                yr = std::clamp (yr * g, -ceil, ceil);
                minGain = std::min (minGain, g);
            }
            else if (mode == Clip)
            {
                yl = softClip (yl / ceil) * ceil;
                yr = softClip (yr / ceil) * ceil;
            }

            l[n] = (float) yl;
            r[n] = (float) yr;
        }

        grDb.store ((float) (-20.0 * std::log10 (std::max (minGain, 1.0e-6))), std::memory_order_relaxed);
    }

    static double softClip (double x) noexcept
    {
        constexpr double knee = 0.75;
        const double a = std::abs (x);
        if (a <= knee)
            return x;
        const double y = knee + (1.0 - knee) * std::tanh ((a - knee) / (1.0 - knee));
        return x < 0.0 ? -y : y;
    }

private:
    Params target;
    double sr = 48000.0, paramCoef = 0.999, inGain = 1.0, relEnv = 1.0, avgSum = 0.0;
    int look = 72, delay = 77;
    TruePeak tp[2];

    std::vector<double> minVal;
    std::vector<long long> minIdx;
    int head = 0, tail = 0;
    long long sampleIndex = 0;

    std::vector<double> avgBuf;
    int avgPos = 0;

    std::vector<double> sigBuf[2];
    int sigPos = 0;
};

} // namespace cf
