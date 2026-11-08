#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class BuzzKillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BuzzKillLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool hi, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool hi, bool down) override;
};

// Live spectrum: the learned noise floor, what comes in, and what gets through
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (BuzzKillProcessor& p) : proc (p) {}
    void update();
    void paint (juce::Graphics&) override;
private:
    float xForHz (float hz) const;
    float yForDb (float db) const;
    juce::Path makeCurve (const std::vector<float>& db, bool closed) const;
    BuzzKillProcessor& proc;
    std::vector<float> in, noise, out;
    float binHz = 0;
    int bins = 0;
    double mainsHz = 0;
    int humLines = 0;
    unsigned humMask = 0;
};

class BuzzKillEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BuzzKillEditor (BuzzKillProcessor&);
    ~BuzzKillEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }   // used by the headless snapshot test

private:
    void timerCallback() override;
    void buildBackground();
    void drawStatus (juce::Graphics&, juce::Rectangle<int>);

    BuzzKillProcessor& proc;
    BuzzKillLookAndFeel lnf;
    SpectrumView spectrum;
    juce::Slider release { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton studioButton { "STUDIO" }, liveButton { "LIVE" }, listenButton { "HEAR REMOVED" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAtt;
    juce::Image background;
    juce::Rectangle<int> statusArea, knobArea, modeArea, crestArea;
    float removedSmooth = 0, pulse = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuzzKillEditor)
};
