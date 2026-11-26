// Offline checks for the mix & master DSP. Build the ColorForgeTests target and run it;
// it prints one line per check and exits non-zero if any check fails.

#include "PluginProcessor.h"

#include <cstdio>
#include <random>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const char* name, double value, const char* detail)
{
    std::printf ("[%s] %-44s %10.3f  %s\n", ok ? "PASS" : "FAIL", name, value, detail);
    if (! ok)
        ++failures;
}

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine (double freq, double amp, int num)
{
    std::vector<float> v ((size_t) num);
    for (int i = 0; i < num; ++i)
        v[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * freq * i / kSr));
    return v;
}

double rmsDb (const std::vector<float>& v, int from)
{
    double s = 0.0;
    for (size_t i = (size_t) from; i < v.size(); ++i)
        s += (double) v[i] * v[i];
    return 10.0 * std::log10 (s / (double) (v.size() - (size_t) from) + 1.0e-30);
}

template <typename Fn>
void inChunks (std::vector<float>& l, std::vector<float>& r, Fn&& fn)
{
    for (size_t pos = 0; pos < l.size(); pos += 128)
        fn (l.data() + pos, r.data() + pos, (int) std::min<size_t> (128, l.size() - pos));
}

//==============================================================================
void testEq()
{
    cf::ParametricEQ eq;
    eq.prepare (kSr, 128);

    cf::ParametricEQ::BandParams bell;
    bell.on = true; bell.freq = 1000.0f; bell.gainDb = 6.0f; bell.q = 1.0f;
    eq.setBand (3, bell);

    auto l = sine (1000.0, 0.1, 48000), r = l;
    const double before = rmsDb (l, 24000);
    inChunks (l, r, [&] (float* a, float* b, int n) { eq.process (a, b, n); });
    check (std::abs (rmsDb (l, 24000) - before - 6.0) < 0.1, "EQ bell +6 dB at its centre", rmsDb (l, 24000) - before, "dB (want 6)");

    const auto d = cf::ParametricEQ::design (cf::ParametricEQ::Type::LowCut, 100.0, 0.0, 0.707, 1, kSr);
    const double at100 = 20.0 * std::log10 (d.magnitude (100.0, kSr));
    const double at25  = 20.0 * std::log10 (d.magnitude (25.0, kSr));
    check (std::abs (at100 + 3.0) < 0.3, "EQ 24 dB/oct low cut: level at cutoff", at100, "dB (want -3, Butterworth)");
    check (std::abs (at25 - at100 + 48.0) < 3.0, "EQ 24 dB/oct low cut: two octaves down", at25 - at100, "dB (want about -48)");
}

void testLimiter()
{
    cf::Limiter lim;
    lim.prepare (kSr);
    cf::Limiter::Params p;
    p.on = true; p.ceilingDb = -1.0f; p.gainDb = 12.0f; p.releaseMs = 50.0f;
    lim.setParams (p);

    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 0.3f);
    std::vector<float> l (96000), r (96000);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = nd (rng); r[i] = nd (rng); }

    inChunks (l, r, [&] (float* a, float* b, int n) { lim.process (a, b, n); });

    float peak = 0.0f;
    for (size_t i = 0; i < l.size(); ++i) peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
    const double peakDb = 20.0 * std::log10 (peak);
    check (peakDb <= -0.999, "Limiter sample peak under the ceiling", peakDb, "dBFS (ceiling -1)");

    cf::TruePeak tp[2];
    double tpMax = 0.0;
    for (size_t i = 0; i < l.size(); ++i)
        tpMax = std::max (tpMax, std::max (tp[0].push (l[i]), tp[1].push (r[i])));
    check (20.0 * std::log10 (tpMax) <= -0.7, "Limiter true peak near the ceiling", 20.0 * std::log10 (tpMax), "dBTP (want <= -0.7)");
    check (lim.getLatencySamples() > 0 && lim.getLatencySamples() < 200, "Limiter latency", lim.getLatencySamples(), "samples");
}

void testLoudness()
{
    cf::LoudnessMeter m;
    m.prepare (kSr);
    auto l = sine (1000.0, std::pow (10.0, -20.0 / 20.0), 48000 * 10), r = l;
    inChunks (l, r, [&] (float* a, float* b, int n) { m.process (a, b, n); });

    check (std::abs (m.integrated.load() + 20.0f) < 0.2f, "LUFS integrated, 1 kHz sine -20 dBFS (L+R)", m.integrated.load(), "LUFS (want -20)");
    check (std::abs (m.momentary.load() + 20.0f) < 0.2f, "LUFS momentary", m.momentary.load(), "LUFS (want -20)");
    check (std::abs (m.shortTerm.load() + 20.0f) < 0.2f, "LUFS short-term", m.shortTerm.load(), "LUFS (want -20)");
    check (std::abs (m.truePeakMaxDb.load() + 20.0f) < 0.2f, "True peak of the sine", m.truePeakMaxDb.load(), "dBTP (want -20)");
    check (m.correlation.load() > 0.99f, "Correlation of identical channels", m.correlation.load(), "(want 1)");
}

void testCompressor()
{
    cf::Compressor c;
    c.prepare (kSr);
    cf::Compressor::Params p;
    p.on = true; p.thresholdDb = -24.0f; p.ratio = 4.0f; p.kneeDb = 0.0f; p.autoMakeup = false;
    p.attackMs = 5.0f; p.releaseMs = 100.0f;
    c.setParams (p);

    auto l = sine (200.0, 0.5, 48000), r = l;
    const double before = rmsDb (l, 24000);
    inChunks (l, r, [&] (float* a, float* b, int n) { c.process (a, b, n); });
    const double gr = before - rmsDb (l, 24000);
    check (gr > 8.0 && gr < 14.0, "Compressor gain reduction on a hot sine", gr, "dB (expect about 11)");
    check (std::abs (c.grDb.load() - gr) < 2.5, "Compressor GR meter agrees", c.grDb.load(), "dB");
}

void testSaturator()
{
    cf::Saturator s;
    s.prepare (kSr);
    for (int mode = 0; mode < 3; ++mode)
    {
        cf::Saturator::Params p;
        p.on = true; p.mode = mode; p.driveDb = 18.0f;
        s.setParams (p);
        s.reset();
        auto l = sine (110.0, 0.25, 48000), r = l;
        const double before = rmsDb (l, 24000);
        inChunks (l, r, [&] (float* a, float* b, int n) { s.process (a, b, n); });
        bool finite = true;
        for (float v : l) finite = finite && std::isfinite (v);
        const char* names[] = { "Saturator tape: level kept (-12 dBFS sine)", "Saturator tube: level kept (-12 dBFS sine)",
                                "Saturator clip: level kept (-12 dBFS sine)" };
        check (finite && std::abs (rmsDb (l, 24000) - before) < 3.0, names[mode], rmsDb (l, 24000) - before, "dB change");
    }
}

void testProcessor()
{
    ColorForgeAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (kSr, 512);
    proc.prepareToPlay (kSr, 512);

    const int base = proc.getLatencySamples();
    check (base > 0 && base < 200, "Default latency (limiter lookahead only)", base, "samples");

    // Default settings should pass a quiet signal through untouched (after the latency).
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    auto in = sine (440.0, 0.1, 48000);
    std::vector<float> out;
    for (size_t pos = 0; pos + 512 <= in.size(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, in.data() + pos, 512);
        proc.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
    }
    double maxErr = 0.0;
    for (size_t i = 4800; i < out.size(); ++i)
        maxErr = std::max (maxErr, (double) std::abs (out[i] - in[i - (size_t) base]));
    check (maxErr < 1.0e-4, "Default chain is transparent (null test)", maxErr, "max abs error");

    // Turning on the colour chain with the pitch map adds its latency; turning it off removes it again.
    auto setParam = [&proc] (const char* id, float plain)
    {
        auto* prm = proc.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    };
    setParam ("colorOn", 1.0f);
    proc.processBlock (buf, midi);
    const int withMap = proc.getLatencySamples();
    check (withMap > base + 1000, "Latency with the pitch map in use", withMap, "samples");

    setParam ("pmOn", 0.0f);
    proc.processBlock (buf, midi);
    check (proc.getLatencySamples() == base, "Latency with the pitch map off", proc.getLatencySamples(), "samples");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    testEq();
    testLimiter();
    testLoudness();
    testCompressor();
    testSaturator();
    testProcessor();

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
