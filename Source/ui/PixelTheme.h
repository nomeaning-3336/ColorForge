#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PixelGlyphs.h"

#include <cmath>

/** The 8-bit look: a small fixed palette, a bitmap font and chunky drawing helpers.
    Everything is drawn on a grid of whole "pixels" (usually 2x2 screen points). */
namespace cfui::px
{
//==============================================================================
// Palette (16 colours)
namespace pal
{
    const juce::Colour bg        { 0xff0d0b1e };
    const juce::Colour bgAlt     { 0xff151230 };
    const juce::Colour panel     { 0xff1d1940 };
    const juce::Colour panelHi   { 0xff2a2560 };
    const juce::Colour edgeLight { 0xffd8d4ff };
    const juce::Colour edgeMid   { 0xff6b63c9 };
    const juce::Colour edgeDark  { 0xff07061a };
    const juce::Colour text      { 0xfff3efe0 };
    const juce::Colour dim       { 0xff9089c4 };
    const juce::Colour red       { 0xffff4d6d };
    const juce::Colour orange    { 0xffff9e3d };
    const juce::Colour yellow    { 0xffffe14d };
    const juce::Colour green     { 0xff5ee06a };
    const juce::Colour cyan      { 0xff3fd8f0 };
    const juce::Colour blue      { 0xff4e7bff };
    const juce::Colour purple    { 0xffb25cff };
    const juce::Colour pink      { 0xffff6ad5 };

    /** Eight accents, for things like EQ band handles. */
    inline juce::Colour accent (int i)
    {
        static const juce::Colour c[] = { red, orange, yellow, green, cyan, blue, purple, pink };
        return c[((i % 8) + 8) % 8];
    }
}

//==============================================================================
// Bitmap font
inline const std::uint8_t* glyphFor (juce::juce_wchar ch) noexcept
{
    if (ch >= 'a' && ch <= 'z')
        ch = ch - 'a' + 'A';
    if (ch >= 1 && ch <= 5)
        return glyphs::kSpecial[ch];
    if (ch == 0x2605) return glyphs::kSpecial[1];   // star
    if (ch == 0x2665) return glyphs::kSpecial[2];   // heart
    if (ch < 32 || ch > 95)
        ch = '?';
    return glyphs::kAscii[ch - 32];
}

inline float textWidth (const juce::String& s, float scale) noexcept
{
    const int n = s.length();
    return n <= 0 ? 0.0f : (float) (n * 6 - 1) * scale;
}

inline float textHeight (float scale) noexcept   { return 7.0f * scale; }

/** Draws text with its top-left corner at (x, y). Pixel edges are rounded to whole points. */
inline void drawText (juce::Graphics& g, const juce::String& s, float x, float y, float scale, juce::Colour c)
{
    x = std::round (x);
    y = std::round (y);
    juce::RectangleList<float> rects;

    auto edge = [scale] (float origin, int i) { return std::round (origin + (float) i * scale); };

    int col = 0;
    for (auto it = s.getCharPointer(); ! it.isEmpty(); ++it, ++col)
    {
        const auto* rows = glyphFor (*it);
        const float gx = x + (float) (col * 6) * scale;

        for (int row = 0; row < 7; ++row)
        {
            const auto bits = rows[row];
            if (bits == 0)
                continue;

            const float y0 = edge (y, row), y1 = edge (y, row + 1);
            for (int bit = 0; bit < 5; ++bit)
                if (bits & (0x10 >> bit))
                {
                    const float x0 = std::round (gx + (float) bit * scale), x1 = std::round (gx + (float) (bit + 1) * scale);
                    rects.addWithoutMerging ({ x0, y0, x1 - x0, y1 - y0 });
                }
        }
    }

    g.setColour (c);
    g.fillRectList (rects);
}

/** Largest of 3, 2, 1.5, 1 (capped at maxScale) at which the text fits in `width`. */
inline float fitScale (const juce::String& s, float width, float maxScale) noexcept
{
    for (float sc : { 4.0f, 3.0f, 2.0f, 1.5f })
        if (sc <= maxScale && textWidth (s, sc) <= width)
            return sc;
    return 1.0f;
}

inline void drawTextIn (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                        juce::Justification just, float maxScale, juce::Colour c, bool shadow = false)
{
    const float sc = fitScale (s, area.getWidth(), maxScale);
    const float w = textWidth (s, sc), h = textHeight (sc);
    float x = area.getX(), y = area.getCentreY() - h * 0.5f;

    if (just.testFlags (juce::Justification::horizontallyCentred))  x = area.getCentreX() - w * 0.5f;
    else if (just.testFlags (juce::Justification::right))           x = area.getRight() - w;
    if (just.testFlags (juce::Justification::top))                  y = area.getY();
    else if (just.testFlags (juce::Justification::bottom))          y = area.getBottom() - h;

    if (shadow)
        drawText (g, s, x + sc, y + sc, sc, pal::edgeDark);
    drawText (g, s, x, y, sc, c);
}

//==============================================================================
// Shapes
inline juce::Rectangle<float> snap (juce::Rectangle<float> r) noexcept
{
    return { std::round (r.getX()), std::round (r.getY()), std::round (r.getWidth()), std::round (r.getHeight()) };
}

/** A box with 1-pixel cut corners (the classic sprite-box look). */
inline void fillBox (juce::Graphics& g, juce::Rectangle<float> r, float u, juce::Colour c)
{
    r = snap (r);
    g.setColour (c);
    g.fillRect (r.reduced (u, 0.0f));
    g.fillRect (r.reduced (0.0f, u));
}

/** Raised / sunken bevel: light top-left, dark bottom-right. */
inline void bevel (juce::Graphics& g, juce::Rectangle<float> r, float u, juce::Colour fill, bool sunken)
{
    r = snap (r);
    fillBox (g, r, u, pal::edgeDark);
    auto in = r.reduced (u);
    g.setColour (fill);
    g.fillRect (in);
    g.setColour (sunken ? fill.darker (0.6f) : fill.brighter (0.45f));
    g.fillRect (in.withHeight (u));
    g.fillRect (in.withWidth (u));
    g.setColour (sunken ? fill.brighter (0.25f) : fill.darker (0.55f));
    g.fillRect (in.withTop (in.getBottom() - u));
    g.fillRect (in.withLeft (in.getRight() - u));
}

/** Dialogue-window frame: dark outline, light ring, coloured inner line, filled body. */
inline void windowFrame (juce::Graphics& g, juce::Rectangle<float> r, float u, juce::Colour accent, juce::Colour body)
{
    r = snap (r);
    fillBox (g, r, u, pal::edgeDark);
    fillBox (g, r.reduced (u), u, pal::edgeLight.withAlpha (0.85f));
    fillBox (g, r.reduced (2.0f * u), u, accent);
    g.setColour (body);
    g.fillRect (r.reduced (3.0f * u));
}

/** Checkerboard dither over an area, for 8-bit style shading. */
inline void dither (juce::Graphics& g, juce::Rectangle<float> r, float u, juce::Colour c)
{
    r = snap (r);
    juce::RectangleList<float> rects;
    const int cols = (int) (r.getWidth() / u), rows = (int) (r.getHeight() / u);
    for (int y = 0; y < rows; ++y)
        for (int x = (y & 1); x < cols; x += 2)
            rects.addWithoutMerging ({ r.getX() + (float) x * u, r.getY() + (float) y * u, u, u });
    g.setColour (c);
    g.fillRectList (rects);
}

/** Horizontal segmented bar ("HP bar"). value 0..1. */
inline void segmentBar (juce::Graphics& g, juce::Rectangle<float> r, float u, float value,
                        juce::Colour lo, juce::Colour mid, juce::Colour hi, float midFrom = 0.7f, float hiFrom = 0.9f)
{
    r = snap (r);
    g.setColour (pal::edgeDark);
    g.fillRect (r);
    auto in = r.reduced (u);
    const float segW = 2.0f * u, gap = u;
    const int n = juce::jmax (1, (int) ((in.getWidth() + gap) / (segW + gap)));
    const int lit = (int) std::round (juce::jlimit (0.0f, 1.0f, value) * (float) n);

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) n;
        const auto c = t >= hiFrom ? hi : (t >= midFrom ? mid : lo);
        g.setColour (i < lit ? c : c.withAlpha (0.13f));
        g.fillRect (in.getX() + (float) i * (segW + gap), in.getY(), segW, in.getHeight());
    }
}
} // namespace cfui::px
