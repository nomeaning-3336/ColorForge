#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace cf {

/** 8-band stereo parametric EQ.
    Band layout is fixed: low cut, low shelf, four bells, high shelf, high cut.
    The cut bands have a selectable slope (12 / 24 / 36 / 48 dB per octave). */
class ParametricEQ
{
public:
    static constexpr int kNumBands = 8;
    static constexpr int kMaxSections = 4;

    enum class Type { LowCut, LowShelf, Bell, HighShelf, HighCut };

    static Type typeOfBand (int band) noexcept
    {
        switch (band)
        {
            case 0:  return Type::LowCut;
            case 1:  return Type::LowShelf;
            case 6:  return Type::HighShelf;
            case 7:  return Type::HighCut;
            default: return Type::Bell;
        }
    }

    struct BandParams
    {
        bool on = false;
        float freq = 1000.0f, gainDb = 0.0f, q = 0.707f;
        int slope = 0;   // cut bands: 0..3 -> 12..48 dB/oct
        int mode = 0;    // 0 = stereo (L and R), 1 = mid only, 2 = side only
        bool dynamic = false;          // bells / shelves: apply the gain only above the threshold
        float dynThresholdDb = -30.0f; // full gain 12 dB above it
    };

    /** Gain each band is applying right now (dynamic bands move), for the editor. */
    float getAppliedGainDb (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? appliedGain[(size_t) band].load (std::memory_order_relaxed) : 0.0f;
    }

    struct Design
    {
        std::array<BiquadCoefs, kMaxSections> sections {};
        int numSections = 0;

        double magnitude (double f, double sr) const noexcept
        {
            double m = 1.0;
            for (int i = 0; i < numSections; ++i)
                m *= sections[(size_t) i].magnitude (f, sr);
            return m;
        }
    };

    /** Shared by the DSP and the editor's curve display. */
    static Design design (Type type, double freq, double gainDb, double q, int slope, double sr) noexcept
    {
        Design d;

        if (type == Type::Bell)       { d.sections[0] = biquad::peak (freq, q, gainDb, sr);      d.numSections = 1; return d; }
        if (type == Type::LowShelf)   { d.sections[0] = biquad::lowShelf (freq, q, gainDb, sr);  d.numSections = 1; return d; }
        if (type == Type::HighShelf)  { d.sections[0] = biquad::highShelf (freq, q, gainDb, sr); d.numSections = 1; return d; }

        // Butterworth cascades; the user's Q scales the most resonant section (0.707 = flat).
        static constexpr double bq[4][4] = {
            { 0.70710678, 0.0,        0.0,        0.0 },
            { 0.54119610, 1.30656296, 0.0,        0.0 },
            { 0.51763809, 0.70710678, 1.93185165, 0.0 },
            { 0.50979558, 0.60134489, 0.89997622, 2.56291545 } };

        const int n = std::min (std::max (slope, 0), 3) + 1;
        const double qScale = std::fmax (q, 0.1) / 0.70710678;

        for (int i = 0; i < n; ++i)
        {
            double sq = bq[n - 1][i];
            if (i == n - 1)
                sq *= qScale;

            d.sections[(size_t) i] = type == Type::LowCut ? biquad::highPass (freq, sq, sr)
                                                          : biquad::lowPass (freq, sq, sr);
        }

        d.numSections = n;
        return d;
    }

    //==============================================================================
    void prepare (double sampleRate, int chunkSize)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        detAttack  = std::exp (-1.0 / (0.005 * sr));
        detRelease = std::exp (-1.0 / (0.120 * sr));
        smoothCoef = std::exp (-(double) chunkSize / (0.03 * sr));
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            for (auto& ch : b.filt)
                for (auto& f : ch)
                    f.clear();
            b.det.clear();
            b.env = 0.0;
            b.snap = true;
        }
        masterMix = masterTarget;
    }

    void setBand (int band, const BandParams& p) noexcept
    {
        if (band >= 0 && band < kNumBands)
            bands[(size_t) band].target = p;
    }

    void setEnabled (bool on) noexcept    { masterTarget = on ? 1.0 : 0.0; }

    /** Call once per chunk. */
    void process (float* l, float* r, int num) noexcept
    {
        masterMix = masterTarget + smoothCoef * (masterMix - masterTarget);
        if (std::abs (masterMix - masterTarget) < 1.0e-4)
            masterMix = masterTarget;

        if (masterMix <= 0.0 && masterTarget <= 0.0)
            return;

        // dynamic bands: measure the band's level over this chunk first
        for (int bi = 0; bi < kNumBands; ++bi)
        {
            auto& b = bands[(size_t) bi];
            const bool canBeDynamic = typeOfBand (bi) != Type::LowCut && typeOfBand (bi) != Type::HighCut;
            b.dynAmount = 1.0;

            if (! (canBeDynamic && b.target.dynamic && b.target.on))
            {
                b.env = 0.0;
                continue;
            }

            const double f = std::fmax (20.0, (double) b.target.freq);
            if (std::abs (f - b.detF) > 1.0e-3 * f)
            {
                b.det.c = biquad::bandPass (f, std::fmax (0.5, (double) b.target.q), sr);
                b.detF = f;
            }

            for (int n = 0; n < num; ++n)
            {
                const double m = b.target.mode == 2 ? 0.5 * ((double) l[n] - (double) r[n])
                                                    : 0.5 * ((double) l[n] + (double) r[n]);
                const double v = std::abs (b.det.process (m));
                b.env = v + (v > b.env ? detAttack : detRelease) * (b.env - v);
            }
            b.det.sanitise();

            const double lvl = 20.0 * std::log10 (std::fmax (b.env, 1.0e-9));
            b.dynAmount = std::clamp ((lvl - (double) b.target.dynThresholdDb) / 12.0, 0.0, 1.0);
        }

        for (int bi = 0; bi < kNumBands; ++bi)
            updateBand (bi);

        for (int n = 0; n < num; ++n)
        {
            double x[2] = { (double) l[n], (double) r[n] };
            double y[2] = { x[0], x[1] };

            for (auto& b : bands)
            {
                if (b.mix <= 0.0)
                    continue;

                if (b.mode == 0)
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        double v = y[ch];
                        for (int s = 0; s < b.d.numSections; ++s)
                            v = b.filt[(size_t) ch][(size_t) s].process (v);
                        y[ch] += b.mix * (v - y[ch]);
                    }
                }
                else
                {
                    // filter only the mid or the side component
                    double ms[2] = { 0.5 * (y[0] + y[1]), 0.5 * (y[0] - y[1]) };
                    double& c = ms[b.mode == 1 ? 0 : 1];
                    double v = c;
                    for (int s = 0; s < b.d.numSections; ++s)
                        v = b.filt[0][(size_t) s].process (v);
                    c += b.mix * (v - c);
                    y[0] = ms[0] + ms[1];
                    y[1] = ms[0] - ms[1];
                }
            }

            l[n] = (float) (x[0] + masterMix * (y[0] - x[0]));
            r[n] = (float) (x[1] + masterMix * (y[1] - x[1]));
        }

        for (auto& b : bands)
            for (auto& ch : b.filt)
                for (auto& f : ch)
                    f.sanitise();
    }

private:
    struct Band
    {
        BandParams target;
        double freq = 1000.0, gain = 0.0, q = 0.707, mix = 0.0;
        int slope = 0, mode = 0;
        bool snap = true;
        Design d;
        double lastF = -1.0, lastG = 1.0e9, lastQ = -1.0;
        int lastSlope = -1;
        Biquad det;
        double env = 0.0, detF = -1.0, dynAmount = 1.0;
        std::array<std::array<Biquad, kMaxSections>, 2> filt {};
    };

    void updateBand (int bi) noexcept
    {
        auto& b = bands[(size_t) bi];
        const auto& t = b.target;
        const double tf = std::log (std::fmax (10.0, (double) t.freq));
        const double tm = t.on ? 1.0 : 0.0;
        const double tg = (double) t.gainDb * b.dynAmount;

        if (b.snap)
        {
            b.freq = tf; b.gain = tg; b.q = t.q; b.mix = tm;
            b.snap = false;
        }
        else
        {
            b.freq = tf + smoothCoef * (b.freq - tf);
            b.gain = tg + smoothCoef * (b.gain - tg);
            b.q    = t.q + smoothCoef * (b.q - t.q);
            b.mix  = tm + smoothCoef * (b.mix - tm);
            if (std::abs (b.mix - tm) < 1.0e-4)
                b.mix = tm;
        }

        if (b.slope != t.slope || b.mode != t.mode)
        {
            b.slope = t.slope;
            b.mode = t.mode;
            for (auto& ch : b.filt)
                for (auto& f : ch)
                    f.clear();
        }

        const double f = std::exp (b.freq);

        if (std::abs (f - b.lastF) > 1.0e-3 * f || std::abs (b.gain - b.lastG) > 1.0e-3
            || std::abs (b.q - b.lastQ) > 1.0e-4 || b.slope != b.lastSlope)
        {
            b.d = design (typeOfBand (bi), f, b.gain, b.q, b.slope, sr);
            for (auto& ch : b.filt)
                for (int s = 0; s < kMaxSections; ++s)
                    ch[(size_t) s].c = b.d.sections[(size_t) s];

            b.lastF = f; b.lastG = b.gain; b.lastQ = b.q; b.lastSlope = b.slope;
        }

        appliedGain[(size_t) bi].store ((float) (b.gain * b.mix), std::memory_order_relaxed);
    }

    double sr = 48000.0, smoothCoef = 0.9, detAttack = 0.99, detRelease = 0.999;
    std::array<std::atomic<float>, kNumBands> appliedGain {};
    double masterMix = 1.0, masterTarget = 1.0;
    std::array<Band, kNumBands> bands {};
};

} // namespace cf
