#pragma once

#include <cmath>
#include <complex>

namespace cf {

/** Normalised biquad coefficients (a0 == 1). */
struct BiquadCoefs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** Magnitude response at the given frequency. */
    double magnitude (double freq, double sampleRate) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs (num / den);
    }
};

/** Transposed direct form II biquad, one channel. */
struct Biquad
{
    BiquadCoefs c;
    double z1 = 0.0, z2 = 0.0;

    void clear() noexcept { z1 = z2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    /** Kills denormals / blow-ups after a parameter jump. */
    void sanitise() noexcept
    {
        if (! std::isfinite (z1) || ! std::isfinite (z2)) clear();
        if (std::abs (z1) < 1.0e-20) z1 = 0.0;
        if (std::abs (z2) < 1.0e-20) z2 = 0.0;
    }
};

//==============================================================================
// RBJ cookbook designs.
namespace biquad {

inline double clampFreq (double f, double sr) noexcept
{
    return std::fmin (std::fmax (f, 5.0), sr * 0.49);
}

inline BiquadCoefs make (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    BiquadCoefs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
    c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

inline BiquadCoefs peak (double f, double q, double gainDb, double sr) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979323846 * clampFreq (f, sr) / sr;
    const double alpha = std::sin (w) / (2.0 * std::fmax (q, 0.05));
    const double cw = std::cos (w);
    return make (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
                 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
}

inline BiquadCoefs lowShelf (double f, double q, double gainDb, double sr) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979323846 * clampFreq (f, sr) / sr;
    const double cw = std::cos (w);
    const double alpha = std::sin (w) / (2.0 * std::fmax (q, 0.05));
    const double s = 2.0 * std::sqrt (A) * alpha;
    return make (A * ((A + 1.0) - (A - 1.0) * cw + s),
                 2.0 * A * ((A - 1.0) - (A + 1.0) * cw),
                 A * ((A + 1.0) - (A - 1.0) * cw - s),
                 (A + 1.0) + (A - 1.0) * cw + s,
                 -2.0 * ((A - 1.0) + (A + 1.0) * cw),
                 (A + 1.0) + (A - 1.0) * cw - s);
}

inline BiquadCoefs highShelf (double f, double q, double gainDb, double sr) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w = 2.0 * 3.14159265358979323846 * clampFreq (f, sr) / sr;
    const double cw = std::cos (w);
    const double alpha = std::sin (w) / (2.0 * std::fmax (q, 0.05));
    const double s = 2.0 * std::sqrt (A) * alpha;
    return make (A * ((A + 1.0) + (A - 1.0) * cw + s),
                 -2.0 * A * ((A - 1.0) + (A + 1.0) * cw),
                 A * ((A + 1.0) + (A - 1.0) * cw - s),
                 (A + 1.0) - (A - 1.0) * cw + s,
                 2.0 * ((A - 1.0) - (A + 1.0) * cw),
                 (A + 1.0) - (A - 1.0) * cw - s);
}

inline BiquadCoefs highPass (double f, double q, double sr) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * clampFreq (f, sr) / sr;
    const double cw = std::cos (w);
    const double alpha = std::sin (w) / (2.0 * std::fmax (q, 0.05));
    return make ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5,
                 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
}

/** Band-pass with 0 dB peak gain (detector use). */
inline BiquadCoefs bandPass (double f, double q, double sr) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * clampFreq (f, sr) / sr;
    const double alpha = std::sin (w) / (2.0 * std::fmax (q, 0.05));
    return make (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * std::cos (w), 1.0 - alpha);
}

inline BiquadCoefs lowPass (double f, double q, double sr) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * clampFreq (f, sr) / sr;
    const double cw = std::cos (w);
    const double alpha = std::sin (w) / (2.0 * std::fmax (q, 0.05));
    return make ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5,
                 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
}

} // namespace biquad
} // namespace cf
