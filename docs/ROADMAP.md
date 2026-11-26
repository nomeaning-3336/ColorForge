# ColorForge roadmap: from color-bass multi-FX to a mixing & mastering suite

Goal: one VST3 (plus the standalone player) that can take a track or a full mix from raw to
release-ready, while keeping the color-bass chain as one of its modules. Every control is a host
parameter, so all of it can be automated in a DAW. The interface gets an original 8-bit, game-style
look.

## What the popular tools offer

This survey is from general knowledge of roughly 30 widely used mixing and mastering plugins
(the big EQ, compressor, limiter, saturation, imaging, reverb/delay, metering and "assistant"
products), grouped by what they do rather than by product. It is a feature list, not a design
reference: ColorForge implements its own DSP and its own look.

| Category | Features that keep showing up | How common |
| --- | --- | --- |
| **EQ** | 6 to 24 bands, bell / shelf / cut / notch / tilt, steep cut slopes, live spectrum behind the curve, drag points on the graph, mid/side and left/right per band, dynamic EQ bands, linear-phase mode, auto gain, spectrum match | Every suite |
| **Compressor** | Threshold, ratio, attack, release, knee, makeup / auto makeup, wet/dry mix (parallel), sidechain high-pass, stereo link, lookahead, character styles (clean, punchy, vintage opto/FET/VCA), gain reduction meter and history | Every suite |
| **Multiband dynamics** | 3 to 6 bands with movable crossovers, upward and downward compression per band, solo bands, OTT-style "squash" | Very common |
| **Limiter / maximizer** | Lookahead brickwall, ceiling, release, true-peak detection with oversampling, several limiting styles, unity-gain listen, loudness target (LUFS) | Every mastering tool |
| **Saturation** | Tape, tube, transistor and clipper curves, drive, tone/tilt, mix, oversampling, multiband saturation | Very common |
| **Transient / gate** | Attack and sustain shaping, gate / expander with hysteresis, de-esser (split band, listen mode) | Common |
| **Stereo** | Width (overall and per band), mono-below-frequency for the low end, mid/side balance, correlation meter, vectorscope | Every mastering tool |
| **Space** | Algorithmic reverb with pre-delay, size, decay, damping, EQ on the tail; tempo-synced delay with feedback filtering, ping-pong, ducking | Very common |
| **Metering** | Peak and true peak, RMS, LUFS momentary / short-term / integrated, loudness range, gain reduction, spectrum, goniometer | Every suite |
| **Workflow** | Presets and A/B snapshots, undo, per-module bypass and solo, reorderable chain, input/output gain staging, gain-matched bypass, resizable UI, tooltips | Every suite |
| **Assistants** | Reference track matching, "suggest a starting chain", tonal balance targets, resonance suppression | High-end suites |
| **Modulation** | Built-in LFOs / envelope followers routed to any parameter, macro knobs, tempo sync | Creative tools |

## Prioritized roadmap

Each phase ships as its own pull request and keeps the MinGW build working.

### Phase 1: mix & master core (done, PR #1)
- **Parametric EQ**: 8 bands (low cut, low shelf, four bells, high shelf, high cut), each with on/off, frequency, gain, Q; curve drawn over the live spectrum.
- **Bus compressor**: threshold, ratio, attack, release, knee, makeup with auto makeup, parallel mix, sidechain high-pass, gain reduction meter.
- **Saturator**: tape, tube and hard-clip curves with drive, tone and mix.
- **Lookahead limiter**: ceiling, release, oversampled true-peak detection; replaces the old soft clipper (the clipper stays as a mode).
- **Loudness meters**: LUFS momentary, short-term and integrated (BS.1770 K-weighting), true peak, stereo correlation, gain reduction.
- **Input gain, mono-bass below a frequency, master bypass per module.**
- **No pitch-map latency when the pitch map is off** (it currently adds about 85 ms even when off).
- **8-bit game UI**: pixel font, pixel knobs and switches, a limited palette, tabbed "worlds" (COLOR, EQ, DYNAMICS, MASTER), HP-bar style meters and a loudness-target "quest" readout.

### Phase 2: workflow
- Done: factory presets (mix bus, master, vocal, drums, color bass) and user presets on disk.
- Done: A/B snapshots.
- Done: reorderable module chain (EQ, compressor, saturator, stereo).
- Done: mid/side mode per EQ band.
- Next: dynamic EQ bands, gain-matched bypass, multiband controls on the existing OTT (movable crossovers, solo).

### Phase 3: modulation & automation in the standalone program
- Two LFOs and an envelope follower, each assignable to any parameter, with tempo sync in the plugin.
- Four macro knobs.
- Automation lanes in the standalone player: record knob moves while the file loops, play them back, and render them in Export and `--render`.

### Phase 4: more processors
- Transient shaper, gate / expander, de-esser.
- Tempo-synced stereo delay.
- Better reverb (pre-delay, decay time, tail EQ).
- Multiband stereo width.
- Oversampling options for the saturator and limiter.

### Phase 5: assistants and polish
- Reference-track spectrum match and loudness match in the player.
- "Starting point" chain suggestions from the analyzed spectrum.
- Achievements and other game layers in the UI (for example a badge for hitting a loudness target without clipping).
- CI builds for every PR, a smoke test in a DAW, and an installer.
