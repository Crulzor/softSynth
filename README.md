# softSynth

Softsynth firmware for the **STM32H750B-DK** Discovery board (STM32H750XBH6,
Cortex-M7 @ up to 480 MHz, 4.3" 480×272 RK043FN48H LCD, WM8994 audio codec).

Milestones so far: paint the LCD, bitmap-font text, audio out of the on-board
WM8994 codec, touch input, a six-page touch UI, USB-MIDI host input, and the
synth itself: a **6-voice** engine with two **additive oscillators** per voice
(32-harmonic Fourier series; basic waves plus random shapes in two slots, with
SHP morphing between them), a nonlinear **Moog-style ladder filter**, ADSR,
two LFOs and a modulation matrix — played polyphonically from USB-MIDI and
from a 16-step sequencer with **chords** and **ties**. The firmware follows the
structure of Émilie Gillet's Mutable Instruments modules: the audio interrupt is
the main loop. See `ARCHITECTURE.md` for the design and roadmap.

## Layout

- `App/` — application code (**C++17**). `display.*` brings up the LTDC panel and
  `display_draw.cpp` draws into its framebuffer; `app.cpp` is the entry point
  (`app_main()`), called from the generated `main()`.
- `App/platform/` — hardware drivers: `audio_out.*` (SAI2 + DMA, accurate 48 kHz
  PLL2 clock), `codec.*` (WM8994), `i2c4.*` (shared I²C4 bus used by the codec
  and touch), `touch.*` (FT5336 capacitive touch, polled), and the vendored ST
  `wm8994/` driver. I²C4 and SAI2 are configured by the app, not by the generated
  `MX_*_Init()`.
- `App/dsp/` — **pure, HAL-free DSP**: sine table, spectra, additive
  oscillator, ladder filter, envelope/LFO, voice, 6-voice engine. Compiles
  into the firmware *and* on the desktop for unit tests.
- `App/params/` — the shared parameter set and the oscillators' wave slots,
  which the UI writes and the audio interrupt reads. `App/sequencer/` — the
  step pattern, clock, chord shapes and the note player.
- `App/ui/` — HAL-free widgets and the debug page; also host-built, tested and
  screenshot-able.
- `test/` — standalone host build (native gcc) for the DSP: doctest unit tests
  plus `render_wav` to audition the sound on a PC. See **Host tests** below.
- `Src/`, `Inc/`, `Drivers/` — STM32CubeMX-generated HAL + startup.
- `softSynth.ioc` — CubeMX project (based on the STM32H750B-DK board template,
  "all peripherals" config, so SAI2/FMC/etc. are available for later).
- `cubemx-generate.txt` — headless CubeMX script used to (re)generate the project:
  `stm32cubemx -q cubemx-generate.txt`.

## Build

```sh
cmake --preset Debug
cmake --build build/Debug
```

Output: `build/Debug/softSynth.elf`.

## Host tests

The DSP core is HAL-free, so a separate native build runs its unit tests and
renders audio to a `.wav` — no board required:

```sh
cmake -S test -B test/build
cmake --build test/build
ctest --test-dir test/build --output-on-failure   # DSP, parameter + UI tests
./test/build/render_wav out.wav                   # audition the engine (~25 s tour)
./test/build/render_ui ui                         # UI screenshots (.ppm)
```

## Flash

```sh
STM32_Programmer_CLI -c port=SWD mode=UR -w build/Debug/softSynth.elf -v -rst
```

After flashing: the LCD opens on the OSC page, with a tab bar along the
bottom (OSC, ENV, MOD, SEQ, MIDI, DBG) and a status strip showing transport,
tempo, step pips and the audio-interrupt load (`CPU nn%`). Keys on a USB-MIDI
keyboard on CN13 play the synth (6 voices); play on the SEQ page runs the
pattern through it, chords and ties included. Init failures replace the
status strip in red (`NO AUDIO` if the codec doesn't answer on I²C,
`NO TOUCH` if the FT5336 doesn't).

Build note: in the Debug preset the DSP sources are compiled `-O2` and the
HAL-free UI/model sources `-Og` (see `CMakeLists.txt`) — the audio interrupt
needs the speed, and the 128 KB flash needs the size.

## Manual changes on top of the raw CubeMX output

The board template is a *generic* default and is wrong for the real panel, so a
few things were corrected by hand (these get overwritten if you regenerate from
CubeMX — re-apply them):

1. **`Src/main.c`** — all `MX_*_Init()` calls except `MX_GPIO_Init()` are
   `#if 0`'d out for now. `MX_SDMMC1_MMC_Init()` traps in `Error_Handler()` with
   no eMMC present, and the LCD is brought up by the app instead of the generic
   `MX_LTDC_Init()`. `app_main()` is called from `USER CODE BEGIN 2`.
2. **`Src/stm32h7xx_hal_msp.c`** — `HAL_LTDC_MspInit()` PLL3 values fixed to give
   a ~9.6 MHz pixel clock (template default produced an unusable ~75 MHz).
3. **`App/display.cpp`** — LTDC configured with correct RK043FN48H timings; the
   panel control lines (PD7 DISP, PB12 RST, PK0 backlight) are driven, which the
   template leaves as inputs/in-reset. Drives a single RGB565 layer over a
   double-buffered RGB565 pair in AXI-SRAM (`.framebuffer` linker section);
   the CPU draws the back buffer and the LTDC line-0 interrupt swaps the
   two when a new frame has been drawn (dirty flag set by the drawing
   calls), so the panel never scans a half-drawn or a stale frame. Each swap
   also copies the bounding box of the pixels drawn since the last
   promotion into the demoted buffer (`detail::flush_box()`, tracked by
   `display_draw.cpp`), so a one-shot widget change lands in both buffers
   and can't ghost while anything else keeps the pair flipping.

The CPU clock is **not** set in `main.c` either: the generated
`SystemClock_Config()` leaves the core at 64 MHz, and `App/platform/sysclock`
raises it to 480 MHz at the start of `app_main()`, so regenerating can't undo it.

Note: the **audio, touch, DSP and UI code is *not* on this list** — everything
under `App/platform/` (`audio_out`, `codec`, `i2c4`, `touch`, …), `App/dsp/`,
`App/params/`, `App/sequencer/` and `App/ui/` are new files, plus a `.dma_buffer` section in the linker script, none of which CubeMX
overwrites. Only `Src/`, `Inc/`, the `.ioc`, and the linker script are
regenerated, so re-apply the three changes above (and the `.dma_buffer` section)
after any CubeMX regenerate.

## Code style

C/C++ follows a `.clang-format` at the repo root: **Allman braces, tab
indentation, no space before control-statement parens** (`if(cond)`). The
vendored ST driver in `App/platform/wm8994/` is excluded (`DisableFormat`).
Run `clang-format -i` (or format-on-save) to keep files in style.
