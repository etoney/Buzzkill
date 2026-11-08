// Headless checks run in CI:
//  1) runs synthetic guitar + amp-sim style noise through the plugin, checks it learns and silences the gaps
//  2) renders the editor to gui.png (playing) and gui_silent.png (gap)
//  3) state save/restore round trip, latency reporting
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

static bool writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    if (auto os = f.createOutputStream()) return juce::PNGImageFormat().writeImageToStream (img, *os);
    return false;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argc > 1 ? juce::File (juce::String (argv[1])).getParentDirectory() : juce::File::getCurrentWorkingDirectory());
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; };
    {
        const double sr = 48000.0; const int bs = 512;
        BuzzKillProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, bs);
        proc.prepareToPlay (sr, bs);
        check (proc.getLatencySamples() == 240, "reports 240 samples (5 ms look-ahead) latency at 48 kHz");

        std::unique_ptr<BuzzKillEditor> ed (dynamic_cast<BuzzKillEditor*> (proc.createEditor()));
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        juce::Random rnd (5);
        double t = 0, ph = 0;
        float lp = 0;
        float gapIn = 0, gapOut = 0;
        auto run = [&] (double seconds, bool note, bool measure)
        {
            for (int blk = 0; blk < (int) (seconds * sr / bs); ++blk)
            {
                float ein = 0;
                for (int i = 0; i < bs; ++i, t += 1.0 / sr)
                {
                    double hum = 0;
                    for (int k = 1; k <= 8; ++k) hum += std::pow (10.0, (-56.0 - 4.0 * k) / 20.0) * std::sin (2 * juce::MathConstants<double>::pi * k * 60.01 * t);
                    lp += 0.3f * ((rnd.nextFloat() * 2 - 1) - lp);
                    float x = (float) hum + lp * 0.0012f;
                    if (note) { ph += 110.0 / sr; x += 0.2f * (float) (std::sin (2 * juce::MathConstants<double>::pi * ph) + 0.5 * std::sin (4 * juce::MathConstants<double>::pi * ph) + 0.3 * std::sin (6 * juce::MathConstants<double>::pi * ph)); }
                    buf.setSample (0, i, x); buf.setSample (1, i, x); ein += x * x;
                }
                proc.processBlock (buf, midi);
                if (measure) { gapIn += ein; for (int i = 0; i < bs; ++i) gapOut += buf.getSample (0, i) * buf.getSample (0, i); }
            }
        };
        run (3.0, false, false);                       // amp noise only: learn
        run (1.0, true, false);                        // a note
        ed->refreshNow();
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f), dir.getChildFile ("gui.png")), "playing snapshot");
        run (1.5, false, false);                       // let it go
        run (1.0, false, true);                        // measure the gap
        for (int i = 0; i < 20; ++i) ed->refreshNow();
        check (writePng (ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f), dir.getChildFile ("gui_silent.png")), "silent snapshot");
        const float red = 10.0f * std::log10 ((gapIn + 1e-20f) / (gapOut + 1e-20f));
        std::printf ("gap reduction: %.1f dB, state %d\n", red, proc.telemetry().state.load());
        check (red > 40.0f, "gap noise reduced by more than 40 dB");

        // Relearn: forgets the noise, then is learned and silent again within a second of quiet
        proc.relearn();
        run (0.05, false, false);
        check (proc.telemetry().state.load() == 0, "relearn starts listening again");
        run (1.2, false, false);
        check (proc.telemetry().state.load() == 3, "relearned and silent within ~1 s");

        // state round trip
        auto* rel = proc.apvts.getParameter ("release");
        rel->setValueNotifyingHost (0.8f);
        juce::MemoryBlock state; proc.getStateInformation (state);
        rel->setValueNotifyingHost (0.1f);
        proc.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (proc.apvts.getRawParameterValue ("release")->load() - 0.8f) < 0.01f, "state restores release");
        ed.reset();
    }
    std::printf ("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
