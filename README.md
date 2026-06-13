# softSynth

Softsynth firmware for the **STM32H750B-DK** Discovery board (STM32H750XBH6,
Cortex-M7 @ up to 480 MHz, 4.3" 480×272 RK043FN48H LCD, WM8994 audio codec).

Milestones so far: paint the LCD, bitmap-font text, and a 440 Hz test tone out
of the on-board WM8994 codec. See `ARCHITECTURE.md` for the design and roadmap.

## Layout

- `App/` — application code (**C++17**). `display.*` brings up the LTDC panel;
  `app.cpp` is the entry point (`app_main()`), called from the generated `main()`.
- `App/platform/` — hardware drivers: `audio_out.*` (SAI2 + DMA, accurate 48 kHz
  PLL2 clock), `codec.*` (WM8994 over I²C4), and the vendored ST `wm8994/` driver.
  I²C4 and SAI2 are configured by the app, not by the generated `MX_*_Init()`.
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

## Flash

```sh
STM32_Programmer_CLI -c port=SWD mode=UR -w build/Debug/softSynth.elf -v -rst
```

After flashing: the LCD shows the title + status text, and a 440 Hz test tone
plays out of the headphone jack (the on-screen line is green on success, red if
the codec didn't answer on I²C).

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
   framebuffer in AXI-SRAM (`.framebuffer` linker section).

Note: the **audio path is *not* on this list** — `audio_out`/`codec` are new
files under `App/platform/`, plus a `.dma_buffer` section in the linker script,
none of which CubeMX overwrites. Only `Src/`, `Inc/`, the `.ioc`, and the
linker script are regenerated, so re-apply the three changes above (and the
`.dma_buffer` section) after any CubeMX regenerate.

## Code style

C/C++ follows a `.clang-format` at the repo root: **Allman braces, tab
indentation, no space before control-statement parens** (`if(cond)`). The
vendored ST driver in `App/platform/wm8994/` is excluded (`DisableFormat`).
Run `clang-format -i` (or format-on-save) to keep files in style.
