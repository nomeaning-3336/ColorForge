#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace cf {

/** 4x oversampled peak estimator (ITU-R BS.1770 style true peak).
    push() takes one input sample and returns the peak magnitude around the
    sample that entered kDelay samples earlier, including the three
    interpolated points between it and the next sample. */
class TruePeak
{
public:
    static constexpr int kTaps = 12;
    static constexpr int kDelay = 6;

    TruePeak()
    {
        constexpr double pi = 3.14159265358979323846;
        for (int p = 1; p < 4; ++p)
        {
            const double t = (double) p / 4.0;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const int j = k - (kTaps / 2 - 1);            // -5 .. 6
                const double u = t - (double) j;
                const double sinc = std::abs (u) < 1.0e-12 ? 1.0 : std::sin (pi * u) / (pi * u);
                const double w = 0.5 + 0.5 * std::cos (pi * u / 6.5);   // Hann over +-6.5
                coef[(size_t) p - 1][(size_t) k] = sinc * w;
                sum += sinc * w;
            }
            for (auto& c : coef[(size_t) p - 1])
                c /= sum;   // unity DC gain
        }
    }

    void reset() noexcept   { hist.fill (0.0); pos = 0; }

    double push (double x) noexcept
    {
        hist[(size_t) pos] = x;
        pos = (pos + 1) % kTaps;

        // hist[pos] is now the oldest sample (x[n-11]); centre m = n-6 is index kTaps/2-1 from the oldest
        double peak = std::abs (at (kTaps / 2 - 1));
        for (int p = 0; p < 3; ++p)
        {
            double y = 0.0;
            for (int k = 0; k < kTaps; ++k)
                y += at (k) * coef[(size_t) p][(size_t) k];
            peak = std::max (peak, std::abs (y));
        }
        return peak;
    }

private:
    inline double at (int k) const noexcept  { return hist[(size_t) ((pos + k) % kTaps)]; }

    std::array<std::array<double, kTaps>, 3> coef {};
    std::array<double, kTaps> hist {};
    int pos = 0;
};

} // namespace cf
