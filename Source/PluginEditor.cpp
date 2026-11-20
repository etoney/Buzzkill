#include "PluginEditor.h"
#include <functional>

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

//==============================================================================
namespace pal
{
const juce::Colour ironDark  { 0xff1d1815 };
const juce::Colour iron      { 0xff3a322c };
const juce::Colour rustDeep  { 0xff4a1d0c };
const juce::Colour rust      { 0xff9a4518 };
const juce::Colour rustLight { 0xffc0702a };
const juce::Colour paint     { 0xffd6c7a6 };   // dirty cream stencil paint
const juce::Colour paintDim  { 0xffa8997c };
const juce::Colour signal    { 0xffd2452a };   // rusty red for active things
const juce::Colour signalHot { 0xffff6a3d };
const juce::Colour hazard    { 0xffc99a22 };   // worn safety yellow
const juce::Colour screen    { 0xff0c0a08 };
}

//==============================================================================
namespace tex
{
inline float hash (int x, int y, int seed)
{
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u + (uint32_t) seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xffffffu) / 16777215.0f;
}
inline float smooth (float t) { return t * t * (3.0f - 2.0f * t); }
inline float smoothstep (float a, float b, float x) { return smooth (juce::jlimit (0.0f, 1.0f, (x - a) / (b - a))); }
inline float vnoise (float x, float y, int seed)
{
    const int xi = (int) std::floor (x), yi = (int) std::floor (y);
    const float fx = x - (float) xi, fy = y - (float) yi;
    const float a = hash (xi, yi, seed), b = hash (xi + 1, yi, seed), c = hash (xi, yi + 1, seed), d = hash (xi + 1, yi + 1, seed);
    const float u = smooth (fx), v = smooth (fy);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
inline float fbm (float x, float y, int seed, int octaves)
{
    float s = 0, amp = 0.5f, f = 1, norm = 0;
    for (int i = 0; i < octaves; ++i) { s += amp * vnoise (x * f, y * f, seed + i * 31); norm += amp; f *= 2.03f; amp *= 0.5f; }
    return s / norm;
}
struct RGB { float r, g, b; };
inline RGB rgb (juce::Colour c) { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }
inline RGB mix (RGB a, RGB b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
inline RGB mul (RGB a, float k) { return { a.r * k, a.g * k, a.b * k }; }

inline RGB rustAt (float x, float y, float rustBias, int seed)
{
    const float n      = fbm (x / 170.0f, y / 170.0f, seed, 4);
    const float detail = fbm (x / 36.0f, y / 36.0f, seed + 7, 3);
    const float fine   = vnoise (x / 7.0f, y / 7.0f, seed + 13);
    const float mask   = smoothstep (0.45f, 0.62f, n + (detail - 0.5f) * 0.35f + rustBias);
    RGB base = mix (rgb (pal::ironDark), rgb (pal::iron), detail);
    RGB r    = mix (rgb (pal::rustDeep), rgb (pal::rust), fbm (x / 20.0f, y / 20.0f, seed + 3, 2));
    if (fine > 0.72f) r = mix (r, rgb (pal::rustLight), (fine - 0.72f) * 2.5f);
    return mix (base, r, mask);
}

// Paint-chip a rendered image: knock out chunks of alpha with noise, plus grain.
inline void distress (juce::Image& img, float scale, float wear, int seed)
{
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
        {
            auto c = bd.getPixelColour (x, y);
            if (c.getAlpha() == 0) continue;
            const float n = fbm ((float) x / (7.0f * scale), (float) y / (7.0f * scale), seed, 3);
            const float chip = n < wear ? 0.08f : 1.0f;
            const float grain = 0.7f + 0.3f * hash (x, y, seed + 99);
            bd.setPixelColour (x, y, c.withMultipliedAlpha (chip * grain));
        }
}

inline juce::Font stencil (float size) { return juce::Font (juce::FontOptions ("Impact", size, juce::Font::plain)); }
inline juce::Font plainBold (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }

inline juce::Image distressedText (const juce::String& text, int w, int h, juce::Font font, juce::Colour col,
                                   juce::Justification just, float scale, float wear, int seed, float kerning = 0.0f)
{
    juce::Image img (juce::Image::ARGB, juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale), true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.setColour (col);
        g.setFont (font.withExtraKerningFactor (kerning));
        g.drawFittedText (text, 0, 0, w, h, just, 1, 0.55f);   // squeeze rather than clip if the font runs wide
    }
    distress (img, scale, wear, seed);
    return img;
}
} // namespace tex

//==============================================================================
// The Castle Danger crest: a castle silhouette under a blood moon, on a riveted iron medallion.
static void drawCastleCrest (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;

    // iron ring
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (r.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c), c.x - R * 0.6f, c.y - R * 0.8f, juce::Colour (0xff1a1410), c.x + R, c.y + R, true));
    g.fillEllipse (r);
    g.setColour (pal::rust.withAlpha (0.55f)); g.drawEllipse (r.reduced (1.0f), 1.6f);
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i / 8.0f * juce::MathConstants<float>::twoPi + 0.39f;
        const float rx = c.x + std::cos (a) * R * 0.88f, ry = c.y + std::sin (a) * R * 0.88f;
        g.setColour (juce::Colour (0xff8a7a68)); g.fillEllipse (rx - 1.8f, ry - 1.8f, 3.6f, 3.6f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawEllipse (rx - 1.8f, ry - 1.8f, 3.6f, 3.6f, 0.6f);
    }

    // night sky + blood moon
    const auto inner = r.reduced (R * 0.2f);
    const float ir = inner.getWidth() * 0.5f;
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addEllipse (inner);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a0c08), c.x, inner.getY(), juce::Colour (0xff080505), c.x, inner.getBottom(), false));
    g.fillEllipse (inner);
    const juce::Rectangle<float> moon (c.x - ir * 0.55f, inner.getY() + ir * 0.12f, ir * 1.1f, ir * 1.1f);
    juce::DropShadow (pal::signalHot.withAlpha (0.55f), (int) (ir * 0.45f), {}).drawForPath (g, [&] { juce::Path p; p.addEllipse (moon); return p; }());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff7a4a), moon.getX() + moon.getWidth() * 0.35f, moon.getY() + moon.getHeight() * 0.3f,
                                             juce::Colour (0xff8a1408), moon.getRight(), moon.getBottom(), true));
    g.fillEllipse (moon);
    g.setColour (juce::Colour (0x40400000));
    g.fillEllipse (moon.getX() + moon.getWidth() * 0.55f, moon.getY() + moon.getHeight() * 0.25f, moon.getWidth() * 0.16f, moon.getWidth() * 0.16f);
    g.fillEllipse (moon.getX() + moon.getWidth() * 0.25f, moon.getY() + moon.getHeight() * 0.55f, moon.getWidth() * 0.12f, moon.getWidth() * 0.12f);

    // castle silhouette
    const float x0 = inner.getX(), y0 = inner.getY(), w = inner.getWidth(), h = inner.getHeight();
    auto X = [&] (float f) { return x0 + f * w; };
    auto Y = [&] (float f) { return y0 + f * h; };
    juce::Path castle;
    auto tower = [&] (float l, float rgt, float top, int merlons)
    {
        castle.addRectangle (X (l), Y (top), X (rgt) - X (l), Y (1.05f) - Y (top));
        const float mw = (rgt - l) / (float) (merlons * 2 - 1);
        for (int i = 0; i < merlons; ++i)
            castle.addRectangle (X (l + (float) i * 2.0f * mw), Y (top - 0.06f), X (l + mw) - X (l), Y (0.06f) - Y (0.0f) + 0.5f);
    };
    tower (0.08f, 0.30f, 0.50f, 3);           // left tower
    tower (0.70f, 0.92f, 0.50f, 3);           // right tower
    tower (0.38f, 0.62f, 0.36f, 3);           // keep
    tower (0.30f, 0.70f, 0.64f, 5);           // curtain wall
    castle.addTriangle (X (0.08f), Y (0.44f), X (0.19f), Y (0.27f), X (0.30f), Y (0.44f));   // spires
    castle.addTriangle (X (0.70f), Y (0.44f), X (0.81f), Y (0.27f), X (0.92f), Y (0.44f));
    castle.setUsingNonZeroWinding (true);
    g.setColour (juce::Colour (0xff070505)); g.fillPath (castle);
    // gate + lit windows
    juce::Path gate;
    gate.addRectangle (X (0.44f), Y (0.84f), X (0.56f) - X (0.44f), Y (0.2f) - Y (0.0f));
    gate.addEllipse (X (0.44f), Y (0.79f), X (0.56f) - X (0.44f), Y (0.1f) - Y (0.0f));
    g.setColour (juce::Colour (0xff2a0c06)); g.fillPath (gate);
    g.setColour (pal::signalHot.withAlpha (0.9f));
    for (auto p : { juce::Point<float> (0.19f, 0.62f), { 0.81f, 0.62f }, { 0.5f, 0.5f } })
        g.fillRect (X (p.x) - w * 0.018f, Y (p.y), w * 0.036f, h * 0.07f);
}
//==============================================================================
DeadAirLookAndFeel::DeadAirLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, pal::paintDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, pal::rust.withAlpha (0.6f));
    setColour (juce::Label::textColourId, pal::paintDim);
    setColour (juce::ComboBox::textColourId, pal::paint);
    setColour (juce::ComboBox::arrowColourId, pal::signal);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1411));
    setColour (juce::PopupMenu::textColourId, pal::paint);
    setColour (juce::PopupMenu::headerTextColourId, pal::rustLight);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::rust);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::textColourOffId, pal::paint);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0f0b09));
    setColour (juce::TextEditor::textColourId, pal::paint);
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff5a3420));
    setColour (juce::TextEditor::focusedOutlineColourId, pal::rustLight);
    setColour (juce::TextEditor::highlightColourId, pal::rust.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, pal::signalHot);

    rustDisc = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Image::BitmapData bd (rustDisc, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
        {
            const float n = tex::fbm ((float) x / 22.0f, (float) y / 22.0f, 501, 4);
            const float a = tex::smoothstep (0.5f, 0.7f, n);
            const auto c = tex::rustAt ((float) x * 2.0f, (float) y * 2.0f, 0.3f, 777);
            bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (c.r, c.g, c.b, a * 0.85f));
        }
}


void DeadAirLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.translated (1.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (down ? pal::rustDeep : juce::Colour (0xff3a2a20), 0, r.getY(),
                                             down ? juce::Colour (0xff2a120a) : juce::Colour (0xff17110e), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (hi ? pal::rustLight : juce::Colour (0xff6a3a20)); g.drawRoundedRectangle (r, 3.0f, 1.2f);
}


void DeadAirLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = juce::jmax (2.5f, r * 0.11f);
    const float alpha = s.isEnabled() ? 1.0f : 0.3f;

    // tick marks stamped round the dial
    g.setColour (pal::paintDim.withAlpha (0.45f * alpha));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle) - juce::MathConstants<float>::halfPi;
        const float r1 = r + 1.0f, r0 = r - (i % 5 == 0 ? 5.0f : 3.0f);
        g.drawLine (c.x + std::cos (a) * r0, c.y + std::sin (a) * r0, c.x + std::cos (a) * r1, c.y + std::sin (a) * r1, i % 5 == 0 ? 1.4f : 0.9f);
    }

    const float ar = r - thick - 4.0f;
    juce::Path track; track.addCentredArc (c.x, c.y, ar, ar, 0, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff120e0c).withAlpha (alpha)); g.strokePath (track, juce::PathStrokeType (thick + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff3a2418).withAlpha (alpha)); g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // bipolar knobs (GAIN, DEPTH) sweep out from zero
    float from = startAngle;
    if (s.getProperties().contains ("bip")) from = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);
    juce::Path val; val.addCentredArc (c.x, c.y, ar, ar, 0, std::min (from, angle), std::max (from, angle), true);
    const auto base = s.getProperties().contains ("col") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["col"]) : pal::signal;
    const auto arcCol = (s.isMouseOverOrDragging() ? base.brighter (0.3f) : base).withAlpha (alpha);
    g.setColour (arcCol.withAlpha (0.25f * alpha));
    g.strokePath (val, juce::PathStrokeType (thick + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (arcCol);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float br = ar - thick * 1.4f;
    if (br < 4.0f) return;
    const juce::Rectangle<float> body (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillEllipse (body.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c).withAlpha (alpha), c.x - br * 0.5f, c.y - br * 0.7f,
                                             juce::Colour (0xff17120f), c.x + br * 0.6f, c.y + br, true));
    g.fillEllipse (body);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addEllipse (body);
        g.reduceClipRegion (clip);
        const float off = (float) (((juce::uint64) (juce::pointer_sized_int) &s >> 4) % 64);
        g.setOpacity (0.9f * alpha);
        g.drawImage (rustDisc, body.expanded (br * 0.4f).translated (-off * 0.3f, off * 0.2f), juce::RectanglePlacement::stretchToFit);
    }
    g.setColour (juce::Colour (0xff0f0b09).withAlpha (0.8f));
    for (int i = 0; i < 32; ++i)
    {
        const float a = (float) i / 32.0f * juce::MathConstants<float>::twoPi;
        g.drawLine (c.x + std::cos (a) * br * 0.87f, c.y + std::sin (a) * br * 0.87f,
                    c.x + std::cos (a) * br, c.y + std::sin (a) * br, 1.0f);
    }
    g.setColour (juce::Colour (0xff0b0807)); g.drawEllipse (body, 1.2f);

    juce::Path p; p.addRoundedRectangle (-1.8f, -br * 0.92f, 3.6f, br * 0.6f, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x + 0.8f, c.y + 1.0f));
    g.setColour (pal::paint.withAlpha (alpha));
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

// toggles: an indicator lamp + stencilled label on a rusty plate
void DeadAirLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const bool warn = b.getName() == "listen";
    const auto lit = warn ? pal::hazard : pal::signalHot;
    g.setColour (juce::Colours::black.withAlpha (0.55f)); g.fillRoundedRectangle (r.translated (1.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xff3a1a10) : juce::Colour (0xff2a221d), 0, r.getY(),
                                             juce::Colour (0xff120d0b), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? lit.withAlpha (0.8f) : (hi ? pal::rustLight : juce::Colour (0xff5a3420))); g.drawRoundedRectangle (r, 3.0f, 1.2f);

    const float d = r.getHeight() * 0.42f;
    auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + r.getHeight() * 0.55f, r.getCentreY() });
    if (on)
    {
        juce::Path lp; lp.addEllipse (led);
        juce::DropShadow (lit.withAlpha (0.9f), 10, {}).drawForPath (g, lp);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0c0), led.getCentreX(), led.getCentreY(), lit, led.getX(), led.getY(), true));
    }
    else g.setColour (juce::Colour (0xff2a1c16));
    g.fillEllipse (led);
    g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawEllipse (led, 1.0f);

    g.setColour (on ? pal::paint : pal::paintDim);
    g.setFont (tex::stencil (15.0f).withExtraKerningFactor (0.1f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() * 1.0f).withTrimmedRight (6), juce::Justification::centredLeft);
}

void DeadAirLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool hi, bool)
{
    g.setFont (tex::stencil (juce::jmin (16.0f, (float) b.getHeight() * 0.58f)).withExtraKerningFactor (0.12f));
    g.setColour (hi ? juce::Colours::white : pal::paint);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
// The buzz: a lightning bolt on an iron medallion, struck through by the kill slash.
void BoltMedallion::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const auto c = r.getCentre();
    const float R = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (r.translated (1.5f, 2.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a5a4c), c.x - R * 0.6f, c.y - R * 0.8f, juce::Colour (0xff1a1410), c.x + R, c.y + R, true));
    g.fillEllipse (r);
    g.setColour (pal::rust.withAlpha (0.6f)); g.drawEllipse (r.reduced (1.0f), 1.6f);
    for (int i = 0; i < 8; ++i)
    {
        const float a = (float) i / 8.0f * juce::MathConstants<float>::twoPi + 0.39f;
        const float rx = c.x + std::cos (a) * R * 0.86f, ry = c.y + std::sin (a) * R * 0.86f;
        g.setColour (juce::Colour (0xff8a7a68)); g.fillEllipse (rx - 2.0f, ry - 2.0f, 4.0f, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawEllipse (rx - 2.0f, ry - 2.0f, 4.0f, 4.0f, 0.6f);
    }
    const auto face = r.reduced (R * 0.2f);
    g.setColour (pal::screen); g.fillEllipse (face);
    g.setColour (juce::Colours::black); g.drawEllipse (face, 1.5f);

    // the bolt (hazard yellow, glowing while noise is being heard)
    const float s = face.getWidth();
    auto P = [&] (float fx, float fy) { return juce::Point<float> (face.getX() + fx * s, face.getY() + fy * s); };
    juce::Path bolt;
    bolt.startNewSubPath (P (0.56f, 0.10f)); bolt.lineTo (P (0.28f, 0.54f)); bolt.lineTo (P (0.47f, 0.54f));
    bolt.lineTo (P (0.38f, 0.90f)); bolt.lineTo (P (0.73f, 0.42f)); bolt.lineTo (P (0.53f, 0.42f)); bolt.closeSubPath();
    const bool noisy = state == 0 || state == 2;
    const auto boltCol = noisy ? pal::hazard : pal::hazard.withMultipliedBrightness (0.65f);
    if (noisy) juce::DropShadow (pal::hazard.withAlpha (0.35f + 0.25f * std::sin (phase)), 12, {}).drawForPath (g, bolt);
    g.setColour (boltCol); g.fillPath (bolt);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.strokePath (bolt, juce::PathStrokeType (1.2f));

    // the kill slash
    juce::Path slash;
    slash.startNewSubPath (P (0.16f, 0.20f)); slash.lineTo (P (0.84f, 0.80f));
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.strokePath (slash, juce::PathStrokeType (s * 0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), juce::AffineTransform::translation (1.0f, 1.5f));
    g.setColour (state == 3 ? pal::signalHot : pal::signal); g.strokePath (slash, juce::PathStrokeType (s * 0.11f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
float SpectrumView::xForHz (float hz) const
{
    const auto a = plot();
    const float lo = std::log (30.0f), hi = std::log (16000.0f);
    return a.getX() + (std::log (std::max (hz, 1.0f)) - lo) / (hi - lo) * a.getWidth();
}

float SpectrumView::yForDb (float db) const
{
    const auto a = plot();
    const float top = 0.0f, bottom = -130.0f;
    return a.getY() + juce::jlimit (0.0f, a.getHeight(), (top - db) / (top - bottom) * a.getHeight());
}

void SpectrumView::update()
{
    const auto& t = proc.telemetry();
    bins = std::min (t.numBins.load(), bkill::Telemetry::kMaxBins);
    binHz = t.binHz.load();
    if ((int) in.size() != bins) { in.assign ((size_t) bins, -150.0f); noise.assign ((size_t) bins, -150.0f); out.assign ((size_t) bins, -150.0f); }
    for (int b = 0; b < bins; ++b)
    {
        auto follow = [] (float& v, float target) { v = target > v ? target : v + (target - v) * 0.25f; };
        follow (in[(size_t) b], t.inSpec[b].load());
        follow (out[(size_t) b], t.outSpec[b].load());
        noise[(size_t) b] = t.noiseSpec[b].load();
    }
    repaint();
}

juce::Path SpectrumView::makeCurve (const std::vector<float>& db, bool closed) const
{
    juce::Path p;
    const auto a = plot();
    bool started = false;
    for (float x = 0; x <= a.getWidth(); x += 2.0f)
    {
        const float hz = 30.0f * std::pow (16000.0f / 30.0f, x / a.getWidth());
        const float fb = binHz > 0 ? hz / binHz : 0.0f;
        const int b0 = juce::jlimit (0, std::max (0, bins - 1), (int) fb);
        const int b1 = juce::jlimit (0, std::max (0, bins - 1), b0 + 1);
        const float frac = fb - (float) b0;
        // above a few bins per pixel, take the loudest bin under the pixel
        float v = db.empty() ? -150.0f : db[(size_t) b0] * (1 - frac) + db[(size_t) b1] * frac;
        const float hz2 = 30.0f * std::pow (16000.0f / 30.0f, (x + 2.0f) / a.getWidth());
        for (int b = b1; binHz > 0 && b < bins && b * binHz < hz2; ++b) v = std::max (v, db[(size_t) b]);
        const float px = a.getX() + x, y = yForDb (v);
        if (! started) { if (closed) { p.startNewSubPath (px, a.getBottom()); p.lineTo (px, y); } else p.startNewSubPath (px, y); started = true; }
        else p.lineTo (px, y);
    }
    if (closed) { p.lineTo (a.getRight(), a.getBottom()); p.closeSubPath(); }
    return p;
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto a = plot();
    g.setColour (pal::screen); g.fillRoundedRectangle (full, 4.0f);

    // grid
    g.setFont (tex::plainBold (9.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (hz);
        g.setColour (pal::paintDim.withAlpha (0.07f)); g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (pal::paintDim.withAlpha (0.55f));
        g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz), (int) x - 20, (int) a.getBottom() + 2, 40, 12, juce::Justification::centred);
    }
    for (float db = -20; db >= -120; db -= 20)
    {
        const float y = yForDb (db);
        g.setColour (pal::paintDim.withAlpha (0.07f)); g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        g.setColour (pal::paintDim.withAlpha (0.5f));
        g.drawText (juce::String ((int) db), (int) a.getX() + 3, (int) y - 13, 40, 12, juce::Justification::left);
    }
    // the mains hum lines (50/60 Hz and harmonics), faintly stamped
    g.setColour (pal::signal.withAlpha (0.10f));
    for (float hz : { 50.0f, 60.0f, 100.0f, 120.0f, 150.0f, 180.0f })
        g.drawVerticalLine ((int) xForHz (hz), a.getY(), a.getBottom());

    if (bins > 0)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (a.toNearestInt());
        // learned noise: rust, filled
        auto np = makeCurve (noise, true);
        g.setGradientFill (juce::ColourGradient (pal::signal.withAlpha (0.32f), 0, a.getY(), pal::rustDeep.withAlpha (0.12f), 0, a.getBottom(), false));
        g.fillPath (np);
        g.setColour (pal::signal.withAlpha (0.75f)); g.strokePath (makeCurve (noise, false), juce::PathStrokeType (1.2f));
        // input: dim paint
        g.setColour (pal::paintDim.withAlpha (0.45f)); g.strokePath (makeCurve (in, false), juce::PathStrokeType (1.0f));
        // output: cream, glowing
        auto op = makeCurve (out, false);
        g.setColour (pal::paint.withAlpha (0.16f)); g.strokePath (op, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (pal::paint); g.strokePath (op, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // legend
    g.setFont (tex::plainBold (9.0f).withExtraKerningFactor (0.08f));
    auto key = [&] (float x, juce::Colour col, const juce::String& s)
    {
        g.setColour (col); g.fillRect (x, a.getY() + 7.0f, 12.0f, 3.0f);
        g.setColour (pal::paintDim); g.drawText (s, (int) x + 16, (int) a.getY() + 1, 120, 14, juce::Justification::centredLeft);
    };
    key (a.getRight() - 290, pal::paintDim.withAlpha (0.7f), "IN");
    key (a.getRight() - 240, pal::paint, "OUT (WHAT YOU HEAR)");
    key (a.getRight() - 112, pal::signal, "LEARNED NOISE");

    // CRT glass
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), 0, full.getY(), juce::Colours::transparentWhite, 0, full.getCentreY(), false));
    g.fillRoundedRectangle (full, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f)); g.drawRoundedRectangle (full.reduced (0.5f), 4.0f, 1.2f);
}

//==============================================================================
DeadAirEditor::DeadAirEditor (DeadAirProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (spectrum); spectrum.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (bolt);     bolt.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (release);
    release.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    release.setDoubleClickReturnValue (true, 0.35);
    release.setMouseDragSensitivity (220);
    release.setTooltip ("How fast the gate closes once you stop. NATURAL lets tails fade gently; TIGHT stops palm mutes dead. "
                        "Ringing notes are never cut while they're still above the noise, whatever you set.");
    releaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "release", release);

    addAndMakeVisible (relearnButton);
    relearnButton.setTooltip ("Forget the learned noise and listen again. Leave a second of quiet (nobody playing).");
    relearnButton.onClick = [this] { proc.relearn(); };

    addAndMakeVisible (listenButton);
    listenButton.setName ("listen");
    listenButton.setTooltip ("Hear only what Dead Air takes out. Turn it off before you bounce!");
    listenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "listen", listenButton);

    addAndMakeVisible (rebel);
    setSize (900, 560);
    startTimerHz (30);
}

DeadAirEditor::~DeadAirEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DeadAirEditor::resized()
{
    plates.clear();
    bolt.setBounds (18, 12, 90, 90);
    badgeArea  = { 632, 14, 252, 86 };
    crestArea  = { 640, 19, 76, 76 };
    stripeArea = { 0, 110, getWidth(), 10 };
    linkArea   = { 120, 86, 500, 14 };
    rebel.setBounds (linkArea.getX(), linkArea.getCentreY() - 8, 54, 16);

    plates.push_back ({ "THE AIR BETWEEN NOTES", { 16, 130, 868, 234 } });
    spectrum.setBounds (28, 154, 844, 198);

    statusArea  = { 16, 374, 392, 170 };
    knobArea    = { 418, 374, 200, 170 };
    monitorArea = { 628, 374, 256, 170 };
    plates.push_back ({ "THE GATE", statusArea, true });
    plates.push_back ({ "RELEASE", knobArea });
    plates.push_back ({ "MONITOR", monitorArea });

    release.setBounds (knobArea.getCentreX() - 56, knobArea.getY() + 26, 112, 104);
    relearnButton.setBounds (monitorArea.getX() + 14, monitorArea.getY() + 62, monitorArea.getWidth() - 28, 38);
    listenButton.setBounds (monitorArea.getX() + 14, monitorArea.getY() + 110, monitorArea.getWidth() - 28, 38);

    buildBackdrop();
}

//==============================================================================
void DeadAirEditor::buildBackdrop()
{
    const float scale = 2.0f;
    const int W = getWidth(), H = getHeight();
    if (W <= 0 || H <= 0) return;
    const int PW = (int) ((float) W * scale), PH = (int) ((float) H * scale);
    juce::Image base (juce::Image::RGB, W, H, false);

    std::vector<juce::Rectangle<float>> pr;
    for (auto& s : plates) pr.push_back (s.bounds.toFloat());
    pr.push_back (badgeArea.toFloat());
    const auto stripe = stripeArea.toFloat();

    {
        juce::Image::BitmapData bd (base, juce::Image::BitmapData::writeOnly);
        for (int py = 0; py < H; ++py)
        {
            const float y = (float) py;
            for (int px = 0; px < W; ++px)
            {
                const float x = (float) px;
                tex::RGB col;
                int inside = -1; float edge = 1e9f; bool shadow = false;
                for (int i = 0; i < (int) pr.size(); ++i)
                {
                    const auto& p = pr[(size_t) i];
                    if (p.contains (x, y))
                    {
                        inside = i;
                        edge = std::min ({ x - p.getX(), p.getRight() - x, y - p.getY(), p.getBottom() - y });
                        break;
                    }
                    if (p.translated (3.0f, 4.0f).contains (x, y)) shadow = true;
                }
                if (stripe.contains (x, y))
                {
                    const bool yellow = std::fmod (x + y * 1.0f + 1000.0f, 28.0f) < 14.0f;
                    const float wear = tex::fbm (x / 12.0f, y / 4.0f, 2024, 3);
                    col = yellow ? tex::rgb (pal::hazard) : tex::rgb (juce::Colour (0xff141010));
                    col = tex::mix (col, tex::rustAt (x, y, 0.3f, 99), tex::smoothstep (0.5f, 0.68f, wear));
                    col = tex::mul (col, 0.85f + 0.15f * tex::hash (px, py, 3));
                }
                else if (inside >= 0)
                {
                    const float d = tex::fbm (x / 30.0f, y / 30.0f, 211, 3);
                    tex::RGB steel = tex::mix (tex::rgb (juce::Colour (0xff1e1a17)), tex::rgb (juce::Colour (0xff332c27)), d);
                    const float u = std::fmod (x + y + 2000.0f, 16.0f) / 16.0f, v = std::fmod (x - y + 2000.0f, 16.0f) / 16.0f;
                    const float tread = (std::abs (u - 0.5f) < 0.08f && std::abs (v - 0.25f) < 0.18f) || (std::abs (v - 0.5f) < 0.08f && std::abs (u - 0.75f) < 0.18f) ? 1.0f : 0.0f;
                    steel = tex::mix (steel, tex::rgb (juce::Colour (0xff4a4038)), tread * 0.55f);
                    const float edgeRust = (1.0f - juce::jlimit (0.0f, 1.0f, edge / 14.0f)) * tex::smoothstep (0.35f, 0.6f, tex::vnoise (x / 6.0f, y / 6.0f, 17));
                    const float bloom = tex::smoothstep (0.6f, 0.75f, tex::fbm (x / 55.0f, y / 55.0f, 404, 3)) * 0.7f;
                    col = tex::mix (steel, tex::rustAt (x, y, 0.15f, 1337), juce::jlimit (0.0f, 1.0f, edgeRust + bloom));
                    col = tex::mul (col, 0.88f);
                }
                else
                {
                    col = tex::rustAt (x, y, 0.1f, 77);
                    if (shadow) col = tex::mul (col, 0.45f);
                }

                const float streak = tex::fbm (x / 9.0f, y / 240.0f, 909, 3);
                const float drip = tex::smoothstep (0.58f, 0.78f, streak) * (0.25f + 0.5f * y / (float) H);
                col = tex::mul (col, 1.0f - drip * 0.6f);
                const float runoff = tex::smoothstep (0.66f, 0.82f, tex::fbm (x / 5.0f, y / 120.0f, 31337, 2));
                col = tex::mix (col, tex::rgb (pal::rustDeep), runoff * 0.35f);
                const float oil = tex::smoothstep (0.7f, 0.8f, tex::fbm (x / 70.0f, y / 50.0f, 555, 3));
                col = tex::mul (col, 1.0f - oil * 0.45f);
                if (tex::hash (px, py, 5) > 0.988f) col = tex::mul (col, 0.45f);
                const float grime = 0.72f + 0.28f * tex::fbm (x / 260.0f, y / 260.0f, 77, 2);
                const float dx = (x / (float) W - 0.5f) * 2.0f, dy = (y / (float) H - 0.5f) * 2.0f;
                const float vig = 1.0f - 0.45f * std::min (1.0f, (dx * dx + dy * dy) * 0.6f);
                col = tex::mul (col, grime * vig);
                bd.setPixelColour (px, py, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, col.r), juce::jlimit (0.0f, 1.0f, col.g),
                                                                        juce::jlimit (0.0f, 1.0f, col.b), 1.0f));
            }
        }
    }

    backdrop = juce::Image (juce::Image::RGB, PW, PH, false);
    juce::Graphics g (backdrop);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (base, 0, 0, PW, PH, 0, 0, W, H);
    g.addTransform (juce::AffineTransform::scale (scale));
    juce::Random rnd (2112);

    auto rivet = [&] (float cx, float cy, float rad)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillEllipse (cx - rad + 1, cy - rad + 1.5f, rad * 2, rad * 2);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a7a68), cx - rad * 0.4f, cy - rad * 0.5f, juce::Colour (0xff2a201a), cx + rad, cy + rad, true));
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
        g.setColour (pal::rust.withAlpha (0.55f)); g.drawEllipse (cx - rad, cy - rad, rad * 2, rad * 2, 1.2f);
        g.setColour (pal::rustDeep.withAlpha (0.35f)); g.fillRect (cx - 0.8f, cy + rad, 1.6f, 3.0f + rnd.nextFloat() * 9.0f);
    };
    auto plateFrame = [&] (juce::Rectangle<float> p, bool hot)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawLine (p.getX() + 2, p.getY() + 1, p.getRight() - 2, p.getY() + 1, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f)); g.drawRect (p, 1.0f);
        if (hot) { g.setColour (pal::signal.withAlpha (0.45f)); g.drawRect (p.reduced (2.0f), 1.5f); }
        rivet (p.getX() + 6, p.getY() + 6, 3.0f); rivet (p.getRight() - 6, p.getY() + 6, 3.0f);
        rivet (p.getX() + 6, p.getBottom() - 6, 3.0f); rivet (p.getRight() - 6, p.getBottom() - 6, 3.0f);
    };

    for (int i = 0; i < 300; ++i)
    {
        const float x = rnd.nextFloat() * (float) W, y = rnd.nextFloat() * (float) H;
        const float len = 6.0f + rnd.nextFloat() * 70.0f, ang = rnd.nextFloat() * juce::MathConstants<float>::pi;
        g.setColour ((rnd.nextBool() ? juce::Colour (0xffd0c0a8) : juce::Colours::black).withAlpha (0.04f + rnd.nextFloat() * 0.1f));
        g.drawLine (x, y, x + std::cos (ang) * len, y + std::sin (ang) * len, 0.5f + rnd.nextFloat() * 0.6f);
    }

    int seed = 10;
    for (auto& s : plates)
    {
        const auto p = s.bounds.toFloat();
        plateFrame (p, s.hot);
        auto title = tex::distressedText (s.title, s.bounds.getWidth() - 40, 18, tex::stencil (15.0f), s.hot ? pal::signalHot : pal::paint,
                                          juce::Justification::centredLeft, scale, 0.2f, seed++, 0.12f);
        g.drawImage (title, juce::Rectangle<float> (p.getX() + 14, p.getY() + 3, (float) s.bounds.getWidth() - 40, 18.0f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (0, stripe.getY(), (float) W, stripe.getY(), 1.5f);
    g.drawLine (0, stripe.getBottom(), (float) W, stripe.getBottom(), 1.5f);

    // RELEASE scale: NATURAL ... TIGHT
    {
        auto nat = tex::distressedText ("NATURAL", 80, 14, tex::plainBold (10.0f), pal::paintDim, juce::Justification::centredLeft, scale, 0.15f, 51, 0.14f);
        auto tig = tex::distressedText ("TIGHT", 80, 14, tex::plainBold (10.0f), pal::paintDim, juce::Justification::centredRight, scale, 0.15f, 52, 0.14f);
        g.drawImage (nat, juce::Rectangle<float> ((float) knobArea.getX() + 12, (float) knobArea.getBottom() - 22, 80, 14));
        g.drawImage (tig, juce::Rectangle<float> ((float) knobArea.getRight() - 92, (float) knobArea.getBottom() - 22, 80, 14));
    }

    // CASTLE DANGER MUSIC COMPANY badge with the castle crest
    const auto badge = badgeArea.toFloat();
    plateFrame (badge, false);
    g.setColour (pal::paint.withAlpha (0.22f)); g.drawRect (badge.reduced (5.0f), 1.0f);
    drawCastleCrest (g, crestArea.toFloat());
    const float tx = (float) crestArea.getRight() + 8.0f, tw = badge.getRight() - tx - 10.0f;
    auto l1 = tex::distressedText ("CASTLE DANGER", (int) tw, 28, tex::stencil (25.0f), juce::Colour (0xffeadfc4), juce::Justification::centredLeft, scale, 0.24f, 4242, 0.08f);
    auto l2 = tex::distressedText ("MUSIC COMPANY", (int) tw, 20, tex::stencil (17.0f), pal::rustLight, juce::Justification::centredLeft, scale, 0.24f, 4343, 0.2f);
    auto l3 = tex::distressedText ("WE MAKE MUSIC", (int) tw, 14, tex::plainBold (9.0f), pal::paintDim, juce::Justification::centredLeft, scale, 0.15f, 4444, 0.3f);
    g.setColour (juce::Colours::black);
    g.setOpacity (0.7f); g.drawImage (l1, juce::Rectangle<float> (tx + 1.5f, badge.getY() + 13.0f, tw, 28.0f));
    g.setOpacity (1.0f); g.drawImage (l1, juce::Rectangle<float> (tx, badge.getY() + 11.0f, tw, 28.0f));
    g.setOpacity (0.7f); g.drawImage (l2, juce::Rectangle<float> (tx + 1.5f, badge.getY() + 40.0f, tw, 20.0f));
    g.setOpacity (1.0f); g.drawImage (l2, juce::Rectangle<float> (tx, badge.getY() + 38.0f, tw, 20.0f));
    g.drawImage (l3, juce::Rectangle<float> (tx + 1.0f, badge.getY() + 60.0f, tw, 14.0f));

    // stamped subtitle under the logo
    auto sub = tex::distressedText ("AMP NOISE GATE  //  V" + juce::String (JucePlugin_VersionString),
                                    220, 14, tex::plainBold (10.5f), pal::paintDim, juce::Justification::centredLeft, scale, 0.15f, 99, 0.14f);
    g.drawImage (sub, juce::Rectangle<float> (124.0f, 70.0f, 220.0f, 14.0f));

    // the logo: DEAD in cream, AIR in rusty red, knocked down a little
    {
        const int lw = 420, lh = 64;
        logoImage = juce::Image (juce::Image::ARGB, (int) (lw * scale), (int) (lh * scale), true);
        juce::Graphics lg (logoImage);
        lg.addTransform (juce::AffineTransform::scale (scale));
        auto f = tex::stencil (60.0f).withExtraKerningFactor (0.05f);
        {
            juce::GlyphArrangement all; all.addLineOfText (f, "DEAD AIR", 0, 0);
            const float wAll = all.getBoundingBox (0, -1, true).getWidth();
            if (wAll > (float) lw - 8.0f) f = f.withHeight (60.0f * ((float) lw - 8.0f) / wAll);
        }
        lg.setFont (f);
        lg.setColour (pal::paint);
        lg.drawText ("DEAD", 0, -3, lw, lh, juce::Justification::centredLeft, false);
        juce::GlyphArrangement ga; ga.addLineOfText (f, "DEAD", 0, 0);
        const float bx = ga.getBoundingBox (0, -1, true).getWidth() + f.getHeight() * 0.2f;   // a word space
        {
            juce::Graphics::ScopedSaveState ss (lg);
            lg.addTransform (juce::AffineTransform::rotation (0.05f, bx, (float) lh * 0.5f).translated (0.0f, 4.0f));
            lg.setColour (pal::signal);
            lg.drawText ("AIR", (int) bx, 0, lw - (int) bx, lh, juce::Justification::centredLeft, false);
        }
        tex::distress (logoImage, scale, 0.3f, 667);
    }
}

//==============================================================================
void DeadAirEditor::drawStatus (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& t = proc.telemetry();
    const int st = juce::jlimit (0, 3, t.state.load());
    auto r = area.reduced (18, 0).withTrimmedTop (28).withTrimmedBottom (12);

    const char* names[] = { "LISTENING", "PLAYING", "LETTING GO", "DEAD AIR" };
    const char* lines[] = { "Learning the noise after your amp sim. Leave a second of quiet.",
                            "Gate open: your guitar passes untouched.",
                            "Your note has faded into the noise; closing gently.",
                            "Not playing: gate shut, hum and hiss gone." };
    const juce::Colour stateCol = st == 0 ? pal::hazard : (st == 3 ? pal::signalHot : (st == 1 ? juce::Colour (0xff9aa86a) : pal::rustLight));

    // status lamp + big stencilled state
    auto top = r.removeFromTop (30);
    const float glow = st == 0 ? 0.5f + 0.5f * std::sin (pulse) : 1.0f;
    auto lamp = juce::Rectangle<float> (14, 14).withCentre ({ (float) top.getX() + 8.0f, (float) top.getCentreY() });
    { juce::Path lp; lp.addEllipse (lamp); juce::DropShadow (stateCol.withAlpha (0.85f * glow), 12, {}).drawForPath (g, lp); }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0c0).withAlpha (glow), lamp.getCentreX(), lamp.getCentreY(), stateCol, lamp.getX(), lamp.getY(), true));
    g.fillEllipse (lamp);
    g.setColour (juce::Colours::black.withAlpha (0.7f)); g.drawEllipse (lamp, 1.0f);
    g.setFont (tex::stencil (24.0f).withExtraKerningFactor (0.08f));
    g.setColour (st == 3 ? pal::signalHot : pal::paint);
    g.drawText (names[st], top.withTrimmedLeft (26), juce::Justification::centredLeft);

    g.setFont (tex::plainBold (11.0f));
    g.setColour (pal::paintDim);
    g.drawText (lines[st], r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (8);

    auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour vc)
    {
        auto line = r.removeFromTop (24);
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (line.reduced (0, 2));
        g.setFont (tex::stencil (13.0f).withExtraKerningFactor (0.1f));
        g.setColour (pal::paintDim);
        g.drawText (label, line.removeFromLeft (118).withTrimmedLeft (8), juce::Justification::centredLeft);
        g.setFont (tex::plainBold (12.5f));
        g.setColour (vc);
        g.drawText (value, line, juce::Justification::centredLeft);
    };
    const float floorDb = t.floorDb.load();
    row ("NOISE FLOOR", st == 0 && floorDb <= -119 ? juce::String ("...") : juce::String (floorDb, 1) + " dBFS", pal::paint);
    const float lvl = t.levelDb.load(), ago = t.clampAgo.load();
    juce::String gateText = "-";
    if (st != 0)
    {
        if (ago < 0.6f) gateText = "fast clamp (mute / choke)";
        else if (st == 1) gateText = "open, " + juce::String (juce::roundToInt (std::max (0.0f, lvl))) + " dB over the noise";
        else if (st == 2) gateText = "closing gently";
        else gateText = "shut";
    }
    row ("GATE", gateText, ago < 0.6f ? pal::signalHot : pal::paint);
    juce::String cut = "-";
    if (st == 3 || removedSmooth > 60.0f) cut = "all of it";
    else if (st != 0 && removedSmooth < 3.0f) cut = "none (gate open)";
    else if (st != 0) cut = "-" + juce::String (juce::roundToInt (removedSmooth)) + " dB";
    row ("NOISE CUT", cut, pal::hazard);
}

void DeadAirEditor::paint (juce::Graphics& g)
{
    if (backdrop.isValid()) g.drawImage (backdrop, getLocalBounds().toFloat());
    else g.fillAll (pal::ironDark);

    if (logoImage.isValid())
    {
        const float alpha = juce::jlimit (0.45f, 1.0f, 1.0f - flicker * 0.6f);
        const auto la = juce::Rectangle<float> (120.0f, 8.0f, 420.0f, 64.0f);
        g.setColour (juce::Colours::black);
        g.setOpacity (0.65f); g.drawImage (logoImage, la.translated (2.5f, 3.5f));
        g.setOpacity (alpha); g.drawImage (logoImage, la);
        g.setOpacity (1.0f);
    }

    drawStatus (g, statusArea);

    // RELEASE value
    {
        const auto v = release.getTextFromValue (release.getValue());
        auto vr = juce::Rectangle<int> (knobArea.getCentreX() - 50, knobArea.getBottom() - 42, 100, 18);
        g.setColour (juce::Colour (0x90000000)); g.fillRoundedRectangle (vr.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff3a2418)); g.drawRoundedRectangle (vr.toFloat(), 2.0f, 1.0f);
        g.setColour (pal::paint); g.setFont (tex::plainBold (11.5f));
        g.drawText (v, vr, juce::Justification::centred);
    }

    // MONITOR: latency note
    {
        const double ms = 1000.0 * proc.getLatencySamples() / std::max (1.0, proc.getRateForDisplay());
        g.setFont (tex::plainBold (10.5f));
        g.setColour (pal::paintDim);
        g.drawFittedText (juce::String (juce::roundToInt (ms)) + " ms look-ahead: play through it live. Your DAW lines it up.",
                          monitorArea.reduced (16, 0).withTrimmedTop (26).withHeight (30), juce::Justification::centredLeft, 2, 0.9f);
    }

    // Castle Danger Link
    {
        auto& lk = proc.getLink();
        const bool linked = lk.masterNearby();
        const bool justSet = lk.getLastNoteTime() > 0 && juce::Time::getMillisecondCounter() - lk.getLastNoteTime() < 60000;
        const bool rebelOn = lk.isRebel();
        auto r = linkArea.toFloat().withTrimmedLeft (60);   // REBEL sits first on the line
        const auto col = rebelOn ? pal::signal : (justSet ? pal::signalHot : (linked ? pal::hazard : pal::paintDim.withAlpha (0.35f)));
        if (linked) { juce::Path lp; lp.addEllipse (r.getX(), r.getCentreY() - 3.5f, 7, 7); juce::DropShadow (col.withAlpha (0.8f), 6, {}).drawForPath (g, lp); }
        g.setColour (col); g.fillEllipse (r.getX(), r.getCentreY() - 3.5f, 7, 7);
        g.setFont (tex::plainBold (9.5f).withExtraKerningFactor (0.1f));
        const auto track = proc.getTrackName();
        g.drawText (rebelOn ? juce::String ("REBEL: THE MASTER KEEPS ITS HANDS OFF")
                    : ! linked ? juce::String ("NOT LINKED TO THE MASTER")
                             : (justSet ? juce::String ("RELEASE SET BY THE MASTER  //  TURN IT ANY TIME")
                                        : "LINKED TO THE MASTER" + (track.isNotEmpty() ? "  //  " + track.toUpperCase() : juce::String())),
                    r.withTrimmedLeft (12).toNearestInt(), juce::Justification::centredLeft, true);
    }
}

void DeadAirEditor::timerCallback()
{
    spectrum.update();
    const auto& t = proc.telemetry();
    removedSmooth += (t.removedDb.load() - removedSmooth) * 0.15f;
    pulse += 0.18f;
    const int st = t.state.load();
    bolt.state = st; bolt.phase = pulse;
    bolt.repaint();
    repaint (statusArea);
    repaint (knobArea);
    if (++linkTick >= 15) { linkTick = 0; repaint (linkArea.expanded (8)); repaint (monitorArea); }

    // the logo flickers now and then, and when the gate slams shut
    auto& rnd = juce::Random::getSystemRandom();
    const float before = flicker;
    const bool slam = st == 3 && lastState != 3 && lastState >= 0;
    flicker = (rnd.nextFloat() < 0.012f || slam) ? 0.4f + 0.6f * rnd.nextFloat() : flicker * 0.5f;
    lastState = st;
    if (std::abs (flicker - before) > 0.01f) repaint (114, 4, 440, 74);
}
