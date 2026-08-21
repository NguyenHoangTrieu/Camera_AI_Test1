# WORKLOG - Camera_AI_Test1

Running log of this project's bring-up, for picking up in a fresh session.
For the stable reference doc (pinout, build instructions), see
[README.md](README.md) - this file is the messier "what's been tried and
what happened" history, kept separate so README doesn't get cluttered.

## Current status (most recent first)

**PROGRESSING, not yet fully fixed. Both the wiring/panel/init-sequence
explanation and the "RD floats during writes" explanation are now
confirmed correct (see below) - the FlexIO path went from solid white to
producing random black/white noise (a bus-speed/signal-integrity symptom),
then a too-aggressive speed-reduction attempt caused a full hang (see
below) that's now been backed off to a smaller, safer step - not yet
flashed/confirmed. Camera side remains fully confirmed working and
untouched by any of this.**

- **The /50 FlexIO-clock-divider attempt (~10 kHz/pin) hung completely -
  confirmed via serial log, not just a guess.** User captured
  `./build.sh monitor` output: the boot banner prints
  (`Camera_AI_Test1 - FRDM-MCXN947` / `Camera: OV7670 on J9...` /
  `Display: HSD024131-C1...`) and then **nothing else at all** - not even
  the camera init log line that unconditionally follows `LCD_Init()` in
  `main.c`. Since `LCD_InitFlexioMcuLcd()` would print an explicit
  "LCD: FlexIO MCULCD init failed." if `FLEXIO_MCULCD_Init()` itself
  failed (it didn't print that), the hang is specifically inside
  `LCD_InitPanel()`, almost certainly stuck in the very first
  `LCD_WriteCommand(0x01)` (SW reset) call's busy-wait loop inside
  `FLEXIO_MCULCD_WriteCommandBlocking()`, waiting on a FlexIO timer
  completion flag that never asserts at that specific (extreme) clock
  configuration. This looks like a real lower operating-frequency bound
  for the FlexIO peripheral/timer mode itself (not just
  `FLEXIO_MCULCD_SetBaudRate()`'s software 8-bit-divider-field ceiling,
  which was satisfied - divider computed to ~150, well within 0-255).
  **Backed off**: `hardware_init.c`'s `kCLOCK_DivFlexioClk` changed from
  `50u` to `4u` (150 MHz -> 37.5 MHz FlexIO clock instead of 3 MHz), and
  `app.h`'s `DEMO_FLEXIO_BAUDRATE_BPS` changed from `80000U` (10 kHz/pin)
  to `800000U` (100 kHz/pin, 4x slower than the noisy 400 kHz/pin case
  instead of 40x). This keeps the resulting internal SetBaudRate() divider
  at ~188 - the same numeric magnitude that's already proven to actually
  run (the un-divided 400 kHz/pin case used essentially that same divider
  value, just against a 150 MHz source instead of 37.5 MHz) - a smaller,
  safer step to test the speed/noise hypothesis without also risking
  whatever lower bound the /50 attempt broke. **Built clean, not yet
  flashed/tested - this is the immediate next step.**

- **Bit-bang diagnostic build (`LCD_BITBANG_DIAGNOSTIC=ON`) confirmed
  working - displays a real image.** This proves the wiring, panel, and
  generic MIPI-DCS init sequence are all fine; the bug was specific to the
  FlexIO transport path, not "downstream" of it. Rules out re-checking
  wiring/panel/init sequence going forward.

- **Fixed and confirmed a real improvement: LCD_RD floating during writes
  (see below) really was part of the bug.** After switching P0_8/LCD_RD
  from FlexIO to a plain always-high GPIO output (matching the bit-bang
  driver - see "Fixed" list below for the exact change), the *normal*
  FlexIO build went from **solid white (nothing at all)** to **the panel
  visibly responding but showing random black/white noise instead of a
  clean image**. That's real progress: the panel is now correctly
  receiving and acting on commands (SW reset/sleep-out/MADCTL/pixel-format/
  display-on all landing), and pixel *data* writes are reaching GRAM - the
  remaining problem is that some of those data bytes are being corrupted
  in transit.

- **Current hypothesis: FlexIO bus speed / signal integrity, even at
  400 kHz/pin.** Black/white "snow" noise (not a structured error like
  wrong colors, wrong orientation, or a shifted/torn image) is the classic
  signature of individual bits being sampled wrong on some byte transfers
  - consistent with WORKLOG's earlier bus-speed hypothesis (step 7 below),
  just not fully explored before because RD-floating was masking it
  (nothing displayed at all, so "is the data correct" couldn't even be
  evaluated). 400 kHz/pin was already the practical floor reachable with
  the FlexIO peripheral running undivided off the 150 MHz PLL0 (see the
  divider-overflow note further down) - so **this session lowered the
  FlexIO source clock itself** (`hardware_init.c`:
  `CLOCK_SetClkDiv(kCLOCK_DivFlexioClk, 50u)`, was `1u`, giving a 3 MHz
  FlexIO clock instead of 150 MHz) specifically so the per-pin baud rate
  could be dropped much further without hitting the same divider-overflow
  ceiling. `DEMO_FLEXIO_BAUDRATE_BPS` in `app.h` is now `80000U`
  (~10 kHz/pin, ~40x slower than the 400 kHz/pin that produced noise).
  This is deliberately very slow (a full 320x240 frame takes on the order
  of 10+ seconds at this rate) purely to test whether slowing down further
  eliminates the noise at all - not a usable final value for the real
  camera loop's frame rate. **Built clean, not yet flashed/tested - this is
  the next thing to try.**

### This session's findings

- **Panel identity, resolved (high confidence): ILI9341-family, 240x320
  native.** `requirement.md` names the panel `HSD024131-C1` - this is the
  *only* panel this whole project ever bought (the "different board" open
  question from earlier sessions is moot; there was only ever one panel).
  Web search for "2.4 inch TFT LCD shield arduino uno spiflash" (matching
  WORKLOG's description of the earlier working Arduino-header hardware)
  turns up an extremely consistent pattern across many vendors: ILI9341
  controller, 240x320, 8-bit parallel 8080 bus, onboard SD card slot (the
  "spiflash" in the product name), control lines on the Arduino header's
  A0-A3 analog pins - which matches this project's own note about needing
  "analog-only-pin jumper workarounds" on the Arduino-header revision. This
  is also consistent with the generic MIPI-DCS sequence (0x01/0x11/0x36/
  0x3A/0x29) having worked via bit-bang, since ILI9341 is MIPI-DCS
  compatible (unlike e.g. ILI9325/HX8347-family chips from the same shield
  category, which use a different, non-DCS register protocol and would NOT
  have responded to that sequence).

- **Fixed: `DEMO_PANEL_WIDTH`/`DEMO_PANEL_HEIGHT` were wrong (480x320,
  leftover from the original ST7796S assumption) - now 320x240.** This
  mattered because `main.c`'s boot-time "blank the screen" loop
  (`LCD_DrawImage(0, y, DEMO_PANEL_WIDTH, 1, ...)` for
  `y in 0..DEMO_PANEL_HEIGHT`) used these constants directly, sending
  `LCD_SetWindow()` column/page addresses up to 479/319 to a panel whose
  real addressable GRAM range (240x320 native, ~320x240 in the
  MADCTL-rotated landscape orientation this code already uses) tops out
  around 319/239. That's out-of-range column/page address commands sent to
  the panel on **every single boot, before the first camera frame is ever
  drawn** - a real, previously-unnoticed bug, and a plausible independent
  contributor to "solid white, no image" (some controllers can be left in
  an odd internal addressing state by an invalid EC/EP value). Fixed in
  `app.h`. Camera draw calls elsewhere already used `DEMO_BUFFER_WIDTH/
  HEIGHT` (320x240), which were already correct - only the boot-blank loop
  was affected.

- **Found (not yet fixed - needs a wiring change only the user can make):
  the LCD_RD line floats (Hi-Z) during every write, not just when
  physically unconnected.** Traced through the SDK's
  `fsl_flexio_mculcd.c`: the RD pin's FlexIO timer config only actively
  drives it during an explicit read; between reads (i.e. during ALL of
  `LCD_InitPanel()` and `LCD_PushPixels()`), `TIMCTL[timerIndex]` is
  cleared, which reverts RD's pin config to output-disabled - it floats.
  This is true of NXP's own reference example too (not a project bug), but
  it means the earlier suspicion "RD might not be connected on the panel's
  own PCB" (see "Where things stand" below) has a second, MCU-side
  component: even if the panel's RD trace *is* connected, the MCU never
  actively holds it at a defined level except during the one diagnostic
  read call. The already-inconsistent `LCD_DiagnosticReadId()` results
  (`C0 C0 C0 C0` -> ` 0 40 40 40` -> ` 0  0  0  0` across different
  sessions) are a classic floating-input signature.
  **Untried next step**: physically tie the panel's RD pin to a solid
  3.3V rail (disconnect it from P0_8 and hard-wire to 3V3, or add a ~10kOhm
  pull-up to 3.3V) instead of leaving it MCU-driven-when-idle. Cheap, no
  tools needed beyond a jumper wire.

- **Code audit of the FlexIO transport layer (ruled out, don't re-check):**
  diffed this project's `pin_mux.c`/`app.h` FlexIO config line-by-line
  against NXP's actual verified `examples/_boards/frdmmcxn947/
  display_examples/smartdma_camera_flexio_mculcd` board port in the
  `../mcuxsdk` checkout - pin numbers, ALT-function mux, shifter start/end
  indices (`0..7` - looks unusual but is NXP's own literal reference value,
  not a copy-paste bug), and baud-rate-divider math all match. Also
  confirmed via `build/compile_commands.json` that
  `-DFLEXIO_MCULCD_DATA_BUS_WIDTH=8` really does reach
  `fsl_flexio_mculcd.c`'s compile command (ruling out a CMake macro-timing/
  scoping bug, since `mcux_add_macro()` is called after
  `include(${SdkRootDirPath}/CMakeLists.txt)` in this project's
  `CMakeLists.txt` - that ordering turned out to still work correctly).
  Also confirmed the `setCSPin`/`setRSPin` callback signature (1 arg, not
  2) matches the SDK's default `FLEXIO_MCULCD_LEGACY_GPIO_FUNC=1`, so no
  calling-convention mismatch either. **In short: nothing found in the
  FlexIO transport code itself. If this is a FlexIO-specific bug, it's not
  a config/wiring-table mistake that a code review can catch - it would
  have to be a genuine electrical/timing behavior difference vs bit-bang.**

- **Added: a bit-bang GPIO diagnostic build**, to directly test "is this
  FlexIO-specific, or would it fail with any transport." Build with:
  ```
  $ ./build.sh rebuild -DLCD_BITBANG_DIAGNOSTIC=ON
  ```
  (plain `./build.sh rebuild` / `./build.sh build` goes back to the normal
  FlexIO path). This bypasses the FlexIO peripheral entirely and drives the
  same 14 J8 pins with plain `GPIO_PinWrite()` loops - see
  `source/display/lcd_bitbang_j8.c` for the full rationale (also holds RD
  high continuously the whole time it runs, as a second test of the RD
  finding above). Uses the exact same generic MIPI-DCS init sequence, byte
  for byte, so this isolates the transport, not the init sequence. Both
  build variants compile clean with `-Werror` (verified this session) but
  **neither has been flashed/tested on real hardware yet - that's the
  immediate next step for a new session.**

### Pending tests for next session, in order of effort

1. ~~Flash the bit-bang diagnostic build~~ **Done - confirmed working
   (displays an image).** Rules out wiring/panel/init sequence for good.
2. ~~Tie LCD_RD to 3.3V~~ **Done differently: fixed in firmware instead
   (P0_8 now a plain always-high GPIO, not routed through FlexIO at all) -
   no physical rewiring needed.** Confirmed this was a real, necessary fix:
   normal FlexIO build went from solid white to visibly responding (with
   noise) after this change.
3. **Flash the current build (normal FlexIO path, no
   `LCD_BITBANG_DIAGNOSTIC` flag) and check if the black/white noise is
   gone**, now that `hardware_init.c`/`app.h` drop the FlexIO bus down to
   ~10 kHz/pin (~40x slower than the 400 kHz/pin that produced noise - see
   "Current status" above). This is the immediate next thing to try; not
   yet tested on real hardware as of this session.
   - **If the image is now clean**: the remaining problem was purely bus
     speed. Raise `DEMO_FLEXIO_BAUDRATE_BPS` (app.h) and
     `kCLOCK_DivFlexioClk` (hardware_init.c) back up together in steps
     (e.g. halve the clock divider each time, or double the baud rate) to
     find the actual safe ceiling for this panel - it needs to be fast
     enough for real-time camera frame rate (320x240 @ ~30 fps needs
     roughly >4 Mbit/s of actual pixel data alone), which 10 kHz/pin is
     nowhere near (~10+ sec/frame). Stop raising it as soon as noise
     reappears and back off one step.
   - **If noise is still present even this slow**: bus speed probably
     isn't the (whole) explanation. Next things to check: (a) whether the
     noise pattern is identical/deterministic across resets (points to a
     logic bug, e.g. GRAM addressing) vs different every time (points to
     analog noise/interference on the data lines themselves, e.g. long
     unshielded dupont wires with no series termination); (b) whether the
     noise appears even during `main.c`'s initial all-black "blank the
     screen" loop (trivial constant 0x0000 data) - if even that comes out
     noisy, the bug is almost certainly in the transport/electrical layer,
     not in anything camera-data-dependent; (c) fall back to an
     oscilloscope/logic-analyzer if available - probe P0_9 (WR) and a
     couple of data pins during a write and look for actual glitches/
     ringing on the lines, which firmware-side experiments alone can't
     conclusively rule in or out.

## Goal (from requirement.md)

FRDM-MCXN947 + OV7670 camera (J9) + TFT display, capture frames and show them
on the LCD, with a placeholder hook for an AI model later. See
[requirement.md](requirement.md) for the original ask.

## Hardware in hand

- FRDM-MCXN947 board
- OV7670 camera module -> J9 (SmartDMA/Camera header) - **works, confirmed**
- A TFT panel with an **8-bit parallel data bus** (LCD_D0..D7 only). Backlight
  is **white** and turns on correctly. **Exact controller chip is unknown** -
  user could not find/read a part number on the module. Currently wired to J8
  (FlexIO/LCD header) using the pin table in README.md.
- A *different* board (Arduino-header shield, "tftlcd for arduino uno
  (spiflash)", HSD024131-C1 glass) was used earlier in this project on the
  Arduino header via bit-banged GPIO, and **that setup successfully displayed
  a recognizable camera image**. It's not clear if this is the *same physical
  panel* re-wired to J8, or a *different* panel bought for J8 - **worth
  clarifying with the user first thing in a new session**, since it changes
  the debugging approach a lot (see "Open question" below).

## Camera (J9) - fully confirmed working, do not need to re-debug

Pin table and rework notes in [README.md](README.md#camera-ov7670---j9-smartdmacamera-header).
Serial log proof (PID/VER readback + changing pixel data across frames):
```
Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
Camera: frame #16 ready, 792 samples, pixel range 0xC0C6..0xFBEB, avg=0xE330
Camera: frame #46 ready, 792 samples, pixel range 0xC0C4..0xFDF1, avg=0xDE26
```
This has stayed working through every LCD-side change described below -
if it ever stops working, that's a NEW regression, not a pre-existing issue.

## LCD bring-up timeline

1. **Arduino header, bit-banged GPIO 8080 bus** (project's original design,
   per literal requirement.md wording). Fully brought up: correct orientation
   after fixing MADCTL, camera image (a face) visibly displayed. Had two
   accepted downsides (analog-only-pin jumper workarounds, screen tearing
   from slow bit-bang racing SmartDMA) that motivated switching to J8.
   **This code no longer exists in the repo** (deleted when switching to
   J8) - the working generic MIPI-DCS init sequence from it was carried
   forward into step 3 below.

2. **Switched to J8 FlexIO, 16-bit bus, ST7796S SDK driver** (matching NXP's
   own `display_examples/smartdma_camera_flexio_mculcd` example exactly).
   Builds and flashes fine, camera keeps working, but this was based on the
   *assumption* the panel is 16-bit and ST7796S - both turned out wrong (see
   next steps).

3. **User corrected: panel is 8-bit only** (`LCD_D0..D7`, not `D0..D15`).
   Added `FLEXIO_MCULCD_DATA_BUS_WIDTH=8` compile define
   (`CMakeLists.txt`), trimmed pin_mux.c/app.h to only configure/use 8 data
   pins (`FLEXIO0_D16..D23` = `P2_8,P2_9,P2_10,P2_11,P4_12,P4_13,P4_14,P4_15`).
   Builds fine. **Result: solid white screen, no image at all.**

4. **Backlight fix.** Realized J8 has a dedicated `LCD_BLK` pin (`P4_6`,
   confirmed from board pinout diagram) that NXP's reference never drives
   (the official LCD-PAR-S035 panel has an always-on backlight; a generic
   module usually doesn't). Added `DEMO_LCD_BLK_GPIO/PIN` + drive it high in
   `LCD_Init()`. **Result: backlight came on (white), but still no image
   content** - this was a real, necessary fix (screen was literally dark
   before), just not sufficient on its own.

5. **Diagnostic: Read Display ID (cmd 0x04).** Added `LCD_DiagnosticReadId()`
   in `lcd_flexio_mculcd.c`, prints 4 bytes read back right after reset.
   Results have been **inconclusive/inconsistent across attempts**:
   `C0 C0 C0 C0`, then ` 0 40 40 40`, then ` 0  0  0  0` (each after some
   other change, so not a controlled A/B test). Don't over-trust this
   signal - see the comment in that function for why (RD line often
   genuinely not wired on cheap panel PCBs, so a "bad" read doesn't prove
   the write path is also broken).

6. **User reported the panel used to work fine with a *different*, simpler
   init sequence on the old Arduino-header bit-bang code** (this is what
   prompted questioning whether the panel is really ST7796S). Rewrote
   `lcd_flexio_mculcd.c` to **stop using the SDK's `ST7796S_Init()`/
   `ST7796S_WritePixels()` etc. entirely**, and instead send the exact same
   generic MIPI-DCS command sequence that worked before (SW reset 0x01,
   sleep-out 0x11, MADCTL 0x36=0x68, pixel-format 0x3A=0x55, display-on
   0x29), now over the FlexIO hardware bus via raw
   `FLEXIO_MCULCD_WriteCommandBlocking`/`WriteDataArrayBlocking` calls
   instead of `ST7796S_*`. Also had to hand-roll pixel byte-swapping
   (`LCD_PushPixels`'s chunked loop) since `FLEXIO_MCULCD_WriteDataArrayBlocking`
   in 8-bit mode sends raw bytes with no endian handling, unlike the 16-bit
   path the reference example uses. **Result: still solid white, no image.**
   Removed `driver.st7796s` from `prj.conf` since no longer used.

7. **Bus speed.** Reasoned that the FlexIO hardware bus (originally
   20 MHz/pin, i.e. ~50ns per write cycle) might simply be too fast for
   this specific (probably very cheap/slow) controller to latch data,
   whereas the old GPIO bit-bang path was inherently much slower (software
   overhead) and "accidentally" gave the panel enough settling time.
   Dropped `DEMO_FLEXIO_BAUDRATE_BPS` way down. First attempt (2,000,000 =
   250 kHz/pin) made `FLEXIO_MCULCD_Init()` itself fail
   (`kStatus_InvalidArgument` - the timer divider field overflowed; with
   this board's 150 MHz FlexIO source clock, ~293 kHz/pin is roughly the
   floor for a valid divider). Raised to 3,200,000 (400 kHz/pin), which is
   accepted. **Result: still solid white, no image.** (User hasn't yet
   confirmed if the ID-read diagnostic value changed meaningfully at this
   speed - last known value was `00 00 00 00`.)

## Where things stand / what's NOT yet been tried

- **No oscilloscope/logic-analyzer verification.** Everything so far has
  been "change something plausible, rebuild, reflash, ask the human to look
  at the screen." Nobody has actually confirmed with an instrument whether
  WR/CS/RS/data pins are toggling as expected during a write. This is the
  most definitive next step if the user has access to a scope or even a
  logic analyzer/cheap oscilloscope - probe `P0_9` (WR) during
  `LCD_WriteCommand()`/`LCD_PushPixels()` and confirm pulses are happening
  at all.
- **Wiring hasn't been re-verified pin-by-pin with a multimeter** since the
  very first J8 wiring pass. Given how many fixes have *not* worked, a
  plain wiring error (one swapped/loose/miswired signal among the 14: RD,
  WR, CS, RS, RST, BLK, D0..D7) is now looking like the single most likely
  remaining explanation - nothing else has moved the needle from "solid
  white."
- **RD pin (`P0_8`) not actually connected on the panel PCB** is a live
  possibility (common on cheap write-only-in-practice modules) - if true,
  it could also disrupt writes depending on how the specific controller's
  8080 interface expects RD to idle. Worth trying: temporarily tie the
  panel's RD pin to a fixed logic level (matching whatever the datasheet/
  similar modules suggest, usually idle-high) instead of leaving it
  MCU-driven, *if* a datasheet or similar module's schematic can be found.
- **3.3V vs 5V logic levels.** MCXN947 GPIO is 3.3V. Some very cheap
  panel modules expect 5V TTL thresholds and may not reliably read a 3.3V
  "high" as high. Worth checking the module's actual logic-level
  requirement if a datasheet/listing can be found.
- **Panel might need a different init sequence than either one tried.**
  Both ST7796S's real init AND the generic ILI9481-family-ish sequence
  failed to produce an image. If the exact controller chip can be
  identified (check the panel PCB very closely with a magnifier/bright
  light for a laser-etched part number, or find the exact product listing/
  seller page it was bought from), a controller-specific init sequence
  could be tried instead.
- **Have not tried reverting to bit-banged GPIO on J8's pins** (as a
  diagnostic, not a permanent fix) to isolate "is this a FlexIO-hardware-
  specific problem" vs "is this a wiring/panel problem that would fail
  either way." Since bit-bang WAS proven to work on the Arduino header with
  a working panel, if bit-banging the *same* J8 pins also fails, that's
  strong evidence of a wiring problem specific to the J8 connections (not a
  FlexIO peculiarity). This is a relatively cheap thing to try and would be
  a good next step: temporarily repurpose the old bit-bang approach
  (`GPIO_PinWrite` loops) but targeting the J8 pins in `app.h`/`pin_mux.c`
  instead of the Arduino ones, bypassing FlexIO/baud-rate variables
  entirely.

## Open question for the user (ask first in a new session)

**Is the panel now wired to J8 the exact same physical LCD glass/board that
worked on the Arduino header earlier, or a different one?** This matters a
lot:
- If it's the **same physical panel**, then the working Arduino-header
  bring-up already proves the panel itself is good and the generic init
  sequence is correct for it - the bug is almost certainly in the **J8
  wiring or the FlexIO transport**, not the panel or init commands. Point
  debugging at continuity-checking the 14 J8 wires and/or the bit-bang
  isolation test above.
- If it's a **different/new panel bought for this**, then nothing has
  actually been proven to work with *this specific panel* yet - the
  "proven working init sequence" assumption from step 6 above may not even
  apply, and the controller-identification path becomes more important.

## Key current values (for quick reference without reading all the code)

- `DEMO_FLEXIO_BAUDRATE_BPS` = `3200000U` (400 kHz/pin) in
  `firmware/camera_ai_demo/board_port/cm33_core0/app.h`
- `FLEXIO_MCULCD_DATA_BUS_WIDTH=8` set in
  `firmware/camera_ai_demo/CMakeLists.txt` via `mcux_add_macro`
- LCD init sequence: see `LCD_InitPanel()` in
  `firmware/camera_ai_demo/source/display/lcd_flexio_mculcd.c` (generic
  MIPI-DCS, not ST7796S-specific)
- Full J8 pin table: [README.md](README.md#tft-8-bit-panel---j8-flexiolcd-header)
- Build/flash: `./firmware/camera_ai_demo/build.sh` (build+flash),
  `build.sh monitor` for serial console. See README.md "Building and
  flashing" for full details, including that the board sometimes drops off
  USB when being physically rewired - just reconnect and re-run.
