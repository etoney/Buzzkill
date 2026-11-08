// BUZZKILL - intelligent noise gate for electric guitar
// Castle Danger Music Company
//
// Pure C++ (no JUCE) so it can be tested offline.
//
// How it works
//  1. Noise floor learning
//     Every few milliseconds the input is analysed with a short FFT. When the
//     level sits at its long-term minimum and stays flat (nobody is playing),
//     that moment is treated as "just the amp", and its spectrum is folded into
//     a per-frequency noise profile. A decaying note is never mistaken for
//     noise because it keeps falling; noise holds still.
//  2. Hum hunter
//     During those quiet moments a bank of precise detectors listens for mains
//     hum (50 Hz or 60 Hz) and its harmonics, measures which harmonics poke above
//     the hiss, and fine-tunes the mains frequency from phase drift. An adaptive
//     canceller then locks onto the exact amplitude and phase of each hum line and
//     subtracts a synthesised copy, so hum is removed even while you play, without
//     notching notes that happen to sit near a hum harmonic.
//  3. Spectral gate (the sustain saver)
//     Instead of one gate for the whole signal, each frequency band opens and
//     closes on its own, compared with the learned noise in that band. As a note
//     decays, its harmonics stay open for as long as they are above the noise,
//     while the hiss and buzz between them are removed. When nothing is left
//     above the noise anywhere, the remaining floor fades to true silence.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

namespace bkill
{
constexpr double kPi = 3.14159265358979323846;
using cpx = std::complex<float>;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float powToDb (float p) { return 10.0f * std::log10 (std::max (p, 1.0e-20f)); }

//==============================================================================
// Iterative radix-2 FFT
struct FFT
{
    int n = 0;
    std::vector<cpx> tw;
    std::vector<int> rev;

    void init (int size)
    {
        n = size;
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
        rev.resize ((size_t) n);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    void run (cpx* x, bool inverse) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cpx w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const cpx a = x[i + j], b = x[i + j + half] * w;
                    x[i + j] = a + b; x[i + j + half] = a - b;
                }
        }
    }
};

//==============================================================================
// Hum canceller (one per channel).
// Each hum harmonic is demodulated against a reference oscillator locked to the
// mains frequency, and its exact amplitude and phase are read through a steep
// 4-pole smoothing filter. A synthesised copy of the hum is then subtracted.
// The estimate is only updated while nobody is playing; while you play it is held
// and keeps turning at the exactly-tracked mains frequency, so guitar notes, even
// ones right next to a hum line, are never cancelled and can't disturb it.
// Nothing rings after a note the way a notch filter would.
struct HumCanceller
{
    static constexpr int K = 16;
    std::complex<double> osc[K], rot[K], st[K][4], snap[2][K][4];
    bool enabled[K] {};
    double depth[K] {};            // 0..1, how much of the estimate to trust (fades if it may have drifted)
    int active = 0, renorm = 0, snapIdx = 0;

    // direct frequency measurement: phase advance between consecutive quiet slices
    static constexpr int KF = 8;
    std::complex<double> slice[KF], prevSlice[KF];
    int sliceLen = 4800, sliceCount = 0;
    bool prevValid = false;
    double measNum = 0, measDen = 0, sliceSeconds = 0.1;
    std::complex<double> reacqAcc[K] {};
    int reacqLen = 0, reacqLeft = 0;
    int measCount = 0;

    void reset()
    {
        for (int k = 0; k < K; ++k)
        {
            osc[k] = { 1.0, 0.0 }; rot[k] = { 1.0, 0.0 }; enabled[k] = false; depth[k] = 0; reacqAcc[k] = 0;
            for (int j = 0; j < 4; ++j) { st[k][j] = 0; snap[0][k][j] = snap[1][k][j] = 0; }
        }
        active = 0;
        for (int k = 0; k < KF; ++k) slice[k] = prevSlice[k] = 0;
        sliceCount = 0; prevValid = false; measNum = measDen = 0; measCount = 0; reacqLeft = 0;
    }
    void tune (double fs, double f0, int numHarmonics)
    {
        sliceLen = (int) (fs * 0.1); sliceSeconds = sliceLen / fs;
        prevValid = false; sliceCount = 0;
        for (int k = 0; k < KF; ++k) slice[k] = 0;
        active = numHarmonics;
        for (int k = 0; k < active; ++k) rot[k] = std::polar (1.0, 2.0 * kPi * f0 * (k + 1) / fs);
    }
    std::complex<double> estimate (int k) const { return st[k][3]; }

    // After a long hold, the held estimate may have drifted in phase. Measure each line afresh
    // over exactly a whole number of mains cycles (which separates the harmonics cleanly) and
    // restart the smoothing from that, instead of slowly converging from a stale value.
    void beginReacquire (int samples)
    {
        reacqLen = reacqLeft = std::max (1, samples);
        for (auto& v : reacqAcc) v = 0;
    }
    bool reacquiring() const { return reacqLeft > 0; }

    void finishSlice()
    {
        if (prevValid)
        {
            for (int k = 0; k < std::min (KF, active); ++k)
            {
                if (! enabled[k]) continue;
                const double dphi = std::arg (slice[k] * std::conj (prevSlice[k]));
                // precision of the fundamental from harmonic k grows with k x amplitude
                const double w = std::norm (slice[k]) * (k + 1) * (k + 1);
                measNum += w * dphi / (2.0 * kPi * sliceSeconds * (k + 1));
                measDen += w;
            }
            ++measCount;
        }
        for (int k = 0; k < KF; ++k) { prevSlice[k] = slice[k]; slice[k] = 0; }
        prevValid = true; sliceCount = 0;
    }
    // returns true and the frequency error (Hz, at the fundamental) if a measurement is ready
    bool takeMeasurement (double& dfHz)
    {
        if (measDen <= 0 || measCount < 4) return false;   // average 4 slice pairs (0.5 s)
        dfHz = measNum / measDen; measNum = measDen = 0; measCount = 0;
        return true;
    }

    // keep the two most recent frame-boundary states, so a pick attack that slipped
    // in before the analysis noticed it can be undone
    void snapshot()
    {
        snapIdx ^= 1;
        for (int k = 0; k < active; ++k) for (int j = 0; j < 4; ++j) snap[snapIdx][k][j] = st[k][j];
    }
    void rollback()
    {
        const int older = snapIdx ^ 1;
        for (int k = 0; k < active; ++k) for (int j = 0; j < 4; ++j) st[k][j] = snap[older][k][j];
    }

    // a: smoothing coefficient per sample
    float tick (float x, double a)
    {
        double hum = 0;
        for (int k = 0; k < active; ++k)
        {
            auto* s = st[k];
            if (a > 0)
            {
                const std::complex<double> d = (double) x * std::conj (osc[k]);
                if (reacqLeft > 0) reacqAcc[k] += d;
                else { s[0] += a * (d - s[0]); s[1] += a * (s[0] - s[1]); s[2] += a * (s[1] - s[2]); s[3] += a * (s[2] - s[3]); }
                if (k < KF) slice[k] += d;
            }
            if (enabled[k]) hum += 2.0 * depth[k] * (s[3] * osc[k]).real();
            osc[k] *= rot[k];
        }
        if (++renorm >= 2048) { renorm = 0; for (int k = 0; k < active; ++k) osc[k] /= std::abs (osc[k]); }
        if (a > 0)
        {
            if (reacqLeft > 0 && --reacqLeft == 0)
                for (int k = 0; k < active; ++k)
                {
                    const auto v = reacqAcc[k] / (double) reacqLen;
                    for (int j = 0; j < 4; ++j) st[k][j] = v;
                }
            if (++sliceCount >= sliceLen) finishSlice();
        }
        else if (sliceCount > 0 || prevValid) { sliceCount = 0; prevValid = false; for (auto& v : slice) v = 0; }
        return (float) ((double) x - hum);
    }
};

//==============================================================================
// Listens to quiet passages for mains hum and its harmonics.
class HumHunter
{
public:
    static constexpr int K = 16;          // harmonics tracked per series
    static constexpr int kSeries = 2;     // 50 Hz and 60 Hz

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        blockLen = (int) (fs * 0.5);
        for (int s = 0; s < kSeries; ++s)
        {
            const double f0 = s == 0 ? 50.0 : 60.0;
            for (int k = 0; k < K; ++k)  initOsc (on[s][k], (k + 1) * f0);
            for (int m = 0; m <= K; ++m) initOsc (mid[s][m], (m + 0.5) * f0);
            for (int k = 0; k < K; ++k) { pOn[s][k] = 0; prevAcc[s][k] = 0; }
            for (int m = 0; m <= K; ++m) pMid[s][m] = 0;
        }
        count = 0; blocksSeen = 0; prevValid = false;
        locked = -1; offsetHz = 0;
        for (auto& a : atten) a = 0;
    }

    // feed one raw (mono) input sample; quiet = analysis says nobody is playing
    void feed (float x, bool quiet)
    {
        if (! quiet)
        {
            if (count > 0) { count = 0; prevValid = false; clearAcc(); }
            advanceAll();
            return;
        }
        for (int s = 0; s < kSeries; ++s)
        {
            for (int k = 0; k < K; ++k)  step (on[s][k], x);
            for (int m = 0; m <= K; ++m) step (mid[s][m], x);
        }
        if (++count >= blockLen) finishBlock();
    }

    // results
    int lockedSeries() const { return locked; }                 // -1 none, 0 = 50 Hz, 1 = 60 Hz
    double fundamental() const { return locked < 0 ? 0.0 : (locked == 0 ? 50.0 : 60.0) + offsetHz; }
    float attenuationDb (int k) const { return atten[k]; }       // how much to remove at harmonic k+1
    int version() const { return resultVersion; }

private:
    struct Osc { std::complex<double> p { 1.0, 0.0 }, rot, acc { 0.0, 0.0 }; bool valid = false; };

    void initOsc (Osc& o, double f)
    {
        o.valid = f < std::min (1000.0, fs * 0.45);
        o.rot = std::polar (1.0, -2.0 * kPi * f / fs);
        o.p = { 1.0, 0.0 }; o.acc = { 0.0, 0.0 };
    }
    static void step (Osc& o, float x)
    {
        if (! o.valid) return;
        o.acc += (double) x * o.p;
        o.p *= o.rot;
    }
    void advanceAll()
    {
        // keep the reference oscillators running so phase stays continuous
        for (int s = 0; s < kSeries; ++s)
        {
            for (int k = 0; k < K; ++k) if (on[s][k].valid) on[s][k].p *= on[s][k].rot;
            for (int m = 0; m <= K; ++m) if (mid[s][m].valid) mid[s][m].p *= mid[s][m].rot;
        }
        if (++renorm >= 4096) { renorm = 0; normalise(); }
    }
    void normalise()
    {
        for (int s = 0; s < kSeries; ++s)
        {
            for (int k = 0; k < K; ++k) on[s][k].p /= std::abs (on[s][k].p);
            for (int m = 0; m <= K; ++m) mid[s][m].p /= std::abs (mid[s][m].p);
        }
    }
    void clearAcc()
    {
        for (int s = 0; s < kSeries; ++s)
        {
            for (int k = 0; k < K; ++k) on[s][k].acc = 0;
            for (int m = 0; m <= K; ++m) mid[s][m].acc = 0;
        }
    }

    void finishBlock()
    {
        normalise();
        const double L2 = (double) blockLen * blockLen;
        const double w = blocksSeen < 2 ? 1.0 : 0.35;   // smoothing across blocks
        double score[kSeries] = { 0, 0 };
        int strong[kSeries] = { 0, 0 };
        float prom[kSeries][K] {};
        for (int s = 0; s < kSeries; ++s)
        {
            for (int m = 0; m <= K; ++m)
                if (mid[s][m].valid) pMid[s][m] += w * (std::norm (mid[s][m].acc) / L2 - pMid[s][m]);
            for (int k = 0; k < K; ++k)
            {
                if (! on[s][k].valid) { prom[s][k] = 0; continue; }
                pOn[s][k] += w * (std::norm (on[s][k].acc) / L2 - pOn[s][k]);
                double ref = pMid[s][k];
                if (mid[s][k + 1].valid) ref = std::sqrt (pMid[s][k] * pMid[s][k + 1]);
                const float p = (float) (10.0 * std::log10 ((pOn[s][k] + 1e-24) / (ref + 1e-24)));
                prom[s][k] = p;
                score[s] += std::max (0.0f, p - 6.0f);
                if (p >= 10.0f) ++strong[s];
            }
        }

        // which mains series is it?
        const int best = score[1] >= score[0] ? 1 : 0, other = 1 - best;
        if (strong[best] >= 3 && score[best] > 24.0 && score[best] > 1.6 * score[other] + 4.0)
        {
            if (best == candidate) ++candidateCount; else { candidate = best; candidateCount = 1; }
            if (locked < 0 || (best != locked && candidateCount >= 3)) { locked = best; offsetHz = 0; }
        }

        // fine-tune the mains frequency from phase drift between contiguous blocks
        if (locked >= 0 && prevValid)
        {
            const double T = (double) blockLen / fs;
            double num = 0, den = 0;
            for (int k = 0; k < 6; ++k)
            {
                if (! on[locked][k].valid || prom[locked][k] < 15.0f) continue;
                const double dphi = std::arg (on[locked][k].acc * std::conj (prevAcc[locked][k]));
                const double dk = dphi / (2.0 * kPi * T) / (k + 1);
                const double wt = pOn[locked][k];
                num += wt * dk; den += wt;
            }
            if (den > 0)
                offsetHz = std::clamp (offsetHz + 0.5 * (num / den - offsetHz), -0.4, 0.4);
        }
        for (int s = 0; s < kSeries; ++s) for (int k = 0; k < K; ++k) prevAcc[s][k] = on[s][k].acc;
        prevValid = true;

        if (locked >= 0)
            for (int k = 0; k < K; ++k)
                atten[k] = on[locked][k].valid ? clampf (prom[locked][k] - 4.0f, 0.0f, 42.0f) : 0.0f;

        ++blocksSeen; ++resultVersion;
        count = 0; clearAcc();
    }

    double fs = 48000;
    int blockLen = 24000, count = 0, blocksSeen = 0, renorm = 0, candidate = -1, candidateCount = 0;
    bool prevValid = false;
    Osc on[kSeries][K], mid[kSeries][K + 1];
    double pOn[kSeries][K] {}, pMid[kSeries][K + 1] {};
    std::complex<double> prevAcc[kSeries][K] {};
    int locked = -1, resultVersion = 0;
    double offsetHz = 0;
    float atten[K] {};
};

//==============================================================================
// Live readouts for the editor (written by the audio thread, read by the UI)
struct Telemetry
{
    static constexpr int kMaxBins = 4097;
    std::atomic<int> state { 0 };            // 0 listening, 1 playing, 2 tail, 3 silenced
    std::atomic<float> floorDb { -120 }, inDb { -120 }, outDb { -120 }, removedDb { 0 };
    std::atomic<float> mainsHz { 0 };
    std::atomic<int> humLines { 0 }, numBins { 0 };
    std::atomic<unsigned> humMask { 0 };   // bit k = harmonic k+1 is being cancelled
    std::atomic<float> binHz { 0 };
    std::atomic<float> inSpec[kMaxBins], noiseSpec[kMaxBins], outSpec[kMaxBins];
    Telemetry()
    {
        for (int i = 0; i < kMaxBins; ++i) { inSpec[i] = -150; noiseSpec[i] = -150; outSpec[i] = -150; }
    }
};

//==============================================================================
class Engine
{
public:
    Telemetry tele;

    // Analysis always uses a long window (~43 ms) so faint note tails can be told apart from the
    // noise. The audio itself is processed through a short window, which sets the latency:
    // latency 0 = ~11 ms, 1 = ~21 ms, 2 = ~43 ms
    void prepare (double sampleRate, int latency = 0)
    {
        fs = sampleRate;
        const int scale = fs <= 56000 ? 1 : (fs <= 112000 ? 2 : 4);
        N = 2048 * scale;
        Ns = (512 << std::clamp (latency, 0, 2)) * scale;
        H = Ns / 4; B = N / 2 + 1; Bs = Ns / 2 + 1;
        fft.init (N); fftS.init (Ns);
        win.resize ((size_t) N); winS.resize ((size_t) Ns);
        for (int j = 0; j < N; ++j) win[(size_t) j] = (float) std::sin (kPi * j / N);     // sqrt-Hann
        for (int j = 0; j < Ns; ++j) winS[(size_t) j] = (float) std::sin (kPi * j / Ns);
        winPow = 0; winSum = 0; for (float v : win) { winPow += v * v; winSum += v; }

        for (int c = 0; c < 2; ++c) { inRing[c].assign ((size_t) N, 0); rawRing[c].assign ((size_t) N, 0); outRing[c].assign ((size_t) Ns, 0); }
        frame.assign ((size_t) N, cpx {}); frameS.assign ((size_t) Ns, cpx {}); gainS.assign ((size_t) Bs, 1.0f);
        P.assign ((size_t) B, 0); Psm.assign ((size_t) B, 0); noise.assign ((size_t) B, 0);
        gain.assign ((size_t) B, 1); gainTarget.assign ((size_t) B, 1); open.assign ((size_t) B, 0); humBin.assign ((size_t) B, 0); humNear.assign ((size_t) B, -1);
        noiseRaw.assign ((size_t) B, 0); ref.assign ((size_t) B, 0); rawFrame.assign ((size_t) N, cpx {});
        tmp.assign ((size_t) B, 0); masked.assign ((size_t) B, 0); pending.assign ((size_t) B, 0);
        cumP.assign ((size_t) B + 1, 0); cumR.assign ((size_t) B + 1, 0); xiPrev.assign ((size_t) B, 0);

        hunter.prepare (fs);
        for (auto& hc : canceller) hc.reset();
        hunterVersion = -1; tunedHz = 0; tunedSeries = -1; humCount = 0; sigmaF = 0.05; frozenSec = 0;
        for (auto& d : humDepth) d = 0;
        for (auto& v : offVotes) v = 0;
        aQuiet = 1.0 - std::exp (-2.0 * kPi * 3.0 / fs);    // 3 Hz smoothing while quiet: locks on in ~0.2 s
        humQuiet = false;

        framesPerSec = (float) (fs / H);
        ddBeta = std::exp (-1.0f / (0.06f * framesPerSec));
        pendFrames = std::max (2, (int) std::lround (0.02f * framesPerSec));
        subLen = std::max (1, (int) (0.5f * framesPerSec));
        slowCoef = 1.0f - std::exp (-1.0f / (0.08f * framesPerSec));
        hist.assign ((size_t) std::max (1, (int) framesPerSec), -150.0f);   // 1 s of frame levels
        reset();
        tele.numBins = B; tele.binHz = (float) (fs / N);
        setTightness (tight);
    }

    void reset()
    {
        pos = 0; hopCount = 0;
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inRing[c].begin(), inRing[c].end(), 0.0f);
            std::fill (rawRing[c].begin(), rawRing[c].end(), 0.0f);
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
        }
        std::fill (Psm.begin(), Psm.end(), 0.0f);
        std::fill (gain.begin(), gain.end(), 1.0f);
        std::fill (open.begin(), open.end(), 0);
        std::fill (pending.begin(), pending.end(), 0);
        std::fill (xiPrev.begin(), xiPrev.end(), 0.0f);
        floorDb = 0; floorInit = false; learnedFrames = 0; learned = false; engage = 0;
        frames = 0; subMin = 1000; std::fill (subMins, subMins + kSubs, 1000.0f);
        subIdx = 0; subCount = 0; openness = 0; holdLeft = 0; histIdx = 0; quiet = false;
        std::fill (hist.begin(), hist.end(), -150.0f);
    }

    int latencySamples() const { return Ns; }

    // 0 = natural (long, gentle tails), 1 = tight (fast cut for palm mutes)
    void setTightness (float t)
    {
        tight = clampf (t, 0, 1);
        openThrDb  = 8.0f + 2.0f * tight;
        closeThrDb = 3.5f + 2.5f * tight;
        const float rel   = 0.140f * std::pow (0.08f, tight);        // 140 ms -> 11 ms per-band release
        const float brRel = 0.300f * std::pow (0.08f, tight);        // 300 ms -> 24 ms floor fade
        const float hold  = 0.120f * std::pow (0.10f, tight);        // 120 ms -> 12 ms hold
        relCoef   = 1.0f - std::exp (-1.0f / (rel * framesPerSec));
        brRelCoef = std::exp (-1.0f / (brRel * framesPerSec));
        holdFrames = std::max (1, (int) (hold * framesPerSec));
    }
    void setListenRemoved (bool b) { listenRemoved = b; }

    // L, R: in-place. R may be nullptr for mono.
    void process (float* L, float* R, int n)
    {
        const int nch = R != nullptr ? 2 : 1;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            // ---- hum cancellation (zero latency) + hum analysis on the raw input
            float in[2], hummed[2];
            for (int c = 0; c < nch; ++c) in[c] = ch[c][i];
            const float mono = nch == 2 ? 0.5f * (in[0] + in[1]) : in[0];
            hunter.feed (mono, quiet);
            const double a = humQuiet ? aQuiet : 0.0;   // frozen while playing
            for (int c = 0; c < nch; ++c) hummed[c] = canceller[c].tick (in[c], a);

            // ---- STFT stream (latency = Ns samples)
            const size_t oi = (size_t) (pos % Ns);
            for (int c = 0; c < nch; ++c)
            {
                const float dry = rawRing[c][(size_t) ((pos + N - Ns) % N)];
                const float wet = outRing[c][oi];
                rawRing[c][(size_t) pos] = in[c];
                inRing[c][(size_t) pos] = hummed[c];
                outRing[c][oi] = 0;
                ch[c][i] = listenRemoved ? dry - wet : wet;
            }
            if (++pos >= N) pos = 0;
            if (++hopCount >= H) { hopCount = 0; processFrame (nch); }
        }
        if (hunter.version() != hunterVersion) updateHum();
        trackHumFreq();
        updateHumDepth (n);
    }

private:
    void updateHum()
    {
        hunterVersion = hunter.version();
        if (hunter.lockedSeries() < 0) return;
        const double f0 = hunter.fundamental();
        int lines = 0, count = 0;
        for (int k = 0; k < HumCanceller::K; ++k)
        {
            if (f0 * (k + 1) > std::min (1000.0, fs * 0.45)) break;
            count = k + 1;
            // lines switch on at once, but only switch off after several blocks in a row without
            // hum (one block disturbed by a fading note must not drop a line)
            const float a = hunter.attenuationDb (k);
            bool on = canceller[0].enabled[k];
            if (a >= 3.0f) { on = true; offVotes[k] = 0; }
            else if (on && a < 1.0f && ++offVotes[k] >= 4) on = false;
            for (auto& hc : canceller) hc.enabled[k] = on;
            if (on) ++lines;
        }
        humCount = count;
        if (tunedSeries < 0 || hunter.lockedSeries() != tunedSeries)
        {
            tunedSeries = hunter.lockedSeries();
            sigmaF = 0.05;            // nominal mains: could be off by a few hundredths of a Hz
            setHumFreq (f0);
        }
        else setHumFreq (tunedHz);   // refresh which lines are active
        if (lines != tele.humLines.load()) relearnFrames = (int) (0.6f * framesPerSec);   // profile must forget the hum
        tele.humLines = lines;
    }

    void setHumFreq (double f0)
    {
        tunedHz = f0;
        std::fill (humBin.begin(), humBin.end(), 0);
        for (int b = 0; b < B; ++b)
        {
            const double hz = (double) b * fs / N;
            const int k = (int) std::lround (hz / f0) - 1;
            humNear[(size_t) b] = (hz < 1100.0 && k >= 0 && k < humCount) ? (signed char) k : (signed char) -1;
        }
        unsigned mask = 0;
        for (int k = 0; k < humCount; ++k)
            if (canceller[0].enabled[k])
            {
                const int b = (int) std::lround (f0 * (k + 1) * N / fs);
                if (b > 0 && b < B) humBin[(size_t) b] = 1;
                mask |= 1u << k;
            }
        tele.humMask = mask;
        for (auto& hc : canceller) hc.tune (fs, f0, humCount);
        tele.mainsHz = (float) f0;
    }

    // While playing, each hum line's estimate is held and keeps turning at our best mains
    // frequency. If that frequency is uncertain, the held hum may slowly drift out of phase
    // and stop cancelling (or worse, add). Fade each line out before that can happen.
    void updateHumDepth (int n)
    {
        frozenSec = humQuiet ? 0.0 : frozenSec + n / fs;
        for (int k = 0; k < canceller[0].active; ++k)
        {
            const double phaseErr = 2.0 * kPi * (k + 1) * sigmaF * frozenSec;
            const double target = humQuiet ? 1.0 : std::clamp (1.0 - phaseErr / (kPi / 3.0), 0.0, 1.0);
            for (auto& hc : canceller)
            {
                double& d = hc.depth[k];
                if (hc.reacquiring()) d = 0;
                else d += (target - d) * std::min (1.0, n / (0.03 * fs));
            }
            humDepth[k] = (float) canceller[0].depth[k];
        }
    }

    // Keep the reference locked to the real mains frequency (it drifts by hundredths of a Hz)
    void trackHumFreq()
    {
        double df = 0;
        if (tunedHz <= 0 || ! canceller[0].takeMeasurement (df)) return;
        const double nominal = tunedSeries == 0 ? 50.0 : 60.0;
        const double f0 = std::clamp (tunedHz + 0.6 * df, nominal - 0.5, nominal + 0.5);
        sigmaF = 0.0008 + 0.5 * std::abs (df);   // how unsure we still are about the frequency
        if (std::abs (f0 - tunedHz) > 1.0e-6) setHumFreq (f0);
    }

    void processFrame (int nch)
    {
        // gather windowed frame, both channels packed into one complex FFT (L real, R imag)
        for (int j = 0; j < N; ++j)
        {
            const size_t idx = (size_t) ((pos + j) % N);
            const float l = inRing[0][idx] * win[(size_t) j];
            const float r = nch == 2 ? inRing[1][idx] * win[(size_t) j] : 0.0f;
            frame[(size_t) j] = { l, r };
        }
        fft.run (frame.data(), false);

        // per-bin power (average of channels), normalised so a full-scale sine ~ 0 dB
        const float norm = 4.0f / (winSum * winSum);
        for (int b = 0; b < B; ++b)
        {
            const cpx z = frame[(size_t) b], zc = std::conj (frame[(size_t) ((N - b) % N)]);
            const cpx xl = 0.5f * (z + zc), xr = cpx (0, -0.5f) * (z - zc);
            float p = std::norm (xl);
            if (nch == 2) p = 0.5f * (p + std::norm (xr));
            P[(size_t) b] = p * norm;
        }

        // smoothing across frequency, then time
        const float aT = std::exp (-1.0f / (0.01f * framesPerSec));
        double total = 0;
        for (int b = 0; b < B; ++b)
        {
            const float lo = P[(size_t) std::max (0, b - 1)], hi = P[(size_t) std::min (B - 1, b + 1)];
            const float f = 0.25f * lo + 0.5f * P[(size_t) b] + 0.25f * hi;
            Psm[(size_t) b] = aT * Psm[(size_t) b] + (1.0f - aT) * f;
            total += Psm[(size_t) b];
        }
        const float Edb = powToDb ((float) total);
        // floor decisions use a slower level (a short frame sees hum phase as a 3 dB wobble)
        totalSlow += (total - totalSlow) * slowCoef;
        if (frames == N / H + 3) totalSlow = total;   // first frame after the ring has filled
        const float Es = powToDb ((float) totalSlow);
        ++frames;

        const bool digitalSilence = Edb < -125.0f || frames <= N / H + 3 + (int) (0.1f * framesPerSec);   // skip ring fill + level settling
        if (digitalSilence) totalSlow = total;
        if (! digitalSilence) trackFloor (Es);

        // quiet = level at the long-term floor and holding steady for a second
        const float Eago = hist[(size_t) histIdx];
        hist[(size_t) histIdx] = Es; histIdx = (histIdx + 1) % (int) hist.size();
        const bool steady = std::abs (Es - Eago) < 1.5f;
        quiet = ! digitalSilence && floorInit && Es < floorDb + 2.5f && steady && frames > (int) hist.size();


        // learn the noise profile
        if (quiet)
        {
            const bool fast = learnedFrames < (int) (0.15f * framesPerSec) || relearnFrames > 0;
            if (relearnFrames > 0) --relearnFrames;
            const float a = fast ? 0.08f : 1.0f - std::exp (-1.0f / (0.4f * framesPerSec));
            rawSpectrum (nch);   // -> tmp: the same moment with no hum removed
            for (int b = 0; b < B; ++b)
            {
                noise[(size_t) b] += a * (Psm[(size_t) b] - noise[(size_t) b]);
                noiseRaw[(size_t) b] += a * (tmp[(size_t) b] - noiseRaw[(size_t) b]);
            }
            if (learnedFrames == 0) for (int b = 0; b < B; ++b) { noise[(size_t) b] = Psm[(size_t) b]; noiseRaw[(size_t) b] = tmp[(size_t) b]; }
            ++learnedFrames;
            if (learnedFrames > (int) (0.3f * framesPerSec)) learned = true;
        }


        // ---- per-band gate
        // noise reference = learned noise, plus whatever hum the canceller is currently not removing
        for (int b = 0; b < B; ++b)
        {
            const int k = humNear[(size_t) b];
            const float undone = k >= 0 ? 1.0f - humDepth[k] : 0.0f;
            ref[(size_t) b] = noise[(size_t) b] + undone * std::max (0.0f, noiseRaw[(size_t) b] - noise[(size_t) b]);
        }
        const float openLin = std::pow (10.0f, openThrDb / 10.0f), closeLin = std::pow (10.0f, closeThrDb / 10.0f);
        const float strongLin = openLin * 4.0f;   // +6 dB: opens at once (pick attacks)
        const int bLo = std::max (1, (int) (60.0 * N / fs)), bHi = std::min (B - 1, (int) (12000.0 * N / fs));
        const int bHum = std::max (bLo, (int) (1100.0 * N / fs));   // hum lines stay below this
        int openCount = 0, openHigh = 0, openMusic = 0;
        double hiE = 0, hiN = 0;

        // local masking: level of the music vs the noise over about a third of an octave around
        // each band. Where the guitar is far above the noise, the noise is inaudible, so nothing
        // there is gated (gating loud playing only carves the guitar's own texture and makes
        // phasey, wah-like movement as notes bend). Gating fades in as the music nears the noise.
        cumP[0] = 0; cumR[0] = 0;
        for (int b = 0; b < B; ++b) { cumP[(size_t) b + 1] = cumP[(size_t) b] + Psm[(size_t) b]; cumR[(size_t) b + 1] = cumR[(size_t) b] + ref[(size_t) b]; }
        for (int b = 0; b < B; ++b)
        {
            const int half = std::max (2, (int) (b * 0.12f));
            const int lo = std::max (0, b - half), hi = std::min (B, b + half + 1);
            const double sp = cumP[(size_t) hi] - cumP[(size_t) lo], sr = cumR[(size_t) hi] - cumR[(size_t) lo];
            const float localDb = powToDb ((float) (sp / (sr + 1.0e-20)));
            masked[(size_t) b] = clampf ((localDb - maskLoDb) / (maskHiDb - maskLoDb), 0.0f, 1.0f);
        }

        for (int b = 0; b < B; ++b)
        {
            if (b >= bHum && b <= bHi) { hiE += Psm[(size_t) b]; hiN += noise[(size_t) b]; }
            const float snr = Psm[(size_t) b] / (ref[(size_t) b] + 1.0e-20f);
            // decision-directed signal-to-noise estimate: blends what this band carried a moment
            // ago with what it carries now, so bands near the threshold stop flickering
            // (smooths the frame-to-frame scatter while music is there, but is capped at a few times
            // what the band holds right now, so it lets go within a frame or two once it is gone)
            const float ml = std::max (snr - 1.0f, 0.0f);
            float& xi = xiPrev[(size_t) b];
            xi = std::min (ddBeta * xi + (1.0f - ddBeta) * ml, 4.0f * ml + 0.5f);
            if (snr > strongLin) xi = std::max (xi, ml);   // attacks at once
            const float lvl = xi + 1.0f;
            // a band opens after ~20 ms of steady evidence, or at once if it jumps in strongly;
            // a lone noise peak can no longer blip open as a short whistle
            unsigned char& pend = pending[(size_t) b];
            pend = lvl > openLin ? (unsigned char) std::min (255, pend + 1) : (unsigned char) 0;
            if (snr > strongLin || pend >= pendFrames) open[(size_t) b] = 1;
            else if (lvl < closeLin) open[(size_t) b] = 0;
            float g = 0;
            if (open[(size_t) b])
            {
                g = std::sqrt (xi / (1.0f + xi));   // Wiener: removes only the noise's share
                if (b >= bLo && b <= bHi) ++openCount;
                if (b >= bHum && b <= bHi) ++openHigh;
                // near the hum lines, only count it as music if it is louder than the amp's
                // full, uncancelled hum would be there (leftover hum can never qualify)
                if (b >= bLo && b <= bHi && (humNear[(size_t) b] < 0 || Psm[(size_t) b] > 2.0f * noiseRaw[(size_t) b])) ++openMusic;
            }
            tmp[(size_t) b] = g;
        }
        // protect each harmonic's main lobe (neighbouring bins follow the peak), then lift
        // everything that is masked by the music around it
        for (int b = 0; b < B; ++b)
        {
            const float nb = std::max (tmp[(size_t) std::max (0, b - 1)], tmp[(size_t) std::min (B - 1, b + 1)]);
            const float g = std::max (tmp[(size_t) b], 0.75f * nb);
            gainTarget[(size_t) b] = g + (1.0f - g) * masked[(size_t) b];
        }

        // hum estimator listens whenever nothing is sounding above the noise (every gap, even
        // between palm mutes) and undoes the last couple of hops when playing starts.
        // Bins sitting exactly on a hum line are ignored, so leftover hum can never block its
        // own correction, but any musical partial anywhere else counts as playing.
        {
            const bool hq = learned ? (openMusic == 0 && hiE < 2.0 * hiN) : quiet;
            if (humQuiet && ! hq) for (auto& hc : canceller) hc.rollback();
            if (! humQuiet && hq && frozenSec > 0.5 && tunedHz > 0)
                for (auto& hc : canceller)
                {
                    hc.beginReacquire ((int) std::lround (3.0 * fs / tunedHz));   // 3 mains cycles
                    for (auto& d : hc.depth) d = 0;
                }
            for (auto& hc : canceller) hc.snapshot();
            humQuiet = hq;
        }

        // whole-signal floor: residual noise is kept at -30 dB while playing, fades to silence after
        if (openCount > 0) { openness = 1.0f; holdLeft = holdFrames; }
        else if (holdLeft > 0) --holdLeft;
        else openness *= brRelCoef;
        const float floorGain = 0.0316f * openness;

        // engage smoothly once the noise floor is known
        engage += ((learned ? 1.0f : 0.0f) - engage) * 0.05f;

        double outTotal = 0, nIn = 0, nOut = 0;
        for (int b = 0; b < B; ++b)
        {
            const float t = gainTarget[(size_t) b];
            float& g = gain[(size_t) b];
            g = t > g ? t : g + (t - g) * relCoef;
            float gf = std::max (g, floorGain);
            gf = 1.0f - engage * (1.0f - gf);
            tmp[(size_t) b] = gf;
            outTotal += Psm[(size_t) b] * gf * gf;
            nIn += noise[(size_t) b]; nOut += noise[(size_t) b] * gf * gf;
        }

        // ---- apply through the short synthesis window: each short band takes the most open of
        // the analysis bands it covers (so a note's harmonics are never cut)
        const int r = N / Ns, margin = r > 1 ? r / 2 + 2 : 0;
        for (int sb = 0; sb < Bs; ++sb)
        {
            const int c0 = std::max (0, sb * r - margin), c1 = std::min (B - 1, sb * r + margin);
            float g = 0;
            for (int b = c0; b <= c1; ++b) g = std::max (g, tmp[(size_t) b]);
            gainS[(size_t) sb] = g;
        }
        for (int j = 0; j < Ns; ++j)
        {
            const size_t idx = (size_t) ((pos + N - Ns + j) % N);
            const float l = inRing[0][idx] * winS[(size_t) j];
            const float rr = nch == 2 ? inRing[1][idx] * winS[(size_t) j] : 0.0f;
            frameS[(size_t) j] = { l, rr };
        }
        fftS.run (frameS.data(), false);
        for (int sb = 0; sb < Bs; ++sb)
        {
            frameS[(size_t) sb] *= gainS[(size_t) sb];     // gain is real and symmetric, so packed
            if (sb > 0 && sb < Ns / 2) frameS[(size_t) (Ns - sb)] *= gainS[(size_t) sb];   // stereo stays separable
        }
        fftS.run (frameS.data(), true);
        const float scale = 0.5f / (float) Ns;   // sqrt-Hann^2 at 75% overlap sums to 2
        for (int j = 0; j < Ns; ++j)
        {
            const size_t idx = (size_t) ((pos + j) % Ns);
            const float w = winS[(size_t) j] * scale;
            outRing[0][idx] += frameS[(size_t) j].real() * w;
            if (nch == 2) outRing[1][idx] += frameS[(size_t) j].imag() * w;
        }

        // ---- telemetry
        int st = 0;
        if (learned) st = openCount > 0 ? 1 : (openness > 0.05f ? 2 : 3);
        tele.state = st;
        tele.floorDb = floorInit ? floorDb : -120.0f;
        tele.inDb = Edb;
        const float oDb = powToDb ((float) outTotal);
        tele.outDb = oDb;
        tele.removedDb = learned ? clampf (powToDb ((float) nIn) - powToDb ((float) nOut), 0.0f, 99.0f) : 0.0f;   // how far the noise is turned down
        if ((frames & 3) == 0)
            for (int b = 0; b < B && b < Telemetry::kMaxBins; ++b)
            {
                tele.inSpec[b] = powToDb (Psm[(size_t) b]);
                tele.noiseSpec[b] = learnedFrames > 0 ? powToDb (noise[(size_t) b]) : -150.0f;
                tele.outSpec[b] = powToDb (Psm[(size_t) b] * tmp[(size_t) b] * tmp[(size_t) b]);
            }
    }

    // power spectrum of the input before hum removal for the current frame -> tmp (freq-smoothed)
    void rawSpectrum (int nch)
    {
        for (int j = 0; j < N; ++j)
        {
            const size_t idx = (size_t) ((pos + j) % N);
            const float m = nch == 2 ? 0.5f * (rawRing[0][idx] + rawRing[1][idx]) : rawRing[0][idx];
            rawFrame[(size_t) j] = { m * win[(size_t) j], 0.0f };
        }
        fft.run (rawFrame.data(), false);
        const float norm = 4.0f / (winSum * winSum);
        for (int b = 0; b < B; ++b) P[(size_t) b] = std::norm (rawFrame[(size_t) b]) * norm;
        for (int b = 0; b < B; ++b)
        {
            const float lo = P[(size_t) std::max (0, b - 1)], hi = P[(size_t) std::min (B - 1, b + 1)];
            tmp[(size_t) b] = 0.25f * lo + 0.5f * P[(size_t) b] + 0.25f * hi;
        }
    }

    // long-term broadband floor: falls immediately, rises slowly toward the 6 s minimum
    void trackFloor (float Edb)
    {
        if (! floorInit) { floorDb = Edb; floorInit = true; }
        subMin = std::min (subMin, Edb);
        if (++subCount >= subLen)
        {
            subMins[subIdx] = subMin; subIdx = (subIdx + 1) % kSubs; subMin = 1000; subCount = 0;
        }
        float winMin = subMin;
        for (float v : subMins) winMin = std::min (winMin, v);
        if (Edb < floorDb) floorDb += 0.3f * (Edb - floorDb);
        else if (winMin < 999.0f && winMin > floorDb)
        {
            const float tau = learned ? 3.0f : 0.25f;
            floorDb += (winMin - floorDb) * (1.0f - std::exp (-1.0f / (tau * framesPerSec)));
        }
    }

    double fs = 48000;
    int N = 2048, Ns = 512, H = 128, B = 1025, Bs = 257;
    FFT fft, fftS;
    std::vector<float> win, winS, gainS;
    std::vector<cpx> frameS;
    float winPow = 1, winSum = 1;
    std::vector<float> inRing[2], rawRing[2], outRing[2];
    std::vector<cpx> frame;
    std::vector<float> P, Psm, noise, gain, gainTarget, tmp;
    std::vector<unsigned char> open, humBin;
    std::vector<signed char> humNear;
    std::vector<float> noiseRaw, ref, masked;
    std::vector<unsigned char> pending;
    std::vector<double> cumP, cumR;
    float maskLoDb = 12.0f, maskHiDb = 25.0f, ddBeta = 0.9f;
    int pendFrames = 2;
    std::vector<float> xiPrev;
    std::vector<cpx> rawFrame;
    int pos = 0, hopCount = 0;

    // floor tracking
    static constexpr int kSubs = 12;
    float floorDb = 0, subMin = 1000, subMins[kSubs] {};
    int subIdx = 0, subCount = 0, subLen = 1;
    bool floorInit = false, quiet = false, learned = false;
    int learnedFrames = 0, frames = 0, histIdx = 0, relearnFrames = 0;
    std::vector<float> hist;
    float framesPerSec = 375;
    double totalSlow = 0; float slowCoef = 0.03f;

    // gate
    float tight = 0.5f, openThrDb = 8, closeThrDb = 4, relCoef = 0.1f, brRelCoef = 0.9f;
    int holdFrames = 10, holdLeft = 0;
    float openness = 0, engage = 0;
    bool listenRemoved = false;

    // hum
    HumHunter hunter;
    HumCanceller canceller[2];
    double aQuiet = 0;
    bool humQuiet = false;

    int hunterVersion = -1, humCount = 0, tunedSeries = -1;
    double sigmaF = 0.05, frozenSec = 0;
    float humDepth[HumCanceller::K] {};
    int offVotes[HumCanceller::K] {};
    double tunedHz = 0;
};
} // namespace bkill
