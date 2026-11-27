#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <initializer_list>
#include <utility>

namespace cf
{
//==============================================================================
/** A factory preset: every parameter at its default, then these overrides (plain values). */
struct FactoryPreset
{
    const char* name;
    std::initializer_list<std::pair<const char*, float>> values;
    const char* chain = "TRANS,EQ,COMP,SAT,STEREO";
};

/** Starting points, not finished sounds: tune them by ear on your material. */
inline const FactoryPreset kFactoryPresets[] =
{
    { "INIT", {} },

    { "MIX BUS GLUE", {
        { "cpOn", 1 }, { "cpThresh", -20 }, { "cpRatio", 2 }, { "cpAttack", 30 }, { "cpRelease", 200 },
        { "cpKnee", 8 }, { "cpScHpf", 90 }, { "cpMix", 80 },
        { "eq1On", 1 }, { "eq1Freq", 25 },
        { "satOn", 1 }, { "satMode", 0 }, { "satDrive", 3 }, { "satMix", 50 },
        { "lmCeiling", -1 } } },

    { "MASTER STREAMING -14", {
        { "eq1On", 1 }, { "eq1Freq", 28 }, { "eq1Slope", 2 },
        { "eq2Gain", 0.8f }, { "eq2Freq", 90 }, { "eq7Gain", 1.2f }, { "eq7Freq", 11000 },
        { "cpOn", 1 }, { "cpThresh", -16 }, { "cpRatio", 1.6f }, { "cpAttack", 25 }, { "cpRelease", 250 },
        { "cpKnee", 10 }, { "cpScHpf", 120 },
        { "imgMonoOn", 1 }, { "imgMonoFreq", 110 },
        { "lmGain", 4 }, { "lmCeiling", -1 }, { "lmRelease", 120 } } },

    { "MASTER LOUD CLUB", {
        { "eq1On", 1 }, { "eq1Freq", 30 }, { "eq1Slope", 2 },
        { "eq2Gain", 1.5f }, { "eq2Freq", 70 }, { "eq7Gain", 1.5f },
        { "cpOn", 1 }, { "cpThresh", -18 }, { "cpRatio", 2.5f }, { "cpAttack", 15 }, { "cpRelease", 120 },
        { "cpScHpf", 100 },
        { "satOn", 1 }, { "satMode", 1 }, { "satDrive", 4 }, { "satMix", 40 },
        { "imgMonoOn", 1 }, { "imgMonoFreq", 140 }, { "imgWidth", 110 },
        { "lmGain", 9 }, { "lmCeiling", -0.5f }, { "lmRelease", 60 } } },

    { "VOCAL POLISH", {
        { "eq1On", 1 }, { "eq1Freq", 90 }, { "eq1Slope", 1 },
        { "eq3Freq", 300 }, { "eq3Gain", -2.5f }, { "eq3Q", 1.4f },
        { "eq5Freq", 3200 }, { "eq5Gain", 2 }, { "eq5Q", 0.9f },
        { "eq7Freq", 12000 }, { "eq7Gain", 3 },
        { "cpOn", 1 }, { "cpThresh", -22 }, { "cpRatio", 3.5f }, { "cpAttack", 5 }, { "cpRelease", 90 },
        { "cpKnee", 4 },
        { "lmCeiling", -1 } } },

    { "DRUM PUNCH", {
        { "eq2Freq", 70 }, { "eq2Gain", 2.5f }, { "eq4Freq", 450 }, { "eq4Gain", -3 }, { "eq4Q", 1.2f },
        { "eq6Freq", 5000 }, { "eq6Gain", 2 },
        { "cpOn", 1 }, { "cpThresh", -24 }, { "cpRatio", 4 }, { "cpAttack", 20 }, { "cpRelease", 80 },
        { "cpKnee", 2 }, { "cpMix", 60 },
        { "satOn", 1 }, { "satMode", 2 }, { "satDrive", 6 }, { "satMix", 35 },
        { "trOn", 1 }, { "trAttack", 40 }, { "trSustain", -15 },
        { "lmCeiling", -1 } } },

    { "WARM TAPE", {
        { "satOn", 1 }, { "satMode", 0 }, { "satDrive", 9 }, { "satTone", -25 }, { "satMix", 100 },
        { "eq8On", 1 }, { "eq8Freq", 16000 }, { "eq8Slope", 0 },
        { "lmCeiling", -1 } } },

    { "WIDE + MONO BASS", {
        { "imgWidth", 140 }, { "imgMonoOn", 1 }, { "imgMonoFreq", 150 },
        { "eq7Mode", 2 }, { "eq7Freq", 6000 }, { "eq7Gain", 2.5f },
        { "lmCeiling", -1 } } },

    { "COLOR BASS CLASSIC", {
        { "colorOn", 1 }, { "eqOn", 1 }, { "lmMode", 1 }, { "lmCeiling", 0 } } },
};

inline constexpr int kNumFactoryPresets = (int) (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));

/** User preset folder: Documents/ColorForge/Presets. */
inline juce::File userPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("ColorForge").getChildFile ("Presets");
}

inline constexpr const char* kPresetExtension = ".cfpreset";
} // namespace cf
