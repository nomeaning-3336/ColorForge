# ColorForge

A color-bass multi-effect for Windows: a **VST3 plugin** and a **standalone player** that loops any audio file you drop on it through the effect.

![ColorForge Player](docs/screenshot.png)

Color bass turns a plain bass sound into something that plays chords: every harmonic gets pulled onto the notes of a scale or chord, then smeared, widened and squashed. ColorForge puts the whole chain in one plugin.

## Signal chain

`Drive → [Resonator · Freq Shift · Pitch Map, in any order] → Chorus → Reverb → OTT → Width → Limiter`

The sub (below a cutoff you choose) can bypass the whole chain so the low end stays clean and mono-safe.

| Stage | What it does |
| --- | --- |
| **Pitch Map** | Phase-vocoder remapping: each spectral partial is moved to the nearest allowed note. Amount, purity (cuts energy far from the notes), ±24 st shift, spectral smear. |
| **Resonator** | Up to 24 tuned voices on the chord tones, stacked over 1–4 octaves. Modes: `Comb +`, `Comb -` (hollow, odd harmonics), `Tone` (sine-like ringing). Decay, damping, stereo detune. |
| **Freq Shift** | Single-sideband shifter, ±2000 Hz, with stereo spread, feedback and an LFO. |
| **Chorus / Reverb** | Widening and wash; the reverb has a freeze switch. |
| **OTT** | 3-band upward + downward compression with depth, time and per-band gain. |
| **Notes** | Root, 15 scales, 17 chords, or **MIDI Follow**: the notes you hold define the note set. |

The analyzer colours every frequency by its pitch class, so you can see which notes the sound is made of.

## Download

Grab the files from the [latest release](../../releases/latest):

- `ColorForge-Player-win64.exe`: the standalone player. No install; just run it. It is unsigned, so Windows SmartScreen may ask for confirmation (*More info → Run anyway*).
- `ColorForge-VST3-win64.zip`: unzip and copy the `ColorForge.vst3` folder to `C:\Program Files\Common Files\VST3\`, then rescan plugins in your DAW.

## Using the player

- Drop an audio file anywhere on the window (WAV, AIFF, FLAC, OGG, MP3). It starts looping through the plugin.
- `Space` plays / stops. Click or drag on the waveform to seek.
- **Export WAV** renders the loaded file with the current settings to a 24-bit WAV.
- Turn on **MIDI Follow** and play the on-screen keyboard (or a MIDI keyboard enabled under *Audio Settings*) to set the chord by hand.
- Headless render with default settings: `ColorForge-Player-win64.exe --render input.wav output.wav`

## Building

Requirements on `PATH`: Git, CMake 3.22+, Ninja and MinGW-w64 GCC (for example [WinLibs](https://winlibs.com/)).

```bat
make.bat
```

The script fetches JUCE 7.0.12 into `JUCE7/`, then builds both targets into `build-mingw/`:

- `build-mingw/ColorForge_artefacts/Release/VST3/ColorForge.vst3`
- `build-mingw/ColorForgePlayer_artefacts/Release/ColorForge Player.exe`

JUCE 7 is used because JUCE 8 no longer supports MinGW. The code has only been built with this toolchain (GCC 16.2, JUCE 7.0.12).

## Status

Early release. Checked so far: offline renders land on the selected chord tones, the VST3 module loads and exposes its classes, and the standalone player plays and exports. It has **not yet been tested inside a DAW**, and the default settings have not been tuned by ear.

## License

ColorForge is released under the [GNU General Public License v3.0](LICENSE). It is built on [JUCE](https://juce.com/), used here under its GPLv3 option.

The frequency shifter uses Olli Niemitalo's allpass Hilbert-transformer coefficients.
