# WORKLOG - Camera_AI_Test1

Running log of this project's bring-up, for picking up in a fresh session.
For the stable reference doc (pinout, build instructions), see
[README.md](README.md) - this file is the messier "what's been tried and
what happened" history, kept separate so README doesn't get cluttered.

> Older history (camera/LCD/USB bring-up, all of that now stable/working -
> see README.md's "History" section for the summary) was trimmed from this
> file to keep it focused on the current, unresolved problem below.

## SD card writes measured at ~3.3 SECONDS each on real hardware - the card was stuck running at its 400kHz identification speed forever, never switching up to a real operating speed - fixed (2026-08-25)

User reported the pipeline visibly pausing for a long time on every save
and asked for real timing numbers instead of a guess. Added a DWT-cycle
timer around the whole file-create-through-`f_close()` span in
`SNAPSHOT_OnFrame()` (`source/storage/snapshot.c`, same measurement
technique `AI_MODEL_RunInference()` already uses), rebuilt, reflashed,
and measured on real hardware:
```
Snapshot: saved FACE0030.BMP (write took 3302566us, 3302ms)
Snapshot: saved FACE0031.BMP (write took 3302722us, 3302ms)
Snapshot: saved FACE0032.BMP (write took 3302869us, 3302ms)
```
**~3.3 seconds, consistently, every single save** - far too slow for a
~150KB file on any real SD card.

**Root cause** (`source/storage/sd_spi_disk.c`): `s_host.busBaudRate` was
hardcoded to `400000` (400kHz) - the *card-identification* speed SD-over-
SPI is required to start at - and never updated afterward. Per SD-over-
SPI protocol, `middleware/sdmmc/sdspi/fsl_sdspi.c`'s `SDSPI_Init()`
switches up to a real operating speed right after reading the card's CSD
register, via `card->host->setFrequency(min(SD_CLOCK_25MHZ,
card->host->busBaudRate))` - since `busBaudRate` was left at the
identification-phase value, this speed-up step computed
`min(25MHz, 400kHz) = 400kHz`, so the card ran at 400kHz forever, for
every single byte of every read/write, not just the initial handshake.
At 400kbps, a ~153,666-byte BMP (66-byte header + 320×240×2 pixel bytes)
takes 153,666 × 8 / 400,000 ≈ **3.07 seconds** just from the bit rate
alone - matches the measured ~3.3s almost exactly once SD/FAT protocol
overhead is added on top.

**Fix**: added `SD_SPI_OPERATING_BAUDRATE` (8MHz) and set
`s_host.busBaudRate` to that instead of the identification-phase
constant. 8MHz, not the driver's 25MHz ceiling, chosen deliberately
conservative - this exact card/shield/wiring combination already needed
an internal pull-up just to communicate at all (see the floating-MISO
entry below), so signal integrity margin at higher speeds on this
specific setup isn't confirmed yet. Confirmed compiling clean and the
board booting/mounting normally after reflashing - **the actual faster
write time itself still needs a fresh face-triggered capture to
measure** (no face was in frame during the reflash-and-verify window in
this session).

### Next steps for a fresh session (this bug specifically)

1. Trigger a capture and check the new `(write took ...)` timing - should
   be roughly 8000/400 ≈ 20x faster than the 3.3s baseline above, i.e.
   very roughly ~150-200ms, though real SD/FAT overhead means don't
   expect it to be exactly linear.
2. If 8MHz turns out unreliable (write failures, corrupted files) on this
   specific card/shield, that's a signal-integrity headroom problem, not
   a logic bug - drop `SD_SPI_OPERATING_BAUDRATE` back down (try 4MHz,
   then 1MHz) rather than assuming something else broke.
3. If 8MHz works reliably, it may be safe to try higher (up to the
   driver's `SD_CLOCK_25MHZ` ceiling) - not attempted in this session,
   deliberately left as a known follow-up rather than pushed opportunistically.

## Confirmed a real capture end-to-end - first look at a saved snapshot revealed two more real-world usability issues, both fixed (2026-08-25)

With the previous two bugs fixed, the user pointed the camera at a real
face, got a saved `.BMP`, and looked at the actual result for the first
time:

1. **The green box only covered a small corner of the face** (e.g. the
   glasses/nose area, not the whole face). Not a bug - this is inherent
   to the FOMO model architecture already in use: FOMO detects "which 8x8
   grid cell (in the 72x72 NPU model's 9x9 grid) most likely contains an
   object center", it does not regress an actual bounding box size the
   way YOLO-style detectors do (see `NPU_HandleCube()`/
   `NPU_CubeCheckOverlap()` in `model_runner_npu.cpp`, ported from Edge
   Impulse's own `ei_handle_cube()`/`ei_cube_check_overlap()`). A box
   only grows past one grid cell if multiple *adjacent* cells
   independently score above the 0.5 confidence threshold for the same
   class and get merged - with one activated cell, the box is always
   exactly 8x8px in model-input space, scaled up to a visibly tiny
   rectangle in the saved 320x240 image, regardless of how big the real
   face is. This has always been true of every log line in this project
   showing `w=8 h=8` - just never visually obvious until an actual image
   was looked at.

   **Fix, tried then reverted** (`source/storage/snapshot.c`): grew the
   box 2.5x around its own center (`SNAPSHOT_ExpandBox()`,
   `SNAPSHOT_BBOX_EXPAND_FACTOR`) purely for how it looks in the saved
   file - never touched the AI model/threshold or the raw coordinates
   used for `FACE:1/0`/the rate limit. **User asked to revert this and
   keep the raw box as-is** (2026-08-25, same day) - removed
   `SNAPSHOT_ExpandBox()` entirely, `BBOX_DrawRect()` is called with the
   unpadded scaled box again, same as before this entry.

2. **The LCD's `CAPTURE: 1` (green) notice was never actually seen** by
   the user, even right after a successful save. Not a logic bug either -
   confirmed by re-reading `SNAPSHOT_OnFrame()`/`SNAPSHOT_IsNoticeActive()`
   line by line, the state transitions correctly on the very next status-
   line redraw after a save. The real problem: the notice window was tied
   to the same 1-second value as the capture rate-limit, and 1 second is
   not enough time for a person to notice a capture just happened and
   react (look at/photograph the LCD) before it's already reverted to
   `CAPTURE: 0` (gray).

   **Fix** (`source/storage/snapshot.c`/`.h`): split into two independent
   constants - `SNAPSHOT_RATE_LIMIT_MS` (1000, unchanged - still "never
   twice within 1 second", a hard requirement) and
   `SNAPSHOT_NOTICE_DURATION_MS` (4000, new) - the LCD notice now stays
   lit for 4 seconds regardless of when the next capture becomes
   possible again. `SNAPSHOT_IsNoticeActive()` is a human-facing display
   flag, not a machine-readable "capture in progress" signal, so
   decoupling these two windows has no correctness implications.

Both confirmed compiling clean and the board booting normally
(`Snapshot: SD card ready.`) after reflashing - the actual expanded-box/
longer-notice behavior itself needs a fresh face-triggered capture to see
directly (not re-verified in this exact form yet, only the boot path).

## Two more bugs found and FIXED on real hardware, after the mount itself started working: false "SD card init timed out" on every real file write, and 3 missing letters on the LCD's new status line (2026-08-25)

With the mount confirmed working (previous entry below), the user pointed
the camera at a real face and hit two more bugs immediately:

1. **`Snapshot: SD card init timed out after 2000ms (no valid response) -
   giving up.` printed on the very first face detection - even though the
   card had just mounted successfully at boot** (`Snapshot: SD card
   ready.` had printed moments earlier), followed by `Snapshot: could not
   create a new file on the SD card.` on every subsequent detection,
   never recovering.

   **Root cause**: `SDCARD_SPI_Exchange()`'s deadline check (added
   earlier this session, see the entry below) was unconditional - it
   compares the current cycle count against a deadline armed once, at
   boot, inside `SDCARD_SPI_Init()` (called only by `SDSPI_Init()`,
   called only by `disk_initialize()`, called only once by
   `SNAPSHOT_Init()`'s `f_mount()`). But `exchange()` itself is called
   for *every* SPI transaction forever after, including all later
   `SDSPI_ReadBlocks()`/`SDSPI_WriteBlocks()` calls during real file I/O -
   which don't go through `SDSPI_Init()` again. By the time the user's
   face was actually detected (many seconds into runtime), the
   boot-time 2-second deadline had long since passed, so the very first
   real read/write after boot failed instantly regardless of whether the
   card was healthy - and stayed permanently "timed out" from then on,
   since the failure flag was never reset.

   **Fix** (`source/storage/sd_spi_disk.c`): added an `s_initInProgress`
   flag, true only for the duration of `disk_initialize()`'s own
   `SDSPI_Init()` call - the deadline check in `SDCARD_SPI_Exchange()`
   now only applies while that flag is set, leaving later
   read/write operations to rely on `fsl_sdspi.c`'s own (now safely
   bounded, thanks to the `SPI_RETRY_TIMES` fix below) retry logic
   instead, same as any healthy SD-over-SPI stack.

2. **LCD showed `CAP   E: 0` instead of `CAPTURE: 0`** - missing exactly
   the T, U, R glyph positions. `source/display/font5x7.h` is a
   hand-picked minimal font (by design, to save flash - see its own
   header comment) that only ever covered the letters `"FACE"` needed;
   adding the `"CAPTURE"` status line earlier this session never checked
   whether the font actually had glyphs for the *new* letters it needed.
   Missing glyphs render as a blank glyph-width gap, not an error - easy
   to miss without actually looking at the screen. **Fixed**: added T, U,
   R glyphs (standard 5x7 dot-matrix bitmaps, same style as the existing
   letters).

Both confirmed fixed by rebuilding and reflashing on the same physical
board this session (though a **real face-triggered save
`Snapshot: saved FACE0001.BMP`** still wasn't captured in this specific
session - no face stayed in frame during testing; see "Next steps"
below).

### Next steps for a fresh session (these bugs specifically)

1. Point the camera at a real face and confirm `Snapshot: saved
   FACE0001.BMP` (or higher-numbered) appears in the log with no timeout
   warning beforehand, and that the LCD's `CAPTURE: 1` line lights up
   (fully readable now, not `CAP   E`) for ~1s after.
2. Pull the card and check the actual `.BMP` file on a computer - confirm
   it opens as a valid image with a green box roughly where the face was,
   not corrupted/truncated.

## Bug found and FIXED (confirmed on real hardware): SD snapshot feature's first boot hung completely - two separate bugs, not the SD wiring - full recovery confirmed by direct SWD register inspection (2026-08-25)

User flashed the SD snapshot feature (see the entry below) for the first
time on real hardware. Boot log stopped dead after
`LCD: bit-bang GPIO on the Arduino header` (right after `SNAPSHOT_Init()`
was reached in `main.c`) - no crash, no fault dump, just silence,
reproduced identically across multiple resets. User plugged in an SD
card and reflashed a first fix (below) - **hang persisted, byte-for-byte
identical symptom**, which is what led to the deeper investigation in
this entry.

Board was physically attached in this session, which made it possible to
actually halt the stuck core over SWD and read hardware state directly,
rather than continuing to guess from source alone - see
[ARCHITECTURE.md §5](ARCHITECTURE.md#5-debugging--tooling-notes) for the
full technical writeup (register addresses, exact values, the retry-math
explanation). Short version:

- **Bug 1**: `LPSPI_MasterTransferBlocking()` has no timeout at all unless
  the `SPI_RETRY_TIMES` macro is defined - and the Kconfig option for it
  only exists under a *different* LPSPI driver component than this board
  actually uses (`driver.lpspi` vs. the correct `driver.lpflexcomm_lpspi`),
  so it was silently never defined. **Fixed** by defining
  `-DSPI_RETRY_TIMES=100000` directly in `CMakeLists.txt`.
- **Bug 2** (why bug 1's fix alone wasn't enough - this is why the hang
  looked identical even after reflashing): confirmed via `pyocd commander
  -c halt -c reg` (PC was moving, not frozen - real work, just an
  enormous amount of it) and direct LPSPI1 register reads over SWD
  (`RDR` - the received-byte register - read `0x00` on every single poll,
  never the `0xFF` SD-over-SPI expects) that `fsl_sdspi.c`'s own
  20000-iteration retry constant is nested 2-3 levels deep in
  `SDSPI_Init()`'s call graph, so a *consistently* wrong response
  multiplies those budgets instead of adding them - minutes to hours
  worst case, not seconds, even though every individual wait is
  technically bounded. **Fixed** (`source/storage/sd_spi_disk.c`): the
  `exchange()` callback now enforces its own 2-second wall-clock deadline
  (DWT cycle counter) across the *whole* init attempt, and returns
  failure immediately once it's passed - since `fsl_sdspi.c` bails out of
  every retry loop the instant `exchange()` itself reports failure, this
  is what actually bounds total time, regardless of how large
  `fsl_sdspi.c`'s own internal retry math allows.

**Confirmed fixed on real hardware** (both fixes together, this session):
```
Snapshot: initializing SD card (LPSPI1, D10..D13)...
Snapshot: SD card init timed out after 2000ms (no valid response) - giving up.
Snapshot: no usable SD card on the shield's slot (D10..D13) - snapshots disabled.
AI_MODEL_RunInference: total classifier time = 3935us (3ms)
Camera: frame #31 ready, 792 samples, pixel range 0x2965..0x8C92, avg=0x45C7
```
Boot now reaches the camera+AI loop within ~2 seconds regardless of SD
card state; watched it run continuously through 90+ frames (3 log
cycles) with real, non-flat pixel data - the rest of the demo is
unaffected by whatever's still wrong with the SD card.

### Bug 3 (the real, final root cause) - `RDR=0x00` was a floating MISO line, not a short - fixed with one internal pull-up, confirmed on real hardware

After bugs 1+2 above stopped the hang, `RDR` still read a constant `0x00`
instead of the SD-over-SPI idle-high `0xFF`, meaning the card genuinely
never communicated (`Snapshot: no usable SD card...` every boot). Ruled
out first: the SD card itself - user plugged it into this laptop's
built-in SD reader (`O2Micro OZ711`, `sdhci-pci` driver) and it mounted
cleanly as `vfat`/FAT32 (`sudo blkid`/`fsck.fat -n` output confirmed this,
also cleared a stale dirty-bit from a previous non-clean eject) - and
confirmed both "card is in the shield's slot" and "shield is fully seated
in the Arduino header" directly with the user before going further.

**Root cause**: the TFT shield's SD slot has no pull-up of its own on the
DO (MISO) line - common on cheap SD shields, which assume the host MCU
provides one, same as most Arduino-family boards default to. This
project's `BOARD_InitSdCardPins()` (`board_port/pin_mux.c`) originally
muxed all 4 LPSPI1 pins with no pull config at all (matching the SDK's
own LPSPI1 b2b reference example, which never needed one because it talks
to another on-board LPSPI instance wired directly, not through a
connector to a 3rd-party shield) - with nothing anywhere pulling DO high
when the card isn't actively driving it, the MCU's input floated and
happened to read a stable `0x00`, indistinguishable code-side from a
genuine wiring short until proven otherwise.

**Diagnosis approach**: rather than guessing, tested it directly - added
this chip's own weak internal pull-up (`kPORT_PullUp` in `port_pin_config_t`)
to just the SDI/DO pin (P0_26) as a live experiment, reasoning: if the
line is floating, a pull-up should fix the reading; if something is
actively driving it low (a real short), a weak internal pull can't
override that and nothing would change. Rebuilt, reflashed, and the very
next boot printed `Snapshot: SD card ready.` - confirming floating, not
shorted, on the first try.

**Fix** (`board_port/pin_mux.c`, `BOARD_InitSdCardPins()`): the pull-up
is now a permanent part of the SDI pin's config, not a diagnostic-only
change - it's genuinely required for this shield to work on this board.

**Confirmed fixed on real hardware** (2026-08-25, all three fixes
together):
```
Snapshot: initializing SD card (LPSPI1, D10..D13)...
Snapshot: SD card ready.
AI_MODEL_RunInference: total classifier time = 3879us (3ms)
Camera: frame #16 ready, 792 samples, pixel range 0x2965..0xD65B, avg=0x6426
```

**Takeaway**: "the MCU reads garbage from an SPI slave" has (at least)
two electrically distinct causes that look identical from software alone
- a genuine short/wrong-signal, or a legitimately unpowered/undriven line
with nowhere for logic level to be pulled toward. A weak internal
pull-up is a cheap, fast, non-destructive way to tell them apart on real
hardware before reaching for a multimeter.

### Next steps for a fresh session (this bug specifically)

**None outstanding** - SD card snapshot feature is now confirmed working
end-to-end on real hardware (mount succeeds, no timeout). Remaining
verification, if picking this up fresh: point the camera at an actual
face and confirm `Snapshot: saved FACE0001.BMP` appears with the LCD's
`CAPTURE: 1` line lighting up for ~1s after - this specific board/card/
shield combination hasn't had a real face-triggered capture confirmed
yet, only the mount/init step.

## New feature: save a snapshot (boxed) to the TFT shield's microSD card on face detection, rate-limited to 1/sec - compile-verified only, not yet run on real hardware (2026-08-25)

Added `source/storage/sd_spi_disk.c` (LPSPI1-based SD-over-SPI + FatFs
`diskio.h` glue) and `source/storage/snapshot.c` (rate-limit + box-draw +
BMP write, wired into `main.c`'s main loop right after
`AI_MODEL_RunInference()`, before `CAMERA_CAPTURE_Reinit()`). Full design
rationale in [ARCHITECTURE.md §2](ARCHITECTURE.md#2-components), usage in
[README.md](README.md#snapshot-on-face-detection).

**Two SDK dead ends hit and worked around, both discovered only by trying
to build, not by reading docs first:**

1. `middleware/fatfs/source/fsl_sdspi_disk/` (the SDK's only ready-made
   SD-over-SPI + FatFs glue) is hardcoded to the DSPI peripheral
   (`fsl_dspi.h`) - Kinetis-family SPI, which doesn't exist on the MCXN947
   (LPSPI-family). Not a runtime failure, a **build-time** one: the header
   itself has `#if (BOARD_SDSPI_SPI_BASE == SPI0_BASE) ... #else #error`.
   Wrote `sd_spi_disk.c` from scratch instead, reimplementing the same 5
   `diskio.h` functions against `fsl_sdspi.h` (the actual card-protocol
   layer, which turned out to be chip-agnostic - only the host glue
   underneath it needed replacing) with an `sdspi_host_t` driving `LPSPI1`.
2. `CONFIG_MCUX_COMPONENT_middleware.sdmmc.sdspi=y` alone doesn't build
   either - it auto-selects `middleware.sdmmc.common`
   (`fsl_sdmmc_common.c`), and that component's own header
   unconditionally `#include`s `fsl_sdmmc_host.h`, which only exists when
   a host-controller component (SDHC/USDHC) is also selected. This project
   has no such controller (talks to the card over LPSPI1 only). Fix:
   pull just `sdspi/fsl_sdspi.c` + `sdspi/fsl_sdspi.h` +
   `common/fsl_sdmmc_spec.h` directly in `CMakeLists.txt`, bypassing the
   Kconfig component - confirmed `fsl_sdspi.c` never actually calls into
   `fsl_sdmmc_common.c`, so nothing was lost by skipping it.

Also **not `CONFIG_MCUX_COMPONENT_driver.lpspi`** for the SPI peripheral
itself, even though that's the name used elsewhere in the SDK (and in
`../../touch_rgb`-style examples on other MCX chips) - on the MCXN947,
LPSPI lives inside a `LP_FLEXCOMM` interface, so the Kconfig gate is
`MCUX_HAS_COMPONENT_driver.lpflexcomm_lpspi` /
`CONFIG_MCUX_COMPONENT_driver.lpflexcomm_lpspi=y` instead (see
`devices/MCX/MCXN/MCXN947/Kconfig.chip`'s
`MCUX_HW_IP_DriverType_LPFLEXCOMM_LPSPI`) - same `fsl_lpspi.c`/`.h` API
either way, confirmed by finding `examples/_boards/frdmmcxn947/driver_examples/lpspi/interrupt_b2b_transfer/master/cm33_core0/app.h` already using `LPSPI1`/`LPSPI_MasterInit()` successfully on this exact board.

**Pin mapping (Arduino D10..D13 → P0_27/P0_24/P0_26/P0_25, LPSPI1 PCS0/SDO/SDI/SCK) came from NXP's UM12018 pin tables** (`pdftotext -layout` over the manual, searching for "D10"/"D11"/"D12"/"D13"), not from a continuity check like the LCD pins - **not physically confirmed**, see README.md's Known Limitations.

Both `-DAI_MODEL_USE_NPU=ON` (default) and `=OFF` build clean with the
feature added: `m_data` at 94.00%/93.28% respectively (up from
93.78%/93.05% before this feature - the added static RAM is the `FATFS`
object, `sdspi_card_t`/`sdspi_host_t`, and snapshot.c's own small statics;
the BMP write itself needs no extra frame-sized buffer, see
ARCHITECTURE.md). Still real headroom left (~18-21KB) on both.

### Next steps for a fresh session (this feature specifically)

1. **Run on real hardware** with an actual FAT-formatted microSD card in
   the shield's slot - nothing above has been powered on yet. Check for
   `Snapshot: saved FACE0001.BMP` in the serial log on the first detected
   face, and open the resulting file to confirm it's a valid, correctly-
   oriented (not upside-down/mirrored) image with a box roughly where the
   face actually was.
2. If the box position looks wrong: check the AI-input-space -> camera-
   frame-space scaling in `SNAPSHOT_OnFrame()` (`source/storage/snapshot.c`)
   against `AI_MODEL_GetInputWidth/Height()`'s actual values for whichever
   model is active.
3. If nothing gets written at all: check for `Snapshot: no usable SD card`
   at boot first (means `SDSPI_Init()` itself failed - verify wiring
   against the UM12018-derived pin table above with a multimeter before
   assuming it's a code bug) vs. no snapshot log at all (means no face was
   ever detected with `score` above the model's own threshold - not a
   snapshot-code issue).

## CURRENT STATUS (as of the previous session): face-only FOMO model (deploy version 2) fully working end-to-end on real hardware, including real (non-flat) camera data - AI_MODEL_USE_NPU ON by default (2026-08-25)

**Confirmed on real hardware, real (non-flat) pixel data**:
```
AI_MODEL_Init: Neutron NPU face detector ready (72x72 input, 1 class(es), arena used 94388/122880 bytes)
AI_MODEL_RunInference: total classifier time = 3898us (3ms)
Camera: frame #61 ready, 792 samples, pixel range 0x426C..0xF71D, avg=0xC42E
```
Runs continuously, no crashes, no allocation failures, camera frame count climbs normally.

### Bug found and fixed: every captured frame read back completely flat (`pixel range 0x0..0x0`) in the AI-integrated build, even though the identical camera driver worked fine in the plain camera-preview build and even after a genuine power cycle (not just a probe reset) - ruled out as a hardware/physical issue

User reported this after retraining the model above and getting it running - the pipeline no longer failed to allocate, but detection was clearly broken (every logged frame showed `pixel range 0x0..0x0, avg=0x0000` - literally every sampled pixel exactly zero, not just dark). Investigated and fixed in this session:

- **Ruled out first**: `source/camera/camera_capture.c` is byte-identical to the version already confirmed working with the project's earlier 3-class model at a similarly fast (~3ms) NPU inference cadence (confirmed via `git diff` - this file isn't in the list of files touched this session at all). Also ruled out: physical camera disconnection (user confirmed testing the plain camera-preview build immediately before the AI build, same physical setup, no reconnection - preview showed a real live image); a genuine hardware power-on-reset vs. a probe-triggered warm reset (user did a real USB unplug/replug, still flat).
- **Root cause**: `main.c`'s default AI loop calls `CAMERA_CAPTURE_Deinit()` -> `AI_MODEL_RunInference()` -> `CAMERA_CAPTURE_Reinit()` around every single inference (needed to keep SmartDMA off `m_sramx` while the AI arena might use it - see "Bug #3" below), then immediately trusts the *next* frame `CAMERA_CAPTURE_IsFrameReady()` reports. That very next frame is the first one SmartDMA captures after being freshly re-booted by `CAMERA_CAPTURE_Reinit()` (which reinstalls its firmware and re-`SMARTDMA_Boot()`s) - it needs that first cycle to (re-)synchronize with the OV7670's HREF/VSYNC/PCLK timing, and isn't trustworthy yet. The 3-class model's much slower CPU-path inference (or, apparently, just different timing margins even on its ~3ms NPU path) evidently gave enough slack for this to not matter before; this model's consistent ~3.9ms cadence exposed it every time, deterministically (100% of frames, not intermittent).
- **Fix** (`source/main.c`): added a `skipNextFrame` flag, set right after `CAMERA_CAPTURE_Reinit()`. The very next `IsFrameReady()` frame is discarded (`continue`s the loop without logging/running inference on it); the frame *after that* is used. Confirmed fixed on real hardware - `pixel range` immediately went from `0x0..0x0` to real, varying values (`0x424B..0xEF5D`, `0x426C..0xF71D`, ...) across multiple logged frames.
- **Cost**: each inference cycle now consumes 2 real camera frames (one discarded, one used) instead of 1 - effectively halves the achievable inference rate versus a hypothetical fix that didn't need to discard a frame, though at ~3.9ms/inference there's enormous headroom before the camera's own 30fps (~33ms/frame) cadence becomes the bottleneck either way.
- **Not fully root-caused at the register level** - this is a working, hardware-confirmed fix for the *symptom*, but *why* the very first post-reinit frame is bad (SmartDMA's own internal DVP sync state machine needing a settle cycle, vs. something else) wasn't confirmed via register-level inspection (e.g. SmartDMA status registers, logic analyzer on the DVP bus) - if a future session sees a related issue (e.g. still-occasionally-flat frames, or wants to recover the lost ~2x frame rate), start there rather than assuming this comment's theory is precisely correct.

**What changed to fix it**: the deploy-version-1 model below (96x96 input, FOMO MobileNetV2 alpha=0.35, 185,036-byte CPU arena / >=166,464-byte NPU arena) genuinely did not fit this chip's `m_data`, on either backend - see the "RAM shortfall" analysis kept below for the full arithmetic. Rather than any further memory trick, the user **retrained the same Studio project (1095726) with a smaller FOMO backbone** - deploy version 2: **72x72 input, `EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` = 112,460 bytes** (CPU path), NPU path's own `neutron_converter` estimate dropped to 93,636 bytes (`Total data`, inputs+outputs+scratch). Grid is now 9x9 (`INT8[1,9,9,2]` output, same FOMO stride-8 convention as every model this project has used). Both fit stock `m_data` (312KB) with real margin now: NPU build measures 93.78% `m_data` used, CPU build 93.05% - no linker tricks, no reclaimed core1 RAM, nothing risky.

Integration steps taken (identical process to the deploy-version-1 swap, confirms that process itself was always sound - only the model's size was ever the problem):
- `source/ai/edge_impulse/` replaced with the new Studio "C++ library" export (same file names as before - `tflite_learn_1095726_3.*` - since it's the same impulse, just retrained, not a new export/project).
- `source/ai/neutron/tflite_learn_1095726_3_npu.{tflite,h}` regenerated via `neutron_converter --input tflite_learn_1095726_3.tflite --target mcxn94x --output ..._npu.tflite --dump-header-file-output true` against the new plain `.tflite` - still 31/33 ops offloaded (`Slice`+`Softmax` stay CPU-side, same as before).
- `source/ai/model_runner_npu.cpp`: `NPU_MODEL_INPUT_WIDTH/HEIGHT` 96->72, `NPU_MODEL_GRID_WIDTH/HEIGHT` 12->9, `kTensorArenaSize` 184KB->**120KB** (93,636-byte estimate + ~28% margin, same proportional margin the project's original 64x64 model used).
- `source/main.c`: CPU-path `s_aiScratchPool` (the `m_data` overflow pool for `ei_sramx_alloc.c`) sized to **120KB** too - must be >= `EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` (112,460) on its own, since (as learned from the deploy-v1 incident) this allocator cannot split one `ei_calloc()` request across the primary (`m_sramx`) and overflow (`m_data`) tiers - see the comment there for the full explanation, worth reading before ever touching these sizes again.
- `CMakeLists.txt`: `AI_MODEL_USE_NPU` default flipped back **ON** now that its arena fits with real margin (~43KB spare in `m_data` on top of the 120KB arena + camera frame buffer).

**Flashed via the recovery-mode command sequence** (see "Incident" below and [ARCHITECTURE.md](ARCHITECTURE.md) section 4) - `nxpdebugmbox start-debug-session` immediately before `pyocd flash -O "pack.debug_sequences.disabled_sequences=..."`, then `nxpdebugmbox tool reset -h` to boot the new image. This is still needed every time on this probe/chip combination, unrelated to any of today's earlier incident - see "Next steps" below.

### Next steps for a fresh session

1. **Point the camera at an actual face** (lens cap off, aimed at someone) and re-check the serial log / LCD for `AI result: box[0] label=face x=... y=... w=... h=... score=...%` and `FACE: 1` on the LCD - the pipeline is confirmed working end-to-end, but real detection accuracy on this specific retrained (alpha smaller, F1 not yet re-checked in this integration session) model hasn't been eyeballed against a real face yet.
2. **Re-measure/sanity-check NPU vs. CPU timing** for this exact deploy-version-2 model if a CPU-path comparison print is wanted (NPU measured ~3.96ms/inference above; CPU path builds fine but hasn't been flashed/timed in this session since NPU was confirmed working first and is now the default).
3. Flashing this board **always** needs the `nxpdebugmbox start-debug-session` + `pyocd -O pack.debug_sequences.disabled_sequences=...` dance (see [ARCHITECTURE.md](ARCHITECTURE.md) section 4) - plain `./build.sh flash` alone still fails on this probe/pyOCD/pack combination, unrelated to any model/RAM issue. Worth eventually baking into `build.sh` itself.
4. If the model is ever retrained/re-exported again: check `EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` in the new `model_metadata.h` *before* touching firmware, same lesson as the incident below - a single-allocation arena over ~140KB will not fit `m_data` at all (camera frame buffer already claims 153,600 of the 319,488-byte stock region), regardless of how the two-tier `m_sramx`/`m_data` allocator is tuned.

### Historical: the deploy-version-1 (96x96, alpha=0.35) RAM shortfall and the core1-RAM incident it led to - all resolved by the retrain above, kept for the lessons in it

**Board was flashed with a SAFE, STABLE build** (CPU/CMSIS-NN path, `AI_MODEL_USE_NPU` was temporarily defaulted OFF - see below for why) - boots cleanly, camera runs continuously (frame count climbing normally), no crashes. **AI inference itself failed every frame** with `EI_SRAMX: alloc of 185053 bytes failed` / `Failed to allocate TFLite arena` - expected given the RAM shortfall explained below, not a bug that was chased further; fixed by retraining a smaller model instead (see the current-status entry above).

### Incident: the `m_data_ext.ld` "reclaim core1's RAM" trick from earlier today was WRONG and briefly made the board undebuggable - REVERTED

Earlier today (see the "Firmware integration" entry immediately below this one for the original context), `board_port/m_data_ext.ld` widened `m_data` by reclaiming the 104KB the SDK's board linker script reserves for this chip's second Cortex-M33 core (core1, never booted in this single-core project) - reasoning: address-contiguous, nothing in this tree disables/powers down SRAM banks, so it looked safe by the same logic that justified reusing `m_sramx` for the AI arena earlier (see "Bug #3" further down). **This reasoning was wrong in practice** - see [ARCHITECTURE.md](ARCHITECTURE.md) section 3 ("Core1's reserved RAM region cannot be reused either") for the full technical explanation (short version: multicore SoCs commonly power-gate SRAM per-core-domain, and a core that's never released from reset can leave its associated RAM bank unpowered - "nothing disables it" proved nothing, since nothing ever *enables* it either; this is a fundamentally different, less recoverable failure mode than the SmartDMA/`m_sramx` case, which was a software-timing problem). The build succeeded and pyOCD reported a normal flash+reset, but the board then:

- Produced **zero UART output** ever again (confirmed by capturing the serial port for 20+ seconds across multiple resets - not even the very first boot-banner `PRINTF`, which happens before camera/AI init) - consistent with a hang during `.bss` zero-initialization the instant startup code touched the "reclaimed" region, before `main()` even starts.
- Became **undebuggable over SWD** - `pyocd flash`/`reset`/`erase` all failed at the `DebugPortStart` debug sequence with `SWD/JTAG communication failure (WAIT ACK)` or `(FAULT ACK)`, consistently, across power cycles, cable/port swaps, and ISP-mode (SW3+SW1) attempts. `DP IDR` always read fine (physical SWD link was never the problem), but the chip-specific power-up handshake kept failing.

**Recovery procedure that worked** (kept here as the incident record; see [ARCHITECTURE.md](ARCHITECTURE.md) section 4, "Flashing/debugging this board reliably", for the same recipe written up as general reusable how-to, not because `m_data_ext.ld` should ever be re-added):
1. Installed NXP's official `spsdk` (`pip install spsdk`), which provides `nxpdebugmbox` - a CLI that speaks the MCX/LPC55-family **Debug Mailbox** protocol directly (`-i mcu-link` interface), bypassing pyOCD's generic CMSIS-Pack-based `DebugPortStart` sequence entirely (which has no retry logic for `WAIT ACK`/`FAULT ACK`, unlike spsdk's `debug_probe.py`, which has a built-in "recovery level 1" retry that pyOCD's mcxn947 pack doesn't).
2. `nxpdebugmbox -i mcu-link cmd -f mcxn947 erase` - **mass-erased the chip via the Debug Mailbox, succeeded on the first try** despite pyOCD being completely unable to connect. This is the key recovery step.
3. After erase, pyOCD's *generic* `mcxn947` target still couldn't fully connect (`Invalid AP address (#0)` in ISP mode; `ResetCatchClear`/`ResetSystem` debug-sequence `FAULT ACK` on `cm33_core1` in normal boot - **this MCX N94x CMSIS pack's stock debug sequences for core1 don't work with this pyOCD/probe combination at all, unrelated to the m_data incident** - flag this as a standing pyOCD/mcxn947-pack quirk for future flashing, not just erase-recovery). Worked around by:
   - `nxpdebugmbox -i mcu-link cmd -f mcxn947 start-debug-session` right before each `pyocd flash`/`reset` call (re-unlocks AHB access - doesn't persist across a fresh pyOCD connection, so must be re-run every time).
   - `pyocd flash -t mcxn947 -O "pack.debug_sequences.disabled_sequences=ResetCatchSet:cm33_core1,ResetCatchClear:cm33_core1,ResetSystem:cm33_core0,ResetSystem:cm33_core1" <elf>` - disables the specific broken debug sequences (pyOCD option `pack.debug_sequences.disabled_sequences`, comma-separated `SequenceName:coreName`). Flash then succeeds normally (`Erased .../ programmed ...` lines appear).
   - Disabling `ResetSystem` means pyOCD's own post-flash reset doesn't happen - reset the board manually afterward (`nxpdebugmbox -i mcu-link tool reset -f mcxn947 -h` for a hardware reset via the probe, or just press the board's SW1 / power-cycle) to actually boot the newly-flashed image.
4. Serial output confirmed real once flashed with a build that fits in `m_data` (see below) - this whole recovery chain is verified working, not just theorized.

**Fix applied**: `board_port/m_data_ext.ld` deleted; the `mcux_add_armgcc_linker_script()` call for it removed from `CMakeLists.txt` (left a warning comment there instead - see the file). `AI_MODEL_USE_NPU` CMake option **default changed from ON to OFF** (see "RAM shortfall" below for why NPU doesn't fit either, so ON was no longer a safe default).

### The real, still-unsolved problem: this model's tensor arena doesn't fit in m_data at all, on either backend

Once back on safe ground, the CPU-path build boots and runs but fails every inference:
```
EI_SRAMX: alloc of 185053 bytes failed (pool=32/96768, overflow=0/98304)
Failed to allocate TFLite arena (zu bytes)
```
Root cause, worked out precisely from the actual link map + this runtime log: the CPU path's tensor arena is **one single, contiguous `ei_calloc()` call** (185,036 bytes, `EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` from `model_metadata.h`) - `ei_sramx_alloc.c`'s two-tier allocator (`m_sramx` primary pool, `m_data` overflow pool) can only satisfy a *single* allocation that fits **entirely within one tier**, it cannot span/combine both tiers for one request (they're physically non-contiguous memory banks - `m_sramx` at `0x04000000`, `m_data` at `0x20000000` - a single C pointer can't span them). Neither tier is big enough alone: `m_sramx` primary is a hardware-fixed ~95KB, and `m_data`'s overflow pool - however big it's made - still has to coexist with the 320x240 camera frame buffer (153,600 bytes, always resident) inside the *stock, safe* `m_data` region (312KB total). `185,036 + 153,600 = 338,636` already exceeds `m_data`'s stock 319,488 bytes by ~19KB before counting stack/other small buffers - there is **no tuning of pool sizes that fixes this**, the deficit is structural. The NPU path has the same shape of problem (its own arena needs `neutron_converter`'s reported minimum of 166,464 bytes as one static buffer, and stock `m_data` only has ~141,752 bytes free for it after the frame buffer) - a smaller shortfall (~25-45KB) but the same root cause.

**This was the shortfall that led to retraining the model at alpha=0.1... actually deployed as a slightly different, smaller-input config (72x72) rather than literally "alpha=0.1 at 96x96" - see the current-status entry at the top of this file for exactly what the user's retrain produced and how it was integrated.** Kept the arithmetic above since it's the reusable lesson (single-allocation arena size vs. `m_data` budget), not because the specific alpha=0.35 numbers matter anymore.

## CURRENT STATUS (superseded by the entry at the top of this file): face-only FOMO model (CPU+NPU) is now wired into firmware, replacing the old 3-class model entirely - build-verified only, NOT YET FLASHED/TESTED on real hardware (2026-08-25)

The single-class **`face`** detector (Edge Impulse Studio project
`Face_Detection_NXP`, ID 1095726 - see "Face-only detection model" below
for the dataset/training story) has fully replaced the old 3-class
(`closed_eye`/`open_eye`/`yawning`, `Test_Drowsy_NXP` project) FOMO model
in this session:

- **Old model files deleted**: `source/ai/edge_impulse/` (the old Studio
  C++ library export) and `source/ai/neutron/tflite_learn_1094697_39_npu.*`
  (the old NPU-converted model) are gone, replaced with the new model's
  equivalents (`tflite_learn_1095726_3_npu.tflite`/`.h`).
- **`model_runner.cpp`** (CPU/CMSIS-NN path) needed **no logic changes** -
  it reads all dimensions/labels from `EI_CLASSIFIER_*` macros in the
  freshly-exported `model_metadata.h`, so it's model-agnostic by design.
  Only its comments were updated.
- **`model_runner_npu.cpp`** (NPU path) was rewritten: input 64x64 ->
  96x96, grid 8x8 -> 12x12, 3 classes -> 1 (`face`), and the op resolver
  gained an `AddSlice()` (this model's NPU output comes back with its
  channel dimension padded to 4 and needs slicing down to the real 2 -
  background + face - the earlier model's channel count happened not to
  need this). New NPU model produced via the same `neutron_converter
  --target mcxn94x` flow as before (see "NPU (Neutron) plan" below for
  the original walkthrough) - **31 of 33 operators offloaded** this time
  (one more un-offloaded op than before: `Slice` + `Softmax`, vs. just
  `Softmax`).
- **`main.c`**: the 3-line eye-status readout (`OPEN EYE`/`YAWNING`/
  `CLOSED EYE`) became a single `FACE: 1`/`FACE: 0` line.
- **New, bigger memory footprint required real linker changes**: this
  model's 96x96 input is much larger than the old 64x64 one, and its
  NPU graph needs a much bigger internal scratch buffer
  (`neutron_converter`'s own report: 166,464 bytes minimum, vs. the old
  model's ~99.8KB estimate / 74,660 bytes actual usage). Both backends'
  memory requirements now **overflow the SDK's stock `m_data` region
  (312KB)** - confirmed by an actual failed build attempt
  (`region m_data overflowed by 46664 bytes` with a 184KB NPU arena).
  Fixed by widening `m_data` via a new linker fragment,
  `board_port/m_data_ext.ld`, which reclaims the 104KB the SDK's board
  linker script reserves for this chip's second Cortex-M33 core (core1) -
  never booted in this single-core project, so genuinely idle RAM, same
  category of fix as the `m_sramx` reuse below (see that file's own
  header comment for the full reasoning, including why this is expected
  to be safe: no code anywhere in this tree powers down/disables SRAM
  banks at boot, and unlike `m_sramx` there's no active coprocessor that
  could collide with this region). CPU-path's `m_data` overflow pool
  (`main.c`'s `s_aiScratchPool`) was also bumped 16KB -> 96KB (this
  model's ~185KB CPU arena needs far more overflow above the 96KB
  `m_sramx` primary pool than the old model did), and is now only
  declared for the CPU build (guarded by a new `DEMO_AI_MODEL_USE_NPU`
  macro) so it doesn't waste `m_data` in the NPU build.
- **Build-verified**: both `-DAI_MODEL_USE_NPU=ON` (default) and `=OFF`
  build clean from scratch (`./build.sh rebuild` / `rebuild
  -DAI_MODEL_USE_NPU=OFF`) - NPU build: `m_data` 85.95% used (366,152 /
  425,984 bytes); CPU build: `m_data` 64.02% used (272,712 / 425,984
  bytes). Neither was flashed - **no debug probe was attached in this
  session**, so none of this has been confirmed on real hardware yet:
  not `AllocateTensors()` actually succeeding at the NPU arena's chosen
  size (184KB, above the converter's 166,464-byte minimum but with an
  unverified safety margin), not the widened `m_data` region actually
  being accessible/powered at that address, not detection accuracy, not
  NPU timing (the 3.3ms/~370-390x NPU speedup number throughout this file
  is from the *old* 3-class model - expected to carry over roughly, since
  the integration pattern is identical, but not re-measured for this
  model).
- Old orphaned artifact also removed per this cleanup: an unused
  `yolox_nano_custom.onnx` file that had been sitting at the project root,
  not referenced by any code or doc anywhere in this tree.

### Next steps for a fresh session

1. **Flash and test on real hardware** - this is the main gap. Watch for:
   `AI_MODEL_Init` printing "AllocateTensors() failed" (bump
   `kTensorArenaSize` in `model_runner_npu.cpp` past 184KB if so - there's
   headroom in the widened `m_data`, see the "Used Size" numbers above);
   any fault/hang touching addresses at or past the old `m_data` boundary
   (0x2004E000) - would indicate the `m_data_ext.ld` widening assumption
   was wrong (see that file's comment for what to check first); and
   whether `FACE: 1`/`FACE: 0` actually tracks a real face in front of the
   camera.
2. **Re-measure NPU vs. CPU timing** for this specific model (same DWT
   "total classifier time" print both paths already have) - don't assume
   the old model's ~370-390x number carries over exactly.
3. Everything under "Face-only detection model" below about improving
   detection quality (low-light performance specifically) still applies -
   that's about the model/dataset, unaffected by this firmware
   integration work.

## Face-only detection model (lightweight replacement) - Studio-side DONE, firmware integration DONE (see top of file), not yet flashed (2026-08-25)

**Goal (per user request):** replace the 3-class drowsy-eye FOMO model
above with a lighter, single-class **face detector** (`face` bounding
boxes only) - lower compute/RAM than the 3-class model, since it's a
simpler task (1 class vs 3, coarser grid decision). This section covers the
Edge Impulse Studio side (dataset + training), done via direct calls to the
Edge Impulse HTTP API (project API keys the user pasted into chat each
time, none saved to disk/repo - a fresh session needing further API access
will need the user to provide a new key) - see the top of this file for the
firmware integration that followed once the user downloaded this model.

### New project, not reusing the existing one - and why

Initially tried adding `face`-labeled data into the *existing* project
(`Test_Camera_NXP` / `Test_Drowsy_NXP`, project ID `1094697`, the one used
by the 3-class model above), since it's already `labelingMethod:
object_detection`. **This doesn't work**: Edge Impulse has no way to scope
an Impulse's training data to a label subset within one project - any
Object Detection impulse trains on *every* bounding box across *all*
images in the project's training/testing categories, so the new impulse
kept showing `Classes: 4 (closed_eye, face, open_eye, yawning)` instead of
just `face`. Disabling the old-label samples would fix this impulse but
break the ability to retrain the *original* 3-class impulse later (same
shared data pool) - so a **separate project was created instead**:
`Face_Detection_NXP`, project ID **1095726**. Confirmed via API
(`GET /v1/api/projects`) this is a distinct project owned by the same
account; the two projects/models don't interact.

### Dataset: WIDER FACE + DarkFace, filtered/uploaded via the ingestion API

- **WIDER FACE** (`Bingsu/wider_face_yolo` mirror on Hugging Face, YOLO-format
  labels, ~3.3GB zip) - filtered to images with 1-8 faces where
  `min(width_norm, height_norm) >= 0.06` (drops faces too small to matter at
  96x96 input), randomly sampled **1500 train + 300 valid**->test.
- **DarkFace** (`hieupth/dark_face_384` on Hugging Face, 384x384 resized,
  COCO-format labels) - low-light/nighttime faces, for robustness closer to
  the OV7670's real low-light image quality. Filtered similarly
  (`min_norm_dim >= 0.045`, <=10 faces/image), sampled **250 train + 50
  valid**->test.
- **License note**: both source datasets are CC-BY-NC-ND / research-only -
  fine for this prototype/research use, **not cleared for a commercial
  product** without sourcing a differently-licensed or self-collected
  dataset.
- Final upload: **1750 training + 350 testing** samples, single label
  `face`, pixel-space bounding boxes.
- Local prep scripts (if picking this up again -
  `/home/nguyenhoangtrieu/dataset_prep/`, outside the repo):
  `prepare_and_upload.py` (filter + build `manifest/full_manifest.json`,
  deterministic via `random.seed(42)`) and `upload.py` (ingestion API
  upload). **Raw dataset files were deleted after upload** to save disk
  (`wider_face/`, `dark_face/` dirs) - re-running `prepare_and_upload.py`
  requires re-downloading both datasets first (see script for HF URLs).

### Gotchas hit during upload (useful if scripting the EI API again)

1. **Ingestion API multipart field name must be literally `data` for every
   part** (both image files and the `bounding_boxes.labels` JSON part) -
   using the filename as the form field name gives a `500 Unexpected field`
   error with no other explanation.
2. **`bounding_boxes.labels` coordinates are in pixels of the actual
   uploaded image**, not normalized - `{"version":1,"type":"bounding-box-labels","boundingBoxes":{"<filename>":[{"label":"face","x":..,"y":..,"width":..,"height":..}]}}`,
   matched to images in the *same* multipart request by filename.
3. **Project `labelingMethod` must be explicitly `object_detection` before
   uploading**, or bounding boxes are silently dropped (sample gets
   `label: <filename>`, `boundingBoxes: []` instead) with no error from the
   API - burned one test upload confirming this. Set via
   `POST /v1/api/{projectId}` (JSON body) with
   `{"labelingMethod":"object_detection"}` - a project API key with admin
   role on that project can call this; the default ingestion-only key
   returned earlier could not (`insufficient permissions... current role:
   ingestion_deployment`). New projects created via the Studio UI without
   picking an "Object Detection" template default to `single_label`.
4. **Sandbox background-process quirk caused duplicate uploads twice** -
   backgrounding a Python upload script (via shell `&`, `nohup`, or the
   coding agent's own `run_in_background`) in this environment sometimes
   silently kept running *in addition to* a later foreground re-run of the
   same script, rather than actually being dead (empty log + no `ps` match
   was misleading - it can still be alive and finish minutes later). This
   caused **4x duplicate uploads** the first time (1094697, cleaned up by
   deleting the extra project's worth of test data - see below) and **2x**
   on testing-category uploads the second time (1095726, cleaned up too).
   **Lesson: always verify actual `dataSummaryPerCategory` counts via
   `GET /v1/api/{projectId}` after any backgrounded upload, and dedupe by
   filename (keep lowest sample `id`) via `DELETE /v1/api/{projectId}/raw-data/{id}`
   before trusting the numbers.** Also note: `DELETE` requires an admin-role
   key, not the default ingestion-scoped project key.
5. Also (unrelated to project 1095726, applies to the *old* project
   1094697): that project's free-tier compute-time quota is fully used up
   for the current period (resets ~2026-09-22) - training jobs there will
   likely fail/queue until reset. `Face_Detection_NXP` (1095726) is a fresh
   project with its own untouched compute quota.

### Impulse config that's been settled on (project 1095726)

- Image input: **96x96**, resize mode **Squash** (not the default "Fit
  shortest axis" - that center-crops to square and was silently dropping
  bounding boxes near the edges of wide WIDER-FACE images, seen as
  `WARN: failed to process ...: No bounding boxes after resizing` during
  "Generate features" - switching to Squash eliminated the warnings).
- DSP block: **Image** (plain, no extra config).
- Learning block: **Object Detection -> FOMO (Faster Objects, More Objects)
  MobileNetV2 0.35** - the largest FOMO variant Edge Impulse offers (only
  0.1 and 0.35 exist; the other listed options, MobileNetV2 SSD FPN-Lite
  and YOLO-Pro, don't fit here - SSD FPN-Lite is fixed at 320x320 input and
  explicitly can't be quantized properly, YOLO-Pro looked gated behind a
  paid tier).
- **Data augmentation: ON** - measurably helped (see results below).
- **"Use learned optimizer" (VeLO): tried once, DO NOT USE** - crashed
  training with `CUDA_ERROR_OUT_OF_MEMORY` on the free-tier Tesla T4 GPU
  (VeLO needs much more GPU RAM than the default Adam optimizer). Left
  unchecked in the working config.
- **Epochs: 60** - the sweet spot found by trial. 100 epochs was tried
  twice (with and without augmentation) and was *consistently worse* both
  times (overfitting - training keeps improving past 60 epochs but
  validation F1 drops), not just noise.

### Training results so far (validation set, quantized int8)

| Config | F1 | Recall (face) | Notes |
|---|---|---|---|
| 60 epochs, no augmentation | 71.1% | 73.3% | first working run |
| 100 epochs, no augmentation | 70.3% | 67.8% | worse - overfit |
| 60 epochs + augmentation | 72.4% | 76.1% | best so far |
| 100 epochs + augmentation | 69.4% | 66.6% | still worse - confirms 60 is the sweet spot even with augmentation |
| 60 epochs + augmentation (rerun) | 71.7% | 78.1% | confirms ~71-72% is a stable result, not a fluke; run-to-run variance of ~1% is expected (random init/augmentation/train-val split) |

On-device performance estimate (EON Compiler, CPU path - **not** the real
Neutron NPU numbers, see caveat below): ~132.9KB peak RAM, ~81.3KB flash,
241ms inferencing. Chip has 512KB total SRAM, so plenty of headroom even at
this CPU-path estimate. **Caveat**: like the 3-class model, the real
deployment path bypasses Edge Impulse's own runtime and runs the converted
model via raw TFLite Micro + Neutron NPU (see "NPU (Neutron) plan" below for
how that was done last time) - actual NPU inference time/RAM will differ
(almost certainly much faster/comparable-or-smaller RAM, per the 3-class
model's ~370-390x speedup precedent) once actually converted and measured
on hardware; the Studio numbers above are an upper-bound CPU estimate only.

**Model testing (350-sample held-out test set, run separately from the
training/validation split above):** F1 0.69, precision 0.63, recall 0.77 -
consistent with the validation numbers (no big overfit gap between
val/test). The Studio "Accuracy" metric on this same page showed a
misleadingly low **41.71%** - that's whole-image exact-match (every face in
an image must be detected correctly, or the whole image counts as wrong),
not a per-object metric, and images average ~1.7 faces each - not a sign of
a bad model, just a strict/different metric from F1. The Feature Explorer's
"incorrect" (pink) points cluster noticeably in the same region as the
DarkFace (low-light) samples identified during "Generate features" -
**the model is visibly weaker on low-light images specifically** than on
normally-lit WIDER FACE images - a real area for improvement, not yet
addressed.

### Next steps for a fresh session

1. **Decide whether ~F1 0.71-0.72 is good enough to ship, or worth
   improving first.** If improving: the clearest lever identified is
   low-light performance specifically (DarkFace cluster underperforms) -
   options include adding more DarkFace/low-light samples, or testing
   WIDER-FACE-only training to isolate how much the low-light data is
   actually hurting vs. helping overall F1 (hasn't been tried).
2. ~~Deployment~~, ~~convert for Neutron NPU~~, ~~firmware integration~~ -
   **all DONE**, see the top of this file. This model now fully
   **replaces** the 3-class model (decided with the user: not a coexisting
   build flag) - `source/ai/edge_impulse/` and
   `source/ai/neutron/tflite_learn_1095726_3_npu.*` are the new model;
   the old model's files were deleted.
3. Once flashed: re-verify camera/LCD/NPU integration doesn't regress
   anything documented as already-working below (Bug #2/#3/#4 fixes,
   NPU Phases 1-6) - this is new model weights (bigger input/arena, one
   extra NPU-resolver op) through an *already-proven* integration path,
   so regression risk should be low, but **hasn't been confirmed on real
   hardware yet** - no debug probe was available in the session that did
   the integration (see the top of this file for exactly what's
   build-verified vs. not).

## NPU (Neutron) plan - Phase 1 DONE: raw `.tflite` located, no Studio re-export needed

Goal (per user request): get the FOMO model running on this chip's Neutron16
NPU instead of the CPU+CMSIS-NN path, to cut the ~1.27s/inference time
measured above, with a build-time flag to compare NPU on/off using the
same DWT timing print already added to `model_runner.cpp`. Full plan (5
more phases after this one) discussed with the user before starting -
not repeated in full here, see the phase list below for what's left.

**Phase 1 result:** no need to log into Edge Impulse Studio and re-export
- the plain quantized `.tflite` this Studio project already produced is
sitting right next to the C++ library export that's already in the tree:
`source/ai/edge_impulse/tflite-model/tflite_learn_1094697_39.tflite`
(53.4KB). Confirmed this is the exact model currently running on-device -
`1094697` matches `EI_CLASSIFIER_PROJECT_ID` in
`model-parameters/model_metadata.h`.

Peeked at the flatbuffer's embedded strings (no `tensorflow`/`tflite_runtime`
Python package available in this environment to fully parse it, so this
is `strings`-level inspection only, not a real flatbuffer dump):
- Backbone is a MobileNetV2-style stack, `block_1` through `block_6`
  (inverted-residual blocks - `expand`/`depthwise`/`project`/`add`,
  standard fused Conv2D/DepthwiseConv2D/Relu6/BiasAdd naming), consistent
  with FOMO's usual "truncated MobileNetV2 backbone + a small detection
  head" architecture and the alpha=0.35 already noted for this Studio
  project elsewhere in this file.
- Head: `model_1/head/...` (Conv2D+BiasAdd+Relu) -> `model_1/logits/...`
  (Conv2D+BiasAdd) -> `output_0`.
- This is architecturally very close to the MobileNetV1 model NXP's own
  `middleware/eiq/mpp/tests/test_camera_mobilenet_view` example already
  runs on this exact board's Neutron16 NPU (int8, Conv2D/DepthwiseConv2D-
  heavy) - a good sign for op-support compatibility, though not a
  guarantee (won't know for sure until Phase 2's converter actually runs
  on it).

### Remaining phases (unstarted)

2. **Run `neutron_converter`** (Python package `eiq_neutron_sdk`, per
   `middleware/eiq/executorch/backends/nxp/backend/neutron_converter_manager.py`'s
   import) against the `.tflite` above, targeting the Neutron16 variant
   (matches `APP_USE_NEUTRON16_MODEL` in NXP's own frdmmcxn947 NPU
   example) - produces a new `.tflite` with supported subgraphs replaced
   by one `NEUTRON_GRAPH` custom op. **Not yet confirmed this package is
   installable/available in this environment** - may need an NXP/eIQ
   account; check before assuming this phase is a quick step.
3. **Convert the NPU `.tflite` to a C byte-array header**, same pattern as
   NXP's own `mobilenetv1_model_data_npu16_tflite.h`.
4. **Write `model_runner_npu.cpp`** - raw TFLM `MicroInterpreter` (not
   `ei_run_classifier()`, which has no way to register the `NEUTRON_GRAPH`
   custom op without patching EI's generated code) + `MicroMutableOpResolver`
   registering `Register_NEUTRON_GRAPH()` plus whatever ops stay
   un-offloaded (Dequantize/Softmax etc., per NXP's own
   `mobilenetv1_ops_micro_tflite.cpp` pattern) + link
   `middleware/eiq/neutron/mcxn/libNeutronDriver.a`/`libNeutronFirmware.a`.
   Reuse the existing resize/quantize preprocessing from `model_runner.cpp`
   (NPU only accelerates the conv/pool ops, not DSP preprocessing). FOMO's
   grid-decode postprocessing needs to be hand-written here too, since
   this path bypasses EI's `ei_run_classifier()` entirely (and thus its
   postprocessing) - fairly small, it's a per-cell argmax+threshold over
   the output grid tensor.
5. **`CMakeLists.txt`**: add an `AI_MODEL_USE_NPU` option (default OFF)
   selecting `model_runner.cpp` vs `model_runner_npu.cpp`, linking the
   Neutron libs and enabling the `middleware.eiq.tensorflow_lite_micro.neutron`
   Kconfig component only when ON.
6. **Build both configs, flash each, compare** the existing `total
   classifier time` DWT print (baseline already recorded above: ~1.27s,
   CPU+CMSIS-NN, non-EON) against the NPU build's number.

**Known risks, flagged before starting Phase 2:** `eiq_neutron_sdk`
package availability in this environment is unverified; not all of the
MobileNetV2-style graph is guaranteed to be Neutron-offloadable (partial
offload is normal/expected, real speedup unknown until measured); the
NPU-compiled model likely needs a different arena/scratch memory budget
than the current CMSIS-NN path's ~93KB (re-check against the `m_sramx`
budget worked out in "Bug #2"/"Bug #3" below before assuming it just
fits).

## Phase 2 DONE (2026-08-24): `eiq_neutron_sdk` installed, model converted, results very promising

**Install:** the package is NOT on public PyPI - it's on NXP's own index,
found by grepping the `mcuxsdk` checkout for install instructions
(`middleware/eiq/executorch/backends/nxp/requirements-eiq.txt` and
`docs/nxp/topics/overview.md`):
```
pip install --index-url https://eiq.nxp.com/repository eiq_neutron_sdk==3.1.1
```
Confirmed with the user before running (installing from a third-party
index isn't something to do silently). Installed cleanly into this
project's `tools/westenv` venv (140MB wheel). Provides CLI tools, not a
Python API to `import` directly - `neutron_converter`, `tflite_profiler`,
`tflite_quantizer` all land on `PATH` inside the venv.

**Target name for this chip:** `neutron_converter --show-targets` lists
`mcxn54x`/`mcxn94x`/`imxrt700`/`imx95`/`imx943`/`imx952`/`s32k5`/`s32n79`.
**MCXN947 is `mcxn94x`** (MCX N94x family). Note this contradicts the
ExecuTorch Neutron backend's own README (`backends/nxp/README.md`), which
claims only "eIQ Neutron N3-64 (i.MX RT700)" is supported - that's a
narrower, higher-level ExecuTorch-specific backend; the lower-level
`neutron_converter` CLI used here (the same tool, invoked directly)
supports MCX N94x directly and is what NXP's own TFLM-based mcxn947
examples (`middleware/eiq/mpp/tests/test_camera_mobilenet_view`) actually
use under the hood.

**Ran it against the exact model in this project** (Phase 1's
`tflite_learn_1094697_39.tflite`):
```
neutron_converter --input tflite_learn_1094697_39.tflite --target mcxn94x \
  --output tflite_learn_1094697_39_npu.tflite --dump-header-file-output true
```
**Result: 31 of 32 operators (96.9%) got offloaded into a single
`NeutronGraph` custom op** - only one operator stayed as a regular
builtin op. Converter's own cycle estimate for the NPU-accelerated part:
**353,039 cycles**. At this board's 150MHz core clock
(`BOARD_BOOTCLOCKPLL150M_CORE_CLOCK`, `examples/_boards/frdmmcxn947/clock_config.h`
- not yet confirmed this is the exact clock config this project's
`BOARD_InitHardware()` actually selects, so treat as an estimate, not a
promise) that's roughly **~2.35ms** for the NPU-accelerated portion -
compare against the ~1.27s (1270ms) CPU+CMSIS-NN baseline measured
earlier this session. That's a very large potential speedup (~500x) on
paper, but **this is the converter's static cycle estimate for the NPU
graph alone**, not a real on-device measurement - it does NOT include the
DSP resize/quantize preprocessing (still runs on CPU regardless of NPU),
the one un-offloaded operator, or any Neutron driver/data-marshalling
overhead. Treat as "very promising, worth pursuing" not "confirmed 500x
faster" until Phase 4-6 actually run it on hardware with the same DWT
timing method used for the CPU baseline.

**Memory footprint (from the converter's own report) is smaller than the
current CMSIS-NN arena, too:** NPU path needs 73,984 bytes of data
(inputs+outputs+scratch) + 25,840 bytes of weights = ~99.8KB total vs. the
current path's ~93KB tensor arena - roughly comparable, maybe fits the
same `m_sramx` budget, but not yet checked against the exact
`ei_sramx_alloc.c` allocator (which is EI-SDK-specific and won't be used
by the NPU path's raw TFLM interpreter anyway - Phase 4 needs its own
memory plan, likely simpler since it's not sharing an allocator with EI's
DSP step).

**Bonus - the converter tells you exactly what op resolver to write**,
right in a comment at the top of the generated header
(`tflite_learn_1094697_39_npu.h`):
```cpp
static tflite::MicroMutableOpResolver<2> s_microOpResolver;
s_microOpResolver.AddSoftmax();
s_microOpResolver.AddCustom(tflite::GetString_NEUTRON_GRAPH(), tflite::Register_NEUTRON_GRAPH());
```
Only 2 ops needed (simpler than NXP's own MobileNetV1 example, which
needed 3) - directly usable in Phase 4's `model_runner_npu.cpp`.

**Output files saved in-tree** (not just `/tmp`, so a fresh session can
pick this up without re-running the converter):
`source/ai/neutron/tflite_learn_1094697_39_npu.tflite` (27KB - smaller
than the original 53.4KB `.tflite`, since 31 ops collapsed into one
compact NeutronGraph blob) and
`source/ai/neutron/tflite_learn_1094697_39_npu.h` (168KB, ready-to-embed
C header, has the op resolver snippet above at the top).

## Phases 3-6 DONE (2026-08-24): NPU path fully working on real hardware - ~370-390x faster than CPU+CMSIS-NN

**Phase 3** (C header) was already done as a side effect of Phase 2's
`--dump-header-file-output` - nothing more needed there.

**Phase 4:** `source/ai/model_runner_npu.cpp` (new file) - implements the
same `model_runner.h` API as `model_runner.cpp`, but talks to TFLite
Micro directly instead of going through `ei_run_classifier()`:
- Op resolver exactly as the converter's generated header suggested
  (`AddSoftmax()` + `AddCustom(NEUTRON_GRAPH)`).
- Preprocessing: same nearest-neighbor squash resize as
  `model_runner.cpp`'s `get_signal_data()`, but writes straight into an
  int8 NHWC tensor instead of EI's packed-float signal format. Turned out
  trivial once the input tensor's actual quantization was known (via
  `neutron_converter --dump-after-import console`, see Phase 2 above):
  scale=0.003922 (~1/255), zero_point=-128, so quantizing an RGB channel
  value is just `q = channel_value - 128`.
- Postprocessing: hand-ported from Edge Impulse's own
  `process_fomo_i8()`/`ei_handle_cube()`/`process_cubes()`
  (`edge-impulse-sdk/classifier/postprocessing/ei_postprocessing_common.h`)
  - confirmed via that source and the model dump that the output tensor
  is `INT8[1,8,8,4]` (8x8 grid, channel 0 = FOMO's implicit "background"
  class, channels 1-3 map to `categories[0..2]` =
  closed_eye/open_eye/yawning), with the same adjacent-cell merge-into-box
  logic EI uses, just with fixed-size arrays instead of `std::vector`
  (this model's grid is tiny - 64 cells, 3 classes - so no dynamic
  allocation needed). Detection threshold 0.5, matching
  `model_variables.h`'s `.threshold`.
- Same DWT cycle-counter timing print as `model_runner.cpp`, so both
  paths' "total classifier time" lines are directly comparable.

**Phase 5:** `CMakeLists.txt` - `AI_MODEL_USE_NPU` option (default OFF).
ON selects `model_runner_npu.cpp` over `model_runner.cpp` and skips the
whole Edge Impulse SDK glob entirely (the two TFLM snapshots - EI's
vendored copy vs. NXP's `middleware/eiq/tensorflow-lite` - must not both
be linked into the same image). NPU branch instead: compiles
`tensorflow/lite/micro/kernels/neutron/neutron.cpp` +
`micro_time.cpp` + `debug_log.cpp` from source (none of these three are
in the precompiled lib - confirmed by first getting `undefined reference
to DebugLog` and fixing it by adding NXP's own ready-made
`debug_log.cpp`, which just wraps `PRINTF`/`fsl_debug_console`), links
the precompiled `lib/cm33/armgcc/libtflm.a` (whole TFLM core +
CMSIS-NN reference kernels) plus
`middleware/eiq/neutron/mcxn/libNeutronDriver.a`/`libNeutronFirmware.a`
directly via `target_link_libraries()` (simpler than fighting
`mcux_add_library()`'s CORES/TOOLCHAINS condition machinery for a
project that isn't Kconfig-driven anyway). **Did NOT need to touch
`main.c` at all** - `ei_sramx_alloc.c`/`ei_debug_porting.c` stay built in
both configs (harmless dead code in the NPU build, avoids having to
conditionally guard `main.c`'s `EI_SRAMX_SetOverflowPool()` call), and
both model runners implement the identical header.

Hit two build issues, both fixed:
- `mcux_add_include()` without `BASE_PATH` silently prepends
  `CMAKE_CURRENT_LIST_DIR` onto every entry - mangled the
  already-absolute `TFLM_ROOT`/`NEUTRON_ROOT` paths into a bogus nested
  path. Fixed by using `BASE_PATH ${SdkRootDirPath}` with relative
  `INCLUDES`, same pattern the EI SDK block above already uses.
- `kTensorArenaSize` first tried at 160KB - `m_data overflowed by 36648
  bytes` at link time (this project's non-NPU baseline already uses
  ~186KB of the 312KB `m_data` region for camera/LCD buffers etc., see
  "Bug #2"/"Bug #3" above - not much room left for a large static
  arena). Shrunk to 112KB, which fits (96.09% of `m_data` at link time -
  tight but works) and turned out to be plenty at runtime (see below).

**Phase 6 - built, flashed, measured on real hardware:**
```
AI_MODEL_Init: Neutron NPU FOMO ready (64x64 input, 3 classes, arena used 74660/114688 bytes)
AI_MODEL_RunInference: total classifier time = 3279us (3ms)
AI result: box[0] label=open_eye x=32 y=32 w=8 h=8 score=74%
AI_MODEL_RunInference: total classifier time = 3443us (3ms)
AI_MODEL_RunInference: total classifier time = 3270us (3ms)
AI result: box[0] label=open_eye x=24 y=24 w=8 h=8 score=52%
AI result: box[1] label=closed_eye x=40 y=32 w=8 h=8 score=59%
AI_MODEL_RunInference: total classifier time = 3264us (3ms)
AI_MODEL_RunInference: total classifier time = 3274us (3ms)
```
**~3.3ms per inference, consistently, across many consecutive runs - no
faults, no stalls.** Compare against the ~1.27s (1,270,000us) CPU+CMSIS-NN
baseline measured earlier this session: **roughly 370-390x faster**,
close to (a bit slower than, as expected - this includes the Softmax op
and framework overhead the converter's own 353,039-cycle/~2.35ms estimate
didn't count) the theoretical estimate from Phase 2. Arena headroom is
comfortable too - only 74,660 of the allocated 114,688 bytes actually
used, real margin above `AllocateTensors()`'s actual needs despite the
tight `m_data` link-time budget. Detection results vary sensibly
frame-to-frame (`open_eye`/`closed_eye`, different box positions/sizes/
scores, multi-cell boxes merging correctly, e.g. `w=8 h=8` single-cell
vs. a later `w=16 h=16` merged box) - the hand-rolled FOMO postprocessing
port is producing sane-looking output, not just "not crashing".

**Verified the CPU/non-NPU default path still builds identical to
before** (`rm -rf build && ./build.sh build` with `AI_MODEL_USE_NPU`
unset/OFF) - same `m_text`/`m_data`/`m_sramx` numbers as earlier in this
file, so the CMakeLists.txt changes for the NPU path are additive, not a
regression on the default build.

### Next steps for a fresh session

The NPU path is now functionally complete and confirmed fast/stable on
hardware. What's left is refinement, not core functionality:

1. **Detection-quality validation** (same caveat as the CPU path's own
   "Next steps" above) - varied/plausible results were observed, but
   accuracy hasn't been cross-checked frame-by-frame against what's
   actually in front of the camera.
2. **`kTensorArenaSize` (112KB) has ~40KB of unused headroom** at runtime
   (74,660 used) despite being link-time-tight against `m_data` (96.09%) -
   could shrink it back down (e.g. to ~80-88KB) to free up `m_data`
   margin for other uses, now that the real runtime number is known
   instead of guessing from the converter's static report.
3. **LCD status color + inference timing prints already work identically
   on both paths** (`main.c` didn't need to change) - no further wiring
   needed there.
4. **Not yet tried:** disabling `EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN` on
   the CPU path (older "Next steps" note, superseded in priority by the
   NPU result above - CMSIS-NN vs. reference-kernel CPU speed is much
   less interesting now that NPU is ~370x faster than CMSIS-NN already).

**Goal:** run a trained Edge Impulse FOMO object-detection model
("Test_Drowsy_NXP" Studio project, 3 classes: `closed_eye`/`open_eye`/
`yawning`, 64x64 input, int8 quantized) on-device, fed from the OV7670
camera, with results shown on the LCD (originally bounding boxes on the
live image, now just a solid status color - see below for why).

**Current export in tree:** `source/ai/edge_impulse/` = Studio deployment
v14 (impulse #11), **non-EON** (`EI_CLASSIFIER_COMPILED=0`, plain TFLite
Micro interpreter, not EON-compiled generated code - see "Bug #2" below
for why this was switched).

### Firmware architecture added for this (all still in the tree, believed
### structurally correct - see "Open problem" below for what's still broken)

- `source/ai/edge_impulse/` - the Edge Impulse C++ library export, built
  via `mcux_add_source()` with a hand-rolled `file(GLOB_RECURSE ...)` in
  the top-level `CMakeLists.txt` (**not** the export's own
  `CMakeLists.txt`/`add_subdirectory()` - that hardcodes `if(NOT TARGET
  app)`, but this SDK's west/non-find_package build mode names its real
  target `${MCUX_SDK_PROJECT_NAME}`, e.g. `camera_ai_demo_cm33_core0`, not
  literally `app`).
- `source/ai/model_runner.cpp` (was `.c` - had to become C++ to call the
  SDK) - calls `ei_run_classifier()` (the `extern "C"` wrapper in
  `ei_run_classifier_c.h`, not the templated `run_classifier()` in
  `ei_run_classifier.h` directly - including that header caused ODR
  "multiple definition" link errors against
  `edge-impulse-sdk/classifier/ei_run_classifier_c.cpp`, which already
  includes it. Only `ei_classifier_types.h` + `dsp/returntypes.h` are
  included here, plus a hand-written `extern "C"` forward-declaration of
  `ei_run_classifier()` - see the comment in that file for the exact ODR
  reasoning if this needs revisiting).
- `source/ai/ei_sramx_alloc.c/.h` - **custom allocator**, overrides the
  SDK's weak `ei_malloc`/`ei_calloc`/`ei_free`
  (`edge-impulse-sdk/porting/clib/ei_classifier_porting.cpp`) to serve
  memory from `m_sramx` (a 96KB SRAM bank that exists on this chip but
  isn't used by anything else, declared but never placed into any output
  section by the SDK's own linker script) instead of the real heap
  (`m_data`, which is ~99% full from camera+LCD framebuffers + stack -
  nowhere near enough for a ~93KB tensor arena). Two-tier: primary pool
  (m_sramx, fixed 96KB) + an optional "overflow" pool lent in via
  `EI_SRAMX_SetOverflowPool()` (main.c lends it a dedicated
  `s_aiScratchPool` buffer, repurposed from what used to be the live LCD
  image framebuffer - see below). Proper LIFO free-record stack (not just
  a no-op) - the DSP image-resize step allocates/frees a small scratch
  buffer ~75 times per inference (once per 1024-pixel page), and a naive
  no-op free() leaked every one of those and exhausted the pool well
  before the real bug (see "Bug #1" below) was found and fixed.
- `source/ai/ei_debug_porting.c` - overrides `ei_printf`/`ei_printf_float`
  (also weak in the SDK's clib porting layer) to go through this
  project's `fsl_debug_console` `PRINTF()`/`DbgConsole_Vprintf()` instead
  of plain `vprintf()`/`printf()`, which go nowhere useful under this
  project's `--specs=nosys.specs` link.
- `board_port/ei_sramx.ld` - **additive** linker fragment (a *second* `-T`
  passed via `mcux_add_armgcc_linker_script()` in `CMakeLists.txt`, after
  the SDK's own board linker script) adding one new output section that
  places `.ei_sramx`-tagged symbols into the `m_sramx` region. Does NOT
  use `INSERT AFTER` - tried that first, GNU ld rejected it ("`.bss` not
  found for insert"); a plain second `SECTIONS {}` block without INSERT
  just gets concatenated after the base script's sections by default,
  which is sufficient here (position within `m_sramx` relative to `m_data`
  sections doesn't matter, they're different physical banks).
- `CMakeLists.txt` additions: the linker fragment above; the hand-rolled
  EI source glob; `mcux_add_macro(... -DEI_PORTING_CLIB=1
  -DEI_C_LINKAGE=1 -DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1
  -DARM_MATH_LOOPUNROLL
  -DSILENCE_EI_CLASSFIER_OBJECT_DETECTION_COUNT_WARNING=1)` both in `CC`
  and `CX` categories (`mcux_add_macro()`'s C++ flag category is `CX`, not
  `CXX` - easy to miss); `-Wno-error` scoped to just the EI source files
  (the whole project otherwise builds `-Werror`, and EI's vendored
  TFLite-Micro/CMSIS-NN isn't fully warning-clean under this toolchain);
  `mcux_add_linker_symbol(SYMBOLS "__stack_size__=0x1000")` (bumped from
  the SDK default 2KB to 4KB - TFLite Micro's C++ call chain is deep).
- `source/fault_handler.c` - pre-existing HardFault dump handler, fixed
  during this work: it used `%08lX` throughout, which
  `debug_console_lite`'s minimal printf mishandles (prints the literal
  characters `lX` instead of the value - same class of bug as this
  project's pre-existing "`%f` not supported" notes elsewhere). Now uses
  `%08X` with `(unsigned int)` casts (fine, `unsigned long`/`unsigned int`
  are both 32-bit on this target). **Without this fix the fault dumps are
  useless** (all register values print as literal `0xlX`) - if a fresh
  session sees garbled fault dumps again, check this hasn't regressed.
- `source/display/bbox_overlay.c/.h` - draws bounding-box rectangles into
  an RGB565 buffer. **Currently unused** - see "Live image display
  disabled" below. Still compiles (harmless dead code), not deleted.
- `source/main.c` - runs `AI_MODEL_RunInference()` once per captured
  camera frame; on success, was drawing bounding boxes on the live camera
  image and pushing it to the LCD - **this is currently disabled** (see
  below).

### Live image display disabled (RAM optimization + simplification)

Per explicit direction mid-session: `main.c`'s live-camera-image-on-LCD
path (`memcpy` into `s_lcdSnapshot` + `DEMO_DrawAiBoxes()` +
`LCD_DrawImage()`) is commented out (`#if 0`, search
`DEMO_DrawAiBoxes`/`DEMO_ColorForLabel` in `main.c`), not deleted. The LCD
now just fills solid with a status color instead
(`DEMO_ShowStatusColor()`): red = `closed_eye`/`yawning` detected this
frame, green = `open_eye`, blue = nothing detected. The RAM that used to
be `s_lcdSnapshot` (a second 150KB framebuffer, needed for tearing-free
image display) is now `s_aiScratchPool`, permanently dedicated as the AI
allocator's overflow pool (see `ei_sramx_alloc.c` above) - no more of the
fragile "don't touch this buffer while inference is running" ordering
constraint that came with temporarily borrowing the display buffer (an
earlier, now-abandoned approach).

If image + bounding-box display is revisited, the commented-out code in
`main.c` is the starting point - but see the RAM budget math in
README.md/this file first, since re-adding a 150KB display buffer
directly competes with the AI model's RAM needs again.

### Bug #1 (FOUND AND FIXED): wrong `signal.total_length` - the real
### cause of the very first HardFault symptom, took most of this session
### to find

**Symptom:** a precise Bus Fault (`BFSR=0x82`, `PRECISERR`+`BFARVALID`),
`BFAR`/`MMFAR` = `0x04018000` **exactly** - the first byte past the end of
`m_sramx` (`0x04000000` + `0x18000` = `0x04018000`) - every single time,
regardless of how much RAM was given to the allocator (tried: bigger
primary pool, a 150KB overflow pool = 246KB combined, EON vs.
non-EON/interpreter model, proper LIFO free instead of a leaking no-op).
PC always inside `extract_image_features_quantized()`
(`edge-impulse-sdk/classifier/ei_run_dsp.h`), called via
`signal->get_data()` → my callback.

**Root cause:** `extract_image_features()`/`_quantized()` do **not
resize** - they read exactly `signal->total_length` elements via
`get_data()` and write that many pixels straight into `output_matrix`,
which the *caller* sizes for `EI_CLASSIFIER_INPUT_WIDTH *
EI_CLASSIFIER_INPUT_HEIGHT` (64*64 = 4096 pixels for this model).
`model_runner.cpp` was setting `signal.total_length = camera_width *
camera_height` (320*240 = 76800 - the **raw camera frame** size, not the
model's input size) - so the loop wrote ~76800*3 = 230,400 output values
into a buffer sized for 4096*3 = 12,288. A ~218KB overflow, silently
marching through `m_sramx` for the entire ~93KB tensor arena's worth of
memory and beyond, until it finally ran off the end of the whole bank and
faulted - **not** a capacity problem, which is why every RAM-budget fix
tried first had zero effect.

**Fix applied** (`source/ai/model_runner.cpp`): `signal.total_length` is
now `EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT` (4096), and
`get_signal_data()` does the resize itself - nearest-neighbor "squash"
(independent per-axis scaling, matching `EI_CLASSIFIER_RESIZE_MODE`):
for each requested *target* (64x64) pixel index, maps back to the nearest
*source* (320x240) camera pixel (`sx = tx * srcWidth / dstWidth`, same for
y), then converts that source pixel RGB565 → packed `0xRRGGBB` float as
before.

**Confirmed fixed**: after this change, the crash signature changed
entirely (see Bug #2) - no more `0x04018000` bus fault, so the DSP
resize/feature-extraction step now completes successfully. This was the
right fix.

### Bug #2 (FOUND AND FIXED, but see "Open problem" - crash persists past this): missing 8-byte alignment on the overflow pool

**Symptom (after fixing Bug #1):** a Usage Fault, not a Bus Fault this
time - `UFSR = 0x10` (bit 4, `UNALIGNED`), `MMFSR`/`BFSR` both 0. `MMFAR`/
`BFAR` show `0xE000ED34`/`0xE000ED38` (the *addresses of those two
registers themselves* - not meaningful; MMFAR/BFAR aren't valid/written
for a pure UsageFault, ignore them for this fault type). PC inside
`arm_nn_mat_mult_nt_t_s8` (CMSIS-NN), called from
`arm_convolve_1x1_s8_fast` - i.e. **actual NN convolution now running**,
real progress past the DSP stage.

**Suspected cause:** `s_aiScratchPool` (`main.c`, the overflow pool lent
to the AI allocator) was declared as a plain `static uint8_t
s_aiScratchPool[...]` with no alignment attribute, unlike `s_pool` in
`ei_sramx_alloc.c` (which has `__attribute__((aligned(8)))`). CMSIS-NN
kernels use wide/vectorized loads on tensor buffers and require alignment
- the allocator computes offsets that are 8-aligned *relative to the
pool's own start*, which only produces real 8-byte-aligned *addresses* if
the pool's start address is itself 8-aligned.

**Fix applied:** added `__attribute__((aligned(8)))` to
`s_aiScratchPool`'s declaration in `main.c`.

**Verified via `nm` after rebuilding** - all three pool-related symbols
land on 8-byte-aligned addresses:
```
04000000 b s_pool            (ei_sramx_alloc.c, primary pool)
04017a00 b s_records          (ei_sramx_alloc.c, LIFO free-record stack)
200009b8 b s_aiScratchPool.0  (main.c, overflow pool - 0x9b8 = 184 = 23*8)
```

## Bug #2 was MISDIAGNOSED - real cause was stack overflow (STKOF), not alignment; FOUND AND FIXED (2026-08-24)

**The "UsageFault, UNALIGNED" diagnosis above was wrong.** `CFSR = 0x00100000`
is bit 20 of CFSR, which is `UFSR` bit 4 - and per this SDK's own
`core_cm33.h`, **bit 4 of UFSR is `STKOF` (hardware stack-limit check,
ARMv8-M-only), not `UNALIGNED`** (`UNALIGNED` is UFSR bit 8, i.e. would
need `CFSR = 0x01000000`, not `0x00100000` - easy to misread without
checking the actual bit position against `core_cm33.h`). Confirmed by
disassembly: `arm-none-eabi-objdump
-d` at the faulting PC (`0x18A44`) shows `subw sp, sp, #2780` - the
*prologue* of `arm_nn_mat_mult_nt_t_s8`, reserving 2780 bytes of locals for
its unrolled `q31` accumulators. That's a pure arithmetic instruction, not
a memory access - it cannot raise `UNALIGNED`. It's exactly the kind of
instruction that trips a hardware `SP < PSPLIM` check the instant it
executes, which is what `STKOF` is. The `aligned(8)`/`aligned(16)` changes
made chasing the wrong diagnosis were harmless (real fixes, just not
_the_ fix) and have been kept.

**Fix applied:**
- `CMakeLists.txt`: `__stack_size__` `0x1000` (4KB) -> `0x4000` (16KB).
- `source/main.c`: `s_aiScratchPool` (the AI allocator's overflow pool)
  shrunk from `DEMO_BUFFER_WIDTH*DEMO_BUFFER_HEIGHT*sizeof(uint16_t)`
  (150KB) down to a fixed `16*1024` (16KB) - the tensor arena
  (`EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE` = 92876 bytes) fits entirely
  inside the primary 96KB `m_sramx` pool on its own (96KB minus the
  `ei_sramx_alloc.c` free-record stack leaves ~94KB), so the overflow pool
  was never doing much beyond covering the DSP resize step's small
  per-page scratch buffers - 150KB was massive overkill, and was exactly
  what was starving `m_data` of room to grow the stack (`m_data` was at
  98.67% before this fix, 59.56% after).

**Confirmed fixed**: flashed and reset - **no HardFault at all**, boot log
completes and the main loop runs. (See Bug #3 below for what turned up
next, once inference was actually completing multiple times in a row.)

## Bug #3 (FOUND, NOT YET FIXED): `m_sramx` collides with the SmartDMA camera coprocessor's own firmware RAM - camera capture stalls a few frames after AI starts allocating there

**This is a bigger problem than Bug #2 and needs a real design decision,
not just a one-line fix - flagging clearly rather than guessing at a fix.**

**Symptom:** after the Bug #2 fix above, inference runs and completes
without faulting - genuine progress - but `s_frameCount`
(`camera_capture.c`) freezes after a small number of frames (observed:
stuck at exactly `3`, for 3+ seconds of continued execution, confirmed via
`pyocd commander` - halted the core, read `pc`/`lr`/`sp`, saw it sitting in
`CAMERA_CAPTURE_IsFrameReady()`'s poll loop in `main.c` (not stuck *inside*
inference, not faulted - just legitimately idle, waiting on a
`s_frameReady` flag that never gets set again). Confirmed via `read32` on
`s_frameCount`'s address, taken twice 3 seconds apart while the core was
resumed and running: identical value both times. Per README.md, the camera
+ SmartDMA path was previously confirmed to run continuously and
indefinitely on its own (frame #16, #46, ... in old logs) - so freezing
after 3 frames is new, and only showed up once AI inference started
actually completing multiple times in a row (every earlier session's
crash happened on/near the very first inference, before this had a chance
to surface).

**Root cause:** `SMARTDMA_CAMERA_MEM_ADDR` (defined in the MCUXpresso
SDK's own `drivers/smartdma/mcxn/fsl_smartdma_fw.h`) is `0x04000000` -
**the exact same physical address as `m_sramx`**, which is also where
`ei_sramx_alloc.c`'s `s_pool` (the AI tensor arena's primary pool) is
placed (confirmed via `nm`: `04000000 b s_pool`). `CAMERA_CAPTURE_InitSmartDma()`
(`camera_capture.c`) calls `SMARTDMA_InstallFirmware(SMARTDMA_CAMERA_MEM_ADDR,
...)`, loading the SmartDMA coprocessor's own camera-capture microcode
into that bank, and the coprocessor keeps using that memory as its
live working RAM for as long as it's capturing frames. This directly
contradicts this project's own working assumption (stated earlier in this
file and in the `ei_sramx_alloc.c` header comment) that `m_sramx` "exists
on this chip but isn't used by anything else" - **that's wrong**: it's not
used by anything in the *statically-linked* image (hence "not placed into
any output section by the SDK's own linker script"), but it *is* actively
used at runtime by the SmartDMA coprocessor, which explains why nothing in
the base linker script reserves it (SmartDMA firmware is installed
programmatically via `SMARTDMA_InstallFirmware()`, not through the linker).

Once the AI classifier starts bump-allocating and writing real data into
`s_pool` (tensor arena, working buffers, etc.), it's directly overwriting
the SmartDMA camera coprocessor's own firmware/state in the same physical
bytes. The coprocessor doesn't share the ARM core's fault mechanism - it
just silently stops producing frame-complete interrupts once its own
memory gets corrupted enough, which is consistent with the "frame count
just stops, no crash" symptom (the first few frames survive because the
tensor arena bump-allocator hasn't grown far/into the specific bytes
SmartDMA still needs yet).

### Next steps for a fresh session

This needs an actual architectural decision, not a quick patch - the AI
tensor arena and live SmartDMA camera capture **cannot coexist in
`m_sramx`** as currently structured. Options, roughly cheapest to most
invasive:

1. **Stop SmartDMA before running inference, restart it after**
   (`CAMERA_CAPTURE_Deinit()` + `CAMERA_CAPTURE_Reinit()`, both already
   exist in `camera_capture.c` and are already used for the USB-streaming
   build's time-multiplexing - see the `#else` branch in `main.c`). Turns
   this into a strictly time-multiplexed pipeline: capture one frame,
   deinit SmartDMA, run inference (safe to fill `m_sramx` now), reinit
   SmartDMA, wait for the next frame. Simplest fix, matches a pattern this
   codebase already has precedent for - but re-check `OV7670_Init()`/SCCB
   re-negotiation cost each `Reinit()` (may not be as cheap as just
   toggling an IRQ) and confirm the sensor doesn't need a settle delay
   after re-enabling before the first frame is trustworthy.
2. **Move the AI tensor arena somewhere else entirely** (not `m_sramx`) -
   but `m_data` is the only other RAM candidate and it's already
   constrained (59.56% used after the Bug #2 fix, but that's *without* a
   ~93KB arena in it - adding one back there would blow past capacity
   again, the exact problem `m_sramx` was originally adopted to solve).
   Not obviously viable unless something else in `m_data` shrinks a lot
   first.
3. **Only allocate/write to the parts of `m_sramx` SmartDMA isn't using** -
   would require knowing the SmartDMA camera firmware's actual size/layout
   precisely (not just its base address) and carving the AI pool to start
   after it - fragile (undocumented, could change with SDK updates) and
   not recommended over option 1.

**Recommended: option 1** - it directly matches this project's own
time-multiplexed USB-streaming precedent, and per the file-level comment
in `main.c`, capture and heavy compute already can't overlap on this chip
for unrelated reasons (SmartDMA + USB HS conflict) - suggesting
capture/inference mutual exclusion is already an accepted constraint here,
not a new one.

**FIXED (option 1 implemented, 2026-08-24):** `source/main.c`'s default
(non-USB) loop now calls `CAMERA_CAPTURE_Deinit()` right before
`AI_MODEL_RunInference()` and `CAMERA_CAPTURE_Reinit()` right after -
exactly mirroring the existing `DEMO_CaptureFramesAtMidVoltage()` /
USB-streaming pattern already in the same file. SmartDMA is fully torn
down (IRQ disabled, coprocessor deinited) before the AI arena touches
`m_sramx`, and only reinstalled/re-armed once inference has returned and
freed it again.

**Confirmed fixed on real hardware** - flashed, reset, monitored ~45s
continuously:
```
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=61%
Camera: frame #16 ready, 792 samples, pixel range 0x31A7..0xCE1A, avg=0x9AE2
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=53%
...
Camera: frame #46 ready, 792 samples, pixel range 0x31C7..0xCF1A, avg=0xA48B
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=61%
AI result: box[0] label=closed_eye x=24 y=40 w=8 h=8 score=72%
```
No HardFault, no frame-count stall - `frame #16`/`#46` confirm SmartDMA
keeps delivering frames indefinitely across many deinit/reinit cycles, and
`AI result` lines confirm the whole pipeline (capture -> DSP resize ->
CMSIS-NN convolution -> FOMO postprocessing -> box output) is now running
end-to-end repeatedly without crashing. **The AI integration goal stated
at the top of this file is met** - closed_eye is genuinely being detected
each cycle (consistent with the camera currently pointed at a closed/no
eye scene during this test - not yet validated against open_eye/yawning
scenes or bounding-box position/size accuracy, see "Next steps" below).

### Inference timing MEASURED (2026-08-24) - `ei_result.timing` is a dead end on this SDK build, use DWT instead

`ei_result.timing` (the SDK's own timing struct, `ei_impulse_result_t.timing`)
reads all-zero on this platform - traced to
`edge-impulse-sdk/porting/clib/ei_classifier_porting.cpp`'s
`ei_read_timer_us()`, which is hard-coded `return 0;` (and, unlike
`ei_malloc`/`ei_printf` in the same file, **not** marked `__attribute__((weak))`,
so it can't be overridden the usual way this project overrides other clib
porting stubs).

**Fix applied:** `source/ai/model_runner.cpp` now times the whole
`ei_run_classifier()` call itself using the Cortex-M33's DWT cycle counter
(`AI_MODEL_InitTiming()`, called once from `AI_MODEL_Init()`, enables
`DWT->CYCCNT` via `CoreDebug->DEMCR`/`DWT->CTRL`; `AI_MODEL_RunInference()`
samples `DWT->CYCCNT` before/after and converts to microseconds via
`SystemCoreClock`). Prints `AI_MODEL_RunInference: total classifier time =
<N>us (<N>ms)` after every inference.

**Measured on real hardware, 5 consecutive frames:** 1271927us, 1271390us,
1271188us, 1271286us, 1270365us - **consistently ~1.27 seconds per
inference**, +/-2ms across samples (i.e. essentially deterministic - makes
sense for a fixed-size int8 model with no early-exit/data-dependent
branching). Combined with the `CAMERA_CAPTURE_Deinit()`/`Reinit()`
round-trip added for Bug #3, full pipeline throughput is well under 1fps
right now (~0.78 inferences/sec, ignoring the deinit/reinit overhead
itself, which wasn't separately measured).

This is the non-EON TFLite Micro interpreter (`EI_CLASSIFIER_COMPILED=0`,
see the file's top for why EON was switched off) with CMSIS-NN
acceleration on a plain Cortex-M33 core - no surprise it's slow by
smart-camera standards. Worth revisiting EON and/or the Neutron NPU path
(see the trimmed "Next steps" list further up this file, item 4 - NPU
alternative) if sub-second/higher-fps response ever becomes a real
requirement; out of scope for just getting the pipeline correct, which is
what this session was about.

### Next steps for a fresh session

1. **Validate detection quality further**: this session's live testing
   (after the Bug #3 fix) has already shown varied, plausible-looking
   results across frames - `closed_eye`, `open_eye`, and `yawning` all
   appeared with different box positions/sizes and confidence scores
   50-97% as the camera view changed, not just one fixed-looking result
   like the very first post-fix test suggested. That's a good sign (rules
   out the "stuck on one static wrong answer" failure mode), but scores
   and box positions haven't been cross-checked against what's actually in
   front of the camera frame-by-frame - still worth a proper side-by-side
   check if detection accuracy matters for real use, not just "does it
   produce varied output".
2. ~~Check inference timing~~ - DONE, see above (~1.27s/inference,
   non-EON+CMSIS-NN on Cortex-M33 core).
3. **Bounding-box display is still disabled** ("Live image display
   disabled" section above) - now that inference is actually working
   end-to-end, revisiting that (if wanted) means re-budgeting RAM again:
   `m_data` is currently at 59.56% (`build.sh build` output), so there's
   real headroom now (unlike when that feature was disabled), but a 150KB
   live-image buffer would eat most of it back up - check current numbers
   before re-adding. Given ~1.27s/inference, live boxes would also update
   well under 1fps - may be worth confirming that's an acceptable UX
   before spending the RAM budget on it.

## Bug #4 (FOUND AND FIXED, 2026-08-24): LCD status-color fill only ever painted the first row - `LCD_PushPixels()` closes CS every call, `DEMO_ShowStatusColor()` called it in a loop

**Symptom (reported by user, on real hardware):** the LCD showed only a
single vertical stripe of the status color, with the rest of the screen
alternating black/white horizontal stripes (stale/garbage GRAM content).

**Root cause:** `LCD_SetWindow()` (both `lcd_bitbang.c` and
`lcd_flexio_mculcd.c`) asserts CS and issues the column/row/memory-write
(`0x2A`/`0x2B`/`0x2C`) commands, leaving the transfer open - by design,
so one `LCD_SetWindow()` + one `LCD_PushPixels()` pushes one block of
pixels and then `LCD_PushPixels()` itself closes CS at the end. But
`DEMO_ShowStatusColor()` (`main.c`) doesn't have a full 320x240 framebuffer
to push in one call (that's the whole point of it - filling the screen
with one small 320-pixel row buffer reused `DEMO_BUFFER_HEIGHT` times to
avoid a 150KB buffer), so it was calling `LCD_SetWindow()` **once** and
then `LCD_PushPixels()` **240 times** (once per row) - and every one of
those calls closes CS at the end. Only the first row's bytes actually
reach the panel while CS is still asserted; the other 239 calls send
their WR pulses/data with CS already deasserted, which the panel simply
ignores (GRAM position doesn't advance, pixel data isn't latched). With
this panel's MADCTL `MV=1` (row/column exchange, see `LCD_InitPanel()`),
one "row" of memory-write data physically renders as one vertical stripe,
matching the reported symptom exactly. The rest of the screen kept
whatever was already in GRAM from before (explains the black/white
stripe pattern - leftover panel-internal content, unrelated to anything
this firmware wrote).

**Fix applied:** added `LCD_PushPixelsOpen()` (same as `LCD_PushPixels()`
but does NOT close CS) and `LCD_EndWindow()` (closes CS) to both LCD
backends (`lcd_bitbang.c/.h` and `lcd_flexio_mculcd.c/.h`, same API on
both per the existing "same public API" convention - `lcd_display.h`
picks one at compile time). `LCD_PushPixels()` itself is now just
`LCD_PushPixelsOpen()` + `LCD_EndWindow()`, so existing single-call users
(`LCD_DrawImage()`) are unaffected. `DEMO_ShowStatusColor()` (`main.c`)
now calls `LCD_PushPixelsOpen()` in its per-row loop and a single
`LCD_EndWindow()` after the loop, keeping CS asserted for the whole
320x240 fill.

**Build/flash verified** (builds clean, flashes, boots without fault) -
**not yet visually re-confirmed on the physical panel** (no camera
access from this session to see the screen) - a fresh session or the
user should visually check the LCD now shows a solid full-screen color
that changes with the detected state, not just one stripe.

## OPEN PROBLEM (SUPERSEDED - see Bug #2 fix above): identical HardFault persists even after the alignment fix above

Flashed the `aligned(8)` fix and got **the exact same fault** as before it
- byte-for-byte identical register dump:
```
CFSR  = 0x00100000 (MMFSR=0x00 BFSR=0x00 UFSR=0x0010)   -> UsageFault, UNALIGNED
HFSR  = 0x40000000
MMFAR = 0xE000ED34  (not meaningful for UsageFault, see above)
BFAR  = 0xE000ED38  (not meaningful for UsageFault, see above)
Stacked r0=0x04000020 r1=0x0003A284 r2=0x0003A310 r3=0x0400C020
Stacked r12=0xFF414651 LR=0x0000E30B PC=0x00018A44 xPSR=0x69100200
```
(PC/LR resolve to `arm_nn_mat_mult_nt_t_s8`
(`edge-impulse-sdk/CMSIS/NN/Source/NNSupportFunctions/arm_nn_mat_mult_nt_t_s8.c:63`)
called from `arm_convolve_1x1_s8_fast`
(`.../ConvolutionFunctions/arm_convolve_1x1_s8_fast.c:133`), via
`arm-none-eabi-addr2line -e build/camera_ai_demo_cm33_core0.elf -f -C -i <addr>`.)

**This means the `aligned(8)` fix on `s_aiScratchPool` either wasn't the
real cause, or wasn't sufficient** (e.g. CMSIS-NN might need 16-byte
alignment for this specific fast-path kernel, not just 8 - untested).
Register values worth noting for whoever picks this up:
`r0=0x04000020` and `r3=0x0400C020` are both addresses *inside*
`s_pool`/`m_sramx` (`0x04000000`-`0x04018000` range) - `0x04000020` is
only 0x20 (32 bytes) past `s_pool`'s start, `0x0400C020` is 0x4000C020 -
0x04000000 = 0xC020 = 49184 bytes in. Neither is obviously misaligned to
8 (`0x20 % 8 = 0`, `0xC020 % 8 = 0`) or even 16 (`0x20 % 16 = 0`, but
`0xC020 % 16 = 0` too) - so if one of these two registers is the actual
faulting address, plain alignment doesn't explain it at first glance;
might be worth checking 4-byte vs. wider access width assumptions, or
whether the *access size* (e.g. a `LDRD`/64-bit load) combined with a
32-but-not-64-bit-aligned address is the actual trigger (8-aligned isn't
automatically 8-byte-*access*-safe on all instruction forms - some need
the access size itself, not just 8, as the alignment requirement, e.g. a
16-byte NEON-style load needing 16-byte alignment where 8 isn't enough).

### Next steps for a fresh session

1. **Try 16-byte alignment** on `s_aiScratchPool` (and maybe `s_pool` /
   `s_records` too, for consistency) instead of 8 - cheap to try, CMSIS-NN
   / TFLite tensor arenas are often documented as wanting 16-byte
   alignment, not just 8.
2. **Try disabling CMSIS-NN acceleration**
   (`EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN` - currently forced to `1` in
   `CMakeLists.txt`) to fall back to TFLite Micro's generic reference
   kernels instead of the CMSIS-NN fast-path (`arm_convolve_1x1_s8_fast`).
   Slower inference, but reference kernels are less likely to have strict
   alignment assumptions - useful to confirm/rule out CMSIS-NN alignment
   requirements specifically, even if not the final answer (much slower
   inference isn't great long-term, given `ei_result.timing` wasn't even
   checked yet this session).
3. **Check `arm_nn_mat_mult_nt_t_s8.c:63`** and
   `arm_convolve_1x1_s8_fast.c:133` directly (both under
   `source/ai/edge_impulse/edge-impulse-sdk/CMSIS/NN/Source/`) to see
   exactly which pointer is being dereferenced at the fault, and whether
   it's the tensor arena/activation buffer, a weights pointer (which would
   point into flash/`.rodata`, `m_text`, not `m_sramx`/`m_data` - a
   different fix entirely if so, since flash addresses aren't under this
   project's control the same way), or something else.
4. **NPU alternative** (raised mid-session, not pursued yet): this exact
   chip (MCXN947) genuinely has a Neutron NPU -
   `middleware/eiq/neutron/mcxn/libNeutronDriver.a`/`libNeutronFirmware.a`
   exist in the SDK, and there are real NXP example projects using it on
   this exact board (`examples/_boards/frdmmcxn947/eiq_examples/tflm_kws/npu/`,
   `.../mpp/tests/test_camera_mobilenet_view/`, `test_image_ultraface/`).
   This is a **substantial rewrite** - bypasses the Edge Impulse SDK's
   `run_classifier()` entirely, needs the model converted with NXP's own
   `neutron-converter` toolchain (not Edge Impulse Studio), and a new
   `model_runner.cpp` written against NXP's own Neutron driver API. Only
   worth it if the CMSIS-NN alignment issue above turns out to be a deep
   rabbit hole - try the cheaper alignment/CMSIS-NN-disable steps first.
5. If none of the above resolves it: the model itself (FOMO, 64x64,
   alpha 0.35, int8, Studio project "Test_Drowsy_NXP" v14/impulse #11) is
   otherwise confirmed sound - trained/tested in Studio with reasonable
   results (F1 ~63%, see prior session's Studio screenshots/history, not
   repeated here). Re-exporting from Studio isn't likely to help further
   unless specifically changing something that affects tensor
   alignment/layout (e.g. a different quantization or backbone) - the
   remaining issue looks firmware/integration-side, not model-side.
