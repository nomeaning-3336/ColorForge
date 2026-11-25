#pragma once

#include "PluginProcessor.h"

#include <vector>
#include <memory>

namespace cfui
{
//==============================================================================
class ColorForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ColorForgeLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, juce::RangedAudioParameter&,
          const juce::String& paramID, const juce::String& shortName, juce::Colour hue);

    void resized() override;

    juce::Slider slider;
    juce::Label  caption;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

//==============================================================================
class ChoiceControl : public juce::Component
{
public:
    ChoiceControl (juce::AudioProcessorValueTreeState&, juce::RangedAudioParameter&,
                   const juce::String& paramID, const juce::String& captionText, juce::Colour accent);

    void resized() override;

    juce::Label    caption;
    juce::ComboBox box;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceControl)
};

//==============================================================================
class ToggleControl : public juce::Component
{
public:
    ToggleControl (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
                   const juce::String& text, juce::Colour accent, bool isPowerSwitch);

    void resized() override;

    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleControl)
};

//==============================================================================
struct KnobSpec   { const char* id; const char* label; };
struct ToggleSpec { const char* id; const char* label; };

class FxPanel : public juce::Component
{
public:
    FxPanel (juce::AudioProcessorValueTreeState&, const juce::String& title, float hue,
             const char* powerID, const char* comboID,
             std::vector<ToggleSpec> toggleSpecs, std::vector<KnobSpec> knobSpecs, int knobColumns);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updatePowerDim();

    juce::String title;
    juce::Colour colour;
    int columns = 3;

    std::unique_ptr<ToggleControl> power;
    std::unique_ptr<ChoiceControl> combo;
    juce::OwnedArray<ToggleControl> toggles;
    juce::OwnedArray<Knob> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};

//==============================================================================
class SpectrumAnalyzer : public juce::Component
{
public:
    explicit SpectrumAnalyzer (ColorForgeAudioProcessor&);

    void update();   // called from the editor's timer (message thread)
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr float minFreq = 30.0f, maxFreq = 18000.0f;
    static constexpr float minDb = -90.0f, maxDb = 0.0f;
    static constexpr int columnWidth = 3;

    void rebuildColumns();
    float xForFreq (float freq) const noexcept;
    static juce::Colour colourForFreq (float freq, float sat, float bright) noexcept;

    ColorForgeAudioProcessor& processor;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };

    std::vector<float> fftData;        // 2 * fftSize
    std::vector<float> columnLo, columnHi, columnLevel;
    std::vector<juce::Colour> columnColour;

    juce::Rectangle<int> specArea, noteArea;
    double sampleRate = 44100.0;
    int noteMask = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
} // namespace cfui

//==============================================================================
class ColorForgeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit ColorForgeAudioProcessorEditor (ColorForgeAudioProcessor&);
    ~ColorForgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ColorForgeAudioProcessor& audioProcessor;
    cfui::ColorForgeLookAndFeel lnf;

    std::unique_ptr<cfui::SpectrumAnalyzer> analyzer;

    std::unique_ptr<cfui::ChoiceControl> rootBox, scaleBox, chordBox, routingBox;
    std::unique_ptr<cfui::ToggleControl> midiFollow;

    std::unique_ptr<cfui::FxPanel> pitchPanel, resPanel, shiftPanel, chorusPanel,
                                   reverbPanel, ottPanel, ioPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorForgeAudioProcessorEditor)
};
