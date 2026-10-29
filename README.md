# OTT Multiband

A three-band upward + downward compressor (VST3, Windows) built with JUCE 8. It follows Ableton Live's
**OTT** Multiband Dynamics preset and Xfer Records' OTT: the same split points, gains, thresholds,
ratios and timings by default. It splits the bands with a **linear-phase** crossover, so the bands add
back up to the input with no phase shift. The GUI has the look and feel of VIRUS DELAY and is drawn
in code, with no image assets.

![screenshot](docs/screenshot.png)

## Controls

The main view follows the layout of Xfer's OTT. The detailed settings are on the **Advanced** page.

| Section | Controls |
|---------|----------|
| **Top** | **Depth** (dry/wet), **Time** (scales every attack/release), **In Gain**, **Out Gain**. **LINK** (on by default): turning In Gain up turns Out Gain down by the same amount. **Auto Gain** on/off |
| **Bands** | One row per band (H / M / L): **S** (solo) and **A** (band auto gain), an **Amount** knob (how much of the band's compression is used: 0 % = none, 100 % = full OTT, up to 150 % for extra squash), a meter (grey = level in, orange = pushed down, light blue = lifted up, white tick = where it ends up, plus the gain change in dB) and the band's output gain (default 10.3 / 5.7 / 10.3 dB) |
| **Upward / Downward** | Scale each direction (as on Xfer OTT); at 0 % that side is hidden from the bars. **Linear Phase** on = no phase shift (~43 ms latency), off = zero latency. **Soft Knee**, **RMS** (off = peak detection), **Advanced** |
| **Advanced** | Three sections (High / Mid / Low): split frequency with High / Low on/off, power (off = band passes untouched), **S** (solo), **Threshold** (-40 to +24 dB, moves the band's upward and downward thresholds together, double-click = 0), **Attack**, **Release** with **Auto** release (short peaks recover fast, sustained compression slowly). **Crossover Slope**: 6 / 12 / 18 / 24 dB/oct (default 12) |
| **Bars** | **Random**, **Bypass**, factory presets, **Size** (75-200 %), help |

![advanced page](docs/advanced.png)

**Default (OTT) settings**

| Band | Input | Output | Attack | Release | Above thr. / ratio | Below thr. / ratio |
|------|-------|--------|--------|---------|--------------------|--------------------|
| High | 5.20 dB | 10.3 dB | 13.5 ms | 132 ms | -35.5 dB / inf : 1 | -40.8 dB / 1 : 4.17 |
| Mid  | 5.20 dB | 5.70 dB | 22.4 ms | 282 ms | -30.2 dB / 66.7 : 1 | -41.8 dB / 1 : 4.17 |
| Low  | 5.20 dB | 10.3 dB | 47.8 ms | 282 ms | -33.8 dB / 66.7 : 1 | -40.8 dB / 1 : 4.17 |

**Gain matching**: with **LINK** on, raising In Gain to drive the compressor harder lowers Out Gain
by the same amount. **Auto Gain** goes further: it measures the loudness coming into the plugin and
going out (K-weighted, the same weighting LUFS uses, in 100 ms blocks over a sliding 1 s window, so it
settles in about a second) and keeps the output at the input's
loudness, so louder never fools you into thinking it's better. The readout under the button shows the
gain it is applying. Bypass always passes the untouched input.

**Band auto gain (A)** keeps a band's level the same when you change its compression (threshold,
Amount, Upward / Downward), like a compressor's auto make-up. Each band keeps 8 s of its
incoming level (10 ms steps) and works out exactly how much the current settings change its loudness,
so a settings change is compensated at once and the make-up doesn't ride the music. A running
comparison with the compressor's real output corrects for attack and release. In tests the band level
stayed within ~0.2 dB of the uncompressed band, including after a 20 dB threshold change. The band
knob still trims on top, and the corner of the band's bar shows the make-up ("A +x.x").

When **Upward** or **Downward** is at 0 %, that side (its coloured area and orange edge) disappears
from the band bars.

**Random** sets every sound-shaping control to a random value within a musically useful range
(e.g. thresholds -50 to -10 dB, band outputs -6 to +12 dB, the below threshold always under the above
one), so most results are usable starting points. Bypass, Solo, Linear Phase, LINK and Auto Gain are
left alone, and with LINK on, Out Gain follows In Gain. The preset menu shows "Random" until you pick
a preset.

### Mouse

* **Wheel**: 1.0 per notch in the control's own units (dB, ms, %, Hz, ratio). **Shift + wheel**: 0.1.
* **Drag** up/down (shift = fine). **Right-click** (or Ctrl-click) = type a value.
* **Double-click** resets: gain knobs go to **0 dB**, everything else goes to its default.
* **Resize** by dragging the bottom-right corner, or pick 75-200 % in **SIZE**. The window keeps
  its proportions, and the size is saved with the session.

## Linear-phase crossover

**Linear Phase** is on by default. Turn it off for a zero-latency crossover, which has phase shift
around the split points: a 1st-order pair (6 dB/oct), 2nd-order Linkwitz-Riley (12 dB/oct) or 3rd-order
Butterworth (18 dB/oct) or 4th-order Linkwitz-Riley (24 dB/oct), with a matching all-pass so the bands
still sum flat.

**Band gain and slope**: neighbouring bands overlap around each split, more so with gentle slopes. A low
note at 50 Hz is partly in the Mid band too, and Mid's upward compression lifts that part, so turning
L fully down cuts less than 24 dB. Steeper slopes (18 / 24 dB/oct) separate the bands more. Switching fades out,
swaps the crossovers and fades back in, and the host is told about the new latency.

With Linear Phase on:

Two zero-phase FIR low-passes (LP1 at the low split, LP2 at the high split) run as FFT convolutions.
Each has the response 1 / (1 + (f / fc)^k), with k = 1 to 4 for 6, 12, 18 or 24 dB/oct, so the high side
(1 - LP) has the same slope and both sides are -6 dB at the split. Gentle slopes keep the filters short in
time, which avoids the pre-ringing that steep linear-phase filters cause on transients.

```
low  = LP1(x)
mid  = LP2(x) - LP1(x)
high = delay(x) - LP2(x)        ->  low + mid + high = delay(x), exactly
```

* The bands always sum back to the input. A null test with the compression off measures about -148 dB,
  even while the split points are moving.
* The filters are ~85 ms long at every sample rate, so the plugin reports **~43 ms latency** to the host
  (2048 samples at 48 kHz). Your DAW's delay compensation lines everything up. This latency is the cost
  of linear phase, so don't use the plugin on live input monitoring.
* New filters are designed off the audio thread and crossfaded in, so moving a split or switching a
  band on or off doesn't click.

## Project layout

```
CMakeLists.txt
.github/workflows/build.yml     Windows VST3 build -> artifact + "builds" branch
Source/
  Parameters.h                  IDs, ranges, OTT defaults, value formatting
  Presets.h                     factory presets
  PluginProcessor.h/.cpp        parameters, in gain, band split/compress/sum, solo/depth/bypass, state
  PluginEditor.h/.cpp           scalable main view + Advanced page
  CustomLookAndFeel.h/.cpp      knobs, buttons, panels, LCDs (from VIRUS DELAY)
  UI/OttComponents.h/.cpp       knob/value-field mouse handling, dynamics display, help
  DSP/LinearPhaseCrossover.*    FIR crossover (linear phase)
  DSP/MinPhaseCrossover.h       Linkwitz-Riley crossover (zero latency)
  DSP/BandCompressor.h          upward/downward compressor
```

## Building (Windows)

Every push to `main` builds the plugin on GitHub Actions. Download **OTT-Multiband-VST3-Windows-x64**
from the run's artifacts, or the zip from the `builds` branch, and copy `OTT Multiband.vst3` into
`C:\Program Files\Common Files\VST3`.

Building locally needs CMake 3.22 or newer and Visual Studio 2022. JUCE 8.0.4 is downloaded automatically.

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target OTT_VST3
```

The output is `build/OTT_artefacts/Release/VST3/OTT Multiband.vst3`. Options: `-DOTT_BUILD_STANDALONE=ON`
(a test app), `-DOTT_COPY_AFTER_BUILD=ON` (installs after building), `-DJUCE_DIR=...` (uses a local JUCE).

## Notes

"OTT" is used here only to describe the processing style. This project contains no Xfer Records or
Ableton code, artwork or logos.
