#include "PluginEditor.h"

#include <cmath>

namespace cfui
{
namespace
{
    using namespace px;

    constexpr float U = 2.0f;   // one "pixel" of the 8-bit look, in screen points

    juce::String formatValue (double v)
    {
        const auto a = std::abs (v);
        if (a >= 1000.0) return juce::String (v / 1000.0, a >= 10000.0 ? 1 : 2) + "K";
        if (a >= 100.0)  return juce::String (v, 0);
        if (a >= 1.0 || a < 0.005) return juce::String (v, 1);
        return juce::String (v, 2);
    }

    float maxScaleOf (const juce::Component& c, float fallback)
    {
        const auto v = c.getProperties()["pxScale"];
        return v.isVoid() ? fallback : (float) (double) v;
    }

    /** A disc made of whole pixels, one rectangle per row. */
    void pixelDisc (juce::Graphics& g, juce::Point<float> centre, float radius, float u, juce::Colour c)
    {
        juce::RectangleList<float> rows;
        const int n = (int) std::ceil (radius / u);
        const float cx = std::round (centre.x), cy = std::round (centre.y);

        for (int iy = -n; iy < n; ++iy)
        {
            const float yc = ((float) iy + 0.5f) * u;
            const float half = std::sqrt (juce::jmax (0.0f, radius * radius - yc * yc));
            const int cells = (int) std::floor (half / u + 0.25f);
            if (cells > 0)
                rows.addWithoutMerging ({ cx - (float) cells * u, cy + (float) iy * u, (float) (2 * cells) * u, u });
        }

        g.setColour (c);
        g.fillRectList (rows);
    }

    /** Pixel line from a to b (squares of size u snapped to the grid). */
    void pixelLine (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float u, float thick, juce::Colour c)
    {
        juce::RectangleList<float> rects;
        const float len = a.getDistanceFrom (b);
        const int steps = juce::jmax (1, (int) (len / (u * 0.5f)));
        juce::Point<float> last { -1.0e9f, -1.0e9f };

        for (int i = 0; i <= steps; ++i)
        {
            const auto p = a + (b - a) * ((float) i / (float) steps);
            const juce::Point<float> q { std::floor (p.x / u) * u, std::floor (p.y / u) * u };
            if (q != last)
                rects.addWithoutMerging ({ q.x - (thick - u) * 0.5f, q.y - (thick - u) * 0.5f, thick, thick });
            last = q;
        }

        g.setColour (c);
        g.fillRectList (rects);
    }

    juce::Colour pitchClassColour (float freq)
    {
        const float midi = 69.0f + 12.0f * std::log2 (juce::jmax (1.0f, freq) / 440.0f);
        float pc = std::fmod (midi, 12.0f);
        if (pc < 0.0f)
            pc += 12.0f;
        return juce::Colour::fromHSV (std::floor (pc) / 12.0f, 0.65f, 1.0f, 1.0f);
    }

    /** Sunken screen used behind graphs. */
    void screen (juce::Graphics& g, juce::Rectangle<float> r)
    {
        bevel (g, r, U, pal::bgAlt, true);
    }

    void dottedHLine (juce::Graphics& g, float y, float x0, float x1, juce::Colour c)
    {
        juce::RectangleList<float> rects;
        y = std::floor (y / U) * U;
        for (float x = std::ceil (x0 / (2 * U)) * 2 * U; x < x1; x += 3 * U)
            rects.addWithoutMerging ({ x, y, U, U });
        g.setColour (c);
        g.fillRectList (rects);
    }

    void dottedVLine (juce::Graphics& g, float x, float y0, float y1, juce::Colour c)
    {
        juce::RectangleList<float> rects;
        x = std::floor (x / U) * U;
        for (float y = std::ceil (y0 / (2 * U)) * 2 * U; y < y1; y += 3 * U)
            rects.addWithoutMerging ({ x, y, U, U });
        g.setColour (c);
        g.fillRectList (rects);
    }

    juce::String freqText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + "K" : juce::String (juce::roundToInt (f));
    }
}

//==============================================================================
PixelLookAndFeel::PixelLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, pal::bg);
    setColour (juce::Label::textColourId, pal::text);
    setColour (juce::ComboBox::backgroundColourId, pal::bgAlt);
    setColour (juce::ComboBox::textColourId, pal::text);
    setColour (juce::ComboBox::outlineColourId, pal::edgeMid);
    setColour (juce::ComboBox::arrowColourId, pal::dim);
    setColour (juce::PopupMenu::backgroundColourId, pal::panel);
    setColour (juce::PopupMenu::textColourId, pal::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::edgeMid);
    setColour (juce::PopupMenu::highlightedTextColourId, pal::text);
    setColour (juce::Slider::textBoxTextColourId, pal::dim);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, pal::edgeMid);
    setColour (juce::TextEditor::backgroundColourId, pal::bgAlt);
    setColour (juce::TextEditor::textColourId, pal::text);
    setColour (juce::TextEditor::outlineColourId, pal::edgeMid);
    setColour (juce::TextEditor::focusedOutlineColourId, pal::edgeLight);
    setColour (juce::TextButton::buttonColourId, pal::panelHi);
    setColour (juce::TextButton::buttonOnColourId, pal::edgeMid);
    setColour (juce::TextButton::textColourOffId, pal::dim);
    setColour (juce::TextButton::textColourOnId, pal::text);
    setColour (juce::TooltipWindow::backgroundColourId, pal::panel);
    setColour (juce::TooltipWindow::textColourId, pal::text);
}

void PixelLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const float size = (float) juce::jmin (width, height);
    if (size < 16.0f)
        return;

    const auto centre = juce::Rectangle<int> (x, y, width, height).toFloat().getCentre();
    const float radius = size * 0.5f - U;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool enabled = slider.isEnabled();

    // value pips around the dial (bipolar parameters light up from zero)
    float fromAngle = startAngle;
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        fromAngle = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);

    const int numPips = 11;
    const float pipR = radius - U * 1.5f;
    for (int i = 0; i < numPips; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (numPips - 1);
        const float lo = juce::jmin (fromAngle, angle) - 0.01f, hi = juce::jmax (fromAngle, angle) + 0.01f;
        const bool lit = a >= lo && a <= hi;
        const float px = std::round ((centre.x + pipR * std::sin (a)) / U) * U;
        const float py = std::round ((centre.y - pipR * std::cos (a)) / U) * U;
        g.setColour (lit ? (enabled ? accent : pal::dim) : pal::edgeDark.brighter (0.4f));
        g.fillRect (px - U, py - U, 2 * U, 2 * U);
    }

    // body: outline, fill, highlight
    const float bodyR = radius - U * 4.0f;
    if (bodyR < 4.0f)
        return;

    pixelDisc (g, centre, bodyR, U, pal::edgeDark);
    pixelDisc (g, centre, bodyR - U, U, slider.isMouseOverOrDragging() && enabled ? pal::panelHi.brighter (0.15f) : pal::panelHi);
    pixelDisc (g, centre.translated (-U, -U), bodyR * 0.55f, U, pal::panelHi.brighter (0.25f));

    // pointer
    const juce::Point<float> tip { centre.x + (bodyR - 2 * U) * std::sin (angle), centre.y - (bodyR - 2 * U) * std::cos (angle) };
    const juce::Point<float> base { centre.x + bodyR * 0.25f * std::sin (angle), centre.y - bodyR * 0.25f * std::cos (angle) };
    pixelLine (g, base, tip, U, 2 * U, enabled ? accent.brighter (0.3f) : pal::dim);
}

void PixelLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();

    if (b.getProperties().contains ("power"))
    {
        auto box = r.withSizeKeepingCentre (18.0f, 18.0f);
        bevel (g, box, U, pal::panelHi, down);
        auto led = box.reduced (3 * U);
        g.setColour (on ? accent : pal::edgeDark);
        g.fillRect (snap (led));
        if (on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (snap (led).withSize (U, U));
        }
        else if (highlighted)
        {
            g.setColour (accent.withAlpha (0.4f));
            g.fillRect (snap (led));
        }
        return;
    }

    bevel (g, r, U, on ? accent.darker (0.35f) : (highlighted ? pal::panelHi.brighter (0.1f) : pal::panelHi), down || on);

    auto inner = r.reduced (3 * U);
    auto led = inner.removeFromLeft (inner.getHeight()).reduced (U);
    led = led.withSizeKeepingCentre (3 * U, 3 * U);
    g.setColour (on ? accent.brighter (0.6f) : pal::edgeDark);
    g.fillRect (snap (led));

    drawTextIn (g, b.getButtonText(), inner, juce::Justification::centred, maxScaleOf (b, 2.0f),
                on ? pal::text : pal::dim);
}

void PixelLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minPos, float maxPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
        return;
    }

    auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    auto track = r.withSizeKeepingCentre (r.getWidth(), 4 * U);
    bevel (g, track, U, pal::bgAlt, true);
    g.setColour (pal::cyan);
    g.fillRect (snap (track.reduced (U).withRight (sliderPos)));

    auto thumb = juce::Rectangle<float> (5 * U, 8 * U).withCentre ({ std::round (sliderPos), r.getCentreY() });
    bevel (g, thumb, U, slider.isMouseOverOrDragging() ? pal::edgeLight : pal::dim, false);
}

void PixelLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                             bool highlighted, bool down)
{
    const bool on = b.getToggleState();
    auto base = b.findColour (on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    if (highlighted && ! on)
        base = base.brighter (0.15f);
    bevel (g, b.getLocalBounds().toFloat(), U, base, down);
}

void PixelLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (3 * U);
    if (down)
        r.translate (U, U);
    drawTextIn (g, b.getButtonText(), r, juce::Justification::centred, maxScaleOf (b, 2.0f),
                b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId),
                b.getToggleState());
}

void PixelLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool down,
                                     int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    bevel (g, r, U, box.isMouseOver (true) ? pal::bgAlt.brighter (0.2f) : pal::bgAlt, ! down);

    juce::String arrow;
    arrow << (juce::juce_wchar) 3;
    drawText (g, arrow, r.getRight() - 16.0f, r.getCentreY() - 7.0f, 2.0f,
              box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 1.0f : 0.4f));
}

void PixelLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 2, juce::jmax (0, box.getWidth() - 26), juce::jmax (0, box.getHeight() - 4));
    label.getProperties().set ("pxScale", maxScaleOf (box, 2.0f));
}

void PixelLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (label.isBeingEdited())
        return;

    const float alpha = label.isEnabled() ? 1.0f : 0.5f;
    auto area = label.getLocalBounds().toFloat().reduced ((float) label.getBorderSize().getLeft(), 0.0f);
    drawTextIn (g, label.getText(), area, label.getJustificationType(), maxScaleOf (label, 2.0f),
                label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
}

void PixelLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (pal::edgeDark);
    g.setColour (pal::panel);
    g.fillRect (juce::Rectangle<float> ((float) width, (float) height).reduced (U));
}

void PixelLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool,
                                          const juce::String& text, const juce::String&,
                                          const juce::Drawable*, const juce::Colour*)
{
    auto r = area.toFloat();
    if (isSeparator)
    {
        dottedHLine (g, r.getCentreY(), r.getX() + 6, r.getRight() - 6, pal::dim);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (pal::edgeMid);
        g.fillRect (r.reduced (U, 0.0f));
    }

    auto textArea = r.reduced (8.0f, 0.0f);
    auto tick = textArea.removeFromLeft (14.0f);
    if (isTicked)
    {
        juce::String arrow;
        arrow << (juce::juce_wchar) 4;
        drawTextIn (g, arrow, tick, juce::Justification::centredLeft, 2.0f, pal::yellow);
    }
    drawTextIn (g, text, textArea, juce::Justification::centredLeft, 2.0f,
                isActive ? pal::text : pal::dim);
}

void PixelLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                                  int& idealWidth, int& idealHeight)
{
    idealHeight = isSeparator ? 10 : 22;
    idealWidth = (int) textWidth (text, 2.0f) + 40;
}

juce::Label* PixelLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->getProperties().set ("pxScale", 1.5f);
    l->setColour (juce::Label::outlineWhenEditingColourId, pal::edgeMid);
    l->setColour (juce::Label::backgroundWhenEditingColourId, pal::bgAlt);
    l->setColour (juce::Label::textWhenEditingColourId, pal::text);
    return l;
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, juce::RangedAudioParameter& param,
            const juce::String& paramID, const juce::String& shortName, juce::Colour hue)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 14);
    slider.setColour (juce::Slider::rotarySliderFillColourId, hue);
    slider.setColour (juce::Slider::thumbColourId, hue);
    slider.setTooltip (param.getName (64));
    addAndMakeVisible (slider);

    caption.setText (shortName, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, pal::text);
    caption.getProperties().set ("pxScale", 2.0f);
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);

    // compact value display (the attachment installed parameter-based text functions; replace display only)
    const juce::String unit = param.getLabel().toUpperCase();
    const bool isInt = dynamic_cast<juce::AudioParameterInt*> (&param) != nullptr;
    slider.textFromValueFunction = [unit, isInt] (double v)
    {
        const juce::String s = isInt ? juce::String (juce::roundToInt (v)) : formatValue (v);
        if (unit.isEmpty())
            return s;
        return unit.startsWithChar (':') ? s + unit : s + " " + unit;
    };
    slider.updateText();

    slider.setDoubleClickReturnValue (true, (double) param.convertFrom0to1 (param.getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    caption.setBounds (r.removeFromTop (16));
    r.removeFromBottom (juce::jlimit (0, 6, (r.getHeight() - 60) / 4));
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (r.getWidth(), 96), 14);
    slider.setBounds (r);
}

//==============================================================================
ChoiceControl::ChoiceControl (juce::AudioProcessorValueTreeState& apvts, juce::RangedAudioParameter& param,
                              const juce::String& paramID, const juce::String& captionText, juce::Colour accent)
{
    caption.setText (captionText, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, accent);
    caption.setJustificationType (juce::Justification::centredLeft);
    caption.getProperties().set ("pxScale", 1.5f);
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&param))
    {
        int itemId = 1;
        for (const auto& c : choice->choices)
            box.addItem (c, itemId++);
    }
    box.setTooltip (param.getName (64));
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
    if (auto* prm = apvts.getParameter (paramID))
        button.setTooltip (isPowerSwitch ? prm->getName (64) + " (bypass / enable)" : prm->getName (64));
    if (isPowerSwitch)
        button.getProperties().set ("power", true);
    addAndMakeVisible (button);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramID, button);
}

void ToggleControl::resized()
{
    button.setBounds (getLocalBounds());
}

//==============================================================================
FxPanel::FxPanel (juce::AudioProcessorValueTreeState& apvts, const juce::String& titleText, juce::Colour accent,
                  const char* powerID, const char* comboID,
                  std::vector<ToggleSpec> toggleSpecs, std::vector<KnobSpec> knobSpecs, int knobColumns,
                  int displayH)
    : title (titleText), colour (accent), columns (juce::jmax (1, knobColumns)), displayHeight (displayH)
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
    const float alpha = on ? 1.0f : 0.45f;
    for (auto* k : knobs)
        k->setAlpha (alpha);
    for (auto* t : toggles)
        t->setAlpha (alpha);
    if (combo != nullptr)
        combo->setAlpha (alpha);
}

void FxPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    windowFrame (g, r, U, colour.withMultipliedSaturation (0.8f).darker (0.2f), pal::panel);

    // title tab
    const float maxTitle = r.getWidth() - (power != nullptr ? 40.0f : 16.0f);
    const float sc = fitScale (title, maxTitle - 12.0f, 2.0f);
    const float tw = textWidth (title, sc) + 12.0f;
    auto tab = juce::Rectangle<float> (3 * U, 3 * U, tw, 20.0f);
    g.setColour (colour);
    g.fillRect (snap (tab));
    g.setColour (colour.darker (0.5f));
    g.fillRect (snap (tab.withTop (tab.getBottom() - U)));
    drawTextIn (g, title, tab.reduced (6.0f, 0.0f), juce::Justification::centredLeft, sc, pal::edgeDark);
}

void FxPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto header = r.removeFromTop (20);

    if (power != nullptr)
        power->setBounds (header.removeFromRight (22).withSizeKeepingCentre (20, 20));

    r.removeFromTop (6);

    if (displayHeight > 0)
    {
        displayArea = r.removeFromTop (displayHeight);
        r.removeFromTop (6);
    }

    if (combo != nullptr || ! toggles.isEmpty())
    {
        auto extras = r.removeFromTop (24);
        r.removeFromTop (4);

        const int n = (combo != nullptr ? 1 : 0) + toggles.size();
        const int w = juce::jmin (150, (extras.getWidth() - 6 * (n - 1)) / juce::jmax (1, n));

        if (combo != nullptr)
        {
            combo->setBounds (extras.removeFromLeft (w));
            extras.removeFromLeft (6);
        }

        for (auto* t : toggles)
        {
            t->setBounds (extras.removeFromLeft (w));
            extras.removeFromLeft (6);
        }
    }

    const int n = knobs.size();
    if (n == 0)
        return;

    const int cols = juce::jmin (columns, n);
    const int rows = (n + cols - 1) / cols;
    const int cellW = r.getWidth() / cols;
    const int cellH = juce::jmin (r.getHeight() / rows, 110);
    r = r.withSizeKeepingCentre (r.getWidth(), cellH * rows);

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
void SpectrumSource::update (ColorForgeAudioProcessor& processor)
{
    double sr = processor.getScopeSampleRate();
    sampleRate = sr > 1000.0 ? sr : 44100.0;

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    processor.getScopeSnapshot (fftData.data(), fftSize);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 4.0f / (float) fftSize;   // Hann coherent gain 0.5, single-sided
    for (size_t b = 0; b < binDb.size(); ++b)
    {
        const float db = juce::jlimit (minDb, maxDb, juce::Decibels::gainToDecibels (fftData[b] * norm, minDb - 10.0f));
        float& level = binDb[b];
        if (db > level) level += (db - level) * 0.75f;                               // fast attack
        else            level = juce::jmax (db, level - 1.2f - (level - db) * 0.04f); // slow release
    }
}

float SpectrumSource::levelDb (float f0, float f1) const noexcept
{
    const float binPerHz = (float) fftSize / (float) sampleRate;
    const int maxBin = (int) binDb.size() - 1;
    const float lo = f0 * binPerHz, hi = f1 * binPerHz;

    if (hi - lo < 1.0f)
    {
        const float c = juce::jlimit (0.0f, (float) maxBin - 1.0f, 0.5f * (lo + hi));
        const int b = (int) c;
        const float frac = c - (float) b;
        return binDb[(size_t) b] * (1.0f - frac) + binDb[(size_t) b + 1] * frac;
    }

    float m = minDb;
    for (int b = juce::jlimit (0, maxBin, (int) lo); b <= juce::jlimit (0, maxBin, (int) std::ceil (hi)); ++b)
        m = juce::jmax (m, binDb[(size_t) b]);
    return m;
}

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (ColorForgeAudioProcessor& p, SpectrumSource& s)
    : processor (p), source (s)
{
    setInterceptsMouseClicks (false, false);
}

void SpectrumAnalyzer::resized()
{
    auto r = getLocalBounds();
    noteArea = r.removeFromRight (190);
    r.removeFromRight (8);
    specArea = r.reduced (8).withTrimmedLeft (26).withTrimmedBottom (14);
}

void SpectrumAnalyzer::update()
{
    noteMask = processor.activeNoteMask.load (std::memory_order_relaxed);
    repaint();
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    auto frame = getLocalBounds().withTrimmedRight (noteArea.getWidth() + 8).toFloat();
    screen (g, frame);

    const auto area = specArea.toFloat();
    const float minDb = SpectrumSource::minDb, maxDb = SpectrumSource::maxDb;

    for (float db = -80.0f; db < 0.0f; db += 20.0f)
    {
        const float y = juce::jmap (db, minDb, maxDb, area.getBottom(), area.getY());
        dottedHLine (g, y, area.getX(), area.getRight(), pal::edgeMid.withAlpha (0.5f));
        drawTextIn (g, juce::String ((int) db), { frame.getX() + 4.0f, y - 5.0f, 24.0f, 10.0f },
                    juce::Justification::centredRight, 1.0f, pal::dim);
    }

    auto xForFreq = [&] (float f) { return area.getX() + std::log (f / minFreq) / std::log (maxFreq / minFreq) * area.getWidth(); };

    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        dottedVLine (g, x, area.getY(), area.getBottom(), pal::edgeMid.withAlpha (0.4f));
        drawTextIn (g, freqText (f), { x - 20.0f, area.getBottom() + 3.0f, 40.0f, 10.0f },
                    juce::Justification::centred, 1.0f, pal::dim);
    }

    // block columns
    const float colW = 3 * U, blockH = 2 * U;
    const int numCols = (int) (area.getWidth() / colW);
    const float ratio = maxFreq / minFreq;
    juce::RectangleList<float> rects;

    for (int c = 0; c < numCols; ++c)
    {
        const float f0 = minFreq * std::pow (ratio, (float) c / (float) numCols);
        const float f1 = minFreq * std::pow (ratio, (float) (c + 1) / (float) numCols);
        const float level = source.levelDb (f0, f1);
        const float h = juce::jmap (level, minDb, maxDb, 0.0f, area.getHeight());
        const int blocks = (int) (h / (blockH + U));
        if (blocks <= 0)
            continue;

        const float x = area.getX() + (float) c * colW;
        const auto col = pitchClassColour (std::sqrt (f0 * f1));

        juce::RectangleList<float> colRects;
        for (int b = 0; b < blocks; ++b)
            colRects.addWithoutMerging ({ x, area.getBottom() - (float) (b + 1) * (blockH + U), colW - U, blockH });

        g.setColour (col.withAlpha (0.75f));
        g.fillRectList (colRects);
        g.setColour (col.brighter (0.5f));
        g.fillRect (x, area.getBottom() - (float) blocks * (blockH + U), colW - U, blockH);
    }

    // note grid
    auto notes = noteArea.toFloat();
    screen (g, notes);
    auto inner = noteArea.reduced (10, 8);
    drawTextIn (g, "ACTIVE NOTES", inner.removeFromTop (14).toFloat(), juce::Justification::centredLeft, 1.5f, pal::dim);
    inner.removeFromTop (6);

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float cw = (float) inner.getWidth() / 4.0f, ch = (float) inner.getHeight() / 3.0f;

    for (int i = 0; i < 12; ++i)
    {
        const bool active = (noteMask & (1 << i)) != 0;
        const auto hue = juce::Colour::fromHSV ((float) i / 12.0f, 0.65f, 1.0f, 1.0f);
        auto cell = juce::Rectangle<float> ((float) inner.getX() + (float) (i % 4) * cw,
                                            (float) inner.getY() + (float) (i / 4) * ch, cw, ch).reduced (3.0f);
        if (active)
            bevel (g, cell, U, hue, false);
        else
            fillBox (g, cell, U, pal::panel);

        drawTextIn (g, noteNames[i], cell, juce::Justification::centred, 2.0f,
                    active ? pal::edgeDark : hue.withAlpha (0.55f));
    }
}

//==============================================================================
EqGraph::EqGraph (ColorForgeAudioProcessor& p, SpectrumSource& s)
    : processor (p), source (s)
{
    setRepaintsOnMouseActivity (false);
}

void EqGraph::resized()
{
    plot = getLocalBounds().toFloat().reduced (10.0f).withTrimmedLeft (22.0f).withTrimmedBottom (12.0f);
    curveDb.assign ((size_t) juce::jmax (1, (int) (plot.getWidth() / U)), 0.0f);
}

float EqGraph::xForFreq (float f) const noexcept
{
    return plot.getX() + std::log (f / minFreq) / std::log (maxFreq / minFreq) * plot.getWidth();
}

float EqGraph::freqForX (float x) const noexcept
{
    return minFreq * std::pow (maxFreq / minFreq, juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth()));
}

float EqGraph::yForDb (float db) const noexcept
{
    return juce::jmap (db, -rangeDb, rangeDb, plot.getBottom(), plot.getY());
}

float EqGraph::dbForY (float y) const noexcept
{
    return juce::jlimit (-rangeDb, rangeDb, juce::jmap (y, plot.getBottom(), plot.getY(), -rangeDb, rangeDb));
}

float EqGraph::paramValue (const juce::String& id) const
{
    if (auto* v = processor.apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void EqGraph::setParam (const juce::String& id, float plain)
{
    if (auto* prm = processor.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

juce::Point<float> EqGraph::handlePos (int band) const
{
    const float f = paramValue (cf::eqId (band, "Freq"));
    const float db = bandHasGain (band) ? paramValue (cf::eqId (band, "Gain")) : 0.0f;
    return { xForFreq (f), yForDb (db) };
}

int EqGraph::hitBand (juce::Point<float> pos) const
{
    int best = -1;
    float bestD = 12.0f;
    for (int b = 0; b < cf::kEqBands; ++b)
    {
        const float d = handlePos (b).getDistanceFrom (pos);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqGraph::update()
{
    const double sr = source.getSampleRate();
    std::array<cf::ParametricEQ::Design, cf::kEqBands> designs;
    std::array<bool, cf::kEqBands> on {};

    for (int b = 0; b < cf::kEqBands; ++b)
    {
        on[(size_t) b] = paramValue (cf::eqId (b, "On")) > 0.5f;
        const int slope = (b == 0 || b == cf::kEqBands - 1) ? (int) paramValue (cf::eqId (b, "Slope")) : 0;
        designs[(size_t) b] = cf::ParametricEQ::design (cf::ParametricEQ::typeOfBand (b),
                                                        paramValue (cf::eqId (b, "Freq")),
                                                        paramValue (cf::eqId (b, "Gain")),
                                                        paramValue (cf::eqId (b, "Q")), slope, sr);
    }

    const bool eqOn = paramValue ("eqOn") > 0.5f;
    for (size_t i = 0; i < curveDb.size(); ++i)
    {
        const float f = freqForX (plot.getX() + ((float) i + 0.5f) * U);
        double mag = 1.0;
        if (eqOn)
            for (int b = 0; b < cf::kEqBands; ++b)
                if (on[(size_t) b])
                    mag *= designs[(size_t) b].magnitude (f, sr);
        curveDb[i] = (float) (20.0 * std::log10 (juce::jmax (1.0e-6, mag)));
    }

    repaint();
}

void EqGraph::paint (juce::Graphics& g)
{
    screen (g, getLocalBounds().toFloat());

    // grid
    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        const float y = yForDb (db);
        dottedHLine (g, y, plot.getX(), plot.getRight(), db == 0.0f ? pal::edgeLight.withAlpha (0.6f) : pal::edgeMid.withAlpha (0.45f));
        drawTextIn (g, (db > 0 ? "+" : "") + juce::String ((int) db), { plot.getX() - 26.0f, y - 5.0f, 22.0f, 10.0f },
                    juce::Justification::centredRight, 1.0f, pal::dim);
    }
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f);
        dottedVLine (g, x, plot.getY(), plot.getBottom(), pal::edgeMid.withAlpha (0.35f));
        drawTextIn (g, freqText (f), { x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 10.0f },
                    juce::Justification::centred, 1.0f, pal::dim);
    }

    // live spectrum behind the curve
    {
        juce::RectangleList<float> rects;
        const float colW = 2 * U;
        const int numCols = (int) (plot.getWidth() / colW);
        for (int c = 0; c < numCols; ++c)
        {
            const float f0 = freqForX (plot.getX() + (float) c * colW), f1 = freqForX (plot.getX() + (float) (c + 1) * colW);
            const float level = source.levelDb (f0, f1);
            const float h = std::floor (juce::jmap (level, SpectrumSource::minDb, SpectrumSource::maxDb, 0.0f, plot.getHeight()) / U) * U;
            if (h > 0.0f)
                rects.addWithoutMerging ({ plot.getX() + (float) c * colW, plot.getBottom() - h, colW - U, h });
        }
        g.setColour (pal::edgeMid.withAlpha (0.35f));
        g.fillRectList (rects);
    }

    // curve: dithered fill to 0 dB, then the line as stacked pixels
    const bool eqOn = paramValue ("eqOn") > 0.5f;
    const auto curveColour = eqOn ? pal::yellow : pal::dim;
    {
        juce::RectangleList<float> fill, line;
        const float y0 = std::floor (yForDb (0.0f) / U) * U;
        float prevY = -1.0f;

        for (size_t i = 0; i < curveDb.size(); ++i)
        {
            const float x = plot.getX() + (float) i * U;
            const float y = std::floor (juce::jlimit (plot.getY(), plot.getBottom() - U, yForDb (curveDb[i])) / U) * U;

            const float top = juce::jmin (y, y0), bottom = juce::jmax (y, y0);
            for (float yy = top; yy < bottom; yy += U)
                if ((((int) (yy / U) + (int) i) & 1) == 0)
                    fill.addWithoutMerging ({ x, yy, U, U });

            const float a = prevY < 0.0f ? y : juce::jmin (prevY, y);
            const float b = prevY < 0.0f ? y : juce::jmax (prevY, y);
            line.addWithoutMerging ({ x, a, U, b - a + U });
            prevY = y;
        }

        g.setColour (curveColour.withAlpha (0.28f));
        g.fillRectList (fill);
        g.setColour (pal::edgeDark);
        g.fillRectList ([&] { auto l = line; l.offsetAll (0.0f, U); return l; }());
        g.setColour (curveColour);
        g.fillRectList (line);
    }

    // handles
    static const char* bandNames[] = { "LOW CUT", "LOW SHELF", "BELL 1", "BELL 2", "BELL 3", "BELL 4", "HIGH SHELF", "HIGH CUT" };
    for (int b = 0; b < cf::kEqBands; ++b)
    {
        const auto pos = handlePos (b);
        const bool on = paramValue (cf::eqId (b, "On")) > 0.5f;
        const bool active = (b == dragBand || b == hoverBand);
        const float size = active ? 18.0f : 14.0f;
        auto box = juce::Rectangle<float> (size, size).withCentre ({ std::round (pos.x), std::round (pos.y) });
        const auto c = pal::accent (b);

        if (on)
            bevel (g, box, U, c, false);
        else
            windowFrame (g, box, U * 0.5f, c, pal::bgAlt);

        drawTextIn (g, juce::String (b + 1), box, juce::Justification::centred, 1.0f, on ? pal::edgeDark : c);
    }

    // readout for the band under the mouse
    const int shown = dragBand >= 0 ? dragBand : hoverBand;
    if (shown >= 0)
    {
        juce::String txt = juce::String (bandNames[shown]) + "  " + freqText (paramValue (cf::eqId (shown, "Freq"))) + " HZ";
        if (bandHasGain (shown))
        {
            const float gdb = paramValue (cf::eqId (shown, "Gain"));
            txt << "  " << (gdb >= 0.0f ? "+" : "") << juce::String (gdb, 1) << " DB";
        }
        txt << "  Q " << juce::String (paramValue (cf::eqId (shown, "Q")), 2);

        auto box = juce::Rectangle<float> (textWidth (txt, 2.0f) + 20.0f, 26.0f).withPosition (plot.getX() + 8.0f, plot.getY() + 6.0f);
        windowFrame (g, box, U * 0.5f, pal::accent (shown), pal::panel);
        drawTextIn (g, txt, box.reduced (10.0f, 0.0f), juce::Justification::centredLeft, 2.0f, pal::text);
    }
    else
    {
        drawTextIn (g, "DRAG A DOT  \x04  WHEEL = Q  \x04  DOUBLE-CLICK = ON/OFF", plot.withHeight (18.0f).translated (8.0f, 6.0f),
                    juce::Justification::centredLeft, 1.5f, pal::dim);
    }
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitBand (e.position);
    if (h != hoverBand)
    {
        hoverBand = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragBand = hitBand (e.position);
    if (dragBand < 0)
        return;

    for (const char* suffix : { "Freq", "Gain" })
        if (auto* prm = processor.apvts.getParameter (cf::eqId (dragBand, suffix)))
            prm->beginChangeGesture();

    // grabbing a disabled band switches it on
    if (paramValue (cf::eqId (dragBand, "On")) < 0.5f)
        setParam (cf::eqId (dragBand, "On"), 1.0f);
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    setParam (cf::eqId (dragBand, "Freq"), freqForX (e.position.x));
    if (bandHasGain (dragBand))
        setParam (cf::eqId (dragBand, "Gain"), dbForY (e.position.y));
    update();
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
        for (const char* suffix : { "Freq", "Gain" })
            if (auto* prm = processor.apvts.getParameter (cf::eqId (dragBand, suffix)))
                prm->endChangeGesture();
    dragBand = -1;
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitBand (e.position);
    if (b >= 0)
        setParam (cf::eqId (b, "On"), paramValue (cf::eqId (b, "On")) > 0.5f ? 0.0f : 1.0f);
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = hitBand (e.position);
    if (b < 0)
        return;

    const auto id = cf::eqId (b, "Q");
    if (auto* prm = processor.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        setParam (id, paramValue (id) * std::pow (1.15f, w.deltaY * 8.0f));
        prm->endChangeGesture();
    }
    update();
}

//==============================================================================
CompDisplay::CompDisplay (ColorForgeAudioProcessor& p) : processor (p)
{
    history.fill (0.0f);
    setInterceptsMouseClicks (false, false);
}

void CompDisplay::update()
{
    history[(size_t) writePos] = processor.getCompGainReductionDb();
    writePos = (writePos + 1) % (int) history.size();
    repaint();
}

void CompDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto curveBox = r.removeFromLeft (r.getHeight());
    r.removeFromLeft (8.0f);

    // ---- transfer curve
    screen (g, curveBox);
    auto plot = curveBox.reduced (10.0f);
    auto raw = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };
    const float thr = raw ("cpThresh"), ratio = raw ("cpRatio"), knee = raw ("cpKnee");
    const bool on = raw ("cpOn") > 0.5f;

    auto xFor = [&] (float db) { return juce::jmap (db, -60.0f, 0.0f, plot.getX(), plot.getRight()); };
    auto yFor = [&] (float db) { return juce::jmap (db, -60.0f, 0.0f, plot.getBottom(), plot.getY()); };

    for (float db = -48.0f; db < 0.0f; db += 12.0f)
    {
        dottedHLine (g, yFor (db), plot.getX(), plot.getRight(), pal::edgeMid.withAlpha (0.4f));
        dottedVLine (g, xFor (db), plot.getY(), plot.getBottom(), pal::edgeMid.withAlpha (0.4f));
    }
    dither (g, juce::Rectangle<float>::leftTopRightBottom (xFor (thr), plot.getY(), plot.getRight(), plot.getBottom()),
            U, pal::orange.withAlpha (0.12f));

    juce::RectangleList<float> line;
    float prev = -1.0f;
    for (float x = plot.getX(); x < plot.getRight(); x += U)
    {
        const float inDb = juce::jmap (x, plot.getX(), plot.getRight(), -60.0f, 0.0f);
        const float outDb = (float) cf::Compressor::staticCurve (inDb, thr, ratio, knee);
        const float y = std::floor (juce::jlimit (plot.getY(), plot.getBottom() - U, yFor (outDb)) / U) * U;
        const float a = prev < 0 ? y : juce::jmin (prev, y), b = prev < 0 ? y : juce::jmax (prev, y);
        line.addWithoutMerging ({ std::floor (x / U) * U, a, U, b - a + U });
        prev = y;
    }
    g.setColour (on ? pal::orange : pal::dim);
    g.fillRectList (line);
    drawTextIn (g, "IN \x04 OUT", plot.removeFromBottom (12.0f), juce::Justification::centredRight, 1.0f, pal::dim);

    // ---- gain reduction history
    screen (g, r);
    auto hist = r.reduced (10.0f);
    auto label = hist.removeFromTop (16.0f);
    const float now = history[(size_t) ((writePos + (int) history.size() - 1) % (int) history.size())];
    drawTextIn (g, "GAIN REDUCTION", label, juce::Justification::centredLeft, 1.5f, pal::dim);
    drawTextIn (g, "-" + juce::String (now, 1) + " DB", label, juce::Justification::centredRight, 2.0f,
                now > 6.0f ? pal::red : pal::orange);

    const float colW = hist.getWidth() / (float) history.size();
    juce::RectangleList<float> bars;
    for (size_t i = 0; i < history.size(); ++i)
    {
        const float v = history[(i + (size_t) writePos) % history.size()];
        const float h = std::floor (juce::jlimit (0.0f, 1.0f, v / 18.0f) * hist.getHeight() / U) * U;
        if (h > 0.0f)
            bars.addWithoutMerging ({ hist.getX() + std::floor ((float) i * colW), hist.getY(), juce::jmax (U, std::floor (colW)), h });
    }
    g.setColour (pal::orange);
    g.fillRectList (bars);

    for (float db : { 6.0f, 12.0f })
        dottedHLine (g, hist.getY() + db / 18.0f * hist.getHeight(), hist.getX(), hist.getRight(), pal::edgeMid.withAlpha (0.6f));
}

//==============================================================================
LoudnessHistory::LoudnessHistory (ColorForgeAudioProcessor& p) : processor (p)
{
    history.fill (-70.0f);
    setInterceptsMouseClicks (false, false);
}

void LoudnessHistory::update()
{
    if (++tick < 3)   // 30 Hz timer -> 10 values per second
        return;
    tick = 0;
    history[(size_t) writePos] = processor.meter.shortTerm.load (std::memory_order_relaxed);
    writePos = (writePos + 1) % (int) history.size();
    if (isShowing())
        repaint();
}

void LoudnessHistory::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    windowFrame (g, r, U, pal::green.darker (0.3f), pal::panel);

    auto inner = r.reduced (16.0f, 12.0f);
    auto head = inner.removeFromTop (22.0f);
    drawTextIn (g, "LOUDNESS MAP  \x04  SHORT-TERM, LAST 60 S", head, juce::Justification::centredLeft, 2.0f, pal::text);

    const int idx = (int) processor.apvts.state.getProperty (ColorForgeAudioProcessor::kLufsTargetProp, 2);
    const float target = cf::kLoudnessTargets[juce::jlimit (0, cf::kNumLoudnessTargets - 1, idx)].lufs;

    inner.removeFromTop (6.0f);
    auto plotArea = inner.withTrimmedLeft (34.0f).withTrimmedBottom (4.0f);
    screen (g, plotArea);
    auto plot = plotArea.reduced (8.0f);

    const float lo = -40.0f, hi = 0.0f;
    auto yFor = [&] (float lufs) { return juce::jmap (juce::jlimit (lo, hi, lufs), lo, hi, plot.getBottom(), plot.getY()); };

    for (float v = -36.0f; v < 0.0f; v += 6.0f)
    {
        dottedHLine (g, yFor (v), plot.getX(), plot.getRight(), pal::edgeMid.withAlpha (0.35f));
        drawTextIn (g, juce::String ((int) v), { inner.getX(), yFor (v) - 5.0f, 30.0f, 10.0f },
                    juce::Justification::centredRight, 1.0f, pal::dim);
    }

    // target zone (+-1 LU)
    dither (g, juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yFor (target + 1.0f), plot.getRight(), yFor (target - 1.0f)),
            U, pal::green.withAlpha (0.35f));
    dottedHLine (g, yFor (target), plot.getX(), plot.getRight(), pal::green);

    const float colW = 2 * U;
    const int numCols = (int) (plot.getWidth() / colW);
    juce::RectangleList<float> under, zone, over;

    for (int c = 0; c < numCols; ++c)
    {
        const size_t hi2 = (size_t) ((float) (c + 1) / (float) numCols * (float) history.size());
        const size_t lo2 = (size_t) ((float) c / (float) numCols * (float) history.size());
        float v = -70.0f;
        for (size_t i = lo2; i < juce::jmax (lo2 + 1, hi2); ++i)
            v = juce::jmax (v, history[(i + (size_t) writePos) % history.size()]);
        if (v <= lo)
            continue;

        const float y = std::floor (yFor (v) / U) * U;
        const juce::Rectangle<float> cell { plot.getX() + (float) c * colW, y, colW - U, plot.getBottom() - y };
        (std::abs (v - target) <= 1.0f ? zone : (v > target ? over : under)).addWithoutMerging (cell);
    }

    g.setColour (pal::blue);   g.fillRectList (under);
    g.setColour (pal::green);  g.fillRectList (zone);
    g.setColour (pal::red);    g.fillRectList (over);
}

//==============================================================================
SignalPath::SignalPath (ColorForgeAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (false, false);
}

void SignalPath::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    windowFrame (g, r, U, pal::purple.darker (0.3f), pal::panel);
    auto inner = r.reduced (16.0f, 12.0f);
    drawTextIn (g, "SIGNAL PATH", inner.removeFromTop (22.0f), juce::Justification::centredLeft, 2.0f, pal::text);
    inner.removeFromTop (8.0f);

    auto on = [this] (const char* id) { return id == nullptr || processor.apvts.getRawParameterValue (id)->load() > 0.5f; };

    struct Node { const char* name; const char* id; juce::Colour c; };
    const Node nodes[] = {
        { "IN",     nullptr,   pal::cyan },   { "COLOR",  "colorOn", pal::pink },
        { "EQ",     "eqOn",    pal::yellow }, { "COMP",   "cpOn",    pal::orange },
        { "SAT",    "satOn",   pal::red },    { "STEREO", "imgOn",   pal::blue },
        { "LIMIT",  "limit",   pal::red },    { "OUT",    nullptr,   pal::green } };
    constexpr int n = (int) (sizeof (nodes) / sizeof (nodes[0]));

    // two rows, snaking like a level map
    const int perRow = 4;
    const float cellW = inner.getWidth() / (float) perRow;
    const float rowH = juce::jmin (90.0f, inner.getHeight() / 2.0f);
    std::array<juce::Point<float>, n> centres {};

    for (int i = 0; i < n; ++i)
    {
        const int row = i / perRow, col = row % 2 == 0 ? i % perRow : perRow - 1 - i % perRow;
        centres[(size_t) i] = { inner.getX() + cellW * ((float) col + 0.5f), inner.getY() + rowH * ((float) row + 0.5f) };
    }

    for (int i = 0; i + 1 < n; ++i)
    {
        const bool live = on (nodes[i].id) && on (nodes[i + 1].id);
        pixelLine (g, centres[(size_t) i], centres[(size_t) i + 1], U, 2 * U, live ? pal::edgeLight.withAlpha (0.7f) : pal::edgeMid.withAlpha (0.5f));
    }

    for (int i = 0; i < n; ++i)
    {
        const bool active = on (nodes[i].id);
        auto box = juce::Rectangle<float> (juce::jmin (cellW - 12.0f, 84.0f), 30.0f).withCentre (centres[(size_t) i]);
        if (active)
            bevel (g, box, U, nodes[i].c.darker (0.2f), false);
        else
            windowFrame (g, box, U * 0.5f, nodes[i].c.withAlpha (0.5f), pal::bgAlt);
        drawTextIn (g, nodes[i].name, box.reduced (4.0f, 0.0f), juce::Justification::centred, 2.0f,
                    active ? pal::edgeDark : pal::dim);
    }

    drawTextIn (g, "LIT = ON  \x04  DIM = BYPASSED", inner.removeFromBottom (14.0f), juce::Justification::centredLeft, 1.5f, pal::dim);
}

//==============================================================================
StatusPanel::StatusPanel (ColorForgeAudioProcessor& p) : processor (p)
{
    for (int i = 0; i < cf::kNumLoudnessTargets; ++i)
        targetBox.addItem (cf::kLoudnessTargets[i].name, i + 1);

    targetBox.setSelectedId ((int) processor.apvts.state.getProperty (ColorForgeAudioProcessor::kLufsTargetProp, 2) + 1,
                             juce::dontSendNotification);
    targetBox.getProperties().set ("pxScale", 1.5f);
    targetBox.setTooltip ("Loudness target for the quest (integrated / short-term LUFS)");
    targetBox.onChange = [this]
    {
        processor.apvts.state.setProperty (ColorForgeAudioProcessor::kLufsTargetProp, targetBox.getSelectedId() - 1, nullptr);
        zoneSeconds = 0.0;
    };
    addAndMakeVisible (targetBox);

    resetButton.getProperties().set ("pxScale", 1.5f);
    resetButton.setTooltip ("Reset the integrated loudness, true peak and quest timer");
    resetButton.onClick = [this]
    {
        processor.meter.resetRequested.store (true);
        zoneSeconds = 0.0;
    };
    addAndMakeVisible (resetButton);
}

float StatusPanel::targetLufs() const
{
    const int idx = (int) processor.apvts.state.getProperty (ColorForgeAudioProcessor::kLufsTargetProp, 2);
    return cf::kLoudnessTargets[juce::jlimit (0, cf::kNumLoudnessTargets - 1, idx)].lufs;
}

void StatusPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    questArea = r.removeFromBottom (210);
    metersArea = r;

    auto q = questArea.reduced (10).withTrimmedTop (30);
    auto row = q.removeFromTop (24);
    resetButton.setBounds (row.removeFromRight (64));
    row.removeFromRight (6);
    targetBox.setBounds (row);
}

void StatusPanel::update()
{
    auto& m = processor.meter;
    peakL = m.peakL.load (std::memory_order_relaxed);
    peakR = m.peakR.load (std::memory_order_relaxed);
    lufsM = m.momentary.load (std::memory_order_relaxed);
    lufsS = m.shortTerm.load (std::memory_order_relaxed);
    lufsI = m.integrated.load (std::memory_order_relaxed);
    truePeak = m.truePeakMaxDb.load (std::memory_order_relaxed);
    corr = m.correlation.load (std::memory_order_relaxed);
    compGr = processor.getCompGainReductionDb();
    limGr = processor.getLimiterGainReductionDb();

    // the quest timer only runs while the short-term loudness sits in the target zone
    const auto now = juce::Time::getMillisecondCounter();
    const double dt = lastTick == 0 ? 0.0 : juce::jlimit (0.0, 0.25, (double) (now - lastTick) * 0.001);
    lastTick = now;
    if (lufsS > cf::LoudnessMeter::kFloor + 0.5f && std::abs (lufsS - targetLufs()) <= 1.0f)
        zoneSeconds += dt;

    ++frame;
    repaint();
}

void StatusPanel::paint (juce::Graphics& g)
{
    windowFrame (g, getLocalBounds().toFloat(), U, pal::cyan.darker (0.3f), pal::panel);

    auto r = metersArea.toFloat();
    auto title = r.removeFromTop (22.0f);
    g.setColour (pal::cyan);
    g.fillRect (snap (title.withWidth (textWidth ("STATUS", 2.0f) + 12.0f)));
    drawTextIn (g, "STATUS", title.withTrimmedLeft (6.0f), juce::Justification::centredLeft, 2.0f, pal::edgeDark);
    r.removeFromTop (10.0f);

    auto label = [&g] (juce::Rectangle<float> a, const juce::String& t, juce::Colour c = pal::dim)
    {
        drawTextIn (g, t, a, juce::Justification::centredLeft, 1.5f, c);
    };
    auto value = [&g] (juce::Rectangle<float> a, const juce::String& t, juce::Colour c, float sc = 2.0f)
    {
        drawTextIn (g, t, a, juce::Justification::centredRight, sc, c);
    };
    auto dbText = [] (float db) { return db <= -99.0f ? juce::String ("-INF") : juce::String (db, 1); };
    auto lufsText = [] (float v) { return v <= cf::LoudnessMeter::kFloor + 0.05f ? juce::String ("--.-") : juce::String (v, 1); };

    // ---- peak meters
    label (r.removeFromTop (14.0f), "PEAK  DBFS");
    r.removeFromTop (4.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto row = r.removeFromTop (16.0f);
        const float v = ch == 0 ? peakL : peakR;
        drawTextIn (g, ch == 0 ? "L" : "R", row.removeFromLeft (14.0f), juce::Justification::centredLeft, 2.0f, pal::text);
        auto num = row.removeFromRight (46.0f);
        value (num, dbText (v), v > -0.1f ? pal::red : pal::text, 1.5f);
        row.removeFromRight (4.0f);
        segmentBar (g, row, U, juce::jmap (juce::jlimit (-60.0f, 0.0f, v), -60.0f, 0.0f, 0.0f, 1.0f),
                    pal::green, pal::yellow, pal::red, 0.75f, 0.95f);
        r.removeFromTop (4.0f);
    }
    r.removeFromTop (8.0f);

    // ---- loudness
    label (r.removeFromTop (14.0f), "LOUDNESS  LUFS");
    r.removeFromTop (4.0f);
    const float target = targetLufs();
    for (int i = 0; i < 3; ++i)
    {
        static const char* names[] = { "MOMENTARY", "SHORT-TERM", "INTEGRATED" };
        const float v = i == 0 ? lufsM : (i == 1 ? lufsS : lufsI);
        auto row = r.removeFromTop (20.0f);
        label (row, names[i], pal::text);
        const bool valid = v > cf::LoudnessMeter::kFloor + 0.05f;
        const auto c = ! valid ? pal::dim : (std::abs (v - target) <= 1.0f ? pal::green : (v > target ? pal::red : pal::cyan));
        value (row, lufsText (v), c, i == 2 ? 2.0f : 2.0f);
    }
    r.removeFromTop (6.0f);
    {
        auto row = r.removeFromTop (20.0f);
        label (row, "TRUE PEAK", pal::text);
        const float ceil = processor.apvts.getRawParameterValue ("lmCeiling")->load();
        value (row, dbText (truePeak) + " DBTP", truePeak > ceil + 0.05f ? pal::red : pal::text, 1.5f);
    }
    r.removeFromTop (10.0f);

    // ---- correlation
    label (r.removeFromTop (14.0f), "PHASE  -1 \x04 +1");
    r.removeFromTop (4.0f);
    {
        auto bar = snap (r.removeFromTop (14.0f));
        g.setColour (pal::edgeDark);
        g.fillRect (bar);
        auto in = bar.reduced (U);
        dither (g, in.withWidth (in.getWidth() * 0.5f), U, pal::red.withAlpha (0.25f));
        dither (g, in.withLeft (in.getCentreX()), U, pal::green.withAlpha (0.25f));
        g.setColour (pal::dim);
        g.fillRect (in.getCentreX() - U * 0.5f, in.getY(), U, in.getHeight());
        const float x = std::round (juce::jmap (corr, -1.0f, 1.0f, in.getX(), in.getRight() - 3 * U) / U) * U;
        g.setColour (corr < 0.0f ? pal::red : pal::green);
        g.fillRect (x, in.getY() - U, 3 * U, in.getHeight() + 2 * U);
    }
    r.removeFromTop (12.0f);

    // ---- gain reduction
    for (int i = 0; i < 2; ++i)
    {
        const float v = i == 0 ? compGr : limGr;
        auto row = r.removeFromTop (14.0f);
        label (row, i == 0 ? "COMP GR" : "LIMIT GR");
        value (row, v > 0.05f ? juce::String (-v, 1) + " DB" : juce::String ("0.0 DB"), v > 0.05f ? pal::orange : pal::dim, 1.5f);
        r.removeFromTop (4.0f);
        segmentBar (g, r.removeFromTop (12.0f), U, juce::jlimit (0.0f, 1.0f, v / 12.0f),
                    pal::orange, pal::orange, pal::red, 0.5f, 0.75f);
        r.removeFromTop (8.0f);
    }

    // ---- quest
    auto q = questArea.toFloat();
    windowFrame (g, q, U, pal::yellow.darker (0.2f), pal::bgAlt);
    auto qi = q.reduced (10.0f);
    {
        juce::String t;
        t << (juce::juce_wchar) 1 << " LOUDNESS QUEST " << (juce::juce_wchar) 1;
        drawTextIn (g, t, qi.removeFromTop (22.0f), juce::Justification::centred, 2.0f, pal::yellow, true);
    }
    qi.removeFromTop (36.0f);   // target box + reset button live here

    // lives: hearts lost by overshooting the ceiling / clipping (true peak max hold)
    const float ceil = processor.apvts.getRawParameterValue ("lmCeiling")->load();
    const int hearts = truePeak <= ceil + 0.05f ? 3 : (truePeak <= 0.0f ? 2 : (truePeak <= 1.0f ? 1 : 0));
    {
        auto row = qi.removeFromTop (18.0f);
        label (row, "LIVES", pal::text);
        juce::String h;
        for (int i = 0; i < 3; ++i)
            h << (juce::juce_wchar) 2 << " ";
        auto heartsArea = row.removeFromRight (textWidth (h.trimEnd(), 2.0f));
        for (int i = 0; i < 3; ++i)
        {
            juce::String one;
            one << (juce::juce_wchar) 2;
            drawText (g, one, heartsArea.getX() + (float) i * 24.0f, heartsArea.getCentreY() - 7.0f, 2.0f,
                      i < hearts ? pal::red : pal::edgeDark.brighter (0.5f));
        }
    }
    qi.removeFromTop (8.0f);

    // progress: 30 seconds in the zone clears the stage
    constexpr double goal = 30.0;
    const float progress = (float) juce::jlimit (0.0, 1.0, zoneSeconds / goal);
    {
        auto row = qi.removeFromTop (14.0f);
        label (row, "IN THE ZONE", pal::text);
        const int secs = (int) zoneSeconds;
        value (row, juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2), pal::green, 1.5f);
        qi.removeFromTop (4.0f);
        segmentBar (g, qi.removeFromTop (12.0f), U, progress, pal::green, pal::green, pal::yellow, 0.99f, 1.0f);
    }
    qi.removeFromTop (10.0f);

    juce::String msg;
    juce::Colour msgColour = pal::dim;
    if (progress >= 1.0f && hearts == 3)
    {
        msg = "STAGE CLEAR!";
        msgColour = (frame / 8) % 2 == 0 ? pal::yellow : pal::green;
    }
    else if (lufsS <= cf::LoudnessMeter::kFloor + 0.05f) { msg = "PRESS PLAY"; }
    else if (std::abs (lufsS - target) <= 1.0f)          { msg = "IN THE ZONE"; msgColour = pal::green; }
    else if (lufsS < target)                             { msg = "TOO QUIET \x05"; msgColour = pal::cyan; }
    else                                                 { msg = "TOO LOUD \x03"; msgColour = pal::red; }

    drawTextIn (g, msg, qi.removeFromTop (22.0f), juce::Justification::centred, 2.0f, msgColour, true);
}
} // namespace cfui

//==============================================================================
ColorForgeAudioProcessorEditor::ColorForgeAudioProcessorEditor (ColorForgeAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), audioProcessor (p)
{
    using namespace cfui;
    using namespace cfui::px;
    setLookAndFeel (&lnf);

    auto& apvts = audioProcessor.apvts;

    auto makeChoice = [&apvts] (const char* id, const char* caption, juce::Colour c) -> std::unique_ptr<ChoiceControl>
    {
        if (auto* param = apvts.getParameter (id))
            return std::make_unique<ChoiceControl> (apvts, *param, id, caption, c);
        return {};
    };

    auto addTo = [this] (int page, juce::Component* c)
    {
        if (c != nullptr)
        {
            addChildComponent (c);
            pageComponents[(size_t) page].push_back (c);
        }
    };

    // ---- tabs
    static const char* tabNames[numPages] = { "1 COLOR", "2 EQ", "3 DYNAMICS", "4 MASTER" };
    static const juce::Colour tabColours[numPages] = { pal::pink, pal::yellow, pal::orange, pal::green };
    for (int i = 0; i < numPages; ++i)
    {
        auto t = std::make_unique<juce::TextButton> (tabNames[i]);
        t->setClickingTogglesState (false);
        t->setColour (juce::TextButton::buttonOnColourId, tabColours[i].darker (0.25f));
        t->setColour (juce::TextButton::textColourOnId, pal::text);
        t->onClick = [this, i] { showPage (i); };
        addAndMakeVisible (*t);
        tabs[(size_t) i] = std::move (t);
    }

    status = std::make_unique<StatusPanel> (audioProcessor);
    addAndMakeVisible (*status);

    // ---- COLOR
    colorPower = std::make_unique<ToggleControl> (apvts, "colorOn", "COLOR FX", pal::pink, false);
    midiFollow = std::make_unique<ToggleControl> (apvts, "midiFollow", "MIDI FOLLOW", pal::purple, false);
    rootBox    = makeChoice ("root",    "ROOT",    pal::red);
    scaleBox   = makeChoice ("scale",   "SCALE",   pal::orange);
    chordBox   = makeChoice ("chord",   "CHORD",   pal::green);
    routingBox = makeChoice ("routing", "ROUTING", pal::cyan);
    analyzer   = std::make_unique<SpectrumAnalyzer> (audioProcessor, spectrum);

    pitchPanel = std::make_unique<FxPanel> (apvts, "PITCH MAP", pal::pink, "pmOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "pmAmount", "AMOUNT" }, { "pmPurity", "PURITY" }, { "pmShift", "SHIFT" },
                                { "pmSmear", "SMEAR" }, { "pmMix", "MIX" } }, 3);
    resPanel = std::make_unique<FxPanel> (apvts, "RESONATOR", pal::red, "resOn", "resMode", std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "resDecay", "DECAY" }, { "resDamp", "DAMP" }, { "resOct", "OCT" },
                                { "resStack", "STACK" }, { "resDetune", "DETUNE" }, { "resMix", "MIX" } }, 3);
    shiftPanel = std::make_unique<FxPanel> (apvts, "FREQ SHIFT", pal::orange, "fsOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "fsShift", "SHIFT" }, { "fsSpread", "SPREAD" }, { "fsFeedback", "FDBK" },
                                { "fsLfoRate", "LFO" }, { "fsLfoDepth", "DEPTH" }, { "fsMix", "MIX" } }, 3);
    chorusPanel = std::make_unique<FxPanel> (apvts, "CHORUS", pal::green, "chOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "chRate", "RATE" }, { "chDepth", "DEPTH" }, { "chMix", "MIX" } }, 3);
    reverbPanel = std::make_unique<FxPanel> (apvts, "REVERB", pal::cyan, "rvOn", nullptr,
        std::vector<ToggleSpec> { { "rvFreeze", "FREEZE" } },
        std::vector<KnobSpec> { { "rvSize", "SIZE" }, { "rvDamp", "DAMP" }, { "rvMix", "MIX" } }, 3);
    ottPanel = std::make_unique<FxPanel> (apvts, "OTT", pal::blue, "ottOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "ottDepth", "DEPTH" }, { "ottTime", "TIME" }, { "ottUp", "UP" },
                                { "ottDown", "DOWN" }, { "ottLow", "LOW" }, { "ottMid", "MID" },
                                { "ottHigh", "HIGH" } }, 4);
    colorIoPanel = std::make_unique<FxPanel> (apvts, "COLOR IN/OUT", pal::purple, nullptr, nullptr,
        std::vector<ToggleSpec> { { "subOn", "SUB KEEP" } },
        std::vector<KnobSpec> { { "drive", "DRIVE" }, { "subFreq", "SUB HZ" }, { "width", "WIDTH" }, { "mix", "MIX" } }, 4);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             colorPower.get(), midiFollow.get(), rootBox.get(), scaleBox.get(), chordBox.get(), routingBox.get(),
             analyzer.get(), pitchPanel.get(), resPanel.get(), shiftPanel.get(), chorusPanel.get(),
             reverbPanel.get(), ottPanel.get(), colorIoPanel.get() })
        addTo (pageColor, c);

    // ---- EQ
    eqPower = std::make_unique<ToggleControl> (apvts, "eqOn", "EQ ON", pal::yellow, false);
    eqGraph = std::make_unique<EqGraph> (audioProcessor, spectrum);
    addTo (pageEq, eqPower.get());
    addTo (pageEq, eqGraph.get());

    static const char* bandTitles[cf::kEqBands] = { "LO CUT", "LO SHELF", "BELL 1", "BELL 2", "BELL 3", "BELL 4", "HI SHELF", "HI CUT" };
    for (int b = 0; b < cf::kEqBands; ++b)
    {
        const bool isCut = (b == 0 || b == cf::kEqBands - 1);
        const auto on = cf::eqId (b, "On"), freq = cf::eqId (b, "Freq"), gain = cf::eqId (b, "Gain"),
                   q = cf::eqId (b, "Q"), slope = cf::eqId (b, "Slope");

        // FxPanel keeps raw pointers to the spec strings only during construction
        std::vector<KnobSpec> knobs { { freq.toRawUTF8(), "FREQ" } };
        if (! isCut)
            knobs.push_back ({ gain.toRawUTF8(), "GAIN" });
        knobs.push_back ({ q.toRawUTF8(), "Q" });

        eqBands[(size_t) b] = std::make_unique<FxPanel> (apvts, bandTitles[b], pal::accent (b), on.toRawUTF8(),
                                                         isCut ? slope.toRawUTF8() : nullptr,
                                                         std::vector<ToggleSpec> {}, knobs, 1);
        addTo (pageEq, eqBands[(size_t) b].get());
    }

    // ---- DYNAMICS
    compPanel = std::make_unique<FxPanel> (apvts, "BUS COMPRESSOR", pal::orange, "cpOn", nullptr,
        std::vector<ToggleSpec> { { "cpAuto", "AUTO GAIN" } },
        std::vector<KnobSpec> { { "cpThresh", "THRESH" }, { "cpRatio", "RATIO" }, { "cpAttack", "ATTACK" },
                                { "cpRelease", "RELEASE" }, { "cpKnee", "KNEE" }, { "cpMakeup", "MAKEUP" },
                                { "cpMix", "MIX" }, { "cpScHpf", "SC HPF" } }, 8, 190);
    compDisplay = std::make_unique<CompDisplay> (audioProcessor);
    compPanel->addAndMakeVisible (*compDisplay);
    satPanel = std::make_unique<FxPanel> (apvts, "SATURATOR", pal::red, "satOn", "satMode", std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "satDrive", "DRIVE" }, { "satTone", "TONE" }, { "satMix", "MIX" }, { "satOut", "OUTPUT" } }, 4);
    signalPath = std::make_unique<SignalPath> (audioProcessor);
    addTo (pageDynamics, compPanel.get());
    addTo (pageDynamics, satPanel.get());
    addTo (pageDynamics, signalPath.get());

    // ---- MASTER
    inputPanel = std::make_unique<FxPanel> (apvts, "INPUT", pal::cyan, nullptr, nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "inGain", "GAIN" } }, 1);
    stereoPanel = std::make_unique<FxPanel> (apvts, "STEREO", pal::blue, "imgOn", nullptr,
        std::vector<ToggleSpec> { { "imgMonoOn", "MONO BASS" } },
        std::vector<KnobSpec> { { "imgWidth", "WIDTH" }, { "imgMonoFreq", "MONO HZ" }, { "imgBalance", "BALANCE" } }, 3);
    limiterPanel = std::make_unique<FxPanel> (apvts, "LIMITER", pal::red, "limit", "lmMode", std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "lmGain", "GAIN" }, { "lmCeiling", "CEILING" }, { "lmRelease", "RELEASE" } }, 3);
    outputPanel = std::make_unique<FxPanel> (apvts, "OUTPUT", pal::green, nullptr, nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "outGain", "GAIN" } }, 1);
    loudnessHistory = std::make_unique<LoudnessHistory> (audioProcessor);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             inputPanel.get(), stereoPanel.get(), limiterPanel.get(), outputPanel.get(), loudnessHistory.get() })
        addTo (pageMaster, c);

    sendLookAndFeelChange();
    setResizable (false, false);
    setSize (1280, 820);

    showPage (juce::jlimit (0, numPages - 1, (int) apvts.state.getProperty ("uiPage", pageMaster)));
    startTimerHz (30);
}

ColorForgeAudioProcessorEditor::~ColorForgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ColorForgeAudioProcessorEditor::showPage (int page)
{
    currentPage = page;
    audioProcessor.apvts.state.setProperty ("uiPage", page, nullptr);

    for (int i = 0; i < numPages; ++i)
    {
        tabs[(size_t) i]->setToggleState (i == page, juce::dontSendNotification);
        for (auto* c : pageComponents[(size_t) i])
            c->setVisible (i == page);
    }
    repaint();
}

void ColorForgeAudioProcessorEditor::timerCallback()
{
    if (currentPage == pageColor || currentPage == pageEq)
        spectrum.update (audioProcessor);

    if (currentPage == pageColor) analyzer->update();
    if (currentPage == pageEq)    eqGraph->update();
    if (currentPage == pageDynamics) { compDisplay->update(); signalPath->repaint(); }

    loudnessHistory->update();   // keeps collecting while hidden
    status->update();
}

void ColorForgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace cfui::px;

    g.fillAll (pal::bg);

    // starfield + scanlines
    juce::Random rng (1234);
    juce::RectangleList<float> stars;
    for (int i = 0; i < 140; ++i)
        stars.addWithoutMerging ({ std::floor (rng.nextFloat() * (float) getWidth() / 2.0f) * 2.0f,
                                   std::floor (rng.nextFloat() * (float) getHeight() / 2.0f) * 2.0f, 2.0f, 2.0f });
    g.setColour (pal::edgeMid.withAlpha (0.45f));
    g.fillRectList (stars);

    // title: one palette colour per letter, with a drop shadow
    const juce::String title = "COLORFORGE";
    const float sc = 4.0f;
    float x = 16.0f;
    for (int i = 0; i < title.length(); ++i)
    {
        const auto ch = title.substring (i, i + 1);
        drawText (g, ch, x + sc, 14.0f + sc, sc, pal::edgeDark);
        drawText (g, ch, x, 14.0f, sc, pal::accent (i));
        x += 6.0f * sc;
    }
    drawText (g, "MIX + MASTER SUITE  V2", 18.0f, 52.0f, 1.5f, pal::dim);

    // scanlines over the whole background
    juce::RectangleList<float> lines;
    for (int y = 0; y < getHeight(); y += 4)
        lines.addWithoutMerging ({ 0.0f, (float) y, (float) getWidth(), 1.0f });
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRectList (lines);
}

void ColorForgeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    // header: tabs to the right of the title
    auto header = r.removeFromTop (60);
    header.removeFromLeft (290);
    header = header.withSizeKeepingCentre (header.getWidth(), 36);
    auto statusCol = r.withTrimmedLeft (r.getWidth() - 270);

    auto tabRow = header.withTrimmedRight (statusCol.getWidth() + 10);
    const int tabW = (tabRow.getWidth() - 3 * 8) / numPages;
    for (int i = 0; i < numPages; ++i)
    {
        tabs[(size_t) i]->setBounds (tabRow.removeFromLeft (tabW));
        tabRow.removeFromLeft (8);
    }

    r.removeFromTop (8);
    status->setBounds (r.removeFromRight (270));
    r.removeFromRight (10);
    const auto content = r;
    const int gap = 8;

    // ---- COLOR
    {
        auto c = content;
        auto top = c.removeFromTop (40);
        colorPower->setBounds (top.removeFromLeft (140).withTrimmedTop (12));
        top.removeFromLeft (12);
        auto place = [&top] (juce::Component* comp, int w)
        {
            auto slot = top.removeFromLeft (w);
            top.removeFromLeft (10);
            if (comp != nullptr)
                comp->setBounds (slot);
        };
        place (rootBox.get(), 70);
        place (scaleBox.get(), 200);
        place (chordBox.get(), 140);
        place (routingBox.get(), 230);
        midiFollow->setBounds (top.withTrimmedTop (12));

        c.removeFromTop (gap);
        analyzer->setBounds (c.removeFromTop (140));
        c.removeFromTop (gap);

        const int rowH = (c.getHeight() - gap) / 2;
        const int colW = (c.getWidth() - 3 * gap) / 4;
        auto cell = [&] (int row, int col, int span) -> juce::Rectangle<int>
        {
            return { c.getX() + col * (colW + gap), c.getY() + row * (rowH + gap), colW * span + gap * (span - 1), rowH };
        };
        pitchPanel->setBounds (cell (0, 0, 1));
        resPanel->setBounds (cell (0, 1, 1));
        shiftPanel->setBounds (cell (0, 2, 1));
        chorusPanel->setBounds (cell (0, 3, 1));
        reverbPanel->setBounds (cell (1, 0, 1));
        ottPanel->setBounds (cell (1, 1, 1));
        colorIoPanel->setBounds (cell (1, 2, 2));
    }

    // ---- EQ
    {
        auto c = content;
        eqPower->setBounds (c.removeFromTop (28).removeFromLeft (120));
        c.removeFromTop (gap);
        eqGraph->setBounds (c.removeFromTop (320));
        c.removeFromTop (gap);
        const int w = (c.getWidth() - 7 * 6) / cf::kEqBands;
        for (auto& b : eqBands)
        {
            b->setBounds (c.removeFromLeft (w));
            c.removeFromLeft (6);
        }
    }

    // ---- DYNAMICS
    {
        auto c = content;
        compPanel->setBounds (c.removeFromTop (430));
        compDisplay->setBounds (compPanel->getDisplayArea());
        c.removeFromTop (gap);
        satPanel->setBounds (c.removeFromLeft (560));
        c.removeFromLeft (gap);
        signalPath->setBounds (c);
    }

    // ---- MASTER
    {
        auto c = content;
        auto top = c.removeFromTop (300);
        const int small = 150;
        const int big = (top.getWidth() - 2 * small - 3 * gap) / 2;
        inputPanel->setBounds (top.removeFromLeft (small));   top.removeFromLeft (gap);
        stereoPanel->setBounds (top.removeFromLeft (big));    top.removeFromLeft (gap);
        limiterPanel->setBounds (top.removeFromLeft (big));   top.removeFromLeft (gap);
        outputPanel->setBounds (top);
        c.removeFromTop (gap);
        loudnessHistory->setBounds (c);
    }
}
