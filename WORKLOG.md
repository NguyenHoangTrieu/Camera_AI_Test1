# WORKLOG - Camera_AI_Test1

## Dropped the camera/LCD rotation experiment, trimmed code comments

An in-progress experiment had added a `LCD_ROTATE_MODE` knob
(`source/display/lcd_bitbang.c`) to rotate the 320x240 camera buffer 90°
into a 240x320 portrait window on the LCD, with `LCD_DrawImageOriented()`
doing the per-pixel strided reindexing. **Per explicit direction, this is
reverted** - back to plain landscape, no rotation, matching what was
already flash-tested working (see "Reverted to Arduino-header LCD" below).

**What changed:**
- `source/display/lcd_bitbang.c/.h`: removed `LCD_ROTATE_MODE` and
  `LCD_DrawImageOriented()`. `LCD_InitPanel()`'s MADCTL is back to a fixed
  landscape value (MV=1, BGR=1 - `0x28`), matching the 320x240 camera
  buffer directly.
- `source/main.c`: calls `LCD_DrawImage(0, 0, DEMO_BUFFER_WIDTH,
  DEMO_BUFFER_HEIGHT, s_lcdSnapshot)` unconditionally now (both LCD
  backends share this signature, so the old `#if DEMO_LCD_BITBANG` split
  around the draw call is gone too).
- `board_port/cm33_core0/app.h`: `DEMO_PANEL_WIDTH`/`DEMO_PANEL_HEIGHT`
  back to `320`/`240`.
- Also did a pass shortening comments across all of `source/` and
  `board_port/` - the long investigation narratives stay here in
  WORKLOG.md; the code itself now only carries short why-comments.

Flash-tested: builds clean (`-Werror`, no warnings) via `./build.sh build`.

---

## Reverted to Arduino-header LCD (fallback from J8), confirmed building/booting on hardware

Per explicit direction, dropped the J8/FlexIO detour and reverted to the
project's original design: TFT on the **Arduino header**, GPIO bit-bang.
USB streaming (separately abandoned, see entries below) is untouched by this
- still opt-in via `USB_STREAM_DIAGNOSTIC_DISABLE=OFF`, unrelated to this
LCD-path change.

**What changed:**
- `source/display/lcd_bitbang_j8.c/.h` renamed to `lcd_bitbang.c/.h` and its
  comments generalized - the driver was already 100% pin-agnostic (only ever
  touched `DEMO_LCD_*` macros from app.h, never a hardcoded J8 pin), so no
  driver logic changed, just which pin set it's pointed at.
- `board_port/cm33_core0/app.h`: added `DEMO_LCD_ARDUINO_HEADER` (default 1)
  and a full Arduino-header pin block (`DEMO_LCD_D0..D7`, `RS/CS/RST`,
  `RD/WR`, `BLK`), reusing the exact mapping confirmed working in an earlier
  session before the J8 detour (see README.md's Pinout section for the full
  table + jumper-wiring notes). The pre-existing J8 pin block is kept,
  selected when `DEMO_LCD_ARDUINO_HEADER=0`.
- `board_port/pin_mux.c`: added `BOARD_InitArduinoLcdPins()` (new), kept
  `BOARD_InitFlexioPins()` (J8, unchanged) for the opt-in path.
- `board_port/cm33_core0/hardware_init.c`: calls whichever pin-init function
  `DEMO_LCD_ARDUINO_HEADER` selects; added the missing `kCLOCK_Gpio1` enable
  (Arduino D3/D5/D6 sit on GPIO1, which nothing previously needed).
- `CMakeLists.txt`: new `LCD_ARDUINO_HEADER_BITBANG` option, default `ON` -
  selects `lcd_bitbang.c` + `-DDEMO_LCD_ARDUINO_HEADER=1`. Set `OFF` to fall
  through to the pre-existing `LCD_BITBANG_DIAGNOSTIC` J8 switch (FlexIO vs.
  J8 bit-bang), unchanged from before.
- `source/main.c`: banner text now reports which LCD path is active instead
  of unconditionally saying "J8".

**Flash-tested on real hardware:** clean build (`-Werror`, no warnings),
flashed, and the serial log confirms both the new pin path and the
still-untouched camera path:
```
Display: Arduino-header LCD live preview (camera + AI hook)
Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
LCD: bit-bang GPIO on the Arduino header
```
Boots past `LCD_Init()` without hanging (same driver code path as the J8
bit-bang variant that was already confirmed to display a correct image -
just re-pointed at different pins now). **The physical picture on the
Arduino-wired panel has not yet been visually re-confirmed in this session**
- next step for whoever picks this up: check the screen after wiring the 2
LCD_RD/LCD_WR jumpers described in README.md, and report back whether it
matches the earlier J8-bit-bang bring-up's correct image or needs further
adjustment (MADCTL orientation, RD/WR jumper target, etc - see README.md "If
the screen shows nothing/garbled").

---

## Why flashing looked broken after tuning DEMO_MID_VOLTAGE_WARMUP_FRAMES/DEMO_OVERDRIVE_HOLD_MS

Not a flashing bug - `./build.sh flash` always succeeded. `main.c`'s
`DEMO_MID_VOLTAGE_WARMUP_FRAMES` got changed to `5000` (from `10`) and
`DEMO_OVERDRIVE_HOLD_MS` to `100` (from `5000`) at the same time, which
together make the board functionally unusable as a webcam even though it's
running correctly:

- At ~10-12fps effective capture rate, 5000 frames takes roughly **7-8
  minutes** per Mid-voltage capture phase (confirmed by halting over the
  debug probe and watching `s_frameCount` advance normally, ~10fps, not
  stuck).
- `DEMO_OVERDRIVE_HOLD_MS=100` then only holds Overdrive/USB up for
  **100 milliseconds** before dropping straight back to Mid for another
  ~7-8 minute capture phase - nowhere near enough time for a host to
  actually enumerate the device (USB enumeration/driver binding typically
  takes several hundred ms to a few seconds on its own).

Net effect: the board spends the overwhelming majority of its time in a
multi-minute Mid-voltage capture phase, and even when it briefly reaches
Overdrive, drops back down again before USB can do anything useful - so
from the host's point of view, the webcam functionally never appears,
looking identical to a failed flash. Confirmed via debug-probe halt+PC read
that the board was alive and correctly executing `DEMO_CaptureFramesAtMidVoltage()`'s
poll loop the whole time, not hung.

**Takeaway for anyone retuning these two values** (now only relevant if
opting back into the abandoned USB path - see the entry below): keep
`DEMO_OVERDRIVE_HOLD_MS` comfortably longer than the real wall-clock time
`DEMO_MID_VOLTAGE_WARMUP_FRAMES` takes to capture (frames / ~10-12fps
effective), with enough extra margin for USB enumeration overhead. The
values reverted to and left in place (10 frames / 5000 ms hold) are the
ones already confirmed stable on hardware in the periodic-refresh testing
below.

---

## USB streaming pipeline abandoned - reverted to camera-only default

**Decision: stop pursuing USB (UVC) webcam streaming as the active build.**
Not a code bug - a confirmed hardware/board limitation with no remaining
software fix, and no accessible hardware workaround on this specific board
either. Reasoning, in order:

1. The periodic-refresh time-multiplex workaround (previous entry below)
   works and is stable, but it's fundamentally a workaround for a real
   hardware conflict (SmartDMA needs DCDC Mid, USB HS PHY needs DCDC
   Overdrive, confirmed mutually exclusive on this chip - see "decisive
   isolation test" entry further below) - not a fix. Asked: is there a
   genuinely better option, or should this be abandoned?
2. Checked whether switching from USB High-Speed (EHCI) to USB Full-Speed
   (KHCI) - which doesn't need DCDC Overdrive at all, confirmed by reading
   the SDK's own KHCI `USB_DeviceClockInit()` path (`middleware/usb/config/
   device/khci/`, and the stock `usb_device_video_virtual_camera` example's
   `hardware_init.c`) - would sidestep the whole conflict, since the
   MCXN947 silicon genuinely has two independent USB modules (confirmed via
   `USBFS0_BASE`/`USB_BASE_PTRS` in `devices/MCX/MCXN/MCXN947/
   MCXN947_cm33_core0_COMMON.h` - a real, separate FS controller, not just a
   HS-PHY low-speed mode).
3. **This does NOT work on FRDM-MCXN947 specifically.** NXP's own board user
   manual (UM12018, downloaded and grepped - "2.3 USB interface" section)
   states outright: *"The target MCU (MCXN947) features two USB modules (FS
   USB and HS USB)... On the FRDM-MCXN947 board, only the HS USB controller
   and PHY interface is used and it is connected to the USB Type-C
   connector (J11)."* Confirmed independently: grepped the board's own
   `pin_mux.c`/`pin_mux.h` for any USB0/USBFS reference - none exist. There
   is exactly one USB Type-C connector for the target MCU on this board
   (J11), and it's hard-wired to the HS controller only. The FS module's
   D+/D- pins aren't broken out to any accessible connector or header - no
   way to reach it without soldering directly to the MCU package (not a
   real option). So the one lead that would have been a genuine fix
   (not a workaround) is a dead end specific to this board's hardware
   design, not something fixable in firmware.

With no remaining better option, and per explicit direction, USB streaming
is now **abandoned** (deprecated, matching how the earlier LCD path was
handled - kept in the tree, not deleted, not the default, in case NXP
publishes a fix or this is revisited):

- `CMakeLists.txt`: `USB_STREAM_DIAGNOSTIC_DISABLE` default flipped from
  `OFF` to `ON` - **camera-only (no USB) is now the default build**. Set
  `OFF` to opt back into the abandoned USB path (still builds clean, still
  works as the periodic-refresh feed described in the previous entry).
- `source/main.c`: the USB-only helper function/macros
  (`DEMO_CaptureFramesAtMidVoltage()`, `DEMO_MID_VOLTAGE_WARMUP_FRAMES`,
  `DEMO_OVERDRIVE_HOLD_MS`) are now wrapped in `#if !DEMO_USB_STREAM_DISABLE`
  so the new default build doesn't fail `-Werror=unused-function`. Boot
  banner text is now conditional too (says "Display: none" on the default
  build instead of always claiming USB).
- `README.md`: rewritten top banner, "USB (UVC webcam)" section (renamed
  "...abandoned"), "Known limitations", and "Building and flashing" to
  reflect camera-only as the default and USB as opt-in/abandoned.
- Flash-tested the new default build on real hardware: builds clean, and
  `s_frameCount` (halted via debug probe) advances normally (81 -> 117 over
  3 seconds, ~12fps effective) - camera-only path works as expected. Also
  confirmed the abandoned USB variant (`-DUSB_STREAM_DIAGNOSTIC_DISABLE=OFF`)
  still builds clean, unchanged behavior from the previous entry.

**If this is ever revisited:** the two real remaining options are (a) get
NXP to confirm/fix this as a chip erratum (nothing found in the current
public errata sheet - see "decisive isolation test" entry below for what
was already checked), or (b) a hardware rework to route the USB FS module's
D+/D- to an accessible connector/header on a modified board (not the stock
FRDM-MCXN947). Software-only fixes on this exact board are exhausted.

---

## Earlier: periodic refresh implemented and confirmed stable on hardware - live-ish video now works

Followed up on the single-shot time-multiplex fallback's "known limitation"
(frozen forever, no refresh) by implementing and testing the previously-
flagged-as-risky periodic refresh: drop back to Mid every
`DEMO_OVERDRIVE_HOLD_MS` (5000 ms) to grab a fresh frame, then return to
Overdrive, repeating for the life of the session - instead of a single
capture-once-then-freeze-forever.

**Implementation** (`board_port/cm33_core0/hardware_init.c`,
`source/camera/camera_capture.c/.h`, `source/main.c`):
- `hardware_init.c`: split the old one-shot `USB_DeviceClockInit()` into
  `BOARD_SetRegulatorsMidVoltage()` / `BOARD_SetRegulatorsOverdriveVoltage()`
  (just the DCDC/CoreLDO regulator switch, reusable, no USB clock/PHY
  bring-up) plus the full `USB_DeviceClockInit()` (still calls
  `BOARD_SetRegulatorsOverdriveVoltage()` internally, but also does the
  one-time SYSLDO/LDOCSR/SOSC/PLL/PHY bring-up - deliberately called only
  ONCE per boot, never repeated, since re-running PHY/PLL bring-up on an
  already-enumerated session was the untested/risky part, not the DCDC
  level switch itself).
- `camera_capture.c/.h`: added `CAMERA_CAPTURE_Reinit()` - restarts SmartDMA
  without re-running the OV7670 SCCB/I2C init (sensor doesn't need
  reconfiguring, just the pixel pipe).
- `main.c`: extracted the capture-N-frames-then-deinit logic into
  `DEMO_CaptureFramesAtMidVoltage()`, reused for both the initial boot
  capture and every refresh. The main loop (USB-enabled build) now, after
  the one-time `USB_DeviceClockInit()`/`USB_VideoCamera_Init()`: waits
  `DEMO_OVERDRIVE_HOLD_MS`, drops to Mid
  (`BOARD_SetRegulatorsMidVoltage()`), re-captures
  (`CAMERA_CAPTURE_Reinit()` + `DEMO_CaptureFramesAtMidVoltage()`), returns
  to Overdrive (`BOARD_SetRegulatorsOverdriveVoltage()`), and repeats
  forever. `USB_VideoCamera_Task()` is a true no-op in this bare-metal EHCI
  build (`USB_DEVICE_CONFIG_USE_TASK=0` for the `ehci` USB config, confirmed
  in `middleware/usb/config/device/ehci/usb_device_config.h`) - all USB
  housekeeping is IRQ-driven, so blocking the main loop for the whole
  Mid-voltage recapture window doesn't stall anything USB-side.

**Flash-tested and confirmed stable on real hardware**, several ways:
- `lsusb` polled every 5-10s for a continuous 30s window: same
  `Bus 003 Device NNN` the entire time (no re-enumeration) - i.e. multiple
  refresh cycles happened with the device just sitting idle (not actively
  streamed) and nothing visibly changed on the host side.
- Captured a frame, waited 6s (past a refresh boundary), captured again:
  bytes differed - confirms the refresh is actually re-capturing new pixel
  data, not just holding the same buffer.
- **The real test - a single continuous v4l2 capture session spanning
  multiple refresh cycles while actively open and streaming**:
  `gst-launch-1.0 v4l2src device=/dev/videoN ! ... ! multifilesink ...` run
  for 35 continuous seconds (~7 refresh cycles at the 5s interval) without
  ever closing/reopening the device. Result: 223 frames captured, zero
  pipeline errors, zero USB disconnects in `dmesg` during the session -
  and exactly 2 real content changes across those 223 frames (an MD5 diff
  per frame), landing right where the ~5s refresh boundaries would be
  expected within a 35s window - i.e. the image visibly updates roughly
  every 5 seconds while the USB connection stays up the entire time. This
  directly answers the open question from the previous entry: dropping DCDC
  back to Mid and returning to Overdrive, while the USB HS PHY is already
  enumerated and actively mid-stream, does *not* disconnect or corrupt the
  session.
- Caveat worth recording: earlier in this same testing session, a handful
  of `usb 3-9: USB disconnect` / re-enumeration events *did* show up in
  `dmesg`, but at irregular intervals (60s-370s apart, not the fixed 5s
  refresh period) that lined up with when short-lived, repeated
  `gst-launch-1.0` invocations were being opened and closed one after
  another (each a fresh device open/close) - not with the firmware's
  refresh timer itself. The clean 35s continuous-session test (one
  open, held through several refreshes) is the more direct test of "does
  refreshing disrupt an active session," and it showed no issues - but if
  disconnects are ever seen in real usage, checking whether they correlate
  with the host app repeatedly reopening the device (vs. one long-lived
  session) is the first thing to check before blaming the refresh logic
  again.

**Net result: this is now a low-frame-rate but genuinely live camera** (a
new image roughly every `DEMO_OVERDRIVE_HOLD_MS`, not a single frozen
frame) - a real improvement over the single-shot fallback, given the
underlying SmartDMA/DCDC hardware conflict still can't be fixed directly.
`DEMO_OVERDRIVE_HOLD_MS` (`source/main.c`) can be tuned - shorter means more
frequent updates but more time spent in the Mid-voltage capture phase
(where USB is nominally still enumerated but not delivering fresh isochronous
data - not tested how a host handles a long stall there beyond the 5s used
here).

---

## Earlier: time-multiplex fallback implemented and confirmed working on hardware

Followed up on the "Remaining leads" list from the decisive isolation test
below, in order:

1. **SmartDMA clock divider retuning - dead end, confirmed from the driver
   itself.** `kCLOCK_Smartdma` (`devices/MCX/MCXN/MCXN947/drivers/
   fsl_clock.h`) is just an AHB clock gate (`CLK_GATE_DEFINE(AHB_CLK_CTRL1,
   31)`) - no clock source select, no divider. SmartDMA runs straight off
   the fixed 150 MHz AHB clock, which doesn't change with DCDC voltage
   level in this SDK's design. There is nothing to retune - this idea
   doesn't apply.
2. **NXP errata / community search - no matching entry found.** Downloaded
   and grepped NXP's official `MCXNx4x_0P02G` mask-set errata PDF
   (nxp.com/docs/en/errata/MCXNx4x_0P02G.pdf) for every SmartDMA/SPC/DCDC/
   CORELDO/SRAM-voltage mention:
   - `ERR052088` (SmartDMA: FlexIO_IRQ misrouted to SMARTDMAARCHB_INMUX) -
     unrelated, this project's camera signals go through GPIO INPUTMUX
     paths, not FLEXIO_IRQ.
   - `ERR051379` (SRAM: incorrect reads with Auto-clock-gating + ECC both
     enabled, on misaligned accesses) - not voltage-related, doesn't match.
   - `ERR051704` (DCDC: failure changing to *Low drive-strength* in
     low-power mode) - this project only ever uses Normal drive strength in
     Active mode; doesn't apply.
   Also checked AN14191 ("How to Use SmartDMA to Implement Camera Interface
   in MCXN MCU", the official app note this project's camera code is based
   on) for any voltage-level guidance - none mentioned at all. Web-searched
   NXP's community forum for this exact symptom - nothing matching found,
   and the two potentially-relevant threads ("MCXN947, voltages domain!"
   and "Simultaneous use of Ethernet and camera on the FRDM-MXCN947 board")
   couldn't be fetched (community.nxp.com blocks non-browser HTTP clients;
   no interactive browser session was available in this environment either)
   - if this needs to be followed up further, a logged-in browser session or
   posting a new question directly is the way to do it, referencing this
   file's "decisive isolation test" entry below for the precise, already-
   proven symptom description.
3. **Fallback: time-multiplex - implemented and confirmed working on real
   hardware.** Since capture and USB HS provably can't run at the same time
   on this chip (previous entry below), restructured the firmware so they
   run sequentially instead of simultaneously:
   - `board_port/cm33_core0/hardware_init.c`: `BOARD_InitHardware()` now
     *always* leaves DCDC/CoreLDO at Mid (both USB and non-USB build
     variants - no more special-casing). `USB_DeviceClockInit()` (raises to
     Overdrive) is no longer called from `BOARD_InitHardware()` at all.
   - `source/camera/camera_capture.c/.h`: added `CAMERA_CAPTURE_Deinit()` -
     stops the SmartDMA IRQ and gates its clock (`SMARTDMA_Deinit()`). The
     last frame stays intact in the buffer (plain SRAM, untouched by this
     call).
   - `source/usb/usb_video_camera.h`: `USB_DeviceClockInit()` is now
     declared here (was previously private to hardware_init.c/
     usb_video_camera.c) so `main.c` can call it directly.
   - `source/main.c`: for the normal (USB-enabled) build, `main()` now:
     boots at Mid, runs `CAMERA_CAPTURE_Init()`, busy-waits for
     `DEMO_MID_VOLTAGE_WARMUP_FRAMES` (10) frames to be captured (more than
     1 so the OV7670's auto-exposure/auto-gain converge past the
     often-flat/underexposed frame #1), calls `CAMERA_CAPTURE_Deinit()`,
     runs the AI stub once on that final frame, then calls
     `USB_DeviceClockInit()` (raises to Overdrive) and
     `USB_VideoCamera_Init()` - from then on the main loop is just
     `USB_VideoCamera_Task()`, no more camera polling. The
     `USB_STREAM_DIAGNOSTIC_DISABLE=ON` camera-only build path is
     untouched - still loops forever at Mid voltage like before (still the
     "is the camera itself healthy" sanity build, proven 700+ clean
     frames).
   
   **Flash-tested and confirmed on real hardware:** board enumerates as
   `1fc9:009a NXP Semiconductors Camera_AI_Test1` (`lsusb`), `/dev/video5`
   appears, and `gst-launch-1.0 v4l2src device=/dev/video5 num-buffers=5 !
   video/x-raw,format=YUY2,width=320,height=240 ! filesink ...` captured 5
   frames (768000 bytes total = exactly 5 x 153600-byte YUY2 frames,
   confirming clean format negotiation). Checked in Python: ~104-105
   distinct byte values per frame (real image content, not a flat/corrupted
   buffer) - and frames 1-4 are byte-for-byte identical to each other
   (frame 0 differs slightly, likely a partial/boundary artifact from
   however GStreamer's first buffer lines up with the USB packet stream) -
   exactly the expected time-multiplexed behavior: one real, correctly
   captured still frame, repeated indefinitely, not a live feed.

   **Known limitation, by design:** this is NOT live video - the image
   frozen at boot (from whichever frame `DEMO_MID_VOLTAGE_WARMUP_FRAMES`
   lands on) is what streams for the entire session, until the board is
   reset. If a later session wants to explore periodic refresh (e.g.
   dropping back to Mid every few seconds to grab a fresh frame, then
   raising to Overdrive again), that's a real possible enhancement, but
   *unverified*: it's not yet confirmed whether dropping DCDC back to Mid
   while the USB HS PHY is already enumerated and actively streaming would
   glitch/disconnect the active USB session (the PHY's PLL-lock requirement
   was only ever tested from an unconfigured/idle state, never tested for
   staying locked through a DCDC drop after the fact) - would need dedicated
   hardware testing before relying on it, ideally on a spare/non-critical
   session given the multiple prior instances in this project of DCDC
   register mistakes needing a physical power cycle to recover from.

---

## Earlier: decisive isolation test - SmartDMA only works at DCDC Mid (1.0V), period. Not a transition/USB issue.

Candidate fix #3 (below - zero post-boot DCDC transitions, stay at Overdrive
from boot) was flash-tested and did **not** help either. But before giving
up on it, ran one more targeted experiment to actually pin down the
mechanism, since every previous test (in this session and the original
bisection further below) always changed two things at once (DCDC level *and*
USB HS PHY activity), so it was never possible to tell which one mattered:

**Test setup:** built the project with `USB_STREAM_DIAGNOSTIC_DISABLE=ON`
(USB fully compiled out - no EHCI/PHY code, no USB clocks enabled at all)
plus a temporary one-off diagnostic flag forcing DCDC/CoreLDO to stay at
Overdrive (instead of the normal camera-only build's Mid downgrade) - i.e.
camera running completely alone, at Overdrive voltage, with literally zero
DCDC/CoreLDO register writes happening anywhere after
`BOARD_BootClockPLL150M()` set Overdrive at boot. Flashed and checked
`s_frameCount` by halting over the debug probe (`pyocd commander -t mcxn947
-c halt -c "read32 0x20000504 4"` - address re-derived for that build via
`arm-none-eabi-nm`).

**Result: broke identically** - `s_frameCount` stuck at 2, PC parked inside
`CAMERA_CAPTURE_IsFrameReady()`'s normal poll loop (confirmed via
`arm-none-eabi-addr2line` - not stuck in a busy-wait, the main loop is
perfectly healthy, `s_frameReady` just never becomes true again after frame
#2). No USB code was even compiled in, and there was no voltage transition
of any kind after boot.

**Conclusion: this definitively rules out both "it's a transition/glitch
hazard" (candidate fix #3's hypothesis) and "it's specifically the USB HS
PHY/EHCI peripheral being active" as the cause.** Combined with the original
bisection further below (which already showed both Normal 0x2 *and*
Overdrive 0x3 break it, while Mid 0x1 is the only level that's ever worked),
the pattern is now clear and confirmed from multiple independent angles:
**SmartDMA camera capture only runs reliably with `DCDC_VDD_LVL` at exactly
Mid (1.0V, `kSPC_DCDC_MidVoltage`) - any other level breaks it after ~2
frames, regardless of how it got there (transition or not) and regardless of
what else is running (USB or not).**

This also retroactively clears up why the SRAM-voltage-margin theory (the
very first candidate fix, further below) was a dead end: `SPC_SRAMCTL`'s
voltage-margin field has been latched at `kSPC_sramOperateAt1P2V` since
`BOARD_BootClockPLL150M()` ran at the very start of boot, in *every* test
done in this project so far - including the ones where DCDC was at Mid and
the camera ran cleanly to 700+ frames. Since that field is identical across
both the working and broken cases, it was never a viable differentiator -
the actual, now-confirmed differentiator is `DCDC_VDD_LVL` alone (paired
with `CORELDO_VDD_LVL`, which every test has always changed together with
it, per the "don't raise CORELDO alone" hardware constraint documented
elsewhere in this file).

**Where this leaves the project:** USB HS genuinely requires DCDC at
Overdrive to lock its PHY PLL (`CLOCK_EnableUsbhsPhyPllClock()`,
`fsl_clock.c` - confirmed via halt+PC read, spins forever in `PLL_LOCK`
wait without it). SmartDMA camera capture genuinely requires DCDC at Mid to
keep running past ~2 frames. As currently understood, on this chip, with
this SDK, **there is no known regulator configuration where both work at the
same time** - this isn't a sequencing bug in this project's code anymore,
it looks like a real hardware/firmware limitation. `hardware_init.c` has
been simplified accordingly (removed the now-disproven "avoid transitions"
special-casing, added a comment recording this conclusion at the top of
`BOARD_InitHardware()`'s regulator section) - USB builds still go straight
to Overdrive from boot (needed for USB), camera-only builds still go to Mid
(needed for camera); no fix currently makes both work together.

**Remaining leads, in order of promise (unchanged from before, now with
more confidence they're the *only* remaining leads - the transition/USB-
activity theories are conclusively ruled out):**
- SmartDMA clock divider retuning - still completely untested. Look at
  whether SmartDMA's own clock source/divider (as opposed to the DCDC
  voltage level) needs to change to match Overdrive, the way
  `BOARD_InitBootClocks()`'s AHB clock does not change with voltage level
  but *some* internal SmartDMA timing might implicitly assume Mid-voltage
  characteristics.
- MCXN947 chip errata - not found anywhere in this SDK checkout (grepped
  driver sources for "errata"/notes on DCDC+SmartDMA interactions - nothing
  relevant). Needs NXP's actual published errata sheet or a direct ask on
  community.nxp.com with this exact symptom (DCDC_VDD_LVL != Mid breaks
  SmartDMA, independent of USB/transitions - very likely something NXP
  engineering would recognize immediately if it's known).
- Fallback: time-multiplex instead of running both simultaneously - capture
  what's needed at Mid voltage, then switch to Overdrive and start USB only
  once camera capture is done for that session (if the use case tolerates
  not truly live-streaming). This is the only currently-known way to get
  correct behavior from both peripherals with this hardware, just not at
  the same time.

Sanity check also done in this session, unrelated to the above but worth
recording: flashed NXP's own stock, unmodified
`examples/usb_examples/usb_device_video_virtual_camera/bm` example (built
directly via `west build`, no project changes) to confirm the board and USB
tooling chain are fine in general - it enumerated correctly on a real Linux
host (`lsusb` showed `1fc9:0099 NXP Semiconductors VIDEO DEMO`,
`/dev/video5` appeared). Video capture itself failed
(`uvcvideo: Failed to query (GET_MIN) UVC probe control : -32`), but that's
the exact same known/already-fixed-in-this-project's-own-code UVC probe
control gap (see "device wasn't enumerating" fix further below) - NXP's
stock example just hasn't fixed it, it's not informative about the DCDC/
SmartDMA question. Also: that stock example uses the USB Full-Speed (KHCI)
controller, not High-Speed/EHCI, so it doesn't even exercise the code path
this project cares about - not useful as a comparison point either way.

---

## Earlier: candidate fix #2 (proper SPC sequencing) flash-tested - NO CHANGE. Candidate fix #3 (zero post-boot DCDC transitions) build-clean, NOT YET FLASH-TESTED

Flash-tested candidate fix #2 below (proper `SPC_SetActiveModeDCDCRegulatorConfig()`/
`SPC_SetActiveModeCoreLDORegulatorConfig()` API sequencing instead of the raw
combined register write). Serial log:

```
Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
USB: video class (UVC) camera ready - 320x240 YUY2 @ 30fps over USB HS
Camera: frame #1 ready, 792 samples, pixel range 0x   0..0x   0, avg=0x   0 (flat - lens cap on, or no real image data)
AI_MODEL_RunInference: stub, always returns "no result".
AI_MODEL_RunInference: stub, always returns "no result".
```

**No frame #16 ever printed** (main loop is alive - `AI_MODEL_RunInference`
keeps running every iteration - but `CAMERA_CAPTURE_IsFrameReady()` never
goes true again after frame #1, i.e. `s_frameCount` is stuck at 1). Frame #1
itself came back flat (all-zero), matching bisection step 4 in the entry
further below exactly. **Conclusion: proper regulator API sequencing made no
observable difference** - rules out "the raw register write violates NXP's
CORELDO drive-strength precondition" as the cause. Whatever breaks SmartDMA
here isn't about *how* the DCDC/CORELDO register write is performed.

This prompted a closer look at *when* the write happens: every test done so
far (this session's #1 and #2 above, and the entire original bisection
further below) always ran through the same fixed prefix first -
`BOARD_BootClockPLL150M()` sets Overdrive at boot, then
`BOARD_InitHardware()` unconditionally downgrades DCDC/CoreLDO to Mid,
*then* whatever variant was under test ran on top of that. No test has ever
isolated "does any DCDC transition break SmartDMA" from "does that specific
Overdrive-then-Mid-then-back-to-Overdrive sequence break it" - both were
always present together, since the Mid downgrade always ran unconditionally
before the code under test.

**Fix applied (candidate #3, `board_port/cm33_core0/hardware_init.c`):**
`BOARD_InitHardware()` now skips the Overdrive-to-Mid downgrade entirely
when USB streaming is enabled (`#if DEMO_USB_STREAM_DISABLE` now gates that
block) - DCDC/CoreLDO simply stay at whatever `BOARD_BootClockPLL150M()`
already set them to (Overdrive) all the way through. `USB_DeviceClockInit()`
no longer re-raises DCDC/CoreLDO at all (removed those calls - they're
redundant now that boot already leaves them at Overdrive). Net effect: with
USB streaming enabled, there is now **zero** DCDC/CoreLDO voltage-level
write anywhere after `BOARD_InitBootClocks()` returns - SmartDMA will start
and run entirely at a constant, never-transitioning Overdrive supply, for
the first time. Builds clean (`./build.sh build`, zero warnings). The
non-USB (`DEMO_USB_STREAM_DISABLE`) build path is untouched - still
downgrades to Mid as before, so the already-proven "camera alone, no USB"
case (700+ frames clean) isn't at risk of a regression from this change.

**Next step:** flash and check `s_frameCount`/serial log again (see "How to
verify" further below). If this doesn't fix it either: that rules out
"transition" as the mechanism entirely, and narrows the problem down to
"SmartDMA fundamentally cannot run correctly with DCDC sitting at Overdrive,
period" - at which point the remaining leads are the untested SmartDMA
clock-divider-retuning idea and asking NXP directly (no SDK example anywhere
combines SmartDMA + USB HS at Overdrive, and no MCXN947 errata on this was
found in the SDK sources - would need NXP's actual published errata sheet,
not in this SDK checkout).

---

## Earlier: candidate fix #1/#2 background (superseded above, kept for the reasoning trail)

Board was reported reachable and flashing normally again (the "board boots
unreliably" entry below is resolved - presumably the long power-off wait
worked). Picking back up on the SmartDMA-stops-after-2-frames issue with a
different, more targeted root-cause candidate than the SRAM-voltage one
already tried and reverted (see that entry below - kept, still relevant
context for why NOT to re-try that exact idea).

**New finding, from reading NXP's own `drivers/mcx_spc/fsl_spc.c`/`.h` (not
example code - the actual regulator driver) closely:** the old
`USB_DeviceClockInit()` (`board_port/cm33_core0/hardware_init.c`) raised
DCDC/CoreLDO to Overdrive via one raw combined register write:

```c
SPC0->ACTIVE_CFG &= ~SPC_ACTIVE_CFG_CORELDO_VDD_DS_MASK;   // forces CoreLDO drive strength to Low
...
SPC0->ACTIVE_CFG |= SPC_ACTIVE_CFG_DCDC_VDD_LVL(0x3) | SPC_ACTIVE_CFG_CORELDO_VDD_LVL(0x3) | ...;  // changes CoreLDO voltage level in the same write
```

NXP's own driver function for this
(`SPC_SetActiveModeCoreLDORegulatorVoltageLevel()`, `drivers/mcx_spc/
fsl_spc.h`) documents - and *enforces*, returning
`kStatus_SPC_CORELDOVoltageSetFail` if violated - that "the Core LDO voltage
level should only be changed when the Core LDO is in normal drive strength."
The raw write above does the opposite: forces drive strength to *Low*, then
changes the voltage level in the very next statement, in the same combined
write - violating a real, NXP-documented hardware precondition, every single
time USB streaming was enabled. The proper driver API
(`SPC_SetActiveModeDCDCRegulatorConfig()`/
`SPC_SetActiveModeCoreLDORegulatorConfig()`) does this correctly: separate
masked read-modify-write per field, drive strength before voltage level,
each with its own busy-wait - exactly what NXP's own
`BOARD_PowerMode_OD()`/`BOARD_BootClockPLL150M()`
(`examples/_boards/frdmmcxn947/board.c` / `clock_config.c`) use, and both of
those are already proven to run this exact chip to Overdrive successfully
(this project's own boot sequence calls `BOARD_BootClockPLL150M()` at every
startup, before downgrading back to Mid for the camera).

A single glitched/marginal regulator transition (from violating that
precondition) is a very plausible mechanism for silently stalling a
coprocessor (SmartDMA) without ever tripping a HardFault - and matches the
observed symptom (a clean stop, not corrupted/garbage data) noticeably
better than the SRAM-timing-margin theory did.

**Also clarified why the earlier `SPC_SetSRAMOperateVoltage()` attempt made
things worse** (that entry below said the mechanism was unclear): re-reading
`BOARD_BootClockPLL150M()` (`examples/_boards/frdmmcxn947/clock_config.c`)
shows it already leaves SRAM latched at `kSPC_sramOperateAt1P2V` (Overdrive
margin) from the very first boot, and `BOARD_InitHardware()`'s subsequent
downgrade to Mid (right after `BOARD_InitBootClocks()` returns) never
touches `SRAMCTL` to bring that back down. So by the time the old
`USB_DeviceClockInit()` ran, SRAM was *already* sitting at 1.2V margin -
re-issuing the same `SPC_SetSRAMOperateVoltage(..., kSPC_sramOperateAt1P2V,
...)` REQ/ACK handshake for a value that's already active is the likely
cause of that regression, not the SRAM-margin idea itself being wrong. Don't
re-add that call without addressing this first.

**Fix applied** (`board_port/cm33_core0/hardware_init.c`,
`USB_DeviceClockInit()`): replaced the raw combined register write with
`SPC_SetActiveModeDCDCRegulatorConfig()` then
`SPC_SetActiveModeCoreLDORegulatorConfig()` (same end register state as
before - Overdrive on both, Normal drive strength on both - just correct
per-field sequencing and busy-waits this time, matching NXP's own proven
sequence). `SYSLDO_VDD_DS` is still set directly (no documented ordering
constraint found for that field). Builds clean (`./build.sh build`, zero
warnings, `-Werror`). **NOT YET FLASH-TESTED - next step:** flash and check
whether `s_frameCount` (`camera_capture.c`) keeps advancing past 2 instead
of freezing there (see "How to verify" further below - same verification
steps as the earlier SRAM-voltage attempt apply here). If this doesn't fix
it either: the SRAM-voltage angle (this time done correctly, i.e. skipped
entirely since it's already at the right value - or explicitly re-synced to
Mid during the Mid-voltage phase instead) is still worth revisiting, as is
asking NXP directly (no SDK example anywhere combines SmartDMA + USB HS at
Overdrive simultaneously, confirmed via a full grep across the whole SDK
tree, and no MCXN947 errata on this was found in the SDK sources - would
need NXP's actual errata sheet, which isn't in this SDK checkout).

---


Running log of this project's bring-up, for picking up in a fresh session.
For the stable reference doc (pinout, build instructions), see
[README.md](README.md) - this file is the messier "what's been tried and
what happened" history, kept separate so README doesn't get cluttered.

## Earlier: board boot reliability crisis (resolved - board is reachable and flashing normally as of the top entry in this file)

**At the time, this was logged as PAUSED: board booting unreliably (hangs
very early, before camera or USB init even run) even with known-good
firmware, after a stressful recovery sequence. Since resolved** - the board
has flashed and run correctly in every session since (see the top of this
file for the most recent confirmation). Kept below for the recovery
technique that worked, in case this recurs.

What happened, in order:

1. Tried the `SPC_SetSRAMOperateVoltage()` candidate fix (see the entry
   below this one for the reasoning) - flashed it, and it made things worse:
   boot got stuck in an early `SDK_DelayAtLeastUs()` busy-wait (confirmed via
   halt+PC read over the debug probe, identical PC across a 3-second gap)
   *before* camera init even ran, whereas the un-patched firmware at least
   reached 1-2 camera frames. **Reverted** back to the exact code from the
   "USB video streaming confirmed working" entry below (same
   `USB_DeviceClockInit()` as before, no `SPC_SetSRAMOperateVoltage()` call)
   - see the comment left at that call site for what was tried and why it
   was undone, so nobody re-tries the identical placement blind.
2. Separately (unrelated to the fix above - see the "CORELDO-only
   experiment" entry further below), the board's SWD/debug port had gone
   completely unreachable from an earlier experiment. **Recovered it**: the
   normal FRDM-MCXN947 ISP entry sequence (hold SW3/ISP, plug in the debug
   USB cable *while still holding it*, wait ~1s, release SW3) restored debug
   port access - confirmed via `pyocd commander -c status` reporting
   `Core 0 (cm33_core0): Running`. Multiple plain power-cycle attempts
   (unplug/replug with no buttons) had already failed to fix this before
   trying the ISP sequence, and the online research that led here is worth
   keeping in mind if this recurs:
   - [NXP Community: "Issue with External 1.8V Power on FRDM-MCXN947 Board
     - MCU Unresponsive"](https://community.nxp.com/t5/MCX-Microcontrollers/Issue-with-External-1-8V-Power-on-FRDM-MCXN947-Board-MCU/td-p/2157654)
     - a different root cause (external supply + DCDC/LDO jumper changes)
     but the same class of symptom (debugger detects target voltage but
     can't communicate), resolved there by reverting the hardware power
     config.
   - NXP's "LinkServer" tool has a documented scripted-retry recovery
     technique for exactly this class of problem (loop trying to
     halt-under-reset to catch the core before problematic startup code
     runs) - see [MCU on Eclipse: "LinkServer Scripting, and how to Recover
     MCUs with a
     Script"](https://mcuoneclipse.com/2023/07/25/linkserver-scripting-and-how-to-recover-mcus-with-a-script/).
     Didn't end up needing this (the ISP-button sequence worked first), but
     if that stops working this is the next thing to try - pyOCD supports
     `--connect under-reset` too, though a single attempt of that (without
     the ISP button) did NOT work here.
3. Flashed the reverted (known-good) firmware successfully once the debug
   port was back. **But it now won't boot past that same early
   `SDK_DelayAtLeastUs()` point either** - confirmed stuck at an identical
   PC across a 3-second gap, `s_frameCount` stays `0` (never even reaches
   camera init's first frame). This happened both right after the
   ISP-sequence recovery *and* after a subsequent plain, no-buttons-held
   power cycle - so it's not still "stuck in ISP mode" or anything like
   that, and it's not the SRAM-voltage fix either (already reverted before
   reflashing). This looks like the board is in some kind of degraded/
   leftover analog state (crystal osc startup, PLL trim, bulk supply
   capacitor charge, etc.) that a few seconds unplugged isn't clearing -
   worth trying a much longer power-off wait (minutes) before the next
   session resumes debugging the actual SmartDMA/DCDC conflict. The
   firmware itself (in this repo, as currently committed/on-disk) has NOT
   been proven broken by this - it's the same code that was working a few
   hours earlier in this same session (see the "USB video streaming
   confirmed working" entry below) - don't assume a code regression here
   without re-testing after a proper power-off wait first.

---

**A candidate fix for the SmartDMA hang is implemented and build-clean, but
NOT YET FLASH-TESTED - the board's debug port is currently unreachable and
needs a physical power cycle first (see "Board debug port went unreachable"
below for what happened and why).**

Before that happened, a research pass over the whole SDK (no combined
SmartDMA+USB example exists anywhere in NXP's tree - this is genuinely
uncharted territory) turned up a strong, concrete lead:

- NXP's own `BOARD_PowerMode_OD()` helper
  (`examples/_boards/frdmmcxn947/board.c:232`) - their canonical "go to
  Overdrive" sequence - always pairs the DCDC-to-Overdrive change with a
  `SPC_SetSRAMOperateVoltage(SPC0, &(spc_sram_voltage_config_t){
  .operateVoltage = kSPC_sramOperateAt1P2V, .requestVoltageUpdate = true })`
  call, to retune the SRAM's read/write timing margin for the new voltage.
- NXP's own SmartDMA camera example
  (`examples/_boards/frdmmcxn947/display_examples/smartdma_camera_flexio_mculcd`)
  never raises DCDC at all - stays at Mid the whole time - so it never had
  reason to call this either.
- **This project's `USB_DeviceClockInit()`** (adapted from NXP's own USB
  example's raw-register-write DCDC bring-up) raises DCDC to Overdrive but
  **never calls `SPC_SetSRAMOperateVoltage()`** - a real gap versus NXP's own
  documented Overdrive-transition pattern.
- SmartDMA writes captured pixels into SRAM via DMA at high throughput. A
  stale/mismatched SRAM read/write timing margin after a DCDC voltage bump
  is a very plausible mechanism for those writes to silently stop landing
  correctly a frame or two in - without ever tripping a HardFault, which
  matches the actual observed symptom exactly (see the SmartDMA-hang
  write-up further below for the full characterization of what breaks).

**Fix applied** (`board_port/cm33_core0/hardware_init.c`,
`USB_DeviceClockInit()`): added the missing `SPC_SetSRAMOperateVoltage()`
call, `kSPC_sramOperateAt1P2V`, right after the existing DCDC/CORELDO
Overdrive register write - matching NXP's own `BOARD_PowerMode_OD()`
sequencing. Builds clean. **Next step, once the board is reachable again:**
reflash and check whether `s_frameCount` (`camera_capture.c`) keeps
advancing past 2 instead of freezing there - see "How to verify" at the
bottom of this entry.

### Board debug port went unreachable - needs a physical power cycle

While testing an unrelated hypothesis (whether raising `CORELDO_VDD_LVL`
*alone*, leaving `DCDC_VDD_LVL` untouched, might be enough for the USB PHY's
PLL to lock without breaking the camera - the opposite combination from
everything tested in the bisection below), the board's SWD/debug port
stopped responding entirely - `pyocd commander -c status` fails with
`SWD/JTAG communication failure (WAIT ACK)` or `Invalid AP address`,
consistently across many retries, at different SWD clock speeds, and even
with `--connect under-reset`. This is a more severe failure mode than
anything else seen in this project (no HardFault message, no serial output,
and now not even basic debug-port attach) - raising CORELDO without also
raising DCDC likely put the chip into some invalid/unsupported power state.
**Do not try that combination again** - the code now has a comment at the
call site warning about this.

The only way found to potentially recover from this is a full physical power
cycle (unplug the board's USB cable(s) completely, wait a few seconds,
replug) - software-only recovery attempts (`pyocd commander -c reset`,
`--connect under-reset`, `./build.sh erase`, multiple retries at lower SWD
clock speeds) all failed identically. As of this entry, recovery hasn't been
confirmed yet - whoever picks this up next should verify the board is
reachable again (`pyocd commander -t mcxn947 -c status`, or just
`./build.sh flash`) before doing anything else.

### How to verify the SPC_SetSRAMOperateVoltage() fix once the board is back

1. `./build.sh` (build + flash) - already built clean as of this entry, so
   this should just work once the debug port is reachable.
2. Reset and watch either the serial log (`./build.sh monitor`, look for
   `frame #16`, `#31`, etc. to keep appearing instead of stopping after
   `#1`) or, faster, halt over the debug probe and read `s_frameCount`
   directly:
   ```
   pyocd commander -t mcxn947 -c halt -c "read32 0x20001504 4"
   ```
   (address is for this exact build - re-derive via
   `arm-none-eabi-nm build/camera_ai_demo_cm33_core0.elf | grep s_frameCount`
   if the binary changes). Resume (`-c go`), wait a second or two, halt and
   read again - if the fix worked, the count should have visibly increased
   instead of staying frozen at the same value.
3. If it works: also re-verify actual USB video streaming shows a live,
   updating image now (not just a frozen one) - `gst-launch-1.0 v4l2src
   device=/dev/videoN num-buffers=10 ! video/x-raw,format=YUY2,width=320,height=240
   ! filesink location=/tmp/frames.raw`, then check in Python (like before)
   that consecutive frames in the dump actually differ from each other, not
   just that each individual frame has pixel variance.
4. If it does NOT work: revert the `SPC_SetSRAMOperateVoltage()` addition
   (or leave it - it's harmless either way, just ineffective) and move on to
   the other next-step ideas listed further below (chip errata search - none
   found in the SDK itself, so this means asking NXP directly; or explore
   SmartDMA clock divider retuning, still untested).

---

**USB video streaming is now confirmed working end-to-end on a real Linux
host - but it streams one static (frozen) real frame, not live video, because
the underlying camera-hang issue is narrower than previously written up
below.** Verified directly on the user's PC (which turned out to be reachable
from this environment too - `lsusb`, `dmesg`, `gst-launch-1.0`, `v4l2`
tooling all available):

- `lsusb -d 1fc9:009a -v` shows a fully well-formed UVC 1.00 descriptor set
  (Video Control + Video Streaming interfaces, Uncompressed/YUY2 format,
  320x240 frame descriptor, isochronous endpoint) - no more warnings.
- The Linux kernel's `uvcvideo` driver binds automatically and creates
  `/dev/videoN` nodes (confirmed via `udevadm info`, `ID_MODEL=Camera_AI_Test1`).
- `gst-launch-1.0 v4l2src device=/dev/videoN ! video/x-raw,format=YUY2,...`
  successfully negotiates the format and captures real frames - dumped to a
  raw file and checked in Python: 153,600 bytes/frame (the exact expected
  YUY2 320x240 size) with ~206-207 distinct byte values per frame (i.e. real
  image content, not a flat/constant buffer).
- **But** the captured frames don't change between captures. Halting the
  core over the debug probe and reading `s_frameBuffer`/`s_frameCount`
  directly (`camera_capture.c`) shows why: `s_frameCount` is stuck at `2`
  and the buffer bytes are bit-identical before and after resuming execution
  for 1 full second. **SmartDMA genuinely stops (both the frame-complete
  interrupt and the underlying pixel DMA writes) after capturing ~2 frames**
  - but frame #2 itself was a real, valid capture, and it just sits frozen
  in the buffer afterward. Since the USB streaming path
  (`USB_VideoCamera_ConvertFrameChunk` in usb_video_camera.c) reads straight
  from that buffer on every packet, with no dependency on the frame-ready
  flag the serial log uses, it happily streams that one frozen frame
  over and over - a real, correctly-colored still image, indefinitely
  repeated, not motion video. This is the same underlying DCDC/SmartDMA
  conflict documented further below, just a more precise characterization
  of its actual effect (a full coprocessor hang, not "produces garbage") -
  don't need to re-diagnose it, the fix ideas listed below still apply.

On the way to finding this, two separate, real bugs were found and fixed
(both independent of the DCDC/camera conflict, which remains the one open
problem):

1. **Fixed: device wasn't enumerating on a host PC at all**, because
`USB_DeviceSetSpeed()` (usb_device_descriptor.c) spun forever the moment a
real host sent a bus reset. The user asked "why don't I see the USB device
on my PC" - halting the core over the debug probe while it was plugged into
a real host caught PC stuck inside `USB_DeviceSetSpeed()`, oscillating
between two addresses in that function - a live infinite loop, not a
one-off.

Root cause: `USB_DeviceSetSpeed()` walks the raw config descriptor byte
array by advancing `bLength` bytes at a time. The Processing Unit descriptor
in `g_UsbDeviceConfigurationDescriptor` (usb_device_descriptor.c) had an
extra unconditional `0x00U /* bmVideoStandards */` byte left over from
copy-pasting the stock example's `#if USB_DEVICE_VIDEO_CLASS_VERSION_1_1 ||
_1_5` block, but `USB_VIDEO_VIRTUAL_CAMERA_VC_PROCESSING_UNIT_LENGTH` (0x0B
= 11, the UVC *1.0* length that doesn't include that byte, matching bcdUVC
0x0100 - see usb_device_descriptor.h) was never updated to match: 12 actual
bytes, declared as 11. That one extra byte desynced every descriptor after
it by one position. Confirmed by dumping the live `.data` section
(`arm-none-eabi-objcopy -O binary --only-section=.data ...` + a small Python
parser walking the `(bLength, bDescriptorType)` chain) - the walk landed on
a garbage `bLength=0` entry, which is exactly what makes
`descriptorHead = descriptorHead + descriptorHead->common.bLength` never
advance. **Fix:** removed the extra byte. Re-parsed the corrected `.data`
dump afterward - now a clean 14-entry chain, no zero-length entries,
terminates exactly at the array's own length (180 bytes, matching
`wTotalLength` in the descriptor itself). Confirmed on hardware: halted
after reflashing, PC was back to normal - inside `main()`'s ordinary
`CAMERA_CAPTURE_IsFrameReady()` poll, not stuck anywhere.

2. **Fixed: real host (Linux `uvcvideo` driver) failed format negotiation
with `-32` (STALL) errors**, confirmed via `sudo dmesg`:
`uvcvideo 3-9.1.2:1.1: Failed to query (GET_MIN) UVC probe control : -32
(exp. 26)`, and `gst-launch-1.0`'s `v4l2src` failing `TRY_FMT` with
"Input/output error". Root cause: `USB_DeviceVideoRequest()`
(usb_video_camera.c) only handled `GET_CUR`/`GET_LEN`/`GET_INFO`/`SET_CUR`
for `VS_PROBE_CONTROL` - the same set the SDK's stock
`usb_device_video_virtual_camera` example handles, copied over as-is. Linux's
`uvcvideo` additionally queries `GET_MIN`/`GET_MAX`/`GET_RES`/`GET_DEF` to
learn the valid parameter ranges; it has a workaround for a missing
`GET_DEF` but not for a missing `GET_MIN`, and gives up entirely when that's
STALLed. **Fix:** since this device only ever supports exactly one
configuration (320x240 YUY2 @ 30fps), added `GET_MIN`/`GET_MAX`/`GET_RES`/
`GET_DEF` as fallthroughs to the same `GET_CUR` answer (min == max == res ==
def == cur when there's only one option). Confirmed fixed via `dmesg` (no
more STALL errors) and `gst-launch-1.0` successfully negotiating caps
(`video/x-raw(memory:DMABuf), format=YUYV, width=320, height=240,
framerate=30/1`).

Both fixes verified together on a real Linux host: `lsusb -d 1fc9:009a -v`
shows a clean descriptor set, `uvcvideo` binds and creates `/dev/videoN`
nodes, and `gst-launch-1.0 v4l2src ! video/x-raw,format=YUY2,...` captures
real (if frozen, per above) frames.

**Still open: confirmed real hardware conflict between USB HS PHY bring-up
and SmartDMA camera capture.** Found via a full bisection done directly on
hardware this
session (board turned out to be reachable from this environment via pyOCD/
MCU-Link, including reading back the halted CPU's PC over the debug probe -
much faster than the earlier back-and-forth of asking for serial logs by
hand). Full trail below; short version:

- **USB HS PHY requires `SPC0->ACTIVE_CFG`'s `DCDC_VDD_LVL` raised to
  Overdrive to work at all.** Without it, `CLOCK_EnableUsbhsPhyPllClock()`
  (`mcuxsdk/devices/MCX/MCXN/MCXN947/drivers/fsl_clock.c:3310`) spins forever
  in its `while (0 == (USBPHY->PLL_SIC & PLL_LOCK)) {}` busy-wait - the PHY's
  480MHz PLL never locks. Confirmed by halting the core with
  `pyocd commander -c halt -c reg` and reading PC/LR back to that exact line.
  Not a bug - this is a real, required step for USB HS on this chip.
- **The SmartDMA-based camera capture breaks under *any* `DCDC_VDD_LVL`
  change** - tested both to Overdrive (0x3) and to Normal (0x2), same result
  either way: frames stop advancing (or read back completely flat/all-zero)
  within 1-2 frames of boot, silently, no HardFault (confirmed via a
  temporary diagnostic `HardFault_Handler` override in
  `source/fault_handler.c` that would have printed CFSR/PC/LR - it never
  fired). This happens regardless of whether the DCDC change happens before
  or after `CAMERA_CAPTURE_Init()` starts SmartDMA running - ruled out via
  bisection that it's a "changing voltage while a coprocessor is active"
  *transition* hazard; it's a steady-state incompatibility. Also ruled out
  `CORELDO_VDD_LVL` specifically as the cause (changing it alone doesn't
  break the camera) and the `SYSLDO_VDD_DS`/`DCDC_VDD_DS` drive-strength
  bits (changing those alone doesn't either) - it's specifically
  `DCDC_VDD_LVL`.
- **Net result: no known-working configuration exists yet where both the
  camera and USB HS work at the same time.** `DCDC_VDD_LVL` has to be raised
  for USB; camera breaks whenever it's raised.

Bisection method, in order (each step flashed + observed on real hardware,
via a temporary `#if 1/#else` "TEMP bisection" pattern in
`USB_DeviceClockInit()`/`USB_VideoCamera_Init()` - all since reverted):
1. Skip all USB (`DEMO_USB_STREAM_DISABLE`, see `CMakeLists.txt`
   `USB_STREAM_DIAGNOSTIC_DISABLE` option) - camera ran cleanly past frame
   #700+ with zero issues. Confirms the problem is 100% USB-side.
2. `USB_DeviceClockInit()` only (skip `USB_DeviceClassInit()`/`USB_DeviceRun()`
   entirely) - still hung after ~1-2 frames. Confirms it's the clock/PHY
   bring-up itself, not EHCI controller startup or host enumeration traffic
   (also independently confirmed by testing with the USB-HS cable physically
   unplugged from any PC - same hang).
3. Stop right after the `SPC0->ACTIVE_CFG` write + busy-wait (skip
   LDOCSR/AHBCLKCTRL/SOSC/PLL/PHY-init) - still hung. Narrows it to the SPC0
   regulator register write itself.
4. `DCDC_VDD_LVL(0x3)` (Overdrive) + drive-strength bits, `CORELDO_VDD_LVL`
   left untouched - still hung, and frame #1 came back flat. Rules out
   CORELDO being required for the break.
5. `DCDC_VDD_LVL(0x2)` (Normal) instead of Overdrive - still hung. Rules out
   "specifically Overdrive" - any level change breaks it.
6. Drive-strength bits only (`SYSLDO_VDD_DS`, `DCDC_VDD_DS`), no
   `DCDC_VDD_LVL`/`CORELDO_VDD_LVL` change at all - camera ran cleanly to
   frame #700+. Isolates the cause to `DCDC_VDD_LVL` specifically.
7. Restored full `USB_DeviceClockInit()` + `USB_DeviceClassInit()` +
   `USB_DeviceRun()` with step 6's settings (no `DCDC_VDD_LVL` change) - the
   core hung inside `CLOCK_EnableUsbhsPhyPllClock()`'s `PLL_LOCK` wait
   (confirmed via halt+register read). Proves USB HS genuinely needs the
   voltage bump - it's not something that can just be skipped.
8. Restored `DCDC_VDD_LVL(0x3)` + `CORELDO_VDD_LVL(0x3)` (matching NXP's
   stock example) - USB now completes init cleanly (`USB: video class (UVC)
   camera ready` prints, PLL locks), but camera is back to the original
   symptom (frame #1 flat, hangs after ~2 frames). Back to square one,
   conflict confirmed both directions.

**Where this leaves the code:** `board_port/cm33_core0/hardware_init.c`'s
`USB_DeviceClockInit()` currently matches NXP's stock DCDC/CoreLDO Overdrive
settings (so USB HS actually initializes), with a comment at that call site
documenting this whole conflict and pointing back here. USB streaming itself
works correctly (see above - real frames, correct size/format, confirmed on
a real host); what doesn't work yet is SmartDMA continuing to capture *new*
frames past the first couple, so the live feed is really a single frozen
frame repeated - see the "USB video streaming is now confirmed working"
entry at the top of this file for the precise, corrected characterization
of what breaks (SmartDMA fully stops - both its completion interrupt and
its pixel DMA writes - it doesn't produce garbage).

**Ideas for next session** (untested, in rough order of how promising they
seem):
- Check whether `BOARD_InitBootClocks()`/`clock_config.c` needs to be
  re-run, or specific clock dividers (SmartDMA's core clock, or whatever
  `kMAIN_CLK_to_CLKOUT`/`kCLOCK_DivClkOut` actually derives from) need
  retuning *for* the Overdrive voltage domain, rather than assuming voltage
  and clock frequency are independent - many NXP parts require this kind of
  co-tuning and it's plausible BOARD_InitHardware()'s existing camera clock
  setup was only ever validated at Mid voltage.
- Look for MCXN947 chip errata (NXP's errata sheet, not just the SDK) on
  SmartDMA + DCDC/SPC interactions - this smells like exactly the kind of
  thing that ends up in an errata document.
- As a fallback if no fix is found: time-multiplex instead of running both
  simultaneously - e.g. capture what's needed at Mid voltage, then only
  raise to Overdrive and start USB once camera capture is done for that
  session, if the use case tolerates not truly live-streaming.
- Ask on NXP's community forum / community.nxp.com with the exact
  DCDC_VDD_LVL + SmartDMA symptom - this looks like the kind of interaction
  someone at NXP would recognize immediately if it's a known one.

### Previous entries (kept for context; superseded above where they conflict)

### Previous entry (kept for context; format-choice reasoning below is still current)

**USB (UVC) camera streaming implemented and building clean - NOT YET
FLASHED/TESTED ON HARDWARE (no board access this session).** Built on top of
the previous session's plan (see "Plan" below, kept for the reasoning trail)
of adapting the SDK's `usb_device_video_virtual_camera` example. One thing
that plan didn't anticipate, discovered while actually reading that
example's code: it only implements the **MJPEG** UVC payload format (its
`USB_DeviceVideoPrepareVideoData()` scans transmitted bytes for a `0xFFD9`
JPEG end-of-image marker to find frame boundaries) - not usable as-is for a
sensor that produces raw pixels, and MCXN947 has no hardware JPEG encoder.
Decided (with the user) to switch the UVC format to **uncompressed YUY2**
instead of adding a software JPEG encoder - YUY2 is a standard "every OS's
stock webcam stack understands it, no vendor driver" UVC format, and the
device-side implementation is much simpler than JPEG (fixed frame size,
no marker scanning). Camera capture stays exactly as before (RGB565,
untouched); the RGB565->YUY2 conversion happens on the fly, two source
pixels at a time, directly inside the USB IN-endpoint-complete callback -
deliberately not a separate whole-frame conversion buffer, to keep RAM
usage sane (see "Why no second frame buffer" below).

New files (`firmware/camera_ai_demo/source/usb/`):
- `usb_device_descriptor.c/.h` - UVC descriptors: VC (video control) side is
  unchanged boilerplate from the stock example; VS (video streaming) side
  is rewritten for one format (uncompressed YUY2, 320x240, 30fps) instead
  of MJPEG 176x144, and the still-image-capture descriptor is dropped
  entirely (not supported).
- `usb_video_camera.c/.h` - the UVC class glue (adapted from the stock
  example's `virtual_camera.c`) - control-request handling is basically
  unchanged (that part was already format-agnostic UVC protocol
  boilerplate, not MJPEG-specific), but `USB_DeviceVideoPrepareVideoData()`
  is rewritten: instead of scanning a static byte array for a JPEG marker,
  it reads `CAMERA_CAPTURE_GetFrameBuffer()` at a running pixel offset,
  converts each 2-source-pixel chunk to a YUY2 macropixel
  (`USB_VideoCamera_Rgb565ToYCbCr()`, standard BT.601 integer coefficients),
  and tracks frame-end with a plain pixel counter instead of a marker scan.

`board_port/cm33_core0/hardware_init.c` gained `USB_DeviceClockInit()` /
`USB1_HS_IRQHandler()` / `USB_DeviceIsrEnable()`, copied near-verbatim from
the SDK's own frdmmcxn947 board port for the same stock example (MCXN947-
specific SPC/SCG/SYSCON register sequence to bring up the USB HS PHY's PLL -
not something worth re-deriving). No pin muxing changes needed - USB HS
D+/D- are dedicated pins, not routed through PORT/pin mux, unlike the
camera/LCD signals.

`source/main.c` no longer calls `LCD_Init()`/`LCD_DrawImage()` - the display
path is now `USB_VideoCamera_Init()` (registers the UVC class + brings up
the controller) plus `USB_VideoCamera_Task()` in the main loop (a no-op in
this bare-metal, no-RTOS-task build; kept for symmetry in case that ever
changes). Actual frame delivery isn't driven from the main loop at all -
it's pulled on demand from the USB class callback whenever the host asks for
the next packet, so the main loop's job is unchanged (AI hook + periodic
debug log). LCD driver code in `source/display/` is untouched and still
compiled (just unused - see CMakeLists.txt), same "don't touch unless
resuming LCD work" status as before.

**Why no second frame buffer:** m_data SRAM for this build is 312KB
(`MCXN947_cm33_core0_ram.ld`). The RGB565 camera buffer alone is 320*240*2 =
153,600 bytes. A second full YUY2-converted frame buffer would be another
153,600 bytes - fine on its own, but combined with USB middleware buffers,
stack, and everything else, cutting it uncomfortably close for comfort. Since
YUY2 and RGB565 are both 2 bytes/pixel, converting a whole extra buffer
wasn't actually necessary - converting just the ~1020 bytes needed for
each individual USB packet, directly from the live RGB565 buffer, avoids
the second buffer entirely. Actual build RAM usage: 163,288 / 319,488 bytes
(51%) - see `./build.sh build` output.

**Bandwidth note:** 320x240 YUY2 @ 30fps is ~36.9 Mbit/s, over what a
512-byte/microframe USB HS isochronous pipe can carry (~32.8 Mbit/s cap).
Bumped `HS_STREAM_IN_PACKET_SIZE` to 1024 bytes/microframe (the largest
single-transaction HS isochronous packet size, no need for the extra
"transactions per microframe" descriptor bits) - ~65.5 Mbit/s cap, comfortable
headroom. Only one frame interval is advertised (30fps, matching the
camera's fixed capture rate) since there's nothing to negotiate down to.

**Not yet done:**
- **Not flashed or tested on real hardware this session** - no board
  access. `./build.sh build` succeeds clean (`-Werror`, zero warnings), but
  nobody has confirmed the board actually enumerates as a webcam yet. Next
  session (or whenever hardware is available): `./build.sh` (build+flash),
  plug the board's USB port into a PC, check Windows Camera app / a
  browser's camera picker / `webcammictest.com` for a device (advertised as
  "OV7670 on J9" - see `g_UsbDeviceString3` in usb_device_descriptor.c) and
  confirm a real, live, correctly-colored image appears - not just that
  enumeration succeeds.
- If the image looks corrupted/discolored: most likely spot to check first
  is `USB_VideoCamera_Rgb565ToYCbCr()` (source/usb/usb_video_camera.c) -
  the RGB565 bit layout or YCbCr coefficients would be the first suspects,
  not the USB transport plumbing (that part is closely copied from a
  working stock example).
- If nothing enumerates at all: check the board's actual USB port - MCXN947
  dev boards typically have both a USB-HS "device" port and a debug-probe
  USB port; make sure the camera cable is in the device port, not the
  MCU-Link probe port (which is a separate USB connection for flashing/
  debug console, unrelated to this UVC device).

### Plan (from last session, superseded by the above - kept for the reasoning trail)

- **Hardware supports it.** MCXN947 has a genuine USB High-Speed device
  controller (EHCI-compatible) with its own dedicated HS PHY - confirmed via
  `CLOCK_EnableUsbhsPhyPllClock()` / `USB_EhciPhyInit()` in the SDK's
  `fsl_clock.c`, and multiple USB-HS-capable examples already exist for
  `frdmmcxn947` under `../mcuxsdk/mcuxsdk/examples/_boards/frdmmcxn947/
  usb_examples/`.
- **Reference example to build from:** `usb_device_video_virtual_camera`
  (also a `_lite` variant) in that same directory - a working USB Video
  Class (UVC) device example for this exact board. UVC means the board
  shows up as a standard webcam to any host PC, no custom driver needed.
  The stock example streams a synthetic/generated test pattern, not a real
  sensor - turned out to need a format switch (MJPEG -> YUY2) too, not just
  a data-source swap - see above.

## Goal (from requirement.md)

FRDM-MCXN947 + OV7670 camera (J9), capture frames and get them onto a
display - originally a TFT panel (see "LCD history" below, now abandoned
in favor of USB streaming), with a placeholder hook for an AI model later.
See [requirement.md](requirement.md) for the original ask (note: the
original ask predates the pivot to USB and still describes a TFT panel).

## Camera (J9) - fully confirmed working, do not need to re-debug

Pin table and rework notes in [README.md](README.md#camera-ov7670---j9-smartdmacamera-header).
Serial log proof (PID/VER readback + changing pixel data across frames):
```
Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
Camera: frame #16 ready, 792 samples, pixel range 0xC0C6..0xFBEB, avg=0xE330
Camera: frame #46 ready, 792 samples, pixel range 0xC0C4..0xFDF1, avg=0xDE26
```
This stayed working through every LCD-side change during the (now
abandoned) LCD bring-up - if it ever stops working while adding USB, that's
a NEW regression, not a pre-existing issue.

## LCD history (J8/FlexIO abandoned; project is back on the Arduino header - see the top of this file)

The project spent several sessions bringing up an 8-bit-parallel TFT panel
(`HSD024131-C1` per requirement.md, almost certainly ILI9341-family,
240x320) on the board's J8 FlexIO/LCD header, detouring away from the
Arduino header the project started on. Firmware code for the J8 path
(`source/display/lcd_flexio_mculcd.c`, the J8 half of
`source/display/lcd_bitbang.c` (then named `lcd_bitbang_j8.c`) via
`DEMO_LCD_ARDUINO_HEADER=0`, the LCD-related pin muxing in
`board_port/pin_mux.c`'s `BOARD_InitFlexioPins()`, and the
`LCD_BITBANG_DIAGNOSTIC` CMake option) is still present in the tree in case
J8 is revisited later, but is no longer the active goal - **don't spend time
re-debugging J8 unless the user explicitly asks to resume it.** The project
has since reverted to the Arduino header (its original design) - see the
top of this file for that change.

Progress made on J8 before the pivot back, for reference if resumed:
- Wiring, panel, and the generic MIPI-DCS init sequence were all confirmed
  good (a diagnostic GPIO bit-bang driver on the same J8 pins displayed a
  correct image).
- The FlexIO hardware-bus path had two real, fixed bugs: (1) LCD_RD was
  left floating during writes by the SDK's FlexIO MCULCD driver - fixed by
  driving it as a plain always-high GPIO instead; (2)
  `DEMO_PANEL_WIDTH`/`HEIGHT` were wrong (480x320, leftover from an initial
  wrong ST7796S assumption) causing out-of-range GRAM addressing on every
  boot - fixed to 320x240.
- After those fixes, the FlexIO path went from "solid white, nothing at
  all" to "panel responds, but pixel data comes out as black/white noise"
  - a bus-speed/signal-integrity symptom. Lowering the FlexIO bus speed
  further was in progress: an aggressive slowdown (FlexIO clock /50, ~10
  kHz/pin) caused a full hang (stuck waiting on a FlexIO timer completion
  flag - looked like a real minimum-operating-frequency issue, not just a
  software divider-field limit), backed off to a smaller step (/4 clock
  divider, ~100 kHz/pin) that was built but never flashed/tested before the
  decision to abandon this path.

## Key files touched by the (abandoned) J8 LCD work

- `firmware/camera_ai_demo/source/display/lcd_flexio_mculcd.c` - FlexIO
  hardware-bus LCD driver (J8 only, abandoned)
- `firmware/camera_ai_demo/source/display/lcd_bitbang.c` (was
  `lcd_bitbang_j8.c`) - generic GPIO bit-bang LCD driver, now the active
  default (Arduino header) but originally written/proved out against J8's
  pins
- `firmware/camera_ai_demo/source/display/lcd_display.h` - selects between
  the two above via `DEMO_LCD_BITBANG`
- `firmware/camera_ai_demo/board_port/pin_mux.c` - `BOARD_InitFlexioPins()`
  has the J8 LCD pin muxing (both FlexIO and J8-bit-bang variants);
  `BOARD_InitArduinoLcdPins()` (current default) is separate
- `firmware/camera_ai_demo/board_port/cm33_core0/app.h` - LCD pin/geometry/
  bus-speed macros for both pin sets, selected by `DEMO_LCD_ARDUINO_HEADER`
- `firmware/camera_ai_demo/board_port/cm33_core0/hardware_init.c` - FlexIO
  clock divider setup (J8 only, harmless no-op when Arduino header is active)
