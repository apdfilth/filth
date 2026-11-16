# Filth audio path

```mermaid
flowchart TD
    I["Input"] --> C["Compressor"]
    I --> DD["Dry Delay"]
    C --> OS1["4× oversampling"]
    OS1 --> OD["Soft Overdrive"]
    OD --> DS1["Host sample rate"]
    DS1 --> FFT["STFT · 1,024 samples · 512-sample hop"]
    FFT --> LP["MP3 Low-Pass"]
    LP --> MS["MP3 Masking"]
    MS --> Q["MP3 Quantization"]
    Q --> IFFT["Pass 1 inverse FFT / overlap-add"]
    IFFT --> P2["MP3 Pass 2 · reanalysis and reconstruction"]
    P2 --> P3["MP3 Pass 3 · reanalysis and reconstruction"]
    P3 --> OS2["4× oversampling"]
    OS2 --> AC["Wavefolder 1"]
    AC --> WF["Wavefolder 2"]
    WF --> DS2["Host sample rate"]
    DS2 --> DC["DC Blocker"]
    DC --> T["Output Trim"]
    T --> S["Soft Ceiling"]
    S --> M["Dry/Wet Mix"]
    DD --> M
    M --> O["Output"]
```

FILTH is a normalized value from 0 to 1. The DSP ramps changes over 20 ms.
In the table, `S(a,b)` is a smoothstep ramp from 0 to 1 between the indicated
knob percentages. Compressor settings update per processing block, spectral
settings update per FFT hop, and drive and output settings update per sample.

| Debug stage | What FILTH controls | Other settings |
| --- | --- | --- |
| **Compressor** | With `c = S(0,70)`, threshold is `−30 × c dB` and ratio is `(1 + 15 × c):1`. | 3 ms attack, 60 ms release. |
| **Soft Overdrive** | Blend `d = S(0,50)` into the tanh-shaped signal. Pre-gain is `1 + 10 × d`. | Runs inside the first 4× oversampling pass. |
| **MP3 Low-Pass** | Spectral strength `m = S(18,100)^1.25`. Cutoff moves geometrically from Nyquist to `min(3,600 Hz, 0.8 × Nyquist)` as `m` increases, rounded down to a retained FFT bin. | Brickwall bin cutoff. |
| **MP3 Masking** | Band gate is `0.20 × m` of the frame peak. Bin gate is `0.62 × m` of the band peak. | Band width grows with frequency. |
| **MP3 Quantization** | Step size is `0.48 × m²` of the band peak. | Rounds real and imaginary bin coefficients. |
| **MP3 Pass 2** | Strength is `S(50,100)^1.25`. | Cutoff approaches 2,900 Hz. Maximum band gate 25%, bin gate 69%, and quantization step 60% of band peak. |
| **MP3 Pass 3** | Strength is `S(75,100)^1.25`. | Cutoff approaches 2,300 Hz. Maximum band gate 30%, bin gate 76%, and quantization step 72% of band peak. |
| **Wavefolder 1** | Blend `a = S(22,67)`. Pre-gain is `1 + 2.75 × a`. | Triangle fold inside the second 4× oversampling pass. |
| **Wavefolder 2** | Blend `w = S(42,86)`. Pre-gain is `1 + 4 × w`. | Triangle fold after Wavefolder 1. |
| **DC Blocker** | Fixed. | 15 Hz high-pass, after the second oversampling pass. |
| **Output Trim** | Wet gain is `1 − 0.35 × S(0,50) × (1 − S(50,80))`. It bottoms out near −3.74 dB at 50%. | Gain returns to unity at 80%. |
| **Soft Ceiling** | Fixed. | Soft knee at 0.90 amplitude (−0.92 dBFS), approaching a ceiling of 1.0 (0 dBFS). |
| **Dry/Wet Mix** | Wet fraction is `S(0,18)`. At 0% FILTH, the output is the delayed dry signal. | Dry Delay compensates for 1,024 samples per codec pass plus 61 samples per oversampling pass, for 3,194 samples total. |

## Debug panel

Click the upper-right corner screw to switch views. The checkboxes enable and
bypass individual stages. **Solo** enables only that row, and clicking it again
enables all stages. **Bypass all** plays latency-aligned dry audio so you can
compare against a repeatable reference. Disabling **Dry/Wet Mix** while other
stages are enabled passes the wet chain at 100%.

The readouts show settings sampled from audio processing, including configured
settings for bypassed rows. The first pass exposes separate controls for its
three spectral operations. Passes 2 and 3 each expose one switch for the whole
pass. Each pass has independent STFT and overlap-add state. Bypassed passes
skip their FFTs and retain the same timing through overlap-add. Stage changes apply at the next block or FFT hop.
Closing the panel or editor restores all stages. The switches are temporary and
live separately from Live's saved plugin state and automation.
