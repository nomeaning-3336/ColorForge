#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <bitset>
#include <vector>

#include "Params.h"
#include "Presets.h"
#include "dsp/FreqShifter.h"
#include "dsp/ResonatorBank.h"
#include "dsp/SpectralPitchMap.h"
#include "dsp/OTT.h"
#include "dsp/ParametricEQ.h"
#include "dsp/Compressor.h"
#include "dsp/Saturator.h"
#include "dsp/StereoImager.h"
#include "dsp/Limiter.h"
#include "dsp/LoudnessMeter.h"

//==============================================================================
// The editor is written against the JUCE 8 font API. When building with JUCE 7
// (MinGW toolchain), provide a minimal stand-in that converts to juce::Font.
#if JUCE_MAJOR_VERSION < 8
namespace juce
{
class FontOptions
{
public:
    FontOptions() = default;
    FontOptions (float h) : height (h) {}
    FontOptions (float h, int flags) : height (h), styleFlags (flags) {}
    FontOptions (const String& n, float h, int flags) : name (n), height (h), styleFlags (flags) {}
    FontOptions (const String& n, const String& s, float h) : name (n), style (s), height (h) {}

    FontOptions withName (const String& n) const        { auto c = *this; c.name = n; return c; }
    FontOptions withStyle (const String& s) const       { auto c = *this; c.style = s; return c; }
    FontOptions withHeight (float h) const              { auto c = *this; c.height = h; return c; }
    FontOptions withPointHeight (float h) const         { auto c = *this; c.height = h; return c; }
    FontOptions withKerningFactor (float k) const       { auto c = *this; c.kerning = k; return c; }
    FontOptions withHorizontalScale (float s) const     { auto c = *this; c.horizontalScale = s; return c; }
    FontOptions withUnderline (bool u = true) const     { auto c = *this; c.underline = u; return c; }

    operator Font() const
    {
        Font f = name.isNotEmpty() ? Font (name, height, styleFlags) : Font (height, styleFlags);

        if (style.isNotEmpty())
            f.setTypefaceStyle (style);

        f.setExtraKerningFactor (kerning);
        f.setHorizontalScale (horizontalScale);
        f.setUnderline (underline);
        return f;
    }

private:
    String name, style;
    float height = 14.0f, kerning = 0.0f, horizontalScale = 1.0f;
    int styleFlags = 0;
    bool underline = false;
};
} // namespace juce
#endif

//==============================================================================
class ColorForgeAudioProcessor : public juce::AudioProcessor,
                                 private juce::ValueTree::Listener
{
public:
    ColorForgeAudioProcessor();
    ~ColorForgeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "ColorForge"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 10.0; }

    int getNumPrograms() override                           { return cf::kNumFactoryPresets; }
    int getCurrentProgram() override                        { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Editor-facing API
    juce::AudioProcessorValueTreeState apvts;

    /** 12-bit pitch-class mask currently in use (bit 0 = C ... bit 11 = B). */
    std::atomic<int> activeNoteMask { 0 };

    /** Copies the most recent mono output samples, oldest first. numSamples <= 8192. */
    void getScopeSnapshot (float* dest, int numSamples) const noexcept;
    double getScopeSampleRate() const noexcept              { return scopeSampleRate.load(); }

    /** Output loudness / peak / correlation meters (post limiter). */
    cf::LoudnessMeter meter;
    float getCompGainReductionDb() const noexcept           { return comp.grDb.load (std::memory_order_relaxed); }
    float getLimiterGainReductionDb() const noexcept        { return limiter.grDb.load (std::memory_order_relaxed); }
    float getEqAppliedGainDb (int band) const noexcept      { return eq.getAppliedGainDb (band); }
    /** Gain applied to the dry signal while the level-matched bypass is on (dB). */
    float getBypassMatchDb() const noexcept                 { return bypassMatchDb.load (std::memory_order_relaxed); }

    /** Non-automatable UI state stored with the plugin state. */
    static constexpr const char* kLufsTargetProp = "lufsTarget";
    static constexpr const char* kPresetNameProp = "presetName";
    static constexpr const char* kChainProp      = "chainOrder";

    //==============================================================================
    // Presets (message thread)
    void applyFactoryPreset (int index);
    bool savePreset (const juce::File&);
    bool loadPreset (const juce::File&);
    juce::String getPresetName() const;

    // A/B snapshots (message thread; kept for the session, not saved with the project)
    void switchToSlot (int slot);
    void copyActiveToOtherSlot();
    int getActiveSlot() const noexcept                      { return abActive; }

    // Mix chain order: EQ, COMP, SAT and STEREO in any order (message thread)
    enum ChainModule { modEq = 0, modComp, modSat, modStereo, numChainModules };
    std::array<int, numChainModules> getChainOrder() const noexcept;
    void setChainOrder (const std::array<int, numChainModules>&);
    static const char* chainModuleName (int module) noexcept;

private:
    static constexpr int kChunk = 128;          // control-rate granularity
    static constexpr int kScopeSize = 16384;    // power of two

    struct StereoDelay
    {
        void prepare (int maxDelaySamples);          // allocates; message thread / prepareToPlay
        void setDelay (int delaySamples) noexcept;   // clears; real-time safe
        void process (float* l, float* r, int num) noexcept;

        std::vector<float> buf[2];
        int size = 0, pos = 0;
    };

    struct ParamRefs
    {
        std::atomic<float> *root, *scale, *chord, *midiFollow, *routing, *drive, *subOn, *subFreq, *width, *mix, *outGain, *limit;
        std::atomic<float> *pmOn, *pmAmount, *pmPurity, *pmShift, *pmSmear, *pmMix;
        std::atomic<float> *resOn, *resMode, *resDecay, *resDamp, *resOct, *resStack, *resDetune, *resMix;
        std::atomic<float> *fsOn, *fsShift, *fsSpread, *fsFeedback, *fsLfoRate, *fsLfoDepth, *fsMix;
        std::atomic<float> *chOn, *chRate, *chDepth, *chMix;
        std::atomic<float> *rvOn, *rvSize, *rvDamp, *rvMix, *rvFreeze;
        std::atomic<float> *ottOn, *ottDepth, *ottTime, *ottUp, *ottDown, *ottLow, *ottMid, *ottHigh;

        // v2
        std::atomic<float> *inGain, *colorOn;
        std::atomic<float> *eqOn;
        std::atomic<float> *eqBandOn[cf::kEqBands], *eqFreq[cf::kEqBands], *eqGain[cf::kEqBands], *eqQ[cf::kEqBands];
        std::atomic<float> *eqLoSlope, *eqHiSlope, *eqMode[cf::kEqBands];
        std::atomic<float> *eqDyn[cf::kEqBands] {}, *eqDynThr[cf::kEqBands] {};
        std::atomic<float> *matchBypass, *ottXLow, *ottXHigh, *ottSolo;
        std::atomic<float> *cpOn, *cpThresh, *cpRatio, *cpAttack, *cpRelease, *cpKnee, *cpMakeup, *cpAuto, *cpMix, *cpScHpf;
        std::atomic<float> *satOn, *satMode, *satDrive, *satTone, *satMix, *satOut;
        std::atomic<float> *imgOn, *imgWidth, *imgMonoOn, *imgMonoFreq, *imgBalance;
        std::atomic<float> *lmMode, *lmGain, *lmCeiling, *lmRelease;
    };

    void updateParameters();
    void updateLatency (bool force);
    void restoreState (juce::ValueTree, bool keepUiState);
    void readChainOrderFromState();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void processChunk (float* l, float* r, int num);
    void processColorChunk (float* l, float* r, int num);

    ParamRefs p {};
    double sr = 48000.0;

    // DSP
    cf::ResonatorBank resonator;
    cf::FreqShifter shifter;
    cf::SpectralPitchMap pitchMap;
    cf::OTT ott;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::LinkwitzRileyFilter<float> crossover;
    juce::Reverb reverb;
    StereoDelay subDelay, dryDelay, bypassDelay;

    /** K-weighted energy with a ~1 s average, for level matching. */
    struct LevelTracker
    {
        void prepare (double sampleRate);
        void process (const float* l, const float* r, int num) noexcept;
        double energy = 0.0, coef = 0.9999;
        cf::Biquad kw[2][2];
    };
    LevelTracker inLevel, wetLevel;
    std::array<float, kChunk> rawBuf[2] {};
    juce::SmoothedValue<float> bypassSm, bypassGainSm;
    std::atomic<float> bypassMatchDb { 0.0f };

    cf::ParametricEQ eq;
    cf::Compressor comp;
    cf::Saturator sat;
    cf::StereoImager imager;
    cf::Limiter limiter;

    juce::SmoothedValue<float> driveSm, widthSm, mixSm, gainSm, subSm, inGainSm, colorSm;
    std::array<float, kChunk> dryBuf[2] {}, subBuf[2] {};

    // Block state
    std::bitset<128> held;
    int routing = 0;
    float fsShiftHz = 0.0f, fsSpreadHz = 0.0f, fsFeedback = 0.0f, fsMix = 0.0f, lfoRateHz = 0.25f, lfoDepthHz = 0.0f;
    double lfoPhase = 0.0;
    float lastSubFreq = -1.0f;
    bool reverbOn = true, reverbWasOn = true;
    bool colorOn = false, pitchMapActive = false;   // pitch map latency only while it is in use

    std::atomic<int> chainPacked { 0 | (1 << 2) | (2 << 4) | (3 << 6) };   // 2 bits per slot
    int currentProgram = 0;
    juce::ValueTree abSlots[2];
    int abActive = 0;

    // Scope
    std::array<float, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<double> scopeSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorForgeAudioProcessor)
};
