#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Rebel.h"

class DeadAirLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DeadAirLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hi, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool hi, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool hi, bool down) override;
    juce::Image rustDisc;
};

// Live spectrum on a CRT behind the iron: the learned noise, what comes in, and what gets through
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (DeadAirProcessor& p) : proc (p) {}
    void update();
    void paint (juce::Graphics&) override;
private:
    float xForHz (float hz) const;
    float yForDb (float db) const;
    juce::Path makeCurve (const std::vector<float>& db, bool closed) const;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedBottom (14.0f); }
    DeadAirProcessor& proc;
    std::vector<float> in, noise, out;
    float binHz = 0;
    int bins = 0;
};

// The badge on the left of the logo: a lightning bolt (the buzz) with a kill slash; sparks while open
class BoltMedallion : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    int state = 0;      // engine state: 0 listening, 1 playing, 2 letting go, 3 closed
    float phase = 0;
};

class DeadAirEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DeadAirEditor (DeadAirProcessor&);
    ~DeadAirEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }   // used by the headless snapshot test

private:
    struct Plate { juce::String title; juce::Rectangle<int> bounds; bool hot = false; };

    void timerCallback() override;
    void buildBackdrop();
    void drawStatus (juce::Graphics&, juce::Rectangle<int>);

    DeadAirProcessor& proc;
    DeadAirLookAndFeel lnf;
    SpectrumView spectrum;
    BoltMedallion bolt;
    juce::Slider release { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::ToggleButton listenButton { "HEAR REMOVED" };
    juce::TextButton relearnButton { "RELEARN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAtt;
    juce::TooltipWindow tooltips { this, 700 };

    std::vector<Plate> plates;
    juce::Image backdrop, logoImage;
    juce::Rectangle<int> statusArea, knobArea, monitorArea, badgeArea, crestArea, stripeArea, linkArea;
    float removedSmooth = 0, pulse = 0, flicker = 0;
    int linkTick = 0, lastState = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeadAirEditor)
    cdrebel::Button rebel { proc.apvts };   // REBEL: The Master hands off
};
