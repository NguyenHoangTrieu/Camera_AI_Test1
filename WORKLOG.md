# WORKLOG - Camera_AI_Test1

Running log of this project's bring-up, for picking up in a fresh session.
For the stable reference doc (pinout, build instructions), see
[README.md](README.md) - this file is the messier "what's been tried and
what happened" history, kept separate so README doesn't get cluttered.

> Older history (camera/LCD/USB bring-up, all of that now stable/working -
> see README.md's "History" section for the summary) was trimmed from this
> file to keep it focused on the current, unresolved problem below.

## Camera fps ROOT-CAUSED AND FIXED: XCLK was actually 6MHz (MAIN_CLK/25), not the 24MHz the OV7670 driver assumed - a real ~4x mismatch, confirmed present in NXP's own reference example too - fixed by re-sourcing CLKOUT from FRO_HF/2. Preview fps: 7 -> 8 (eDMA) -> 18 (this fix) (2026-09-04)

Follow-up to the entry directly below (same day). User asked to confirm the
camera's real free-running fps before deciding whether to invest in
fixing it (per the entry below's own recommended next step), then asked
to dive into fixing it once the number came back low.

**Confirmation test**: added a one-shot diagnostic in `main.c` (before the
normal preview loop) that counts `CAMERA_CAPTURE_GetFrameCount()`
increments over a fixed 3-second window with ZERO consumption - no
`LCD_DrawImage()` call at all, not even reading the frame buffer, since
the frame counter increments in the SmartDMA completion ISR regardless of
whether anything reads the frame out. Result: **`22 frames in 3s = 7.3fps
free-running`** - conclusively rules out the "software handshake/consume-
loop throttling" theory from the previous entry; the camera/SmartDMA path
itself is only delivering ~7.3fps, decoupled from anything LCD-side.

**Root cause, found by reading `hardware_init.c`'s own camera clock setup
next to `camera_capture.c`'s OV7670 config**: `BOARD_InitHardware()`
routes the camera's XCLK pin (P2_2/CLKOUT) from `MAIN_CLK` (150MHz, from
`BOARD_BootClockPLL150M()`) divided by 25 -
`CLOCK_SetClkDiv(kCLOCK_DivClkOut, 25U)` - giving a real, exact
**6,000,000 Hz**. But `camera_capture.c`'s `ov7670_resource_t` declares
`.xclock = kOV7670_InputClock24MHZ` and requests `framePerSec = 30U`.
`fsl_ov7670.c`'s `OV7670_Configure()` picks its `CLKRC` register value
(and the sensor's whole internal frame-timing state machine derives from
that, per the OV7670 datasheet) from a lookup table keyed on the
DECLARED xclock (24MHz) - it has no way to detect what XCLK the sensor is
ACTUALLY receiving. Feeding it 1/4 of the rate its own `CLKRC` setting
assumes makes its entire capture cycle run ~4x slower than intended:
30fps / 4 = 7.5fps - matching the measured 7.3fps almost exactly.

**Not a bug introduced by this project** - checked NXP's own
`examples/_boards/frdmmcxn947/display_examples/smartdma_camera_flexio_mculcd`
reference example (this project's `hardware_init.c` header comment
already credited it as the source of this camera clock bring-up code):
it has the EXACT SAME `CLOCK_AttachClk(kMAIN_CLK_to_CLKOUT);
CLOCK_SetClkDiv(kCLOCK_DivClkOut, 25U);` and its own camera source
(`smartdma_camera_flexio_mculcd.c`) ALSO declares `.xclock =
kOV7670_InputClock24MHZ` with `framePerSec = 30`. This looks like a
latent bug in NXP's own reference example - most likely never caught
because that example just shows a live low-fps feed on a parallel LCD
without anyone measuring the achieved rate against the requested one, the
same way this project hadn't measured it either until this session added
the zero-consumption diagnostic above.

**Why divisor=25 was chosen: it's the closest clean number to 24MHz from
150MHz, not actually 24MHz.** 150MHz has no integer divisor landing
exactly on any of the 4 XCLK rates `fsl_ov7670.c`'s lookup table supports
(24/12/26/13 MHz) - 150/24=6.25, 150/12=12.5, 150/26≈5.77, 150/13≈11.54,
none are whole numbers. Divisor 25 gives a suspiciously clean 6MHz, which
is probably why nobody's automated build/lint caught an obviously "wrong"
non-integer divisor - the number LOOKS deliberate, it's just deliberately
targeting the wrong clock rate for what the driver call three lines away
actually declares.

**Fix**: source CLKOUT from `FRO_HF` (48MHz) instead of `MAIN_CLK` -
`FRO_HF` is already running and independently confirmed stable at exactly
48MHz on this board (via `spi1_bus.c`'s `SPI1_BUS_GetSourceClockFreq()`
diagnostic, used for LPSPI1's own clock). `48,000,000 / 2 = 24,000,000` -
a genuine, exact 24MHz, actually matching what `camera_capture.c`
declares this time. Changed in `hardware_init.c`:
`CLOCK_AttachClk(kFRO_HF_to_CLKOUT); CLOCK_SetClkDiv(kCLOCK_DivClkOut,
2U);`.

**Confirmed on real hardware - dramatic, real improvement:**
- Re-ran the same zero-consumption camera-only diagnostic:
  **`90 frames in 3s = 30.0 fps free-running`** - hits the configured
  target exactly, confirming the XCLK mismatch really was the entire
  story.
- `LCD_CAMERA_PREVIEW=ON` build, full preview loop: **`LCD preview: 18
  fps`**, up from 8fps (eDMA fix) and 7fps (original CPU-polled path) -
  more than DOUBLED. `wait-for-frame` (time spent waiting for
  `CAMERA_CAPTURE_IsFrameReady()`) dropped from ~75.5ms/frame to
  **~0.3ms/frame** - the camera is now so much faster than the LCD push
  that frames are essentially always already waiting by the time the
  loop checks. The loop is now purely bound by the LCD push time
  (~56.9ms/frame, unchanged, still near its ~51ms bit-clock floor) -
  1000/56.9 ≈ 17.6fps, matching the measured 18fps almost exactly. This
  also means the whole investigation has come full circle: with the
  camera fixed, the LCD/SPI side (this file's very first entries) IS
  now, finally, actually the binding constraint again, just like the
  original ~19-20fps best-case math always assumed - that math was right
  all along, it just couldn't be reached while the camera was
  independently 4x too slow underneath it.
- Default AI-enabled production build (SD card + inference + LCD status
  text): reflashed, captured a fresh serial log - boots clean, SD card
  initializes, inference keeps running at the same ~3.9ms/frame cost as
  before (unaffected by camera rate, as expected - it's a separate NPU
  operation), the periodic `Camera: frame #N ready...` diagnostic log
  advances much faster than in previous captures (consistent with the
  camera genuinely running faster now), pixel range/avg values printed
  look like real varying image data, not flat/dead - no corruption or
  hangs observed.
- Removed the temporary zero-consumption diagnostic block from `main.c`
  afterward (it added a fixed 3-second boot delay, no longer needed once
  the fix was confirmed) - not left in as permanent instrumentation,
  unlike the per-frame fps/push-time diagnostics from the entry below,
  which stay since they're cheap and remain generally useful.

**Not yet confirmed: image quality/color at the new, much higher real
frame rate** - no camera/photo access this session, same caveat as the
eDMA-fix entry below. A faster camera clock changes the SmartDMA
capture cadence, not the SPI pixel-push mechanism, so this is a lower-risk
gap than the earlier eDMA corruption risk, but still genuinely
unconfirmed by eye.

### Next steps for a fresh session

1. **Get the user to look at the live image** at the new, higher fps -
   still the one thing this session's numbers can't substitute for (now
   doubly true: both the eDMA pixel-push AND the camera clock changed
   since the last human visual check).
2. 18fps is very close to the ~19-20fps best-case math for a 320x240 push
   at 24MHz SPI - LCD-side gains alone have very little headroom left
   without a higher SPI clock (the PLL0-route option discussed in earlier
   entries, with its own signal-integrity risk on this breadboard wiring)
   or reduced per-frame data (resolution/color-depth cut). Worth
   explicitly checking with the user whether 18fps is an acceptable
   stopping point before pursuing either.
3. Sanity-check whether the OV7670's OTHER frame-rate options (25/15/14fps,
   also indexed by declared xclock in the same lookup table) were ever
   used anywhere in this project with the OLD 6MHz-actual/24MHz-declared
   mismatch in effect - if `framePerSec` was ever changed away from 30
   anywhere, that specific configuration's assumptions should be
   re-checked against the real, now-fixed 24MHz XCLK too (not expected,
   given `camera_capture.c` only ever requests 30, but worth a quick grep
   before assuming no other code path is affected).
4. The bus-sharing baud-reclaim/delay-register logic and touch still have
   zero real-hardware confirmation beyond the LCD-only camera-preview
   test - see earlier entries for what to test (unchanged from previous
   entries - not touched this session).

## fps follow-up: per-chunk Prepare() hypothesis DISPROVED by direct A/B test - LCD push is already near its theoretical floor (~57ms/frame vs. ~51ms best-case); the real remaining bottleneck moved to the CAMERA side, not measured before now (2026-09-04)

Follow-up to the entry directly below (same day). User asked to try
raising fps for the LCD preview build specifically.

**Hypothesis tested: hoist `LPSPI_MasterTransferPrepareEDMALite()` out of
the per-chunk loop.** The previous entry's back-of-envelope math (451ms
"per window" total, divided by 19 chunks/frame instead of by frame count)
suggested ~21ms/chunk of Prepare()-related overhead. Split
`SPI1_BUS_TransferBytesDMA()` into `SPI1_BUS_PrepareDMA()` (called once
per `LCD_PushPixelsOpen()` invocation) + a leaner
`SPI1_BUS_TransferBytesDMA()` (no more per-chunk Prepare/RXMSK-clear) -
matches mcuxsdk's own reference example's call pattern exactly
(`examples/_boards/frdmmcxn947/driver_examples/lpspi/edma_b2b_transfer`
calls its Prepare-equivalent once, Transfer repeatedly). Confirmed safe
via `fsl_lpspi_edma.c` source: the eDMA completion callback resets
`handle->state` back to idle, so back-to-back `Transfer()` calls after one
`Prepare()` are supported, not just something the reference example
happens to get away with.

**Result: no measurable change** (455ms/window both before and after,
8fps both times). This DISPROVES the per-chunk-overhead hypothesis
outright - real A/B test, not just theory - the same "test it directly
instead of trusting the math" lesson this project's fps investigation
already learned once before (the CPU-polled path's chunk-size experiment,
several entries below).

**Root cause of the wrong math: misread the existing diagnostic.**
`LCD_DrawImage()`'s printed "`frame push=Xms (per window)`" is a TOTAL
across however many frames occurred in that ~1-second window, not a
single frame's time - dividing 451ms by 19 (chunks in ONE frame) was
comparing a many-frames total against a one-frame chunk count, nonsense
units. Fixed the diagnostic to also print frame count and a proper
`us/frame avg` - real number: **~56.9ms/frame** for `LCD_SetWindow()` +
`LCD_PushPixels()` combined - remarkably close to the ~51ms theoretical
bit-clock floor for a full 320x240 push at 24MHz. The eDMA fix from the
entry below was ALREADY performing near-optimally; there was no
meaningful per-chunk overhead left to remove, which is exactly why
hoisting Prepare() out changed nothing.

**So why is fps still only 8 (~125ms/frame) if the LCD push is only
~57ms?** Added a second diagnostic in `main.c`'s preview loop: time spent
waiting for `CAMERA_CAPTURE_IsFrameReady()` to go true, measured
separately from the LCD push. Result: **~75.5ms/frame spent waiting for
the camera**, MORE than the ~56.9ms spent pushing to the LCD (75.5 + 56.9
≈ 132ms ≈ 7.6fps, matching the measured 8fps closely). **The camera/
SmartDMA side is now the larger of the two bottlenecks, not the LCD
side** - this reverses this whole investigation's original assumption
(every earlier entry in this file assumed the SPI bus was the sole
ceiling on fps).

**Not yet investigated**: why camera frame delivery takes ~75ms/frame
when the sensor is configured for 30fps (~33ms/frame native,
`OV7670_Configure()`'s own `CLKRC` register math targets this
correctly for a 24MHz `xclock`) - `camera_capture.c` has no
`CAMERA_CAPTURE_Deinit()`/`Reinit()` calls in this specific preview loop
(unlike the default AI-build loop, which explicitly cycles SmartDMA around
inference for a different, already-understood reason - RAM bank
conflict, see below), so that's not the explanation here. `~75ms ≈ 2 x
~33ms` is a suggestive coincidence (worth checking whether
`kSMARTDMA_CameraWholeFrameQVGA`'s firmware needs 2 real sensor frames per
delivered output frame for some structural reason) but NOT confirmed -
could just as easily be a single-buffer handshake (SmartDMA only starts
capturing the next frame once the CPU clears the ready flag, i.e. no
overlap between capture and LCD-push time) rather than a 2-frames-per-1
ratio. Needs actual measurement (e.g. counting `CAMERA_CAPTURE_GetFrameCount()`
increments over a fixed time window with the LCD push disabled entirely,
to isolate the camera's true free-running delivery rate from whatever the
consuming loop does) before guessing further.

**Kept the Prepare()-once-per-frame restructuring** despite the null
result - it's still the architecturally correct usage pattern (matches
the tested reference example, avoids redundant module disable/flush/
re-enable churn every chunk even though it didn't move the needle on
THIS bottleneck) and the new diagnostics (per-frame LCD-push time,
per-frame camera-wait time) are useful, real instrumentation for whoever
picks up the camera-side investigation next - not reverted.

### Next steps for a fresh session

1. Isolate the camera's true free-running frame rate: temporarily skip
   `LCD_DrawImage()` in the preview loop (or make it a no-op) and measure
   `CAMERA_CAPTURE_GetFrameCount()` increments over a few seconds - if it's
   still ~13fps (matching the ~75ms/frame wait) with NOTHING consuming
   frames, the bottleneck is genuinely in SmartDMA/the sensor, not a
   software handshake; if it's much faster (near 30fps), something in the
   current loop is artificially throttling delivery.
2. If the camera really only delivers ~13fps: check whether
   `kSMARTDMA_CameraWholeFrameQVGA` structurally needs 2 sensor frames per
   delivered frame (would need reading the SmartDMA camera firmware's own
   behavior/docs, not just the calling API), or whether a different
   SmartDMA camera API mode exists with less per-frame overhead.
2b. If the camera delivers close to native 30fps when unconsumed: the
   next candidate is adding real double-buffering (two frame buffers,
   ping-pong between them) so SmartDMA can capture the NEXT frame while
   the CPU is still pushing the PREVIOUS one to the LCD - a bigger change
   than anything in this specific investigation so far (new buffer
   allocation, correct synchronization), current single-buffer design has
   no overlap between capture and push.
3. Once the real ceiling is understood, revisit whether further LCD-side
   optimization (raising `LCD_SPI_CHUNK_PIXELS`, pushing the SPI clock
   past 24MHz) is even worth it - at ~57ms/frame already near the 24MHz
   bit-clock floor, LCD-side gains alone can't get overall fps much past
   what the camera side allows anyway, unless double-buffering closes the
   gap between the two.

## eDMA RX-channel hang: ROOT-CAUSED AND FIXED - TCR.RXMSK was stuck at 1, inherited from the driver's own write-only blocking transfers; eDMA pixel-push now runs on real hardware (7fps -> 8fps, no hang) (2026-09-04)

Follow-up to the entry directly below (same day). User asked to search the
internet and mcuxsdk's own examples for this exact hang, then try to solve
it. Web search (NXP community, Zephyr's LPSPI/eDMA issue trackers, the
MCXNx4x errata sheet) turned up related-but-not-matching reports (LPI2C
eDMA bus-error handling, an unrelated LPSPI slave-mode TX-FIFO-underrun
erratum, a Zephyr regression report with different symptoms) - no
externally documented fix for this exact symptom, so root-caused it
directly against mcuxsdk's own driver source and a fresh live register
trace instead.

**New diagnostic, not just re-reading source.** Added
`SPI1_BUS_RunDmaDiagnostic()` (spi1_bus.c, temporary - removed again once
the fix was confirmed) - runs one small, fully isolated 64-byte eDMA
transfer via `kLPSPI_MasterPcs1` (nothing physically selected, safe to run
at any point in boot) and prints a live trace of LPSPI1's FIFO counts
(FSR), both DMA channels' CH_CSR/CH_ES, and LPSPI1's own CR/TCR/DER every
time any of them changes - richer than the previous session's single
static SWD snapshot. Called once at boot in the `LCD_CAMERA_PREVIEW`
build, right after `LCD_Init()`.

**Result, decisive:** TX's eDMA channel reached `CH_CSR=0x40000000`
(DONE) within 1 microsecond of starting - far faster than a real 64-byte
SPI transfer physically requires - while `FSR` read `tx=0, rx=0` for the
ENTIRE 200ms timeout window, never changing even once. `CR=0x00000001`
confirmed the module genuinely was enabled (ruling out a "module
disabled" theory). The one value that stood out: `TCR=0x01280007` - bit
19 set, which is **RXMSK** (Receive Data Mask): when set, the LPSPI
hardware discards incoming data instead of storing it to the RX FIFO.

**Root cause, confirmed by reading `fsl_lpspi.c` line-by-line:**
`LPSPI_MasterTransferBlocking()` sets `TCR.RXMSK = (rxData == NULL)` on
every call. This project's shared bus calls this with `rxData=NULL` for
EVERY write-only transfer - every LCD command byte via
`lcd_spi_hw.c`'s `LCD_WriteByte()`, and even the (until now) CPU-polled
pixel-push path itself - so RXMSK=1 gets set constantly, and nothing ever
clears it back to 0 for a subsequent transfer that actually wants RX data.
`LPSPI_MasterTransferPrepareEDMALite()` (`fsl_lpspi_edma.c`) only clears
`CONT`/`CONTC`/`BYSW`/`PCS` in its own TCR write - it silently **inherits**
whatever RXMSK was left at by the last blocking transfer. Since
`LCD_Init()`'s panel-init sequence writes several command bytes right
before the (attempted) eDMA pixel push, RXMSK was always 1 by the time the
eDMA transfer started - meaning the RX FIFO could structurally never
receive a single byte, so it could never reach the DMA watermark the RX
eDMA channel waits on, so that channel waited forever. This fully explains
every symptom from the previous session: TX completing (RXMSK doesn't
touch the TX path), the hang being identical regardless of SPI frame size
or chunk size (RXMSK has nothing to do with either), and the "channel-mux/
clock/DER all read correct" dead end (none of those were ever the
problem).

**Fix:** `SPI1_BUS_TransferBytesDMA()` (spi1_bus.c) now explicitly clears
`TCR.RXMSK` and `TCR.TXMSK` right after `LPSPI_MasterTransferPrepareEDMALite()`
succeeds, before starting the transfer - this transfer always wants real
(if discarded-by-the-caller) data flowing on both sides so eDMA can
observe FIFO activity and signal completion.

**Confirmed on real hardware, twice.** First via the diagnostic itself:
same 64-byte test, same live trace - `TCR` read `0x01280007` (RXMSK=1)
right before the fix's clear, then `DMA-DIAG: transfer completed
normally` instead of timing out. Second, for real: `lcd_spi_hw.c`'s
`LCD_PushPixelsOpen()` switched back from `SPI1_BUS_TransferBlocking()` to
`SPI1_BUS_TransferBytesDMA()`, diagnostic call removed, rebuilt, reflashed.
`LCD_CAMERA_PREVIEW=ON` build: runs continuously, no hang, `LCD preview: 8
fps` / `frame push=451ms (per window)` steady (up from the CPU-polled
path's confirmed 7fps / 1076ms). Default AI-enabled build (SD card +
inference + LCD status text, i.e. the actual shipped configuration, which
also exercises this same pixel-push path via `DEMO_ClearScreen()`): boots
and runs continuously for the full capture window - SD card initializes
fine, inference keeps running (~3.9ms/frame), no hangs or corruption
observed in the serial log.

**Not yet confirmed: image quality.** This session had no camera/photo
access to visually inspect the panel - all confirmation above is
register-level and fps-timing, not a look at the actual displayed image.
Given the previous eDMA attempt's real risk was image corruption (not
just hangs), and this project's own history has a precedent for a
"looks fine in diagnostics but was actually shifted/corrupted" bug (the
BGR color-cast entry, caught only by looking at a photo) - **get the user
to look at the live image before calling this fully done.**

**fps is 7->8, a real but modest gain - well short of the ~19-20fps
best-case math.** Back-of-envelope: 19 DMA chunk calls/frame
(`LCD_SPI_CHUNK_PIXELS=4096`) at 451ms/window is ~23.7ms/chunk, against a
~2.7ms theoretical bit-clock time per 8192-byte chunk at 24MHz - a
~21ms/chunk gap suggesting `LPSPI_MasterTransferPrepareEDMALite()`'s own
per-call cost (module disable/re-enable, FIFO flush, interrupt/DMA-request
bookkeeping - called on every chunk, not once per frame like mcuxsdk's own
reference example does it) is now the dominant remaining cost, the same
*class* of per-call-overhead issue the CPU-polled path had earlier in this
file, just with a different underlying cause. **Not yet tested directly**
(e.g. by raising `LCD_SPI_CHUNK_PIXELS` and re-measuring, the same
methodology that worked for previous per-call-overhead questions in this
project) - RAM is the blocker: the default AI-enabled build is already at
309,104/319,488 bytes (~96.75%) of `m_data` at the CURRENT chunk size,
only ~10.4KB free, not enough headroom to double the scratch buffer
without shrinking something else (the AI tensor arena, most likely)
first.

### Next steps for a fresh session

1. **Get the user to look at the live image** (camera preview or the
   default build's status-line screen) and confirm no corruption/tearing/
   glitching - this is the one thing this session's register-level/fps
   confirmation can't substitute for.
2. If chasing further fps: the next concrete lever is confirming the
   "`LPSPI_MasterTransferPrepareEDMALite()`'s per-chunk cost is now
   dominant" theory directly (e.g. wrap just that call with a DWT delta,
   the same measurement style already used elsewhere in this file) before
   deciding whether raising chunk size (needs RAM freed up elsewhere
   first) or restructuring to call Prepare() once per frame instead of
   once per chunk (mcuxsdk's own reference example's pattern) is the
   better next move.
3. Given 8fps is a real, working improvement over the previous confirmed-
   good 7fps, and further gains need either a RAM trade-off or restructuring
   work - worth explicitly checking with the user whether 8fps is
   acceptable before spending more effort here.
4. The bus-sharing baud-reclaim/delay-register logic and touch still have
   zero real-hardware confirmation beyond the LCD-only camera-preview
   test - see earlier entries for what to test.

## Older work (condensed) - full narratives trimmed, see git history for the original blow-by-blow if ever needed

Everything below this point predates the current fps/eDMA/camera investigation
above and is settled/superseded - kept only as a compact reference of what
was done and why, not as active next-steps.

### LCD/SPI bring-up path that led to today's 7fps CPU-polled baseline (2026-09-03 - 2026-09-04, before the entries above)

- Arduino-header LCD swapped from an 8-bit-parallel shield to a 2.4" SPI TFT
  module. First wired as bit-banged GPIO SPI (own driver,
  `lcd_spi_bitbang.c`), then moved to hardware LPSPI1, **sharing the bus**
  with the onboard microSD slot and wiring up the panel's touch controller
  (XPT2046, `touch_xpt2046.c` - present but never integrated into the UI).
  New `spi1_bus.c/h` shared-bus wrapper handles per-transaction baud-rate/
  delay-register reclaiming since SD/LCD/touch each want different rates on
  the same physical bus.
- First real-hardware test found and fixed a blue/cyan color cast (MADCTL
  BGR bit wrong for this panel vs. the earlier parallel one) and improved
  fps via pixel-push batching (chunked transfers instead of one SPI call
  per byte).
- Root-caused a real fps ceiling in three separate, independently-confirmed
  steps: (1) LPSPI1's source clock was FRO12M/12MHz (max ~6MHz SPI baud) -
  switched to FRO_HF/48MHz for a real ~24MHz ceiling; (2) missing
  `kLPSPI_MasterPcsContinuous` was inserting a full PCS setup/hold delay
  between every single byte even on an unrouted "don't care" PCS channel -
  fixed, 2fps -> 5fps; (3) `SPI1_BUS_SetBaudRate()` only updated the SCK
  divider, leaving PCS-to-SCK/between-transfer delay registers sized for a
  stale 400kHz baseline - fixed by recalculating them on every baud-rate
  change, 5fps -> 7fps, confirmed via live SWD register reads. A first
  eDMA attempt at this point hung on real hardware and was reverted (root
  cause found and fixed later - see the entries above this section).

### SD card snapshot-on-face-detection feature (2026-08-25)

Added SD-over-SPI + FatFs glue (`sd_spi_disk.c`) and a snapshot feature
(`snapshot.c`) that saves a boxed BMP to the shield's microSD slot on face
detection, rate-limited to 1/sec. Real-hardware bring-up found and fixed,
in order: a first-boot hang (missing SPI retry-timeout Kconfig option, plus
a retry-budget multiplication bug in `fsl_sdspi.c`'s call graph that could
block for minutes); a floating MISO line (shield has no pull-up of its own,
diagnosed by adding this chip's internal pull-up and confirming the fix
live rather than guessing short-vs-floating); a stale one-shot "SD init
timed out" deadline check that started incorrectly applying to every later
file write, not just the initial mount; writes stuck at the 400kHz
card-identification speed forever (`busBaudRate` never updated after
identification) - measured 3.3s/save, fixed by raising the operating baud
rate to 8MHz; missing T/U/R glyphs in the project's hand-picked minimal
font, rendering "CAPTURE" as "CAP   E"; and two usability fixes after
looking at a real saved image (detection box is inherently one 8x8 FOMO
grid cell, not a real bounding box - tried expanding it for display, user
asked to revert and keep it raw; split the "just saved" LCD notice's
display duration from the 1-second capture rate limit so it's actually
visible). All confirmed working end-to-end on real hardware.

### AI model integration and NPU (Neutron) bring-up (2026-08-24 - 2026-08-25)

- Integrated a trained Edge Impulse FOMO object-detection model (first a
  3-class drowsy-eye detector, later replaced entirely by a lighter
  single-class face detector, retrained twice to fit this chip's RAM -
  final deploy version: 72x72 input, fits stock `m_data` with real margin
  on both CPU and NPU backends). Fixed several real bugs along the way:
  a buffer overflow from setting `signal.total_length` to the raw camera
  frame size instead of the model's own input size (caused a precise bus
  fault marching off the end of the AI RAM pool); a misdiagnosed alignment
  fault that was actually a stack overflow (STKOF, an ARMv8-M hardware
  stack-limit check - easy to misread against the wrong CFSR bit); and a
  RAM collision between the AI tensor arena and the SmartDMA camera
  coprocessor's own firmware, which share the same physical `m_sramx` bank
  - fixed by stopping SmartDMA before inference and restarting it after
  (same time-multiplexing pattern already used elsewhere in this project
  for the camera/USB-HS voltage conflict).
- A same-day linker experiment that widened `m_data` by reclaiming the
  second CPU core's (core1, never booted) reserved RAM region was WRONG in
  practice (multicore SoCs commonly power-gate RAM per core, so "nothing
  disables it" didn't mean "it's powered") and briefly bricked UART output
  and SWD debug access entirely - recovered via NXP's `nxpdebugmbox`
  Debug-Mailbox tool (bypasses pyOCD's normal connection sequence, which
  has no retry logic for this specific failure) and reverted; not
  reintroduced.
- Ported the model to run on the chip's Neutron NPU instead of CPU+CMSIS-NN,
  via NXP's `neutron_converter` tool (fuses supported layers into one
  custom op) and a hand-written raw TFLite-Micro runner
  (`model_runner_npu.cpp`, bypassing Edge Impulse's own classifier
  entry point). Confirmed on real hardware: **~370-390x faster** than the
  CPU path (~3.3ms vs. ~1.27s per inference), same detection-quality
  pipeline (hand-ported FOMO grid postprocessing), comfortable arena
  headroom. This NPU path is the current default (`AI_MODEL_USE_NPU=ON`).
- Also fixed: an LCD bug where a multi-row status-color fill only ever
  painted the first row, because `LCD_PushPixels()` closes the SPI chip
  select every call and the fill loop called it once per row instead of
  keeping the transfer open across all rows (`LCD_PushPixelsOpen()`/
  `LCD_EndWindow()` added to fix this, still the pattern
  `LCD_PushPixelsOpen()`'s current callers use).
