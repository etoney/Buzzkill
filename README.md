# BuzzKill: intelligent guitar noise gate

A zero-setup noise gate for electric guitar (VST3 / AU / Standalone, built with JUCE 8.0.15) by Castle Danger Music Company. It learns your amp's noise floor by itself, cancels mains hum and buzz, and removes noise **without cutting your sustain**.

## How it works
| Stage | What it does |
|---|---|
| **Noise learning** | Short-FFT analysis tracks the long-term minimum level. Moments that sit at that floor and hold steady for a second are "just the amp", and their spectrum is folded into a per-band noise profile. Decaying notes are never learned (they keep falling). Re-learns throughout the song. |
| **Hum hunter** | During silence, a bank of phase-coherent detectors listens for 50 Hz and 60 Hz harmonic series, picks the mains frequency, and finds which harmonics stand above the hiss. |
| **Hum canceller** | Each hum harmonic is demodulated against an oscillator locked to the measured mains frequency (tracked to about 1 mHz from phase advance in every quiet gap). A steep 4-pole estimate of its amplitude and phase is held while you play, and a synthesized copy is subtracted. Hum is removed during notes, with no notch ringing and no damage to notes sitting near a hum line. |
| **Spectral gate** | Every FFT band opens and closes independently against the learned noise, with hysteresis, main-lobe protection and per-band release. Harmonics of a decaying note stay open while they are above the noise. When nothing is left, the residual floor fades to true silence. |

Controls (all optional): **Release** (Natural ↔ Tight), **Mode** (Studio ≈43 ms / Live ≈11 ms latency), **Hear Removed**.

## Measured (offline test, synthetic guitar + 60 Hz hum, buzz, hiss)
- Gaps: about 125–135 dB reduction (digital silence)
- Hum while notes ring: cancelled 25–40 dB. Mains lock within about 0.003 Hz (50 and 60 Hz).
- Pick attacks: 0.00 dB change. Loud notes: about 35 dB fidelity (the guitar is left alone).
- Note tails in Studio mode follow the clean guitar within about 1 dB until they reach the hiss level.

Run it yourself: `cmake --build build --target buzzkill_render && ./build/buzzkill_render out_dir [mains_hz] [release 0..1] [quality 0..2]`. It writes before/after/removed WAVs.

## Building
Requirements: CMake 3.22+, Git, and a C++17 compiler. JUCE 8.0.15 is downloaded automatically (same version as DREAD).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target BuzzKill_VST3 BuzzKill_Standalone   # add BuzzKill_AU on macOS
```

**No compiler?** Push to GitHub. `.github/workflows/build.yml` builds macOS and Windows and attaches them as artifacts on the Actions tab. It also renders the GUI to the `screenshots` branch.

See `readme.txt` for installing unsigned plugins and `controls.txt` for the controls.
