# Agent notes (softSynth)

Read `README.md` and `ARCHITECTURE.md` first — they are the source of truth and are
kept current. This file is only the working context the docs can't express.

## Division of labour
- **Bart (the user)** prototypes oscillator / filter ideas in his own sandbox
  (`~/repos/kladblok`, not a git repo — see its `docs/AGENTS.md`). On
  2026-11-11 he asked the assistant to write the real oscillator, filter and
  6-voice engine here (`App/dsp/`, see "Done: synth engine" below). From now
  on treat `App/dsp/` as shared: changes to its *sound* (spectra, filter
  model, envelope curves, gain staging) are his call — propose, don't just
  change.
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
  `end_draw()`, which only set/clear `g_in_draw` — the line interrupt stays
  armed (masking it raced the LTDC and livelocked the IRQ after minutes, fixed
  2026-11-10, `docs/ltdc-lie-race.md`).
  **Convergence**: the drawing calls also track the bounding box of pixels
  written since the last promotion (`display::detail::g_box_*`), and
  `swap_buffers()` calls `detail::flush_box()` after the exchange, copying
  that box from the promoted buffer into the demoted one. This is what keeps
  a one-shot widget change (a cycled label) from ghosting against the stale
  buffer while the tab bar keeps the pair flipping. Target + host stand-in
  both converge; idle cost stays zero.
- The audio ISR (`app.cpp::audio_callback`) runs `dsp::Engine` (6 voices):
  MIDI events drained from the parser's FIFO (tag 0), then the sequencer
  clock → `sequencer::Player` (tag 1); the block is split at the step's /
  gate's frame for sample accuracy. The old PolyBLEP `dsp::Oscillator` is
  deleted (git history has it).
- Params set is a first-draft sized for the multi-page UI; it will
  grow/re-range as the real DSP lands (UI only addresses params by
  `params::Id`, so that is the intended churn point). The waveshapes are
  not params: they are `params::WaveSlots` (per osc A/B spectra).

## Workflow rules
- **Flash is tight** (Debug 121020 B = 92.3 % after the engine landed).
  Debug builds `App/dsp/` engine files at `-O2 -ffp-contract=fast` (CPU) and
  the HAL-free UI/model files at `-Og` (size) — `CMakeLists.txt`. A new
  `.cpp` under `App/ui/` etc. should join that `-Og` list. Release (`-Os`)
  is 79.7 KB.
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

## Done: UI look pass — OSC re-layout, section accents, status strip (2026-11-11)

Bart picked suggestions 1, 2 and 4 from a list of UI improvements, then 5
and 6 (MOD routing rows with bipolar amount bars and dimmed idle rows; ENV
gradient fill, 2 px stroke, handles, stage labels; SEQ cells without the X,
playhead outline, brighter beat numbers — ARCHITECTURE.md §11). The LFO
shape preview from 5 was left out on purpose: no LFO shape param or LFO
engine exists yet, so it would only animate a guess. Still open: 3 (a
bigger 1-bit font, ~2–3 KB flash). **Flash budget**: 116884 B (89.2 %) in
the Debug build, which is `-O0`; Release is `-Os` and much smaller, so a
switch to Release (or `-Og` for Debug) is the first lever if space runs
out.
- **OSC page** re-laid out full-page with large knobs (`ParamControl::
  set_large`) — see ARCHITECTURE.md §11. Bart then asked for room for the
  wave shape: cells went 84→76 px wide so CUT/RES sit side by side and the
  bottom-right 160×100 is a **WAVE** slot (`ui::Scope`, empty graticule +
  flat trace). The oscilloscope itself is still to do — it needs a sample
  snapshot handed from the audio side (ISR writes, UI reads; no blocking),
  best designed together with Bart's oscillator.
- **Per-section accents** (`theme.hpp`, `Widget::set_accent`,
  `kPageAccents` in `page.hpp`), `draw_section()` rules instead of grey
  title bars on OSC/ENV/MOD/MIDI. DBG keeps its old headers (CPU / MHz).
- **Status strip** in the tab bar (transport, BPM, step pips, CPU %) and the
  **MIDI tab LED** (`Ui::update_midi_led`). `Ui::set_cpu_load` now also
  feeds the bar.
- Anti-aliased knob rings / caps (pointer drawn per pixel via the existing
  `atan2`, so no `cosf` link). Flash 113460 B (86.6 %, +4.4 KB).
- Tests 54/54: OSC test re-pointed at the new grid; ENV stroke tests count
  `kEnvAccent` (they would pass vacuously on orange); new tests for the tab
  accent fill and the MIDI LED states/flash. Flashed 2026-11-11.

## Done: USB-MIDI host on CN13 (BeatStep Pro) — vendor ST USBH (2026-11-10)

**Goal**: USB-MIDI input. Board = USB **host only** on CN13 (OTG_FS); a BeatStep
Pro (device-only micro-USB port) plugs in directly — no PC in the loop.
Starter scope: note playback from MIDI only (CC/sysex ignored), monophonic
latest-wins. Board powered from CNI4; the BeatStep is self-powered (ships with
data/power splitter), so the board does NOT need to feed VBUS on CN13.
`docs/usb-midi-descriptor.md` holds the USB-MIDI 1.0 descriptor/class bytes as
the parse-side reference.

**What landed**:
- **M0**: `App/platform/sysclock.cpp` PLLQ 4→20 (48 MHz USB, role-agnostic).
- **M1' host module**: vendored ST USB Host in `App/platform/usbh/` —
  `usbh_core.c`/`usbh_ctlreq.c`/`usbh_ioreq.c`/`usbh_pipes.c` (byte-identical
  to upstream) + `usbh_*.h`; `usbh_conf.{h,c}` = full LL glue (owns
  `HCD_HandleTypeDef hhcd_USB_OTG_FS`, `USBH_USE_OS 0`, static ≤512 B
  config-desc buffer); `usbh_midi.{h,c}` = the MIDI class: finds the
  Audio (0x01)/MIDI-Streaming (0x03) interface, opens its bulk-IN EP, keeps
  one bulk-IN armed at all times, static 64 B data buffer, zero allocation,
  raw data leaves via `USBH_MIDI_RegisterHook`. The device-side
  `App/platform/usbfs.{hpp,cpp}` is superseded — kept in-tree as reference,
  not compiled.
- `App/platform/usbhost.{hpp,cpp}` C++ facade: owns the single
  `USBH_HandleTypeDef`; `init()` = register hook + `USBH_Init(&hhost, NULL,
  HOST_HS)` + `USBH_RegisterClass(&hhost, USBH_MIDI_CLASS)` + `USBH_Start`;
  `process()` = `USBH_Process` in the foreground frame loop (enumeration
  blocks ~110 ms — never from an ISR).
- **M2 parser**: `App/midi/midi.{hpp,cpp}` (pure C++, compiles in **both**
  builds): 4-byte cable packets → packed `std::atomic<uint32_t> note_state`
  (bits 0–6 note, 7–13 velocity, 14–17 channel, 18 active) + a `packets`
  counter. Only 8/14-bit packets are decoded (the ones that carry a status
  byte); 16/32-bit (sysex, running-status continuations) are dropped whole.
  Vel-0 note-on = note-off; note-off clears only on channel+note match;
  latest-wins. `test/test_midi.cpp` (13 cases).
- **M2 hook**: `usbhost.cpp` trampoline → `midi::parser().on_packet(buf,
  len)`, registered before `USBH_Start`. Foreground is the single writer;
  the ISR (and the future MIDI tab) read the atomic.
- **M3**: `app.cpp` — one `Voice{dsp::Oscillator osc; uint8_t note; float
  gain; bool active}` + a `g_latched_note` atomic. `audio_callback`
  re-triggers on the block edge when `note_state` changes: saw, `note_to_hz`
  (440·2^((n−69)/12)), phase reset, gain = vel/127 × `kMaxGain` (0.35),
  monophonic latest-wins, soft-clamped to int16.
  **Future 6-voice synth (Bart)**: the voice is deliberately kept as a
  struct — the upgrade is "make it an array" (a 6-slot pool with
  assignment/steal policy); `note_state` is already packed per-note, so the
  parser extension is a 6-state array.
- **Wiring swap** (device → host, done): `Src/main.c` PCD handle/init → HCD
  (the handle lives in `usbh_conf.c`); `Src/stm32h7xx_it.c` →
  `HAL_HCD_IRQHandler`; `Src/stm32h7xx_hal_msp.c` `HAL_PCD_MspInit` →
  `HAL_HCD_MspInit` (48 MHz PLLQ, PA11/PA12 `AF_PP`, no PA9 VBUS sense) +
  NVIC arm OTG_FS (6,0); `Inc/stm32h7xx_hal_conf.h` HCD on / PCD off; PCD
  drivers (`hal_pcd.c`/`hal_pcd_ex.c`) dropped from the build;
  `midi/midi.cpp` added to both CMakeLists, `usbhost.cpp` + `usbh/*.c` to
  the root one.

**Verification**: host suite 50/50 (37 + 13 MIDI). Firmware green, 0
warnings. Flash **105364 B (80.39 %)** — pre-USBH baseline 90360 B
(69.06 %): +8940 B wiring, +980 B parser, +4928 B oscillator + `powf`.
DTCM ≈ 8.5/128 KB (the real data budget; the 510/512 KB AXI-SRAM figure is
the `.framebuffer`, not data). Flashed to the board 2026-11-10
(verified + reset); **sound not yet auditioned** — there is no enum/status
readout until the MIDI tab lands, so "works" = a tone out of the WM8994
when a key is played.

**Done 2026-11-10 (evening, flashed)** — "finish MIDI" pass before bring-up:
- **MIDI tab** (`App/ui/midi_page.{hpp,cpp}`): 6th tab, order OSC, ENV, MOD,
  SEQ, **MIDI**, DBG (DBG is now index 5; tabs stay 48 px wide). Read-only:
  link state (NO DEVICE / ENUMERATING / READY / NOT A MIDI DEVICE), DEVICE
  VID:PID, raw `HOST <gState> / ENUM <EnumState>` + control-error count, the
  held note (name, velocity, channel 1-based), packet counter and the last raw
  packet in hex. Link state comes from `usbhost::link()` → `Ui::set_usb_link()`
  each frame (`midi/link.hpp` is HAL-free); notes/counters are read straight
  from `midi::parser()`. `render_ui` writes `ui_midi.ppm`.
- **Crash fix**: `USBH_Init` was given a NULL user callback, but the vendored
  core calls `pUser` unguarded on `HOST_USER_CLASS_SELECTED` (usbh_core.c) and
  `HOST_USER_UNRECOVERED_ERROR` (usbh_ctlreq.c) → HardFault the moment the
  BeatStep finished enumerating. `usbhost.cpp` now registers `user_event`
  (counts unrecovered errors). Host id is `HOST_FS` (defined in usbh_conf.h).
- **Parser** decodes by USB-MIDI 1.0 CIN (`b0` low nibble; high nibble = cable,
  any cable plays) instead of the made-up "b0>>6 packet type"; status must match
  the CIN. Adds `last_packet` (raw). Tests updated (52/52).
- **usbh_midi**: bulk-IN re-armed on `URB_ERROR` too (it used to go silent).
- **VBUS decided**: the board does NOT power CN13. The BeatStep is powered (and
  sees VBUS) through its own data/power splitter; PA5 (`OTG_FS2_PSO`, the CN13
  power switch, active high in ST's H750B-DK example) stays an input/off on
  purpose so two 5 V sources never meet.
- Flash 109068 B (83.2 %), DTCM 8.6 KB.

**Remaining, in order**
1. **Hardware bring-up** (board on CNI4, BeatStep on CN13 via its splitter):
   open the MIDI tab. Expect NO DEVICE → ENUMERATING → READY with an Arturia
   VID (0x1C75), then notes/PACKETS moving when keys are played, and a tone
   from the WM8994. If it sticks in ENUMERATING, note the HOST/ENUM numbers
   (`HOST_*` / `ENUM_*` enums in `usbh_def.h`). NOT A MIDI DEVICE = no
   MIDIStreaming interface found — first suspect `USBH_MAX_NUM_INTERFACES 2`
   (raise it if the BeatStep exposes more interfaces). MIDI clock from the
   BeatStep makes PACKETS count continuously — that alone proves the data path.
2. **Docs**: `ARCHITECTURE.md` §2 → USB-MIDI host-mode, README milestone note.

**FORK — decided: A (vendor ST USBH), chosen by Bart 2026-11-10**
- **A. Vendor ST USBH (chosen 2026-11-10)**: the complete ST USB Host Library is on disk
  and drives this exact HAL HCD; enumeration/NAK/retry logic built in.
  `/home/crulzor/STM32Cube/Repository/STM32Cube_FW_H7_V1.13.0/Middlewares/ST/STM32_USB_Host_Library`
  - Vendor `Core/Src/usbh_core.c` (state machine), `usbh_ctlreq.c`
    (`USBH_CtlReq` — 3-stage control: SETUP→DATA→STATUS; `USBH_GetDescriptor`,
    `USBH_SetAddress`, `USBH_SetCfg`, `USBH_Get_CfgDesc`), `usbh_ioreq.c`
    (`USBH_BulkReceiveData` → `USBH_LL_SubmitURB`), `usbh_pipes.c`
    (`USBH_OpenPipe`/`USBH_ActivatePipe`/`USBH_ClosePipe`), plus `Core/Inc/usbh_*.h`.
  - `usbh_conf.h`: make from `Core/Inc/usbh_conf_template.h` — **`USBH_USE_OS 0`**,
    `USBH_MAX_NUM_ENDPOINTS 2`, `USBH_MAX_NUM_INTERFACES 2`,
    `USBH_KEEP_CFG_DESCRIPTOR 1`, `USBH_MAX_SIZE_CONFIGURATION 0x200`,
    `USBH_IN_NAK_PROCESS 0`.
  - Class layer: `Class/Template/` = registration skeleton (`Init`/`IOProcess`/
    `Process`, `USBH_RegisterClass`); `Class/HID/Src/usbh_hid.c` is the working
    model. The MIDI class (~120 LOC): `USBH_FindInterface(phost, 0x01, 0x03, 0x00)`
    (Audio class / MIDI-streaming subclass), read bulk-IN EP (bit 7 set on
    `bEndpointAddress`) from the parsed `phost->device.Cfg_Desc.Itf_Desc[]`,
    `USBH_OpenPipe` + `USBH_ActivatePipe`, submit bulk-IN URB, poll
    `USBH_LL_GetURBState(phost, pipe) == USBH_URB_DONE` → feed the M2
    `on_bulk_in()` hook (`xfer_count` bytes landed in our static buffer).
  - Entry: `USBH_Init(&hhost, &MIDI_Class, USBH_LL_Init)` + `USBH_Start` once;
    `USBH_Process(&hhost)` in the app foreground frame loop (l.134) — enumeration
    (incl. the ~110 ms blocking `USB_ResetPort`) is driven there, never ISR.
    LL glue (a `usbh_conf.c`-equivalent): `USBH_LL_Init/DeInit/Start/Stop/Connect/
    Disconnect/ResetPort/OpenPipe/ActivatePipe/ClosePipe/SubmitURB/GetURBState`
    map 1:1 onto HAL HCD calls; `USBH_LL_DriverVBUS` → `USB_DriverVBUS`/
    `USB_DriveVbus`.
  - **RAM budget (the "risk" behind the fork — resolved 2026-11-10)**: the
    99.61 % (510/512 KB) figure was a miscalculation — that is the **AXI-SRAM
    `.framebuffer`** region (2 × 480×272×2), not the data budget. Real `.data`/
    `.bss`/heap/stack live in **DTCMRAM** at ~7.3/128 KB, so USBH's ~1.5 KB
    (`USBH_HandleTypeDef` with the parsed config descriptor —
    `device.Cfg_Desc.Itf_Desc[2]` × `Ep_Desc[2]` — + `Pipes[16]`, and a ≤512 B
    static config-desc buffer; the HCD handle, 16 channels, is ≈1 KB in *both*
    options) fits with room to spare. OTG_FS runs `dma_enable=0`, so DTCM buffers
    are fine.
- **B. Raw HAL HCD, no stack** (original plan): hand-roll connect→reset→
  GET_DESCRIPTOR→SET_ADDRESS→SET_CONFIGURATION in `usbfs.cpp` over the pinned
  driver facts below (~400–600 LOC, more bring-up risk).
- **C. Hybrid**: USBH for enumeration, drop to direct `HAL_HCD` bulk-IN polling for
  the MIDI data path. Most moving parts; only if A's RAM is unacceptable but the raw
  data loop is still wanted.
- M2/M3 are identical under A/B/C — only M1' differs.

**HCD driver facts (verified against in-tree `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hcd.c`)**
- Weak callbacks (l.127+): `SOFCallback`, `ConnectCallback`, `DisconnectCallback`,
  `PortEnabledCallback`, `PortDisabledCallback`, `HC_NotifyURBChangeCallback`.
- `HAL_HCD_HC_Init(hhcd, ch, epnum, dev_addr, speed, ep_type, mps)` (l.202) once per
  (addr, EP); `HAL_HCD_HC_SubmitRequest(hhcd, ch, direction, ep_type, token, pbuff,
  length, do_ping)` (l.378) re-arms per transfer. Control transfer = 3 stages:
  SETUP (token 0 → PID 3), DATA (token 1), STATUS; PID table l.400–442; zero-length
  status-OUT forces PID 1.
- IN data path is CPU-driven (`dma_enable=0`): `HCD_RXQLVL_IRQHandler` drains the RX
  FIFO → `hc[ch].xfer_buff`; completion = `hc[ch].urb_state` (`URB_DONE`/`URB_STALL`/
  `URB_NOTREADY`/`URB_ERROR`/`URB_IDLE`) + `HAL_HCD_HC_NotifyURBChange_Callback`;
  `hc[ch].xfer_count` = bytes actually received (IN `XferSize` rounds *up* to a whole
  packet in `USB_HC_StartXfer`, so returned count may be < requested).
- NAK: the driver auto-re-arms NAK'd bulk/ctrl channels (ErrCnt ≤ 2 →
  `URB_NOTREADY` + re-enable) — a NAK storm is *not* an error.
- `HAL_HCD_ResetPort` → `USB_ResetPort` **blocks** (~110 ms `HAL_Delay`,
  ll_usb.c l.1600) → enumeration must run foreground (app frame loop), not ISR.
- Constants: `EP_TYPE_CTRL=0`/`ISOC=1`/`BULK=2`/`INTR=3`; `HC_PID_DATA0=0`/`DATA1=2`/
  `SETUP=3`; `HC_MAX_PKT_CNT=256`; `HCFG_48_MHZ=1`; DPID sits at
  `USB_OTG_HCTSIZ_DPID` (bit 29). `USB_HostInit` (ll_usb.c l.1476)
  **unconditionally clears `GCCFG.VBDEN` and `GCCFG.BCDEN` in host mode** → `.ioc`
  `vbus_sensing_enable` / PA9 VBUS sense is moot for host connect detection
  (host mode is HPRT0-driven).
- No in-tree host example exists: `grep` for `HAL_HCD_HC_SubmitRequest` matches the
  driver itself only. (Pre-pivot belief that "there's no host stack anywhere" is
  wrong — see the USBH library above; nothing in-tree though.)

**Caveat: `softSynth.ioc` was NOT updated** — it still lists
`MX_USB_OTG_FS_PCD_Init` and the PA9/PA11/PA12 signals on the device side.
Host role is forced by `USB_HostInit` in code, not by the `.ioc`; a CubeMX
regenerate would restore the PCD wrapper and needs the HCD init re-added
(plus the README "Manual changes"). Board wiring facts: PA11/PA12 = DM/DP,
PA9 = VBUS sense, **no OTG ID pin on the board** (PA10's `USB_OTG_FS2_ID`
label is a leftover), PH11 is a plain `GPXTI11` GPIO (overcurrent not wired
to the OTG core).

**VBUS (resolved 2026-11-10)**: no VBUS from the board — see "Done 2026-11-10
(evening)" above.

**Committed 2026-11-10** (everything in "What landed"): `App/app.cpp`,
`App/platform/sysclock.cpp`, `App/midi/`, `App/platform/usbh/`,
`App/platform/usbhost.{hpp,cpp}`, `App/platform/usbfs.{hpp,cpp}` (reference
only, not compiled), `CMakeLists.txt`, `cmake/stm32cubemx/CMakeLists.txt`,
`test/CMakeLists.txt`, `test/test_midi.cpp`, `Inc/stm32h7xx_hal_conf.h`,
`Src/main.c`, `Src/stm32h7xx_it.c`, `Src/stm32h7xx_hal_msp.c`, `docs/`.
**Baselines**: host suite 50/50; flash text 105364 B (80.39 %);
DTCM ≈ 8.5/128 KB.

## Done: synth engine — additive oscillators, ladder filter, 6 voices, chords + ties (2026-11-11)

Bart asked (in order): the oscillators (3 basic waves selectable, plus two
buttons per oscillator that each roll a random waveshape into a slot the
oscillator remembers, SHP morphing between the two Fourier series), then a
"smooth, Moog style" filter "with as much harmonics as possible", then 6
voices per spec, then a SEQ chord option (all the usual chords) and ties
between steps. Reference for the oscillator: `~/repos/kladblok`
(`AdditiveOsc` — phase × harmonic into the sine table; its `morph()` and
`make_saw/triangle` are deliberately left for Bart there, don't fill them in).

**Decisions taken without asking — flagged to Bart, easy to change:**
- **WAV puts the basic wave in both slots** (so the oscillator plays exactly
  that wave whatever SHP is); A / B each roll a random shape into their own
  slot. Alternative: WAV writes slot A only (then SHP 0 = the basic wave).
  One line in `WaveButton::press` / `WaveSlots::set_basic`.
- **Four basic waves**, SIN TRI SAW SQR (he said "the 3 basic waves"; the
  spec lists four). Drop SIN by trimming `spectrum::Basic`.
- **DRV knob added** to the filter (`params::Id::FilterDrive`) for the
  "harmonics" ask.
- **Gate = half a step** (`Sequencer::kGate`); ties extend it.
- **The mod matrix and LFOs are now live** (they were UI-only); ENV → CUT is
  the filter envelope. Not asked for explicitly, but "as per spec".
- Slots are **not persisted** across power cycles (no preset storage yet).

**What landed** (details in ARCHITECTURE.md §4, §7, §11):
- `dsp/sine_table` (2048 pts, RAM, `init()` in `app_main` before audio),
  `dsp/spectrum` (basic + random spectra, peak-normalised, `render_cycle`),
  `dsp/additive_osc` (sin/cos from the table, harmonics by the
  angle-addition recurrence in two chains; morph as two running sums so SHP
  ramps per sample; per-block band-limit fade 0.41–0.45 fs),
  `dsp/ladder` (Huovilainen nonlinear ladder, 2× OS, passband comp., tanh
  output, table tanh; **tuning and resonance corrections were measured with probe
  programs and fitted** — peak within 0.6 % of cutoff 100 Hz–20 kHz,
  self-oscillation at RES ≈ 0.91 at any cutoff), `dsp/envelope` (ADSR + LFO),
  `dsp/patch`, `dsp/voice`, `dsp/engine` (allocation: retrigger same
  note+tag → free → quietest releasing → oldest; spectra glide ~5 ms).
- `params/wave_slots` (double-buffered A/B per osc), `midi` event FIFO
  (+ all-notes-off when the USB link drops), `sequencer/chords` (19 shapes),
  `sequencer/player`, gate events in `Sequencer::process`.
- UI: OSC page re-laid out on 56-px cells (PIT SHP OCT + WAV/A/B stack, MIX
  48 wide so "127" fits, CUT RES DRV, WAVE scope drawing both oscillators'
  morphed shapes); SEQ page CHORD button + `ChordPicker`, TIE button, grid
  shows chord names and tie bars; tab-bar pips count ties.
- Tests 94/94 (`test_dsp.cpp` replaces `test_oscillator.cpp`; new sequencer,
  MIDI-queue and UI cases). `render_wav` is a ~25 s engine tour;
  `render_ui` adds `ui_chords.ppm`.

**Hardware bring-up (2026-11-11, same day)**: flashed. First boot
HardFaulted (imprecise bus fault, a `memset` running off the end of DTCM):
`audio_out::start()` pre-fills the **whole** DMA buffer in one callback
(128 frames), and the new callback rendered into a 64-frame stack buffer.
Fixed twice over: `audio_callback` cuts any request into `kBlockSize`
blocks, and `Engine::render` chunks long calls itself (host test
`a render call longer than one block is cut into blocks`). Lesson: the
render callback's `frames` is not always one block.
CPU measured over SWD (write a pattern + `g_transport.run` with
`STM32_Programmer_CLI mode=HOTPLUG -w32`, read `cpu_load`'s
`g_sum/g_count/g_max`, budget 640000 cycles/block): idle 2.8 %, worst case
(C2 min9 tied, 5–6 voices × all 32 harmonics) 74 / 87.5 % peak at first →
**59.6 / 70.4 %** after the oscillator fast path + table tanh. Profile:
~2/3 oscillators, ~1/4 ladder.

**Not done / next**:
1. **Listen on the board** — not auditioned by ear yet.
2. Level staging is by ear on the host only (voice gain 0.3, master soft
   clip); may want a master volume param.
3. kladblok's ImPlot scope shows the real output; the OSC page scope draws
   the shapes from the spectra instead (no audio tap yet).

## Done: touch froze after a while — I2C4 bus recovery (2026-11-11)

Report: "the board seems to get stuck after touching the mix slider". Not a
hang: audio and the UI loop kept running, but every touch read timed out
(I2C4 handle `ErrorCode = HAL_I2C_ERROR_TIMEOUT`, PC samples ~25 % inside
`I2C_IsErrorOccurred`). Cause: `i2c4::read/write` never recovered from a
failed transfer — after **one** timeout the HAL handle / bus stayed
mid-transfer and every later read failed, so touch was dead until reset.
Fix (`App/platform/i2c4.cpp`): on any failure, `HAL_I2C_DeInit` + RCC
reset of I2C4, nine hand-clocked SCL pulses + a STOP on PD12/PD13 (frees a
slave holding SDA), then `init()` again. Counters `g_errors`,
`g_recoveries`, `g_last_error` (non-halting SWD read: `nm` for the
addresses).
Measured: **each SWD hotplug connect (`STM32_Programmer_CLI mode=HOTPLUG`)
provokes about one touch-read timeout** — so debugging over SWD will show
errors; that's the tool, not the firmware. Untouched for 60 s with no SWD:
zero. The timeout that froze the MIX drag happened without SWD, so real
ones are rare but exist (root cause not pinned down — FT5336 clock
stretching while tracking a finger is the main suspect); each now costs
one ~10 ms hiccup. If they turn out frequent in use, next steps: a longer
timeout for the touch read only, or reading on the FT5336's INT pin (PG2)
instead of polling.
