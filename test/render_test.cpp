// Offline test for the BUZZKILL engine (no JUCE).
//   buzzkill_render out_dir [mains_hz] [tightness]
// Builds a synthetic guitar performance and realistic amp noise separately, runs
// guitar+noise through the engine, then measures:
//   - noise left in the gaps (dB reduction)
//   - how closely the processed note tails follow the clean guitar (sustain kept)
// Writes before.wav, after.wav and removed.wav.
#include "BuzzKillEngine.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <chrono>
#include <string>

using namespace bkill;

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return;
    const uint32_t n = (uint32_t) x.size(), dataBytes = n * 2, riff = 36 + dataBytes;
    const uint16_t fmt = 1, ch = 1, bits = 16, align = 2; const uint32_t rate = (uint32_t) sr, bps = rate * 2, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (float v : x) { int16_t s = (int16_t) (clampf (v, -1, 1) * 32767); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

// Karplus-Strong string with adjustable sustain; mute = palm mute (short, dark)
static void pluck (std::vector<float>& y, int sr, double t0, double freq, double dur, float amp, double sustainSec, bool mute, std::mt19937& rng)
{
    const int start = (int) (t0 * sr), len = (int) (dur * sr);
    const int D = std::max (2, (int) std::lround (sr / freq));
    std::vector<float> line ((size_t) D);
    std::uniform_real_distribution<float> u (-1, 1);
    float lp = 0;
    for (auto& v : line) { lp += 0.5f * (u (rng) - lp); v = lp; }
    // per-period loop gain for the requested -60 dB time
    const double g = std::pow (10.0, -3.0 / (sustainSec * freq));
    const float bright = mute ? 0.25f : 0.5f;
    int idx = 0; float prev = 0;
    for (int i = 0; i < len && start + i < (int) y.size(); ++i)
    {
        const float cur = line[(size_t) idx];
        const float nxt = (float) g * (bright * cur + (1 - bright) * prev);
        prev = cur; line[(size_t) idx] = nxt; idx = (idx + 1) % D;
        float env = 1.0f;
        const int rel = (int) (0.03 * sr);
        if (i > len - rel) env = (float) (len - i) / rel;   // fret hand release at the end
        y[(size_t) (start + i)] += amp * cur * env;
    }
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    const double mains = argc > 2 ? std::atof (argv[2]) : 60.02;
    const float tight = argc > 3 ? (float) std::atof (argv[3]) : 0.5f;
    const int sr = std::getenv ("DA_SR") ? std::atoi (std::getenv ("DA_SR")) : 48000; const double secs = 32;
    const bool noHum = std::getenv ("DA_NOHUM") != nullptr, noLead = std::getenv ("DA_NOLEAD") != nullptr, stereo = std::getenv ("DA_STEREO") != nullptr;
    const size_t n = (size_t) (sr * secs);
    std::mt19937 rng (3);
    std::vector<float> gtr (n, 0.0f), noise (n, 0.0f);

    // ---- performance (first 2 s are just the amp humming)
    const double E2 = 82.41, A2 = 110.0, D3 = 146.83, G3 = 196.0, B3 = 246.94, E4 = 329.63, F4 = 349.23;
    auto chord = [&] (double t, std::initializer_list<double> fs, double dur, float amp, double sus, bool mute)
    { double d = 0; for (double f : fs) { pluck (gtr, sr, t + d, f, dur, amp, sus, mute, rng); d += 0.012; } };
    if (noLead) chord (0.0, { E2, 123.47, E2 * 2 }, 1.9, 0.2f, 3.0, false);
    chord (2.0, { E2, 123.47, E2 * 2, G3 * 1.0, B3, E4 }, 7.0, 0.16f, 6.0, false);  // E major, let ring 7 s
    pluck (gtr, sr, 10.0, F4, 3.5, 0.3f, 4.0, false, rng);                          // F4 single note (near a 50 Hz hum line)
    pluck (gtr, sr, 14.0, A2, 3.0, 0.35f, 3.0, false, rng);
    for (int i = 0; i < 12; ++i) chord (18.0 + i * 0.25, { E2, 123.47 }, 0.16, 0.35f, 0.5, true); // palm-mute chugs
    pluck (gtr, sr, 22.0, D3, 6.5, 0.12f, 8.0, false, rng);                         // quiet, very long note
    pluck (gtr, sr, 28.0, G3, 2.0, 0.3f, 2.0, false, rng);
    for (auto& v : gtr) v = std::tanh (v * 1.5f) / 1.5f;                              // a little amp grit

    // ---- amp noise: mains hum + rectifier buzz + hiss
    std::normal_distribution<float> nd (0, 1);
    float lp1 = 0, lp2 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        double hum = 0;
        for (int k = 1; k <= 12; ++k)
            hum += std::pow (10.0, (-56.0 - 4.0 * (k - 1) - (k % 2 == 0 ? 0 : 3)) / 20.0) * std::sin (2 * kPi * k * mains * t + k * 0.7);
        // buzz: sharp pulses at 2x mains (full-wave rectifier charging current)
        const double ph = std::fmod (t * mains * 2.0, 1.0);
        const double buzz = std::exp (-ph * 60.0) * 0.0012;
        lp1 += 0.35f * (nd (rng) - lp1); lp2 += 0.35f * (lp1 - lp2);
        const float hiss = lp2 * 0.0011f;
        noise[i] = noHum ? hiss : (float) (hum + buzz) + hiss;
    }
    double mean = 0; for (float v : noise) mean += v; mean /= n; for (auto& v : noise) v -= (float) mean;

    std::vector<float> in (n), y (n);
    for (size_t i = 0; i < n; ++i) in[i] = gtr[i] + noise[i];
    y = in;

    const int quality = argc > 4 ? std::atoi (argv[4]) : 1;
    Engine eng; eng.prepare (sr, quality); eng.setTightness (tight);
    const int lat = eng.latencySamples();
    std::vector<float> y2 = y;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; i += 256)
    {
        eng.process (y.data() + i, stereo ? y2.data() + i : nullptr, (int) std::min<size_t> (256, n - i));
        if (std::getenv ("DA_TRACE") && (i / 256) % 94 == 0)
            std::printf ("  t=%5.2f state %d floor %6.1f in %6.1f mains %.3f lines %d\n", (double) i / sr, eng.tele.state.load(), eng.tele.floorDb.load(), eng.tele.inDb.load(), eng.tele.mainsHz.load(), eng.tele.humLines.load());
    }
    const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("cpu: %.2f%% of one core (%s)\n", 100.0 * cpu / secs, stereo ? "stereo" : "mono");
    if (stereo) { double d = 0; for (size_t i = 0; i < n; ++i) d = std::max (d, (double) std::abs (y[i] - y2[i])); std::printf ("stereo L/R max difference (identical inputs): %g\n", d); }

    // align output to input
    std::vector<float> outA (n, 0.0f);
    for (size_t i = 0; i + lat < n; ++i) outA[i] = y[i + lat];

    auto rmsDb = [&] (const std::vector<float>& x, double a, double b)
    {
        double s = 0; size_t i0 = (size_t) (a * sr), i1 = (size_t) (b * sr);
        for (size_t i = i0; i < i1; ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) (i1 - i0) + 1e-30);
    };
    std::printf ("latency %d samples (%.1f ms) | mains %.2f Hz | tightness %.2f\n", lat, 1000.0 * lat / sr, mains, tight);
    std::printf ("engine: mains detected %.3f Hz, %d hum lines notched\n", (double) eng.tele.mainsHz.load(), eng.tele.humLines.load());

    // noise in gaps: compare with the noise-only signal over regions where the guitar is silent
    std::printf ("\nGAPS (noise only)     input    output   reduction\n");
    const double gaps[][2] = { { 1.0, 1.9 }, { 9.3, 9.9 }, { 13.6, 13.95 }, { 17.2, 17.95 }, { 31.0, 31.9 } };
    for (auto& g : gaps)
    {
        const double a = rmsDb (in, g[0], g[1]), b = rmsDb (outA, g[0], g[1]);
        std::printf ("  %5.1f-%5.1f s     %7.1f  %8.1f   %6.1f dB\n", g[0], g[1], a, b, a - b);
    }

    // sustain: windows where clean guitar is above the noise; error between output and clean
    std::printf ("\nSUSTAIN (clean guitar vs output, 250 ms windows)\n  time   clean   in+noise  output   err-vs-clean\n");
    const double check[] = { 2.5, 4, 6, 7.5, 8.5, 8.9, 10.5, 12, 13, 13.4, 14.5, 16, 16.8, 22.5, 25, 27, 28.3, 29.5, 29.9 };
    for (double t : check)
    {
        std::vector<float> err (n);
        size_t i0 = (size_t) (t * sr), i1 = (size_t) ((t + 0.25) * sr);
        double e = 0; for (size_t i = i0; i < i1; ++i) { const double d = outA[i] - gtr[i]; e += d * d; }
        const double errDb = 10 * std::log10 (e / (i1 - i0) + 1e-30);
        const double c = rmsDb (gtr, t, t + 0.25);
        std::printf ("  %4.1f  %6.1f  %7.1f   %7.1f    %6.1f dB below\n", t, c, rmsDb (in, t, t + 0.25), rmsDb (outA, t, t + 0.25), c - errDb);
    }

    std::vector<float> removed (n); for (size_t i = 0; i < n; ++i) removed[i] = in[i] - outA[i];
    writeWav (out + "/before.wav", in, sr);
    writeWav (out + "/after.wav", outA, sr);
    writeWav (out + "/removed.wav", removed, sr);
    writeWav (out + "/clean.wav", gtr, sr);
    return 0;
}
