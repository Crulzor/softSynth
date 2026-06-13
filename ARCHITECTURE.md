# softSynth — Architecture

A polyphonic subtractive synthesizer running bare-metal on the
**STM32H750B-DK** (Cortex-M7 @ 480 MHz, single-precision FPU). The UI is a
hand-rolled framebuffer on the on-board 4.3" RK043FN48H panel — no TouchGFX, no
designer tool, no LVGL.

This document is the design contract for the project. Code should follow it;
when reality forces a change, update this file.

---

## 1. Goals & shape

- **8-voice polyphonic**, **subtractive** synthesis: oscillators → resonant
  filter → amplifier, shaped by envelopes and modulated by LFOs.
- **Float DSP** (the M7 has an FPU; fixed-point buys us nothing here).
- **Block-based** audio: render in small blocks (~64 samples @ 48 kHz, ≈1.3 ms)
  driven by the SAI DMA.
- **Pure, host-testable DSP core**: `dsp/` has zero HAL dependencies and
  compiles on a desktop, so oscillator/filter/envelope math is unit-tested off
  the target and can be auditioned by rendering to a `.wav`.
- **CV/modular-friendly control model**: notes are gates; pots and (future) CV
  inputs are the same normalized float to the engine.

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
- **Touch axes are swapped** *(handled)*: the FT5336 reports in its own portrait
  frame; `touch::read()` swaps to framebuffer space (display.x = raw_y,
  display.y = raw_x — pure swap, no mirror, verified on hardware).

---

## 3. The real-time model — three rates

A synth is not one loop; it is three, and the boundaries between them are the
core of the design.

```
AUDIO RATE   (per-sample, in the SAI DMA ISR, processed in blocks of ~64)
   engine.render(block): voices → filter → amp → mix → codec buffer
        ▲ reads smoothed parameters; never blocks, never allocates
        │   ───────────── the concurrency boundary ─────────────
CONTROL RATE (~1 kHz, main loop or timer)
   read pots (ADC+DMA), debounce buttons → note events + parameter targets
        │
UI RATE      (~30–60 Hz, main loop, lowest priority)
   draw the framebuffer from a snapshot of synth state
```

- The audio render runs in **interrupt context**: lean, no blocking, no heap.
- The main loop owns **controls + UI**.
- The shared surface is the **parameter set**: the control side writes *targets*,
  the audio side *reads and smooths* them. Single-writer / single-reader
  discipline — no locks. Getting this boundary right is what keeps audio
  glitch-free.

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

---

## 5. Layering

- **`dsp/` — pure.** No HAL includes. Floats in, floats out. Compiles on the
  host for unit tests and `.wav` rendering.
- **`platform/` — HAL drivers.** Audio I/O, codec, controls, display.
- **`ui/`** — screens/widgets built on the display driver.
- **`params/`** — the shared parameter model and smoothing (the control↔audio
  boundary).
- **`App/app.cpp`** — wires the three rates together.

Hardware singletons (display, audio_out) are module-style namespaces; DSP
building blocks are classes/structs (you instantiate many oscillators, voices,
etc.).

---

## 6. Components & folder structure

A `✓` marks what exists today; unmarked entries are planned.

```
App/
  app.cpp                  // ✓ entry; for now: debug touch-gated test tone
  display.{hpp,cpp}        // ✓ framebuffer + text (lives at App/ root)
  font8x8.hpp              // ✓ 8x8 bitmap font

  platform/                // HAL-dependent (hardware)
    audio_out.{hpp,cpp}    // ✓ SAI2 + DMA double-buffer → render callback
    codec.{hpp,cpp}        // ✓ WM8994 init/control (over i2c4)
    i2c4.{hpp,cpp}         // ✓ shared I²C4 bus (codec + touch)
    touch.{hpp,cpp}        // ✓ FT5336 capacitive touch (polled)
    wm8994/                // ✓ vendored ST WM8994 driver
    controls.{hpp,cpp}     //   ADC pots/CV (DMA) + button/encoder debounce

  dsp/                     // PURE — no HAL, compiles on desktop
    dsp_config.hpp         // ✓ sample rate, block size, float typedefs
    oscillator.{hpp,cpp}   // ✓ band-limited (PolyBLEP) saw/square/tri/sine
    envelope.{hpp,cpp}     //   ADSR
    filter.{hpp,cpp}       //   resonant state-variable or ladder filter
    lfo.{hpp,cpp}          //   modulation source
    voice.{hpp,cpp}        //   one note: osc(s) + filter + amp env + filter env
    voice_manager.{hpp,cpp}//   8-voice allocation + stealing (oldest/quietest)
    engine.{hpp,cpp}       //   note_on/off/gate API; render(block); mix
    mixer.{hpp,cpp}        //   sum voices, master gain, (later) FX

  params/
    parameters.{hpp,cpp}   //   shared control↔audio parameter set
    smoothing.hpp          //   one-pole smoother (de-zipper)

  ui/
    ui.{hpp,cpp}           //   screen routing
    widgets.{hpp,cpp}      //   knob / value / meter draw helpers

test/                      // ✓ host build (native gcc, no MCU) — standalone
  CMakeLists.txt           // ✓ pulls doctest via FetchContent
  test_main.cpp            // ✓ doctest entry point
  test_oscillator.cpp      // ✓ pitch / level / anti-alias asserts
  wav_writer.hpp           // ✓ 16-bit PCM WAV writer (host-only)
  render_wav.cpp           // ✓ render the DSP to a .wav to audition on a PC
```

---

## 7. DSP decisions locked in

- **Float**, single precision.
- **Block size** ~64 samples @ **48 kHz** (revisit if CPU budget demands).
- **Band-limited oscillators (PolyBLEP)** — naive saw/square alias badly; build
  band-limiting in from the start.
- **Parameter smoothing** (one-pole) on every continuous control to kill zipper
  noise.
- **No allocation / no blocking** anywhere in the audio path.

---

## 8. Host-test workflow

Because `dsp/` is HAL-free, a separate **standalone** CMake project under
`test/` compiles it for the desktop (native gcc, never the ARM toolchain):

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
./test/build/render_wav out.wav
```

- **doctest** (single header, fetched via FetchContent) asserts on oscillator
  frequency, level and aliasing today; envelope timing, filter response and
  voice-stealing as those land.
- `render_wav` runs the DSP for a few seconds and writes a `.wav`, so changes
  can be *heard* on a PC before flashing. For a synth, audible regression
  testing is invaluable.
- The same `dsp/` `.cpp` files are also compiled into the firmware, so they are
  built by both toolchains.

---

## 9. Build order / milestones

1. ✅ **Audio path** — accurate SAI2 clock (PLL2 → exact 48 kHz), codec init, a
   test tone streaming via SAI2 + DMA double-buffer. *(Highest-risk; do first.)*
2. ✅ **`dsp/` scaffold + host build** — `dsp_config`, one PolyBLEP oscillator, a
   passing desktop test, and `render_wav`.
3. **One voice** — oscillator → ADSR amp → mixer, hardcoded note on hardware.
4. **Filter + filter envelope + LFO.**
5. **8-voice manager + button-driven gates.**
6. **Controls → parameters** (pots as CV-style controls, smoothed).
7. **UI** — live parameter display on the panel.

(Touch input + an on-screen debug button landed alongside #2 to drive gates by
hand; see §10.)

---

## 10. Status

- ✅ **Display** — LTDC bring-up, RGB565 framebuffer in AXI-SRAM, 8×8 bitmap text.
- ✅ **Audio path (§9.1)** — PLL2 → 48 kHz SAI clock, SAI2 Block A master + MCLK,
  DMA2_Stream1 circular double-buffer in `RAM_D2`, WM8994 over I²C4. A 440 Hz
  test tone streams via a per-half-buffer render callback.
  (`platform/audio_out`, `platform/codec`, `platform/i2c4`, `platform/wm8994/`.)
- ✅ **DSP core (§9.2)** — pure `dsp/` (`dsp_config` + PolyBLEP `oscillator`),
  host-built and unit-tested under `test/`, auditionable via `render_wav`.
- ✅ **Touch + debug button** — FT5336 over I²C4 (`platform/touch`), polled at
  ~50 Hz. `app.cpp` draws an on-screen button; touching it raises a gate
  (`volatile bool`, read in the SAI ISR) that switches the test tone on/off — a
  hand-driven stand-in for the real `note_on`/`note_off` until the engine lands.
- ⬜ §9.3 onward (voice, filter, LFO, 8-voice manager, params, UI).
