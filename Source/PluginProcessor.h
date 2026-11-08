#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BuzzKillEngine.h"

class BuzzKillProcessor : public juce::AudioProcessor
{
public:
    BuzzKillProcessor();
    ~BuzzKillProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BuzzKill"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // live readouts for the editor
    const bkill::Telemetry& telemetry() const { return engine.tele; }
    void relearn() { engine.requestRelearn(); }   // safe to call from the UI thread
    double getRateForDisplay() const { return currentRate; }

private:
    bkill::Engine engine;
    double currentRate = 48000.0;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* listenParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuzzKillProcessor)
};
