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
| **SAI2** | Block A master + MCLK (PI4–7) *(implemented)*; Block B sync slave (PG10) | Audio out to on-board codec; Block B = line/mic in (future) |
| **I²C4** | PD12 SCL / PD13 SDA, ~100 kHz *(implemented)* | Shared control bus: WM8994 codec (0x34) + FT5336 touch (0x70) |
| **ADC1/2/3** | one channel each (PA0, PC3, PF8 …) | Pots / CV inputs (0–3.3 V) |
| **USB OTG FS** | device (PCD) | Future USB-MIDI input |
| **SDMMC1 / QUADSPI** | SD card + QSPI flash | Wavetable / preset / sample storage (only 128 KB internal flash) |
| **USART3** | ST-LINK VCP | Debug logging |
| **LTDC** | RK043FN48H 480×272, PLL3 pixel clock *(implemented)* | Display |
| **FT5336 touch** | I²C4 @ 0x70, INT on PG2 / EXTI2 *(implemented, polled)* | Capacitive touch on the panel |

### Hardware gotchas
- **SAI clock accuracy** *(solved)*: the CubeMX default gave `ErrorAudioFreq =
  -2.32 %` on SAI2 — an audible ~40-cent pitch error. `audio_out` derives the
  SAI kernel clock from **PLL2** (M=25, N=344, P=7 → 49.142 MHz → MCLK ≈
  12.288 MHz), ~0.02 % off exact 48 kHz. LTDC keeps PLL3, untouched.
- **DMA can't reach DTCM** *(handled)*: the H7 DMA engines cannot access
  DTCMRAM. The SAI DMA buffer lives in `RAM_D2` (`0x30000000`) via the
  `.dma_buffer` linker section. The LTDC framebuffer stays in AXI-SRAM
  (`0x24000000`, D1).
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

The note interface is therefore a **source-agnostic API on the engine**.
On-board buttons are its first driver; USB-MIDI / external gates / CV are added
later as additional sources with no engine changes.

### The parameter set (`params/`) *(implemented, first draft)*

`params::Parameters` is a flat table of floats indexed by `params::Id`, one per
user-settable value (osc pitch/shape/octave, mix, envelope, LFOs, tempo). Each
has a static `Spec`: label, range, default, step (0 = continuous) and display
format. Values are stored in **natural units** (semitones, octaves, BPM, or 0..1
where no unit is decided yet); anything that thinks in 0..1 (touch knobs, pots,
CV) goes through `set_normalized()`. The UI is the only writer and the audio
interrupt the only reader; a float store is atomic on the M7, so no locks.

The list is sized for the debug page and meant to change as the DSP lands: add
or re-range entries in `parameters.cpp`; the UI only refers to parameters by
`Id` and takes everything else from the `Spec`.

The step pattern (`sequencer::Pattern`: 16 × {on, MIDI note}) is plain data the
UI edits and the sequencer clock (in the interrupt) reads. Tempo is a
parameter, not part of the pattern, so it can be modulated like anything else.

Play/stop and the playhead cross the boundary through `sequencer::Transport`:
the UI writes `run`, the interrupt writes `step` (−1 when stopped). One writer
per field, one aligned word each.

### The sequencer clock (`sequencer/sequencer`) *(implemented)*

`Sequencer::process(run, bpm, frames)` runs once per block in the audio
interrupt. Steps are 16th notes; position is kept as a fraction of a step, so
tempo changes mid-step keep their progress. A rising `run` starts at step 0 at
the top of the block; stopping and playing again restarts from step 0. It
returns the step that began in the block and the frame offset where it began,
which is where the engine's sample-accurate `note_on` will hook in (at most one
step per 64-frame block: a step is ≥ 2400 frames even at 300 BPM). No notes are
fired yet.

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
  app.cpp                  // ✓ entry: audio callback (silence for now) +
                           //   foreground loop (touch → UI)
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
    oscillator.{hpp,cpp}   //   additive oscillator: 32-harmonic sine-table sum
                           //   with per-harmonic amplitude (morphable)
    sine_table.hpp         //   precomputed 2048-point sine lookup (8 KB, shared
                           //   across all oscillator instances)
    envelope.{hpp,cpp}     //   ADSR
    filter.{hpp,cpp}       //   resonant state-variable or ladder filter
    lfo.{hpp,cpp}          //   modulation source
    voice.{hpp,cpp}        //   one note: 2× osc + filter + amp env + filter env
    voice_manager.{hpp,cpp}//   6-voice allocation + stealing (oldest/quietest)
    engine.{hpp,cpp}       //   note_on/off/gate API; render(block); mix
    mixer.{hpp,cpp}        //   sum voices, master gain, (later) FX

  params/
    parameters.{hpp,cpp}   // ✓ shared UI↔audio parameter set (draft list)
    smoothing.hpp          //   interpolator / one-pole smoother (de-zipper)

  sequencer/
    pattern.hpp            // ✓ 16-step pattern data (on + note per step)
    sequencer.{hpp,cpp}    // ✓ clock + Transport, runs in the audio interrupt

  ui/
    ui.{hpp,cpp}           // ✓ touch → press/drag/release, page routing
    widgets.{hpp,cpp}      // ✓ knob, crossfader, slider, step grid, note box,
                           //   note picker
    debug_page.{hpp,cpp}   // ✓ the one-screen dev/debug page (§11)
    theme.hpp              // ✓ colours

test/                      // ✓ host build (native gcc, no MCU) — standalone
  CMakeLists.txt           // ✓ pulls doctest via FetchContent
  test_main.cpp            // ✓ doctest entry point
  test_oscillator.cpp      // ✓ pitch / level / anti-alias asserts
  test_parameters.cpp      // ✓ clamping, snapping, value formatting
  test_ui.cpp              // ✓ debug-page interactions (tap, hold, drag)
  test_sequencer.cpp       // ✓ step timing, start/stop, tempo changes
  wav_writer.hpp           // ✓ 16-bit PCM WAV writer (host-only)
  render_wav.cpp           // ✓ render the DSP to a .wav to audition on a PC
  host_framebuffer.cpp     // ✓ stand-in framebuffer for the host build
  render_ui.cpp            // ✓ screenshot the UI pages to .ppm
```

---

## 7. DSP decisions locked in

- **Float**, single precision.
- **Block size** ~64 samples @ **48 kHz** (revisit if CPU budget demands).
- **Additive oscillator with sine lookup table** — each oscillator maintains 32
  independent phase accumulators (one per harmonic). Output is the weighted sum
  of `sine_table[phase_n]` across all harmonics. This gives:
  - Natural alias-free output at all pitches: harmonics above Nyquist
    (SR/2 = 24 kHz) are simply zeroed at note-on time, so the wave
    band-limits itself automatically. No PolyBLEP, no per-octave table sets.
  - Continuous waveform morphing: the 32 amplitude coefficients are just
    floats. Interpolating between two coefficient sets (e.g. sawtooth →
    square) morphs the timbre smoothly with no discontinuities.
  - Classic waveform coefficient sets (precomputed, stored as `const float[32]`):
    - **Sine**: `[1, 0, 0, ...]`
    - **Sawtooth**: `[1, 1/2, 1/3, 1/4, ...]` (all harmonics, 1/n amplitude)
    - **Square**: `[1, 0, 1/3, 0, 1/5, ...]` (odd harmonics only, 1/n amplitude)
    - **Triangle**: `[1, 0, 1/9, 0, 1/25, ...]` (odd harmonics, 1/n² amplitude)
- **CPU budget** (estimated at 8 cycles/harmonic on M7 with sine table):
  `8 × 32 harmonics × 2 oscs × 6 voices × 64 samples = 196 608 cycles/block`
  out of 640 000 available at 480 MHz (~30%). Leaves comfortable headroom for
  filter, envelopes, LFO, mixing, UI, and controls. *At the current 64 MHz
  only ~85 000 were available; the clock is now raised (§2).*
- **Sine lookup table**: 2048 points, single precision float, 8 KB. One global
  instance shared across all oscillators. Linear interpolation between adjacent
  entries for sub-sample phase accuracy.
- **Parameter smoothing**: per-block linear interpolation of every continuous
  control (§3) to kill zipper noise; one-pole only where a slower glide is
  wanted.
- **No allocation / no blocking** anywhere in the audio path.

---

## 8. Host-test workflow

Because `dsp/` is HAL-free, a separate **standalone** CMake project under
`test/` compiles it for the desktop (native gcc, never the ARM toolchain):

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
./test/build/render_wav out.wav
./test/build/render_ui ui    # writes ui_main.ppm + ui_picker.ppm
```

- **doctest** (single header, fetched via FetchContent) asserts on oscillator
  frequency, level and aliasing today; envelope timing, filter response and
  voice-stealing as those land.
- `render_wav` runs the DSP for a few seconds and writes a `.wav`, so changes
  can be *heard* on a PC before flashing. For a synth, audible regression
  testing is invaluable.
- `render_ui` drives the debug page through a few states and dumps the
  framebuffer, so layout changes can be looked at without flashing.
- The same `dsp/`, `params/`, `ui/` and `display_draw` `.cpp` files are also
  compiled into the firmware, so they are built by both toolchains.

---

## 9. Build order / milestones

1. ✅ **Audio path** — accurate SAI2 clock (PLL2 → exact 48 kHz), codec init, a
   test tone streaming via SAI2 + DMA double-buffer. *(Highest-risk; do first.)*
2. ✅ **`dsp/` scaffold + host build** — `dsp_config`, oscillator skeleton, a
   passing desktop test, and `render_wav`.
3. **Additive oscillator** — `sine_table`, 32-harmonic phase accumulator,
   precomputed coefficient sets for saw/square/triangle/sine, morph parameter.
   Desktop-tested first via `render_wav`.
4. **One voice** — 2× oscillator → ADSR amp → mixer, hardcoded note on hardware.
5. **Filter + filter envelope + LFO.**
6. **6-voice manager + button/touch-driven gates.**
7. **Controls → parameters** (pots as CV-style controls, smoothed).
8. **UI** — live parameter display on the panel.

(Touch input landed alongside #2, and the debug page (§11) was built ahead of
the DSP so there is something to hook the engine to; see §10.)

---

## 10. Status

- ✅ **Display** — LTDC bring-up, RGB565 framebuffer in AXI-SRAM, 8×8 bitmap text.
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
- ✅ **Debug UI page + parameter set** — `ui/` widgets and the one-screen debug
  page (§11), editing `params::Parameters` and a `sequencer::Pattern`. Not
  hooked to sound yet: the audio callback streams silence. Host-tested and
  screenshot-able under `test/`.
- ⬜ §9.3 onward (additive oscillator, voice, filter, LFO, 6-voice manager,
  sequencer clock, controls).
- ✅ **Sequencer clock + play/stop** — `sequencer/sequencer`, ticked in the
  audio callback; the play button and the grid's playhead go through
  `sequencer::Transport`. Steps are timed but trigger no notes yet.
- ✅ **CPU at 480 MHz + load meter** — `platform/sysclock`, `platform/cpu_load`;
  clock and audio-interrupt load shown in the debug page headers (§2, §3).

---

## 11. UI

Hand-rolled on the framebuffer (no TouchGFX / LVGL). Retained-mode widgets:
each owns a rectangle and redraws only itself, only when dirty. Every frame,
`sync()` compares what a widget last drew with the model (parameter set /
pattern) and invalidates on a change, so a value changed from anywhere (touch,
pot, CV, preset load) appears without the UI being told, and an idle screen
costs no drawing. That matters because we draw straight into the
scanned-out framebuffer: less redrawing, less flicker.

Touch is polled at ~50 Hz in the foreground loop. `ui::Ui` turns samples into
press / drag / release; the widget under the finger at press owns the touch
until release.

### The debug page

```
+-- OSC ---------------+-----------------------------------------+
| OS1  PIT SHP OCT  M  | STEP 05 C#4 v | > |  120  =====O=====  |
| OS2  PIT SHP OCT  I  |---------------+---+--------------------|
|                   X  |   1   |   2 X  |   3    |   4           |
+-- MOD ---------------+   5   |   6    |   7 X  |   8           |
| ENV  ATK DEC         |   9   |  10    |  11    |  12           |
| LF1  SPD DST DEP     |  13   |  14    |  15    |  16           |
| LF2  SPD DST DEP     |                                         |
+----------------------+-----------------------------------------+
     left third                   right two thirds
```

- **Knobs** (Elektron-style: arc, value, label) use *relative* drag: right or
  up increases, ~160 px for a full sweep or 14 px per step for stepped values.
  Nothing jumps on touch-down. Bipolar values fill from 12 o'clock.
- **MIX** is a vertical crossfader spanning both oscillator rows: top = osc 1,
  bottom = osc 2.
- **Step grid**: tap toggles a step (X) and selects it; hold (~0.45 s) selects
  without toggling and opens the note picker. The playing step is shaded,
  read from `Transport::step`.
- **Play/stop** button between the note box and tempo: a play triangle when
  stopped, a lit stop square while playing. Writes `Transport::run`.
- **Note box** shows the selected step and its note; tap it for the picker.
- **Note picker**: one-octave keyboard over the grid with octave −/+; picking a
  key sets the note, turns the step on and closes. Touching outside dismisses.
- **Tempo** is an absolute slider (30–300 BPM), 16th-note steps.
- The OSC header shows the CPU clock (or, in red, an init failure); the MOD
  header shows the audio-interrupt load as `CPU avg/peak%`.
