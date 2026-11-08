# BuzzKill: dynamic guitar noise gate

A zero-setup dynamic noise gate for electric guitar, made to sit right after an amp sim in a DAW (VST3 / AU / Standalone, built with JUCE 8.0.15) by Castle Danger Music Company. While you play it does nothing. When you stop, it closes as soon as the level reaches the noise floor: within milliseconds for palm mutes and chokes, and only after the last harmonic is gone for notes you let ring.

## How it works
| Part | What it does |
|---|---|
| **Audio path** | The look-ahead-delayed input times one smooth gain. Nothing is filtered or subtracted. 5 ms look-ahead. |
| **Fast level** | Peak of the signal over the last 10 ms (always holds a waveform peak of any note down to 50 Hz, so low notes don't ripple, but drops at once when the sound stops). Compared to the *peaks* of the learned noise, because buzz after a high-gain sim is spiky. |
| **Abrupt-stop clamp** | A peak tracker falls at 250 dB/s, faster than any natural decay. If the level drops 12 dB+ below it *and* is at the noise, the stop was abrupt (palm mute / choke): the gate clamps in ~2 ms and the analysis is wiped for longer than its window, so it can't hold the gate open from memory. |
| **Tail detector** | 2048-point analysis with per-band smoothed (decision-directed) SNR against the learned noise spectrum, 30 ms persistence. Keeps the gate open while any harmonic of a ringing note is above the noise in its band. |
| **Hysteresis** | Open: anything above the noise keeps it open. Closed: only playing 8 dB over the noise peaks, or a confirmed note in the analysis, reopens it. |
| **Noise learning** | Learned automatically in quiet moments. Re-learned automatically when the spectrum holds still for 2 s with no pick attack for 3 s (volume knob, preset change). **Relearn** button: ready in ~0.5 s. |

Controls (all optional): **Release** (Natural ↔ Tight, how gently it closes after a ringing note has gone), **Relearn**, **Hear Removed**. Latency: 5 ms.

## Measured (offline test: DI guitar with pickup hum + hiss through a high-gain amp sim)
- While playing: 0 of 2,873 5 ms windows changed (completely transparent); attacks 0.00 dB
- Palm-mute 8ths @160: 0 ms of noise after each chug; 16ths @160: 0.4 ms avg (9 ms max)
- Hand-choked stabs: 4–8 ms of noise after the choke
- Ringing notes are never cut early; the gate closes 80–300 ms after the note reaches the noise (Tight ↔ default)
- Same results at amp-sim gain 15, 60 and 200; at 48 and 96 kHz; mono and stereo
- Guitar volume back up with nobody playing: silent again ~3 s later on its own, ~0.5 s with Relearn
- CPU: ~1% of one core at 48 kHz

Run it yourself: `cmake --build build --target buzzkill_render && ./build/buzzkill_render out_dir [release 0..1] [amp gain]` (writes before/after/removed/clean WAVs; `DA_VOLUME=1` adds the volume-knob scenario).

## Building
Requirements: CMake 3.22+, Git, and a C++17 compiler. JUCE 8.0.15 is downloaded automatically (same version as DREAD).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target BuzzKill_VST3 BuzzKill_Standalone   # add BuzzKill_AU on macOS
```

**No compiler?** Push to GitHub. `.github/workflows/build.yml` builds macOS and Windows and attaches them as artifacts on the Actions tab. It also renders the GUI to the `screenshots` branch.

See `readme.txt` for installing unsigned plugins and `controls.txt` for the controls.
