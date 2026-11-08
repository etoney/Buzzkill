#include "PluginEditor.h"

namespace pal
{
const juce::Colour bg      { 0xff070a0b };
const juce::Colour panel   { 0xd00b1113 };
const juce::Colour edge    { 0xff1d3034 };
const juce::Colour ghost   { 0xff5ef2d6 };   // phosphor teal
const juce::Colour ghostDim{ 0xff2b8f80 };
const juce::Colour bone    { 0xffe2ddd2 };
const juce::Colour ash     { 0xff7d8a89 };
const juce::Colour hum     { 0xffff5a3c };
}

static juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
// Castle Danger Music Company crest: a lighthouse throwing two beams, inside a ring
static void drawLighthouseCrest (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink, juce::Colour light, float beamPhase)
{
    const float s = std::min (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const float R = s * 0.48f;

    // ring
    g.setColour (ink.withAlpha (0.18f)); g.fillEllipse (c.x - R, c.y - R, R * 2, R * 2);
    g.setColour (ink.withAlpha (0.9f));  g.drawEllipse (c.x - R, c.y - R, R * 2, R * 2, s * 0.03f);

    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip; clip.addEllipse (c.x - R + 1, c.y - R + 1, R * 2 - 2, R * 2 - 2);
    g.reduceClipRegion (clip);

    const float lampY = c.y - s * 0.20f;
    // beams
    for (int side = -1; side <= 1; side += 2)
    {
        const float spread = s * (0.16f + 0.04f * std::sin (beamPhase + (float) side));
        juce::Path beam;
        beam.startNewSubPath (c.x, lampY);
        beam.lineTo (c.x + side * R * 1.1f, lampY - spread);
        beam.lineTo (c.x + side * R * 1.1f, lampY + spread * 0.7f);
        beam.closeSubPath();
        g.setGradientFill (juce::ColourGradient (light.withAlpha (0.75f), c.x, lampY, light.withAlpha (0.0f), c.x + side * R, lampY, false));
        g.fillPath (beam);
    }
    // ground / rocks
    g.setColour (ink.withAlpha (0.9f));
    g.fillRect (c.x - R, c.y + s * 0.30f, R * 2, R);

    // tower (tapered, banded)
    const float baseW = s * 0.26f, topW = s * 0.15f, topY = c.y - s * 0.12f, baseY = c.y + s * 0.31f;
    juce::Path tower;
    tower.startNewSubPath (c.x - baseW * 0.5f, baseY); tower.lineTo (c.x - topW * 0.5f, topY);
    tower.lineTo (c.x + topW * 0.5f, topY); tower.lineTo (c.x + baseW * 0.5f, baseY); tower.closeSubPath();
    g.setColour (ink); g.fillPath (tower);
    {
        juce::Graphics::ScopedSaveState s2 (g);
        g.reduceClipRegion (tower);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        for (int i = 0; i < 3; ++i)
        {
            const float y = topY + (baseY - topY) * (0.18f + i * 0.30f);
            g.fillRect (c.x - baseW, y, baseW * 2, (baseY - topY) * 0.13f);
        }
    }
    // gallery, lantern room, dome
    g.setColour (ink);
    g.fillRect (c.x - topW * 0.75f, topY - s * 0.025f, topW * 1.5f, s * 0.03f);
    g.setColour (light); g.fillRect (c.x - topW * 0.33f, lampY - s * 0.035f, topW * 0.66f, s * 0.075f);
    g.setColour (light.withAlpha (0.35f)); g.fillEllipse (c.x - s * 0.08f, lampY - s * 0.08f, s * 0.16f, s * 0.16f);
    g.setColour (ink);
    juce::Path dome; dome.addPieSegment (c.x - topW * 0.45f, lampY - s * 0.09f, topW * 0.9f, s * 0.11f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, 0.0f);
    g.fillPath (dome);
    g.fillRect (c.x - 0.6f, lampY - s * 0.12f, 1.2f, s * 0.04f);
}

//==============================================================================
BuzzKillLookAndFeel::BuzzKillLookAndFeel()
{
    setColour (juce::TextButton::textColourOffId, pal::ash);
    setColour (juce::TextButton::textColourOnId, pal::bg);
}

void BuzzKillLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thick = std::max (3.0f, r * 0.09f);
    const bool hot = s.isMouseOverOrDragging();

    // tick ring
    for (int i = 0; i <= 20; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 20.0f;
        const bool lit = (float) i / 20.0f <= pos + 0.001f;
        const float r0 = r - thick * 0.2f, r1 = r - thick * (i % 5 == 0 ? 2.2f : 1.4f);
        g.setColour (lit ? pal::ghost.withAlpha (hot ? 1.0f : 0.85f) : pal::edge);
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), i % 5 == 0 ? 2.0f : 1.3f);
    }
    // body
    const float br = r - thick * 3.0f;
    g.setColour (pal::ghost.withAlpha (hot ? 0.16f : 0.08f));
    g.fillEllipse (c.x - br - 6, c.y - br - 6, (br + 6) * 2, (br + 6) * 2);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff253033), c.x - br * 0.5f, c.y - br, juce::Colour (0xff070a0b), c.x + br * 0.4f, c.y + br, true));
    g.fillEllipse (c.x - br, c.y - br, br * 2, br * 2);
    g.setColour (juce::Colours::black); g.drawEllipse (c.x - br, c.y - br, br * 2, br * 2, 1.5f);
    g.setColour (pal::edge.brighter (0.3f)); g.drawEllipse (c.x - br + 2, c.y - br + 2, br * 2 - 4, br * 2 - 4, 0.8f);
    // pointer
    juce::Path p; p.addRoundedRectangle (-2.0f, -br * 0.92f, 4.0f, br * 0.5f, 2.0f);
    g.setColour (pal::ghost.withAlpha (0.35f));
    g.fillPath (p, juce::AffineTransform::scale (2.2f, 1.05f).rotated (angle).translated (c));
    g.setColour (pal::ghost); g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
}

void BuzzKillLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const juce::Colour accent = b.getProperties().getWithDefault ("hot", false) ? pal::hum : pal::ghost;
    if (on)
    {
        g.setColour (accent.withAlpha (0.25f)); g.fillRoundedRectangle (r.expanded (1.0f), 5.0f);
        g.setColour (accent.withAlpha (down ? 0.75f : 0.9f)); g.fillRoundedRectangle (r, 4.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff0c1214)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (hi ? accent.withAlpha (0.7f) : pal::edge.brighter (0.2f)); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
}

void BuzzKillLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (font (12.5f, true).withExtraKerningFactor (0.12f));
    g.setColour (b.getToggleState() ? pal::bg : pal::bone.withAlpha (0.85f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
float SpectrumView::xForHz (float hz) const
{
    const float lo = std::log (30.0f), hi = std::log (16000.0f);
    return (std::log (std::max (hz, 1.0f)) - lo) / (hi - lo) * (float) getWidth();
}

float SpectrumView::yForDb (float db) const
{
    const float top = 0.0f, bottom = -130.0f;
    return juce::jlimit (0.0f, (float) getHeight(), (top - db) / (top - bottom) * (float) getHeight());
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
    const float w = (float) getWidth(), h = (float) getHeight();
    bool started = false;
    for (float x = 0; x <= w; x += 2.0f)
    {
        const float hz = 30.0f * std::pow (16000.0f / 30.0f, x / w);
        const float fb = binHz > 0 ? hz / binHz : 0.0f;
        const int b0 = juce::jlimit (0, std::max (0, bins - 1), (int) fb);
        const int b1 = juce::jlimit (0, std::max (0, bins - 1), b0 + 1);
        const float frac = fb - (float) b0;
        // above a few bins per pixel, take the loudest bin under the pixel
        float v = db.empty() ? -150.0f : db[(size_t) b0] * (1 - frac) + db[(size_t) b1] * frac;
        const float hz2 = 30.0f * std::pow (16000.0f / 30.0f, (x + 2.0f) / w);
        for (int b = b1; binHz > 0 && b < bins && b * binHz < hz2; ++b) v = std::max (v, db[(size_t) b]);
        const float y = yForDb (v);
        if (! started) { if (closed) { p.startNewSubPath (0, h); p.lineTo (x, y); } else p.startNewSubPath (x, y); started = true; }
        else p.lineTo (x, y);
    }
    if (closed) { p.lineTo (w, h); p.closeSubPath(); }
    return p;
}

void SpectrumView::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setColour (juce::Colour (0xff050809)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // grid
    g.setFont (font (10.0f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (hz);
        g.setColour (pal::edge.withAlpha (0.6f)); g.drawVerticalLine ((int) x, 0, h);
        g.setColour (pal::ash.withAlpha (0.6f));
        g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + "k" : juce::String ((int) hz), (int) x + 3, (int) h - 15, 40, 12, juce::Justification::left);
    }
    for (float db = -20; db >= -120; db -= 20)
    {
        const float y = yForDb (db);
        g.setColour (pal::edge.withAlpha (0.45f)); g.drawHorizontalLine ((int) y, 0, w);
        g.setColour (pal::ash.withAlpha (0.45f));
        g.drawText (juce::String ((int) db), 4, (int) y - 13, 40, 12, juce::Justification::left);
    }

    if (bins > 0)
    {
        // learned noise floor
        auto np = makeCurve (noise, true);
        g.setColour (pal::hum.withAlpha (0.10f)); g.fillPath (np);
        g.setColour (pal::hum.withAlpha (0.45f)); g.strokePath (makeCurve (noise, false), juce::PathStrokeType (1.2f));
        // input
        g.setColour (pal::ash.withAlpha (0.45f)); g.strokePath (makeCurve (in, false), juce::PathStrokeType (1.0f));
        // output (what gets through)
        auto op = makeCurve (out, false);
        g.setColour (pal::ghost.withAlpha (0.18f)); g.strokePath (op, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (pal::ghost); g.strokePath (op, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }


    // legend
    auto leg = juce::Rectangle<float> (w - 230, 8, 220, 14);
    auto item = [&] (juce::Colour c, const juce::String& text, float width)
    {
        auto r = leg.removeFromLeft (width);
        g.setColour (c); g.fillRect (r.getX(), r.getCentreY() - 1, 14.0f, 2.0f);
        g.setColour (pal::ash); g.drawText (text, r.withTrimmedLeft (18), juce::Justification::centredLeft);
    };
    g.setFont (font (10.5f));
    item (pal::ash.withAlpha (0.8f), "IN", 44);
    item (pal::ghost, "OUT", 54);
    item (pal::hum.withAlpha (0.8f), "LEARNED NOISE", 112);

    g.setColour (pal::edge); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

//==============================================================================
BuzzKillEditor::BuzzKillEditor (BuzzKillProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (spectrum);

    addAndMakeVisible (release);
    release.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    release.setDoubleClickReturnValue (true, 0.35);
    releaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "release", release);

    addAndMakeVisible (relearnButton);
    relearnButton.setTooltip ("Forget the learned noise and listen again (leave a second of quiet)");
    relearnButton.onClick = [this] { proc.relearn(); };

    addAndMakeVisible (listenButton);
    listenButton.setClickingTogglesState (true);
    listenButton.getProperties().set ("hot", true);
    listenAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "listen", listenButton);

    setSize (780, 480);
    startTimerHz (30);
}

BuzzKillEditor::~BuzzKillEditor() { setLookAndFeel (nullptr); }

void BuzzKillEditor::resized()
{
    spectrum.setBounds (20, 100, getWidth() - 40, 220);
    statusArea = { 20, 336, 300, 124 };
    knobArea = { 336, 336, 196, 124 };
    monitorArea = { 548, 336, getWidth() - 568, 124 };
    crestArea = { getWidth() - 270, 14, 250, 72 };
    release.setBounds (knobArea.getCentreX() - 50, knobArea.getY() + 20, 100, 92);
    relearnButton.setBounds (monitorArea.getX() + 14, monitorArea.getY() + 50, monitorArea.getWidth() - 28, 28);
    listenButton.setBounds (monitorArea.getX() + 14, monitorArea.getY() + 86, monitorArea.getWidth() - 28, 28);
    buildBackground();
}

void BuzzKillEditor::buildBackground()
{
    background = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (background);
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e1416), 0, 0, pal::bg, 0, h, false));
    g.fillAll();

    // TV static speckle + scanlines (dead air)
    juce::Random rnd (1337);
    for (int i = 0; i < 9000; ++i)
    {
        g.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.035f));
        g.fillRect (rnd.nextFloat() * w, rnd.nextFloat() * h, 1.0f, 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    for (int y = 0; y < getHeight(); y += 3) g.drawHorizontalLine (y, 0, w);
    // vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, w * 0.5f, h * 0.45f, juce::Colours::black.withAlpha (0.55f), 0, 0, true));
    g.fillAll();

    // panels
    for (auto r : { statusArea, knobArea, monitorArea })
    {
        g.setColour (pal::panel); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (pal::edge); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    // title
    g.setFont (font (50.0f, true).withExtraKerningFactor (0.10f));
    g.setColour (pal::ghost.withAlpha (0.12f));
    for (int dx = -2; dx <= 2; dx += 2) g.drawText ("BUZZKILL", 24 + dx, 14, 420, 56, juce::Justification::centredLeft);
    g.setColour (pal::bone);
    g.drawText ("BUZZKILL", 24, 14, 420, 56, juce::Justification::centredLeft);
    g.setFont (font (11.5f, true).withExtraKerningFactor (0.18f));
    g.setColour (pal::ghostDim);
    g.drawText ("DYNAMIC GUITAR NOISE GATE  //  V" + juce::String (JucePlugin_VersionString), 28, 68, 420, 16, juce::Justification::centredLeft);

    // crest + company
    auto cr = crestArea.toFloat();
    drawLighthouseCrest (g, cr.removeFromLeft (72).reduced (2), pal::bone, pal::ghost, 0.0f);
    cr.removeFromLeft (10);
    g.setColour (pal::bone);
    g.setFont (font (14.0f, true).withExtraKerningFactor (0.14f));
    g.drawText ("CASTLE DANGER", cr.removeFromTop (24).withTrimmedTop (6), juce::Justification::bottomLeft);
    g.setFont (font (11.0f, true).withExtraKerningFactor (0.2f));
    g.setColour (pal::ash);
    g.drawText ("MUSIC COMPANY", cr.removeFromTop (18), juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::italic)));
    g.setColour (pal::ghostDim);
    g.drawText ("We make music", cr.removeFromTop (18), juce::Justification::topLeft);

    // static labels
    g.setFont (font (11.0f, true).withExtraKerningFactor (0.18f));
    g.setColour (pal::ash);
    g.drawText ("RELEASE", knobArea.withHeight (24).translated (0, 4), juce::Justification::centred);
    g.drawText ("MONITOR", monitorArea.getX() + 14, monitorArea.getY() + 8, 100, 18, juce::Justification::centredLeft);
    g.setFont (font (10.0f, true).withExtraKerningFactor (0.12f));
    g.drawText ("NATURAL", knobArea.getX() + 10, knobArea.getBottom() - 24, 70, 14, juce::Justification::centredLeft);
    g.drawText ("TIGHT", knobArea.getRight() - 80, knobArea.getBottom() - 24, 70, 14, juce::Justification::centredRight);
}

void BuzzKillEditor::drawStatus (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& t = proc.telemetry();
    const int st = t.state.load();
    auto r = area.reduced (16, 10);

    const char* names[] = { "LISTENING", "PLAYING", "LETTING GO", "BUZZ KILLED" };
    const char* lines[] = { "Learning the noise after your amp sim. Leave a second of quiet.",
                            "Gate open: your signal passes untouched.",
                            "Your note has faded into the noise; closing gently.",
                            "Not playing: gate closed, noise gone." };
    const juce::Colour stateCol = st == 0 ? pal::hum : (st == 3 ? pal::ash : pal::ghost);

    // status LED
    auto top = r.removeFromTop (26);
    const float glow = st == 0 ? 0.5f + 0.5f * std::sin (pulse) : 1.0f;
    g.setColour (stateCol.withAlpha (0.25f * glow)); g.fillEllipse (top.getX() - 3.0f, top.getCentreY() - 9.0f, 18, 18);
    g.setColour (stateCol.withAlpha (0.6f + 0.4f * glow)); g.fillEllipse (top.getX() + 1.0f, top.getCentreY() - 5.0f, 10, 10);
    g.setFont (font (17.0f, true).withExtraKerningFactor (0.14f));
    g.setColour (pal::bone);
    g.drawText (names[juce::jlimit (0, 3, st)], top.withTrimmedLeft (22), juce::Justification::centredLeft);

    g.setFont (font (11.5f));
    g.setColour (pal::ash);
    g.drawText (lines[juce::jlimit (0, 3, st)], r.removeFromTop (18), juce::Justification::centredLeft);
    r.removeFromTop (8);

    auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour vc)
    {
        auto line = r.removeFromTop (19);
        g.setFont (font (10.5f, true).withExtraKerningFactor (0.14f));
        g.setColour (pal::ash.withAlpha (0.8f));
        g.drawText (label, line.removeFromLeft (100), juce::Justification::centredLeft);
        g.setFont (font (13.0f, true));
        g.setColour (vc);
        g.drawText (value, line, juce::Justification::centredLeft);
    };
    const float floorDb = t.floorDb.load();
    row ("NOISE FLOOR", st == 0 && floorDb <= -119 ? juce::String ("...") : juce::String (floorDb, 1) + " dBFS", pal::bone);
    const float lvl = t.levelDb.load(), ago = t.clampAgo.load();
    juce::String gateText = "-";
    if (st != 0)
    {
        if (ago < 0.6f) gateText = "fast clamp (mute / choke)";
        else if (st == 1) gateText = "open, " + juce::String (juce::roundToInt (std::max (0.0f, lvl))) + " dB over noise";
        else if (st == 2) gateText = "closing gently";
        else gateText = "closed";
    }
    row ("GATE", gateText, ago < 0.6f ? pal::hum : pal::bone);
    juce::String cut = "-";
    if (st == 3 || removedSmooth > 60.0f) cut = "all of it";
    else if (st != 0 && removedSmooth < 3.0f) cut = "none (gate open)";
    else if (st != 0) cut = "-" + juce::String (juce::roundToInt (removedSmooth)) + " dB";
    row ("NOISE CUT", cut, pal::ghost);
}

void BuzzKillEditor::paint (juce::Graphics& g)
{
    if (background.isValid()) g.drawImageAt (background, 0, 0);
    drawStatus (g, statusArea);

    g.setFont (font (11.0f));
    g.setColour (pal::ash);
    const double ms = 1000.0 * proc.getLatencySamples() / std::max (1.0, proc.getRateForDisplay());
    auto info = monitorArea.reduced (14, 0).withTrimmedTop (28).withHeight (18);
    g.drawText (juce::String (juce::roundToInt (ms)) + " ms latency: play through it live", info, juce::Justification::centredLeft);
    g.setFont (font (11.5f, true));
    g.setColour (pal::bone);
    g.drawText (release.getTextFromValue (release.getValue()), knobArea.getX(), knobArea.getBottom() - 25, knobArea.getWidth(), 16, juce::Justification::centred);
}

void BuzzKillEditor::timerCallback()
{
    spectrum.update();
    const auto& t = proc.telemetry();
    removedSmooth += (t.removedDb.load() - removedSmooth) * 0.15f;
    pulse += 0.18f;
    repaint (statusArea);
    repaint (monitorArea);
    repaint (knobArea);
}
