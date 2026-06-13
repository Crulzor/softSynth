# softSynth

Softsynth firmware for the **STM32H750B-DK** Discovery board (STM32H750XBH6,
Cortex-M7 @ up to 480 MHz, 4.3" 480×272 RK043FN48H LCD, WM8994 audio codec).

Milestone 0: bring up the board and paint the LCD a solid colour.

## Layout

- `App/` — application code (**C++17**). `display.*` brings up the LTDC panel;
  `app.cpp` is the entry point (`app_main()`), called from the generated `main()`.
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

Output: `build/Debug/softSynth.elf` (+ `.hex`/`.bin` if you run objcopy).

## Flash

```sh
STM32_Programmer_CLI -c port=SWD mode=UR -w build/Debug/softSynth.hex -v -rst
```

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
   template leaves as inputs/in-reset. The whole screen shows the LTDC
   background colour (no framebuffer/SDRAM needed yet).

To change the colour, edit the `display::init(...)` argument in `App/app.cpp`
(e.g. `display::colors::Red`).
