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

void testMidSideEq()
{
    // A side-only boost must leave a mono (mid-only) signal untouched, and vice versa.
    for (int mode = 1; mode <= 2; ++mode)
    {
        cf::ParametricEQ eq;
        eq.prepare (kSr, 128);
        cf::ParametricEQ::BandParams bell;
        bell.on = true; bell.freq = 1000.0f; bell.gainDb = 9.0f; bell.q = 1.0f; bell.mode = mode;
        eq.setBand (3, bell);

        auto l = sine (1000.0, 0.1, 48000);
        auto r = l;
        if (mode == 1)
            for (auto& v : r) v = -v;          // pure side signal: a mid band must not touch it
        const auto refL = l;
        inChunks (l, r, [&] (float* a, float* b, int n) { eq.process (a, b, n); });

        double maxErr = 0.0;
        for (size_t i = 0; i < l.size(); ++i)
            maxErr = std::max (maxErr, (double) std::abs (l[i] - refL[i]));
        check (maxErr < 1.0e-5, mode == 1 ? "EQ mid band leaves a pure side signal alone" : "EQ side band leaves a mono signal alone",
               maxErr, "max abs error");
    }

    // ...and a mid boost on a mono signal does boost it.
    cf::ParametricEQ eq;
    eq.prepare (kSr, 128);
    cf::ParametricEQ::BandParams bell;
    bell.on = true; bell.freq = 1000.0f; bell.gainDb = 6.0f; bell.q = 1.0f; bell.mode = 1;
    eq.setBand (3, bell);
    auto l = sine (1000.0, 0.1, 48000), r = l;
    const double before = rmsDb (l, 24000);
    inChunks (l, r, [&] (float* a, float* b, int n) { eq.process (a, b, n); });
    check (std::abs (rmsDb (l, 24000) - before - 6.0) < 0.1, "EQ mid band boosts a mono signal", rmsDb (l, 24000) - before, "dB (want 6)");
}

void testDynamicEq()
{
    // A dynamic bell stays flat while the band is quiet and reaches its full gain when it is loud.
    auto run = [] (double amp)
    {
        cf::ParametricEQ eq;
        eq.prepare (kSr, 128);
        cf::ParametricEQ::BandParams bell;
        bell.on = true; bell.freq = 1000.0f; bell.gainDb = 6.0f; bell.q = 1.0f;
        bell.dynamic = true; bell.dynThresholdDb = -30.0f;
        eq.setBand (3, bell);
        auto l = sine (1000.0, amp, 96000), r = l;
        const double before = rmsDb (l, 72000);
        inChunks (l, r, [&] (float* a, float* b, int n) { eq.process (a, b, n); });
        return std::make_pair (rmsDb (l, 72000) - before, (double) eq.getAppliedGainDb (3));
    };
    const auto quiet = run (0.002);   // about -57 dB, far below the threshold
    const auto loud = run (0.5);      // about -9 dB, well above threshold + 12
    check (std::abs (quiet.first) < 0.3, "Dynamic EQ band idle below threshold", quiet.first, "dB change (want 0)");
    check (std::abs (loud.first - 6.0) < 0.3, "Dynamic EQ band full gain when loud", loud.first, "dB change (want 6)");
    check (std::abs (loud.second - 6.0) < 0.3, "Dynamic EQ reports its applied gain", loud.second, "dB (want 6)");
}

void testOttBands()
{
    auto run = [] (double freq, int solo, float xLow, float xHigh)
    {
        cf::OTT ott;
        ott.prepare (kSr);
        ott.setParams (0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);   // depth 0: crossovers only
        ott.setBands (xLow, xHigh, solo);
        auto l = sine (freq, 0.1, 48000), r = l;
        const double before = rmsDb (l, 24000);
        inChunks (l, r, [&] (float* a, float* b, int n) { ott.process (a, b, n); });
        return rmsDb (l, 24000) - before;
    };
    const double flat = run (1000.0, -1, 200.0f, 5000.0f);
    check (std::abs (flat) < 0.5, "OTT bands sum flat with moved crossovers", flat, "dB change (want 0)");
    const double lowKept = run (50.0, 0, 200.0f, 5000.0f);
    const double highCut = run (10000.0, 0, 200.0f, 5000.0f);
    check (std::abs (lowKept) < 1.0 && highCut < -30.0, "OTT solo low keeps lows, drops highs", highCut, "dB at 10 kHz");
    const double midSolo = run (1000.0, 1, 200.0f, 5000.0f);
    check (std::abs (midSolo) < 1.0, "OTT solo mid passes the mid band", midSolo, "dB at 1 kHz");
}

void testMatchedBypass()
{
    // With +6 dB of input gain, the level-matched bypass plays the dry input raised to the same level.
    ColorForgeAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (kSr, 512);
    proc.prepareToPlay (kSr, 512);
    auto setParam = [&proc] (const char* id, float plain)
    {
        auto* prm = proc.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    };
    setParam ("inGain", 6.0f);
    setParam ("matchBypass", 1.0f);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    auto in = sine (440.0, 0.05, 48000 * 4);
    std::vector<float> out;
    for (size_t pos = 0; pos + 512 <= in.size(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, in.data() + pos, 512);
        proc.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
    }
    const int lat = proc.getLatencySamples();
    const size_t from = out.size() - 48000;
    double maxErr = 0.0, peak = 0.0;
    const double g = juce::Decibels::decibelsToGain ((double) proc.getBypassMatchDb());
    for (size_t i = from; i < out.size(); ++i)
    {
        maxErr = std::max (maxErr, (double) std::abs (out[i] - g * in[i - (size_t) lat]));
        peak = std::max (peak, (double) std::abs (out[i]));
    }
    check (std::abs (proc.getBypassMatchDb() - 6.0f) < 0.3f, "Bypass match gain follows the processing", proc.getBypassMatchDb(), "dB (want 6)");
    check (maxErr < 1.0e-3, "Bypass output is the delayed dry input", maxErr, "max abs error");
}

void testModulation()
{
    // LFO shapes and the envelope follower
    check (std::abs (cf::ModEngine::shapeValue (cf::ModEngine::triangle, 0.5, 0.0) - 1.0) < 1.0e-9
               && std::abs (cf::ModEngine::shapeValue (cf::ModEngine::sawUp, 0.0, 0.0) + 1.0) < 1.0e-9,
           "LFO shapes (triangle peak, saw start)", cf::ModEngine::shapeValue (cf::ModEngine::triangle, 0.5, 0.0), "(want 1)");

    {
        cf::ModEngine m;
        m.prepare (kSr);
        cf::ModEngine::Lfo lp;
        lp.rateHz = 2.0f;
        m.setLfo (0, lp);
        m.setEnvelope (5.0f, 100.0f, 0.0f);
        std::vector<float> loud (128, 0.5f), quiet (128, 0.0f);
        for (int i = 0; i < 375; ++i)   // 1 s
            m.process (loud.data(), loud.data(), 128, 120.0, false, 0.0);
        const float envLoud = m.value (3);
        for (int i = 0; i < 375; ++i)
            m.process (quiet.data(), quiet.data(), 128, 120.0, false, 0.0);
        check (std::abs (envLoud - (float) ((20.0 * std::log10 (0.5) + 60.0) / 60.0)) < 0.02f && m.value (3) < 0.05f,
               "Envelope follows the level and falls back", envLoud, "(want 0.90 at -6 dBFS)");
        check (std::abs (m.getPhase (0)) < 0.01 || std::abs (m.getPhase (0) - 1.0) < 0.01,
               "Free LFO: 2 Hz is back at phase 0 after 2 s", m.getPhase (0), "phase");

        // synced to the host: phase follows the song position
        lp.sync = true;
        lp.beats = 4.0;
        m.setLfo (0, lp);
        m.process (quiet.data(), quiet.data(), 128, 128.0, true, 6.0);
        check (std::abs (m.getPhase (0) - 0.5) < 1.0e-6, "Synced LFO locks to the bar (beat 6 of a 1-bar cycle)", m.getPhase (0), "phase (want 0.5)");
    }

    // a macro routed to the input gain moves the level, without moving the knob
    ColorForgeAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (kSr, 512);
    proc.prepareToPlay (kSr, 512);
    auto setParam = [&proc] (const juce::String& id, float plain)
    {
        auto* prm = proc.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    };
    auto runLevel = [&proc] ()
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        auto in = sine (440.0, 0.05, 48000);
        std::vector<float> out;
        for (size_t pos = 0; pos + 512 <= in.size(); pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, in.data() + pos, 512);
            proc.processBlock (buf, midi);
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512);
        }
        return rmsDb (out, 24000);
    };
    const double base = runLevel();
    setParam ("mod1Src", 4.0f);   // macro 1
    setParam ("mod1Dst", (float) ColorForgeAudioProcessor::modTargetIndex ("inGain"));
    setParam ("mod1Amt", 25.0f);  // inGain spans 48 dB, so 25 % of it = +12 dB at full macro
    setParam ("macro1", 50.0f);
    const double moved = runLevel();
    check (std::abs (moved - base - 6.0) < 0.3, "Macro -> input gain: half macro, +6 dB", moved - base, "dB (want 6)");
    check (proc.apvts.getRawParameterValue ("inGain")->load() == 0.0f, "Modulation leaves the knob value alone",
           proc.apvts.getRawParameterValue ("inGain")->load(), "dB");
    check (std::abs (proc.getModOffset (ColorForgeAudioProcessor::modTargetIndex ("inGain")) - 0.125f) < 1.0e-4f,
           "Mod offset is reported to the editor", proc.getModOffset (ColorForgeAudioProcessor::modTargetIndex ("inGain")), "(want 0.125)");

    int missing = 0;
    for (int t = 1; t < cf::kNumModTargets; ++t)
        if (proc.apvts.getParameter (cf::kModTargets[t].id) == nullptr)
            ++missing;
    check (missing == 0, "Every mod target is a real parameter", missing, "missing");
}

void testPresetsAndChain()
{
    ColorForgeAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (kSr, 512);
    proc.prepareToPlay (kSr, 512);
    auto raw = [&proc] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };

    proc.applyFactoryPreset (5);   // DRUM PUNCH
    check (raw ("cpOn") > 0.5f && std::abs (raw ("cpRatio") - 4.0f) < 0.01f, "Factory preset sets its values",
           raw ("cpRatio"), "comp ratio (want 4)");
    check (proc.getPresetName() == "DRUM PUNCH", "Factory preset name is shown", proc.getCurrentProgram(), "program index");

    proc.applyFactoryPreset (0);
    check (raw ("cpOn") < 0.5f && std::abs (raw ("cpRatio") - 2.0f) < 0.01f, "INIT puts every value back to default",
           raw ("cpRatio"), "comp ratio (want 2)");

    // save -> change -> load round trip
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cf_test_preset.cfpreset");
    proc.apvts.getParameter ("satDrive")->setValueNotifyingHost (proc.apvts.getParameter ("satDrive")->convertTo0to1 (17.0f));
    proc.savePreset (file);
    proc.applyFactoryPreset (0);
    proc.loadPreset (file);
    check (std::abs (raw ("satDrive") - 17.0f) < 0.05f, "User preset save + load round trip", raw ("satDrive"), "sat drive (want 17)");
    file.deleteFile();

    // A/B: B starts as a copy, then holds its own settings
    proc.applyFactoryPreset (0);
    proc.switchToSlot (1);
    proc.apvts.getParameter ("cpOn")->setValueNotifyingHost (1.0f);
    proc.switchToSlot (0);
    const bool aOff = raw ("cpOn") < 0.5f;
    proc.switchToSlot (1);
    check (aOff && raw ("cpOn") > 0.5f, "A/B snapshots keep separate settings", raw ("cpOn"), "comp on in B");
    proc.switchToSlot (0);

    // chain order: saturating before vs after a big EQ boost sounds different
    auto render = [&proc] ()
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        double energy = 0.0;
        for (int blk = 0; blk < 40; ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float v = (float) (0.3 * std::sin (2.0 * kPi * 100.0 * (blk * 512 + i) / kSr));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            proc.processBlock (buf, midi);
            if (blk >= 20)
                for (int i = 0; i < 512; ++i)
                    energy += (double) buf.getSample (0, i) * buf.getSample (0, i);
        }
        return 10.0 * std::log10 (energy + 1.0e-30);
    };
    auto set = [&proc] (const char* id, float v) { proc.apvts.getParameter (id)->setValueNotifyingHost (proc.apvts.getParameter (id)->convertTo0to1 (v)); };
    proc.applyFactoryPreset (0);
    set ("satOn", 1.0f); set ("satMode", 2.0f); set ("satDrive", 12.0f);
    set ("eq2Gain", 18.0f); set ("eq2Freq", 100.0f); set ("limit", 0.0f);

    proc.setChainOrder ({ ColorForgeAudioProcessor::modEq, ColorForgeAudioProcessor::modComp,
                          ColorForgeAudioProcessor::modSat, ColorForgeAudioProcessor::modStereo });
    const double eqFirst = render();
    proc.setChainOrder ({ ColorForgeAudioProcessor::modSat, ColorForgeAudioProcessor::modComp,
                          ColorForgeAudioProcessor::modEq, ColorForgeAudioProcessor::modStereo });
    const double satFirst = render();
    check (std::abs (eqFirst - satFirst) > 3.0, "Chain order changes the sound (EQ>SAT vs SAT>EQ)", satFirst - eqFirst, "dB difference");
    check (proc.getChainOrder()[0] == ColorForgeAudioProcessor::modSat, "Chain order is read back", proc.getChainOrder()[0], "first module");

    // the order survives a save / restore of the plugin state
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    ColorForgeAudioProcessor other;
    other.setStateInformation (state.getData(), (int) state.getSize());
    check (other.getChainOrder()[0] == ColorForgeAudioProcessor::modSat, "Chain order is saved with the session",
           other.getChainOrder()[0], "first module after reload");
}

/** Renders each editor page to a PNG after feeding the processor some audio. */
int snapshot (const juce::File& dir)
{
    dir.createDirectory();

    for (int page = 0; page < 5; ++page)
    {
        ColorForgeAudioProcessor proc;
        auto setParam = [&proc] (const char* id, float plain)
        {
            auto* prm = proc.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
        };

        // a mix-ish setting so every meter and display has something to show
        setParam ("cpOn", 1.0f);          setParam ("cpThresh", -24.0f);   setParam ("cpRatio", 3.0f);
        setParam ("eq2Gain", 3.0f);       setParam ("eq4Gain", -4.0f);     setParam ("eq4Q", 2.0f);
        setParam ("eq6Gain", 2.5f);       setParam ("eq1On", 1.0f);        setParam ("satOn", 1.0f);
        setParam ("lmGain", 6.0f);
        if (page == 0)
            setParam ("colorOn", 1.0f);
        // a couple of mod routings so the MOD page and the knob pips have something to show
        setParam ("mod1Src", 1.0f);   setParam ("mod1Dst", (float) ColorForgeAudioProcessor::modTargetIndex ("eq4Gain"));  setParam ("mod1Amt", 30.0f);
        setParam ("mod2Src", 3.0f);   setParam ("mod2Dst", (float) ColorForgeAudioProcessor::modTargetIndex ("satDrive")); setParam ("mod2Amt", -40.0f);
        setParam ("mod3Src", 4.0f);   setParam ("mod3Dst", (float) ColorForgeAudioProcessor::modTargetIndex ("imgWidth")); setParam ("mod3Amt", 50.0f);
        setParam ("macro1", 60.0f);   setParam ("lfo2Shape", 5.0f);   setParam ("lfo2Sync", 1.0f);
        proc.apvts.state.setProperty ("uiPage", page, nullptr);

        proc.setRateAndBufferSizeDetails (kSr, 512);
        proc.prepareToPlay (kSr, 512);

        std::mt19937 rng (7);
        std::normal_distribution<float> nd (0.0f, 1.0f);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        double ph = 0.0;
        float pinkL = 0.0f, pinkR = 0.0f;
        for (int blk = 0; blk < (int) (kSr * 8 / 512); ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                pinkL = 0.985f * pinkL + 0.05f * nd (rng);
                pinkR = 0.985f * pinkR + 0.05f * nd (rng);
                const float kick = (float) (0.5 * std::sin (ph) * std::exp (-std::fmod (blk * 512 + i, 24000) / 3000.0));
                ph += 2.0 * kPi * 55.0 / kSr;
                buf.setSample (0, i, 0.3f * pinkL + kick + 0.05f * (float) std::sin (ph * 8.0));
                buf.setSample (1, i, 0.3f * pinkR + kick + 0.05f * (float) std::sin (ph * 8.0 + 0.5));
            }
            proc.processBlock (buf, midi);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto file = dir.getChildFile ("page" + juce::String (page + 1) + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
        proc.editorBeingDeleted (ed.get());
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File (juce::String (argv[2])));

    testEq();
    testLimiter();
    testLoudness();
    testCompressor();
    testSaturator();
    testProcessor();
    testMidSideEq();
    testPresetsAndChain();
    testDynamicEq();
    testOttBands();
    testMatchedBypass();
    testModulation();

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
