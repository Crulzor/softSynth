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
| **LTDC** | RK043FN48H 480×272, PLL3 pixel clock, double-buffered *(implemented)* | Display |
| **FT5336 touch** | I²C4 @ 0x70, INT on PG2 / EXTI2 *(implemented, polled)* | Capacitive touch on the panel |

### Hardware gotchas
- **SAI clock accuracy** *(solved)*: the CubeMX default gave `ErrorAudioFreq =
  -2.32 %` on SAI2 — an audible ~40-cent pitch error. `audio_out` derives the
  SAI kernel clock from **PLL2** (M=25, N=344, P=7 → 49.142 MHz → MCLK ≈
  12.288 MHz), ~0.02 % off exact 48 kHz. LTDC keeps PLL3, untouched.
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

The list is sized for the multi-page UI and meant to change as the DSP
lands: add or re-range entries in `parameters.cpp`; the UI only refers to
parameters by `Id` and takes everything else from the `Spec`.

The step pattern (`sequencer::Pattern`: 16 × {on, MIDI note}) is plain data the
UI edits and the sequencer clock (in the interrupt) reads. Tempo is a
parameter, not part of the pattern, so it can be modulated like anything else.

Play/stop and the playhead cross the boundary through `sequencer::Transport`:
the UI writes `run`, the interrupt writes `step` (−1 when stopped). One writer
per field, one aligned word each.

### The modulation matrix (`modulation/matrix.hpp`) *(implemented)*

Six rows of `{source, destination, amount}`, the same plain-data model as the
parameter set: the UI writes, the audio ISR reads. Sources are LFO1, LFO2,
AmpEnv, Tempo; destinations are Pitch, Shape, Mix, Amp, Cutoff; `amount` is
−1..1 with 0 = no modulation. It replaces the per-LFO `Dest`/`Amount`
parameters; per-block the interrupt will fold each active row into the
destination.

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

  modulation/
    matrix.hpp             // ✓ 6 rows of {source, destination, amount}; plain
                           //   data: UI writes, audio ISR reads

  sequencer/
    pattern.hpp            // ✓ 16-step pattern data (on + note per step)
    sequencer.{hpp,cpp}    // ✓ clock + Transport, runs in the audio interrupt

  ui/
    page.hpp               // ✓ Page base + Ctx (model refs), page dimensions
    ui.{hpp,cpp}           // ✓ the 5 pages + tab bar; touch → press/drag/
                           //   release, page routing
    widgets.{hpp,cpp}      // ✓ knob, float knob, crossfader, slider, step
                           //   grid, note box, note picker, tab bar, cycle
                           //   button
    osc_page.{hpp,cpp}     // ✓ OSC: 2× (PIT SHP OCT) + MIX, FIL: CUT/RES
    env_page.{hpp,cpp}     // ✓ ENV: ATK/DEC/SUS/REL + envelope curve preview
    mod_page.{hpp,cpp}     // ✓ MOD: LFO1/2 rates + 6-row modulation matrix
    seq_page.{hpp,cpp}     // ✓ SEQ: note box, play/stop, tempo, 4×4 grid
    debug_page.{hpp,cpp}   // ✓ DBG: the old one-screen debug layout (§11)
    theme.hpp              // ✓ colours

test/                      // ✓ host build (native gcc, no MCU) — standalone
  CMakeLists.txt           // ✓ pulls doctest via FetchContent
  test_main.cpp            // ✓ doctest entry point
  test_oscillator.cpp      // ✓ pitch / level / anti-alias asserts
  test_parameters.cpp      // ✓ clamping, snapping, value formatting
  test_ui.cpp              // ✓ page interactions: tab switch, OSC/ENV/MOD/SEQ,
                           //   ENV curve reflects ADSR + no sustain trails, buffer-convergence
                            //   regressions (page switch, one-shot widget,
                            //   picker grid-gap, playhead)
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
./test/build/render_ui ui    # one screenshot per page: ui_{osc,env,mod,seq,dbg,picker}.ppm
```

- **doctest** (single header, fetched via FetchContent) asserts on oscillator
  frequency, level and aliasing today; envelope timing, filter response and
  voice-stealing as those land.
- `render_wav` runs the DSP for a few seconds and writes a `.wav`, so changes
  can be *heard* on a PC before flashing. For a synth, audible regression
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
- ✅ **Multi-page UI** — `ui/` widgets and a five-page tab-bar layout
  (OSC / ENV / MOD / SEQ / DBG; the old one-screen debug page kept as the
  fifth tab, §11), editing `params::Parameters`, `modulation::Matrix` and a
  `sequencer::Pattern`. Not hooked to sound yet: the audio callback streams
  silence. Host-tested and screenshot-able under `test/`.
- ⬜ §9.3 onward (additive oscillator, voice, filter, LFO, 6-voice manager,
  controls).
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
bottom. Six tabs — **OSC, ENV, MOD, SEQ, MIDI, DBG** — 48 px each, the active
one filled with its page's accent; a press on the bar's tab zone switches
page. The **MIDI** tab carries a small LED in its corner: off with nothing on
CN13, amber while enumerating, green when the MIDI class is running (flashing
white for 60 ms on each packet), red for a device without a MIDI interface.

Right of the tabs is a **status strip**, visible on every page: a play
triangle (SEQ accent) or stop square, the tempo, a 16-pip step row (beat pips
taller, on-steps lighter, the playing step in the SEQ accent) and the
audio-interrupt load as `CPU nn%` (red from 80 %). A status message
(`Ui::set_status`, e.g. an init failure) replaces the strip, in red, while
set. The bar redraws every frame (it is invalidated each frame anyway).

### Look

Black page background, `kPanel` grey cells with rounded corners, and **one
accent per section** (`theme.hpp`): OSC orange, FILTER cyan, ENV green, MOD
violet, SEQ amber, MIDI pink. A widget draws its value / "on" state in its
own accent (`Widget::set_accent`, set by the page's constructor; the default
is orange, which is what the DBG page's mix of widgets uses). Pages head
their sections with `draw_section()` — the label in the accent and a thin
darker rule out to the section's width — instead of grey title bars. Knob
rings are anti-aliased against the cell colour (`theme::mix`), and the last
few pixels of lit arc are drawn bright as the value pointer.

### The OSC page

Full-page, on one 76×100 cell grid of **large** knobs (`set_large(true)`:
a 52 px ring, a knob cap with an anti-aliased pointer, the value at scale 2):
rows **OSC 1** / **OSC 2**, each with PIT / SHP / OCT; the **MIX** crossfader
in its own column spanning both rows (top = osc 1, bottom = osc 2); on the
right, **FILTER** CUT and RES side by side on row 1, in the filter's cyan —
stored in the parameter set, inert until the filter DSP lands — and under
them the **WAVE** slot (160×100, `Scope` widget) for the oscilloscope: for
now an empty graticule with a flat trace, waiting for a sample source from
the audio side.

### The ENV page

**ATK / DEC / SUS / REL** knobs across the top, and a read-only preview of the
resulting envelope below: the attack, decay and release spans share three
quarters of the width in proportion to their values, and the sustain level
(a level, not a duration) is the flat top of the remaining quarter. The
preview clears its own rect before drawing, like every other widget, so
dragging a knob never leaves ghost strokes behind.

### The MOD page

**LFO1 / LFO2** rate knobs, and the 6-row modulation matrix in two columns of
three rows: per row a source and a destination (tap-to-cycle buttons) and an
**AMT** knob (−100..+100, step 5; 0 = off).

### The SEQ page

The note box, play/stop and tempo on a bar, the 4×4 step grid full width
below.

### The DBG page

The original one-screen debug layout — oscillator / envelope / LFO cells on the
left, the step grid on the right — kept as a fifth tab until the others cover
it. The OSC header shows the CPU clock (or, in red, an init failure); the MOD
header shows the audio-interrupt load as `CPU avg/peak%`.

### Widget behaviour

- **Knobs** (Elektron-style: arc, value, label; compact 36×44 or large
  84×100) use *relative* drag: right or
  up increases, ~160 px for a full sweep or 14 px per step for stepped values.
  Nothing jumps on touch-down. Bipolar values fill from 12 o'clock.
- **Cycle buttons** tap-cycle through their names (the matrix's sources and
  destinations).
- **Step grid**: tap toggles a step (X) and selects it; hold (~0.45 s) selects
  without toggling and opens the note picker. The playing step is shaded,
  read from `Transport::step`.
- **Play/stop** button between the note box and tempo: a play triangle when
  stopped, a lit stop square while playing. Writes `Transport::run`.
- **Note box** shows the selected step and its note; tap it for the picker.
- **Note picker**: one-octave keyboard over the grid with octave −/+; picking a
  key sets the note, turns the step on and closes. Touching outside dismisses.
- **Tempo** is an absolute slider (30–300 BPM), 16th-note steps.
