#pragma once

#include "Biquad.h"

#include <algorithm>
#include <array>
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
    };

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

                for (int ch = 0; ch < 2; ++ch)
                {
                    double v = y[ch];
                    for (int s = 0; s < b.d.numSections; ++s)
                        v = b.filt[(size_t) ch][(size_t) s].process (v);
                    y[ch] += b.mix * (v - y[ch]);
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
        int slope = 0;
        bool snap = true;
        Design d;
        double lastF = -1.0, lastG = 1.0e9, lastQ = -1.0;
        int lastSlope = -1;
        std::array<std::array<Biquad, kMaxSections>, 2> filt {};
    };

    void updateBand (int bi) noexcept
    {
        auto& b = bands[(size_t) bi];
        const auto& t = b.target;
        const double tf = std::log (std::fmax (10.0, (double) t.freq));
        const double tm = t.on ? 1.0 : 0.0;

        if (b.snap)
        {
            b.freq = tf; b.gain = t.gainDb; b.q = t.q; b.mix = tm;
            b.snap = false;
        }
        else
        {
            b.freq = tf + smoothCoef * (b.freq - tf);
            b.gain = t.gainDb + smoothCoef * (b.gain - t.gainDb);
            b.q    = t.q + smoothCoef * (b.q - t.q);
            b.mix  = tm + smoothCoef * (b.mix - tm);
            if (std::abs (b.mix - tm) < 1.0e-4)
                b.mix = tm;
        }

        if (b.slope != t.slope)
        {
            b.slope = t.slope;
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
    }

    double sr = 48000.0, smoothCoef = 0.9;
    double masterMix = 1.0, masterTarget = 1.0;
    std::array<Band, kNumBands> bands {};
};

} // namespace cf
