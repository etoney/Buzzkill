// BUZZKILL - dynamic noise gate for electric guitar (use right after your amp sim)
// Castle Danger Music Company
//
// Pure C++ (no JUCE) so it can be tested offline.
//
// The idea: while you play, you don't need a gate at all, so the signal passes untouched.
// The gate only acts when you stop, and how fast it acts depends on HOW you stopped:
//
//  * Abrupt stop (palm mute, choke, string mute): the level crashes toward the noise floor at
//    hundreds of dB per second. The moment it reaches the noise, the gate clamps in ~2 ms, so
//    there is no burst of noise before it closes. A 5 ms look-ahead means it can even close a
//    hair before the noise reaches the output.
//  * Natural fade (let a chord ring): the level sinks slowly. A detailed spectral analysis
//    listens for the note's harmonics below the hiss and keeps the gate open for as long as any
//    of them is still there, then closes gently. Your sustain is not cut.
//  * Opening: a 5 ms look-ahead opens the gate just before the pick attack reaches the output.
//
// Everything is relative to the noise floor, which is learned automatically (and re-learned when
// it changes), so it works after any amp sim. Nothing is filtered: the output is always your
// signal times one smooth gain.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace bkill
{
constexpr double kPi = 3.14159265358979323846;
using cpx = std::complex<float>;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
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
// Live readouts for the editor (written by the audio thread, read by the UI)
struct Telemetry
{
    static constexpr int kMaxBins = 4097;
    std::atomic<int> state { 0 };            // 0 listening, 1 playing, 2 letting go, 3 closed
    std::atomic<float> floorDb { -120 }, inDb { -120 }, outDb { -120 }, removedDb { 0 };
    std::atomic<float> levelDb { 0 };        // fast level above the noise floor (dB)
    std::atomic<float> clampAgo { 99 };      // seconds since the last fast clamp (palm mute)
    std::atomic<int> numBins { 0 };
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

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int scale = fs <= 56000 ? 1 : (fs <= 112000 ? 2 : 4);
        N = 2048 * scale;
        H = N / 8; B = N / 2 + 1;
        D = std::max (1, (int) std::lround (0.005 * fs));                 // 5 ms look-ahead
        fft.init (N);
        win.resize ((size_t) N);
        for (int j = 0; j < N; ++j) win[(size_t) j] = (float) std::sin (kPi * j / N);   // sqrt-Hann
        winSum = 0; for (float v : win) winSum += v;

        for (int c = 0; c < 2; ++c) ring[c].assign ((size_t) N, 0);
        frame.assign ((size_t) N, cpx {});
        P.assign ((size_t) B, 0); Psm.assign ((size_t) B, 0); noise.assign ((size_t) B, 0);
        open.assign ((size_t) B, 0); pending.assign ((size_t) B, 0); xiPrev.assign ((size_t) B, 0);

        framesPerSec = (float) (fs / H);
        ddBeta = std::exp (-1.0f / (0.06f * framesPerSec));
        pendFrames = std::max (2, (int) std::lround (0.03f * framesPerSec));
        subLen = std::max (1, (int) (0.5f * framesPerSec));
        slowCoef = 1.0f - std::exp (-1.0f / (0.08f * framesPerSec));
        bandCoef = 1.0f - std::exp (-1.0f / (0.3f * framesPerSec));
        snapEvery = std::max (1, (int) std::lround (0.5f * framesPerSec));
        hist.assign ((size_t) std::max (1, (int) (2.0f * framesPerSec)), -150.0f);

        const float f = (float) fs;
        envAtt = 1.0f - std::exp (-1.0f / (0.0003f * f));
        envRel = 1.0f - std::exp (-1.0f / (0.0025f * f));
        gateAtt = 1.0f - std::exp (-1.0f / (0.0004f * f));
        clampRel = std::exp (-1.0f / (0.0015f * f));
        peakFall = std::pow (10.0f, -0.25f / 10.0f / (0.001f * f));         // 250 dB/s
        vetoSamples = N + (int) (0.035 * fs);   // longer than the analysis window
        attackHoldSamples = (int) (0.010 * fs);
        blockLen = std::max (1, (int) std::lround (0.001 * fs));
        reset();
        tele.numBins = B; tele.binHz = (float) (fs / N);
        holdSamples = 0;   // force the time constants to be recomputed for this sample rate
        setTightness (tight);
    }

    void reset()
    {
        pos = 0; hopCount = 0;
        for (int c = 0; c < 2; ++c) std::fill (ring[c].begin(), ring[c].end(), 0.0f);
        resetLearning();
        frames = 0; totalSlow = 0;
        gate = 1.0f; env = 0; peak = 0; blockMax = 0; blockCount = 0; blockIdx = 0; windowMax = 0;
        for (auto& v : blocks) v = 0;
        holdLeft = 0; veto = 0; sinceClamp = 1 << 30; isOpen = true;
    }

    // forget the learned noise and listen again (the Relearn button)
    void requestRelearn() { relearnRequested = true; }

    int latencySamples() const { return D; }

    // 0 = natural (long, gentle tails), 1 = tight (fast cut). Palm mutes clamp fast either way.
    void setTightness (float t)
    {
        if (std::abs (t - tight) < 1.0e-6f && holdSamples > 0) return;
        tight = clampf (t, 0, 1);
        openLin  = std::pow (10.0f, 8.0f / 10.0f);                    // opens 8 dB above the noise's peaks
        closeLin = std::pow (10.0f, (3.0f + 2.0f * tight) / 10.0f);    // level counts as "noise" below this
        openThrDb  = 8.0f + 2.0f * tight;                             // spectral detector
        closeThrDb = 4.5f + 2.0f * tight;
        const float rel  = 0.150f * std::pow (0.05f, tight);          // 150 ms -> 7.5 ms release
        const float hold = 0.060f * std::pow (0.10f, tight);          // 60 ms -> 6 ms hold
        gateRel = std::exp (-1.0f / (rel * (float) fs));
        holdSamples = std::max (1, (int) (hold * fs));
        useSpectral = true;            // never cut a note that is still there, even when tight
    }
    void setListenRemoved (bool b) { listenRemoved = b; }

    // L, R: in-place. R may be nullptr for mono.
    void process (float* L, float* R, int n)
    {
        if (relearnRequested.exchange (false)) { resetLearning(); forcedLearn = std::max (1, (int) (0.5f * framesPerSec)); }
        const int nch = R != nullptr ? 2 : 1;
        float* ch[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            float in[2];
            for (int c = 0; c < nch; ++c) { in[c] = ch[c][i]; ring[c][(size_t) pos] = in[c]; }

            // ---- fast level (a few ms) and how fast it is falling
            const float m = nch == 2 ? 0.5f * (in[0] + in[1]) : in[0];
            // level = peak over the last 10 ms: always contains a waveform peak of any note down to
            // 50 Hz (so low notes don't ripple), yet drops right away once the sound stops
            blockMax = std::max (blockMax, m * m);
            if (++blockCount >= blockLen)
            {
                blocks[blockIdx] = blockMax; blockIdx = (blockIdx + 1) % kBlocks;
                blockMax = 0; blockCount = 0;
                windowMax = 0; for (float v : blocks) windowMax = std::max (windowMax, v);
            }
            env = std::max (windowMax, blockMax);
            if (env > peak * 10.0f) sinceOnset = 0; else ++sinceOnset;   // a sudden jump = a played note
            peak = std::max (env, peak * peakFall);          // falls at most 250 dB/s
            hopMax = std::max (hopMax, env);
            // Judged against the noise's own typical PEAKS (buzz after a high-gain amp sim is spiky,
            // so its short-term level swings well above its average on every mains cycle)
            const float ref = noisePk > 0 ? noisePk : 1.0e-20f;
            const float lvl = env / ref;

            if (! learned) { gate += (1.0f - gate) * gateAtt; }
            else
            {
                if (veto > 0) --veto;
                ++sinceClamp;
                if (lvl > openLin)
                {
                    // playing: open (look-ahead means this happens before the attack is heard)
                    gate += (1.0f - gate) * gateAtt;
                    holdLeft = std::max (holdLeft, attackHoldSamples);
                    veto = 0;
                    isOpen = true;
                }
                else if (lvl < closeLin && peak > env * 15.8f)
                {
                    // abrupt stop: the level fell 12 dB+ faster than any natural decay and is now
                    // at the noise floor (palm mute / choke). Clamp now, and don't let the slower
                    // analysis (which still remembers the note) hold the gate open.
                    gate *= clampRel;
                    holdLeft = 0;
                    if (veto == 0) { sinceClamp = 0; forgetSpectral = true; }
                    veto = vetoSamples;
                    isOpen = false;
                }
                else
                {
                    // Hysteresis: while open, anything above the noise keeps it open; once it has
                    // closed, only real playing (above) or a confirmed note in the analysis reopens it,
                    // never a stray noise peak.
                    const bool spectral = useSpectral && musicPresent && veto == 0;
                    const bool stillThere = isOpen ? ((lvl > closeLin && veto == 0) || spectral) : spectral;
                    if (stillThere) { holdLeft = holdSamples; gate += (1.0f - gate) * gateAtt; isOpen = true; }
                    else if (holdLeft > 0) --holdLeft;
                    else { gate *= gateRel; isOpen = false; }
                }
                if (gate < 1.0e-5f) gate = 0.0f;
            }

            // ---- output: the look-ahead-delayed signal times the gate
            const size_t di = (size_t) ((pos + N - D) % N);
            for (int c = 0; c < nch; ++c)
            {
                const float dry = ring[c][di];
                const float wet = dry * gate;
                ch[c][i] = listenRemoved ? dry - wet : wet;
            }
            if (++pos >= N) pos = 0;
            if (++hopCount >= H) { hopCount = 0; processFrame (nch); }
        }
    }

private:
    void resetLearning()
    {
        std::fill (Psm.begin(), Psm.end(), 0.0f);
        std::fill (open.begin(), open.end(), 0);
        std::fill (pending.begin(), pending.end(), 0);
        std::fill (xiPrev.begin(), xiPrev.end(), 0.0f);
        floorDb = 0; floorInit = false; learnedFrames = 0; learned = false; relearnFrames = 0;
        subMin = 1000; std::fill (subMins, subMins + kSubs, 1000.0f);
        subIdx = 0; subCount = 0; histIdx = 0; histFill = 0; quiet = false; noiseTd = 0; noisePk = 0; hopMax = 0;
        for (auto& v : hopMaxes) v = 0;
        std::fill (hist.begin(), hist.end(), -150.0f);
        musicPresent = false;
        snapIdx = 0; snapsFilled = 0; snapCounter = 0;
        for (auto& v : bandSlow) v = -150.0f;
    }

    void processFrame (int nch)
    {
        // windowed frame, both channels packed into one complex FFT (L real, R imag)
        for (int j = 0; j < N; ++j)
        {
            const size_t idx = (size_t) ((pos + j) % N);
            const float l = ring[0][idx] * win[(size_t) j];
            const float r = nch == 2 ? ring[1][idx] * win[(size_t) j] : 0.0f;
            frame[(size_t) j] = { l, r };
        }
        fft.run (frame.data(), false);
        const float norm = 4.0f / (winSum * winSum);
        for (int b = 0; b < B; ++b)
        {
            const cpx z = frame[(size_t) b], zc = std::conj (frame[(size_t) ((N - b) % N)]);
            const cpx xl = 0.5f * (z + zc), xr = cpx (0, -0.5f) * (z - zc);
            float p = std::norm (xl);
            if (nch == 2) p = 0.5f * (p + std::norm (xr));
            P[(size_t) b] = p * norm;
        }
        const float aT = std::exp (-1.0f / (0.01f * framesPerSec));
        double total = 0;
        for (int b = 0; b < B; ++b)
        {
            const float lo = P[(size_t) std::max (0, b - 1)], hi = P[(size_t) std::min (B - 1, b + 1)];
            Psm[(size_t) b] = aT * Psm[(size_t) b] + (1.0f - aT) * (0.25f * lo + 0.5f * P[(size_t) b] + 0.25f * hi);
            total += Psm[(size_t) b];
        }
        const float Edb = powToDb ((float) total);
        totalSlow += (total - totalSlow) * slowCoef;
        const float Es = powToDb ((float) totalSlow);
        ++frames;

        const bool digitalSilence = Edb < -125.0f || frames <= N / H + 3 + (int) (0.1f * framesPerSec);
        if (digitalSilence) totalSlow = total;
        if (! digitalSilence) trackFloor (Es);

        const int hn = (int) hist.size();
        const float E1 = hist[(size_t) ((histIdx + hn / 2) % hn)];
        hist[(size_t) histIdx] = Es; histIdx = (histIdx + 1) % hn;
        if (histFill < hn) ++histFill;

        // noise changed, then the whole spectrum held perfectly still for 2 s (e.g. guitar volume
        // knob turned back up, or a different amp-sim preset): adopt it as the new noise
        const bool stationary = updateStationarity();
        // (a held note always began with a pick attack; turning a knob up doesn't make one)
        if (floorInit && learned && ! digitalSilence && stationary && Es > floorDb + 2.0f && Es < floorDb + 30.0f
            && sinceOnset > (long long) (3.0 * fs))
        {
            floorDb = Es;
            for (auto& v : subMins) v = Es;
            relearnFrames = (int) (0.6f * framesPerSec);
            std::fill (open.begin(), open.end(), 0);
            std::fill (xiPrev.begin(), xiPrev.end(), 0.0f);
        }

        // quiet = at the long-term floor and holding steady for a second
        const bool steady = std::abs (Es - E1) < 1.5f;
        quiet = ! digitalSilence && floorInit && Es < floorDb + 2.5f && steady && histFill >= hn / 2;
        if (forcedLearn > 0)   // just after Relearn: you've told us it's quiet, so learn right away
        {
            --forcedLearn;
            if (! digitalSilence && floorInit) { quiet = true; floorDb = Es; }
        }

        // peak of the fast level over the last ~4 hops (covers at least one mains cycle)
        hopMaxes[hmIdx] = hopMax; hmIdx = (hmIdx + 1) % 4; hopMax = 0;
        const float recentPeak = std::max (std::max (hopMaxes[0], hopMaxes[1]), std::max (hopMaxes[2], hopMaxes[3]));

        if (quiet)
        {
            const bool fast = learnedFrames < (int) (0.15f * framesPerSec) || relearnFrames > 0;
            if (relearnFrames > 0) --relearnFrames;
            const float a = fast ? 0.15f : 1.0f - std::exp (-1.0f / (0.4f * framesPerSec));
            double ms = 0;
            for (int j = N - H; j < N; ++j)
            {
                const size_t idx = (size_t) ((pos + j) % N);
                const float v = nch == 2 ? 0.5f * (ring[0][idx] + ring[1][idx]) : ring[0][idx];
                ms += (double) v * v;
            }
            ms /= H;
            if (learnedFrames == 0)
            {
                for (int b = 0; b < B; ++b) noise[(size_t) b] = Psm[(size_t) b];
                noiseTd = (float) ms; noisePk = recentPeak;
            }
            else
            {
                for (int b = 0; b < B; ++b) noise[(size_t) b] += a * (Psm[(size_t) b] - noise[(size_t) b]);
                noiseTd += a * ((float) ms - noiseTd);
                noisePk += a * (recentPeak - noisePk);
            }
            ++learnedFrames;
            if (learnedFrames > (int) (0.3f * framesPerSec)) learned = true;
        }

        // after a fast clamp the analysis must not keep "remembering" the note that was just muted:
        // its memory stays wiped for longer than its window, then bands must qualify afresh
        if (forgetSpectral || veto > 0)
        {
            forgetSpectral = false;
            std::fill (open.begin(), open.end(), 0);
            std::fill (pending.begin(), pending.end(), 0);
            std::fill (xiPrev.begin(), xiPrev.end(), 0.0f);
        }

        // ---- spectral detector: is any harmonic of the note still above the noise in its band?
        const float oLin = std::pow (10.0f, openThrDb / 10.0f), cLin = std::pow (10.0f, closeThrDb / 10.0f);
        const float strongLin = oLin * 4.0f;
        const int bLo = std::max (1, (int) (60.0 * N / fs)), bHi = std::min (B - 1, (int) (12000.0 * N / fs));
        int openMusic = 0;
        for (int b = bLo; b <= bHi; ++b)
        {
            const float snr = Psm[(size_t) b] / (noise[(size_t) b] + 1.0e-20f);
            const float ml = std::max (snr - 1.0f, 0.0f);
            float& xi = xiPrev[(size_t) b];
            xi = std::min (ddBeta * xi + (1.0f - ddBeta) * ml, 3.0f * ml + 0.3f);
            if (snr > strongLin) xi = std::max (xi, ml);
            const float lv = xi + 1.0f;
            unsigned char& pend = pending[(size_t) b];
            pend = lv > oLin ? (unsigned char) std::min (255, pend + 1) : (unsigned char) 0;
            if (snr > strongLin || pend >= pendFrames) open[(size_t) b] = 1;
            else if (lv < cLin) open[(size_t) b] = 0;
            if (open[(size_t) b]) ++openMusic;
        }
        musicPresent = learned && openMusic > 0;

        // ---- telemetry
        int st = 0;
        if (learned) st = gate > 0.99f ? 1 : (gate > 0.001f ? 2 : 3);
        tele.state = st;
        tele.floorDb = floorInit ? floorDb : -120.0f;
        tele.inDb = Edb;
        const float g2 = gate * gate;
        tele.outDb = powToDb ((float) total * g2);
        tele.removedDb = learned ? clampf (-powToDb (g2), 0.0f, 99.0f) : 0.0f;
        tele.levelDb = learned ? powToDb (env / (noisePk + 1.0e-20f)) : 0.0f;
        tele.clampAgo = (float) std::min (99.0, sinceClamp / fs);
        if ((frames & 3) == 0)
            for (int b = 0; b < B && b < Telemetry::kMaxBins; ++b)
            {
                tele.inSpec[b] = powToDb (Psm[(size_t) b]);
                tele.noiseSpec[b] = learnedFrames > 0 ? powToDb (noise[(size_t) b]) : -150.0f;
                tele.outSpec[b] = powToDb (Psm[(size_t) b] * g2);
            }
    }

    // compares the smoothed spectrum (24 bands) with itself 1 s and 2 s ago
    bool updateStationarity()
    {
        constexpr int nb = kBands;
        float cur[nb] {};
        for (int i = 0; i < nb; ++i)
        {
            const double f0 = 60.0 * std::pow (200.0, (double) i / nb), f1 = 60.0 * std::pow (200.0, (double) (i + 1) / nb);
            const int b0 = std::max (1, (int) (f0 * N / fs)), b1 = std::min (B - 1, std::max (b0, (int) (f1 * N / fs)));
            float e = 0; for (int b = b0; b <= b1; ++b) e += Psm[(size_t) b];
            bandSlow[i] += (powToDb (e) - bandSlow[i]) * bandCoef;
            cur[i] = bandSlow[i];
        }
        if (++snapCounter >= snapEvery)
        {
            snapCounter = 0;
            snapIdx = (snapIdx + 1) % kSnaps;
            for (int i = 0; i < nb; ++i) snaps[snapIdx][i] = cur[i];
            if (snapsFilled < kSnaps) ++snapsFilled;
        }
        if (snapsFilled < kSnaps) return false;
        auto dist = [&] (int back)
        {
            const int idx = (snapIdx - back + kSnaps) % kSnaps;
            float d = 0; for (int i = 0; i < nb; ++i) d += std::abs (cur[i] - snaps[idx][i]);
            return d / nb;
        };
        return dist (2) < 1.0f && dist (4) < 1.0f;
    }

    // long-term broadband floor: falls immediately, rises slowly toward the 6 s minimum
    void trackFloor (float E)
    {
        if (! floorInit) { floorDb = E; floorInit = true; }
        subMin = std::min (subMin, E);
        if (++subCount >= subLen) { subMins[subIdx] = subMin; subIdx = (subIdx + 1) % kSubs; subMin = 1000; subCount = 0; }
        float winMin = subMin;
        for (float v : subMins) winMin = std::min (winMin, v);
        if (E < floorDb) floorDb += 0.3f * (E - floorDb);
        else if (winMin < 999.0f && winMin > floorDb)
        {
            const float tau = learned ? 3.0f : 0.25f;
            floorDb += (winMin - floorDb) * (1.0f - std::exp (-1.0f / (tau * framesPerSec)));
        }
    }

    double fs = 48000;
    int N = 2048, H = 256, B = 1025, D = 240;
    FFT fft;
    std::vector<float> win;
    float winSum = 1;
    std::vector<float> ring[2];
    std::vector<cpx> frame;
    std::vector<float> P, Psm, noise, xiPrev;
    std::vector<unsigned char> open, pending;
    int pos = 0, hopCount = 0;

    // floor tracking + learning
    static constexpr int kSubs = 12;
    float floorDb = 0, subMin = 1000, subMins[kSubs] {};
    int subIdx = 0, subCount = 0, subLen = 1;
    bool floorInit = false, quiet = false, learned = false;
    int learnedFrames = 0, frames = 0, histIdx = 0, histFill = 0, relearnFrames = 0, forcedLearn = 0;
    std::vector<float> hist;
    float framesPerSec = 187.5f;
    double totalSlow = 0; float slowCoef = 0.03f;
    std::atomic<bool> relearnRequested { false };
    static constexpr int kBands = 24, kSnaps = 5;
    float bandSlow[kBands] {}, snaps[kSnaps][kBands] {}, bandCoef = 0.02f;
    int snapIdx = 0, snapsFilled = 0, snapCounter = 0, snapEvery = 94;

    // detectors + gate
    float tight = 0.35f, openThrDb = 8, closeThrDb = 4.5f, ddBeta = 0.9f;
    int pendFrames = 6;
    float openLin = 8, closeLin = 2.5f;
    float noiseTd = 0, noisePk = 0, hopMax = 0, hopMaxes[4] {};
    int hmIdx = 0;
    float env = 0, peak = 0, envAtt = 0.07f, envRel = 0.008f, peakFall = 0.9999f;
    static constexpr int kBlocks = 10;   // 10 x 1 ms
    float blocks[kBlocks] {}, blockMax = 0, windowMax = 0;
    int blockLen = 48, blockCount = 0, blockIdx = 0;
    float gate = 1, gateAtt = 0.05f, gateRel = 0.9999f, clampRel = 0.98f;
    int holdSamples = 0, holdLeft = 0, attackHoldSamples = 480, veto = 0, vetoSamples = 2880;
    long long sinceClamp = 0, sinceOnset = 0;
    bool musicPresent = false, listenRemoved = false, useSpectral = true, isOpen = true, forgetSpectral = false;
};
} // namespace bkill
