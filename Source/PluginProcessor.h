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

    // the engine currently running (for the editor's live display)
    const bkill::Telemetry& telemetry() const { return engines[activeMode.load()].tele; }
    bool isStudioMode() const { return activeMode.load() == 0; }
    double getRateForDisplay() const { return currentRate; }

private:
    bkill::Engine engines[2];                 // 0 = Studio (best sustain), 1 = Live (low latency)
    std::atomic<int> activeMode { 0 };
    double currentRate = 48000.0;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* listenParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuzzKillProcessor)
};
