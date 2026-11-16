# Filth

Single-knob **VST3 / LV2 / Standalone** audio plugin built with JUCE and CMake.

| FILTH | Response |
| --- | --- |
| 0% | Dry signal, aligned to the plug-in's reported latency. |
| 25% | Moderate soft overdrive and compression. |
| 50% | Heavy overdrive with two wavefolders entering and the first MP3-style pass active. |
| 75% | Two codec passes stack before the wavefolders, and the third begins above this point. |
| 100% | Three codec passes and both wavefolders run at full strength. |

The signal runs through compression and soft overdrive, then three serial
MP3-style analysis and resynthesis passes, then two serial wavefolders. The
drives run at 4× oversampling. Five amber LEDs fill from left to right as FILTH
increases. The percentage appears while you drag the knob.

Click the upper-right corner screw to open the debug panel and solo or bypass
individual stages. See the [signal-flow diagram and knob mappings](docs/signal-flow.md).

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
<summary>Faceplate artwork and ImageGen prompts</summary>

The built-in ImageGen tool created `Assets/FilthPedal.jpg` from the provided face
sketch and generated `Assets/FilthKnob.png` as a separate rotating image.

> Use case: product-mockup. The attached little multicolored abstract face sketch is the sole illustration reference. Create a photoreal, precisely top-down, edge-to-edge narrow rectangular guitar fuzz pedal FACEPLATE product photograph, portrait 4:5. Dark matte warm charcoal-gray enamel on die cast metal, very limited palette of charcoal black, gray, rubbed aluminum and dirty off-white. Convincing road wear with scratches, scuffed old paint, subtle grime and chips along the edges. Four small aged countersunk corner screws. Exactly one word, "filth", in lower-case rough hand-written black or off-white permanent marker in the upper quarter, centered. Exactly one illustration: copy the attached funny face's angular eye and nose profile, lip swoops, clustered eye marks and loose asymmetry as a single cohesive fine black Sharpie doodle on a subdued lighter grey patch of the same enamel, centered below the empty knob area, roughly spanning x 26–74% and y 65–81% of the image. Preserve the reference sketch's spirit and character while translating the colors into Sharpie strokes only. The center around x 50%, y 50% and the strip below y 81% are free for separately composited live controls. No knob, switch, indicator, LED, cable, label other than filth, additional doodles, or bright color. Preserve the appearance of an authentic photographed DIY stompbox.

> Use case: product-mockup. Single isolated vintage black phenolic skirted guitar amplifier control knob, classic old Fender amp silhouette, straight overhead orthographic macro product photograph perfectly centered. A low wide rounded circular flared skirt around a smaller black inset cap, pronounced fine vertical grip ribs, a small faded ivory pointer line on the cap pointing perfectly straight up at 12 o'clock. No numbering, lettering, or logo. Realistic fine scratches, years of worn edges, faded matte black textured Bakelite and a tiny bit of settled dust and patina. Subdued softly diffused studio lighting. Circular footprint, no side perspective, no external shadow, absolutely no surface or other objects; isolated with genuine transparent alpha around the knob. The whole knob fits inside the square canvas with a modest 4% transparent safety margin on all sides. This asset will rotate in an audio plugin UI.

</details>

## License
GPL-3.0 (required when linking open-source JUCE).
