#pragma once

#include "PluginProcessor.h"
#include "ui/PixelTheme.h"

#include <array>
#include <memory>
#include <vector>

namespace cfui
{
//==============================================================================
class PixelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PixelLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

//==============================================================================
class Knob : public juce::Component,
             private juce::Timer
{
public:
    Knob (juce::AudioProcessorValueTreeState&, juce::RangedAudioParameter&,
          const juce::String& paramID, const juce::String& shortName, juce::Colour hue);
    ~Knob() override;

    void resized() override;

    juce::Slider slider;
    juce::Label  caption;

private:
    void timerCallback() override;   // only runs for knobs that can be modulated

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    const ColorForgeAudioProcessor* processor = nullptr;
    int modTarget = 0;
    float shownMod = 0.0f;

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

/** A dialogue-window style module panel: title tab, power LED, optional
    mode box and toggles, and a grid of knobs. An optional "display" area at
    the top can be claimed by a child component (see getDisplayArea). */
class FxPanel : public juce::Component
{
public:
    FxPanel (juce::AudioProcessorValueTreeState&, const juce::String& title, juce::Colour accent,
             const char* powerID, const char* comboID,
             std::vector<ToggleSpec> toggleSpecs, std::vector<KnobSpec> knobSpecs, int knobColumns,
             int displayHeight = 0);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Rectangle<int> getDisplayArea() const noexcept   { return displayArea; }
    juce::Colour getAccent() const noexcept                { return colour; }

    /** Adds another mode box on its own row, above the main combo / toggles row. */
    void addExtraCombo (juce::AudioProcessorValueTreeState&, const juce::String& paramID);

private:
    void updatePowerDim();

    juce::String title;
    juce::Colour colour;
    int columns = 3, displayHeight = 0;
    juce::Rectangle<int> displayArea;

    std::unique_ptr<ToggleControl> power;
    std::unique_ptr<ChoiceControl> combo;
    juce::OwnedArray<ChoiceControl> extraCombos;
    juce::OwnedArray<ToggleControl> toggles;
    juce::OwnedArray<Knob> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};

//==============================================================================
/** FFT of the processor's output scope, smoothed, shared by the analyzers. */
class SpectrumSource
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr float minDb = -90.0f, maxDb = 0.0f;

    void update (ColorForgeAudioProcessor&);

    /** Peak smoothed level (dB) between two frequencies. */
    float levelDb (float f0, float f1) const noexcept;
    double getSampleRate() const noexcept   { return sampleRate; }

private:
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> binDb = std::vector<float> ((size_t) fftSize / 2, minDb);
    double sampleRate = 44100.0;
};

//==============================================================================
/** Pitch-coloured pixel spectrum plus the active note grid (COLOR page). */
class SpectrumAnalyzer : public juce::Component
{
public:
    SpectrumAnalyzer (ColorForgeAudioProcessor&, SpectrumSource&);

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr float minFreq = 30.0f, maxFreq = 18000.0f;

    ColorForgeAudioProcessor& processor;
    SpectrumSource& source;
    juce::Rectangle<int> specArea, noteArea;
    int noteMask = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};

//==============================================================================
/** EQ curve over the live spectrum, with draggable band handles. */
class EqGraph : public juce::Component
{
public:
    EqGraph (ColorForgeAudioProcessor&, SpectrumSource&);

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f, rangeDb = 24.0f;

    float xForFreq (float f) const noexcept;
    float freqForX (float x) const noexcept;
    float yForDb (float db) const noexcept;
    float dbForY (float y) const noexcept;
    juce::Point<float> handlePos (int band) const;
    int hitBand (juce::Point<float>) const;
    float paramValue (const juce::String& id) const;
    void setParam (const juce::String& id, float plain);
    bool bandHasGain (int band) const noexcept   { return band != 0 && band != cf::kEqBands - 1; }

    ColorForgeAudioProcessor& processor;
    SpectrumSource& source;
    juce::Rectangle<float> plot;
    std::vector<float> curveDb, liveDb;   // liveDb: the curve with dynamic bands at their current gain
    bool anyDynamic = false;
    int dragBand = -1, hoverBand = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqGraph)
};

//==============================================================================
/** Compressor transfer curve plus a scrolling gain-reduction history. */
class CompDisplay : public juce::Component
{
public:
    explicit CompDisplay (ColorForgeAudioProcessor&);

    void update();
    void paint (juce::Graphics&) override;

private:
    ColorForgeAudioProcessor& processor;
    std::array<float, 160> history {};
    int writePos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompDisplay)
};

//==============================================================================
/** Short-term loudness over the last minute, with the target zone. */
class LoudnessHistory : public juce::Component
{
public:
    explicit LoudnessHistory (ColorForgeAudioProcessor&);

    void update();
    void paint (juce::Graphics&) override;

private:
    ColorForgeAudioProcessor& processor;
    std::array<float, 600> history {};   // 10 per second
    int writePos = 0, tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessHistory)
};

//==============================================================================
/** Header strip: previous / preset menu / next, and the A/B snapshot buttons. */
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (ColorForgeAudioProcessor&);

    void refresh();
    void resized() override;

private:
    void showMenu();
    void step (int delta);
    juce::Array<juce::File> userPresets() const;

    ColorForgeAudioProcessor& processor;
    juce::TextButton prev { "<" }, next { ">" }, name, slotA { "A" }, slotB { "B" };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

//==============================================================================
/** The processing chain as a node map; lit nodes are on. */
class SignalPath : public juce::Component
{
public:
    explicit SignalPath (ColorForgeAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    static constexpr int kNodes = 8;
    void layoutNodes();
    int nodeAt (juce::Point<float>) const;
    /** Node index -> what it is: -1 IN, -2 COLOR, -3 LIMIT, -4 OUT, or a chain module (0..3). */
    int moduleAtNode (int node) const;

    ColorForgeAudioProcessor& processor;
    std::array<juce::Rectangle<float>, kNodes> nodeBoxes;
    juce::Rectangle<float> hintArea;
    int dragNode = -1;
    juce::Point<float> dragPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalPath)
};

//==============================================================================
/** Always-visible right column: peak / loudness / correlation / GR meters
    and the loudness target readout. */
class StatusPanel : public juce::Component
{
public:
    explicit StatusPanel (ColorForgeAudioProcessor&);

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    float targetLufs() const;

    ColorForgeAudioProcessor& processor;
    juce::ComboBox targetBox;
    juce::TextButton resetButton { "RESET" };
    juce::ToggleButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    float matchDb = 0.0f;

    float peakL = -100.0f, peakR = -100.0f, lufsM = -70.0f, lufsS = -70.0f, lufsI = -70.0f;
    float truePeak = -100.0f, corr = 0.0f, compGr = 0.0f, limGr = 0.0f;
    juce::Rectangle<int> metersArea, targetArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusPanel)
};
//==============================================================================
/** LFO waveform with a moving dot at the current phase. */
class LfoScope : public juce::Component
{
public:
    LfoScope (ColorForgeAudioProcessor&, int lfoIndex, juce::Colour);

    void update()   { repaint(); }
    void paint (juce::Graphics&) override;

private:
    ColorForgeAudioProcessor& processor;
    int index;
    juce::Colour colour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoScope)
};

/** Envelope follower level as a scrolling trace. */
class EnvScope : public juce::Component
{
public:
    explicit EnvScope (ColorForgeAudioProcessor&);

    void update();
    void paint (juce::Graphics&) override;

private:
    ColorForgeAudioProcessor& processor;
    std::array<float, 120> history {};
    int writePos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvScope)
};

/** The 8-slot modulation matrix: source, target, amount and a live meter per slot. */
class ModMatrix : public juce::Component
{
public:
    explicit ModMatrix (ColorForgeAudioProcessor&);

    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        std::unique_ptr<ChoiceControl> src, dst;
        juce::Slider amount;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Rectangle<int> meter, label;
    };

    ColorForgeAudioProcessor& processor;
    std::array<Row, cf::kNumModSlots> rows;
    std::array<float, cf::kNumModSlots> live {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModMatrix)
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
    enum Page { pageColor = 0, pageEq, pageDynamics, pageMaster, pageMod, numPages };

    void timerCallback() override;
    void showPage (int page);

    ColorForgeAudioProcessor& audioProcessor;
    cfui::PixelLookAndFeel lnf;
    cfui::SpectrumSource spectrum;
    juce::TooltipWindow tooltips { this, 700 };

    std::array<std::unique_ptr<juce::TextButton>, numPages> tabs;
    std::array<std::vector<juce::Component*>, numPages> pageComponents;
    int currentPage = pageColor;

    std::unique_ptr<cfui::StatusPanel> status;
    std::unique_ptr<cfui::PresetBar> presetBar;

    // COLOR
    std::unique_ptr<cfui::SpectrumAnalyzer> analyzer;
    std::unique_ptr<cfui::ToggleControl> colorPower, midiFollow;
    std::unique_ptr<cfui::ChoiceControl> rootBox, scaleBox, chordBox, routingBox;
    std::unique_ptr<cfui::FxPanel> pitchPanel, resPanel, shiftPanel, chorusPanel, reverbPanel, ottPanel, colorIoPanel;

    // EQ
    std::unique_ptr<cfui::ToggleControl> eqPower;
    std::unique_ptr<cfui::EqGraph> eqGraph;
    std::array<std::unique_ptr<cfui::FxPanel>, cf::kEqBands> eqBands;

    // DYNAMICS
    std::unique_ptr<cfui::FxPanel> compPanel, satPanel;
    std::unique_ptr<cfui::CompDisplay> compDisplay;
    std::unique_ptr<cfui::SignalPath> signalPath;

    // MASTER
    std::unique_ptr<cfui::FxPanel> inputPanel, stereoPanel, limiterPanel, outputPanel;
    std::unique_ptr<cfui::LoudnessHistory> loudnessHistory;

    // MOD
    std::unique_ptr<cfui::FxPanel> lfoPanels[2], envPanel, macroPanel;
    std::unique_ptr<cfui::LfoScope> lfoScopes[2];
    std::unique_ptr<cfui::EnvScope> envScope;
    std::unique_ptr<cfui::ModMatrix> modMatrix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorForgeAudioProcessorEditor)
};
