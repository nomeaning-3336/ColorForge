#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace cf {

/** Modulation sources: two LFOs (free or tempo-synced), an envelope follower on the input,
    and four macros. Source indices match modSourceNames(): 0 off, 1-2 LFOs, 3 envelope,
    4-7 macros. LFOs are bipolar (-1..1); the envelope and macros are unipolar (0..1).
    Runs on the audio thread at control rate; `display` holds the values for the editor. */
class ModEngine
{
public:
    static constexpr int kNumSources = 8;
    enum Shape { sine = 0, triangle, sawUp, sawDown, square, random };

    struct Lfo
    {
        int shape = sine;
        float rateHz = 1.0f;
        bool sync = false;
        double beats = 4.0;     // quarter notes per cycle when synced
    };

    std::array<std::atomic<float>, kNumSources> display {};

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        reset();
    }

    void reset()
    {
        phase.fill (0.0);
        held = { 0.0, 0.0 };
        env = 0.0;
        values.fill (0.0f);
        for (auto& d : display)
            d.store (0.0f, std::memory_order_relaxed);
    }

    void setLfo (int i, const Lfo& p) noexcept                  { if (i >= 0 && i < 2) lfo[(size_t) i] = p; }
    void setMacro (int i, float v01) noexcept                   { if (i >= 0 && i < 4) macro[(size_t) i] = std::clamp (v01, 0.0f, 1.0f); }

    void setEnvelope (float attackMs, float releaseMs, float gainDb) noexcept
    {
        envAtt = std::exp (-1.0 / (std::max (0.1, (double) attackMs) * 0.001 * sr));
        envRel = std::exp (-1.0 / (std::max (1.0, (double) releaseMs) * 0.001 * sr));
        envGain = std::pow (10.0, gainDb / 20.0);
    }

    /** Advances every source by one chunk. `ppq` is the host position at the chunk start
        (quarter notes), used to lock synced LFOs to the bar when the host is playing. */
    void process (const float* l, const float* r, int num, double bpm, bool playing, double ppq) noexcept
    {
        bpm = (bpm > 20.0 && bpm < 999.0) ? bpm : 120.0;

        for (int n = 0; n < num; ++n)
        {
            const double x = std::max (std::abs ((double) l[n]), std::abs ((double) r[n])) * envGain;
            env = x + (x > env ? envAtt : envRel) * (env - x);
        }
        if (! std::isfinite (env) || env < 1.0e-12)
            env = 0.0;

        for (size_t i = 0; i < 2; ++i)
        {
            const auto& p = lfo[i];
            const double prev = phase[i];
            if (p.sync && playing && std::isfinite (ppq))
            {
                const double c = ppq / std::max (1.0e-3, p.beats);
                phase[i] = c - std::floor (c);
            }
            else
            {
                const double hz = p.sync ? bpm / 60.0 / std::max (1.0e-3, p.beats) : (double) p.rateHz;
                phase[i] += hz * (double) num / sr;
                phase[i] -= std::floor (phase[i]);
            }
            if (phase[i] < prev)   // wrapped: new random step
                held[i] = nextRandom();
            values[1 + i] = (float) shapeValue (p.shape, phase[i], held[i]);
        }

        const double envDb = env > 1.0e-6 ? 20.0 * std::log10 (env) : -120.0;
        values[3] = (float) std::clamp ((envDb + 60.0) / 60.0, 0.0, 1.0);
        for (size_t m = 0; m < 4; ++m)
            values[4 + m] = macro[m];

        for (size_t i = 0; i < (size_t) kNumSources; ++i)
            display[i].store (values[i], std::memory_order_relaxed);
    }

    float value (int source) const noexcept
    {
        return source > 0 && source < kNumSources ? values[(size_t) source] : 0.0f;
    }

    static double shapeValue (int shape, double ph, double randomHeld) noexcept
    {
        constexpr double twoPi = 6.283185307179586;
        switch (shape)
        {
            case triangle: return 1.0 - 4.0 * std::abs (ph - 0.5);
            case sawUp:    return 2.0 * ph - 1.0;
            case sawDown:  return 1.0 - 2.0 * ph;
            case square:   return ph < 0.5 ? 1.0 : -1.0;
            case random:   return randomHeld;
            default:       return std::sin (twoPi * ph);
        }
    }

    double getPhase (int i) const noexcept   { return phase[(size_t) std::clamp (i, 0, 1)]; }

private:
    double nextRandom() noexcept
    {
        rng = rng * 1664525u + 1013904223u;
        return (double) (rng >> 8) / (double) (1u << 24) * 2.0 - 1.0;
    }

    double sr = 48000.0;
    std::array<Lfo, 2> lfo {};
    std::array<double, 2> phase {}, held {};
    std::array<float, 4> macro {};
    std::array<float, kNumSources> values {};
    double env = 0.0, envAtt = 0.99, envRel = 0.999, envGain = 1.0;
    std::uint32_t rng = 0x1234567u;
};

} // namespace cf
