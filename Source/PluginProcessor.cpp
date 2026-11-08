#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout BuzzKillProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;

    // The only "taste" control: how a note is let go once it sinks into the noise.
    ps.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release", NormalisableRange<float> (0.0f, 1.0f), 0.35f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int)
            {
                if (v < 0.15f) return String ("Natural");
                if (v > 0.85f) return String ("Tight");
                return String (roundToInt (v * 100.0f)) + "% tight";
            })
            .withValueFromStringFunction ([] (const String& t)
            {
                if (t.containsIgnoreCase ("nat")) return 0.0f;
                if (t.equalsIgnoreCase ("tight")) return 1.0f;
                return t.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
            })));

    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                          StringArray { "Studio", "Live" }, 0));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "listen", 1 }, "Hear Removed", false));
    return { ps.begin(), ps.end() };
}

BuzzKillProcessor::BuzzKillProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BUZZKILL", createLayout())
{
    releaseParam = apvts.getRawParameterValue ("release");
    modeParam = apvts.getRawParameterValue ("mode");
    listenParam = apvts.getRawParameterValue ("listen");
}

bool BuzzKillProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void BuzzKillProcessor::prepareToPlay (double sampleRate, int)
{
    currentRate = sampleRate;
    engines[0].prepare (sampleRate, 2);   // Studio: ~43 ms, longest sustain
    engines[1].prepare (sampleRate, 0);   // Live:   ~11 ms, for monitoring while you play
    const int mode = juce::jlimit (0, 1, (int) modeParam->load());
    activeMode = mode;
    setLatencySamples (engines[mode].latencySamples());
}

void BuzzKillProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    for (int c = nIn; c < nOut; ++c) buffer.clear (c, 0, n);
    if (n == 0 || nIn == 0) return;

    const int mode = juce::jlimit (0, 1, (int) modeParam->load());
    if (mode != activeMode.load())
    {
        engines[mode].reset();            // relearns the noise floor in about a second
        activeMode = mode;
        setLatencySamples (engines[mode].latencySamples());
    }

    auto& eng = engines[mode];
    eng.setTightness (releaseParam->load());
    eng.setListenRemoved (listenParam->load() > 0.5f);

    float* L = buffer.getWritePointer (0);
    float* R = nIn > 1 && nOut > 1 ? buffer.getWritePointer (1) : nullptr;
    eng.process (L, R, n);
    if (nIn == 1 && nOut > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
}

void BuzzKillProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void BuzzKillProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BuzzKillProcessor::createEditor() { return new BuzzKillEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BuzzKillProcessor(); }
