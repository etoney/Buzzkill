# BuzzKill: intelligent guitar noise gate

A zero-setup noise gate for electric guitar (VST3 / AU / Standalone, built with JUCE 8.0.15) by Castle Danger Music Company. It learns your amp's noise floor by itself, cancels mains hum and buzz, and removes noise **without cutting your sustain**.

## How it works
| Stage | What it does |
|---|---|
| **Noise learning** | Short-FFT analysis tracks the long-term minimum level. Moments that sit at that floor and hold steady for a second are "just the amp", and their spectrum is folded into a per-band noise profile. Decaying notes are never learned (they keep falling). Re-learns throughout the song. |
| **Hum hunter** | During silence, a bank of phase-coherent detectors listens for 50 Hz and 60 Hz harmonic series, picks the mains frequency, and finds which harmonics stand above the hiss. |
| **Hum canceller** | Each hum harmonic is demodulated against an oscillator locked to the measured mains frequency (tracked to about 1 mHz from phase advance in every quiet gap). A steep 4-pole estimate of its amplitude and phase is held while you play, and a synthesized copy is subtracted. Hum is removed during notes, with no notch ringing and no damage to notes sitting near a hum line. |
| **Spectral gate** | A long 2048-point analysis (fine enough to hear faint note tails) drives a short 512-point output path (~11 ms latency). Each band is judged on a smoothed (decision-directed) signal-to-noise estimate with hysteresis, and must show ~20 ms of steady evidence to open unless it jumps in strongly (no lone "whistle" blips). Harmonics of a decaying note stay open while above the noise; when nothing is left, the residual floor fades to true silence. |
| **Masking-aware** | Where the guitar is 25 dB+ above the noise in the surrounding third-octave, nothing is gated (the noise is inaudible there anyway). Gating only fades in as music nears the noise, so loud playing, bends and vibrato are never carved into a wah-like moving filter. |

Controls (all optional): **Release** (Natural ↔ Tight), **Hear Removed**. Latency is ~11 ms.

## Measured (offline test, synthetic guitar + 60 Hz hum, buzz, hiss)
- Gaps: about 125–135 dB reduction (digital silence)
- Hum while notes ring: cancelled 25–40 dB. Mains lock within about 0.003 Hz (50 and 60 Hz).
- Pick attacks: 0.00 dB change. Loud notes: about 35 dB fidelity (the guitar is left alone).
- Note tails follow the clean guitar until they reach the hiss level.
- Guitar texture between harmonics (bends + vibrato lead): frame-to-frame wobble identical to the untouched input (2.8 dB), level change 0.1 dB, so no wah/phasing.

Run it yourself: `cmake --build build --target buzzkill_render && ./build/buzzkill_render out_dir [mains_hz] [release 0..1] [latency 0..2]`. It writes before/after/removed WAVs.

## Building
Requirements: CMake 3.22+, Git, and a C++17 compiler. JUCE 8.0.15 is downloaded automatically (same version as DREAD).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target BuzzKill_VST3 BuzzKill_Standalone   # add BuzzKill_AU on macOS
```

**No compiler?** Push to GitHub. `.github/workflows/build.yml` builds macOS and Windows and attaches them as artifacts on the Actions tab. It also renders the GUI to the `screenshots` branch.

See `readme.txt` for installing unsigned plugins and `controls.txt` for the controls.
