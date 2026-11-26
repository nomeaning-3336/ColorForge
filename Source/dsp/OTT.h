#pragma once

#include <cmath>
#include <algorithm>

namespace cf {

class OTT {
public:
    OTT() { prepare (48000.0); }

    void prepare (double sampleRate)
    {
        fs = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        designFilters();
        paramCoef = std::exp (-1.0 / (0.020 * fs));   // ~20 ms param smoothing
        gainCoef  = std::exp (-1.0 / (0.0015 * fs));  // ~1.5 ms applied-gain smoothing
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 2; ++i)
            {
                lp1[ch][i].clear(); hp1[ch][i].clear();
                lp2[ch][i].clear(); hp2[ch][i].clear();
            }
            ap2[ch].clear();
        }
        for (int b = 0; b < 3; ++b)
        {
            env[b] = 0.0;
            gLin[b] = -1.0; // marks "snap on first sample"
        }
        snapParams = true;
        coefCounter = 0;
    }

    void setParams (float depth, float timeScale, float upward, float downward,
                    float lowGainDb, float midGainDb, float highGainDb)
    {
        tgt[0] = clampf (depth, 0.0, 1.0);
        tgt[1] = clampf (timeScale, 0.1, 10.0);
        tgt[2] = clampf (upward, 0.0, 1.0);
        tgt[3] = clampf (downward, 0.0, 1.0);
        tgt[4] = clampf (lowGainDb, -12.0, 12.0);
        tgt[5] = clampf (midGainDb, -12.0, 12.0);
        tgt[6] = clampf (highGainDb, -12.0, 12.0);
    }

    /** Crossover frequencies (low/mid, mid/high) and solo: -1 = none, 0..2 = only that band. */
    void setBands (float lowMidHz, float midHighHz, int soloBand)
    {
        const double f1 = clampf (lowMidHz, 30.0, 1000.0);
        const double f2 = std::max (f1 * 2.0, clampf (midHighHz, 500.0, 0.45 * fs));
        if (f1 != xf1 || f2 != xf2)
        {
            xf1 = f1;
            xf2 = f2;
            designFilters();
        }
        solo = soloBand < 0 || soloBand > 2 ? -1 : soloBand;
    }

    void process (float* left, float* right, int numSamples)
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        if (snapParams)
        {
            for (int i = 0; i < kNumParams; ++i) cur[i] = tgt[i];
            snapParams = false;
            updateEnvCoefs();
        }

        bool bad = false;

        for (int n = 0; n < numSamples; ++n)
        {
            for (int i = 0; i < kNumParams; ++i)
                cur[i] = tgt[i] + paramCoef * (cur[i] - tgt[i]);

            if (coefCounter <= 0)
            {
                updateEnvCoefs();
                coefCounter = 16;
            }
            --coefCounter;

            double in[2] = { static_cast<double> (left[n]), static_cast<double> (right[n]) };
            double band[3][2];

            for (int ch = 0; ch < 2; ++ch)
            {
                double x = in[ch];
                if (! std::isfinite (x)) x = 0.0;

                double lo = lp1[ch][1].process (lp1[ch][0].process (x));
                double rest = hp1[ch][1].process (hp1[ch][0].process (x));
                double mid = lp2[ch][1].process (lp2[ch][0].process (rest));
                double hi  = hp2[ch][1].process (hp2[ch][0].process (rest));
                lo = ap2[ch].process (lo); // phase-compensate low band for upper crossover

                band[0][ch] = lo;
                band[1][ch] = mid;
                band[2][ch] = hi;
            }

            const double depth = cur[0], up = cur[2], down = cur[3];
            double outL = 0.0, outR = 0.0;

            for (int b = 0; b < 3; ++b)
            {
                const double pk = std::max (std::fabs (band[b][0]), std::fabs (band[b][1]));
                double e = env[b];
                const double c = (pk > e) ? atkC[b] : relC[b];
                e = pk + c * (e - pk);
                if (e < 1.0e-15) e = 0.0;
                env[b] = e;

                const double xDb = 20.0 * std::log10 (e + 1.0e-30);

                double downDb = 0.0;
                if (xDb > kUpperThr[b])
                    downDb = -(xDb - kUpperThr[b]) * (1.0 - 1.0 / kDownRatio[b]);

                double upDb = 0.0;
                if (xDb < kLowerThr[b])
                {
                    upDb = (kLowerThr[b] - xDb) * (1.0 - 1.0 / kUpRatio);
                    if (upDb > 24.0) upDb = 24.0;
                    double fade = (xDb + 75.0) / 15.0; // 1 at -60 dB, 0 at -75 dB
                    fade = fade < 0.0 ? 0.0 : (fade > 1.0 ? 1.0 : fade);
                    upDb *= fade;
                }

                const double gDb = depth * (down * downDb + up * upDb + kMakeup[b]) + cur[4 + b];
                const double target = std::pow (10.0, gDb * 0.05);

                double g = gLin[b];
                if (g < 0.0) g = target;
                else g = target + gainCoef * (g - target);
                gLin[b] = g;

                const double sg = (solo < 0 || solo == b) ? g : 0.0;
                outL += band[b][0] * sg;
                outR += band[b][1] * sg;
            }

            if (! std::isfinite (outL) || ! std::isfinite (outR))
            {
                bad = true;
                outL = outR = 0.0;
            }

            left[n]  = static_cast<float> (outL);
            right[n] = static_cast<float> (outR);
        }

        // denormal flush / sanity check of filter states
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 2; ++i)
            {
                bad |= lp1[ch][i].sanitize(); bad |= hp1[ch][i].sanitize();
                bad |= lp2[ch][i].sanitize(); bad |= hp2[ch][i].sanitize();
            }
            bad |= ap2[ch].sanitize();
        }
        for (int b = 0; b < 3; ++b)
            if (! std::isfinite (env[b]) || ! std::isfinite (gLin[b])) bad = true;
        for (int i = 0; i < kNumParams; ++i)
            if (! std::isfinite (cur[i])) bad = true;

        if (bad)
        {
            reset();
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr int kNumParams = 7;

    // band order: 0 = low, 1 = mid, 2 = high
    static constexpr double kUpperThr[3]  = { -33.8, -30.2, -35.5 };
    static constexpr double kLowerThr[3]  = { -40.8, -41.8, -40.8 };
    static constexpr double kDownRatio[3] = { 66.7, 66.7, 1.0e9 };
    static constexpr double kUpRatio      = 4.17;
    static constexpr double kAttackMs[3]  = { 47.8, 22.4, 13.5 };
    static constexpr double kReleaseMs[3] = { 282.0, 282.0, 132.0 };
    static constexpr double kMakeup[3]    = { 10.3, 5.7, 10.3 };
    static constexpr double kF1 = 88.3, kF2 = 2500.0;

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void clear() { z1 = z2 = 0.0; }

        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        // returns true if state went non-finite
        bool sanitize()
        {
            if (! std::isfinite (z1) || ! std::isfinite (z2)) { clear(); return true; }
            if (std::fabs (z1) < 1.0e-20) z1 = 0.0;
            if (std::fabs (z2) < 1.0e-20) z2 = 0.0;
            return false;
        }

        enum Type { LP, HP, AP };

        void design (Type t, double f, double sr)
        {
            const double q = 0.70710678118654752440;
            const double w0 = 2.0 * kPi * f / sr;
            const double cw = std::cos (w0), sw = std::sin (w0);
            const double alpha = sw / (2.0 * q);
            const double a0 = 1.0 + alpha;
            double nb0, nb1, nb2;
            if (t == LP)      { nb0 = (1.0 - cw) * 0.5; nb1 = 1.0 - cw;    nb2 = nb0; }
            else if (t == HP) { nb0 = (1.0 + cw) * 0.5; nb1 = -(1.0 + cw); nb2 = nb0; }
            else              { nb0 = 1.0 - alpha;      nb1 = -2.0 * cw;   nb2 = 1.0 + alpha; }
            b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0;
            a1 = (-2.0 * cw) / a0;
            a2 = (1.0 - alpha) / a0;
        }
    };

    void designFilters()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 2; ++i)
            {
                lp1[ch][i].design (Biquad::LP, xf1, fs);
                hp1[ch][i].design (Biquad::HP, xf1, fs);
                lp2[ch][i].design (Biquad::LP, xf2, fs);
                hp2[ch][i].design (Biquad::HP, xf2, fs);
            }
            ap2[ch].design (Biquad::AP, xf2, fs);
        }
    }

    void updateEnvCoefs()
    {
        const double ts = cur[1];
        for (int b = 0; b < 3; ++b)
        {
            const double a = std::max (0.05, kAttackMs[b] * ts) * 0.001 * fs;
            const double r = std::max (0.05, kReleaseMs[b] * ts) * 0.001 * fs;
            atkC[b] = std::exp (-1.0 / a);
            relC[b] = std::exp (-1.0 / r);
        }
    }

    static double clampf (float v, double lo, double hi)
    {
        double d = static_cast<double> (v);
        if (! std::isfinite (d)) d = lo;
        return d < lo ? lo : (d > hi ? hi : d);
    }

    double fs = 48000.0;
    double xf1 = kF1, xf2 = kF2;
    int solo = -1;
    double paramCoef = 0.0, gainCoef = 0.0;

    Biquad lp1[2][2], hp1[2][2], lp2[2][2], hp2[2][2], ap2[2];

    double env[3] = { 0.0, 0.0, 0.0 };
    double gLin[3] = { -1.0, -1.0, -1.0 };
    double atkC[3] = { 0.0, 0.0, 0.0 };
    double relC[3] = { 0.0, 0.0, 0.0 };

    double tgt[kNumParams] = { 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0 };
    double cur[kNumParams] = { 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0 };
    bool snapParams = true;
    int coefCounter = 0;
};

} // namespace cf
