#pragma once

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

namespace cf {

class SpectralPitchMap {
public:
    SpectralPitchMap() { setNoteMask (0x0FFF); }

    void prepare (double sampleRate)
    {
        if (! (sampleRate > 0.0) || ! std::isfinite (sampleRate)) sampleRate = 48000.0;
        fs = sampleRate;
        N = (fs <= 50000.0) ? 4096 : (fs <= 100000.0 ? 8192 : 16384);
        hop = N / 4;
        half = N / 2;
        latency = N - 1;

        window.assign ((size_t) N, 0.0f);
        for (int n = 0; n < N; ++n)
            window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (kTwoPi * (double) n / (double) N));

        cosT.assign ((size_t) half, 0.0);
        sinT.assign ((size_t) half, 0.0);
        for (int i = 0; i < half; ++i)
        {
            cosT[(size_t) i] = std::cos (kTwoPi * (double) i / (double) N);
            sinT[(size_t) i] = -std::sin (kTwoPi * (double) i / (double) N);
        }
        bitrev.assign ((size_t) N, 0);
        int bits = 0;
        while ((1 << bits) < N) ++bits;
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            bitrev[(size_t) i] = r;
        }

        re.assign ((size_t) N, 0.0);
        im.assign ((size_t) N, 0.0);
        sumS.assign ((size_t) half + 1, 0.0);
        sumP.assign ((size_t) half + 1, 0.0);
        cMax.assign ((size_t) half + 1, 0.0);
        cFreq.assign ((size_t) half + 1, 0.0);

        for (auto& c : ch)
        {
            c.inBuf.assign ((size_t) N, 0.0f);
            c.outAccum.assign ((size_t) N, 0.0f);
            c.outBuf.assign ((size_t) hop, 0.0f);
            c.lastPhase.assign ((size_t) half + 1, 0.0);
            c.sumPhase.assign ((size_t) half + 1, 0.0);
            c.heldMag.assign ((size_t) half + 1, 0.0);
            c.synFreq.assign ((size_t) half + 1, 0.0);
            c.dry.assign ((size_t) latency, 0.0f);
        }

        kMin = (int) std::ceil (20.0 * (double) N / fs);
        if (kMin < 1) kMin = 1;
        mixStep = (float) (1.0 / (0.02 * fs));
        prepared = true;
        reset();
    }

    void reset()
    {
        for (auto& c : ch) resetChannel (c, true);
        firstBlock = true;
        mixCur = mixTgt;
    }

    int getLatencySamples() const { return prepared ? latency : 0; }

    void setNoteMask (std::uint16_t mask)
    {
        int m = (int) (mask & 0x0FFF);
        if (m == 0) m = 0x0FFF;
        for (int pc = 0; pc < 12; ++pc)
        {
            int dn = 0, up = 0;
            while (! (m & (1 << (((pc - dn) % 12 + 12) % 12)))) ++dn;
            while (! (m & (1 << ((pc + up) % 12)))) ++up;
            downDist[pc] = dn;
            upDist[pc] = up;
            nearestOffset[pc] = (up < dn) ? up : -dn;
        }
    }

    void setParams (float amount, float purity, float shiftSemitones, float smear, float mix)
    {
        if (std::isfinite (amount))         pAmount = clampf (amount, 0.0f, 1.0f);
        if (std::isfinite (purity))         pPurity = clampf (purity, 0.0f, 1.0f);
        if (std::isfinite (shiftSemitones)) pShift  = clampf (shiftSemitones, -24.0f, 24.0f);
        if (std::isfinite (smear))          pSmear  = clampf (smear, 0.0f, 1.0f);
        if (std::isfinite (mix))            mixTgt  = clampf (mix, 0.0f, 1.0f);
    }

    void process (float* left, float* right, int numSamples)
    {
        if (! prepared || numSamples <= 0 || left == nullptr) return;
        if (firstBlock) { mixCur = mixTgt; firstBlock = false; }
        float* bufs[2] = { left, right };
        const int numCh = (right != nullptr && right != left) ? 2 : 1;

        for (int i = 0; i < numSamples; ++i)
        {
            if (mixCur < mixTgt)      mixCur = std::min (mixTgt, mixCur + mixStep);
            else if (mixCur > mixTgt) mixCur = std::max (mixTgt, mixCur - mixStep);
            const float m = mixCur;

            for (int c = 0; c < numCh; ++c)
            {
                Channel& s = ch[c];
                float x = bufs[c][i];
                if (! std::isfinite (x)) x = 0.0f;

                // dry delay line of exactly 'latency' samples
                const float d = s.dry[(size_t) s.dryPos];
                s.dry[(size_t) s.dryPos] = x;
                if (++s.dryPos >= latency) s.dryPos = 0;

                // wet path
                s.inBuf[(size_t) s.fill] = x;
                if (++s.fill >= N)
                {
                    processFrame (s);
                    for (int n = 0; n < hop; ++n) s.outBuf[(size_t) n] = s.outAccum[(size_t) n];
                    std::copy (s.outAccum.begin() + hop, s.outAccum.end(), s.outAccum.begin());
                    std::fill (s.outAccum.end() - hop, s.outAccum.end(), 0.0f);
                    std::copy (s.inBuf.begin() + hop, s.inBuf.end(), s.inBuf.begin());
                    s.fill = N - hop;
                    s.outRead = 0;
                }
                float w = s.outBuf[(size_t) s.outRead];
                if (s.outRead < hop - 1) ++s.outRead;
                if (! std::isfinite (w) || std::fabs (w) > 1.0e6f) { resetChannel (s, false); w = 0.0f; }

                if (std::fabs (w) < 1.0e-20f) w = 0.0f;
                float y = d * (1.0f - m) + w * m;
                if (! std::isfinite (y)) y = d;
                bufs[c][i] = y;
            }
        }
    }

private:
    struct Channel
    {
        std::vector<float> inBuf, outAccum, outBuf, dry;
        std::vector<double> lastPhase, sumPhase, heldMag, synFreq;
        int fill = 0, outRead = 0, dryPos = 0;
    };

    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kTwoPi = 6.28318530717958647692;

    static float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static double wrapPi (double x) { return x - kTwoPi * std::floor ((x + kPi) / kTwoPi); }
    static double sinc (double x)
    {
        if (std::fabs (x) < 1.0e-9) return 1.0;
        return std::sin (kPi * x) / (kPi * x);
    }
    // normalised Hann kernel: OLA gain loss of a sinusoid synthesised d bins off-centre
    static double hannKernel (double d) { return sinc (d) + 0.5 * (sinc (1.0 + d) + sinc (1.0 - d)); }

    void resetChannel (Channel& s, bool includeDry)
    {
        std::fill (s.inBuf.begin(), s.inBuf.end(), 0.0f);
        std::fill (s.outAccum.begin(), s.outAccum.end(), 0.0f);
        std::fill (s.outBuf.begin(), s.outBuf.end(), 0.0f);
        std::fill (s.lastPhase.begin(), s.lastPhase.end(), 0.0);
        std::fill (s.sumPhase.begin(), s.sumPhase.end(), 0.0);
        std::fill (s.heldMag.begin(), s.heldMag.end(), 0.0);
        std::fill (s.synFreq.begin(), s.synFreq.end(), 0.0);
        s.fill = N - hop;
        s.outRead = 0;
        if (includeDry)
        {
            std::fill (s.dry.begin(), s.dry.end(), 0.0f);
            s.dryPos = 0;
        }
    }

    void fft (bool inverse)
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = bitrev[(size_t) i];
            if (j > i) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
        }
        const double sgn = inverse ? -1.0 : 1.0;
        for (int len = 2; len <= N; len <<= 1)
        {
            const int h = len >> 1;
            const int step = N / len;
            for (int i = 0; i < N; i += len)
            {
                for (int j = 0; j < h; ++j)
                {
                    const double wr = cosT[(size_t) (j * step)];
                    const double wi = sgn * sinT[(size_t) (j * step)];
                    const size_t a = (size_t) (i + j), b = (size_t) (i + j + h);
                    const double tr = re[b] * wr - im[b] * wi;
                    const double ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
            }
        }
    }

    void processFrame (Channel& s)
    {
        for (int n = 0; n < N; ++n)
        {
            re[(size_t) n] = (double) s.inBuf[(size_t) n] * (double) window[(size_t) n];
            im[(size_t) n] = 0.0;
        }
        fft (false);

        std::fill (sumS.begin(), sumS.end(), 0.0);
        std::fill (sumP.begin(), sumP.end(), 0.0);
        std::fill (cMax.begin(), cMax.end(), 0.0);

        const double amount = (double) pAmount, purity = (double) pPurity;
        const double shiftRatio = std::exp2 ((double) pShift / 12.0);
        const double expct = kTwoPi * (double) hop / (double) N;
        const double binHz = fs / (double) N;
        const double magFloor = 1.0e-9 * (double) N;

        for (int k = 1; k < half; ++k)
        {
            const double r = re[(size_t) k], i = im[(size_t) k];
            const double mag = std::sqrt (r * r + i * i);
            const double ph = std::atan2 (i, r);
            const double dphi = wrapPi (ph - s.lastPhase[(size_t) k] - (double) k * expct);
            s.lastPhase[(size_t) k] = ph;
            if (k < kMin || ! (mag > magFloor)) continue;

            const double trueFreq = ((double) k + dphi / expct) * binHz;
            const double f1 = trueFreq * shiftRatio;
            if (! (f1 > 1.0)) continue;
            const double midi = 69.0 + 12.0 * std::log2 (f1 / 440.0);
            const double fl = std::floor (midi);
            const int n0 = (int) fl;
            const int pc0 = ((n0 % 12) + 12) % 12;
            const int pc1 = (pc0 + 1) % 12;
            const int down = n0 - downDist[pc0];
            const int up = n0 + 1 + upDist[pc1];
            const double dDown = midi - (double) down, dUp = (double) up - midi;
            const int target = (dUp < dDown) ? up : down;
            const double dev = (dUp < dDown) ? dUp : dDown;

            const double fOut = f1 * std::exp2 (amount * ((double) target - midi) / 12.0);
            const double z = dev / 0.35;
            const double g = 1.0 - purity * (1.0 - std::exp (-0.5 * z * z));
            const int kOut = (int) std::floor (fOut / binHz + 0.5);
            if (kOut < kMin || kOut >= half) continue;
            const double mg = mag * g;
            if (! (mg > 0.0)) continue;
            sumS[(size_t) kOut] += mg;
            sumP[(size_t) kOut] += mg * mg;
            if (mg > cMax[(size_t) kOut]) { cMax[(size_t) kOut] = mg; cFreq[(size_t) kOut] = fOut; }
        }

        const double sm = (double) pSmear;
        const double decay = sm <= 0.0 ? 0.0 : 0.995 * std::pow (sm, 0.25);
        const double phaseScale = kTwoPi * (double) hop / fs;

        re[0] = im[0] = 0.0;
        re[(size_t) half] = im[(size_t) half] = 0.0;
        for (int k = 1; k < half; ++k)
        {
            const size_t ku = (size_t) k;
            double nm = 0.0;
            if (sumS[ku] > 0.0) nm = std::min (sumS[ku], 1.8 * std::sqrt (sumP[ku]));
            const double held = s.heldMag[ku] * decay;
            double mag;
            if (nm > 0.0 && nm >= held) { mag = nm; s.synFreq[ku] = cFreq[ku]; }
            else mag = held;
            if (mag < 1.0e-12 || ! std::isfinite (mag)) mag = 0.0;
            s.heldMag[ku] = mag;

            if (mag > 0.0)
            {
                const double f = s.synFreq[ku];
                s.sumPhase[ku] = wrapPi (s.sumPhase[ku] + phaseScale * f);
                double d = f / binHz - (double) k;
                d = d < -0.5 ? -0.5 : (d > 0.5 ? 0.5 : d);
                const double out = mag / hannKernel (d) * ((k & 1) ? -1.0 : 1.0);
                const double cr = out * std::cos (s.sumPhase[ku]);
                const double ci = out * std::sin (s.sumPhase[ku]);
                re[ku] = cr;  im[ku] = ci;
                re[(size_t) (N - k)] = cr;  im[(size_t) (N - k)] = -ci;
            }
            else
            {
                re[ku] = im[ku] = 0.0;
                re[(size_t) (N - k)] = im[(size_t) (N - k)] = 0.0;
            }
        }

        fft (true);
        const double scale = 0.5 / (double) N;   // 1/N for IFFT, 1/2 for Hann OLA (sum of w over 75% overlap = 2)
        bool bad = false;
        for (int n = 0; n < N; ++n)
        {
            double v = re[(size_t) n] * scale * (double) window[(size_t) n];
            if (! std::isfinite (v)) { bad = true; break; }
            float o = s.outAccum[(size_t) n] + (float) v;
            if (std::fabs (o) < 1.0e-20f) o = 0.0f;
            s.outAccum[(size_t) n] = o;
        }
        if (bad) resetChannel (s, false);
    }

    double fs = 48000.0;
    int N = 4096, hop = 1024, half = 2048, latency = 4095, kMin = 2;
    bool prepared = false, firstBlock = true;

    std::vector<float> window;
    std::vector<double> cosT, sinT, re, im, sumS, sumP, cMax, cFreq;
    std::vector<int> bitrev;
    Channel ch[2];

    int nearestOffset[12] = {};
    int downDist[12] = {};
    int upDist[12] = {};

    float pAmount = 1.0f, pPurity = 0.0f, pShift = 0.0f, pSmear = 0.0f;
    float mixTgt = 1.0f, mixCur = 1.0f, mixStep = 0.001f;
};

} // namespace cf
