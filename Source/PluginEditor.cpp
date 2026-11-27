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

    // modulation: a white pip where the modulated value sits right now
    const float modOffset = (float) slider.getProperties().getWithDefault ("modOffset", 0.0f);
    if (std::abs (modOffset) > 1.0e-3f)
    {
        const float a = startAngle + juce::jlimit (0.0f, 1.0f, sliderPos + modOffset) * (endAngle - startAngle);
        const float px = std::round ((centre.x + pipR * std::sin (a)) / U) * U;
        const float py = std::round ((centre.y - pipR * std::cos (a)) / U) * U;
        g.setColour (pal::edgeDark);
        g.fillRect (px - 2 * U, py - 2 * U, 4 * U, 4 * U);
        g.setColour (pal::text);
        g.fillRect (px - U - U * 0.5f, py - U - U * 0.5f, 3 * U, 3 * U);
    }
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
    const auto fill = slider.isColourSpecified (juce::Slider::trackColourId) ? slider.findColour (juce::Slider::trackColourId) : pal::cyan;
    auto inner = track.reduced (U);
    float from = inner.getX();
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)   // bipolar: fill from zero
        from = std::round ((float) x + (float) slider.valueToProportionOfLength (0.0) * (float) width);
    g.setColour (fill);
    g.fillRect (snap (inner.withLeft (juce::jmin (from, sliderPos)).withRight (juce::jmax (from, sliderPos))));

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

juce::Rectangle<int> PixelLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    const int w = (int) textWidth (tipText.toUpperCase(), 1.5f) + 20, h = 24;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

void PixelLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    windowFrame (g, { 0.0f, 0.0f, (float) width, (float) height }, U * 0.5f, pal::yellow.darker (0.2f), pal::panel);
    drawTextIn (g, text.toUpperCase(), juce::Rectangle<float> ((float) width, (float) height).reduced (10.0f, 0.0f),
                juce::Justification::centredLeft, 1.5f, pal::text);
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

    // knobs that the mod matrix can reach show the modulated position as a white pip
    modTarget = ColorForgeAudioProcessor::modTargetIndex (paramID);
    processor = dynamic_cast<const ColorForgeAudioProcessor*> (&apvts.processor);
    if (modTarget > 0 && processor != nullptr)
        startTimerHz (30);
}

Knob::~Knob()
{
    stopTimer();
}

void Knob::timerCallback()
{
    const float m = processor->getModOffset (modTarget);
    if (std::abs (m - shownMod) > 1.0e-3f)
    {
        shownMod = m;
        slider.getProperties().set ("modOffset", m);
        slider.repaint();
    }
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

void FxPanel::addExtraCombo (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
{
    if (auto* p = apvts.getParameter (paramID))
    {
        addAndMakeVisible (extraCombos.add (new ChoiceControl (apvts, *p, paramID, juce::String(), colour)));
        updatePowerDim();
        resized();
    }
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
    for (auto* c : extraCombos)
        c->setAlpha (alpha);
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

    for (auto* c : extraCombos)
    {
        c->setBounds (r.removeFromTop (24).withWidth (juce::jmin (150, r.getWidth())));
        r.removeFromTop (4);
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
    liveDb = curveDb;
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

    // dynamic bands: the live curve uses the gain the detector is applying right now
    auto liveDesigns = designs;
    std::array<bool, cf::kEqBands> dyn {};
    anyDynamic = false;
    for (int b = 1; b < cf::kEqBands - 1; ++b)
        if (on[(size_t) b] && paramValue (cf::eqId (b, "Dyn")) > 0.5f)
        {
            dyn[(size_t) b] = anyDynamic = true;
            liveDesigns[(size_t) b] = cf::ParametricEQ::design (cf::ParametricEQ::typeOfBand (b),
                                                                paramValue (cf::eqId (b, "Freq")),
                                                                processor.getEqAppliedGainDb (b),
                                                                paramValue (cf::eqId (b, "Q")), 0, sr);
        }
    anyDynamic = anyDynamic && eqOn;

    for (size_t i = 0; i < curveDb.size(); ++i)
    {
        const float f = freqForX (plot.getX() + ((float) i + 0.5f) * U);
        double mag = 1.0, live = 1.0;
        if (eqOn)
            for (int b = 0; b < cf::kEqBands; ++b)
                if (on[(size_t) b])
                {
                    mag *= designs[(size_t) b].magnitude (f, sr);
                    live *= (dyn[(size_t) b] ? liveDesigns : designs)[(size_t) b].magnitude (f, sr);
                }
        curveDb[i] = (float) (20.0 * std::log10 (juce::jmax (1.0e-6, mag)));
        liveDb[i] = (float) (20.0 * std::log10 (juce::jmax (1.0e-6, live)));
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

    // live curve for dynamic bands: a dotted white trace of what is applied right now
    if (anyDynamic)
    {
        juce::RectangleList<float> dots;
        for (size_t i = 0; i < liveDb.size(); i += 2)
        {
            const float y = std::floor (juce::jlimit (plot.getY(), plot.getBottom() - U, yForDb (liveDb[i])) / U) * U;
            dots.addWithoutMerging ({ plot.getX() + (float) i * U, y, U, U });
        }
        g.setColour (pal::text);
        g.fillRectList (dots);
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

        const int mode = (int) paramValue (cf::eqId (b, "Mode"));
        const bool isDyn = bandHasGain (b) && paramValue (cf::eqId (b, "Dyn")) > 0.5f;
        juce::String tag = mode > 0 ? (mode == 1 ? "M" : "S") : "";
        if (isDyn)
            tag << "D";
        if (tag.isNotEmpty())
            drawTextIn (g, tag, box.translated (0.0f, box.getHeight() + 2.0f).withHeight (8.0f).expanded (6.0f, 0.0f),
                        juce::Justification::centred, 1.0f, c);

        // dynamic band: a small marker at the gain applied right now
        if (isDyn && on)
        {
            const float y = std::round (yForDb (processor.getEqAppliedGainDb (b)));
            g.setColour (pal::text);
            g.fillRect (snap ({ std::round (pos.x) - 5.0f, y - U * 0.5f, 10.0f, U }));
        }
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
        const int mode = (int) paramValue (cf::eqId (shown, "Mode"));
        if (mode > 0)
            txt << (mode == 1 ? "  MID" : "  SIDE");
        if (bandHasGain (shown) && paramValue (cf::eqId (shown, "Dyn")) > 0.5f)
            txt << "  DYN " << juce::String (paramValue (cf::eqId (shown, "DynThr")), 0) << " DB";

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
TransientMeter::TransientMeter (ColorForgeAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (false, false);
}

void TransientMeter::update()
{
    history[(size_t) writePos] = processor.takeTransientGainDb();
    writePos = (writePos + 1) % (int) history.size();
    repaint();
}

void TransientMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    screen (g, r);
    auto inner = r.reduced (6.0f, 4.0f);
    auto label = inner.removeFromRight (64.0f);

    float up = 0.0f, down = 0.0f;   // peaks over the last half second, for the readout
    for (int i = 1; i <= 15; ++i)
    {
        const auto& h = history[(size_t) ((writePos + (int) history.size() - i) % (int) history.size())];
        up = juce::jmax (up, h.first);
        down = juce::jmin (down, h.second);
    }
    const bool cutWins = -down > up;
    const float shown = cutWins ? down : up;
    drawTextIn (g, (shown >= 0.0f ? "+" : "") + juce::String (shown, 1) + " DB", label, juce::Justification::centredRight,
                1.5f, std::abs (shown) < 0.05f ? pal::dim : (cutWins ? pal::red : pal::green));

    const float mid = std::floor (inner.getCentreY() / U) * U;
    const float half = inner.getHeight() * 0.5f;
    const float colW = inner.getWidth() / (float) history.size();
    juce::RectangleList<float> boosts, cuts;
    for (size_t i = 0; i < history.size(); ++i)
    {
        const auto& v = history[(i + (size_t) writePos) % history.size()];
        const float x = inner.getX() + std::floor ((float) i * colW), w = juce::jmax (U, std::floor (colW));
        const float hu = std::floor (juce::jlimit (0.0f, 1.0f, v.first / 12.0f) * half / U) * U;
        const float hd = std::floor (juce::jlimit (0.0f, 1.0f, -v.second / 12.0f) * half / U) * U;
        if (hu > 0.0f) boosts.addWithoutMerging ({ x, mid - hu, w, hu });
        if (hd > 0.0f) cuts.addWithoutMerging ({ x, mid, w, hd });
    }
    dottedHLine (g, mid, inner.getX(), inner.getRight(), pal::edgeMid.withAlpha (0.6f));
    g.setColour (pal::green);
    g.fillRectList (boosts);
    g.setColour (pal::red);
    g.fillRectList (cuts);
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
PresetBar::PresetBar (ColorForgeAudioProcessor& p) : processor (p)
{
    for (auto* b : { &prev, &next, &name, &slotA, &slotB })
    {
        b->getProperties().set ("pxScale", 1.5f);
        addAndMakeVisible (*b);
    }

    prev.setTooltip ("Previous factory preset");
    next.setTooltip ("Next factory preset");
    name.setTooltip ("Presets: factory, your own, save");
    slotA.setTooltip ("Snapshot A (click to switch; the preset menu can copy A to B)");
    slotB.setTooltip ("Snapshot B");

    for (auto* b : { &slotA, &slotB })
    {
        b->setColour (juce::TextButton::buttonOnColourId, px::pal::yellow.darker (0.3f));
        b->getProperties().set ("pxScale", 2.0f);
    }

    prev.onClick  = [this] { step (-1); };
    next.onClick  = [this] { step (1); };
    name.onClick  = [this] { showMenu(); };
    slotA.onClick = [this] { processor.switchToSlot (0); refresh(); };
    slotB.onClick = [this] { processor.switchToSlot (1); refresh(); };
    refresh();
}

void PresetBar::refresh()
{
    juce::String label = processor.getPresetName();
    label << " " << (juce::juce_wchar) 3;
    if (name.getButtonText() != label)
        name.setButtonText (label);
    slotA.setToggleState (processor.getActiveSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState (processor.getActiveSlot() == 1, juce::dontSendNotification);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    slotB.setBounds (r.removeFromRight (30));
    r.removeFromRight (4);
    slotA.setBounds (r.removeFromRight (30));
    r.removeFromRight (8);
    prev.setBounds (r.removeFromLeft (26));
    r.removeFromLeft (4);
    next.setBounds (r.removeFromRight (26));
    r.removeFromRight (4);
    name.setBounds (r);
}

juce::Array<juce::File> PresetBar::userPresets() const
{
    auto files = cf::userPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + cf::kPresetExtension);
    files.sort();
    return files;
}

void PresetBar::step (int delta)
{
    const int n = cf::kNumFactoryPresets;
    processor.applyFactoryPreset (((processor.getCurrentProgram() + delta) % n + n) % n);
    refresh();
}

void PresetBar::showMenu()
{
    juce::PopupMenu menu, factory, user;

    for (int i = 0; i < cf::kNumFactoryPresets; ++i)
        factory.addItem (1 + i, cf::kFactoryPresets[i].name, true,
                         processor.getPresetName() == juce::String (cf::kFactoryPresets[i].name));

    const auto files = userPresets();
    for (int i = 0; i < files.size(); ++i)
        user.addItem (1000 + i, files[i].getFileNameWithoutExtension().toUpperCase());
    if (files.isEmpty())
        user.addItem (999, "(NONE SAVED YET)", false);

    menu.addSubMenu ("FACTORY", factory);
    menu.addSubMenu ("MY PRESETS", user);
    menu.addSeparator();
    menu.addItem (2, "SAVE PRESET...");
    menu.addItem (3, "LOAD FROM FILE...");
    menu.addItem (4, "SHOW PRESET FOLDER");
    menu.addSeparator();
    menu.addItem (5, processor.getActiveSlot() == 0 ? "COPY A TO B" : "COPY B TO A");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name),
        [this, files] (int result)
        {
            if (result >= 1 && result <= cf::kNumFactoryPresets)
                processor.applyFactoryPreset (result - 1);
            else if (result >= 1000 && result - 1000 < files.size())
                processor.loadPreset (files[result - 1000]);
            else if (result == 2 || result == 3)
            {
                const bool saving = result == 2;
                cf::userPresetFolder().createDirectory();
                chooser = std::make_unique<juce::FileChooser> (saving ? "Save preset" : "Load preset",
                                                               cf::userPresetFolder(), juce::String ("*") + cf::kPresetExtension);
                const int flags = saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                                         : juce::FileBrowserComponent::openMode;
                chooser->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles,
                    [this, saving] (const juce::FileChooser& fc)
                    {
                        auto f = fc.getResult();
                        if (f == juce::File())
                            return;
                        if (saving)
                            processor.savePreset (f.withFileExtension (cf::kPresetExtension));
                        else
                            processor.loadPreset (f);
                        refresh();
                    });
            }
            else if (result == 4)
            {
                cf::userPresetFolder().createDirectory();
                cf::userPresetFolder().startAsProcess();
            }
            else if (result == 5)
                processor.copyActiveToOtherSlot();

            refresh();
        });
}

//==============================================================================
SignalPath::SignalPath (ColorForgeAudioProcessor& p) : processor (p) {}

int SignalPath::moduleAtNode (int node) const
{
    switch (node)
    {
        case 0: return -1;
        case 1: return -2;
        case 7: return -3;
        case 8: return -4;
        default: return processor.getChainOrder()[(size_t) (node - 2)];
    }
}

void SignalPath::resized()
{
    layoutNodes();
}

void SignalPath::layoutNodes()
{
    auto inner = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
    inner.removeFromTop (30.0f);
    hintArea = inner.removeFromBottom (14.0f);

    // three rows, snaking like a level map
    constexpr int perRow = 3;
    const float cellW = inner.getWidth() / (float) perRow;
    const float rowH = juce::jmin (80.0f, inner.getHeight() / 3.0f);

    for (int i = 0; i < kNodes; ++i)
    {
        const int row = i / perRow, col = row % 2 == 0 ? i % perRow : perRow - 1 - i % perRow;
        const juce::Point<float> c { inner.getX() + cellW * ((float) col + 0.5f), inner.getY() + rowH * ((float) row + 0.5f) };
        nodeBoxes[(size_t) i] = juce::Rectangle<float> (juce::jmin (cellW - 12.0f, 84.0f), 30.0f).withCentre (c);
    }
}

int SignalPath::nodeAt (juce::Point<float> pos) const
{
    for (int i = 0; i < kNodes; ++i)
        if (nodeBoxes[(size_t) i].expanded (4.0f).contains (pos))
            return i;
    return -1;
}

void SignalPath::mouseDown (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    dragNode = (n >= 2 && n <= 6) ? n : -1;
    dragPos = e.position;
}

void SignalPath::mouseDrag (const juce::MouseEvent& e)
{
    if (dragNode < 0)
        return;
    dragPos = e.position;
    repaint();
}

void SignalPath::mouseUp (const juce::MouseEvent& e)
{
    const int target = nodeAt (e.position);

    if (dragNode >= 2 && target >= 2 && target <= 6 && target != dragNode)
    {
        auto order = processor.getChainOrder();
        const int moved = order[(size_t) dragNode - 2];
        // move the dragged module to the target slot, shifting the others along
        std::vector<int> v (order.begin(), order.end());
        v.erase (v.begin() + (dragNode - 2));
        v.insert (v.begin() + (target - 2), moved);
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = v[i];
        processor.setChainOrder (order);
    }

    dragNode = -1;
    repaint();
}

void SignalPath::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    windowFrame (g, r, U, pal::purple.darker (0.3f), pal::panel);
    drawTextIn (g, "SIGNAL PATH", r.reduced (16.0f, 12.0f).removeFromTop (22.0f), juce::Justification::centredLeft, 2.0f, pal::text);

    auto on = [this] (const char* id) { return id == nullptr || processor.apvts.getRawParameterValue (id)->load() > 0.5f; };

    struct Node { const char* name; const char* id; juce::Colour c; };
    auto describe = [] (int what) -> Node
    {
        switch (what)
        {
            case -1: return { "IN",     nullptr,   pal::cyan };
            case -2: return { "COLOR",  "colorOn", pal::pink };
            case -3: return { "LIMIT",  "limit",   pal::red };
            case -4: return { "OUT",    nullptr,   pal::green };
            case ColorForgeAudioProcessor::modEq:   return { "EQ",     "eqOn",  pal::yellow };
            case ColorForgeAudioProcessor::modComp: return { "COMP",   "cpOn",  pal::orange };
            case ColorForgeAudioProcessor::modSat:  return { "SAT",    "satOn", pal::red };
            case ColorForgeAudioProcessor::modTrans: return { "TRANS", "trOn",  pal::green };
            default:                                return { "STEREO", "imgOn", pal::blue };
        }
    };

    std::array<Node, kNodes> nodes;
    for (int i = 0; i < kNodes; ++i)
        nodes[(size_t) i] = describe (moduleAtNode (i));

    for (int i = 0; i + 1 < kNodes; ++i)
    {
        const bool live = on (nodes[(size_t) i].id) && on (nodes[(size_t) i + 1].id);
        pixelLine (g, nodeBoxes[(size_t) i].getCentre(), nodeBoxes[(size_t) i + 1].getCentre(), U, 2 * U,
                   live ? pal::edgeLight.withAlpha (0.7f) : pal::edgeMid.withAlpha (0.5f));
    }

    const int hover = dragNode >= 0 ? nodeAt (dragPos) : -1;

    for (int i = 0; i < kNodes; ++i)
    {
        const auto& n = nodes[(size_t) i];
        const bool active = on (n.id);
        auto box = nodeBoxes[(size_t) i];

        if (dragNode >= 0 && i == hover && i != dragNode && i >= 2 && i <= 6)
            fillBox (g, box.expanded (3 * U), U, pal::yellow);

        if (active)
            bevel (g, box, U, n.c.darker (0.2f), i == dragNode);
        else
            windowFrame (g, box, U * 0.5f, n.c.withAlpha (0.5f), pal::bgAlt);

        drawTextIn (g, n.name, box.reduced (4.0f, 0.0f), juce::Justification::centred, 2.0f,
                    active ? pal::edgeDark : pal::dim);

        if (i >= 2 && i <= 6)   // movable: little grip marks
        {
            g.setColour ((active ? pal::edgeDark : pal::dim).withAlpha (0.6f));
            g.fillRect (std::round (box.getX() + 3 * U), std::round (box.getCentreY() - 2 * U), U, U);
            g.fillRect (std::round (box.getX() + 3 * U), std::round (box.getCentreY() + U), U, U);
        }
    }

    if (dragNode >= 0)
    {
        auto ghost = nodeBoxes[(size_t) dragNode].withCentre (dragPos);
        windowFrame (g, ghost, U * 0.5f, pal::yellow, pal::panel.withAlpha (0.85f));
        drawTextIn (g, nodes[(size_t) dragNode].name, ghost.reduced (4.0f, 0.0f), juce::Justification::centred, 2.0f, pal::yellow);
    }

    drawTextIn (g, "DRAG A MIDDLE BLOCK TO REORDER  \x04  DIM = OFF", hintArea,
                juce::Justification::centredLeft, 1.5f, pal::dim);
}

//==============================================================================
StatusPanel::StatusPanel (ColorForgeAudioProcessor& p) : processor (p)
{
    for (int i = 0; i < cf::kNumLoudnessTargets; ++i)
        targetBox.addItem (cf::kLoudnessTargets[i].name, i + 1);

    targetBox.setSelectedId ((int) processor.apvts.state.getProperty (ColorForgeAudioProcessor::kLufsTargetProp, 2) + 1,
                             juce::dontSendNotification);
    targetBox.getProperties().set ("pxScale", 1.5f);
    targetBox.setTooltip ("Loudness target (LUFS) to compare the short-term and integrated loudness against");
    targetBox.onChange = [this]
    {
        processor.apvts.state.setProperty (ColorForgeAudioProcessor::kLufsTargetProp, targetBox.getSelectedId() - 1, nullptr);
    };
    addAndMakeVisible (targetBox);

    resetButton.getProperties().set ("pxScale", 1.5f);
    resetButton.setTooltip ("Reset the integrated loudness and the true-peak hold");
    resetButton.onClick = [this]
    {
        processor.meter.resetRequested.store (true);
    };
    addAndMakeVisible (resetButton);

    bypassButton.getProperties().set ("pxScale", 1.5f);
    bypassButton.setColour (juce::ToggleButton::tickColourId, pal::red);
    bypassButton.setTooltip ("Level-matched bypass: hear the dry input at the same loudness as the processed signal");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "matchBypass", bypassButton);
    addAndMakeVisible (bypassButton);
}

float StatusPanel::targetLufs() const
{
    const int idx = (int) processor.apvts.state.getProperty (ColorForgeAudioProcessor::kLufsTargetProp, 2);
    return cf::kLoudnessTargets[juce::jlimit (0, cf::kNumLoudnessTargets - 1, idx)].lufs;
}

void StatusPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    targetArea = r.removeFromBottom (190);
    metersArea = r;
    bypassButton.setBounds (r.removeFromTop (22).removeFromRight (100));

    auto q = targetArea.reduced (10).withTrimmedTop (30);
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
    matchDb = processor.getBypassMatchDb();


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

    // ---- level-matched bypass: the gain applied to the dry signal so A/B is a fair fight
    {
        auto row = r.removeFromTop (14.0f);
        const bool byp = bypassButton.getToggleState();
        label (row, byp ? "BYPASS ON" : "BYPASS MATCH", byp ? pal::red : pal::dim);
        value (row, (matchDb >= 0.0f ? "+" : "") + juce::String (matchDb, 1) + " DB", byp ? pal::red : pal::dim, 1.5f);
    }

    // ---- loudness target: how far the mix sits from the chosen target and the ceiling
    auto q = targetArea.toFloat();
    windowFrame (g, q, U, pal::yellow.darker (0.2f), pal::bgAlt);
    auto qi = q.reduced (10.0f);
    drawTextIn (g, "LOUDNESS TARGET", qi.removeFromTop (22.0f), juce::Justification::centred, 2.0f, pal::yellow, true);
    qi.removeFromTop (36.0f);   // target box + reset button live here

    const bool hasSignal = lufsS > cf::LoudnessMeter::kFloor + 0.05f;
    auto luText = [] (float d) { return (d >= 0.0f ? "+" : "") + juce::String (d, 1) + " LU"; };
    auto luColour = [] (float d) { return std::abs (d) <= 1.0f ? pal::green : (d > 0.0f ? pal::red : pal::cyan); };

    for (int i = 0; i < 2; ++i)
    {
        const float v = i == 0 ? lufsS : lufsI;
        const bool valid = v > cf::LoudnessMeter::kFloor + 0.05f;
        auto row = qi.removeFromTop (14.0f);
        label (row, i == 0 ? "SHORT-TERM" : "INTEGRATED", pal::text);
        value (row, valid ? luText (v - target) : juce::String ("--"), valid ? luColour (v - target) : pal::dim, 1.5f);
        qi.removeFromTop (6.0f);
    }

    // deviation bar: the centre line is the target, +-6 LU either side
    {
        auto bar = qi.removeFromTop (14.0f);
        bevel (g, bar, U, pal::bgAlt, true);
        auto inner = bar.reduced (U);
        const float mid = std::round (inner.getCentreX() / U) * U;
        g.setColour (pal::green.withAlpha (0.25f));
        g.fillRect (snap (inner.withLeft (mid - inner.getWidth() / 12.0f).withRight (mid + inner.getWidth() / 12.0f)));
        if (hasSignal)
        {
            const float d = juce::jlimit (-6.0f, 6.0f, lufsS - target);
            const float x = std::round ((mid + d / 6.0f * inner.getWidth() * 0.5f) / U) * U;
            g.setColour (luColour (lufsS - target));
            g.fillRect (snap (inner.withLeft (juce::jmin (mid, x)).withRight (juce::jmax (mid, x) + U)));
        }
        g.setColour (pal::edgeLight);
        g.fillRect (mid, inner.getY(), U, inner.getHeight());
    }
    qi.removeFromTop (10.0f);

    // true peak against the limiter ceiling
    {
        const float ceil = processor.apvts.getRawParameterValue ("lmCeiling")->load();
        const float over = truePeak - ceil;
        auto row = qi.removeFromTop (14.0f);
        label (row, "PEAK VS CEILING", pal::text);
        if (truePeak <= -99.0f)
            value (row, "--", pal::dim, 1.5f);
        else
            value (row, over > 0.05f ? "OVER +" + juce::String (over, 1) + " DB" : juce::String ("OK"),
                   over > 0.05f ? pal::red : pal::green, 1.5f);
    }
}
//==============================================================================
LfoScope::LfoScope (ColorForgeAudioProcessor& p, int lfoIndex, juce::Colour c)
    : processor (p), index (lfoIndex), colour (c)
{
}

void LfoScope::paint (juce::Graphics& g)
{
    screen (g, getLocalBounds().toFloat());
    auto plot = getLocalBounds().toFloat().reduced (8.0f, 10.0f);
    const juce::String id ("lfo" + juce::String (index + 1));
    const int shape = (int) processor.apvts.getRawParameterValue (id + "Shape")->load();
    const bool sync = processor.apvts.getRawParameterValue (id + "Sync")->load() > 0.5f;

    dottedHLine (g, plot.getCentreY(), plot.getX(), plot.getRight(), pal::edgeMid.withAlpha (0.6f));

    // one cycle of the shape (the random shape is drawn as fixed steps)
    juce::RectangleList<float> line;
    const int cols = (int) (plot.getWidth() / U);
    float prevY = -1.0f;
    for (int c = 0; c < cols; ++c)
    {
        const double ph = (double) c / (double) juce::jmax (1, cols - 1);
        const double v = shape == cf::ModEngine::random ? std::sin ((double) ((int) (ph * 8.0) * 7 % 11) * 1.7)
                                                        : cf::ModEngine::shapeValue (shape, ph, 0.0);
        const float y = std::floor (juce::jmap ((float) v, -1.0f, 1.0f, plot.getBottom() - U, plot.getY()) / U) * U;
        const float a = prevY < 0.0f ? y : juce::jmin (prevY, y), b = prevY < 0.0f ? y : juce::jmax (prevY, y);
        line.addWithoutMerging ({ plot.getX() + (float) c * U, a, U, b - a + U });
        prevY = y;
    }
    g.setColour (colour.withAlpha (0.8f));
    g.fillRectList (line);

    // current phase and value
    const double ph = processor.getLfoPhase (index);
    const float v = processor.getModSourceValue (1 + index);
    const float x = std::round ((plot.getX() + (float) ph * plot.getWidth()) / U) * U;
    const float y = std::round (juce::jmap (v, -1.0f, 1.0f, plot.getBottom(), plot.getY()) / U) * U;
    g.setColour (pal::edgeDark);
    g.fillRect (x - 4.0f, y - 4.0f, 10.0f, 10.0f);
    g.setColour (pal::text);
    g.fillRect (x - 3.0f, y - 3.0f, 6.0f, 6.0f);

    drawTextIn (g, sync ? "SYNC" : "FREE", getLocalBounds().toFloat().reduced (6.0f, 4.0f).withHeight (10.0f),
                juce::Justification::topRight, 1.0f, sync ? colour : pal::dim);
}

//==============================================================================
EnvScope::EnvScope (ColorForgeAudioProcessor& p) : processor (p)
{
}

void EnvScope::update()
{
    history[(size_t) writePos] = processor.getModSourceValue (3);
    writePos = (writePos + 1) % (int) history.size();
    repaint();
}

void EnvScope::paint (juce::Graphics& g)
{
    screen (g, getLocalBounds().toFloat());
    auto plot = getLocalBounds().toFloat().reduced (8.0f, 8.0f);
    const float colW = plot.getWidth() / (float) history.size();
    juce::RectangleList<float> bars;
    for (size_t i = 0; i < history.size(); ++i)
    {
        const float v = history[(writePos + i) % history.size()];
        const float h = std::floor (v * plot.getHeight() / U) * U;
        if (h > 0.0f)
            bars.addWithoutMerging ({ plot.getX() + (float) i * colW, plot.getBottom() - h, juce::jmax (U, colW - U), h });
    }
    g.setColour (pal::green.withAlpha (0.75f));
    g.fillRectList (bars);
}

//==============================================================================
ModMatrix::ModMatrix (ColorForgeAudioProcessor& p) : processor (p)
{
    auto& apvts = processor.apvts;
    for (int i = 0; i < cf::kNumModSlots; ++i)
    {
        auto& row = rows[(size_t) i];
        const auto srcId = cf::modId (i, "Src"), dstId = cf::modId (i, "Dst"), amtId = cf::modId (i, "Amt");
        row.src = std::make_unique<ChoiceControl> (apvts, *apvts.getParameter (srcId), srcId, juce::String(), pal::accent (i));
        row.dst = std::make_unique<ChoiceControl> (apvts, *apvts.getParameter (dstId), dstId, juce::String(), pal::accent (i));
        addAndMakeVisible (*row.src);
        addAndMakeVisible (*row.dst);

        row.amount.setSliderStyle (juce::Slider::LinearHorizontal);
        row.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
        row.amount.setColour (juce::Slider::trackColourId, pal::accent (i));
        row.amount.setTooltip ("How far this slot moves the target (double-click = 0)");
        addAndMakeVisible (row.amount);
        row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, amtId, row.amount);
        row.amount.textFromValueFunction = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 0) + " %"; };
        row.amount.updateText();
        row.amount.setDoubleClickReturnValue (true, 0.0);
    }
}

void ModMatrix::update()
{
    for (int i = 0; i < cf::kNumModSlots; ++i)
    {
        const auto& apvts = processor.apvts;
        const int src = (int) apvts.getRawParameterValue (cf::modId (i, "Src"))->load();
        const int dst = (int) apvts.getRawParameterValue (cf::modId (i, "Dst"))->load();
        const float amt = apvts.getRawParameterValue (cf::modId (i, "Amt"))->load() * 0.01f;
        live[(size_t) i] = (src > 0 && dst > 0) ? amt * processor.getModSourceValue (src) : 0.0f;
    }
    repaint();
}

void ModMatrix::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    r.removeFromTop (34);   // title + column heads
    const int rowH = juce::jmin (36, r.getHeight() / cf::kNumModSlots);
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rowH).withSizeKeepingCentre (r.getWidth(), 26);
        row.label = line.removeFromLeft (36);
        row.src->setBounds (line.removeFromLeft (130));
        line.removeFromLeft (10);
        row.dst->setBounds (line.removeFromLeft (220));
        line.removeFromLeft (14);
        row.meter = line.removeFromRight (160);
        line.removeFromRight (14);
        row.amount.setBounds (line);
    }
}

void ModMatrix::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    windowFrame (g, r, U, pal::purple.darker (0.2f), pal::panel);

    auto tab = juce::Rectangle<float> (3 * U, 3 * U, textWidth ("MOD MATRIX", 2.0f) + 12.0f, 20.0f);
    g.setColour (pal::purple);
    g.fillRect (snap (tab));
    drawTextIn (g, "MOD MATRIX", tab.reduced (6.0f, 0.0f), juce::Justification::centredLeft, 2.0f, pal::edgeDark);
    drawTextIn (g, "SOURCE \x04 TARGET \x04 AMOUNT.  THE WHITE PIP ON A KNOB SHOWS WHERE IT IS MODULATED TO",
                { tab.getRight() + 14.0f, tab.getY() + 5.0f, r.getWidth() - tab.getRight() - 30.0f, 10.0f },
                juce::Justification::centredLeft, 1.0f, pal::dim);

    for (int i = 0; i < cf::kNumModSlots; ++i)
    {
        const auto& row = rows[(size_t) i];
        const auto c = pal::accent (i);
        drawTextIn (g, juce::String (i + 1), row.label.toFloat(), juce::Justification::centred, 2.0f, c);

        // live offset: centre line, bar towards the current value
        auto m = row.meter.toFloat().withSizeKeepingCentre ((float) row.meter.getWidth(), 14.0f);
        bevel (g, m, U, pal::bgAlt, true);
        auto inner = m.reduced (U);
        const float mid = std::round (inner.getCentreX() / U) * U;
        const float x = std::round ((mid + juce::jlimit (-1.0f, 1.0f, live[(size_t) i]) * inner.getWidth() * 0.5f) / U) * U;
        g.setColour (c);
        g.fillRect (snap (inner.withLeft (juce::jmin (mid, x)).withRight (juce::jmax (mid, x) + U)));
        g.setColour (pal::edgeLight);
        g.fillRect (mid, inner.getY(), U, inner.getHeight());
    }
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
    static const char* tabNames[numPages] = { "1 COLOR", "2 EQ", "3 DYNAMICS", "4 MASTER", "5 MOD" };
    static const juce::Colour tabColours[numPages] = { pal::pink, pal::yellow, pal::orange, pal::green, pal::purple };
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
    presetBar = std::make_unique<PresetBar> (audioProcessor);
    addAndMakeVisible (*presetBar);

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
                                { "ottHigh", "HIGH" }, { "ottXLow", "X LOW" }, { "ottXHigh", "X HIGH" } }, 5);
    ottPanel->addExtraCombo (apvts, "ottSolo");
    colorIoPanel = std::make_unique<FxPanel> (apvts, "COLOR IN/OUT", pal::purple, nullptr, nullptr,
        std::vector<ToggleSpec> { { "subOn", "SUB KEEP" } },
        std::vector<KnobSpec> { { "drive", "DRIVE" }, { "subFreq", "SUB HZ" }, { "width", "WIDTH" }, { "mix", "MIX" } }, 2);

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
        const auto dyn = cf::eqId (b, "Dyn"), dynThr = cf::eqId (b, "DynThr");
        std::vector<KnobSpec> knobs { { freq.toRawUTF8(), "FREQ" } };
        std::vector<ToggleSpec> toggles;
        if (! isCut)
        {
            knobs.push_back ({ gain.toRawUTF8(), "GAIN" });
            knobs.push_back ({ q.toRawUTF8(), "Q" });
            knobs.push_back ({ dynThr.toRawUTF8(), "THR" });
            toggles.push_back ({ dyn.toRawUTF8(), "DYN" });
        }
        else
        {
            knobs.push_back ({ q.toRawUTF8(), "Q" });
        }

        eqBands[(size_t) b] = std::make_unique<FxPanel> (apvts, bandTitles[b], pal::accent (b), on.toRawUTF8(),
                                                         isCut ? slope.toRawUTF8() : nullptr,
                                                         toggles, knobs, isCut ? 1 : 2);
        eqBands[(size_t) b]->addExtraCombo (apvts, cf::eqId (b, "Mode"));
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
        std::vector<KnobSpec> { { "satDrive", "DRIVE" }, { "satTone", "TONE" }, { "satMix", "MIX" }, { "satOut", "OUTPUT" } }, 2);
    transPanel = std::make_unique<FxPanel> (apvts, "TRANSIENT", pal::green, "trOn", nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "trAttack", "ATTACK" }, { "trSustain", "SUSTAIN" }, { "trMix", "MIX" }, { "trOut", "OUTPUT" } }, 2, 30);
    transMeter = std::make_unique<TransientMeter> (audioProcessor);
    transPanel->addAndMakeVisible (*transMeter);
    signalPath = std::make_unique<SignalPath> (audioProcessor);
    addTo (pageDynamics, compPanel.get());
    addTo (pageDynamics, satPanel.get());
    addTo (pageDynamics, transPanel.get());
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

    // ---- MOD
    for (int i = 0; i < 2; ++i)
    {
        const juce::String id ("lfo" + juce::String (i + 1));
        const auto shape = id + "Shape", rate = id + "Rate", sync = id + "Sync", div = id + "Div";
        const auto colour = i == 0 ? pal::cyan : pal::pink;
        lfoPanels[i] = std::make_unique<FxPanel> (apvts, "LFO " + juce::String (i + 1), colour, nullptr, div.toRawUTF8(),
            std::vector<ToggleSpec> { { sync.toRawUTF8(), "SYNC" } },
            std::vector<KnobSpec> { { rate.toRawUTF8(), "RATE" } }, 1, 90);
        lfoPanels[i]->addExtraCombo (apvts, shape);
        lfoScopes[i] = std::make_unique<LfoScope> (audioProcessor, i, colour);
        lfoPanels[i]->addAndMakeVisible (*lfoScopes[i]);
        addTo (pageMod, lfoPanels[i].get());
    }
    envPanel = std::make_unique<FxPanel> (apvts, "ENVELOPE", pal::green, nullptr, nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "envAttack", "ATTACK" }, { "envRelease", "RELEASE" }, { "envGain", "SENS" } }, 3, 90);
    envScope = std::make_unique<EnvScope> (audioProcessor);
    envPanel->addAndMakeVisible (*envScope);
    macroPanel = std::make_unique<FxPanel> (apvts, "MACROS", pal::yellow, nullptr, nullptr, std::vector<ToggleSpec> {},
        std::vector<KnobSpec> { { "macro1", "MACRO 1" }, { "macro2", "MACRO 2" }, { "macro3", "MACRO 3" }, { "macro4", "MACRO 4" } }, 2);
    modMatrix = std::make_unique<ModMatrix> (audioProcessor);
    for (juce::Component* c : std::initializer_list<juce::Component*> { lfoPanels[0].get(), lfoPanels[1].get(),
                                                                         envPanel.get(), macroPanel.get(), modMatrix.get() })
        addTo (pageMod, c);

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
    if (currentPage == pageDynamics) { compDisplay->update(); transMeter->update(); signalPath->repaint(); }
    if (currentPage == pageMod)
    {
        lfoScopes[0]->update();
        lfoScopes[1]->update();
        envScope->update();
        modMatrix->update();
    }

    loudnessHistory->update();   // keeps collecting while hidden
    status->update();
    presetBar->refresh();
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
    presetBar->setBounds (statusCol.getX(), header.getY() + 4, statusCol.getWidth(), 28);

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
        ottPanel->setBounds (cell (1, 1, 2));
        colorIoPanel->setBounds (cell (1, 3, 1));
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
        const int w = (c.getWidth() - 2 * gap) * 3 / 10;
        satPanel->setBounds (c.removeFromLeft (w));
        c.removeFromLeft (gap);
        transPanel->setBounds (c.removeFromLeft (w));
        transMeter->setBounds (transPanel->getDisplayArea());
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

    // ---- MOD
    {
        auto c = content;
        auto top = c.removeFromTop (330);
        const int w = (top.getWidth() - 3 * gap) / 4;
        for (auto* panel : { lfoPanels[0].get(), lfoPanels[1].get(), envPanel.get(), macroPanel.get() })
        {
            panel->setBounds (top.removeFromLeft (w));
            top.removeFromLeft (gap);
        }
        lfoScopes[0]->setBounds (lfoPanels[0]->getDisplayArea());
        lfoScopes[1]->setBounds (lfoPanels[1]->getDisplayArea());
        envScope->setBounds (envPanel->getDisplayArea());
        c.removeFromTop (gap);
        modMatrix->setBounds (c);
    }
}
