# Camera_AI_Test1

Camera + AI project for **FRDM-MCXN947**, built against the local `mcuxsdk`
checkout (`../mcuxsdk`). Captures frames from an **OV7670** camera via
SmartDMA and feeds them into a placeholder AI-model hook. **Default build:
camera + AI loop only, no display, no USB** - continuous, proven stable
(700+ clean frames).

> **USB Video Class (UVC) webcam streaming over USB High-Speed is
> ABANDONED - a genuine hardware/board limitation, not a bug in this
> project's code.** Root cause (see [WORKLOG.md](WORKLOG.md) "USB streaming
> pipeline abandoned" for the full trail): SmartDMA camera capture only runs
> reliably with the chip's DCDC regulator at Mid voltage (1.0V); the USB HS
> PHY only locks its PLL at Overdrive (1.2V) - confirmed via multiple
> independent hardware tests that this is unrelated to voltage *transitions*
> or to anything USB-specific being active, it's the DCDC level itself. The
> chip does have a second, independent USB Full-Speed module that doesn't
> need Overdrive - but FRDM-MCXN947's single USB Type-C connector (J11) is
> hard-wired to the HS controller only (confirmed in NXP's UM12018 board
> user manual, section 2.3), so there's no way to reach the FS module without
> a hardware rework. A software time-multiplex workaround (periodic
> Mid-voltage recapture / Overdrive-streaming switching) was built and
> confirmed stable on real hardware - low-frame-rate but genuinely live - but
> given the underlying conflict can't be fixed, USB streaming is no longer
> the active build. The code is still in the tree (`source/usb/`, still
> builds) in case NXP ever publishes a fix or this is revisited; see
> "USB (UVC webcam, abandoned)" below.
>
> This project originally targeted a TFT panel on the board's J8 FlexIO/LCD
> header before that - also abandoned, for unrelated (signal-integrity)
> reasons; see "LCD (abandoned)" below and WORKLOG.md's "LCD history" section
> for that debugging trail. Both abandoned paths' code stays in the tree,
> just not called from `main.c` by default.

Source of truth: [requirement.md](requirement.md) (note: predates the pivot to
USB and still describes a TFT panel). Camera wiring/pinout is confirmed
against the mcuxsdk pin tables (not just a photo).

## Hardware

- FRDM-MCXN947 board
- OV7670 camera module -> **J9 (SmartDMA/Camera header)**
- The MCU-Link debug-probe USB connection (for flashing/serial console -
  `./build.sh flash`/`monitor`). The board's other USB Type-C port (J11, USB
  HS device) is only needed if opting back into the abandoned USB-streaming
  build - see "USB (UVC webcam, abandoned)" below.
- Optional/abandoned: a TFT panel (`HSD024131-C1` per requirement.md) wired to
  the J8 FlexIO/LCD header - see "LCD (abandoned)" below. Not needed for the
  default build.

## Pinout

### Camera (OV7670 -> J9 SmartDMA/Camera header)

| Camera signal | MCU pin | J9 pin # |
|---|---|---|
| SIOC / SIOD (SCCB, I2C-like) | P3_2 / P3_3 (LP_FLEXCOMM7) | 19 / 20 |
| XCLK | P2_2 (CLKOUT) | 16 |
| PCLK | P0_5 | — |
| HREF | P0_11 | — |
| VSYNC | P0_4 | — |
| D0..D7 | P1_4, P1_5, P1_6, P1_7, P3_4, P3_5, P1_10, P1_11 | 7,8,9,10,11,12,13,14 |
| 3V3 / GND | J9 pin 21 / 22 | 21 / 22 |

Board rework note carried over from the NXP reference design: **change SJ16, SJ26,
SJ27 from the right side to the left side** before attaching the camera to J9.

### TFT (abandoned - 8-bit panel -> J8 FlexIO/LCD header)

**Not part of the active build** - kept for reference in case LCD work is
resumed later (see the banner at the top of this file and WORKLOG.md's "LCD
history"). None of this wiring is needed to use the current USB-streaming
firmware.

Straight from `pin_mux.c` in NXP's `smartdma_camera_flexio_mculcd` board port
(not a guess), narrowed to only the 8 data pins this panel uses - **LCD_D8..D15
(P4_16..P4_23) are not wired/configured, this panel doesn't have them**:

| LCD signal | MCU pin | FlexIO signal |
|---|---|---|
| LCD_RD | P0_8 | FLEXIO0_D0 |
| LCD_WR | P0_9 | FLEXIO0_D1 |
| LCD_CS | P0_12 | plain GPIO |
| LCD_RS (DC) | P0_7 | plain GPIO |
| LCD_RST | P4_7 | plain GPIO |
| LCD_BLK (backlight enable) | P4_6 | plain GPIO |
| LCD_D0 | P2_8 | FLEXIO0_D16 |
| LCD_D1 | P2_9 | FLEXIO0_D17 |
| LCD_D2 | P2_10 | FLEXIO0_D18 |
| LCD_D3 | P2_11 | FLEXIO0_D19 |
| LCD_D4 | P4_12 | FLEXIO0_D20 |
| LCD_D5 | P4_13 | FLEXIO0_D21 |
| LCD_D6 | P4_14 | FLEXIO0_D22 |
| LCD_D7 | P4_15 | FLEXIO0_D23 |
| GND, 3.3V | — | power |

**LCD_BLK matters a lot: without it, the screen can stay completely dark even
though the bus/commands are all working correctly** (first bring-up hit exactly
this - screen showed nothing at all, fixed by wiring/driving this pin). NXP's
own reference example doesn't handle it because the official LCD-PAR-S035 panel
has an always-on backlight, but most generic bare modules need this pin driven
high (`LCD_Init()` in `lcd_flexio_mculcd.c` does this automatically now). If
your module's backlight pin is instead labeled `LED`, `BL`, `LED-A`, or similar,
wire it to P4_6; if it's a always-on/no-control-needed module, it's fine to
leave that wire disconnected (driving an unconnected GPIO high is harmless).

Match each of these 14 signals by name to the panel module's own labeled pins
(most bare "MCU 8-bit parallel TFT" modules label their pins `LCD_D0`..`LCD_D7`,
`RD`, `WR`, `CS`, `RS`/`DC`, `RST` directly - no ambiguity like the earlier
Arduino-shield attempt had). To find the matching physical pin **on J8 itself**,
check the board's own silkscreen next to the J8 connector first (this board's
other headers all have printed signal labels) before trusting any external
pin-number diagram.

The firmware sets `FLEXIO_MCULCD_DATA_BUS_WIDTH=8` (see `CMakeLists.txt`) to match
- this is a compile-time setting read by the SDK's own FlexIO MCULCD driver, so
it can't be changed at runtime; if you swap to a 16-bit panel later, that macro
and the pin table above both need updating together (the removed `LCD_D8..D15`
rows in an earlier revision of this README show the full 16-bit table).

## USB (UVC webcam, abandoned)

> **ABANDONED - see the banner at the top of this file and WORKLOG.md "USB
> streaming pipeline abandoned" for why.** Not a code bug: SmartDMA camera
> capture and the USB HS PHY need mutually exclusive DCDC voltage levels on
> this chip, and this board only exposes the HS USB controller on its one
> connector. The code below still builds and still works (as a
> low-frame-rate, time-multiplexed feed - confirmed stable on hardware), it's
> just not the default anymore and isn't being developed further.

To opt back into this build: `./build.sh rebuild -DUSB_STREAM_DIAGNOSTIC_DISABLE=OFF`
(the default is now `ON` - camera-only). The board then enumerates as a
standard USB Video Class device - format "Uncompressed YUY2", 320x240, 30fps,
over USB High-Speed (see `source/usb/usb_device_descriptor.c` for the exact
descriptor bytes and `source/usb/usb_video_camera.c` for the class glue that
fills each USB packet from the camera buffer):

1. Plug the board's **USB HS device port (J11)** (not the MCU-Link debug
   port) into the host PC.
2. Open the host OS's stock camera app (Windows Camera, `cheese` on Linux,
   etc.) or a browser camera picker (e.g. `webcammictest.com`) and look for
   `Camera_AI_Test1`. On Linux, `v4l2-ctl --list-devices` or
   `udevadm info --name=/dev/videoN` (matching `ID_MODEL=Camera_AI_Test1`)
   will confirm which `/dev/videoN` node is the board.
3. The image updates roughly every `DEMO_OVERDRIVE_HOLD_MS` (5 seconds by
   default, `source/main.c`) - not truly live, but not a single frozen
   image either. See WORKLOG.md for the periodic-refresh design and why it
   can't be made faster/continuous without the underlying voltage conflict
   being fixed.

## LCD (abandoned) troubleshooting

The sections below only apply if resuming the abandoned J8 FlexIO/TFT path
(see the banner at the top of this file) - not relevant to the current
USB-streaming build.

### If the screen still shows nothing

In rough order of likelihood, having already fixed the backlight above:

1. **Power**: is the panel's GND and 3.3V (or 5V, if the module needs it - check
   its datasheet/silkscreen) actually connected? A panel with no power looks
   identical to one with no backlight - completely dark.
2. **RST held low**: if `LCD_RST` isn't wired, or is wired but stuck low, the
   controller stays in reset forever and never responds to anything. Check
   continuity from the panel's RST pin to P4_7.
3. **Wrong controller**: this code assumes ST7796S. If your module actually uses
   a different chip (ILI9341, ILI9486, ST7789, etc - check the panel's part
   number/datasheet), the ST7796S init command sequence may not correctly wake up
   a different controller. See "If your panel isn't ST7796S" below.
4. **CS/RS swapped, or a data bit swapped**: double check each of the 14 wires
   against the table above one at a time with a multimeter (continuity mode),
   panel unpowered.
5. **Loose jumper wire**: with 14 wires this is a lot of connections for
   breadboard/dupont wires - reseat each one and make sure none are making
   only intermittent contact.

### If your panel isn't ST7796S

`LCD_InitPanel()` in `source/display/lcd_flexio_mculcd.c` calls `ST7796S_Init()`
with NXP's LCD-PAR-S035 preset. If your module uses a different controller (e.g.
ILI9486), that call needs to change - this SDK ships driver components for a
handful of panels under `mcuxsdk/mcuxsdk/components/display/`; check there for a
matching one, or adapt the `dbi_xfer_ops_t` pattern already in that file to a
different init command sequence.

## Project layout

Same out-of-tree layout as the sibling projects in `NPX_Workspace` (`touch_rgb`,
`i2s_sniffer`, `wifi_sensing_npu`): `board_port/` holds only pin muxing + app.h;
`board.c`/`clock_config.c` are **not** duplicated here - the SDK supplies them
automatically for `board=frdmmcxn947`.

```
Camera_AI_Test1/
  README.md                  <- this file
  requirement.md, image*.png <- original request
  firmware/camera_ai_demo/
    CMakeLists.txt, prj.conf, build.sh
    board_port/
      pin_mux.c/h                    <- camera (J9) + LCD (J8 FlexIO) pin routing
      cm33_core0/
        app.h                         <- BOARD_InitHardware() proto, shared pin/geometry macros
        hardware_init.c                <- BOARD_InitHardware()
        prj.conf                       <- board-port Kconfig (inputmux, pinmux_project_folder)
    source/
      main.c                         <- capture -> AI hook loop (default); USB path (abandoned) behind USB_STREAM_DIAGNOSTIC_DISABLE=OFF
      camera/camera_capture.c/h      <- OV7670 + SmartDMA (from NXP reference)
      usb/usb_video_camera.c/h       <- UVC class glue, RGB565->YUY2 conversion (abandoned, still builds)
      usb/usb_device_descriptor.c/h  <- UVC descriptors (abandoned, still builds)
      display/lcd_flexio_mculcd.c/h  <- FlexIO + ST7796S driver (abandoned, still built but unused)
      ai/model_runner.c/h            <- AI integration stub (see below)
      ai/model_data.h                <- placeholder for an exported model
```

## Building and flashing

**Verified working** on this machine (arm-none-eabi-gcc, west via the venv at
`../../tools/westenv`, MCU-Link over pyOCD) - builds clean with `-Werror`:

```
$ ./firmware/camera_ai_demo/build.sh          # build, then flash
$ ./firmware/camera_ai_demo/build.sh build    # build only
$ ./firmware/camera_ai_demo/build.sh flash    # flash the last build
$ ./firmware/camera_ai_demo/build.sh monitor  # open the serial console (115200-8-N-1)
```

There's also a diagnostic build variant that bypasses the FlexIO peripheral
and bit-bangs the same J8 pins via plain GPIO instead - a leftover from the
abandoned LCD path (see WORKLOG.md), unrelated to the current USB build:

```
$ ./firmware/camera_ai_demo/build.sh rebuild -DLCD_BITBANG_DIAGNOSTIC=ON
```

`build.sh` mirrors `../../touch_rgb/build.sh`: it runs `west build -b frdmmcxn947
firmware/camera_ai_demo --toolchain armgcc -Dcore_id=cm33_core0` from inside the
`../mcuxsdk` west workspace, then flashes the resulting `.elf` over the on-board
MCU-Link (CMSIS-DAP) with pyOCD, auto-selecting the probe by its MCU-LINK unique ID
so it doesn't prompt if another debug probe is also plugged in. See
`../../touch_rgb/README.md` for the probe-permissions / udev-rule note if `pyocd`
needs `sudo` on your setup. Note the MCU-Link probe port is a *different* USB
connection than the USB HS device port the UVC webcam enumerates on - both may
need to be plugged in (probe port for flashing/serial console, device port for
the webcam).

**Camera confirmed working end-to-end** (from the earlier Arduino-header revision
of this project, same camera code, unchanged since): the OV7670 driver
reads back its PID/VER registers over J9's SCCB/I2C and matches a genuine OV7670
(`PID=0x76 VER=0x73`), and SmartDMA frame-ready interrupts fire with a periodic
diagnostic log (`CAMERA_CAPTURE_LogFrameSignature()` in `main.c`) confirming pixel
data is non-flat and changing frame to frame:
```
Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
Camera: frame #16 ready, 792 samples, pixel range 0xC0C6..0xFBEB, avg=0xE330
Camera: frame #46 ready, 792 samples, pixel range 0xC0C4..0xFDF1, avg=0xDE26
```

**USB streaming, when opted back into, is confirmed working end-to-end on a
real host** as a periodic-refresh feed (enumeration, UVC format negotiation,
frame delivery, and multi-cycle stability all verified via `lsusb`, `dmesg`,
and a continuous `gst-launch-1.0` capture session - see WORKLOG.md) - but see
the banner at the top of this file for why it's not the default build.

## AI model integration

`source/ai/model_runner.c` is currently a stub: `AI_MODEL_RunInference()` always
returns `result->valid = false`. It's the extension point requested in
`requirement.md`. Two ready-made paths, both documented in detail in the comment at
the top of that file:

- **TensorFlow Lite Micro** - reuse the pattern in this SDK's
  `examples/eiq_examples/tflm_label_image` (interpreter setup, tensor arena,
  `Invoke()`).
- **Edge Impulse** - drop in a "C++ library" export from an Edge Impulse project and
  call `run_classifier()`.

`main.c` and `model_runner.h`'s C API are written so that plugging in either one
only touches `model_runner.c`/`.cpp` and `CMakeLists.txt`.

## Known limitations

- **USB streaming is abandoned - a hardware/board limitation, not fixable
  from this project's code.** See the banner at the top of this file and
  WORKLOG.md "USB streaming pipeline abandoned" for the full reasoning
  (DCDC voltage conflict between SmartDMA and USB HS PHY; no accessible
  USB FS alternative on this board). If built anyway (opt-in, see "USB
  (UVC webcam, abandoned)" above), it works as a periodic-refresh feed
  (~1 new frame every few seconds), not continuous live video.
- The camera capture resolution (320x240) is fixed by `DEMO_BUFFER_WIDTH/HEIGHT`
  in `app.h`; the (abandoned) UVC descriptor's advertised frame size
  (`source/usb/usb_device_descriptor.h`) has to be kept in sync with it manually
  if that ever changes.
- `source/ai/model_runner.c` has no real model wired in yet by design.
- LCD-specific limitations (`LCD_InitPanel()` assuming ST7796S, etc.) no longer
  apply to the active build - see "LCD (abandoned) troubleshooting" above if
  reviving that path.

## History

This project originally targeted a TFT shield plugged into the board's **Arduino
header** (bit-banged GPIO 8080 bus), per the literal wording of `requirement.md`.
That path was fully brought up and visually confirmed working (camera image
displayed, correctly oriented, after fixing several wiring/orientation issues), but
had two accepted downsides: needing hand-soldered jumper wires to work around 2
analog-only pins on the Arduino header, and visible screen tearing from the slow
GPIO bit-bang draw racing the camera's SmartDMA writes. The project was then
switched to the **J8 FlexIO** header instead - NXP's own officially-supported,
hardware-accelerated path for driving a parallel LCD on this board - trading the
"exact literal shield from the photo" requirement for reliability and speed. The
Arduino-header bit-bang driver (`lcd_arduino_8080.c`) was removed; if you want to
go back to it, it's straightforward to recreate from this file's git history (or
ask for it to be regenerated - the pin derivation and wiring are documented in
this file's own version history).
