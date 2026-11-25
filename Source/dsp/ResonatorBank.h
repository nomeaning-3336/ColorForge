#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace cf {

class ResonatorBank {
public:
    static constexpr int kMaxVoices = 24;
    enum Mode { CombPlus = 0, CombMinus = 1, Tone = 2 };

    void prepare (double sampleRate)
    {
        sr = (std::isfinite (sampleRate) && sampleRate > 1000.0) ? sampleRate : 48000.0;
        const int need = static_cast<int> (std::ceil (sr / 20.0)) + 8;
        int len = 1;
        while (len < need) len <<= 1;
        lineLen = len;
        mask = len - 1;
        buffer.assign (static_cast<std::size_t> (len) * static_cast<std::size_t> (kMaxVoices * 2), 0.0f);
        prepared = true;
        reset();
    }

    void reset()
    {
        if (! buffer.empty())
            std::fill (buffer.begin(), buffer.end(), 0.0f);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            Voice& vo = voices[v];
            vo.fade = vo.active ? 1.0f : 0.0f;
            vo.logF = vo.targetLogF;
            vo.snap = true;
            for (int c = 0; c < 2; ++c)
                clearLineState (lines[v * 2 + c]);
        }
        writePos = 0;
        for (int c = 0; c < 2; ++c) { dcX[c] = 0.0f; dcY[c] = 0.0f; }
        mixCur = mixTarget;
        modeGain = 1.0f;
        curMode = pendingMode;
        decayLogCur = std::log (decayTarget);
        dampCur = dampTarget;
        detuneCur = detuneTarget;
        voiceScaleCur = voiceScaleTarget();
    }

    void setFrequencies (const float* freqsHz, int numVoices)
    {
        if (freqsHz == nullptr || numVoices < 0) numVoices = 0;
        numVoices = std::min (numVoices, static_cast<int> (kMaxVoices));
        const double hi = 0.45 * sr;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            Voice& vo = voices[v];
            if (v < numVoices)
            {
                double f = static_cast<double> (freqsHz[v]);
                if (! std::isfinite (f)) f = 110.0;
                f = std::min (std::max (f, 20.0), hi);
                vo.targetLogF = std::log2 (f);
                if (! vo.active && vo.fade <= 0.0f)
                    vo.snap = true;
                vo.active = true;
            }
            else
            {
                vo.active = false;
            }
        }
        activeCount = numVoices;
    }

    void setParams (int mode, float decaySeconds, float damping, float stereoDetuneCents, float mix)
    {
        if (mode < 0 || mode > 2) mode = 0;
        double d = std::isfinite (decaySeconds) ? static_cast<double> (decaySeconds) : 1.0;
        decayTarget = std::min (std::max (d, 0.05), 10.0);
        double dp = std::isfinite (damping) ? static_cast<double> (damping) : 0.0;
        dampTarget = std::min (std::max (dp, 0.0), 1.0);
        double dt = std::isfinite (stereoDetuneCents) ? static_cast<double> (stereoDetuneCents) : 0.0;
        detuneTarget = std::min (std::max (dt, 0.0), 50.0);
        float m = std::isfinite (mix) ? mix : 0.0f;
        mixTarget = std::min (std::max (m, 0.0f), 1.0f);
        pendingMode = mode;
        if (! paramsInit)
        {
            paramsInit = true;
            curMode = mode;
            decayLogCur = std::log (decayTarget);
            dampCur = dampTarget;
            detuneCur = detuneTarget;
            mixCur = mixTarget;
        }
    }

    void process (float* left, float* right, int numSamples)
    {
        if (! prepared || left == nullptr || right == nullptr || numSamples <= 0)
            return;

        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min (kBlock, numSamples - done);
            processSub (left + done, right + done, n);
            done += n;
        }
    }

private:
    static constexpr int kBlock = 32;
    static constexpr double kPi = 3.14159265358979323846;

    struct Line
    {
        float lp = 0.0f, s1 = 0.0f, s2 = 0.0f;
        double D = 1.0, g = 0.0, inScale = 0.0;
        double a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double aLp = 1.0;
        bool fresh = true;
    };

    struct Voice
    {
        double logF = 6.78, targetLogF = 6.78;
        float fade = 0.0f;
        bool active = false;
        bool snap = true;
    };

    static void clearLineState (Line& l)
    {
        l.lp = 0.0f; l.s1 = 0.0f; l.s2 = 0.0f;
        l.fresh = true;
    }

    void clearLine (int idx)
    {
        clearLineState (lines[idx]);
        if (! buffer.empty())
        {
            float* b = &buffer[static_cast<std::size_t> (idx) * static_cast<std::size_t> (lineLen)];
            std::fill (b, b + lineLen, 0.0f);
        }
    }

    double voiceScaleTarget() const
    {
        return 1.0 / std::sqrt (static_cast<double> (std::max (activeCount, 1)));
    }

    static float flushDen (float x)
    {
        return (std::fabs (x) < 1.0e-25f) ? 0.0f : x;
    }

    void processSub (float* L, float* R, int n)
    {
        // ---- mode switch handling (duck happens per sample, switch when silent)
        if (pendingMode != curMode && modeGain <= 0.0f)
        {
            curMode = pendingMode;
            for (int v = 0; v < kMaxVoices; ++v)
                voices[v].snap = true;
        }

        // ---- control-rate smoothing
        const double nd = static_cast<double> (n);
        const double cP = 1.0 - std::exp (-nd / (0.02 * sr));
        const double cF = 1.0 - std::exp (-nd / (0.01 * sr));
        decayLogCur += cP * (std::log (decayTarget) - decayLogCur);
        dampCur += cP * (dampTarget - dampCur);
        detuneCur += cP * (detuneTarget - detuneCur);
        const double T60 = std::exp (decayLogCur);

        const double vsOld = voiceScaleCur;
        voiceScaleCur += cP * (voiceScaleTarget() - voiceScaleCur);
        const float vsStep = static_cast<float> ((voiceScaleCur - vsOld) / nd);

        // loop lowpass coefficient (sample-rate independent cutoff)
        const double fcBase = 20000.0 * std::pow (0.0125, dampCur);

        // ---- copy dry
        float dry[2][kBlock];
        float acc[2][kBlock];
        float fadeArr[kBlock];
        for (int i = 0; i < n; ++i)
        {
            dry[0][i] = std::isfinite (L[i]) ? L[i] : 0.0f;
            dry[1][i] = std::isfinite (R[i]) ? R[i] : 0.0f;
            acc[0][i] = 0.0f;
            acc[1][i] = 0.0f;
        }

        const float fadeStep = static_cast<float> (1.0 / (0.02 * sr));
        const double hi = 0.45 * sr;
        const int mode = curMode;

        for (int v = 0; v < kMaxVoices; ++v)
        {
            Voice& vo = voices[v];
            if (! vo.active && vo.fade <= 0.0f)
            {
                vo.fade = 0.0f;
                vo.snap = true;
                continue;
            }
            if (vo.snap)
            {
                vo.logF = vo.targetLogF;
                clearLine (v * 2);
                clearLine (v * 2 + 1);
                vo.snap = false;
            }
            vo.logF += cF * (vo.targetLogF - vo.logF);

            // fade envelope
            {
                float f = vo.fade;
                const float tgt = vo.active ? 1.0f : 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    if (f < tgt) f = std::min (tgt, f + fadeStep);
                    else if (f > tgt) f = std::max (tgt, f - fadeStep);
                    fadeArr[i] = f;
                }
                vo.fade = f;
            }

            for (int c = 0; c < 2; ++c)
            {
                const int idx = v * 2 + c;
                Line& ln = lines[idx];
                const double cents = (c == 0 ? 0.5 : -0.5) * detuneCur;
                double f = std::exp2 (vo.logF + cents / 1200.0);
                f = std::min (std::max (f, 20.0), hi);

                double Dn = 1.0, gn = 0.0, sn = 0.0;
                if (mode == Tone)
                {
                    const double Q = std::max (0.5, kPi * f * T60 / 6.91);
                    const double gg = std::tan (kPi * f / sr);
                    const double k = 1.0 / Q;
                    ln.a1 = 1.0 / (1.0 + gg * (gg + k));
                    ln.a2 = gg * ln.a1;
                    ln.a3 = gg * ln.a2;
                    sn = k * std::sqrt (Q * sr / (kPi * f));
                }
                else
                {
                    // loop lowpass: cutoff never below 2 * f so every voice keeps its own spectrum
                    const double fc = std::min (std::max (fcBase, 2.0 * f), 0.49 * sr);
                    const double aLp = 1.0 - std::exp (-2.0 * kPi * fc / sr);
                    const double bLp = 1.0 - aLp;
                    ln.aLp = aLp;
                    const double w = 2.0 * kPi * f / sr;
                    const double pd = (bLp > 1.0e-12)
                        ? std::atan2 (bLp * std::sin (w), 1.0 - bLp * std::cos (w)) / w
                        : 0.0;
                    const double P = (mode == CombPlus) ? sr / f : sr / (2.0 * f);
                    Dn = std::min (std::max (P - pd, 1.0), static_cast<double> (lineLen - 4));
                    gn = std::min (std::pow (10.0, -3.0 * P / (sr * T60)), 0.99999);
                    const double ga2 = gn * gn * aLp * aLp;
                    const double A = 1.0 + bLp * bLp - ga2;
                    const double B = 2.0 * bLp;
                    const double disc = (aLp * aLp * (1.0 - gn * gn)) * (A + B);
                    // power gain of the feedback (resonant) part only; the direct path is not output
                    // CombMinus: integrate over [w/2, pi] only, the region around DC is an anti-resonance
                    const double amb = std::max (aLp * aLp * (1.0 - gn * gn), 1.0e-30);
                    const double ratio = std::sqrt ((A + B) / amb);
                    const double frac = (mode == CombMinus)
                        ? std::max (1.0 - (2.0 / kPi) * std::atan (ratio * std::tan (0.25 * w)), 1.0e-6)
                        : 1.0;
                    const double pw = ga2 * frac / std::sqrt (std::max (disc, 1.0e-30));
                    sn = 1.0 / std::sqrt (std::max (pw, 1.0e-12));
                }

                if (ln.fresh)
                {
                    ln.D = Dn; ln.g = gn; ln.inScale = sn;
                    ln.fresh = false;
                }

                const float* x = dry[c];
                float* out = acc[c];
                float* buf = &buffer[static_cast<std::size_t> (idx) * static_cast<std::size_t> (lineLen)];

                if (mode == Tone)
                {
                    double s0 = ln.inScale;
                    const double ds = (sn - s0) / nd;
                    const float a1 = static_cast<float> (ln.a1);
                    const float a2 = static_cast<float> (ln.a2);
                    const float a3 = static_cast<float> (ln.a3);
                    float s1 = ln.s1, s2 = ln.s2;
                    for (int i = 0; i < n; ++i)
                    {
                        s0 += ds;
                        const float in = x[i] * static_cast<float> (s0);
                        const float v3 = in - s2;
                        const float v1 = a1 * s1 + a2 * v3;
                        const float v2 = s2 + a2 * s1 + a3 * v3;
                        s1 = 2.0f * v1 - s1;
                        s2 = 2.0f * v2 - s2;
                        out[i] += v1 * fadeArr[i];
                    }
                    ln.s1 = flushDen (s1);
                    ln.s2 = flushDen (s2);
                    ln.inScale = sn;
                    if (! std::isfinite (ln.s1) || ! std::isfinite (ln.s2))
                        clearLine (idx);
                }
                else
                {
                    double D = ln.D, g = ln.g, s0 = ln.inScale;
                    const double dD = (Dn - D) / nd;
                    const double dg = (gn - g) / nd;
                    const double ds = (sn - s0) / nd;
                    const float sign = (mode == CombPlus) ? 1.0f : -1.0f;
                    const float aLpF = static_cast<float> (ln.aLp);
                    float lp = ln.lp;
                    float last = 0.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        D += dD; g += dg; s0 += ds;
                        const int di = static_cast<int> (D);
                        const float fr = static_cast<float> (D - static_cast<double> (di));
                        const int wp = (writePos + i) & mask;
                        const int p0 = (wp - di) & mask;
                        const int p1 = (p0 - 1) & mask;
                        const float d = buf[p0] + fr * (buf[p1] - buf[p0]);
                        lp += aLpF * (d - lp);
                        const float fb = sign * static_cast<float> (g) * lp;
                        const float y = x[i] * static_cast<float> (s0) + fb + 1.0e-20f;
                        buf[wp] = y;
                        last = y;
                        out[i] += fb * fadeArr[i];
                    }
                    ln.lp = flushDen (lp);
                    ln.D = Dn; ln.g = gn; ln.inScale = sn;
                    if (! std::isfinite (ln.lp) || ! std::isfinite (last))
                        clearLine (idx);
                }
            }

            if (! vo.active && vo.fade <= 0.0f)
                vo.snap = true;
        }

        writePos = (writePos + n) & mask;

        // ---- output stage
        const float R_dc = static_cast<float> (std::exp (-2.0 * kPi * 10.0 / sr));
        const float mixC = static_cast<float> (1.0 - std::exp (-1.0 / (0.02 * sr)));
        const float duckStep = static_cast<float> (1.0 / (0.005 * sr));
        const bool ducking = (pendingMode != curMode);
        float vs = static_cast<float> (vsOld);
        bool bad = false;

        for (int i = 0; i < n; ++i)
        {
            if (ducking) modeGain = std::max (0.0f, modeGain - duckStep);
            else         modeGain = std::min (1.0f, modeGain + duckStep);

            mixCur += mixC * (mixTarget - mixCur);
            if (std::fabs (mixTarget - mixCur) < 1.0e-6f) mixCur = mixTarget;
            vs += vsStep;

            const float m = mixCur;
            const float gain = vs * modeGain;
            for (int c = 0; c < 2; ++c)
            {
                const float xin = acc[c][i] * gain;
                float y = xin - dcX[c] + R_dc * dcY[c];
                dcX[c] = xin;
                dcY[c] = y;
                float wet = 2.0f * std::tanh (0.5f * y);
                if (! std::isfinite (wet)) { wet = 0.0f; bad = true; }
                float* o = (c == 0) ? L : R;
                if (m > 0.0f)
                    o[i] = dry[c][i] * (1.0f - m) + wet * m;
                else
                    o[i] = dry[c][i];
            }
        }

        for (int c = 0; c < 2; ++c)
        {
            dcX[c] = flushDen (dcX[c]);
            dcY[c] = flushDen (dcY[c]);
            if (! std::isfinite (dcX[c]) || ! std::isfinite (dcY[c])) bad = true;
        }
        if (bad)
        {
            for (int c = 0; c < 2; ++c) { dcX[c] = 0.0f; dcY[c] = 0.0f; }
            for (int v = 0; v < kMaxVoices; ++v) voices[v].snap = true;
        }
    }

    double sr = 48000.0;
    bool prepared = false;
    bool paramsInit = false;
    int lineLen = 0;
    int mask = 0;
    int writePos = 0;
    std::vector<float> buffer;

    Line lines[kMaxVoices * 2];
    Voice voices[kMaxVoices];
    int activeCount = 0;

    int curMode = CombPlus;
    int pendingMode = CombPlus;
    float modeGain = 1.0f;

    double decayTarget = 1.0, decayLogCur = 0.0;
    double dampTarget = 0.3, dampCur = 0.3;
    double detuneTarget = 0.0, detuneCur = 0.0;
    float mixTarget = 0.0f, mixCur = 0.0f;
    double voiceScaleCur = 1.0;

    float dcX[2] = { 0.0f, 0.0f };
    float dcY[2] = { 0.0f, 0.0f };
};

} // namespace cf
