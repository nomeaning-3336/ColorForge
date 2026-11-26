#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include <initializer_list>

namespace cf
{
//==============================================================================
// Note sets. Masks are 12-bit pitch-class sets relative to the root (bit 0 = root).
constexpr std::uint16_t maskOf (std::initializer_list<int> semis)
{
    std::uint16_t m = 0;
    for (int s : semis)
        m = (std::uint16_t) (m | (1u << (s % 12)));
    return m;
}

struct ScaleDef { const char* name; std::uint16_t mask; };    // mask 0 = special "Chord Tones"
struct ChordDef { const char* name; int num; int semis[6]; }; // num 0  = special "Scale Notes"

inline constexpr ScaleDef kScales[] =
{
    { "Chord Tones",      0 },
    { "Major",            maskOf ({ 0, 2, 4, 5, 7, 9, 11 }) },
    { "Minor",            maskOf ({ 0, 2, 3, 5, 7, 8, 10 }) },
    { "Dorian",           maskOf ({ 0, 2, 3, 5, 7, 9, 10 }) },
    { "Phrygian",         maskOf ({ 0, 1, 3, 5, 7, 8, 10 }) },
    { "Lydian",           maskOf ({ 0, 2, 4, 6, 7, 9, 11 }) },
    { "Mixolydian",       maskOf ({ 0, 2, 4, 5, 7, 9, 10 }) },
    { "Locrian",          maskOf ({ 0, 1, 3, 5, 6, 8, 10 }) },
    { "Harmonic Minor",   maskOf ({ 0, 2, 3, 5, 7, 8, 11 }) },
    { "Melodic Minor",    maskOf ({ 0, 2, 3, 5, 7, 9, 11 }) },
    { "Major Pentatonic", maskOf ({ 0, 2, 4, 7, 9 }) },
    { "Minor Pentatonic", maskOf ({ 0, 3, 5, 7, 10 }) },
    { "Blues",            maskOf ({ 0, 3, 5, 6, 7, 10 }) },
    { "Whole Tone",       maskOf ({ 0, 2, 4, 6, 8, 10 }) },
    { "Chromatic",        0x0FFF }
};
inline constexpr int kNumScales = (int) (sizeof (kScales) / sizeof (kScales[0]));

inline constexpr ChordDef kChords[] =
{
    { "Major",       3, { 0, 4, 7 } },
    { "Minor",       3, { 0, 3, 7 } },
    { "Maj7",        4, { 0, 4, 7, 11 } },
    { "Min7",        4, { 0, 3, 7, 10 } },
    { "Dom7",        4, { 0, 4, 7, 10 } },
    { "Maj9",        5, { 0, 4, 7, 11, 14 } },
    { "Min9",        5, { 0, 3, 7, 10, 14 } },
    { "Add9",        4, { 0, 4, 7, 14 } },
    { "Min Add9",    4, { 0, 3, 7, 14 } },
    { "Sus2",        3, { 0, 2, 7 } },
    { "Sus4",        3, { 0, 5, 7 } },
    { "6/9",         5, { 0, 4, 7, 9, 14 } },
    { "Power",       2, { 0, 7 } },
    { "Octaves",     1, { 0 } },
    { "Dim",         3, { 0, 3, 6 } },
    { "Aug",         3, { 0, 4, 8 } },
    { "Scale Notes", 0, { 0 } }
};
inline constexpr int kNumChords = (int) (sizeof (kChords) / sizeof (kChords[0]));

inline constexpr int kEqBands = 8;

/** "eq<band+1><suffix>", e.g. eqId (0, "Freq") == "eq1Freq". */
inline juce::String eqId (int band, const char* suffix)
{
    return "eq" + juce::String (band + 1) + suffix;
}

/** Loudness targets for the "quest" readout (LUFS integrated). */
struct LoudnessTarget { const char* name; float lufs; };
inline constexpr LoudnessTarget kLoudnessTargets[] =
{
    { "CLUB  -8",      -8.0f },
    { "LOUD  -10",    -10.0f },
    { "STREAM -14",   -14.0f },
    { "PODCAST -16",  -16.0f },
    { "BROADCAST -23", -23.0f }
};
inline constexpr int kNumLoudnessTargets = (int) (sizeof (kLoudnessTargets) / sizeof (kLoudnessTargets[0]));

//==============================================================================
// Parameter IDs (all version 1):
//   Global    : root scale chord midiFollow routing drive subOn subFreq width mix outGain limit
//   Pitch map : pmOn pmAmount pmPurity pmShift pmSmear pmMix
//   Resonator : resOn resMode resDecay resDamp resOct resStack resDetune resMix
//   Shifter   : fsOn fsShift fsSpread fsFeedback fsLfoRate fsLfoDepth fsMix
//   Chorus    : chOn chRate chDepth chMix
//   Reverb    : rvOn rvSize rvDamp rvMix rvFreeze
//   OTT       : ottOn ottDepth ottTime ottUp ottDown ottLow ottMid ottHigh
// Version 2 (mix & master suite):
//   Global    : inGain colorOn
//   EQ        : eqOn, eq1..eq8 On/Freq/Gain/Q, eq1Slope eq8Slope
//   Compressor: cpOn cpThresh cpRatio cpAttack cpRelease cpKnee cpMakeup cpAuto cpMix cpScHpf
//   Saturator : satOn satMode satDrive satTone satMix satOut
//   Stereo    : imgOn imgWidth imgMonoOn imgMonoFreq imgBalance
//   Limiter   : limit (v1) lmMode lmGain lmCeiling lmRelease
// Version 3:
//   EQ        : eq1Mode..eq8Mode (Stereo / Mid / Side)
// Version 4:
//   EQ        : eq2..eq7 Dyn / DynThr (dynamic bells and shelves)
//   Global    : matchBypass        OTT: ottXLow ottXHigh ottSolo
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    int ver = 1;   // parameters added in 2.0 use version 2

    auto addFloat = [&layout, &ver] (const char* pid, const char* name, NormalisableRange<float> range, float def, const char* unit)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid, ver }, name, range, def,
                                                           AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addPct = [&addFloat] (const char* pid, const char* name, float def)
    {
        addFloat (pid, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def, "%");
    };
    auto addBool = [&layout, &ver] (const char* pid, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid, ver }, name, def));
    };
    auto addChoice = [&layout, &ver] (const char* pid, const char* name, const StringArray& choices, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid, ver }, name, choices, def));
    };
    auto addInt = [&layout, &ver] (const char* pid, const char* name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { pid, ver }, name, lo, hi, def));
    };
    auto skewed = [] (float lo, float hi, float step, float centre)
    {
        NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    };

    StringArray scaleNames, chordNames;
    for (const auto& s : kScales) scaleNames.add (s.name);
    for (const auto& c : kChords) chordNames.add (c.name);

    // ---- Global
    addChoice ("root",  "Root",  { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 5);
    addChoice ("scale", "Scale", scaleNames, 0);
    addChoice ("chord", "Chord", chordNames, 3);
    addBool   ("midiFollow", "MIDI Follow", false);
    addChoice ("routing", "Routing", { "Res > Shift > Map", "Res > Map > Shift", "Shift > Res > Map",
                                       "Shift > Map > Res", "Map > Res > Shift", "Map > Shift > Res" }, 0);
    addPct    ("drive", "Color Drive", 20.0f);
    addBool   ("subOn", "Sub Keep", true);
    addFloat  ("subFreq", "Sub Freq", skewed (30.0f, 300.0f, 1.0f, 100.0f), 90.0f, "Hz");
    addFloat  ("width", "Color Width", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 130.0f, "%");
    addPct    ("mix", "Color Mix", 100.0f);
    addFloat  ("outGain", "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, "dB");
    addBool   ("limit", "Limiter On", true);

    // ---- Spectral pitch map
    addBool   ("pmOn", "Pitch Map On", true);
    addPct    ("pmAmount", "Map Amount", 100.0f);
    addPct    ("pmPurity", "Map Purity", 30.0f);
    addFloat  ("pmShift", "Map Shift", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, "st");
    addPct    ("pmSmear", "Map Smear", 30.0f);
    addPct    ("pmMix", "Map Mix", 100.0f);

    // ---- Resonator bank
    addBool   ("resOn", "Resonator On", true);
    addChoice ("resMode", "Res Mode", { "Comb +", "Comb -", "Tone" }, 0);
    addFloat  ("resDecay", "Res Decay", skewed (0.05f, 10.0f, 0.001f, 1.0f), 1.5f, "s");
    addPct    ("resDamp", "Res Damp", 35.0f);
    addInt    ("resOct", "Res Octave", -2, 2, 0);
    addInt    ("resStack", "Res Stack", 1, 4, 3);
    addFloat  ("resDetune", "Res Detune", NormalisableRange<float> (0.0f, 50.0f, 0.1f), 8.0f, "ct");
    addPct    ("resMix", "Res Mix", 70.0f);

    // ---- Frequency shifter
    addBool   ("fsOn", "Shifter On", true);
    addFloat  ("fsShift", "Shift", NormalisableRange<float> (-2000.0f, 2000.0f, 0.1f, 0.35f, true), 35.0f, "Hz");
    addFloat  ("fsSpread", "Shift Spread", skewed (0.0f, 100.0f, 0.01f, 10.0f), 4.0f, "Hz");
    addFloat  ("fsFeedback", "Shift Feedback", NormalisableRange<float> (0.0f, 95.0f, 0.1f), 20.0f, "%");
    addFloat  ("fsLfoRate", "Shift LFO Rate", skewed (0.01f, 20.0f, 0.001f, 1.0f), 0.25f, "Hz");
    addFloat  ("fsLfoDepth", "Shift LFO Depth", skewed (0.0f, 500.0f, 0.01f, 50.0f), 10.0f, "Hz");
    addPct    ("fsMix", "Shift Mix", 100.0f);

    // ---- Chorus
    addBool   ("chOn", "Chorus On", true);
    addFloat  ("chRate", "Chorus Rate", skewed (0.05f, 5.0f, 0.001f, 0.7f), 0.4f, "Hz");
    addPct    ("chDepth", "Chorus Depth", 30.0f);
    addPct    ("chMix", "Chorus Mix", 35.0f);

    // ---- Reverb
    addBool   ("rvOn", "Reverb On", true);
    addPct    ("rvSize", "Reverb Size", 85.0f);
    addPct    ("rvDamp", "Reverb Damp", 30.0f);
    addPct    ("rvMix", "Reverb Mix", 30.0f);
    addBool   ("rvFreeze", "Reverb Freeze", false);

    // ---- OTT
    addBool   ("ottOn", "OTT On", true);
    addPct    ("ottDepth", "OTT Depth", 60.0f);
    addFloat  ("ottTime", "OTT Time", skewed (0.1f, 10.0f, 0.001f, 1.0f), 1.0f, "x");
    addPct    ("ottUp", "OTT Upward", 100.0f);
    addPct    ("ottDown", "OTT Downward", 100.0f);
    addFloat  ("ottLow", "OTT Low", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, "dB");
    addFloat  ("ottMid", "OTT Mid", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, "dB");
    addFloat  ("ottHigh", "OTT High", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, "dB");

    // =========================================================================
    // Version 2: mixing & mastering suite
    ver = 2;

    addFloat  ("inGain", "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, "dB");
    addBool   ("colorOn", "Color FX On", false);

    // ---- Parametric EQ
    addBool   ("eqOn", "EQ On", true);
    {
        static const char* names[kEqBands] = { "Low Cut", "Low Shelf", "Bell 1", "Bell 2",
                                               "Bell 3", "Bell 4", "High Shelf", "High Cut" };
        static const float freqs[kEqBands] = { 30.0f, 100.0f, 250.0f, 800.0f, 2500.0f, 6000.0f, 10000.0f, 18000.0f };
        static const float qs[kEqBands]    = { 0.707f, 0.707f, 1.0f, 1.0f, 1.0f, 1.0f, 0.707f, 0.707f };

        for (int b = 0; b < kEqBands; ++b)
        {
            const String pre = "eq" + String (b + 1);
            const String nm = String ("EQ ") + names[b];
            const bool isCut = (b == 0 || b == kEqBands - 1);

            addBool  (eqId (b, "On").toRawUTF8(), (nm + " On").toRawUTF8(), ! isCut);
            addFloat (eqId (b, "Freq").toRawUTF8(), (nm + " Freq").toRawUTF8(), skewed (20.0f, 20000.0f, 0.1f, 1000.0f), freqs[b], "Hz");
            addFloat (eqId (b, "Gain").toRawUTF8(), (nm + " Gain").toRawUTF8(), NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, "dB");
            addFloat (eqId (b, "Q").toRawUTF8(), (nm + " Q").toRawUTF8(), skewed (0.1f, 18.0f, 0.001f, 1.0f), qs[b], "");

            if (isCut)
                addChoice (eqId (b, "Slope").toRawUTF8(), (nm + " Slope").toRawUTF8(),
                           { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 1);
        }
    }

    // ---- Compressor
    addBool   ("cpOn", "Comp On", false);
    addFloat  ("cpThresh", "Comp Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f, "dB");
    addFloat  ("cpRatio", "Comp Ratio", skewed (1.0f, 20.0f, 0.01f, 4.0f), 2.0f, ":1");
    addFloat  ("cpAttack", "Comp Attack", skewed (0.1f, 100.0f, 0.01f, 10.0f), 10.0f, "ms");
    addFloat  ("cpRelease", "Comp Release", skewed (10.0f, 2000.0f, 0.1f, 200.0f), 150.0f, "ms");
    addFloat  ("cpKnee", "Comp Knee", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f, "dB");
    addFloat  ("cpMakeup", "Comp Makeup", NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f, "dB");
    addBool   ("cpAuto", "Comp Auto Makeup", true);
    addPct    ("cpMix", "Comp Mix", 100.0f);
    addFloat  ("cpScHpf", "Comp SC HPF", skewed (20.0f, 500.0f, 1.0f, 100.0f), 20.0f, "Hz");

    // ---- Saturator
    addBool   ("satOn", "Sat On", false);
    addChoice ("satMode", "Sat Mode", { "Tape", "Tube", "Clip" }, 0);
    addFloat  ("satDrive", "Sat Drive", NormalisableRange<float> (0.0f, 36.0f, 0.1f), 6.0f, "dB");
    addFloat  ("satTone", "Sat Tone", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, "%");
    addPct    ("satMix", "Sat Mix", 100.0f);
    addFloat  ("satOut", "Sat Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, "dB");

    // ---- Stereo
    addBool   ("imgOn", "Stereo On", true);
    addFloat  ("imgWidth", "Stereo Width", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f, "%");
    addBool   ("imgMonoOn", "Mono Bass", false);
    addFloat  ("imgMonoFreq", "Mono Bass Freq", skewed (20.0f, 500.0f, 1.0f, 120.0f), 120.0f, "Hz");
    addFloat  ("imgBalance", "Balance", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, "%");

    // ---- Limiter (on/off is the v1 "limit" parameter)
    addChoice ("lmMode", "Limiter Mode", { "Clean", "Clip" }, 0);
    addFloat  ("lmGain", "Limiter Gain", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f, "dB");
    addFloat  ("lmCeiling", "Limiter Ceiling", NormalisableRange<float> (-12.0f, 0.0f, 0.1f), -1.0f, "dBTP");
    addFloat  ("lmRelease", "Limiter Release", skewed (1.0f, 1000.0f, 0.1f, 100.0f), 100.0f, "ms");

    // =========================================================================
    // Version 3: per-band stereo / mid / side processing
    ver = 3;
    {
        static const char* names[kEqBands] = { "Low Cut", "Low Shelf", "Bell 1", "Bell 2",
                                               "Bell 3", "Bell 4", "High Shelf", "High Cut" };
        for (int b = 0; b < kEqBands; ++b)
            addChoice (eqId (b, "Mode").toRawUTF8(), (String ("EQ ") + names[b] + " Mode").toRawUTF8(),
                       { "Stereo", "Mid", "Side" }, 0);
    }

    // =========================================================================
    // Version 4: dynamic EQ, level-matched bypass, multiband OTT
    ver = 4;
    {
        static const char* names[kEqBands] = { "Low Cut", "Low Shelf", "Bell 1", "Bell 2",
                                               "Bell 3", "Bell 4", "High Shelf", "High Cut" };
        for (int b = 1; b < kEqBands - 1; ++b)
        {
            addBool  (eqId (b, "Dyn").toRawUTF8(), (String ("EQ ") + names[b] + " Dynamic").toRawUTF8(), false);
            addFloat (eqId (b, "DynThr").toRawUTF8(), (String ("EQ ") + names[b] + " Dyn Threshold").toRawUTF8(),
                      NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f, "dB");
        }
    }
    addBool   ("matchBypass", "Bypass (Level Matched)", false);
    addFloat  ("ottXLow", "OTT Low/Mid Split", skewed (40.0f, 500.0f, 1.0f, 120.0f), 88.0f, "Hz");
    addFloat  ("ottXHigh", "OTT Mid/High Split", skewed (1000.0f, 10000.0f, 1.0f, 2500.0f), 2500.0f, "Hz");
    addChoice ("ottSolo", "OTT Solo", { "Solo Off", "Solo Low", "Solo Mid", "Solo High" }, 0);

    return layout;
}
} // namespace cf
