// Offline test for the BuzzKill engine (no JUCE), modelled on real use: a DI guitar (with pickup
// hum and hiss) through a high-gain amp sim, then BuzzKill right after the sim.
//   buzzkill_render out_dir [release 0..1] [amp gain]
// Measures:
//   - palm-mute noise leak: how long noise is heard after each chug has died into the noise
//   - sustain: whether ringing notes are cut before they actually reach the noise floor
//   - transparency: any gain change while you are clearly playing (should be none)
//   - attacks: level of the first 5 ms of each chug vs the input (should be 0 dB)
// Writes before.wav, after.wav, removed.wav, clean.wav.
#include "BuzzKillEngine.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
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

// Karplus-Strong string. muteEnd: the note is stopped abruptly (palm mute lift / hand choke)
static void pluck (std::vector<float>& y, int sr, double t0, double freq, double dur, float amp, double sustainSec,
                   float bright, double stopMs, std::mt19937& rng)
{
    const int start = (int) (t0 * sr), len = (int) (dur * sr);
    const int Dl = std::max (2, (int) std::lround (sr / freq));
    std::vector<float> line ((size_t) Dl);
    std::uniform_real_distribution<float> u (-1, 1);
    float lp = 0;
    for (auto& v : line) { lp += 0.6f * (u (rng) - lp); v = lp; }
    const double g = std::pow (10.0, -3.0 / (sustainSec * freq));
    int idx = 0; float prev = 0;
    const int stop = (int) (stopMs * 0.001 * sr);
    for (int i = 0; i < len && start + i < (int) y.size(); ++i)
    {
        const float cur = line[(size_t) idx];
        line[(size_t) idx] = (float) g * (bright * cur + (1 - bright) * prev);
        prev = cur; idx = (idx + 1) % Dl;
        float e = 1.0f;
        if (i > len - stop) e = (float) (len - i) / (float) stop;
        y[(size_t) (start + i)] += amp * cur * e * e;
    }
}

// simple high-gain amp sim: pre high-pass, huge gain into a saturator, cab-ish filtering
struct AmpSim
{
    double hp = 0, hpx = 0, lp1 = 0, lp2 = 0, lp3 = 0, chp = 0, chpx = 0;
    float gain = 60.0f;
    float tick (float x, int sr)
    {
        const double a = std::exp (-2.0 * kPi * 120.0 / sr);
        hp = a * (hp + x - hpx); hpx = x;                                // tighten the low end
        const double s = std::tanh (hp * gain * 1.2) * 0.8 + std::tanh (hp * gain * 0.3) * 0.2;
        const double c = 1.0 - std::exp (-2.0 * kPi * 4200.0 / sr);
        lp1 += c * (s - lp1); lp2 += c * (lp1 - lp2); lp3 += c * (lp2 - lp3); // cab roll-off
        const double b = std::exp (-2.0 * kPi * 70.0 / sr);
        chp = b * (chp + lp3 - chpx); chpx = lp3;
        return (float) (chp * 0.35);
    }
};

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    const float tight = argc > 2 ? (float) std::atof (argv[2]) : 0.35f;
    const float ampGain = argc > 3 ? (float) std::atof (argv[3]) : 60.0f;
    const int sr = std::getenv ("DA_SR") ? std::atoi (std::getenv ("DA_SR")) : 48000;
    const bool stereo = std::getenv ("DA_STEREO") != nullptr, volumeTest = std::getenv ("DA_VOLUME") != nullptr;
    const double secs = volumeTest ? 40 : 31;
    const size_t n = (size_t) (sr * secs);
    std::mt19937 rng (3);
    std::vector<float> di (n, 0.0f), diNoise (n, 0.0f);

    // ---- performance (DI)
    const double E2 = 82.41, B2 = 123.47, E3 = 164.81, A3 = 220.0, G3 = 196.0;
    struct Ev { double t0, t1; const char* what; };
    std::vector<Ev> chugs, rings, chokes;
    auto chord = [&] (double t, std::initializer_list<double> fs, double dur, float amp, double sus, float bright, double stopMs)
    { double d = 0; for (double f : fs) { pluck (di, sr, t + d, f, dur, amp, sus, bright, stopMs, rng); d += 0.004; } };
    chord (2.0, { E2, B2, E3 }, 5.5, 0.25f, 6.0, 0.5f, 30);                        // power chord, let it ring
    rings.push_back ({ 2.0, 7.4, "power chord ring" });   // next event at 8.5 s
    for (int i = 0; i < 16; ++i)                                                    // palm-mute 8ths @160
    {
        const double t = 8.5 + i * 0.1875;
        chord (t, { E2, B2 }, 0.11, 0.3f, 0.35, 0.3f, 12);
        chugs.push_back ({ t, t + 0.11, "8th chug" });
    }
    for (int i = 0; i < 20; ++i)                                                    // palm-mute 16ths @160
    {
        const double t = 12.0 + i * 0.09375;
        chord (t, { E2, B2 }, 0.065, 0.3f, 0.3, 0.3f, 10);
        chugs.push_back ({ t, t + 0.065, "16th chug" });
    }
    pluck (di, sr, 15.0, A3, 4.5, 0.2f, 5.0, 0.5f, 30, rng);                       // single note, let it ring
    rings.push_back ({ 15.0, 18.9, "single note ring" });   // stabs start at 20 s
    for (int i = 0; i < 4; ++i)                                                     // chord stabs, hand-choked
    {
        const double t = 20.0 + i * 1.0;
        chord (t, { E2, B2, E3, G3 }, 0.35, 0.25f, 4.0, 0.5f, 8);
        chokes.push_back ({ t, t + 0.35, "choked stab" });
    }
    pluck (di, sr, 25.0, 2 * G3, 4.0, 0.25f, 3.0, 0.55f, 40, rng);                 // lead note fading out
    rings.push_back ({ 25.0, 29.0, "lead note fade" });

    // ---- DI noise: pickup hum/buzz + hiss (the volume test turns the guitar off and on again)
    std::normal_distribution<float> nd (0, 1);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        float vol = 1.0f;
        if (volumeTest && t > 31.0 && t < 34.0) vol = 0.0f;
        const double hum = 0.00012 * std::sin (2 * kPi * 60.02 * t) + 0.00008 * std::sin (2 * kPi * 180.06 * t + 1)
                         + 0.00005 * std::sin (2 * kPi * 300.1 * t + 2);
        diNoise[i] = (float) (vol * hum + vol * 0.00010 * nd (rng) + 0.00003 * nd (rng));   // last term: interface hiss
    }

    // ---- through the amp sim: with noise (what the plugin gets) and without (the reference)
    AmpSim simA, simB; simA.gain = simB.gain = ampGain;
    std::vector<float> in (n), clean (n);
    for (size_t i = 0; i < n; ++i) { in[i] = simA.tick (di[i] + diNoise[i], sr); clean[i] = simB.tick (di[i], sr); }

    std::vector<float> y = in, y2 = in;
    Engine eng; eng.prepare (sr); eng.setTightness (tight);
    const int lat = eng.latencySamples();
    const auto c0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; i += 256)
    {
        if (std::getenv ("DA_RELEARN") && i <= (size_t) (34.7 * sr) && i + 256 > (size_t) (34.7 * sr)) eng.requestRelearn();
        eng.process (y.data() + i, stereo ? y2.data() + i : nullptr, (int) std::min<size_t> (256, n - i));
        if (std::getenv ("DA_TRACE") && (i / 256) % (std::getenv ("DA_FINE") ? 4 : 47) == 0 && (double) i / sr >= std::atof (std::getenv ("DA_TRACE")) && (double) i / sr < std::atof (std::getenv ("DA_TRACE")) + 4)
            std::printf ("  t=%5.2f state %d floor %6.1f in %6.1f level %+5.1f dB  clampAgo %.2f\n", (double) i / sr, eng.tele.state.load(), eng.tele.floorDb.load(), eng.tele.inDb.load(), eng.tele.levelDb.load(), eng.tele.clampAgo.load());
    }
    const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - c0).count();
    std::vector<float> o (n, 0.0f);
    for (size_t i = 0; i + lat < n; ++i) o[i] = y[i + lat];

    auto rms = [&] (const std::vector<float>& x, double a, double b)
    {
        size_t i0 = (size_t) std::max (0.0, a * sr), i1 = std::min (n, (size_t) (b * sr));
        double s = 0; for (size_t i = i0; i < i1; ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / std::max<size_t> (1, i1 - i0) + 1e-30);
    };
    const double noiseDb = rms (in, 0.5, 1.9);   // what the noise floor sounds like after the sim
    std::printf ("cpu %.2f%% of one core | latency %d samples (%.1f ms) | release %.2f | amp gain %.0f | noise floor after sim %.1f dBFS\n",
                 100.0 * cpu / secs, lat, 1000.0 * lat / sr, tight, ampGain, noiseDb);
    if (stereo) { double d = 0; for (size_t i = 0; i < n; ++i) d = std::max (d, (double) std::abs (y[i] - y2[i])); std::printf ("stereo L/R max difference: %g\n", d); }

    // time at which a signal (1 ms windows) is last above a level, searching from a..b
    auto lastAbove = [&] (const std::vector<float>& x, double a, double b, double levelDb)
    {
        double last = a;
        for (double t = a; t < b; t += 0.001) if (rms (x, t, t + 0.001) > levelDb) last = t + 0.001;
        return last;
    };

    // ---- palm mutes: noise heard after the chug has died into the noise
    double sum8 = 0, max8 = 0, sum16 = 0, max16 = 0; int c8 = 0, c16 = 0;
    for (size_t k = 0; k < chugs.size(); ++k)
    {
        const double next = k + 1 < chugs.size() && chugs[k + 1].t0 > chugs[k].t0 ? chugs[k + 1].t0 : chugs[k].t1 + 0.25;
        const double cleanEnd = lastAbove (clean, chugs[k].t0, next, noiseDb);        // chug reaches the noise floor
        const double outEnd = lastAbove (o, chugs[k].t0, next, noiseDb - 20.0);       // output is 20 dB under the noise
        const double leak = std::max (0.0, outEnd - cleanEnd) * 1000.0;
        if (std::getenv ("DA_LIST") && leak > 15) std::printf ("   chug %zu at %.3f s: clean dies %.3f, output silent %.3f -> leak %.0f ms\n", k, chugs[k].t0, cleanEnd, outEnd, leak);
        if (chugs[k].what[0] == '8') { sum8 += leak; max8 = std::max (max8, leak); ++c8; }
        else { sum16 += leak; max16 = std::max (max16, leak); ++c16; }
    }
    std::printf ("\nPALM MUTES  noise heard after the chug dies:  8ths avg %.1f ms (max %.1f)   16ths avg %.1f ms (max %.1f)\n",
                 sum8 / c8, max8, sum16 / c16, max16);
    double chokeLeak = 0;
    for (auto& e : chokes)
    {
        const double cleanEnd = lastAbove (clean, e.t0, e.t0 + 0.95, noiseDb);
        const double oe = lastAbove (o, e.t0, e.t0 + 0.95, noiseDb - 20.0);
        if (std::getenv ("DA_LIST")) std::printf ("   stab at %.2f s: clean dies %.3f, output silent %.3f\n", e.t0, cleanEnd, oe);
        chokeLeak = std::max (chokeLeak, (oe - cleanEnd) * 1000.0);
    }
    std::printf ("CHOKED STABS noise heard after the choke: max %.1f ms\n", chokeLeak);

    // ---- sustain: gate closes vs when the note actually reaches the noise floor
    std::printf ("SUSTAIN (gate close time minus when the note reaches the noise floor; negative = cut early)\n");
    for (auto& e : rings)
    {
        const double cleanEnd = lastAbove (clean, e.t0, e.t1 + 1.0, noiseDb - 3.0);
        const double outEnd = lastAbove (o, e.t0, e.t1 + 1.0, noiseDb - 20.0);
        std::printf ("   %-18s note reaches noise at %.2f s, gate closed at %.2f s  ->  %+5.0f ms\n", e.what, cleanEnd, outEnd, (outEnd - cleanEnd) * 1000.0);
    }

    // ---- transparency: any gain change while clearly playing (clean > noise + 10 dB)?
    int windows = 0, touched = 0; double worst = 0;
    for (double t = 2.0; t < 29.0; t += 0.005)
    {
        if (rms (clean, t, t + 0.005) < noiseDb + 10.0) continue;
        ++windows;
        const double d = rms (o, t, t + 0.005) - rms (in, t, t + 0.005);
        if (d < -0.1) { ++touched; if (std::getenv ("DA_LIST")) std::printf ("   touched at %.3f s: %.2f dB (clean %.1f, noise %.1f)\n", t, d, rms (clean, t, t + 0.005), noiseDb); }
        worst = std::min (worst, d);
    }
    std::printf ("TRANSPARENCY while playing: %d of %d 5 ms windows changed (worst %.2f dB)\n", touched, windows, worst);

    // ---- attacks
    double atk = 0;
    for (auto& e : chugs) atk = std::min (atk, rms (o, e.t0, e.t0 + 0.005) - rms (in, e.t0, e.t0 + 0.005));
    std::printf ("ATTACKS: worst first-5 ms level change %.2f dB\n", atk);

    // ---- gaps
    std::printf ("GAPS: 0.5-1.9 s (learning) %.1f dB | 29.5-30.8 s %.1f dB  (input %.1f)\n", rms (o, 0.5, 1.9), rms (o, 29.5, 30.8), noiseDb);
    if (volumeTest)
    {
        std::printf ("VOLUME KNOB off 31-34 s, back up at 34 (nobody playing). Output per 0.5 s from 31 s:");
        for (double t = 31.0; t < 40.0; t += 0.5) std::printf (" %.0f", std::max (-99.0, rms (o, t, t + 0.5)));
        std::printf ("\n   input per 0.5 s:                                                ");
        for (double t = 31.0; t < 40.0; t += 0.5) std::printf (" %.0f", rms (in, t, t + 0.5));
        std::printf ("\n");
    }

    std::vector<float> removed (n);
    for (size_t i = 0; i < n; ++i) removed[i] = in[i] - o[i];
    writeWav (out + "/before.wav", in, sr);
    writeWav (out + "/after.wav", o, sr);
    writeWav (out + "/removed.wav", removed, sr);
    writeWav (out + "/clean.wav", clean, sr);
    return 0;
}
