#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Processing order of the three colour stages. 0 = resonator, 1 = shifter, 2 = pitch map.
    // Must match the "routing" choice list in Params.h.
    constexpr int kRoutes[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };

    constexpr int kResonatorBaseNote = 48;   // C3

    inline float softClip (float x) noexcept
    {
        constexpr float knee = 0.75f;
        const float a = std::abs (x);

        if (a <= knee)
            return x;

        const float y = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
        return x < 0.0f ? -y : y;
    }

    inline std::uint16_t rotateMask (std::uint16_t mask, int semis) noexcept
    {
        semis = ((semis % 12) + 12) % 12;
        const unsigned v = (unsigned) mask & 0x0FFFu;
        return (std::uint16_t) (((v << semis) | (v >> (12 - semis))) & 0x0FFFu);
    }

    inline float midiToHz (float note) noexcept
    {
        return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    }

    inline float val (const std::atomic<float>* v) noexcept   { return v->load (std::memory_order_relaxed); }
    inline float pct (const std::atomic<float>* v) noexcept   { return v->load (std::memory_order_relaxed) * 0.01f; }
    inline bool  isOn (const std::atomic<float>* v) noexcept  { return v->load (std::memory_order_relaxed) > 0.5f; }
}

//==============================================================================
void ColorForgeAudioProcessor::StereoDelay::prepare (int delaySamples)
{
    size = juce::jmax (0, delaySamples);
    pos = 0;

    for (auto& b : buf)
        b.assign ((size_t) juce::jmax (1, size), 0.0f);
}

void ColorForgeAudioProcessor::StereoDelay::process (float* l, float* r, int num) noexcept
{
    if (size <= 0)
        return;

    for (int i = 0; i < num; ++i)
    {
        const auto idx = (size_t) pos;
        const float outL = buf[0][idx], outR = buf[1][idx];
        buf[0][idx] = l[i];
        buf[1][idx] = r[i];
        l[i] = outL;
        r[i] = outR;

        if (++pos >= size)
            pos = 0;
    }
}

//==============================================================================
ColorForgeAudioProcessor::ColorForgeAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ColorForgeState", cf::createParameterLayout())
{
    auto ref = [this] (const char* pid)
    {
        auto* v = apvts.getRawParameterValue (pid);
        jassert (v != nullptr);
        return v;
    };

    p.root = ref ("root");            p.scale = ref ("scale");          p.chord = ref ("chord");
    p.midiFollow = ref ("midiFollow"); p.routing = ref ("routing");      p.drive = ref ("drive");
    p.subOn = ref ("subOn");          p.subFreq = ref ("subFreq");      p.width = ref ("width");
    p.mix = ref ("mix");              p.outGain = ref ("outGain");      p.limit = ref ("limit");

    p.pmOn = ref ("pmOn");            p.pmAmount = ref ("pmAmount");    p.pmPurity = ref ("pmPurity");
    p.pmShift = ref ("pmShift");      p.pmSmear = ref ("pmSmear");      p.pmMix = ref ("pmMix");

    p.resOn = ref ("resOn");          p.resMode = ref ("resMode");      p.resDecay = ref ("resDecay");
    p.resDamp = ref ("resDamp");      p.resOct = ref ("resOct");        p.resStack = ref ("resStack");
    p.resDetune = ref ("resDetune");  p.resMix = ref ("resMix");

    p.fsOn = ref ("fsOn");            p.fsShift = ref ("fsShift");      p.fsSpread = ref ("fsSpread");
    p.fsFeedback = ref ("fsFeedback"); p.fsLfoRate = ref ("fsLfoRate");  p.fsLfoDepth = ref ("fsLfoDepth");
    p.fsMix = ref ("fsMix");

    p.chOn = ref ("chOn");            p.chRate = ref ("chRate");        p.chDepth = ref ("chDepth");
    p.chMix = ref ("chMix");

    p.rvOn = ref ("rvOn");            p.rvSize = ref ("rvSize");        p.rvDamp = ref ("rvDamp");
    p.rvMix = ref ("rvMix");          p.rvFreeze = ref ("rvFreeze");

    p.ottOn = ref ("ottOn");          p.ottDepth = ref ("ottDepth");    p.ottTime = ref ("ottTime");
    p.ottUp = ref ("ottUp");          p.ottDown = ref ("ottDown");      p.ottLow = ref ("ottLow");
    p.ottMid = ref ("ottMid");        p.ottHigh = ref ("ottHigh");
}

bool ColorForgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void ColorForgeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    sr = sampleRate;
    scopeSampleRate.store (sampleRate);

    resonator.prepare (sampleRate);
    shifter.prepare (sampleRate);
    pitchMap.prepare (sampleRate);
    ott.prepare (sampleRate);

    // The pitch map is the only latent stage and always stays in the path, so latency is constant.
    const int latency = pitchMap.getLatencySamples();
    setLatencySamples (latency);
    subDelay.prepare (latency);
    dryDelay.prepare (latency);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) kChunk, 2 };
    chorus.prepare (spec);
    chorus.reset();
    chorus.setCentreDelay (12.0f);
    chorus.setFeedback (0.0f);

    crossover.prepare (spec);
    crossover.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    crossover.reset();
    lastSubFreq = -1.0f;

    reverb.setSampleRate (sampleRate);
    reverb.reset();
    reverbWasOn = true;

    for (auto* s : { &driveSm, &widthSm, &mixSm, &gainSm, &subSm })
        s->reset (sampleRate, 0.03);

    driveSm.setCurrentAndTargetValue (pct (p.drive));
    widthSm.setCurrentAndTargetValue (pct (p.width));
    mixSm.setCurrentAndTargetValue (pct (p.mix));
    gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (val (p.outGain)));
    subSm.setCurrentAndTargetValue (isOn (p.subOn) ? 1.0f : 0.0f);

    lfoPhase = 0.0;
    held.reset();
}

//==============================================================================
void ColorForgeAudioProcessor::updateParameters()
{
    // ---- Note set: held MIDI notes if following, otherwise root + scale / chord.
    const int root     = juce::jlimit (0, 11, (int) val (p.root));
    const int scaleIdx = juce::jlimit (0, cf::kNumScales - 1, (int) val (p.scale));
    const int chordIdx = juce::jlimit (0, cf::kNumChords - 1, (int) val (p.chord));
    const int octave   = juce::roundToInt (val (p.resOct));
    const int stack    = juce::jlimit (1, 4, juce::roundToInt (val (p.resStack)));
    const float maxHz  = juce::jmin (12000.0f, (float) (0.45 * sr));

    std::uint16_t mask = 0;
    float freqs[cf::ResonatorBank::kMaxVoices];
    int numVoices = 0;

    auto addVoice = [&] (int midiNote)
    {
        const float hz = midiToHz ((float) midiNote);

        if (numVoices < cf::ResonatorBank::kMaxVoices && hz >= 20.0f && hz <= maxHz)
            freqs[numVoices++] = hz;
    };

    if (isOn (p.midiFollow) && held.any())
    {
        for (int o = 0; o < stack; ++o)
            for (int note = 0; note < 128; ++note)
                if (held.test ((size_t) note))
                {
                    mask = (std::uint16_t) (mask | (1u << (note % 12)));
                    addVoice (note + 12 * (octave + o));
                }
    }
    else
    {
        const auto& scale = cf::kScales[scaleIdx];
        const auto& chord = cf::kChords[chordIdx];

        std::uint16_t chordRel = 0;
        for (int i = 0; i < chord.num; ++i)
            chordRel = (std::uint16_t) (chordRel | (1u << (chord.semis[i] % 12)));

        // "Chord Tones" scale follows the chord; if the chord is "Scale Notes" too, fall back to chromatic.
        const std::uint16_t scaleRel = scale.mask != 0 ? scale.mask
                                                       : (chord.num > 0 ? chordRel : (std::uint16_t) 0x0FFF);
        mask = rotateMask (scaleRel, root);

        int semis[12];
        int numSemis = 0;

        if (chord.num > 0)
        {
            for (int i = 0; i < chord.num; ++i)
                semis[numSemis++] = chord.semis[i];
        }
        else if (scale.mask != 0)
        {
            for (int s = 0; s < 12; ++s)
                if ((scale.mask >> s) & 1u)
                    semis[numSemis++] = s;
        }
        else
        {
            semis[numSemis++] = 0;
        }

        const int base = kResonatorBaseNote + root + 12 * octave;

        for (int o = 0; o < stack; ++o)
            for (int i = 0; i < numSemis; ++i)
                addVoice (base + semis[i] + 12 * o);
    }

    activeNoteMask.store ((int) mask, std::memory_order_relaxed);
    resonator.setFrequencies (freqs, numVoices);
    pitchMap.setNoteMask (mask);

    // ---- Colour stages
    routing = juce::jlimit (0, 5, (int) val (p.routing));

    resonator.setParams (juce::jlimit (0, 2, (int) val (p.resMode)), val (p.resDecay), pct (p.resDamp),
                         val (p.resDetune), isOn (p.resOn) ? pct (p.resMix) : 0.0f);

    pitchMap.setParams (pct (p.pmAmount), pct (p.pmPurity), val (p.pmShift), pct (p.pmSmear),
                        isOn (p.pmOn) ? pct (p.pmMix) : 0.0f);

    fsShiftHz  = val (p.fsShift);
    fsSpreadHz = val (p.fsSpread);
    fsFeedback = pct (p.fsFeedback);
    fsMix      = isOn (p.fsOn) ? pct (p.fsMix) : 0.0f;
    lfoRateHz  = val (p.fsLfoRate);
    lfoDepthHz = val (p.fsLfoDepth);

    // ---- Space
    chorus.setRate (val (p.chRate));
    chorus.setDepth (pct (p.chDepth) * 0.5f);
    chorus.setMix (isOn (p.chOn) ? pct (p.chMix) : 0.0f);

    reverbOn = isOn (p.rvOn);
    {
        const float m = pct (p.rvMix);
        juce::Reverb::Parameters rp;
        rp.roomSize   = pct (p.rvSize);
        rp.damping    = pct (p.rvDamp);
        rp.wetLevel   = m / 3.0f;            // juce::Reverb scales wet by 3 and dry by 2
        rp.dryLevel   = (1.0f - m) * 0.5f;
        rp.width      = 1.0f;
        rp.freezeMode = isOn (p.rvFreeze) ? 1.0f : 0.0f;
        reverb.setParameters (rp);
    }

    // ---- Dynamics
    const bool ottOn = isOn (p.ottOn);
    ott.setParams (ottOn ? pct (p.ottDepth) : 0.0f, val (p.ottTime), pct (p.ottUp), pct (p.ottDown),
                   ottOn ? val (p.ottLow) : 0.0f, ottOn ? val (p.ottMid) : 0.0f, ottOn ? val (p.ottHigh) : 0.0f);

    // ---- In / out
    const float subFreq = val (p.subFreq);
    if (std::abs (subFreq - lastSubFreq) > 0.01f)
    {
        crossover.setCutoffFrequency (subFreq);
        lastSubFreq = subFreq;
    }

    driveSm.setTargetValue (pct (p.drive));
    widthSm.setTargetValue (pct (p.width));
    mixSm.setTargetValue (pct (p.mix));
    gainSm.setTargetValue (juce::Decibels::decibelsToGain (val (p.outGain)));
    subSm.setTargetValue (isOn (p.subOn) ? 1.0f : 0.0f);
    limitOn = isOn (p.limit);
}

//==============================================================================
void ColorForgeAudioProcessor::processChunk (float* l, float* r, int num)
{
    float* dryL = dryBuf[0].data();
    float* dryR = dryBuf[1].data();
    float* subL = subBuf[0].data();
    float* subR = subBuf[1].data();

    std::copy (l, l + num, dryL);
    std::copy (r, r + num, dryR);

    // ---- Sub split (the kept sub bypasses every effect) and input drive
    for (int i = 0; i < num; ++i)
    {
        const float keep = subSm.getNextValue();
        float lo = 0.0f, hi = 0.0f;

        crossover.processSample (0, l[i], lo, hi);
        subL[i] = lo * keep;
        float xl = hi + lo * (1.0f - keep);

        crossover.processSample (1, r[i], lo, hi);
        subR[i] = lo * keep;
        float xr = hi + lo * (1.0f - keep);

        const float d = driveSm.getNextValue();

        if (d > 1.0e-4f)
        {
            const float g = 1.0f + d * d * 15.0f;
            const float comp = (0.5f / std::tanh (0.5f * g)) / 1.0820f;   // roughly constant loudness for hot input
            const float k = juce::jmin (1.0f, d * 8.0f);
            xl += k * (std::tanh (xl * g) * comp - xl);
            xr += k * (std::tanh (xr * g) * comp - xr);
        }

        l[i] = xl;
        r[i] = xr;
    }

    // ---- Colour stages in the selected order
    lfoPhase += juce::MathConstants<double>::twoPi * (double) lfoRateHz * (double) num / sr;
    if (lfoPhase > juce::MathConstants<double>::twoPi)
        lfoPhase = std::fmod (lfoPhase, juce::MathConstants<double>::twoPi);

    shifter.setParams (fsShiftHz + lfoDepthHz * (float) std::sin (lfoPhase), fsSpreadHz, fsFeedback, fsMix);

    for (int stage : kRoutes[routing])
    {
        switch (stage)
        {
            case 0:  resonator.process (l, r, num); break;
            case 1:  shifter.process (l, r, num);   break;
            default: pitchMap.process (l, r, num);  break;
        }
    }

    // ---- Chorus, reverb, OTT
    {
        float* chans[2] = { l, r };
        juce::dsp::AudioBlock<float> block (chans, (size_t) 2, (size_t) num);
        juce::dsp::ProcessContextReplacing<float> context (block);
        chorus.process (context);
    }

    if (reverbOn)
        reverb.processStereo (l, r, num);
    else if (reverbWasOn)
        reverb.reset();

    reverbWasOn = reverbOn;

    ott.process (l, r, num);

    // ---- Width, sub re-add, global mix, output
    subDelay.process (subL, subR, num);
    dryDelay.process (dryL, dryR, num);

    int w = scopeWrite.load (std::memory_order_relaxed);

    for (int i = 0; i < num; ++i)
    {
        const float width = widthSm.getNextValue();
        const float mid = 0.5f * (l[i] + r[i]);
        const float side = 0.5f * (l[i] - r[i]) * width;
        const float wetL = mid + side + subL[i];
        const float wetR = mid - side + subR[i];

        const float m = mixSm.getNextValue();
        const float g = gainSm.getNextValue();
        float outL = (dryL[i] + (wetL - dryL[i]) * m) * g;
        float outR = (dryR[i] + (wetR - dryR[i]) * m) * g;

        if (limitOn)
        {
            outL = softClip (outL);
            outR = softClip (outR);
        }

        if (! std::isfinite (outL)) outL = 0.0f;
        if (! std::isfinite (outR)) outR = 0.0f;

        l[i] = outL;
        r[i] = outR;

        scope[(size_t) w] = 0.5f * (outL + outR);
        w = (w + 1) & (kScopeSize - 1);
    }

    scopeWrite.store (w, std::memory_order_relaxed);
}

void ColorForgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
            held.set ((size_t) msg.getNoteNumber());
        else if (msg.isNoteOff())
            held.reset ((size_t) msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            held.reset();
    }

    midi.clear();

    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    updateParameters();

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);

    for (int pos = 0; pos < numSamples; pos += kChunk)
        processChunk (l + pos, r + pos, juce::jmin (kChunk, numSamples - pos));
}

//==============================================================================
void ColorForgeAudioProcessor::getScopeSnapshot (float* dest, int numSamples) const noexcept
{
    numSamples = juce::jlimit (0, 8192, numSamples);
    const int w = scopeWrite.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
        dest[i] = scope[(size_t) ((w - numSamples + i) & (kScopeSize - 1))];
}

//==============================================================================
void ColorForgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ColorForgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ColorForgeAudioProcessor::createEditor()
{
    return new ColorForgeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ColorForgeAudioProcessor();
}
