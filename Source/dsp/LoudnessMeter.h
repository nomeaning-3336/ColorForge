#pragma once

#include "Biquad.h"
#include "TruePeak.h"

#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

namespace cf {

/** Stereo loudness meter after ITU-R BS.1770 / EBU R128:
    momentary (400 ms), short-term (3 s), gated integrated loudness,
    true peak (max hold), sample peaks, and stereo correlation.
    process() runs on the audio thread; the atomics are read by the editor. */
class LoudnessMeter
{
public:
    static constexpr float kFloor = -70.0f;

    std::atomic<float> momentary { kFloor }, shortTerm { kFloor }, integrated { kFloor };
    std::atomic<float> truePeakMaxDb { -100.0f }, peakL { -100.0f }, peakR { -100.0f };
    std::atomic<float> correlation { 0.0f };
    std::atomic<bool>  resetRequested { false };

    void prepare (double sampleRate)
    {
        sr = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        designKWeighting();
        blockLen = std::max (1, (int) std::lround (0.1 * sr));
        corrCoef = std::exp (-1.0 / (0.3 * sr));
        reset();
    }

    void reset()
    {
        for (auto& ch : kw)
            for (auto& f : ch)
                f.clear();
        for (auto& t : tp)
            t.reset();
        blocks.fill (0.0);
        numBlocks = 0;
        blockPos = 0;
        blockAcc = 0.0;
        blockWrite = 0;
        histCount.assign (kHistBins, 0);
        histEnergy.assign (kHistBins, 0.0);
        tpMax = 0.0;
        sLR = sLL = sRR = 0.0;
        pkL = pkR = 0.0;
        momentary.store (kFloor);
        shortTerm.store (kFloor);
        integrated.store (kFloor);
        truePeakMaxDb.store (-100.0f);
    }

    void process (const float* l, const float* r, int num) noexcept
    {
        if (resetRequested.exchange (false))
            reset();

        double blockPkL = 0.0, blockPkR = 0.0;

        for (int n = 0; n < num; ++n)
        {
            const double xl = l[n], xr = r[n];

            tpMax = std::max (tpMax, std::max (tp[0].push (xl), tp[1].push (xr)));
            blockPkL = std::max (blockPkL, std::abs (xl));
            blockPkR = std::max (blockPkR, std::abs (xr));

            sLR = corrCoef * sLR + (1.0 - corrCoef) * xl * xr;
            sLL = corrCoef * sLL + (1.0 - corrCoef) * xl * xl;
            sRR = corrCoef * sRR + (1.0 - corrCoef) * xr * xr;

            const double kl = kw[0][1].process (kw[0][0].process (xl));
            const double kr = kw[1][1].process (kw[1][0].process (xr));
            blockAcc += kl * kl + kr * kr;

            if (++blockPos >= blockLen)
                finishBlock();
        }

        for (auto& ch : kw)
            for (auto& f : ch)
                f.sanitise();

        // sample peak meters with a ~1.5 s fall
        const double fall = std::pow (10.0, -20.0 / 20.0 * (double) num / sr);
        pkL = std::max (blockPkL, pkL * fall);
        pkR = std::max (blockPkR, pkR * fall);
        peakL.store (toDb (pkL), std::memory_order_relaxed);
        peakR.store (toDb (pkR), std::memory_order_relaxed);
        truePeakMaxDb.store (toDb (tpMax), std::memory_order_relaxed);

        const double denom = std::sqrt (sLL * sRR);
        correlation.store (denom > 1.0e-10 ? (float) std::clamp (sLR / denom, -1.0, 1.0) : 0.0f,
                           std::memory_order_relaxed);
    }

    static float lufsFromEnergy (double e) noexcept
    {
        return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : kFloor - 10.0f;
    }

private:
    static constexpr int kHistBins = 1600;               // 0.05 LU bins from -70 to +10 LUFS
    static constexpr double kHistLo = -70.0, kHistStep = 0.05;

    static float toDb (double g) noexcept
    {
        return g > 1.0e-5 ? (float) (20.0 * std::log10 (g)) : -100.0f;
    }

    void finishBlock() noexcept
    {
        blocks[(size_t) blockWrite] = blockAcc / (double) blockLen;
        blockWrite = (blockWrite + 1) % (int) blocks.size();
        numBlocks = std::min (numBlocks + 1, (int) blocks.size());
        blockAcc = 0.0;
        blockPos = 0;

        auto meanOf = [this] (int count)
        {
            double s = 0.0;
            for (int i = 1; i <= count; ++i)
                s += blocks[(size_t) ((blockWrite - i + (int) blocks.size()) % (int) blocks.size())];
            return s / (double) count;
        };

        const int nm = std::min (numBlocks, 4);
        const double mE = meanOf (nm) * (double) nm / 4.0;    // treat missing history as silence
        const float m = lufsFromEnergy (mE);
        momentary.store (std::max (kFloor, m), std::memory_order_relaxed);

        const int ns = std::min (numBlocks, 30);
        shortTerm.store (std::max (kFloor, lufsFromEnergy (meanOf (ns) * (double) ns / 30.0)), std::memory_order_relaxed);

        // gating block = the 400 ms momentary window, every 100 ms (75% overlap)
        if (numBlocks >= 4 && m > (float) kHistLo)
        {
            const int bin = std::clamp ((int) ((m - kHistLo) / kHistStep), 0, kHistBins - 1);
            ++histCount[(size_t) bin];
            histEnergy[(size_t) bin] += mE;
        }

        integrated.store (computeIntegrated(), std::memory_order_relaxed);
    }

    float computeIntegrated() const noexcept
    {
        long long n = 0;
        double e = 0.0;
        for (int b = 0; b < kHistBins; ++b)
        {
            n += histCount[(size_t) b];
            e += histEnergy[(size_t) b];
        }
        if (n == 0)
            return kFloor;

        const double relGate = lufsFromEnergy (e / (double) n) - 10.0;
        const int firstBin = std::clamp ((int) std::ceil ((relGate - kHistLo) / kHistStep), 0, kHistBins);

        n = 0;
        e = 0.0;
        for (int b = firstBin; b < kHistBins; ++b)
        {
            n += histCount[(size_t) b];
            e += histEnergy[(size_t) b];
        }
        return n > 0 ? std::max (kFloor, lufsFromEnergy (e / (double) n)) : kFloor;
    }

    void designKWeighting()
    {
        constexpr double pi = 3.14159265358979323846;

        // stage 1: high shelf (+4 dB above ~1.7 kHz)
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            BiquadCoefs c;
            c.b0 = (Vh + Vb * K / Q + K * K) / a0;
            c.b1 = 2.0 * (K * K - Vh) / a0;
            c.b2 = (Vh - Vb * K / Q + K * K) / a0;
            c.a1 = 2.0 * (K * K - 1.0) / a0;
            c.a2 = (1.0 - K / Q + K * K) / a0;
            kw[0][0].c = kw[1][0].c = c;
        }
        // stage 2: RLB high-pass (~38 Hz)
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            BiquadCoefs c;
            c.b0 = 1.0; c.b1 = -2.0; c.b2 = 1.0;
            c.a1 = 2.0 * (K * K - 1.0) / a0;
            c.a2 = (1.0 - K / Q + K * K) / a0;
            kw[0][1].c = kw[1][1].c = c;
        }
    }

    double sr = 48000.0, corrCoef = 0.9999;
    Biquad kw[2][2];
    TruePeak tp[2];

    std::array<double, 30> blocks {};
    int numBlocks = 0, blockPos = 0, blockLen = 4800, blockWrite = 0;
    double blockAcc = 0.0;

    std::vector<long long> histCount;
    std::vector<double> histEnergy;

    double tpMax = 0.0, sLR = 0.0, sLL = 0.0, sRR = 0.0, pkL = 0.0, pkR = 0.0;
};

} // namespace cf
