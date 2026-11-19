#include "PluginProcessor.h"
#include "Rebel.h"
#include "PluginEditor.h"

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "dev"
#endif

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

    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "listen", 1 }, "Hear Removed", false));
    ps.push_back (cdrebel::makeParam());
    return { ps.begin(), ps.end() };
}

BuzzKillProcessor::BuzzKillProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BUZZKILL", createLayout())
{
    releaseParam = apvts.getRawParameterValue ("release");
    listenParam = apvts.getRawParameterValue ("listen");

    link = std::make_unique<Link> ("buzzkill", "BuzzKill", JucePlugin_VersionString, apvts, juce::StringArray { "release", "listen" });
    link->onOtherMessage = [this] (const juce::var& m)
    {
        if (m.getProperty ("type", "").toString() == "measure-reset") statsReset = true;   // The Master's RESET: start counting again
    };
    link->extraStatus = [this]
    {
        auto* o = new juce::DynamicObject();
        const auto& t = engine.tele;
        const auto n = playSamples.load();
        o->setProperty ("track", getTrackName());
        o->setProperty ("learned", t.state.load() != 0 && t.floorDb.load() > -119.0f);
        o->setProperty ("floorDb", (double) t.floorDb.load());
        o->setProperty ("playDb", n > 0 ? 10.0 * std::log10 (std::max (1e-20, playPow.load() / (double) n)) : -120.0);
        o->setProperty ("seconds", (double) n / std::max (1.0, currentRate));
        o->setProperty ("state", t.state.load());
        return juce::var (o);
    };
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
    engine.prepare (sampleRate);          // 5 ms look-ahead: fine to play through live
    setLatencySamples (engine.latencySamples());
}

void BuzzKillProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    for (int c = nIn; c < nOut; ++c) buffer.clear (c, 0, n);
    if (n == 0 || nIn == 0) return;

    auto& eng = engine;
    eng.setTightness (releaseParam->load());
    eng.setListenRemoved (listenParam->load() > 0.5f);

    float* L = buffer.getWritePointer (0);
    float* R = nIn > 1 && nOut > 1 ? buffer.getWritePointer (1) : nullptr;
    auto power = [&]
    {
        double s = 0;
        for (int i = 0; i < n; ++i) s += R != nullptr ? 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]) : (double) L[i] * L[i];
        return s / n;
    };
    const double inPow = power();
    eng.process (L, R, n);
    if (nIn == 1 && nOut > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    link->env.addPowers (inPow, power(), n);
    if (statsReset.exchange (false)) { playPow = 0; playSamples = 0; }
    if (eng.tele.state.load() == 1) { playPow = playPow.load() + inPow * n; playSamples += n; }
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
