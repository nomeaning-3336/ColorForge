#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
    // Processing order of the three colour stages. 0 = resonator, 1 = shifter, 2 = pitch map.
    // Must match the "routing" choice list in Params.h.
    constexpr int kRoutes[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };

    constexpr int kResonatorBaseNote = 48;   // C3

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
void ColorForgeAudioProcessor::StereoDelay::prepare (int maxDelaySamples)
{
    for (auto& b : buf)
        b.assign ((size_t) juce::jmax (1, maxDelaySamples), 0.0f);

    setDelay (maxDelaySamples);
}

void ColorForgeAudioProcessor::StereoDelay::setDelay (int delaySamples) noexcept
{
    size = juce::jlimit (0, (int) buf[0].size(), delaySamples);
    pos = 0;

    for (auto& b : buf)
        std::fill (b.begin(), b.end(), 0.0f);
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
void ColorForgeAudioProcessor::LevelTracker::prepare (double sampleRate)
{
    // same K-weighting as the loudness meter (BS.1770), designed via the meter's helper filters
    cf::LoudnessMeter tmp;
    tmp.prepare (sampleRate);
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 2; ++s)
        {
            kw[ch][s].c = tmp.kWeightingStage (s);
            kw[ch][s].clear();
        }
    coef = std::exp (-1.0 / (1.0 * sampleRate));
    energy = 0.0;
}

void ColorForgeAudioProcessor::LevelTracker::process (const float* l, const float* r, int num) noexcept
{
    for (int i = 0; i < num; ++i)
    {
        const double a = kw[0][1].process (kw[0][0].process (l[i]));
        const double b = kw[1][1].process (kw[1][0].process (r[i]));
        energy = coef * energy + (1.0 - coef) * (a * a + b * b);
    }
    for (auto& ch : kw)
        for (auto& f : ch)
            f.sanitise();
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

    p.inGain = ref ("inGain");        p.colorOn = ref ("colorOn");      p.eqOn = ref ("eqOn");

    for (int b = 0; b < cf::kEqBands; ++b)
    {
        p.eqBandOn[b] = ref (cf::eqId (b, "On").toRawUTF8());
        p.eqFreq[b]   = ref (cf::eqId (b, "Freq").toRawUTF8());
        p.eqGain[b]   = ref (cf::eqId (b, "Gain").toRawUTF8());
        p.eqQ[b]      = ref (cf::eqId (b, "Q").toRawUTF8());
    }
    for (int b = 0; b < cf::kEqBands; ++b)
        p.eqMode[b] = ref (cf::eqId (b, "Mode").toRawUTF8());
    for (int b = 1; b < cf::kEqBands - 1; ++b)
    {
        p.eqDyn[b]    = ref (cf::eqId (b, "Dyn").toRawUTF8());
        p.eqDynThr[b] = ref (cf::eqId (b, "DynThr").toRawUTF8());
    }
    p.matchBypass = ref ("matchBypass");
    p.ottXLow = ref ("ottXLow");      p.ottXHigh = ref ("ottXHigh");    p.ottSolo = ref ("ottSolo");
    p.eqLoSlope = ref (cf::eqId (0, "Slope").toRawUTF8());
    p.eqHiSlope = ref (cf::eqId (cf::kEqBands - 1, "Slope").toRawUTF8());

    p.cpOn = ref ("cpOn");            p.cpThresh = ref ("cpThresh");    p.cpRatio = ref ("cpRatio");
    p.cpAttack = ref ("cpAttack");    p.cpRelease = ref ("cpRelease");  p.cpKnee = ref ("cpKnee");
    p.cpMakeup = ref ("cpMakeup");    p.cpAuto = ref ("cpAuto");        p.cpMix = ref ("cpMix");
    p.cpScHpf = ref ("cpScHpf");

    p.satOn = ref ("satOn");          p.satMode = ref ("satMode");      p.satDrive = ref ("satDrive");
    p.satTone = ref ("satTone");      p.satMix = ref ("satMix");        p.satOut = ref ("satOut");

    p.imgOn = ref ("imgOn");          p.imgWidth = ref ("imgWidth");    p.imgMonoOn = ref ("imgMonoOn");
    p.imgMonoFreq = ref ("imgMonoFreq"); p.imgBalance = ref ("imgBalance");

    p.lmMode = ref ("lmMode");        p.lmGain = ref ("lmGain");        p.lmCeiling = ref ("lmCeiling");
    p.lmRelease = ref ("lmRelease");

    apvts.state.setProperty (kLufsTargetProp, 2, nullptr);   // streaming, -14 LUFS
    apvts.state.setProperty (kPresetNameProp, "INIT", nullptr);
    apvts.state.setProperty (kChainProp, "EQ,COMP,SAT,STEREO", nullptr);
    apvts.state.addListener (this);
}

ColorForgeAudioProcessor::~ColorForgeAudioProcessor()
{
    apvts.state.removeListener (this);
}

//==============================================================================
const char* ColorForgeAudioProcessor::chainModuleName (int module) noexcept
{
    static const char* names[] = { "EQ", "COMP", "SAT", "STEREO" };
    return names[juce::jlimit (0, (int) numChainModules - 1, module)];
}

std::array<int, ColorForgeAudioProcessor::numChainModules> ColorForgeAudioProcessor::getChainOrder() const noexcept
{
    const int packed = chainPacked.load (std::memory_order_relaxed);
    std::array<int, numChainModules> order {};
    for (int i = 0; i < numChainModules; ++i)
        order[(size_t) i] = (packed >> (2 * i)) & 3;
    return order;
}

void ColorForgeAudioProcessor::setChainOrder (const std::array<int, numChainModules>& order)
{
    juce::StringArray names;
    for (int m : order)
        names.add (chainModuleName (m));
    apvts.state.setProperty (kChainProp, names.joinIntoString (","), nullptr);   // the listener updates the atomic
}

void ColorForgeAudioProcessor::readChainOrderFromState()
{
    auto names = juce::StringArray::fromTokens (apvts.state.getProperty (kChainProp, "").toString(), ",", "");
    std::array<int, numChainModules> order { -1, -1, -1, -1 };
    bool used[numChainModules] {};
    int n = 0;

    for (const auto& name : names)
        for (int m = 0; m < numChainModules; ++m)
            if (name.trim().equalsIgnoreCase (chainModuleName (m)) && ! used[m] && n < numChainModules)
            {
                order[(size_t) n++] = m;
                used[m] = true;
            }

    for (int m = 0; m < numChainModules; ++m)   // anything missing goes at the end in default order
        if (! used[m])
            order[(size_t) n++] = m;

    int packed = 0;
    for (int i = 0; i < numChainModules; ++i)
        packed |= order[(size_t) i] << (2 * i);
    chainPacked.store (packed, std::memory_order_relaxed);
}

void ColorForgeAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& prop)
{
    if (tree == apvts.state && prop.toString() == kChainProp)
        readChainOrderFromState();
}

void ColorForgeAudioProcessor::valueTreeRedirected (juce::ValueTree&)
{
    readChainOrderFromState();
}

//==============================================================================
void ColorForgeAudioProcessor::applyFactoryPreset (int index)
{
    index = juce::jlimit (0, cf::kNumFactoryPresets - 1, index);
    const auto& preset = cf::kFactoryPresets[index];

    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, value] : preset.values)
        if (auto* ranged = apvts.getParameter (id))
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));

    apvts.state.setProperty (kChainProp, preset.chain, nullptr);
    apvts.state.setProperty (kPresetNameProp, preset.name, nullptr);
    apvts.state.setProperty ("program", index, nullptr);
    currentProgram = index;
}

void ColorForgeAudioProcessor::setCurrentProgram (int index)
{
    // Hosts re-send the stored program on session load; re-applying it would wipe later tweaks.
    if (index != currentProgram)
        applyFactoryPreset (index);
}

const juce::String ColorForgeAudioProcessor::getProgramName (int index)
{
    return index >= 0 && index < cf::kNumFactoryPresets ? juce::String (cf::kFactoryPresets[index].name) : juce::String();
}

juce::String ColorForgeAudioProcessor::getPresetName() const
{
    return apvts.state.getProperty (kPresetNameProp, "INIT").toString();
}

bool ColorForgeAudioProcessor::savePreset (const juce::File& file)
{
    auto state = apvts.copyState();
    state.setProperty (kPresetNameProp, file.getFileNameWithoutExtension().toUpperCase(), nullptr);
    state.removeProperty ("uiPage", nullptr);

    auto xml = state.createXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory())
        return false;

    if (! xml->writeTo (file))
        return false;

    apvts.state.setProperty (kPresetNameProp, file.getFileNameWithoutExtension().toUpperCase(), nullptr);
    return true;
}

bool ColorForgeAudioProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);
    tree.setProperty (kPresetNameProp, file.getFileNameWithoutExtension().toUpperCase(), nullptr);
    restoreState (tree, true);
    return true;
}

void ColorForgeAudioProcessor::restoreState (juce::ValueTree tree, bool keepUiState)
{
    if (keepUiState)
        for (const char* prop : { "uiPage", kLufsTargetProp })
            if (apvts.state.hasProperty (prop))
                tree.setProperty (prop, apvts.state.getProperty (prop), nullptr);

    apvts.replaceState (tree);
}

//==============================================================================
void ColorForgeAudioProcessor::switchToSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abActive)
        return;

    abSlots[abActive] = apvts.copyState();
    abActive = slot;

    if (abSlots[slot].isValid())   // an empty slot starts as a copy of the other one
        restoreState (abSlots[slot].createCopy(), true);
}

void ColorForgeAudioProcessor::copyActiveToOtherSlot()
{
    abSlots[1 - abActive] = apvts.copyState();
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

    eq.prepare (sampleRate, kChunk);
    comp.prepare (sampleRate);
    sat.prepare (sampleRate);
    imager.prepare (sampleRate);
    limiter.prepare (sampleRate);
    meter.prepare (sampleRate);

    // The dry and kept-sub paths are delayed to match the pitch map, but only while the
    // pitch map is in use; the limiter's lookahead is always there.
    subDelay.prepare (pitchMap.getLatencySamples());
    dryDelay.prepare (pitchMap.getLatencySamples());
    bypassDelay.prepare (pitchMap.getLatencySamples() + limiter.getLatencySamples());
    inLevel.prepare (sampleRate);
    wetLevel.prepare (sampleRate);

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

    for (auto* s : { &driveSm, &widthSm, &mixSm, &gainSm, &subSm, &inGainSm, &colorSm, &bypassSm })
        s->reset (sampleRate, 0.03);
    bypassGainSm.reset (sampleRate, 0.3);
    bypassSm.setCurrentAndTargetValue (isOn (p.matchBypass) ? 1.0f : 0.0f);
    bypassGainSm.setCurrentAndTargetValue (1.0f);

    driveSm.setCurrentAndTargetValue (pct (p.drive));
    widthSm.setCurrentAndTargetValue (pct (p.width));
    mixSm.setCurrentAndTargetValue (pct (p.mix));
    gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (val (p.outGain)));
    subSm.setCurrentAndTargetValue (isOn (p.subOn) ? 1.0f : 0.0f);
    inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (val (p.inGain)));
    colorOn = isOn (p.colorOn);
    colorSm.setCurrentAndTargetValue (colorOn ? 1.0f : 0.0f);

    lfoPhase = 0.0;
    held.reset();

    updateParameters();
    updateLatency (true);
}

//==============================================================================
void ColorForgeAudioProcessor::updateLatency (bool force)
{
    const bool wantPitchMap = colorOn && isOn (p.pmOn);

    if (wantPitchMap == pitchMapActive && ! force)
        return;

    pitchMapActive = wantPitchMap;
    const int pmLatency = pitchMapActive ? pitchMap.getLatencySamples() : 0;

    subDelay.setDelay (pmLatency);
    dryDelay.setDelay (pmLatency);

    if (pitchMapActive)
        pitchMap.reset();

    bypassDelay.setDelay (pmLatency + limiter.getLatencySamples());
    setLatencySamples (pmLatency + limiter.getLatencySamples());
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
    ott.setBands (val (p.ottXLow), val (p.ottXHigh), (int) val (p.ottSolo) - 1);
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

    // ---- Mix & master chain
    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (val (p.inGain)));
    colorOn = isOn (p.colorOn);
    colorSm.setTargetValue (colorOn ? 1.0f : 0.0f);

    eq.setEnabled (isOn (p.eqOn));
    for (int b = 0; b < cf::kEqBands; ++b)
    {
        cf::ParametricEQ::BandParams bp;
        bp.on = isOn (p.eqBandOn[b]);
        bp.freq = val (p.eqFreq[b]);
        bp.gainDb = val (p.eqGain[b]);
        bp.q = val (p.eqQ[b]);
        bp.slope = b == 0 ? (int) val (p.eqLoSlope) : (b == cf::kEqBands - 1 ? (int) val (p.eqHiSlope) : 0);
        bp.mode = juce::jlimit (0, 2, (int) val (p.eqMode[b]));
        if (p.eqDyn[b] != nullptr)
        {
            bp.dynamic = isOn (p.eqDyn[b]);
            bp.dynThresholdDb = val (p.eqDynThr[b]);
        }
        eq.setBand (b, bp);
    }

    {
        cf::Compressor::Params cp;
        cp.on = isOn (p.cpOn);
        cp.thresholdDb = val (p.cpThresh);
        cp.ratio = val (p.cpRatio);
        cp.attackMs = val (p.cpAttack);
        cp.releaseMs = val (p.cpRelease);
        cp.kneeDb = val (p.cpKnee);
        cp.makeupDb = val (p.cpMakeup);
        cp.autoMakeup = isOn (p.cpAuto);
        cp.mix = pct (p.cpMix);
        cp.scHpfHz = val (p.cpScHpf);
        comp.setParams (cp);
    }
    {
        cf::Saturator::Params sp;
        sp.on = isOn (p.satOn);
        sp.mode = (int) val (p.satMode);
        sp.driveDb = val (p.satDrive);
        sp.tone = pct (p.satTone);
        sp.mix = pct (p.satMix);
        sp.outDb = val (p.satOut);
        sat.setParams (sp);
    }
    {
        cf::StereoImager::Params ip;
        ip.on = isOn (p.imgOn);
        ip.width = pct (p.imgWidth);
        ip.monoBass = isOn (p.imgMonoOn);
        ip.monoFreq = val (p.imgMonoFreq);
        ip.balance = pct (p.imgBalance);
        imager.setParams (ip);
    }
    {
        cf::Limiter::Params lp;
        lp.on = isOn (p.limit);
        lp.mode = (int) val (p.lmMode);
        lp.gainDb = val (p.lmGain);
        lp.ceilingDb = val (p.lmCeiling);
        lp.releaseMs = val (p.lmRelease);
        limiter.setParams (lp);
    }
}

//==============================================================================
void ColorForgeAudioProcessor::processColorChunk (float* l, float* r, int num)
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
            default: if (pitchMapActive) pitchMap.process (l, r, num); break;
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

    // ---- Width, sub re-add, colour mix
    subDelay.process (subL, subR, num);
    dryDelay.process (dryL, dryR, num);

    for (int i = 0; i < num; ++i)
    {
        const float width = widthSm.getNextValue();
        const float mid = 0.5f * (l[i] + r[i]);
        const float side = 0.5f * (l[i] - r[i]) * width;
        const float wetL = mid + side + subL[i];
        const float wetR = mid - side + subR[i];

        const float m = mixSm.getNextValue() * colorSm.getNextValue();
        l[i] = dryL[i] + (wetL - dryL[i]) * m;
        r[i] = dryR[i] + (wetR - dryR[i]) * m;
    }
}

void ColorForgeAudioProcessor::processChunk (float* l, float* r, int num)
{
    // ---- Untouched input, for the level-matched bypass
    float* rawL = rawBuf[0].data();
    float* rawR = rawBuf[1].data();
    std::copy (l, l + num, rawL);
    std::copy (r, r + num, rawR);
    inLevel.process (rawL, rawR, num);
    bypassDelay.process (rawL, rawR, num);

    // ---- Input gain
    for (int i = 0; i < num; ++i)
    {
        const float g = inGainSm.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }

    // ---- Colour FX (the original ColorForge chain), skipped entirely while off
    if (colorSm.isSmoothing() || colorOn)
        processColorChunk (l, r, num);

    // ---- Mix & master chain
    const int packed = chainPacked.load (std::memory_order_relaxed);
    for (int i = 0; i < numChainModules; ++i)
    {
        switch ((packed >> (2 * i)) & 3)
        {
            case modEq:   eq.process (l, r, num);     break;
            case modComp: comp.process (l, r, num);   break;
            case modSat:  sat.process (l, r, num);    break;
            default:      imager.process (l, r, num); break;
        }
    }

    for (int i = 0; i < num; ++i)
    {
        const float g = gainSm.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }

    limiter.process (l, r, num);

    // ---- Level-matched bypass: the delayed input, scaled to the processed loudness
    wetLevel.process (l, r, num);
    bypassSm.setTargetValue (isOn (p.matchBypass) ? 1.0f : 0.0f);
    {
        const double ratio = (wetLevel.energy + 1.0e-12) / (inLevel.energy + 1.0e-12);
        const float matchDb = (float) juce::jlimit (-18.0, 18.0, 10.0 * std::log10 (ratio));
        const bool quiet = inLevel.energy < 1.0e-9 || wetLevel.energy < 1.0e-9;
        if (! quiet)
            bypassGainSm.setTargetValue (juce::Decibels::decibelsToGain (matchDb));
        bypassMatchDb.store (juce::Decibels::gainToDecibels (bypassGainSm.getTargetValue()), std::memory_order_relaxed);
    }

    if (bypassSm.isSmoothing() || bypassSm.getTargetValue() > 0.0f)
    {
        for (int i = 0; i < num; ++i)
        {
            const float m = bypassSm.getNextValue();
            const float g = bypassGainSm.getNextValue();
            l[i] += m * (rawL[i] * g - l[i]);
            r[i] += m * (rawR[i] * g - r[i]);
        }
    }
    else
    {
        bypassGainSm.skip (num);
    }

    int w = scopeWrite.load (std::memory_order_relaxed);

    for (int i = 0; i < num; ++i)
    {
        if (! std::isfinite (l[i])) l[i] = 0.0f;
        if (! std::isfinite (r[i])) r[i] = 0.0f;

        scope[(size_t) w] = 0.5f * (l[i] + r[i]);
        w = (w + 1) & (kScopeSize - 1);
    }

    scopeWrite.store (w, std::memory_order_relaxed);
    meter.process (l, r, num);
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
    updateLatency (false);

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
        {
            restoreState (juce::ValueTree::fromXml (*xml), false);
            currentProgram = juce::jlimit (0, cf::kNumFactoryPresets - 1, (int) apvts.state.getProperty ("program", 0));
        }
}

juce::AudioProcessorEditor* ColorForgeAudioProcessor::createEditor()
{
    return new ColorForgeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ColorForgeAudioProcessor();
}
