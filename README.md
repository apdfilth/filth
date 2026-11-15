# Filth

Single-knob **VST3 / LV2 / Standalone** audio plugin built with JUCE and CMake.

| FILTH | Response |
| --- | --- |
| 0% | Dry signal, aligned to the plug-in's reported latency. |
| 25% | Moderate soft overdrive and compression. |
| 50% | Heavy overdrive with asymmetric clipping, wavefolding starting to enter, and codec artifacts. |
| 75% | The stacked drives and spectral glitches dominate the original tone. |
| 100% | Fully stacked drive and full spectral corruption. Frame dropouts, bin shifts, and phase scrambling largely obscure the fundamental. |

The signal runs through compression and soft overdrive, then MP3-style artifacts
and spectral corruption, then asymmetric clipping and wavefolding. All three
drives run at 4× oversampling. The LEDs follow the three drives in signal order.

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

### macOS VST3 development build

Install Apple's Command Line Tools with `xcode-select --install` if needed,
then install CMake and Ninja with `brew install cmake ninja`.

From the repository root, run:

```bash
./scripts/build-macos-vst3.sh
```

The first configure fetches JUCE 9.0.5. Later runs reuse the build directory.
The script builds a Release VST3 for Apple Silicon and Intel, checks its bundle
and code signature, and writes `out/Filth-macos-vst3.zip`. The build keeps the
plugin in the workspace. For a manual CMake build of all formats, pass
`-DFILTH_COPY_PLUGIN_AFTER_BUILD=OFF` to get the same behavior.

### Install and test in Ableton Live

Quit Live and run the installer from the repository root.

```bash
./scripts/install-macos-vst3.sh out/Filth-macos-vst3.zip
```

The installer verifies the signature and installs Filth in the current user's
VST3 plug-in folder.

1. Open Live and enable **Use VST3 Plug-In System Folders** in **Settings → Plug-Ins**.
   Look for **Filth** in the Plug-Ins browser. If it is missing, hold **Option** and click **Rescan**.
2. Add Filth to an audio track with a loop. Open its editor and compare **FILTH**
   at 0%, 25%, 50%, 75%, and 100%. Check that the value updates, that the LEDs light
   in order, and that the original tone gives way to glitches near the top.
3. Record or draw automation for **Filth**, save the Set, and reopen it to check that
   the knob and saved parameter state are restored. Keep the Set as a repeatable test.

See [Ableton's macOS plugin installation guide](https://help.ableton.com/hc/en-us/articles/209068929-Using-AU-and-VST-plug-ins-on-macOS)
and [rescan instructions](https://help.ableton.com/hc/en-us/articles/115000349184-VST-AU-plug-in-doesn-t-appear-in-Live-s-Browser).

DSP lives in `Source/PluginProcessor.cpp`. The editor and custom knob drawing
live in `Source/PluginEditor.cpp`.

<details>
<summary>Faceplate artwork and ImageGen prompt</summary>

`Assets/FilthPedal.jpg` was created with the built-in ImageGen tool.

> Use case: product-mockup. Photorealistic background plate for a guitar stompbox audio-plugin editor, portrait 4:5. Exact overhead orthographic photograph, square to camera. A single rectangular die-cast aluminum effects pedal enclosure with a modest rounded beveled edge almost fills the complete image (roughly 96 percent of width and height). Tiny near-black charcoal surface visible around perimeter. The top face is flat and usable as a UI texture. Fluorescent acid-chartreuse and hot lilac marbled enamel with scattered punk ink doodles, gritty shoegaze DIY zine sensibility. Convincing road wear, chipped corners exposing dull aluminum, scuffs, hairline scratches, grime and localized oxidized speckles. Subtle directional diffuse lighting from top left. Small recessed screw heads near corners. Place the sole text exactly "FILTH" wide and centered in the top quarter, in black bold unruly hand-drawn permanent-marker lettering with a disobedient off-white paint offset for readability. Keep central area and the lower center quiet, flat and unobstructed for a large live rotary knob and a small live acrylic LED cluster to be layered over the photo by the app. The image itself contains zero knobs, LEDs, switches, feet, jacks, cables or other functional hardware apart from corner screws. No other letters, digits, gauges, brands, markings or watermark. This should look like a real beat-up photographed textured object, no 3D illustration gloss or drop shadow around the whole frame.

</details>

## License
GPL-3.0 (required when linking open-source JUCE).
