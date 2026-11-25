#include "PluginEditor.h"

#include <cmath>

namespace cfui
{
namespace
{
    const juce::Colour bgTop       { 0xff0d111c };
    const juce::Colour bgBottom    { 0xff05070d };
    const juce::Colour panelTop    { 0xff151a27 };
    const juce::Colour panelBottom { 0xff0e121c };
    const juce::Colour textBright  { 0xffe6e9f2 };
    const juce::Colour textDim     { 0xff8a91a6 };
    const juce::Colour controlBg   { 0xff1a2030 };
    const juce::Colour controlLine { 0xff2e3547 };

    juce::Colour hueColour (float hue, float sat = 0.75f, float bright = 1.0f)
    {
        hue = hue - std::floor (hue);
        return juce::Colour::fromHSV (hue, sat, bright, 1.0f);
    }

    juce::Font uiFont (float height, bool bold = false)
    {
        auto opts = juce::FontOptions().withHeight (height);
        if (bold)
            opts = opts.withStyle ("Bold");
        return juce::Font (opts);
    }

    juce::String formatValue (double v)
    {
        const auto a = std::abs (v);
        if (a >= 100.0) return juce::String (v, 0);
        if (a >= 10.0)  return juce::String (v, 1);
        return juce::String (v, 2);
    }
}

//==============================================================================
ColorForgeLookAndFeel::ColorForgeLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bgTop);
    setColour (juce::Label::textColourId, textBright);
    setColour (juce::ComboBox::backgroundColourId, controlBg);
    setColour (juce::ComboBox::textColourId, textBright);
    setColour (juce::ComboBox::outlineColourId, controlLine);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff121725));
    setColour (juce::PopupMenu::textColourId, textBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a3350));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxTextColourId, textDim);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, juce::Colour (0xff2a3350));
    setColour (juce::TextEditor::backgroundColourId, controlBg);
    setColour (juce::TextEditor::textColourId, textBright);
    setColour (juce::TextEditor::outlineColourId, controlLine);
    setColour (juce::TextEditor::focusedOutlineColourId, textDim);
}

void ColorForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
{
    const auto hue = slider.findColour (juce::Slider::rotarySliderFillColourId);
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (5.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    if (radius < 4.0f)
        return;

    const auto centre = bounds.getCentre();
    const float arcRadius = radius - 3.0f;
    const float lineW = juce::jmax (2.0f, radius * 0.12f);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff232a3a));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc (bipolar parameters start from zero)
    float fromAngle = startAngle;
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        fromAngle = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);

    if (std::abs (angle - fromAngle) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                           juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);

        const float alphaMul = slider.isEnabled() ? 1.0f : 0.4f;
        g.setColour (hue.withAlpha (0.10f * alphaMul));
        g.strokePath (arc, juce::PathStrokeType (lineW * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (hue.withAlpha (0.22f * alphaMul));
        g.strokePath (arc, juce::PathStrokeType (lineW * 1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (hue.withMultipliedAlpha (alphaMul));
        g.strokePath (arc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body
    const float bodyR = arcRadius - lineW * 1.3f;
    if (bodyR > 2.0f)
    {
        auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (body.translated (0.0f, 1.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3245), centre.x, body.getY(),
                                                 juce::Colour (0xff121620), centre.x, body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xff3a4258));
        g.drawEllipse (body, 1.0f);

        // pointer
        juce::Path pointer;
        const float pw = juce::jmax (2.0f, bodyR * 0.14f);
        pointer.addRoundedRectangle (-pw * 0.5f, -bodyR + 3.0f, pw, bodyR * 0.55f, pw * 0.5f);
        pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
        g.setColour (slider.isEnabled() ? hue.brighter (0.5f) : textDim);
        g.fillPath (pointer);
    }
}

void ColorForgeLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                              bool highlighted, bool /*down*/)
{
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);

    if (b.getProperties().contains ("power"))
    {
        auto track = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 34.0f), juce::jmin (r.getHeight(), 16.0f));
        const float rad = track.getHeight() * 0.5f;

        if (on)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (track.expanded (2.0f), rad + 2.0f);
        }

        g.setColour (on ? accent.withAlpha (0.85f) : juce::Colour (0xff2a2f3d));
        g.fillRoundedRectangle (track, rad);

        const float d = track.getHeight() - 4.0f;
        auto thumb = juce::Rectangle<float> (d, d).withCentre ({ on ? track.getRight() - rad : track.getX() + rad,
                                                                track.getCentreY() });
        g.setColour (on ? juce::Colours::white : (highlighted ? textBright : textDim));
        g.fillEllipse (thumb);
        return;
    }

    const float rad = r.getHeight() * 0.5f;
    g.setColour (on ? accent.withAlpha (highlighted ? 0.32f : 0.24f)
                    : (highlighted ? controlBg.brighter (0.1f) : controlBg));
    g.fillRoundedRectangle (r, rad);
    g.setColour (on ? accent : controlLine);
    g.drawRoundedRectangle (r, rad, 1.0f);

    // little indicator dot
    const float d = juce::jmin (6.0f, r.getHeight() * 0.35f);
    auto dot = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + rad + 2.0f, r.getCentreY() });
    g.setColour (on ? accent.brighter (0.3f) : juce::Colour (0xff454c60));
    g.fillEllipse (dot);

    g.setColour (on ? textBright : textDim);
    g.setFont (uiFont (12.0f, true));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (rad + d + 2.0f).withTrimmedRight (rad * 0.5f).toNearestInt(),
                      juce::Justification::centred, 1);
}

void ColorForgeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                          int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                                          juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.isMouseOver (true) ? box.findColour (juce::ComboBox::outlineColourId).brighter (0.3f)
                                        : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const float ax = (float) width - 13.0f;
    const float ay = (float) height * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font ColorForgeLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return uiFont (13.0f);
}

void ColorForgeLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 1, juce::jmax (0, box.getWidth() - 24), juce::jmax (0, box.getHeight() - 2));
    label.setFont (getComboBoxFont (box));
}

juce::Font ColorForgeLookAndFeel::getPopupMenuFont()
{
    return uiFont (14.0f);
}

juce::Label* ColorForgeLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (uiFont (11.5f));
    l->setColour (juce::Label::outlineWhenEditingColourId, controlLine);
    l->setColour (juce::Label::backgroundWhenEditingColourId, controlBg);
    l->setColour (juce::Label::textWhenEditingColourId, textBright);
    return l;
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, juce::RangedAudioParameter& param,
            const juce::String& paramID, const juce::String& shortName, juce::Colour hue)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, hue);
    slider.setColour (juce::Slider::thumbColourId, hue);
    addAndMakeVisible (slider);

    caption.setText (shortName, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (uiFont (11.0f, true));
    caption.setColour (juce::Label::textColourId, textBright.withAlpha (0.85f));
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

    // nicer value display (the attachment installed parameter-based text functions; replace display only)
    const juce::String unit = param.getLabel();
    const bool isInt = dynamic_cast<juce::AudioParameterInt*> (&param) != nullptr;
    slider.textFromValueFunction = [unit, isInt] (double v)
    {
        const juce::String s = isInt ? juce::String (juce::roundToInt (v)) : formatValue (v);
        return unit.isNotEmpty() ? s + " " + unit : s;
    };
    slider.updateText();

    slider.setDoubleClickReturnValue (true, (double) param.convertFrom0to1 (param.getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (14));
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (r.getWidth(), 80), 16);
    slider.setBounds (r);
}

//==============================================================================
ChoiceControl::ChoiceControl (juce::AudioProcessorValueTreeState& apvts, juce::RangedAudioParameter& param,
                              const juce::String& paramID, const juce::String& captionText, juce::Colour accent)
{
    caption.setText (captionText, juce::dontSendNotification);
    caption.setFont (uiFont (10.5f, true));
    caption.setColour (juce::Label::textColourId, accent);
    caption.setJustificationType (juce::Justification::centredLeft);
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
    {
        int itemId = 1;
        for (const auto& c : choice->choices)
            box.addItem (c, itemId++);
    }
    addAndMakeVisible (box);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramID, box);
}

void ChoiceControl::resized()
{
    auto r = getLocalBounds();
    if (caption.getText().isNotEmpty())
        caption.setBounds (r.removeFromTop (14));
    else
        caption.setBounds ({});
    box.setBounds (r);
}

//==============================================================================
ToggleControl::ToggleControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
                              const juce::String& text, juce::Colour accent, bool isPowerSwitch)
{
    button.setButtonText (text);
    button.setColour (juce::ToggleButton::tickColourId, accent);
    if (isPowerSwitch)
    {
        button.getProperties().set ("power", true);
        button.setTooltip ("Bypass / enable");
    }
    addAndMakeVisible (button);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramID, button);
}

void ToggleControl::resized()
{
    button.setBounds (getLocalBounds());
}

//==============================================================================
FxPanel::FxPanel (juce::AudioProcessorValueTreeState& apvts, const juce::String& titleText, float hue,
                  const char* powerID, const char* comboID,
                  std::vector<ToggleSpec> toggleSpecs, std::vector<KnobSpec> knobSpecs, int knobColumns)
    : title (titleText), colour (hueColour (hue)), columns (juce::jmax (1, knobColumns))
{
    if (powerID != nullptr && apvts.getParameter (powerID) != nullptr)
    {
        power = std::make_unique<ToggleControl> (apvts, powerID, juce::String(), colour, true);
        power->button.onStateChange = [this] { updatePowerDim(); };
        addAndMakeVisible (*power);
    }

    if (comboID != nullptr)
        if (auto* p = apvts.getParameter (comboID))
        {
            combo = std::make_unique<ChoiceControl> (apvts, *p, comboID, juce::String(), colour);
            addAndMakeVisible (*combo);
        }

    for (const auto& t : toggleSpecs)
        if (apvts.getParameter (t.id) != nullptr)
            addAndMakeVisible (toggles.add (new ToggleControl (apvts, t.id, t.label, colour, false)));

    for (const auto& k : knobSpecs)
        if (auto* p = apvts.getParameter (k.id))
            addAndMakeVisible (knobs.add (new Knob (apvts, *p, k.id, k.label, colour)));

    updatePowerDim();
}

void FxPanel::updatePowerDim()
{
    const bool on = (power == nullptr) || power->button.getToggleState();
    const float alpha = on ? 1.0f : 0.4f;
    for (auto* k : knobs)
        k->setAlpha (alpha);
    for (auto* t : toggles)
        t->setAlpha (alpha);
    if (combo != nullptr)
        combo->setAlpha (alpha);
}

void FxPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    g.setGradientFill (juce::ColourGradient (panelTop, 0.0f, r.getY(), panelBottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);

    // soft hue wash in the header
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.12f), 0.0f, r.getY(),
                                             colour.withAlpha (0.0f), 0.0f, r.getY() + 40.0f, false));
    g.fillRoundedRectangle (r.withHeight (40.0f), 8.0f);

    g.setColour (colour.withAlpha (0.28f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);

    // accent bar
    g.setColour (colour);
    g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, r.getY() + 9.0f, 3.0f, 14.0f), 1.5f);

    g.setColour (colour);
    g.setFont (uiFont (14.0f, true));
    g.drawText (title, juce::Rectangle<int> (22, 4, getWidth() - 80, 24), juce::Justification::centredLeft, true);
}

void FxPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto header = r.removeFromTop (24);

    if (power != nullptr)
        power->setBounds (header.removeFromRight (40).withSizeKeepingCentre (38, 20));

    if (combo != nullptr || ! toggles.isEmpty())
    {
        auto extras = r.removeFromTop (24);
        r.removeFromTop (4);
        extras.removeFromLeft (4);

        if (combo != nullptr)
        {
            combo->setBounds (extras.removeFromLeft (juce::jmin (130, extras.getWidth())).reduced (0, 1));
            extras.removeFromLeft (8);
        }

        for (auto* t : toggles)
        {
            t->setBounds (extras.removeFromLeft (juce::jmin (100, extras.getWidth())).reduced (0, 1));
            extras.removeFromLeft (8);
        }
    }

    const int n = knobs.size();
    if (n == 0)
        return;

    const int cols = juce::jmin (columns, n);
    const int rows = (n + cols - 1) / cols;
    const int cellW = r.getWidth() / cols;
    const int cellH = juce::jmin (r.getHeight() / rows, 120);
    const int gridH = cellH * rows;
    r = r.withSizeKeepingCentre (r.getWidth(), gridH);

    for (int i = 0; i < n; ++i)
    {
        const int row = i / cols;
        const int col = i % cols;
        const int inRow = juce::jmin (cols, n - row * cols);
        const int rowOffset = (r.getWidth() - inRow * cellW) / 2;   // centre incomplete rows
        knobs[i]->setBounds (r.getX() + rowOffset + col * cellW, r.getY() + row * cellH, cellW, cellH);
    }
}

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (ColorForgeAudioProcessor& p)
    : processor (p)
{
    fftData.assign ((size_t) fftSize * 2, 0.0f);
    setInterceptsMouseClicks (false, false);
}

juce::Colour SpectrumAnalyzer::colourForFreq (float freq, float sat, float bright) noexcept
{
    const float midi = 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, freq) / 440.0f);
    float pc = std::fmod (midi, 12.0f);
    if (pc < 0.0f)
        pc += 12.0f;
    return juce::Colour::fromHSV (pc / 12.0f, sat, bright, 1.0f);
}

float SpectrumAnalyzer::xForFreq (float freq) const noexcept
{
    const float norm = std::log (freq / minFreq) / std::log (maxFreq / minFreq);
    return (float) specArea.getX() + norm * (float) specArea.getWidth();
}

void SpectrumAnalyzer::rebuildColumns()
{
    const int numCols = juce::jmax (1, specArea.getWidth() / columnWidth);
    columnLo.assign ((size_t) numCols, 0.0f);
    columnHi.assign ((size_t) numCols, 0.0f);
    columnColour.assign ((size_t) numCols, juce::Colours::white);
    if ((int) columnLevel.size() != numCols)
        columnLevel.assign ((size_t) numCols, minDb);

    const float ratio = maxFreq / minFreq;
    const float binPerHz = (float) fftSize / (float) sampleRate;

    for (int c = 0; c < numCols; ++c)
    {
        const float f0 = minFreq * std::pow (ratio, (float) c / (float) numCols);
        const float f1 = minFreq * std::pow (ratio, (float) (c + 1) / (float) numCols);
        columnLo[(size_t) c] = f0 * binPerHz;
        columnHi[(size_t) c] = f1 * binPerHz;
        columnColour[(size_t) c] = colourForFreq (std::sqrt (f0 * f1), 0.8f, 1.0f);
    }
}

void SpectrumAnalyzer::resized()
{
    auto r = getLocalBounds();
    noteArea = r.removeFromRight (196);
    r.removeFromRight (10);
    specArea = r.reduced (1).withTrimmedLeft (28).withTrimmedBottom (14);
    rebuildColumns();
}

void SpectrumAnalyzer::update()
{
    noteMask = processor.activeNoteMask.load (std::memory_order_relaxed);

    double sr = processor.getScopeSampleRate();
    if (! (sr > 1000.0))
        sr = 44100.0;
    if (std::abs (sr - sampleRate) > 0.5)
    {
        sampleRate = sr;
        rebuildColumns();
    }

    if (columnLevel.empty())
        return;

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    processor.getScopeSnapshot (fftData.data(), fftSize);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 4.0f / (float) fftSize;   // Hann coherent gain 0.5, single-sided
    const int maxBin = fftSize / 2 - 1;

    for (size_t c = 0; c < columnLevel.size(); ++c)
    {
        const float lo = columnLo[c], hi = columnHi[c];
        float mag = 0.0f;

        if (hi - lo < 1.0f)
        {
            const float centre = juce::jlimit (0.0f, (float) maxBin - 1.0f, 0.5f * (lo + hi));
            const int b = (int) centre;
            const float frac = centre - (float) b;
            mag = fftData[(size_t) b] * (1.0f - frac) + fftData[(size_t) b + 1] * frac;
        }
        else
        {
            const int b0 = juce::jlimit (0, maxBin, (int) std::floor (lo));
            const int b1 = juce::jlimit (0, maxBin, (int) std::ceil (hi));
            for (int b = b0; b <= b1; ++b)
                mag = juce::jmax (mag, fftData[(size_t) b]);
        }

        const float db = juce::jlimit (minDb, maxDb, juce::Decibels::gainToDecibels (mag * norm, minDb - 10.0f));
        float& level = columnLevel[c];

        if (db > level)
            level += (db - level) * 0.75f;      // fast attack
        else
            level = juce::jmax (db, level - 1.2f - (level - db) * 0.04f);   // slow release
    }

    repaint();
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    // ---------------- spectrum
    auto frame = getLocalBounds().withTrimmedRight (noteArea.getWidth() + 10).toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff080b13));
    g.fillRoundedRectangle (frame, 8.0f);
    g.setColour (controlLine.withAlpha (0.7f));
    g.drawRoundedRectangle (frame, 8.0f, 1.0f);

    const auto area = specArea.toFloat();

    // grid
    g.setFont (uiFont (10.0f));
    for (float db = -80.0f; db < 0.0f; db += 20.0f)
    {
        const float y = juce::jmap (db, minDb, maxDb, area.getBottom(), area.getY());
        g.setColour (juce::Colour (0x14ffffff));
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        g.setColour (textDim.withAlpha (0.6f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (frame.getX() + 2.0f, y - 6.0f, 24.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }

    static const float gridFreqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
    for (auto f : gridFreqs)
    {
        const float x = xForFreq (f);
        g.setColour (juce::Colour (0x14ffffff));
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (textDim.withAlpha (0.7f));
        const juce::String txt = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
        g.drawText (txt, juce::Rectangle<float> (x - 20.0f, area.getBottom() + 1.0f, 40.0f, 12.0f),
                    juce::Justification::centred, false);
    }

    // bars
    const size_t numCols = columnLevel.size();
    if (numCols > 0 && columnColour.size() == numCols)
    {
        const float colW = area.getWidth() / (float) numCols;
        juce::Path outline;

        for (size_t c = 0; c < numCols; ++c)
        {
            const float level = columnLevel[c];
            const float y = juce::jmap (level, minDb, maxDb, area.getBottom(), area.getY());
            const float x = area.getX() + (float) c * colW;
            const float h = area.getBottom() - y;

            if (c == 0) outline.startNewSubPath (x, y);
            else        outline.lineTo (x + colW * 0.5f, y);

            if (h < 0.5f)
                continue;

            const auto col = columnColour[c];
            g.setGradientFill (juce::ColourGradient (col.withAlpha (0.9f), 0.0f, y,
                                                     col.withAlpha (0.12f), 0.0f, area.getBottom(), false));
            g.fillRect (x, y, juce::jmax (1.0f, colW - 1.0f), h);
            g.setColour (col.brighter (0.4f));
            g.fillRect (x, y, juce::jmax (1.0f, colW - 1.0f), juce::jmin (2.0f, h));
        }

        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.strokePath (outline, juce::PathStrokeType (1.0f));
    }

    // ---------------- note strip
    auto notes = noteArea.toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff080b13));
    g.fillRoundedRectangle (notes, 8.0f);
    g.setColour (controlLine.withAlpha (0.7f));
    g.drawRoundedRectangle (notes, 8.0f, 1.0f);

    auto inner = noteArea.reduced (10, 8);
    g.setColour (textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("ACTIVE NOTES", inner.removeFromTop (16), juce::Justification::centredLeft, false);
    inner.removeFromTop (6);

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int cols = 4, rows = 3;
    const float cw = (float) inner.getWidth() / (float) cols;
    const float ch = (float) inner.getHeight() / (float) rows;

    for (int i = 0; i < 12; ++i)
    {
        const bool active = (noteMask & (1 << i)) != 0;
        const auto hue = juce::Colour::fromHSV ((float) i / 12.0f, 0.8f, 1.0f, 1.0f);
        auto cell = juce::Rectangle<float> ((float) inner.getX() + (float) (i % cols) * cw,
                                            (float) inner.getY() + (float) (i / cols) * ch, cw, ch).reduced (3.0f);

        if (active)
        {
            g.setColour (hue.withAlpha (0.25f));
            g.fillRoundedRectangle (cell.expanded (2.0f), 6.0f);
            g.setColour (hue.withAlpha (0.85f));
            g.fillRoundedRectangle (cell, 5.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
        }
        else
        {
            g.setColour (controlBg);
            g.fillRoundedRectangle (cell, 5.0f);
            g.setColour (hue.withAlpha (0.35f));
            g.drawRoundedRectangle (cell, 5.0f, 1.0f);
            g.setColour (textDim.withAlpha (0.8f));
        }

        g.setFont (uiFont (12.0f, true));
        g.drawText (noteNames[i], cell, juce::Justification::centred, false);
    }
}
} // namespace cfui

//==============================================================================
ColorForgeAudioProcessorEditor::ColorForgeAudioProcessorEditor (ColorForgeAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
    using namespace cfui;
    setLookAndFeel (&lnf);

    auto& apvts = audioProcessor.apvts;

    analyzer = std::make_unique<SpectrumAnalyzer> (audioProcessor);
    addAndMakeVisible (*analyzer);

    auto makeChoice = [&apvts, this] (const char* id, const char* caption, float hue) -> std::unique_ptr<ChoiceControl>
    {
        if (auto* param = apvts.getParameter (id))
        {
            auto c = std::make_unique<ChoiceControl> (apvts, *param, id, caption, juce::Colour::fromHSV (hue, 0.6f, 1.0f, 1.0f));
            addAndMakeVisible (*c);
            return c;
        }
        return {};
    };

    rootBox    = makeChoice ("root",    "ROOT",    0.0f);
    scaleBox   = makeChoice ("scale",   "SCALE",   0.15f);
    chordBox   = makeChoice ("chord",   "CHORD",   0.35f);
    routingBox = makeChoice ("routing", "ROUTING", 0.6f);

    if (apvts.getParameter ("midiFollow") != nullptr)
    {
        midiFollow = std::make_unique<ToggleControl> (apvts, "midiFollow", "MIDI FOLLOW",
                                                      juce::Colour::fromHSV (0.8f, 0.6f, 1.0f, 1.0f), false);
        addAndMakeVisible (*midiFollow);
    }

    pitchPanel = std::make_unique<FxPanel> (apvts, "PITCH MAP", 0.95f, "pmOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "pmAmount", "AMOUNT" }, { "pmPurity", "PURITY" }, { "pmShift", "SHIFT" },
                                { "pmSmear", "SMEAR" }, { "pmMix", "MIX" } }, 3);

    resPanel = std::make_unique<FxPanel> (apvts, "RESONATOR", 0.07f, "resOn", "resMode", std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "resDecay", "DECAY" }, { "resDamp", "DAMP" }, { "resOct", "OCTAVE" },
                                { "resStack", "STACK" }, { "resDetune", "DETUNE" }, { "resMix", "MIX" } }, 3);

    shiftPanel = std::make_unique<FxPanel> (apvts, "FREQ SHIFT", 0.15f, "fsOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "fsShift", "SHIFT" }, { "fsSpread", "SPREAD" }, { "fsFeedback", "FEEDBACK" },
                                { "fsLfoRate", "LFO RATE" }, { "fsLfoDepth", "LFO DEPTH" }, { "fsMix", "MIX" } }, 3);

    chorusPanel = std::make_unique<FxPanel> (apvts, "CHORUS", 0.36f, "chOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "chRate", "RATE" }, { "chDepth", "DEPTH" }, { "chMix", "MIX" } }, 3);

    reverbPanel = std::make_unique<FxPanel> (apvts, "REVERB", 0.50f, "rvOn", nullptr,
        std::vector<ToggleSpec> { { "rvFreeze", "FREEZE" } },
        std::vector<KnobSpec> { { "rvSize", "SIZE" }, { "rvDamp", "DAMP" }, { "rvMix", "MIX" } }, 3);

    ottPanel = std::make_unique<FxPanel> (apvts, "OTT", 0.62f, "ottOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "ottDepth", "DEPTH" }, { "ottTime", "TIME" }, { "ottUp", "UPWARD" },
                                { "ottDown", "DOWNWARD" }, { "ottLow", "LOW" }, { "ottMid", "MID" },
                                { "ottHigh", "HIGH" } }, 4);

    ioPanel = std::make_unique<FxPanel> (apvts, "IN / OUT", 0.78f, nullptr, nullptr,
        std::vector<ToggleSpec> { { "subOn", "SUB KEEP" }, { "limit", "LIMITER" } },
        std::vector<KnobSpec> { { "drive", "DRIVE" }, { "subFreq", "SUB FREQ" }, { "width", "WIDTH" },
                                { "mix", "MIX" }, { "outGain", "OUTPUT" } }, 5);

    for (auto* panel : { pitchPanel.get(), resPanel.get(), shiftPanel.get(), chorusPanel.get(),
                         reverbPanel.get(), ottPanel.get(), ioPanel.get() })
        addAndMakeVisible (panel);

    sendLookAndFeelChange();
    setResizable (false, false);
    setSize (1180, 740);
    startTimerHz (30);
}

ColorForgeAudioProcessorEditor::~ColorForgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ColorForgeAudioProcessorEditor::timerCallback()
{
    if (analyzer != nullptr)
        analyzer->update();
}

void ColorForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace cfui;
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e1322), 0.0f, 0.0f,
                                             juce::Colour (0xff04060b), 0.0f, bounds.getBottom(), false));
    g.fillAll();

    // faint diagonal hue glow
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x0d6a4cff), bounds.getRight(), 0.0f,
                                             juce::Colour (0x00000000), bounds.getRight() * 0.5f, bounds.getBottom() * 0.6f, true));
    g.fillAll();

    // rainbow title
    const juce::Rectangle<float> titleArea (16.0f, 8.0f, 260.0f, 34.0f);
    juce::ColourGradient rainbow (juce::Colour::fromHSV (0.0f, 0.8f, 1.0f, 1.0f), titleArea.getX(), 0.0f,
                                  juce::Colour::fromHSV (0.8f, 0.8f, 1.0f, 1.0f), titleArea.getX() + 230.0f, 0.0f, false);
    for (int i = 1; i < 8; ++i)
        rainbow.addColour ((double) i / 8.0, juce::Colour::fromHSV (0.8f * (float) i / 8.0f, 0.8f, 1.0f, 1.0f));
    g.setGradientFill (rainbow);
    g.setFont (juce::Font (juce::FontOptions().withHeight (32.0f).withStyle ("Bold")));
    g.drawText ("COLORFORGE", titleArea, juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (0xff8a91a6));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("COLOR BASS MULTI-FX", juce::Rectangle<float> (18.0f, 42.0f, 260.0f, 14.0f),
                juce::Justification::centredLeft, false);
}

void ColorForgeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 0);

    // header
    auto header = r.removeFromTop (64).withTrimmedTop (12).withTrimmedBottom (6);
    header.removeFromLeft (280);

    auto place = [&header] (juce::Component* c, int w)
    {
        auto slot = header.removeFromLeft (w);
        header.removeFromLeft (14);
        if (c != nullptr)
            c->setBounds (slot);
    };

    place (rootBox.get(), 70);
    place (scaleBox.get(), 160);
    place (chordBox.get(), 120);
    place (routingBox.get(), 180);
    if (midiFollow != nullptr)
        midiFollow->setBounds (header.removeFromLeft (120).withTrimmedTop (14));

    // analyzer
    r.removeFromTop (4);
    analyzer->setBounds (r.removeFromTop (150));
    r.removeFromTop (10);
    r.removeFromBottom (10);

    // panels: 4 x 2 grid
    const int gap = 10;
    const int rowH = (r.getHeight() - gap) / 2;
    const int colW = (r.getWidth() - 3 * gap) / 4;

    auto cell = [&] (int row, int col, int span) -> juce::Rectangle<int>
    {
        return { r.getX() + col * (colW + gap), r.getY() + row * (rowH + gap),
                 colW * span + gap * (span - 1), rowH };
    };

    pitchPanel->setBounds  (cell (0, 0, 1));
    resPanel->setBounds    (cell (0, 1, 1));
    shiftPanel->setBounds  (cell (0, 2, 1));
    chorusPanel->setBounds (cell (0, 3, 1));
    reverbPanel->setBounds (cell (1, 0, 1));
    ottPanel->setBounds    (cell (1, 1, 1));
    ioPanel->setBounds     (cell (1, 2, 2));
}
