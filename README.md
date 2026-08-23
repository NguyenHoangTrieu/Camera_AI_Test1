# Camera_AI_Test1

Camera + AI + TFT display project for **FRDM-MCXN947**, built against the local
`mcuxsdk` checkout (`../mcuxsdk`). Captures frames from an **OV7670** camera via
SmartDMA, feeds them into a placeholder AI-model hook, and draws them to a TFT
panel wired to the board's **Arduino header** (GPIO bit-bang 8080 bus) - the
project's original design, per `requirement.md`.

> **Two other paths were tried and abandoned** (code still in the tree, still
> builds, just not the default - see [WORKLOG.md](WORKLOG.md) for the full
> trail of both):
> - **J8 FlexIO/LCD header** (hardware-accelerated): wiring/panel/init
>   sequence were all confirmed good (a bit-bang diagnostic on the same J8
>   pins displayed a correct image), but the FlexIO hardware-bus path itself
>   never got past "responds to commands but pixel data comes out as
>   black/white noise" - a bus-speed/signal-integrity issue that wasn't fully
>   resolved. Abandoned in favor of reverting to the (already proven-working)
>   Arduino header instead of continuing to chase it.
> - **USB Video Class (UVC) webcam streaming**: a genuine hardware/board
>   limitation, not fixable in software - SmartDMA camera capture and the USB
>   HS PHY need mutually exclusive DCDC voltage levels on this chip, and this
>   board's one USB connector is hard-wired to the HS controller only (no
>   accessible USB FS alternative). See "USB (UVC webcam, abandoned)" below.

Source of truth: [requirement.md](requirement.md). Camera wiring/pinout is
confirmed against the mcuxsdk pin tables (not just a photo). LCD wiring/init
sequence are confirmed against real hardware (see "Building and flashing"
below for the proof).

## Hardware

- FRDM-MCXN947 board
- OV7670 camera module -> **J9 (SmartDMA/Camera header)**
- TFT panel with an 8-bit parallel data bus (`HSD024131-C1` per
  requirement.md, almost certainly ILI9341-family, 240x320) -> **Arduino
  header** (J1-J4), with 2 short jumper wires - see "Pinout" below.
- The MCU-Link debug-probe USB connection (for flashing/serial console -
  `./build.sh flash`/`monitor`).

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

### TFT (Arduino header, GPIO bit-bang)

| Shield pin | Arduino pin | MCU pin |
|---|---|---|
| LCD_D0 | D8 | P0_28 |
| LCD_D1 | D9 | P0_10 |
| LCD_D2 | D2 | P0_29 |
| LCD_D3 | D3 | P1_23 |
| LCD_D4 | D4 | P0_30 |
| LCD_D5 | D5 | P1_21 |
| LCD_D6 | D6 | P1_2 |
| LCD_D7 | D7 | P0_31 |
| LCD_RS (DC) | A2 | P0_14 |
| LCD_CS | A3 | P0_22 |
| LCD_RST | A4 | P0_15 |
| LCD_RD | A0 *(no GPIO)* -> jumper -> **D0** | P4_3 |
| LCD_WR | A1 *(no GPIO)* -> jumper -> **D1** | P4_2 |
| LCD_BLK (backlight, if your panel has a separate enable pin) | A5 | P0_23 |
| GND, 5V, 3V3 | board power header | direct |

**LCD_D0..D7 and LCD_RS/CS/RST plug in normally** - all real GPIO pins, no
rework. **Only LCD_RD and LCD_WR need jumper wires**: they land on the
shield's A0/A1 socket positions, and A0/A1 on FRDM-MCXN947 are analog-only
(no GPIO at all) - so:
- Bend the shield's A0 and A1 pins up/out of the way instead of seating them
  in their native sockets (the rest of that header - RS/CS/RST on A2/A3/A4 -
  plugs in normally).
- Run 2 short jumper wires: shield's **A0 pad (LCD_RD) -> board Arduino D0**,
  and shield's **A1 pad (LCD_WR) -> board Arduino D1**. (Not D10-D13: on this
  shield those are already SD_SS/SD_DI/SD_DO/SD_SCK for its onboard SD slot,
  which this project doesn't use but shouldn't be shorted against either.)

**LCD_BLK** is wired to Arduino A5 as a harmless just-in-case default -
`LCD_Init()` drives it high automatically. If your panel doesn't have a
separate backlight-enable pin (the original shield this was bringing up
worked fine without one), it's fine to leave that wire disconnected.

### If the screen shows nothing / garbled

1. **Power**: GND and 3.3V (or 5V, check your panel) actually connected?
2. **Jumpers**: are LCD_RD/LCD_WR actually reaching Arduino D0/D1 (not just
   plugged into the shield's native A0/A1, which have no GPIO)?
3. **RST held low**: if `LCD_RST` isn't wired/is stuck low, the controller
   never leaves reset. Check continuity to P0_15 (Arduino A4).
4. **Wrong controller**: `LCD_InitPanel()` (`source/display/lcd_bitbang.c`)
   uses a generic MIPI-DCS sequence (works across ILI9481/ILI9486/HX8357/
   ST7796-family panels) - if your panel uses something else entirely, this
   may need adjusting.
5. **Rotated/mirrored image**: the panel is driven in landscape (MADCTL MV
   bit) to match the camera's 320x240 buffer 1:1, no software rotation. If
   the image comes out sideways or mirrored, adjust the MADCTL byte in
   `LCD_InitPanel()` or the camera's mirror/vflip registers
   (`DEMO_CAMERA_MIRROR`/`DEMO_CAMERA_VFLIP` in `camera_capture.c`).
6. Double-check each of the 14 signals against the table above with a
   multimeter (continuity mode, panel unpowered) - easy to swap one with 14
   wires on a breadboard.

## USB (UVC webcam, abandoned)

> **ABANDONED - a genuine hardware/board limitation, not a code bug.**
> SmartDMA camera capture only runs reliably with the chip's DCDC regulator
> at Mid voltage (1.0V); the USB HS PHY only locks its PLL at Overdrive
> (1.2V) - confirmed via multiple independent hardware tests. The chip's
> separate USB Full-Speed module doesn't need Overdrive, but FRDM-MCXN947's
> single USB Type-C connector (J11) is hard-wired to the HS controller only
> (confirmed in NXP's UM12018 board user manual, section 2.3) - no way to
> reach the FS module without a hardware rework. A software time-multiplex
> workaround (periodic Mid-voltage recapture / Overdrive-streaming
> switching) was built and confirmed stable on real hardware - low-frame-rate
> but genuinely live - see WORKLOG.md "USB streaming pipeline abandoned" for
> the full trail. Superseded by reverting to the Arduino-header LCD instead.

To opt back into this build: `./build.sh rebuild -DUSB_STREAM_DIAGNOSTIC_DISABLE=OFF`.
The board then enumerates as a standard USB Video Class device - format
"Uncompressed YUY2", 320x240, 30fps, over USB High-Speed (see
`source/usb/usb_device_descriptor.c` for the exact descriptor bytes and
`source/usb/usb_video_camera.c` for the class glue). Note this doesn't drive
the LCD at all (mutually exclusive with the DCDC level the LCD/camera-preview
build needs, and not really related anyway) - it's a completely separate
build mode.

## Project layout

Same out-of-tree layout as the sibling projects in `NPX_Workspace` (`touch_rgb`,
`i2s_sniffer`, `wifi_sensing_npu`): `board_port/` holds only pin muxing + app.h;
`board.c`/`clock_config.c` are **not** duplicated here - the SDK supplies them
automatically for `board=frdmmcxn947`.

```
Camera_AI_Test1/
  README.md                  <- this file
  WORKLOG.md                 <- full bring-up history (messier, chronological)
  requirement.md, image*.png <- original request
  firmware/camera_ai_demo/
    CMakeLists.txt, prj.conf, build.sh
    board_port/
      pin_mux.c/h                    <- camera (J9) + LCD (Arduino/J8, both) pin routing
      cm33_core0/
        app.h                         <- BOARD_InitHardware() proto, shared pin/geometry macros
        hardware_init.c                <- BOARD_InitHardware()
        prj.conf                       <- board-port Kconfig (inputmux, pinmux_project_folder)
    source/
      main.c                         <- capture -> AI hook -> LCD loop (default); USB path (abandoned) behind USB_STREAM_DIAGNOSTIC_DISABLE=OFF
      camera/camera_capture.c/h      <- OV7670 + SmartDMA (from NXP reference)
      display/lcd_bitbang.c/h        <- GPIO bit-bang LCD driver (current default - Arduino header or J8, see app.h)
      display/lcd_flexio_mculcd.c/h  <- FlexIO + ST7796S driver (abandoned J8 path, still builds but unused)
      usb/usb_video_camera.c/h       <- UVC class glue, RGB565->YUY2 conversion (abandoned, still builds)
      usb/usb_device_descriptor.c/h  <- UVC descriptors (abandoned, still builds)
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

Other build variants (all still build, kept for reference - see WORKLOG.md):

```
$ ./build.sh rebuild -DLCD_ARDUINO_HEADER_BITBANG=OFF                        # J8 FlexIO (abandoned)
$ ./build.sh rebuild -DLCD_ARDUINO_HEADER_BITBANG=OFF -DLCD_BITBANG_DIAGNOSTIC=ON  # J8 bit-bang (abandoned)
$ ./build.sh rebuild -DUSB_STREAM_DIAGNOSTIC_DISABLE=OFF                     # USB streaming (abandoned)
```

`build.sh` mirrors `../../touch_rgb/build.sh`: it runs `west build -b frdmmcxn947
firmware/camera_ai_demo --toolchain armgcc -Dcore_id=cm33_core0` from inside the
`../mcuxsdk` west workspace, then flashes the resulting `.elf` over the on-board
MCU-Link (CMSIS-DAP) with pyOCD, auto-selecting the probe by its MCU-LINK unique ID
so it doesn't prompt if another debug probe is also plugged in. See
`../../touch_rgb/README.md` for the probe-permissions / udev-rule note if `pyocd`
needs `sudo` on your setup.

**Camera confirmed working end-to-end**, unchanged through every LCD/USB
experiment in this project: the OV7670 driver reads back its PID/VER
registers over J9's SCCB/I2C and matches a genuine OV7670 (`PID=0x76
VER=0x73`), and SmartDMA frame-ready interrupts fire with a periodic
diagnostic log (`CAMERA_CAPTURE_LogFrameSignature()` in `main.c`) confirming
pixel data is non-flat and changing frame to frame:
```
Camera: OV7670 detected on J9 (PID=0x76 VER=0x73 confirmed), 320x240 @ 30 fps.
Camera: frame #16 ready, 792 samples, pixel range 0xC0C6..0xFBEB, avg=0xE330
Camera: frame #46 ready, 792 samples, pixel range 0xC0C4..0xFDF1, avg=0xDE26
```

**LCD (Arduino header, current default)**: builds clean, flashed to real
hardware, boots to the `LCD: bit-bang GPIO on the Arduino header` log line
without hanging. The exact same driver/init sequence (targeting J8's pins
instead) previously displayed a correct camera image during the J8
bring-up's isolation testing - see WORKLOG.md "LCD history" - but that was on
J8's wiring specifically; **re-verify the physical picture on your panel
after wiring the Arduino-header jumpers above**, since the pin mapping is
different even though the driver code is identical.

**USB streaming, when opted back into, is confirmed working end-to-end on a
real host** as a periodic-refresh feed (enumeration, UVC format negotiation,
frame delivery, and multi-cycle stability all verified via `lsusb`, `dmesg`,
and a continuous `gst-launch-1.0` capture session - see WORKLOG.md) - not
relevant to the default (Arduino LCD) build, just noted for completeness.

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

- **LCD_RD/LCD_WR jumper-wiring assumption**: the exact per-pin order (which
  of RD/WR/RS/CS/RST maps to which Arduino pin) is this project's own
  best-effort reading, re-derived across several sessions - see "If the
  screen shows nothing/garbled" above if it doesn't match your physical
  shield.
- Bit-bang GPIO is inherently slow - expect a low frame rate (well under the
  camera's 30fps capture rate), not smooth video. This is a live low-fps
  preview, by design (see main.c).
- **USB streaming and J8/FlexIO LCD are both abandoned** - hardware
  limitations (USB) or unresolved signal-integrity issues (J8/FlexIO), not
  actively being developed. See WORKLOG.md for the full history if either is
  ever revisited.
- `source/ai/model_runner.c` has no real model wired in yet by design.

## History

This project originally targeted a TFT shield plugged into the board's
**Arduino header** (bit-banged GPIO 8080 bus) - the current design, and also
the original one, per the literal wording of `requirement.md`. In between, it
was switched to the **J8 FlexIO** header (NXP's officially-supported,
hardware-accelerated path) for reliability/speed, and separately explored
**USB Video Class streaming** as an alternative to a physical display
entirely. Both detours were abandoned (see the banner at the top of this file
and WORKLOG.md for the full trails) and the project reverted to its original
Arduino-header design, now informed by everything learned along the way (the
generic MIPI-DCS init sequence, the A0/A1-analog-only jumper workaround, the
backlight-enable-pin lesson from the J8 detour, etc).
