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
| **SAI2** | Block A master + MCLK (PI4–7); Block B sync slave (PG10) | Audio out to on-board codec; Block B = line/mic in |
| **ADC1/2/3** | one channel each (PA0, PC3, PF8 …) | Pots / CV inputs (0–3.3 V) |
| **USB OTG FS** | device (PCD) | Future USB-MIDI input |
| **SDMMC1 / QUADSPI** | SD card + QSPI flash | Wavetable / preset / sample storage (only 128 KB internal flash) |
| **USART3** | ST-LINK VCP | Debug logging |
| **LTDC** | RK043FN48H 480×272, PLL3 pixel clock | Display (already implemented) |

### Hardware gotchas
- **SAI clock accuracy**: CubeMX currently reports `ErrorAudioFreq = -2.32 %` on
  SAI2 — an audible ~40-cent pitch error. Milestone 1 must derive an accurate
  audio clock (PLL2) for exact 48 kHz; LTDC already owns PLL3.
- **DMA can't reach DTCM**: the H7 DMA engines cannot access DTCMRAM. The SAI
  DMA buffer must live in `RAM_D2` (`0x30000000`). The LTDC framebuffer stays in
  AXI-SRAM (`0x24000000`, D1).
- **Codec needs an I²C driver**: the on-board **WM8994** codec is controlled
  over **I²C4** (PD12 = SCL, PD13 = SDA, AF4, 100 kHz, addr 0x34). I²C4 is not
  in the `.ioc`, so the app brings it up itself (see `platform/codec.cpp`).

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

```
App/
  app.cpp                  // entry; wires the 3 rates together

  platform/                // HAL-dependent (hardware)
    display.{hpp,cpp}      // framebuffer + text  (implemented; to be moved here)
    font8x8.hpp            // 8x8 bitmap font     (implemented)
    audio_out.{hpp,cpp}    // SAI2 + DMA double-buffer → engine.render()
    codec.{hpp,cpp}        // on-board codec init/control over I²C
    controls.{hpp,cpp}     // ADC pots/CV (DMA) + button/encoder debounce + edges

  dsp/                     // PURE — no HAL, compiles on desktop
    dsp_config.hpp         // sample rate, block size, float typedefs
    oscillator.{hpp,cpp}   // band-limited (PolyBLEP) saw/square/tri/sine
    filter.{hpp,cpp}       // resonant state-variable or ladder filter
    envelope.{hpp,cpp}     // ADSR
    lfo.{hpp,cpp}          // modulation source
    voice.{hpp,cpp}        // one note: osc(s) + filter + amp env + filter env
    voice_manager.{hpp,cpp}// 8-voice allocation + stealing (oldest/quietest)
    engine.{hpp,cpp}       // note_on/off/gate API; render(block); mix
    mixer.{hpp,cpp}        // sum voices, master gain, (later) FX

  params/
    parameters.{hpp,cpp}   // shared control↔audio parameter set
    smoothing.hpp          // one-pole smoother (de-zipper)

  ui/
    ui.{hpp,cpp}           // screen routing
    widgets.{hpp,cpp}      // knob / value / meter draw helpers

test/                      // host build (native gcc, no MCU)
  CMakeLists.txt
  test_oscillator.cpp, test_envelope.cpp, ...
  render_wav.cpp           // render the engine to a .wav to audition on a PC
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

Because `dsp/` is HAL-free, a second build target compiles it for the desktop:

- A native CMake preset builds `dsp/` + `test/` with system gcc.
- A small framework (candidate: **doctest** — single header, fast) asserts on
  oscillator frequency, envelope timing, filter response, and voice-stealing.
- `render_wav` runs the engine for a few seconds and writes a `.wav`, so DSP
  changes can be *heard* on a PC before flashing. For a synth, audible
  regression testing is invaluable.

---

## 9. Build order / milestones

1. **Audio path** — accurate SAI2 clock (PLL2 → exact 48 kHz), codec init, a
   test tone streaming via SAI2 + DMA double-buffer. *(Highest-risk; do first.)*
2. **`dsp/` scaffold + host build** — `dsp_config`, one PolyBLEP oscillator, a
   passing desktop test, and `render_wav`. *(Do alongside #1.)*
3. **One voice** — oscillator → ADSR amp → mixer, hardcoded note on hardware.
4. **Filter + filter envelope + LFO.**
5. **8-voice manager + button-driven gates.**
6. **Controls → parameters** (pots as CV-style controls, smoothed).
7. **UI** — live parameter display on the panel.

---

## 10. Status

- ✅ LTDC bring-up, RGB565 framebuffer in AXI-SRAM, 8×8 bitmap-font text.
- ✅ **Audio path (§9.1)**: PLL2 → exact 48 kHz SAI clock, SAI2 Block A master
  + MCLK, DMA2_Stream1 circular double-buffer in RAM_D2, WM8994 over I²C4. A
  440 Hz test tone streams out via a per-half-buffer render callback. Modules:
  `platform/audio_out.{hpp,cpp}`, `platform/codec.{hpp,cpp}`,
  `platform/wm8994/` (vendored ST driver).
- ⬜ §9.2 onward (dsp/ scaffold + host build, voices, filter, …).
