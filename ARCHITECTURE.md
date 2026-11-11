# softSynth — Architecture

A polyphonic additive/subtractive synthesizer running bare-metal on the
**STM32H750B-DK** (Cortex-M7 @ 480 MHz, single-precision FPU). The UI is a
hand-rolled framebuffer on the on-board 4.3" RK043FN48H panel — no TouchGFX, no
designer tool, no LVGL.

This document is the design contract for the project. Code should follow it;
when reality forces a change, update this file.

---

## 1. Goals & shape

- **6-voice polyphonic**, **2 oscillators per voice**, subtractive synthesis:
  oscillators → resonant filter → amplifier, shaped by envelopes and modulated
  by LFOs.
- **Additive oscillator model**: each oscillator sums up to 32 harmonics from a
  precomputed sine lookup table. Harmonic amplitudes are morphable, giving
  continuous timbral interpolation between classic waveforms (sine, triangle,
  square, sawtooth) and arbitrary shapes.
- **Float DSP** (the M7 has an FPU; fixed-point buys us nothing here).
- **Block-based** audio: render in small blocks (~64 samples @ 48 kHz, ≈1.3 ms)
  driven by the SAI DMA.
- **Pure, host-testable DSP core**: `dsp/` has zero HAL dependencies and
  compiles on a desktop, so oscillator/filter/envelope math is unit-tested off
  the target and can be auditioned by rendering to a `.wav`.
- **CV/modular-friendly control model**: notes are gates; pots and (future) CV
  inputs are the same normalized float to the engine.
- **Firmware structure follows Émilie Gillet's Mutable Instruments modules**
  (Braids, Clouds, Plaits, …): the audio interrupt *is* the main loop, DSP is
  a set of plain block-rendering objects with no allocation, and the
  foreground loop only does slow, non-real-time chores. See §3.

---

## 2. Hardware map (from `softSynth.ioc`)

| Peripheral | Configuration | Role |
|---|---|---|
| **SAI2** | Block A master + MCLK (PI4–7); Block B sync slave RX (PG10, DMA2_Stream4) *(implemented)* | Audio out to the codec; Block B = the codec's IN1 analogue input = the modular clock input |
| **I²C4** | PD12 SCL / PD13 SDA, ~100 kHz *(implemented)* | Shared control bus: WM8994 codec (0x34) + FT5336 touch (0x70) |
| **ADC1/2/3** | one channel each (PA0, PC3, PF8 …) | Pots / CV inputs (0–3.3 V) |
| **USB OTG FS** | host (HCD) on CN13 *(implemented)* | USB-MIDI input (vendored ST USBH + MIDI class) |
| **SDMMC1 / QUADSPI** | SD card + QSPI flash | Wavetable / preset / sample storage (only 128 KB internal flash) |
| **USART3** | ST-LINK VCP | Debug logging |
| **LTDC** | RK043FN48H 480×272, PLL3 pixel clock, double-buffered *(implemented)* | Display |
| **FT5336 touch** | I²C4 @ 0x70, INT on PG2 / EXTI2 *(implemented, polled)* | Capacitive touch on the panel |

### Hardware gotchas
- **SAI clock accuracy** *(solved, then fixed again 2026-11-11)*: the CubeMX
  default gave `ErrorAudioFreq = -2.32 %` on SAI2 — an audible ~40-cent pitch
  error. `audio_out` derives the SAI kernel clock from **PLL2** (M=25, N=344,
  P=7 → 49.142 MHz, MCKDIV 4 → MCLK 12.2855 MHz = 256 × 47.99 kHz). **But**
  the CubeMX `HAL_SAI_MspInit()` (Src/stm32h7xx_hal_msp.c) re-selects PLL1Q
  on every `HAL_SAI_Init()`, and PLL1Q is 48 MHz since the USB change: the
  board actually ran at **46.876 kHz, 41 cents flat** (measured with a block
  counter over SWD). `audio_out` now pins MCKDIV and switches the mux back to
  PLL2 after the SAI inits: measured **47,992.7 Hz (−0.3 cents)**. LTDC keeps
  PLL3, untouched.
- **Audio input** *(implemented)*: SAI2 Block B receives the codec's ADC
  (IN1 as a line input — ST's BSP calls it the analogue mic — no mic bias).
  Same frame/slots as the output, synchronous to Block A, its own circular
  DMA with no interrupt: each TX callback reads the capture half the RX DMA
  is not writing. ST's LINE1 codec setup is for a microphone, so `codec.cpp`
  then sets the input mixers to 0 dB (not +30), **mutes the output-mixer →
  input-mixer record path** (ST leaves it at 0 dB, which would feed the
  synth back into the clock input), turns the ADC DRC off and the ADC volume
  to 0 dB. The ADC's hi-fi high-pass (~4 Hz) stays: the input is AC-coupled
  anyway. **Level**: it's a line input — modular 5–10 V gates must be
  attenuated to about 1 V first.
- **DMA can't reach DTCM** *(handled)*: the H7 DMA engines cannot access
  DTCMRAM. The SAI DMA buffer lives in `RAM_D2` (`0x30000000`) via the
  `.dma_buffer` linker section. The LTDC framebuffer stays in AXI-SRAM
  (`0x24000000`, D1) — the 512 KB `.framebuffer` section now holds the
  double-buffered pair, 2 × 480×272×2 = 510 KB, which fills the section.
- **MCLK output bit** *(handled)*: on Rev.B+ silicon the SAI master clock only
  appears if the MCKEN bit is set (`MckOutput = SAI_MCK_OUTPUT_ENABLE`); on
  Rev.Y it is driven regardless. We force ENABLE so it works on both — the ST
  BSP's DISABLE only happens to work because its reference board is Rev.Y.
- **I²C4 is shared, not in the `.ioc`** *(handled)*: both the WM8994 codec and
  the FT5336 touch controller hang off I²C4 (PD12/PD13). The app brings the bus
  up itself via `platform/i2c4` (idempotent — whichever driver inits first wins;
  the other no-ops), since I²C4 is not configured in CubeMX.
- **CPU clock** *(solved)*: the generated `SystemClock_Config()` leaves the
  core on HSI at 64 MHz (VOS2, caches off). `platform/sysclock` re-clocks it
  first thing in `app_main()`: PLL1 from the 25 MHz HSE → **480 MHz** at VOS0
  (400 MHz at VOS1 on rev Y silicon, detected at runtime; this board is rev
  V), AXI/AHB 240 MHz, APBs 120 MHz, 4 flash wait states, I-cache on. Done in
  the app, not in `main.c`, so a CubeMX regenerate can't undo it. Verified on
  hardware over SWD (SYSCLK = PLL1, ODEN set, DWT counts 480 M cycles/s).
  D-cache stays **off** until the framebuffer and DMA buffers get cache
  maintenance or a non-cacheable MPU region.
- **I²C4 kernel clock pinned to HSI** *(handled)*: I2C4 defaults to the APB4
  clock, which re-clocking moves from 64 to 120 MHz. `sysclock` selects HSI
  (64 MHz) for it so the TIMINGR value in `i2c4.cpp` stays valid.
- **I²C timeout** *(handled)*: transfers use a 10 ms timeout instead of the
  HAL's usual 1 s, so a device that stops answering (a wedged touch
  controller) costs one 10 ms stall per frame instead of freezing the UI.
- **Touch axes are swapped** *(handled)*: the FT5336 reports in its own portrait
  frame; `touch::read()` swaps to framebuffer space (display.x = raw_y,
  display.y = raw_x — pure swap, no mirror, verified on hardware).

---

## 3. The real-time model — the audio interrupt is the main loop

We follow the structure of Émilie Gillet's Mutable Instruments firmware
(open source, `pichenettes/eurorack`): there is no separate "control task".
The audio DMA interrupt does all real-time work, once per block, and the
foreground `while(1)` only does what is too slow or blocking to run there.

```
AUDIO INTERRUPT   (SAI DMA half/full-transfer, once per 64-frame block,
                   every 1.33 ms → control rate = block rate = 750 Hz)
   1. read controls     latest ADC values (pots/CV, sampled continuously by
                        ADC+DMA), gate inputs
   2. sequencer clock   advance by `frames`; note_on/off land sample-accurate
   3. modulation        envelopes, LFOs → per-block modulation values
   4. render            engine.render(patch, modulations, out, frames)
        ▲ reads the parameter set (§4); never blocks, never allocates
        │   ───────────── the concurrency boundary ─────────────
FOREGROUND LOOP   (~50 Hz, everything else, preempted by audio at any time)
   poll touch (blocking I²C), update the parameter set / pattern, draw the UI;
   later: preset load/save (QSPI/SD), USB housekeeping
```

Why this shape (and what we take from the Mutable code):

- **Control and audio are never out of step.** Controls are read at the top of
  the same interrupt that renders, so a block always sees one consistent set of
  inputs, with no queue or lock between a control task and the audio.
- **Per-block parameter interpolation.** Values are read once per block and
  ramped linearly across it (Mutable's `ParameterInterpolator` idea); a
  one-pole smoother is kept only where a slower glide is wanted.
- **Patch + modulations.** The engine is rendered from a *patch* (the
  user-set values, written by the UI) plus *modulations* (CV, LFOs,
  envelopes, computed in the interrupt). Pots/CV are read inside the
  interrupt, so they become modulation on top of the patch, not a second
  writer to the parameter set.
- **Plain DSP objects.** `Init()`-style setup, `Render(…, out, size)` per
  block, all state preallocated statically, no heap, no virtuals in the
  inner loop. Lookup tables (sine, pitch→increment, envelope curves) are
  precomputed (a generator script → `const` arrays in flash) rather than
  computed at boot.
- **Desktop harness.** The same DSP compiles on the host and renders to
  `.wav` (§8), as the Mutable repos do with their test folders.

Rules that fall out of it:

- The interrupt must finish well inside one block (1.33 ms). `platform/cpu_load`
  measures it with the DWT cycle counter (`begin()`/`end()` around the render);
  the debug page shows average/peak load per ~0.5 s in the MOD header, red
  above 80 % peak. Baseline today (silence, -O0): ~1.6 k of 640 k cycles,
  ~0.25 %.
- Nothing blocking in the interrupt: no I²C (the touch read alone is ~0.5 ms at
  100 kHz), no display drawing, no flash/SD access, no `printf`.
- The foreground loop may be preempted anywhere, so anything it shares with the
  interrupt is single-writer / single-reader and word-sized (or handed over
  with a flag); see §4.

---

## 4. Control model — CV & gates

Designed the modular way: **every continuous control is a normalized float, and
every note is a gate.** This collapses all input sources into two primitives:

- **Gates / triggers** → `engine.note_on(pitch, velocity)` / `note_off(pitch)`.
  An on-board button held = gate high. A future gate jack into a GPIO produces
  the *same* event — the engine never knows the difference.
- **Continuous controls** (0..1) → parameter targets / modulation sources. A pot
  and a future CV input (the ADCs are already 0–3.3 V) are indistinguishable to
  the engine. A later mod-matrix routing any source to any destination is, in
  effect, modular patching.

The note interface is therefore a **source-agnostic API on the engine**:
`dsp::Engine::note_on(note, velocity, tag)` / `note_off(note, tag)`
*(implemented)*. The `tag` says who played the note — USB-MIDI (0) or the
step sequencer (1) — so a note-off only releases voices of its own source
and the sequencer can never cut off a key held on the keyboard. External
gates / CV become more tags with no engine changes.

USB-MIDI reaches the engine through a lock-free single-producer /
single-consumer event FIFO (`midi::Parser::events`): the USB foreground
pushes every note-on / note-off, the audio interrupt drains it at the top of
each block. A device unplugged with keys down queues an all-notes-off.

### The parameter set (`params/`) *(implemented, first draft)*

`params::Parameters` is a flat table of floats indexed by `params::Id`, one per
user-settable value (osc pitch/shape/octave, mix, envelope, LFOs, tempo). Each
has a static `Spec`: label, range, default, step (0 = continuous) and display
format. Values are stored in **natural units** (semitones, octaves, BPM, or 0..1
where no unit is decided yet); anything that thinks in 0..1 (touch knobs, pots,
CV) goes through `set_normalized()`. The UI is the only writer and the audio
interrupt the only reader; a float store is atomic on the M7, so no locks.

The list is sized for the multi-page UI and meant to change as the DSP
lands: add or re-range entries in `parameters.cpp`; the UI only refers to
parameters by `Id` and takes everything else from the `Spec`.

The step pattern (`sequencer::Pattern`: 16 × {on, MIDI note}) is plain data the
UI edits and the sequencer clock (in the interrupt) reads. Tempo is a
parameter, not part of the pattern, so it can be modulated like anything else.

Play/stop and the playhead cross the boundary through `sequencer::Transport`:
the UI writes `run`, the interrupt writes `step` (−1 when stopped). One writer
per field, one aligned word each.

### The wave slots (`params/wave_slots`) *(implemented)*

Each oscillator has two **slots**, A and B, each a 32-harmonic spectrum; SHP
morphs A → B. The UI writes them (WAV puts a basic wave in both, A / B roll
a random spectrum into one), the audio ISR reads them. A spectrum is 32
floats, not one word, so each slot is **double-buffered**: the UI writes the
spare copy and flips the front index with one release store; the interrupt
reads the front copy. It preempts the UI, never the other way round, so it
never sees a half-written spectrum. The engine then glides to a changed
spectrum over ~5 ms, so a swap mid-note doesn't click.

### The modulation matrix (`modulation/matrix.hpp`) *(implemented, live)*

Six rows of `{source, destination, amount}`, the same plain-data model as the
parameter set: the UI writes, the audio ISR reads. Sources are LFO1, LFO2,
AmpEnv, Tempo; destinations are PIT / PT1 / PT2, SHP / SH1 / SH2, PW / PW1
/ PW2 (each oscillator destination for both oscillators, osc 1 only or osc
2 only — an oscillator gets the sum of its "both" and its own route), MIX,
AMP, CUT; `amount` is −1..1 with 0 = no modulation. Each block `app.cpp` copies the rows into the
engine's `dsp::Patch`, and every voice sums its active rows: LFO1/2
(`dsp/lfo`: SIN / TRI / SAW / SQR / S&H; FREE at 0.05–20 Hz, or SYNC at a
musical length from 4 bars to 1/32 incl. dotted and triplet values, its
phase derived from the song position while the pattern plays so it stays
locked to the bar, free-running at the synced rate when stopped; it follows
the external clock only while pulses arrive — with nothing plugged in it
locks to the internal clock, which keeps running as the reference even
when SRC = EXT), Tempo a
triangle at one cycle per beat (also locked to the beat while playing), AmpEnv the
voice's own envelope (so ENV → CUT is the filter envelope). Full-scale
amounts: Pitch ±12 semitones, Shape / PW / Mix ±1, Amp ×0..2, Cutoff ±the
whole 20 Hz–20 kHz range.

### The clock input (`sequencer/ext_clock`) *(implemented)*

The sequencer runs from its internal clock (SYNC page SRC = INT) or from a
modular clock on the audio input (EXT+, or EXT- for an inverted signal):
**left = clock, right = reset** (a mono cable grounds right: no resets).
`EdgeDetector` finds rising edges per sample through the AC coupling: a
threshold at 40 % of a slow peak follower (never below 2 % of full scale),
hysteresis (re-arm below half of it) and a 4 ms hold-off. `ExtClock` turns
edges into the same `Sequencer::Event` the internal clock makes: DIV 1/16,
1/8, 1/4 (in-between steps spread over the measured pulse interval, every
pulse resyncs to the grid) or 24 / 48 PPQN (every 6th / 12th pulse); gates
at half the measured step; reset → the next pulse is step 0; play → armed
until the next pulse. The tempo is measured from the pulses (and drives the
Tempo mod source); two seconds without a pulse is "no clock". Cost: ~0.5 %
of the CPU.

### The sequencer clock (`sequencer/sequencer`) *(implemented)*

`Sequencer::process(run, bpm, frames)` runs once per block in the audio
interrupt. Steps are 16th notes; position is kept as a fraction of a step, so
tempo changes mid-step keep their progress. A rising `run` starts at step 0 at
the top of the block; stopping and playing again restarts from step 0. It
returns the step that began in the block and the frame offset where it began,
or the frame where the playing step's **gate** closed (half a step in; at most
one of the two per 64-frame block: a step is ≥ 2400 frames even at 300 BPM).

`sequencer::Player` turns those events into notes *(implemented)*: a step
plays its note or its **chord** (`sequencer/chords`: 19 shapes on the step's
root — MAJ, MIN, DIM, AUG, SUS2, SUS4, 6, MIN6, 7, MAJ7, MIN7, MMAJ7, DIM7,
M7B5, 7SUS4, ADD9, 9, MAJ9, MIN9; up to 5 notes); the gate releases them
half a step later — unless the next step is **tied**, in which case they ring
on through it (and through a chain of ties). Stopping releases everything.
The player remembers what it started, so its note-offs always match even if
the pattern is edited meanwhile. `app.cpp` renders the block up to the
event's frame, applies the notes, and renders the rest — sample-accurate.

---

## 5. Layering

- **`dsp/` — pure.** No HAL includes. Floats in, floats out. Compiles on the
  host for unit tests and `.wav` rendering.
- **`platform/` — HAL drivers.** Audio I/O, codec, controls, display.
- **`ui/`** — pages/widgets built on the display driver. HAL-free: it draws
  through `display::` and takes touch as plain `{pressed, x, y}`, so it builds
  and is tested on the host too.
- **`params/`** — the shared parameter set (the UI↔audio boundary).
- **`sequencer/`** — the step pattern (data now; the clock later).
- **`App/app.cpp`** — wires the audio interrupt and the foreground loop
  together.

The display driver is split the same way: `display.cpp` is the HAL half (LTDC,
panel GPIOs, owns the framebuffer), `display_draw.cpp` the pure drawing half
(fill, rects, text), which the host build links against a stand-in framebuffer.

Hardware singletons (display, audio_out) are module-style namespaces; DSP
building blocks are classes/structs (you instantiate many oscillators, voices,
etc.).

---

## 6. Components & folder structure

A `✓` marks what exists today; unmarked entries are planned.

```
App/
  app.cpp                  // ✓ entry: audio callback (MIDI + sequencer →
                           //   engine) + foreground loop (touch → UI)
  display.{hpp,cpp}        // ✓ LTDC bring-up, owns the framebuffer (HAL)
  display_draw.cpp         // ✓ fill / rects / text (pure, host-buildable)
  font8x8.hpp              // ✓ 8x8 bitmap font

  platform/                // HAL-dependent (hardware)
    audio_out.{hpp,cpp}    // ✓ SAI2 + DMA double-buffer → render callback
    codec.{hpp,cpp}        // ✓ WM8994 init/control (over i2c4)
    sysclock.{hpp,cpp}     // ✓ PLL1 → 480 MHz, VOS0, I-cache, I2C4 on HSI
    cpu_load.{hpp,cpp}     // ✓ audio-interrupt load meter (DWT cycles)
    i2c4.{hpp,cpp}         // ✓ shared I²C4 bus (codec + touch)
    touch.{hpp,cpp}        // ✓ FT5336 capacitive touch (polled)
    wm8994/                // ✓ vendored ST WM8994 driver
    controls.{hpp,cpp}     //   ADC pots/CV (DMA) + button/encoder debounce

  dsp/                     // PURE — no HAL, compiles on desktop
    dsp_config.hpp         // ✓ sample rate, block size, float typedefs
    sine_table.{hpp,cpp}   // ✓ 2048-point sine lookup (8 KB RAM, filled at
                           //   boot), shared by every oscillator
    spectrum.{hpp,cpp}     // ✓ 32-harmonic spectra: basic waves, random
                           //   shapes, peak normalising, one-cycle render
    additive_osc.{hpp,cpp} // ✓ additive oscillator: Fourier sum, morph A→B
    ladder.{hpp,cpp}       // ✓ nonlinear Moog-style 4-pole ladder, 2× OS
    envelope.{hpp,cpp}     // ✓ analog-style ADSR
    lfo.{hpp,cpp}          // ✓ LFO: 5 shapes, FREE (Hz) or SYNC (locked
                           //   to the song position)
    patch.hpp              // ✓ the per-block snapshot of every control
    voice.{hpp,cpp}        // ✓ one note: 2× osc → mix → ladder → amp env
    engine.{hpp,cpp}       // ✓ 6 voices, allocation + stealing, tagged
                           //   note_on/off, LFOs, mod matrix, spectra glide

  params/
    parameters.{hpp,cpp}   // ✓ shared UI↔audio parameter set (draft list)
    wave_slots.{hpp,cpp}   // ✓ per-osc A/B spectra, double-buffered
    smoothing.hpp          //   interpolator / one-pole smoother (de-zipper)

  modulation/
    matrix.hpp             // ✓ 6 rows of {source, destination, amount}; plain
                           //   data: UI writes, audio ISR reads

  sequencer/
    pattern.hpp            // ✓ 16-step pattern data (on, note, chord, tie)
    sequencer.{hpp,cpp}    // ✓ clock + gate + Transport, in the audio ISR
    chords.{hpp,cpp}       // ✓ 19 chord shapes + names
    player.{hpp,cpp}       // ✓ steps → note-ons/offs: chords, ties, gates
    ext_clock.{hpp,cpp}    // ✓ clock input: edge detector + external clock

  ui/
    page.hpp               // ✓ Page base + Ctx (model refs), page dimensions
    ui.{hpp,cpp}           // ✓ the 5 pages + tab bar; touch → press/drag/
                           //   release, page routing
    widgets.{hpp,cpp}      // ✓ knob, float knob, crossfader, slider, step
                           //   grid, note box, note picker, tab bar, cycle
                           //   button
    osc_page.{hpp,cpp}     // ✓ OSC: 2× (PIT SHP OCT + WAV/A/B) + MIX,
                           //   FILTER: CUT/RES/DRV, WAVE scope
    env_page.{hpp,cpp}     // ✓ ENV: ATK/DEC/SUS/REL + envelope curve preview
    mod_page.{hpp,cpp}     // ✓ MOD: LFO1/2 rates + 6-row modulation matrix
    seq_page.{hpp,cpp}     // ✓ SEQ: note box, play/stop, chord, tie, tempo,
                           //   4×4 grid; note + chord pickers
    sync_page.{hpp,cpp}    // ✓ SYNC: clock source / division, tempo, input
                           //   level meters with threshold + edge lights
    fx_page.{hpp,cpp}      // ✓ FX: MOD FX / DELAY / REVERB rows
    theme.hpp              // ✓ colours

test/                      // ✓ host build (native gcc, no MCU) — standalone
  CMakeLists.txt           // ✓ pulls doctest via FetchContent
  test_main.cpp            // ✓ doctest entry point
  test_dsp.cpp             // ✓ sine table, spectra, oscillator (pitch, morph,
                           //   band-limit), ladder (slope, resonance, tuning,
                           //   drive), envelope, engine (allocation, tags)
  test_parameters.cpp      // ✓ clamping, snapping, value formatting
  test_ui.cpp              // ✓ page interactions: tab switch, OSC/ENV/MOD/SEQ,
                           //   ENV curve reflects ADSR + no sustain trails, buffer-convergence
                            //   regressions (page switch, one-shot widget,
                            //   picker grid-gap, playhead)
  test_sequencer.cpp       // ✓ step timing, start/stop, tempo, gates, chords,
                           //   player (ties, release on stop)
  test_midi.cpp            // ✓ parser, note-event FIFO
  test_ext_clock.cpp       // ✓ edge detector on AC-coupled / noisy / weak /
                           //   clipped / inverted pulses; divisions, reset,
                           //   arming, tempo, timeout
  wav_writer.hpp           // ✓ 16-bit PCM WAV writer (host-only)
  render_wav.cpp           // ✓ render the DSP to a .wav to audition on a PC
  host_framebuffer.cpp     // ✓ stand-in framebuffer for the host build
  render_ui.cpp            // ✓ screenshot the UI pages to .ppm
```

---

## 7. DSP decisions locked in

- **Float**, single precision.
- **Block size** 64 samples @ **48 kHz** (1.33 ms).
- **Additive oscillator** *(implemented, `dsp/additive_osc`)* — each sample
  is the Fourier sum `Σ c[n]·sin(n·θ)` over up to 32 harmonics:
  - Only sin θ and cos θ come from the 2048-point table (linear
    interpolation); the harmonics follow from the angle-addition recurrence
    `sin((n+2)θ) = 2cos(2θ)·sin(nθ) − sin((n−2)θ)`, run as two independent
    chains (odd / even) so the FPU overlaps them. ~2 FMAs per harmonic
    instead of 32 table reads; error ≈ −75 dB.
  - **Band-limited per block**: each harmonic gets a gain that fades it out
    between 0.41 and 0.45 × the sample rate, and the loop stops at the last
    audible one, so high notes alias-free *and* cheaper, and a harmonic
    gliding across Nyquist fades rather than pops.
  - **Morph**: the oscillator plays `A + shape·(B − A)`. The sum is linear in
    the coefficients, so it keeps two running sums (A and D = B − A) and
    `shape` ramps per sample across the block — zipper-free.
  - **Spectra** (`dsp/spectrum`), all peak-normalised to 1 (so any blend of
    two also peaks ≤ 1):
    - **Sine** `[1, 0, 0, …]`, **Saw** `1/n`, **Square** odd `1/n`,
      **Triangle** odd `±1/n²` (alternating sign — that's what makes it a
      triangle).
    - **Random**: fundamental always present; upper harmonics fall off as
      `1/n^tilt` (random tilt 0.35–1.6), some dropped at random (density
      0.35–1), sometimes odd-only (hollow), a random resonant bump lifting a
      band of harmonics, random signs. Repeatable per seed.
- **PWM for any shape** *(implemented, `AdditiveOsc::render_pw`)* — the
  analogue way, subtracting a phase-shifted copy: y = (w(θ) − m·w(θ + 2πW))
  / (1 + m). On a saw that is exactly a pulse of width W; on any other
  spectrum harmonic n is scaled by |1 − m·e^(i2πnW)|, a comb sweeping through
  the harmonics (a sine only changes level). Still one Fourier sum per
  sample: in the PW knob's fade-in range (amount m < 1, W = 50 %) a
  half-cycle shift is just alternating signs, so it is "even harmonics ×
  (1 − m)/(1 + m)" through the normal path; at full PWM the sum is taken
  as cosines around the midpoint phase θ + πW. Knob: 0 = off, first 10 %
  fades in at 50 %, then the width narrows to 5 %. Measured worst case
  (6 voices, all harmonics, PW on both oscillators): ~57–60 %, the same as
  without.
- **Filter** *(implemented, `dsp/ladder`)* — Huovilainen's nonlinear Moog
  ladder: four tanh-saturating one-pole stages with inverted feedback,
  2× oversampled (input interpolated, output averaged), half-sample feedback
  correction, passband compensation (half the input fed back in, so RES
  doesn't thin the bass), tanh on the output stage (so DRV saturates rather
  than just getting louder). Measured and corrected: **cutoff tuning** (the
  resonant peak lands within 0.6 % of the set cutoff from 100 Hz to 20 kHz)
  and **resonance scaling** per cutoff (self-oscillation starts at ~0.91 on
  RES at any cutoff). tanh is a 1025-point interpolated table (no divides).
  Knobs: CUT 20 Hz–20 kHz exponential, RES, DRV (input gain 0.5–8 with
  makeup gain).
- **Envelope** — analog-style ADSR (one-pole glides; attack aims at 1.3 and
  turns at 1). A retrigger starts from the current level. Attack 1 ms–3 s,
  decay / release 5 ms–8 s.
- **Voices** — 6, each 2 oscillators → mix → ladder → amp envelope. Morph,
  mix, cutoff and gain are ramped across each block. Allocation: same note +
  source retriggers; else a free voice; else steal the quietest releasing
  voice, else the oldest. A stolen voice keeps its phase / filter / level, so
  it glides instead of clicking. Output: voices summed at 0.3 each, then a
  soft clip (the same Padé tanh) to 16 bit.
- **Effects** *(implemented, `dsp/fx`, `modfx`, `delay`, `reverb`)* — mono
  voices → MOD FX (phaser: six swept allpasses + feedback; flanger /
  chorus: a swept 1–6 / 10–25 ms delay; stereo by L/R LFOs 90° apart; FREE
  or SYNC) → stereo delay of the mid (MONO / PING-PONG, tone low-pass +
  soft clip in the feedback, tape-like time glide, 1 s 16-bit lines in
  RAM_D2) → Dattorro plate reverb (one 32k 16-bit buffer with a single
  write head, tank at half rate, 24 kHz). Send-style MIX per effect; MIX 0
  skips the effect entirely. Measured: phaser +3.1 %, delay +3.3 %, reverb
  +4.9 % of the CPU; six voices with everything on **71.8 %**.
- **ITCM**: the per-sample functions (`DSP_HOT`) run from ITCM — with the
  D-cache off, float constants in flash literal pools are uncached reads.
- **CPU budget** — *measured on hardware 2026-11-11* (DWT, Debug build with
  the DSP at `-O2`): idle 2.8 %; worst case — a C2 minor-9 chord held by
  ties, so 5–6 voices with all 32 harmonics on both oscillators — **59.6 %
  average, 70.4 % peak**. A PC-sampling profile puts about two thirds of that
  in the oscillators and a quarter in the ladder. Two optimisations got it
  there from 74 / 87.5 %: the oscillator folds the morph into one
  coefficient set when SHP is steady in the block (one Fourier sum, not
  two), four harmonics per loop step; and the ladder's tanh is a table
  (no float divides). Further levers if needed: ITCM for the two hot loops,
  `-O3`, or 1× oversampling.
- **Parameter smoothing**: per-block linear interpolation of every continuous
  control (§3) to kill zipper noise; one-pole only where a slower glide is
  wanted (the spectra).
- **No allocation / no blocking** anywhere in the audio path.

---

## 8. Host-test workflow

Because `dsp/` is HAL-free, a separate **standalone** CMake project under
`test/` compiles it for the desktop (native gcc, never the ARM toolchain):

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
./test/build/render_wav out.wav
./test/build/render_ui ui    # one screenshot per page: ui_{osc,env,mod,seq,midi,dbg,picker,chords}.ppm
```

- **doctest** (single header, fetched via FetchContent) asserts on oscillator
  frequency, level and aliasing today; envelope timing, filter response and
  voice-stealing as those land.
- `render_wav` runs the real engine through a ~25 s tour (basic waves, morphs,
  a resonant sweep, drive, a sequenced chord progression with a tie) and
  writes a `.wav`, so changes can be *heard* on a PC before flashing. For a synth, audible regression
  testing is invaluable.
- `render_ui` drives the UI through a few states and dumps one screenshot per
  page, so layout changes can be looked at without flashing.
- The same `dsp/`, `params/`, `ui/` and `display_draw` `.cpp` files are also
  compiled into the firmware, so they are built by both toolchains.

---

## 9. Build order / milestones

1. ✅ **Audio path** — accurate SAI2 clock (PLL2 → exact 48 kHz), codec init, a
   test tone streaming via SAI2 + DMA double-buffer. *(Highest-risk; do first.)*
2. ✅ **`dsp/` scaffold + host build** — `dsp_config`, oscillator skeleton, a
   passing desktop test, and `render_wav`.
3. ✅ **Additive oscillator** — `sine_table`, 32-harmonic Fourier sum,
   basic + random spectra, A→B morph. Desktop-tested via `render_wav`.
4. ✅ **One voice** — 2× oscillator → ladder → ADSR amp.
5. ✅ **Filter + LFO** (filter envelope = ENV → CUT in the mod matrix).
6. ✅ **6-voice engine**, driven by USB-MIDI and the sequencer (chords, ties).
7. **Controls → parameters** (pots as CV-style controls, smoothed).
8. **UI** — live parameter display on the panel.

(Touch input landed alongside #2, and the debug page (§11) was built ahead of
the DSP so there is something to hook the engine to; see §10.)

---

## 10. Status

- ✅ **Display** — LTDC bring-up, double-buffered RGB565 in AXI-SRAM (the
  CPU draws the back buffer; a line-0 interrupt swaps the pair when a new
  frame has been drawn, so the panel never scans a half-drawn or a stale
  frame; the box of drawn pixels is pulled into the demoted buffer on the
  same swap, so a one-shot widget change lands in both and can't ghost),
  8×8 bitmap text.
- ✅ **Audio path (§9.1)** — PLL2 → 48 kHz SAI clock, SAI2 Block A master + MCLK,
  DMA2_Stream1 circular double-buffer in `RAM_D2`, WM8994 over I²C4. A 440 Hz
  test tone streams via a per-half-buffer render callback.
  (`platform/audio_out`, `platform/codec`, `platform/i2c4`, `platform/wm8994/`.)
- ✅ **DSP scaffold (§9.2)** — pure `dsp/` (`dsp_config` + oscillator skeleton),
  host-built and unit-tested under `test/`, auditionable via `render_wav`.
- ✅ **Touch + debug button** — FT5336 over I²C4 (`platform/touch`), polled at
  ~50 Hz. `app.cpp` draws an on-screen button; touching it raises a gate
  (`volatile bool`, read in the SAI ISR) that switches the test tone on/off — a
  hand-driven stand-in for the real `note_on`/`note_off` until the engine lands.
- ✅ **Multi-page UI** — `ui/` widgets and a seven-page tab-bar layout
  (OSC / ENV / MOD / FX / SEQ / SYNC / MIDI, §11; the old one-screen DBG page
  was retired 2026-11-11), editing `params::Parameters`,
  `modulation::Matrix` and a `sequencer::Pattern`. Host-tested and
  screenshot-able under `test/`.
- ✅ **FX** — phaser / flanger / chorus, stereo delay, plate reverb (§7).
- ✅ **Synth engine (§9.3–9.6)** — additive oscillators with A/B morph and
  random shapes, nonlinear Moog-style ladder, ADSR, two LFOs + the mod
  matrix, 6 voices with stealing (§7). Polyphonic USB-MIDI in; the sequencer
  plays notes and chords with ties. Host-tested; `render_wav` renders a
  tour of it. Running on hardware (CPU measured, §7); not yet auditioned
  by ear on the board.
- ⬜ §9.7 controls (pots / CV).
- ✅ **Sequencer clock + play/stop** — `sequencer/sequencer`, ticked in the
  audio callback; the play button and the grid's playhead go through
  `sequencer::Transport`. Steps play notes / chords through the Player.
- ✅ **CPU at 480 MHz + load meter** — `platform/sysclock`, `platform/cpu_load`;
  clock and audio-interrupt load shown in the debug page headers (§2, §3).

---

## 11. UI

Hand-rolled on the framebuffer (no TouchGFX / LVGL). Retained-mode widgets:
each owns a rectangle and redraws only itself, only when dirty. Every frame,
`sync()` compares what a widget last drew with the model (parameter set /
pattern) and invalidates on a change, so a value changed from anywhere (touch,
pot, CV, preset load) appears without the UI being told, and an idle screen
costs no drawing. The CPU draws into the back buffer and the LTDC swaps the
pair at line 0, but only once something has actually been drawn (the drawing
calls set a dirty flag that the swap consumes) — otherwise the panel keeps
scanning the last complete frame, so an idle or partially-redrawn UI never
flaps between a fresh and a stale buffer; redraw-on-change still matters
because the foreground loop shares CPU with the audio path, and a full
redraw is a 480×272 fill plus every widget on top of it.

Convergence is the display layer's job, not the UI's: the dirty flag only
guarantees a flip, not that both buffers agree. A one-shot change (a cycled
button label) is drawn into exactly one buffer and the widget then goes
clean, so as long as anything else keeps flipping the pair — the tab bar —
the panel alternates the fresh region against the stale one, a permanent
ghost. `display_draw` therefore tracks the bounding box of everything
written into the back buffer since the last promotion, and `swap_buffers()`
copies that box from the just-promoted front buffer into the newly demoted
back buffer on the same swap; in the line-0 IRQ that runs during vertical
blanking, where the demoted buffer is not scanned, so the copy is
invisible. Both buffers agree on every region drawn since the last flip,
and an idle frame copies nothing.

Touch is polled at ~50 Hz in the foreground loop. `ui::Ui` turns samples into
press / drag / release; the widget under the finger at press owns the touch
until release.

### The pages and the tab bar

The screen is 480×246 for the active page plus a 26 px tab bar along the
bottom. Seven tabs — **OSC, ENV, MOD, FX, SEQ, SYNC, MIDI** — 48 px each, the active
one filled with its page's accent; a press on the bar's tab zone switches
page. The **MIDI** tab carries a small LED in its corner: off with nothing on
CN13, amber while enumerating, green when the MIDI class is running (flashing
white for 60 ms on each packet), red for a device without a MIDI interface.

Right of the tabs is a **status strip**, visible on every page: a play
triangle (SEQ accent) or stop square, the tempo (with an external clock: the
measured tempo in the SYNC accent, `--` with no clock), a 16-pip step row
(beat pips taller, on-steps lighter, the playing step in the SEQ accent;
3 px apart now that seven tabs leave 144 px) and the audio-interrupt load as
`nn%` (red from 80 %; the `CPU` label only where it fits). A status message
(`Ui::set_status`, e.g. an init failure) replaces the strip, in red, while
set. The bar redraws every frame (it is invalidated each frame anyway).

### Look

Black page background, `kPanel` grey cells with rounded corners, and **one
accent per section** (`theme.hpp`): OSC orange, FILTER cyan, ENV green, MOD
violet, SEQ amber, MIDI pink. A widget draws its value / "on" state in its
own accent (`Widget::set_accent`, set by the page's constructor; the default
is orange). Pages head
their sections with `draw_section()` — the label in the accent and a thin
darker rule out to the section's width — instead of grey title bars. Knob
rings are anti-aliased against the cell colour (`theme::mix`), and the last
few pixels of lit arc are drawn bright as the value pointer.

### The OSC page

Full-page, on 56×100 cells of **large** knobs (`set_large(true)`: a 52 px
ring, a knob cap with an anti-aliased pointer, the value at scale 2): rows
**OSC 1** / **OSC 2**, each with PIT / SHP / **PW** (pulse width, 0 = off)
and a stack of four 50×22 cells beside them:

- **WAV** — tap to cycle the basic wave (SIN → TRI → SAW → SQR) and put it
  in *both* slots, so the oscillator plays exactly that wave. The name dims
  once a slot has been randomised since.
- **A** / **B** — tap to roll a new random waveshape into that slot; it stays
  there until rolled again or WAV resets it. Each button shows its letter
  and a small picture of the shape it holds (lit in the accent when it's a
  random one).
- **OCT** — a − / + stepper (`Stepper`): tap the left third to go down an
  octave, the right third to go up.
- **SHP** morphs slot A → slot B; **PW** pulse-width modulates whatever
  shape that gives.

The **MIX** crossfader spans both rows (top = osc 1). On the right,
**FILTER** CUT / RES / DRV in the filter's cyan, and under them the **WAVE**
scope: one cycle of each oscillator's current shape (its two slots blended
at SHP, with its PW applied), osc 1 in orange and osc 2 in white, each brighter the more of it the
MIX lets through. It redraws when a slot, SHP or MIX changes. (It draws the
shapes from the spectra, not a sample tap of the audio output.)

### The ENV page

**ATK / DEC / SUS / REL** knobs across the top, and a read-only preview of the
resulting envelope below: the attack, decay and release spans share three
quarters of the width in proportion to their values, and the sustain level
(a level, not a duration) is the flat top of the remaining quarter. Drawn as
a 2 px green stroke over a vertical gradient fill (`fill_polygon` overload
with top/bottom colours), white handles on the three breakpoints the knobs
move, and A / D / S / R labels under stages wide enough to hold one. The
preview clears its own rect before drawing, like every other widget, so
dragging a knob never leaves ghost strokes behind.

### The MOD page

The 6-row modulation matrix as **routing rows** in two columns of three, each
reading `SRC → DST  amount`: tap-to-cycle source and destination buttons, an
arrow, and a bipolar amount bar (−100..+100, step 5; 0 = off; dragged like a
knob). A row whose amount is 0 does nothing, so its buttons and arrow are
drawn dimmed, and it lights up (accent arrow, accent underline on the
buttons) as soon as the amount leaves zero — from anywhere, via `sync()`.
The **LFO** strip below has, per LFO: a rate knob — SPD (Hz) in FREE, LEN
(4BAR … 1/32, `.` dotted, `T` triplet) in SYNC; the page draws and
hit-tests only the one the mode selects, in the same cell — a shape button
(tap: SIN → TRI → SAW → SQR → S&H, drawn as a small wave) and a MODE
button (FREE / SYNC).

### The SEQ page

The note box, play/stop, **CHORD**, **TIE** and tempo on a bar, the 4×4 step
grid full width below. CHORD shows the selected step's chord; tap it for the
**chord picker** (5×4 names over the grid; picking one sets the chord and
switches the step on). TIE ties the selected step to the one before it — the
previous note or chord rings on through it — and is lit while it is tied.
In the grid an on-step shows its note large and its chord name under it; a
tied step is a bar running through the cell with "TIE" in its corner.
Tapping a tied step un-ties it; picking a note un-ties it too.

### The SYNC page

**CLOCK**: SRC (INT / EXT+ / EXT-) and DIV (1/16, 1/8, 1/4, 24PQ, 48PQ) as
tap-to-cycle buttons (`EnumButton`), and the tempo — internal, measured, or
NO CLOCK — with a light blinking on clock edges while the clock input is in
charge. **INPUT**: a meter per channel (L clock, R reset): the level, the
detector's current threshold as a white tick, an edge light and the edge
count, so a patch can be checked before pressing play. Two lines remind
where to patch and that the input is line level. Accent: sky blue.

### The FX page

Three rows under lime section rules — **MOD FX**: TYPE (PHSR / FLNG / CHRS),
MODE (FREE / SYNC), RATE or LEN, DEPTH, FBK, MIX; **DELAY**: MODE, STEREO
(MONO / PING), MS or LEN, FBK, TONE, MIX; **REVERB**: DECAY, DAMP, MIX. MIX is
always the right-hand column and is the effect's wet level (0 = off, and not
processed). As on the MOD page the time knob shows the free value or the
synced length depending on MODE.

### Widget behaviour

- **Knobs** (Elektron-style: arc, value, label; compact 36×44 or large
  84×100) use *relative* drag: right or
  up increases, ~160 px for a full sweep or 14 px per step for stepped values.
  Nothing jumps on touch-down. Bipolar values fill from 12 o'clock.
- **Cycle buttons** tap-cycle through their names (the matrix's sources and
  destinations).
- **Step grid**: tap toggles a step and selects it; hold (~0.45 s) selects
  without toggling and opens the note picker. An on-step is an accent-tinted
  cell with its note name large in the middle (scale 3);
  the playing step (`Transport::step`) gets a 2 px accent outline, the
  selected one a white outline; beat numbers 1/5/9/13 are brighter.
- **Play/stop** button between the note box and tempo: a play triangle when
  stopped, a lit stop square while playing. Writes `Transport::run`.
- **Note box** shows the selected step and its note; tap it for the picker.
- **Note picker**: one-octave keyboard over the grid with octave −/+; picking a
  key sets the note, turns the step on and closes. Touching outside dismisses.
- **Tempo** is an absolute slider (30–300 BPM), 16th-note steps.
