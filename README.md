
# Filth

Single-knob **VST3 / LV2 / Standalone** audio plugin built with JUCE and CMake.

Turning up **FILTH** (`0%` – `100%`) drives an increasingly aggressive lo-fi chain:
1. **High-Ratio Compressor** — `1:1` to `20:1` ratio, threshold `0 dB` down to `-32 dB` (`3 ms` attack, `60 ms` release) with auto makeup gain.
2. **4x Oversampled Soft Clipper** — Linear-phase FIR oversampling into normalized `tanh` saturation (up to `+21 dB` drive).
3. **MP3 Spectral Artifact Stage** — `1024`-sample 50%-overlap Princen–Bradley sine-windowed STFT applying critical-band psychoacoustic masking, coarse coefficient quantization, and brickwall high-frequency rolloff down to `3.6 kHz`, followed by a `-0.9 dBFS` soft-knee safety ceiling.
<img width="272" height="319" alt="Screenshot_20261115_143832" src="https://github.com/user-attachments/assets/dae96f9a-ff2b-4c9f-8749-785277005a53" />
## Building

### Linux (Fedora)
```bash
sudo dnf install gcc-c++ cmake ninja-build git alsa-lib-devel freetype-devel \
  fontconfig-devel libX11-devel libXrandr-devel libXinerama-devel \
  libXcursor-devel libXcomposite-devel mesa-libGL-devel
```

### Configure & Compile (Linux / macOS / Windows)
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## License
GPL-3.0 (required when linking open-source JUCE).
