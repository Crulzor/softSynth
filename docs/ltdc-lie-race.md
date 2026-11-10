# LTDC LIE mask/re-arm race — minutes-scale freeze

## Bug

- `App/display.cpp:203` (`begin_draw`): clears LIE (`__HAL_LTDC_DISABLE_IT(&g_ltdc, LTDC_IT_LI)`) at the start of every draw pass.
- `App/display.cpp:210–214` (`end_draw`): clears a pending LIF, then `HAL_LTDC_ProgramLineEvent(&g_ltdc, 0)` — which **does re-arm LIE** (`__HAL_LTDC_ENABLE_IT(hltdc, LTDC_IT_LI)` inside the HAL). So in normal operation LIE is off only during the draw pass.
- `App/display.cpp:246` (`HAL_LTDC_LineEventCallback`) also re-arms LIE after every serviced line event.

The HAL handler only clears LIF when LIE is enabled —
`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc.c:689` (LIE-disable :692, LIF-clear :695, callback :709):

```c
if (((isrflags & LTDC_ISR_LIF) != 0U) && ((itsources & LTDC_IER_LIE) != 0U))
```

## Mechanism (livelock, confirmed on a frozen board 2026-11-10)

The trigger is a race between the LTDC hardware and the foreground's IER write in `begin_draw()`:

1. Line 0 arrives while LIE is still 1 → LIF set, LTDC raises its IRQ request (pending in the NVIC).
2. Before the handler runs (exception entry latency + the posted AXI→APB write to `IER`), the foreground's `begin_draw()` store clearing LIE lands.
3. The handler reads `IER` with LIE=0 → the LI branch tests `LIF(1) && LIE(0)` → false → the ICR LIF-clear at :695 is **skipped**; it returns with LIF still 1.
4. The LTDC keeps its IRQ line asserted while LIF is set (observed: the IRQ re-pends at every exception return even with LIE=0) → re-enter → forever.

The only code that could recover is `end_draw()` (clears LIF, re-arms LIE) or the callback (:709 → `display.cpp:246`). The callback sits inside the same gated branch, and `end_draw()` runs in the foreground, which is now permanently preempted by the handler. Nothing can clear LIF → livelock.

Note: LIF rising while LIE is *already* 0 (line 0 during an ordinary draw pass) is harmless — that happens all the time and `end_draw()` drops the flag. Only an IRQ request latched with LIE=1 and serviced after LIE=0 gets stuck.

Observed frozen state: `IER=0x6` (FUIE|TERRIE from `HAL_LTDC_Init`, LIE=0), `ISR=0x1` (LIF), CPU 100% in `HAL_LTDC_IRQHandler`, foreground frozen in `OscPage::update` with `g_in_draw=1`. SysTick and the audio DMA (higher priority) keep running.

## Why minutes

The vulnerable window is a handful of cycles around `begin_draw()`'s write to `IER`, hit once per UI frame. Line 0 has to land exactly in it; the UI loop (`HAL_Delay(20)` + variable draw time) and the ~59 Hz panel drift past each other, so the phases line up only after tens of thousands of frames — minutes. Once it fires, it is permanent until reset.

## Fix: never clear LIE in `begin_draw` (applied 2026-11-10)

`begin_draw()`/`end_draw()` now only set/clear `g_in_draw` (with compiler fences); the LIE mask, the LIF drop and `HAL_LTDC_ProgramLineEvent` are gone from the foreground. The only IER writes left are init and the IRQ callback, so the foreground can no longer race the LTDC.

Delete the LIE mask at `App/display.cpp:203`. The swap is already protected by the `g_dirty && !g_in_draw` guard in the callback (`display.cpp:237`), which defers the buffer swap while a frame is being drawn.

With LIE armed at all times, the HAL's LI branch always clears LIF (:689 is always true when LIF is set), the callback runs and re-arms, and the CPU returns to thread mode. The "request latched with LIE=1, serviced with LIE=0" state is no longer reachable. (`end_draw()`'s flag drop + `ProgramLineEvent` then become unnecessary too.)

Alternative: write an own `LTDC_IRQHandler` that always clears LIF (`LTDC->ICR = LTDC_ICR_CLIF`) regardless of LIE.

## Evidence

3 identical GDB halts + 30-instruction step trace proving the LIF-clear skip;
post-step state unchanged (`ISR=0x1`, `IER=0x6`); NVIC pending bit for IRQ 88 re-sets at exception return while LIE=0.

Not provable read-only: the exact instruction-level interleaving of step 1–2 (inferred from the frozen state and the rarity).
