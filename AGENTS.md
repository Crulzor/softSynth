# Agent notes (softSynth)

Read `README.md` and `ARCHITECTURE.md` first — they are the source of truth and are
kept current. This file is only the working context the docs can't express.

## Division of labour
- **Bart (the user)** writes the oscillator and filter DSP himself, in a *separate
  repo*. Expect that DSP to land here as new/rewritten files under `App/dsp/`
  (voice, engine, etc.). Don't redesign or "improve" his DSP; help wire it in,
  test it on the host, and audition it via `render_wav`.
- **The assistant** owns UI + configuration in this repo: `App/ui/`,
  `App/params/`, `App/sequencer/` (the pattern/transport side), and the glue in
  `App/app.cpp`.

## Current state (as of the handoff from Claude)
- Working on hardware: 480 MHz clock, LTDC debug page, FT5336 touch, SAI2 +
  WM8994 audio path, sequencer clock ticking in the SAI ISR, CPU load meter.
- Display is double-buffered: 2 × 480×272×2 in the 512 KB `.framebuffer`
  AXI-SRAM section; CPU draws the back buffer, the LTDC line-0 IRQ swaps the
  pair **only when a drawing call has set `display::detail::g_dirty`**
  (`App/display.cpp` `HAL_LTDC_LineEventCallback`), so an idle screen keeps
  scanning the last complete frame instead of alternating with a stale
  buffer. Foreground draw passes are wrapped in `display::begin_draw()` /
  `end_draw()` (which also mask the swap and set `g_in_draw`).
  **Convergence**: the drawing calls also track the bounding box of pixels
  written since the last promotion (`display::detail::g_box_*`), and
  `swap_buffers()` calls `detail::flush_box()` after the exchange, copying
  that box from the promoted buffer into the demoted one. This is what keeps
  a one-shot widget change (a cycled label) from ghosting against the stale
  buffer while the tab bar keeps the pair flipping. Target + host stand-in
  both converge; idle cost stays zero.
- `App/dsp/oscillator.*` (PolyBLEP sine/saw/square/triangle) is a **throwaway
  test oscillator** — its only job is to prove the signal path works end to
  end. It is *not* a prototype of the real design. The real additive
  32-harmonic sine-table oscillator (ARCHITECTURE.md §7) is being written by
  Bart in the separate repo — as of 2026-11-05 he is actively on the
  oscillators and filters — and will land here. When it does, update
  §6/§7/§10 of ARCHITECTURE.md to match reality (and the test oscillator
  retires or stays purely as a path test — ask).
- The audio ISR (`app.cpp::audio_callback`) still streams silence; `e.offset` from
  the sequencer is the planned sample-accurate hook for `engine.note_on`.
- Params set is a first-draft sized for the multi-page UI; it will
  grow/re-range as the real DSP lands (UI only addresses params by
  `params::Id`, so that is the intended churn point).

## Workflow rules
- DSP/params/ui/`display_draw` compile in **both** builds. Any change under
  `App/` that the host build compiles must keep the host build green:
  `cmake -S test -B test/build && cmake --build test/build &&
  ctest --test-dir test/build` plus `render_wav` / `render_ui` for
  audio/visual checks. Firmware build: `cmake --preset Debug &&
  cmake --build build/Debug`. Flash with `STM32_Programmer_CLI` (see README).
- Audio path: nothing blocking, no allocation, no virtuals. Foreground only
  touches things the ISR reads as atomic word-sized fields.
- CubeMX regenerate overwrites `Src/`, `Inc/`, the `.ioc` and the linker script;
  re-apply the manual fixes listed in README.md §"Manual changes" afterwards.
- Style: Allman braces, tabs, `if(x)` — `.clang-format` at the repo root.

## Done: Multi-page UI refactoring

The 5-page tab-bar UI is built: **OSC, ENV, MOD, SEQ, DBG** (debug rides as a
fifth tab until the others cover it), 480×246 pages + 26 px tab bar carrying
the status readout and transport dot. See ARCHITECTURE.md §11 for the layout.

What landed:
- `App/modulation/matrix.hpp` — 6 rows of {src, dst, amount}, sources
  {LFO1, LFO2, AmpEnv, Tempo}, destinations {Pitch, Shape, Mix, Amp, Cutoff};
  plain data, UI writes / ISR reads. Replaces the per-LFO `Dest`/`Amount`
  params (`LfoDest` is gone).
- `ui/page.hpp` — `Page` base + `Ctx` (params/pattern/transport/matrix refs);
  `Ui` owns the 5 pages + `TabBar`; a press on the bar's tab zone switches
  page (`TabBar::take_switch`).
- One `*Page` class per tab under `App/ui/` (`osc_page`, `env_page`,
  `mod_page`, `seq_page`, `debug_page`), each a `Ctx` ctor + `hit()` over a
  `widgets_` array.
- New widgets: `CycleButton` (tap-to-cycle names), `FloatKnob` (knob bound to
  a raw `float*`, for the matrix amounts).
- `env_page` curve preview: `fill_polygon` (scanline, simple x-monotone shapes)
  + `draw_line` (Bresenham) added to `display_draw`.
- Tests: 37/37 under `test/` — tab switch, OSC/ENV/MOD/SEQ page edits, plus
  ENV curve regression tests (all four ADSR values reflected, no sustain
  trails, no stroke outside the curve rect).
  `render_ui` dumps one PPM per page (`ui_{osc,env,mod,seq,dbg,picker}.ppm`).

## Done: flicker / ghosting fixes (2026-11-05, both confirmed on hardware)

Two buffer-desync bugs, same root cause at two scales: a region drawn into
one buffer only while the pair kept flipping, so the panel alternated fresh
vs stale.

- **Page-switch flicker** — switching tabs left the new page painted into one
  buffer only. Fix in `ui/ui.cpp`: `needs_full_` set by `switch_page()` /
  `redraw_all()`, consumed at the top of `update()`, so the frame *after* a
  switch is a full repaint of the now-stale back buffer.
- **One-shot widget ghost** — a cycled label redraws one buffer once, then the
  widget goes clean while the tab bar (invalidated every frame) keeps the
  pair flipping. Fix in the display layer: `display_draw` tracks the
  bounding box of everything written into the back buffer since the last
  promotion (`g_box_*`), and `swap_buffers()` (target + host stand-in) calls
  `detail::flush_box()` after the exchange, copying the box from the
  promoted buffer into the demoted one — during vertical blanking in the
  line-0 IRQ, where the demoted buffer is not scanned. No full-frame
  memcpy, no new IRQ behaviour; an idle frame copies nothing.

Regression tests (in `test/test_ui.cpp`, both buffers primed and
`display::swap_buffers()` emulating the frame-start boundary):
`the frame after a page switch repaints the stale buffer in full` and
`a one-shot widget change converges into both buffers` (primes both buffers,
taps the MOD row-0 SRC cycle button, flips 6 tab-bar-driven frames, asserts
both buffers keep the new label). The convergence test was verified to fail
with `flush_box()` disabled.

## Done: note-picker grid-gap ghost (2026-11-05)

The `NotePicker` (piano roll) draws over the whole `StepGrid` rect, and its
keys reach into the gaps between the 4×4 cells (cells are inset 2 px in an
80×52 pitch). When the picker closed, the grid only repainted the dirty cell
rects, so the piano keys were left showing in the inter-cell gaps. Fix in
`StepGrid::draw()` (`App/ui/widgets.cpp`): repaint the **whole** grid rect to
`theme::kBackground` and then redraw **every** cell (the fill wipes them all,
so the dirty mask only decides *whether* the grid draws, never *which*
cells) — the grid owns its full rectangle and stays consistent no matter
which subset of cells a frame dirtied. This also fixes the playback blackout:
a playhead tick marks only the two cells it crossed (`sync()`), so with
dirty-only redraw the rest of the grid went black each step. Both the SEQ
and DBG pages share this `StepGrid`. The full-rect fill also lands in the
draw box, so it converges into both buffers on the next flip and can't
ghost. Regressions (both verified to fail against the pre-fix
dirty-cells-only draw): `closing the picker leaves no piano keys in the grid
gaps` (gap pixels read the piano key colours instead of the background; the
pre-existing on-steps survive) and `a playhead tick redraws every cell, not
just the two it touched` (sets `transport.step` as the SAI clock would).
Both confirmed on hardware 2026-11-05: no piano ghost in the gaps, and the
grid stays fully painted while the pattern plays.

## Done: ENV curve stuck-release-line (2026-11-05)

Third ghost variant, smallest scale: a widget drawing one pixel past its own
clear rect. `CurvePreview::draw()` (`App/ui/env_page.cpp`) computed the
curve's bounding box with `x4 = x0 + w`, `yBot = yTop + r.h` — the corner one
pixel past the `fill_rect(x0, yTop, r.w, r.h)` clear. At release = 0 the
release stage collapses to a vertical line at `x = x0 + w = 472`, outside the
clear rect (x 8..471, y 66..239); adding release then cleared x 8..471 only
and left the old line stuck on the right of the preview (the bottom row
`y = 240` had the same 1 px overhang). Fix: inset the curve one pixel
(`w = r.w - 1`, `h = r.h - 1`, `yBot = yTop + h`) so its bounding box equals
the clear rect; clear and border (`draw_rect(x0, yTop, r.w, r.h)`, drawn
last) still use the full rect, so the border overpaints the columns/rows the
curve touches. Lesson: any widget drawing past its clear rect can ghost —
suspect widget-level overhang first, then time-dependent redraws, then the
flip path. Regression `the ENV curve leaves no stroke outside its rect`
(verified red: 88 failed assertions = the stuck line in the right column +
bottom row). Suite 37/37; confirmed on hardware 2026-11-05.
