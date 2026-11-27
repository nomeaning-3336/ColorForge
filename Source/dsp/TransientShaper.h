#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace cf {

/** Level-independent transient shaper (attack / sustain), stereo linked.

    Two envelope pairs follow the louder channel:
      - attack:  a fast-attack follower against a slow-attack one. Their ratio in dB
                 is positive only while the level is rising, i.e. on the onset.
      - sustain: a slow-release follower against a fast-release one. Their ratio is
                 positive only while the level is falling, i.e. on the tail.
    Because both are ratios, the gain curve does not depend on the input level, so
    there is no threshold to set. ATTACK and SUSTAIN (-1..+1) scale each ratio into
    a gain change in dB, clamped to +-kMaxDb. */
class TransientShaper
{
public:
    static constexpr double kMaxDb = 18.0;

    struct Params
    {
        bool  on = false;
        float attack = 0.0f, sustain = 0.0f;   // -1..+1
        float mix = 1.0f, outDb = 0.0f;
    };

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        fastAtt  = coef (0.0005);
        slowAtt  = coef (0.020);
        fastRel  = coef (0.040);
        slowRel  = coef (0.400);
        commonRel = coef (0.120);
        gainCoef = coef (0.0002);
        peakRel = coef (0.020);
        holdLen = (int) (0.010 * sr);   // longer than half a period of anything above 50 Hz
        paramCoef = coef (0.02);
        reset();
    }

    void reset()
    {
        envFastA = envSlowA = envFastR = envSlowR = peak = 0.0;
        hold = 0;
        gainDb = attDb = susDb = 0.0;
        onMix = target.on ? 1.0 : 0.0;
        att = target.attack;
        sus = target.sustain;
        mix = target.mix;
        out = std::pow (10.0, target.outDb / 20.0);
    }

    void setParams (const Params& p) noexcept { target = p; }

    /** Largest boost and cut applied since the last call, in dB (UI thread). */
    std::pair<float, float> takeGainRangeDb() noexcept
    {
        return { meterUp.exchange (0.0f, std::memory_order_relaxed), meterDown.exchange (0.0f, std::memory_order_relaxed) };
    }

    void process (float* l, float* r, int num) noexcept
    {
        const double onTarget = target.on ? 1.0 : 0.0;
        if (onTarget == 0.0 && onMix < 1.0e-5)
        {
            onMix = 0.0;
            // keep the detector warm so switching on mid-note doesn't pop
            for (int i = 0; i < num; ++i)
                detect (std::max (std::abs ((double) l[i]), std::abs ((double) r[i])));
            return;
        }

        const double outTarget = std::pow (10.0, target.outDb / 20.0);
        constexpr double tiny = 1.0e-9;
        double up = 0.0, down = 0.0;

        for (int i = 0; i < num; ++i)
        {
            onMix = onTarget + paramCoef * (onMix - onTarget);
            att   = target.attack  + paramCoef * (att - target.attack);
            sus   = target.sustain + paramCoef * (sus - target.sustain);
            mix   = target.mix     + paramCoef * (mix - target.mix);
            out   = outTarget      + paramCoef * (out - outTarget);

            const double x = std::max (std::abs ((double) l[i]), std::abs ((double) r[i]));
            detect (x);

            const double attDiff = 20.0 * std::log10 ((envFastA + tiny) / (envSlowA + tiny));   // >= 0 on onsets
            const double susDiff = 20.0 * std::log10 ((envSlowR + tiny) / (envFastR + tiny));   // >= 0 on tails

            // the sustain part lets go at once when a new hit arrives (its detector has just
            // reset), so tails don't leak onto the next onset; otherwise both glide
            const double wantSus = sus * std::max (0.0, susDiff);
            susDb = susDiff <= 0.0 ? 0.0 : wantSus + gainCoef * (susDb - wantSus);
            const double wantAtt = att * std::max (0.0, attDiff);
            attDb = wantAtt + gainCoef * (attDb - wantAtt);
            gainDb = std::clamp (attDb + susDb, -kMaxDb, kMaxDb);

            const double g = std::pow (10.0, gainDb / 20.0);
            const double wet = mix * g + (1.0 - mix);       // parallel mix of shaped and dry
            const double gain = (1.0 - onMix) + onMix * wet * out;

            l[i] = (float) (l[i] * gain);
            r[i] = (float) (r[i] * gain);

            const double shown = gainDb * onMix * mix;
            up = std::max (up, shown);
            down = std::min (down, shown);
        }

        if (up > meterUp.load (std::memory_order_relaxed))     meterUp.store ((float) up, std::memory_order_relaxed);
        if (down < meterDown.load (std::memory_order_relaxed)) meterDown.store ((float) down, std::memory_order_relaxed);
    }

private:
    double coef (double seconds) const { return std::exp (-1.0 / (seconds * sr)); }

    void detect (double in) noexcept
    {
        // peak hold first, so the followers see the level and not the waveform's ripple
        if (in >= peak)                { peak = in; hold = holdLen; }
        else if (hold > 0)             --hold;
        else                           peak = in + peakRel * (peak - in);
        const double x = peak;

        // attack pair: different attack times, shared release
        envFastA = x > envFastA ? x + fastAtt * (envFastA - x) : x + commonRel * (envFastA - x);
        envSlowA = x > envSlowA ? x + slowAtt * (envSlowA - x) : x + commonRel * (envSlowA - x);
        // sustain pair: instant attack (so a new hit resets the tail at once), different releases
        envFastR = x > envFastR ? x : x + fastRel * (envFastR - x);
        envSlowR = x > envSlowR ? x : x + slowRel * (envSlowR - x);
    }

    double sr = 48000.0;
    double fastAtt = 0, slowAtt = 0, fastRel = 0, slowRel = 0, commonRel = 0, gainCoef = 0, paramCoef = 0;
    double peakRel = 0, peak = 0;
    int holdLen = 480, hold = 0;
    double envFastA = 0, envSlowA = 0, envFastR = 0, envSlowR = 0, gainDb = 0, attDb = 0, susDb = 0;
    double onMix = 0, att = 0, sus = 0, mix = 1, out = 1;
    std::atomic<float> meterUp { 0.0f }, meterDown { 0.0f };
    Params target;
};

} // namespace cf
