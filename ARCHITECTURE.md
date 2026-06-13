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
| **ADC1/2/3** | one channel each (PA0, PC3, PF8 …) | Pots / CV inputs (0–3.3 V) |
| **USB OTG FS** | device (PCD) | Future USB-MIDI input |
| **SDMMC1 / QUADSPI** | SD card + QSPI flash | Wavetable / preset / sample storage (only 128 KB internal flash) |
| **USART3** | ST-LINK VCP | Debug logging |
| **LTDC** | RK043FN48H 480×272, PLL3 pixel clock | Display (already implemented) |

### Hardware gotchas
- **SAI clock accuracy** *(solved)*: the CubeMX default gave `ErrorAudioFreq =
  -2.32 %` on SAI2 — an audible ~40-cent pitch error. `audio_out` now derives
  the SAI kernel clock from **PLL2** (M=25, N=344, P=7 → 49.142 MHz → MCLK ≈
  12.288 MHz), ~0.02 % off exact 48 kHz. LTDC keeps PLL3, untouched.
- **DMA can't reach DTCM** *(handled)*: the H7 DMA engines cannot access
  DTCMRAM. The SAI DMA buffer lives in `RAM_D2` (`0x30000000`) via the
  `.dma_buffer` linker section. The LTDC framebuffer stays in AXI-SRAM
  (`0x24000000`, D1).
- **MCLK output bit**: on Rev.B+ silicon the SAI master clock only appears if
  the MCKEN bit is set (`MckOutput = SAI_MCK_OUTPUT_ENABLE`); on Rev.Y it's
  driven by the divider regardless. We set ENABLE so it works on both — the ST
  BSP's DISABLE only happens to work because its board is Rev.Y.
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

---

## 11. Audio path — how it actually flows (as built)

This section documents the implemented output path end-to-end, since it's the
foundation everything else streams into.

### 11.1 The two clocks involved

Two separate clocks have to be right, and they come from different places:

- **MCLK (master clock)** — the codec's reference, ~12.288 MHz (= 256 × 48 kHz).
  Generated by the SAI from its kernel clock, output on **PI4**. Without a clean
  MCLK the codec can't lock and you get silence or noise.
- **The sample-rate / frame clocks** — `FS` (word/frame select, ~48 kHz, the
  L/R boundary) and `SCK` (bit clock). The SAI derives these from MCLK via the
  frame/slot config (128-bit frame, 4 × 16-bit slots).

The chain that produces them:

```
HSE 25 MHz ─► PLL2 (M=25 → 1 MHz, N=344 → 344 MHz VCO, P=7) ─► 49.142 MHz
            └─ SAI2 kernel clock
49.142 MHz ─► SAI master divider (÷4, chosen by HAL) ─► MCLK ≈ 12.288 MHz (PI4)
MCLK ─► (codec's internal PLL) and ─► SAI frame logic ─► FS/SCK ─► 48 kHz frames
```

`audio_out::clock_config()` sets PLL2; `HAL_SAI_Init` + `__HAL_SAI_ENABLE`
start MCLK. LTDC's pixel clock is a *different* PLL (PLL3) and is left alone.

### 11.2 Sample flow (the steady state)

```
 g_buffer[] in RAM_D2  ──DMA2_Stream1 (circular)──►  SAI2_Block_A FIFO  ──►  PI4-7 ──► WM8994 ──► headphone jack
   ▲   ▲                                                     │
   │   │  half-transfer IRQ ────────► HAL_SAI_TxHalfCpltCallback ─┐
   │   └────────────────────────────────────────────────────────┤
   │      transfer-complete IRQ ───► HAL_SAI_TxCpltCallback  ─────┤
   └──────────────── render_tone(out, frames) ◄──────────────────┘
```

- `g_buffer` is **one buffer split into two halves** of `kBlockSize` (64) stereo
  frames. The DMA runs in **circular** mode, so it never stops — it loops over
  the whole buffer forever.
- While the DMA reads half A and sends it to the SAI, the CPU is free to refill
  half B, and vice-versa. The two interrupts mark the hand-off points:
  - **half-transfer** (`TxHalfCplt`): DMA just finished half A, now reading half
    B → we refill **half A**.
  - **transfer-complete** (`TxCplt`): DMA wrapped past half B → we refill
    **half B**.
- Each callback calls `g_render(out, 64)` — currently `render_tone`, later
  `engine.render`. This is the **audio-rate** boundary from §3: it runs in ISR
  context, must not block or allocate.
- One block = 64 frames / 48 kHz ≈ **1.33 ms** of headroom to compute the next
  block. That's the real-time budget the DSP has to fit inside.

### 11.3 Bring-up order (why it's this order)

`app_main()` does, in sequence:

1. `audio_out::init()` — PLL2, SAI2 GPIO, DMA, `HAL_SAI_Init`, enable SAI.
   **MCLK is now running on PI4.**
2. `codec::init()` — I²C4 up, probe WM8994 (read ID `0x8994`), then configure
   its registers. *This must come after step 1*: the codec samples MCLK during
   init to set up its own clocking.
3. `codec::play()` — un-mute the output path.
4. `audio_out::start(render_tone)` — prime the buffer and kick off the circular
   DMA. Samples start flowing.

If the I²C probe fails (codec doesn't ACK), `init()` returns false and the LCD
shows the red "Audio init FAILED" line instead of streaming.

### 11.4 Two control buses, don't confuse them

- **SAI (PI4–7)** carries the *audio samples* — MCLK, SCK, FS, SD. High speed,
  continuous, DMA-fed.
- **I²C4 (PD12/PD13)** is the *control* bus — a handful of register writes at
  boot to power up the codec, route DAC→headphone, and set volume. It is **not**
  in the audio path; after init it's idle until you change volume.
