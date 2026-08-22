# WORKLOG - Camera_AI_Test1

Running log of this project's bring-up, for picking up in a fresh session.
For the stable reference doc (pinout, build instructions), see
[README.md](README.md) - this file is the messier "what's been tried and
what happened" history, kept separate so README doesn't get cluttered.

## Current status (most recent first)

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

## LCD history (abandoned)

The project spent several sessions bringing up an 8-bit-parallel TFT panel
(`HSD024131-C1` per requirement.md, almost certainly ILI9341-family,
240x320) on the board's J8 FlexIO/LCD header. Firmware code for this
(`source/display/lcd_flexio_mculcd.c`, `source/display/lcd_bitbang_j8.c`,
the LCD-related pin muxing in `board_port/pin_mux.c`, and the
`LCD_BITBANG_DIAGNOSTIC` CMake option) is still present in the tree in case
this is revisited later, but is no longer the active goal - **don't spend
time re-debugging it unless the user explicitly asks to resume LCD work.**

Progress made before the pivot, for reference if resumed:
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

## Key files touched by the (abandoned) LCD work

- `firmware/camera_ai_demo/source/display/lcd_flexio_mculcd.c` - FlexIO
  hardware-bus LCD driver
- `firmware/camera_ai_demo/source/display/lcd_bitbang_j8.c` - diagnostic
  GPIO bit-bang LCD driver (proved wiring/panel/init sequence are fine)
- `firmware/camera_ai_demo/source/display/lcd_display.h` - selects between
  the two above via `DEMO_LCD_BITBANG`
- `firmware/camera_ai_demo/board_port/pin_mux.c` - `BOARD_InitFlexioPins()`
  has the J8 LCD pin muxing (both FlexIO and bit-bang variants)
- `firmware/camera_ai_demo/board_port/cm33_core0/app.h` - LCD pin/geometry/
  bus-speed macros
- `firmware/camera_ai_demo/board_port/cm33_core0/hardware_init.c` - FlexIO
  clock divider setup
