#pragma once
// REBEL: tells The Master to keep its hands off this plugin. Your settings stay exactly as you set
// them; The Master still hears what the plugin does in the mix and plans everything else around it.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace cdrebel
{
inline juce::ParameterID paramId() { return { "rebel", 1 }; }

inline std::unique_ptr<juce::AudioParameterBool> makeParam()
{
    return std::make_unique<juce::AudioParameterBool> (paramId(), "Rebel (The Master hands off)", false);
}

class Button : public juce::ToggleButton
{
public:
    explicit Button (juce::AudioProcessorValueTreeState& s) : att (s, "rebel", *this)
    {
        setTooltip ("REBEL: The Master keeps its hands off this plugin. It won't change any setting here "
                    "(it still hears what it does and plans around it). Turn it off to let The Master run it.");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        const juce::Colour red { 0xffd2452a }, hot { 0xffff6a3d }, cream { 0xffd6c7a6 };
        if (on)
        {
            juce::Path p; p.addRoundedRectangle (r, 2.5f);
            juce::DropShadow (hot.withAlpha (0.6f), 6, {}).drawForPath (g, p);
            g.setColour (down ? hot : red); g.fillRoundedRectangle (r, 2.5f);
        }
        else
        {
            g.setColour (juce::Colour (0xff120d0a).withAlpha (0.85f)); g.fillRoundedRectangle (r, 2.5f);
            g.setColour ((over ? cream : red).withAlpha (over ? 0.8f : 0.6f)); g.drawRoundedRectangle (r, 2.5f, 1.0f);
        }
        g.setColour (on ? juce::Colour (0xff120d0a) : cream.withAlpha (over ? 0.95f : 0.7f));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withExtraKerningFactor (0.18f));
        g.drawText (on && getWidth() >= 100 ? "REBEL: HANDS OFF" : "REBEL", getLocalBounds(), juce::Justification::centred);
    }

    

private:
    juce::AudioProcessorValueTreeState::ButtonAttachment att;
};
} // namespace cdrebel
