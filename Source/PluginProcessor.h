#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DeadAirEngine.h"
#include "CDLink.h"

class DeadAirProcessor : public juce::AudioProcessor
{
public:
    DeadAirProcessor();
    ~DeadAirProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dead Air"; }
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

    // ---- Castle Danger Link: The Master (on the master bus) hears the whole mix and sets RELEASE
    //      for the genre and this track's part; it reads the learned noise floor from here
    using Link = cdlink::PluginLink<juce::AudioProcessorValueTreeState>;
    Link& getLink() { return *link; }
    juce::String getTrackName() const { const juce::ScopedLock sl (nameLock); return trackName; }
    void updateTrackProperties (const TrackProperties& props) override { const juce::ScopedLock sl (nameLock); trackName = props.name.value_or (juce::String()); }

private:
    bkill::Engine engine;
    double currentRate = 48000.0;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* listenParam = nullptr;
    std::unique_ptr<Link> link;
    juce::String trackName;
    juce::CriticalSection nameLock;
    // how loud you play, while the gate is open (since The Master's last RESET)
    std::atomic<double> playPow { 0 };
    std::atomic<juce::int64> playSamples { 0 };
    std::atomic<bool> statsReset { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeadAirProcessor)
};
