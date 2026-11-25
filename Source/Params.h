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

//==============================================================================
// Parameter IDs (all version 1):
//   Global    : root scale chord midiFollow routing drive subOn subFreq width mix outGain limit
//   Pitch map : pmOn pmAmount pmPurity pmShift pmSmear pmMix
//   Resonator : resOn resMode resDecay resDamp resOct resStack resDetune resMix
//   Shifter   : fsOn fsShift fsSpread fsFeedback fsLfoRate fsLfoDepth fsMix
//   Chorus    : chOn chRate chDepth chMix
//   Reverb    : rvOn rvSize rvDamp rvMix rvFreeze
//   OTT       : ottOn ottDepth ottTime ottUp ottDown ottLow ottMid ottHigh
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&layout] (const char* pid, const char* name, NormalisableRange<float> range, float def, const char* unit)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid, 1 }, name, range, def,
                                                           AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addPct = [&addFloat] (const char* pid, const char* name, float def)
    {
        addFloat (pid, name, NormalisableRange<float> (0.0f, 100.0f, 0.1f), def, "%");
    };
    auto addBool = [&layout] (const char* pid, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid, 1 }, name, def));
    };
    auto addChoice = [&layout] (const char* pid, const char* name, const StringArray& choices, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid, 1 }, name, choices, def));
    };
    auto addInt = [&layout] (const char* pid, const char* name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { pid, 1 }, name, lo, hi, def));
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
    addPct    ("drive", "Drive", 20.0f);
    addBool   ("subOn", "Sub Keep", true);
    addFloat  ("subFreq", "Sub Freq", skewed (30.0f, 300.0f, 1.0f, 100.0f), 90.0f, "Hz");
    addFloat  ("width", "Width", NormalisableRange<float> (0.0f, 200.0f, 0.1f), 130.0f, "%");
    addPct    ("mix", "Mix", 100.0f);
    addFloat  ("outGain", "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, "dB");
    addBool   ("limit", "Limiter", true);

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

    return layout;
}
} // namespace cf
