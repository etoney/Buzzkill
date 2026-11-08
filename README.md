# BuzzKill: intelligent guitar noise gate

A zero-setup noise gate for electric guitar (VST3 / AU / Standalone, built with JUCE 8.0.15) by Castle Danger Music Company. It learns your amp's noise floor by itself, cancels mains hum and buzz, and removes noise **without cutting your sustain**.

## How it works
| Stage | What it does |
|---|---|
| **Transparent path** | The audio is never split into frequency bands. It has the hum lines subtracted and passes through one smooth volume gate, so whatever is let through is the untouched signal. 5 ms look-ahead. |
| **Noise learning** | A 2048-point analysis tracks the long-term floor; moments at that floor that hold steady are learned as the noise. If the noise changes and the whole spectrum then holds perfectly still for 2 s (e.g. guitar volume knob turned back up), the new noise is adopted automatically. **Relearn** forces it. |
| **Hum + buzz canceller** | Detects 50/60 Hz mains and which of its first 40 harmonics (to ~2.5 kHz) stand above the hiss. Each line is demodulated against an oscillator locked to the measured mains frequency (read only in truly quiet moments), its amplitude and phase are held while you play, and a synthesised copy is subtracted. After a long hold the leftover is re-measured over 3 mains cycles while cancelling continues. A watchdog mutes any line whose cancellation would make it louder. |
| **Detector** | The analysis hears each note's harmonics far below the broadband hiss (smoothed decision-directed SNR per band, 30 ms persistence so noise can't open it, strict rules near hum lines). A 1 ms transient detector catches pick attacks inside the look-ahead. |
| **Gate** | One gain for the whole signal: opens in under a millisecond, holds while any harmonic remains, releases per the **Release** knob (Natural ~250 ms to Tight ~15 ms) to true silence. |

Controls (all optional): **Release** (Natural ↔ Tight), **Relearn**, **Hear Removed**. Latency: 5 ms.

## Measured (offline test: synthetic guitar with texture, 60 Hz hum, rectifier buzz, hiss)
- Gate open: guitar texture level within 0.1–0.2 dB of untouched, frame-to-frame variation identical to untouched (no filtering, no wah)
- Gaps: digital silence (60–240 dB reduction) at the default setting
- Guitar volume turned back up with nobody playing: silent again in ~3 s on its own, under 1 s with Relearn
- CPU: ~3% of one core at 48 kHz

Run it yourself: `cmake --build build --target buzzkill_render && ./build/buzzkill_render out_dir [mains_hz] [release 0..1]` (writes before/after/removed WAVs; `DA_VOLUME=1` adds the volume-knob scenario).

## Building
Requirements: CMake 3.22+, Git, and a C++17 compiler. JUCE 8.0.15 is downloaded automatically (same version as DREAD).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target BuzzKill_VST3 BuzzKill_Standalone   # add BuzzKill_AU on macOS
```

**No compiler?** Push to GitHub. `.github/workflows/build.yml` builds macOS and Windows and attaches them as artifacts on the Actions tab. It also renders the GUI to the `screenshots` branch.

See `readme.txt` for installing unsigned plugins and `controls.txt` for the controls.
