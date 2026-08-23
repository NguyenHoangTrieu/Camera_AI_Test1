# FRDM-MCXN947 — Camera_AI_Test1: OV7670 (J9) capture -> AI hook. Default
# build is camera-only (no display, no USB) - see WORKLOG.md. Two earlier
# output paths were tried and abandoned, both kept in the tree (still build,
# just not called from main.c / not the default) in case either is
# revisited:
#   - J8 FlexIO/TFT display (source/display/, LCD_BITBANG_DIAGNOSTIC below) -
#     abandoned for signal-integrity reasons on the specific panel in hand.
#   - USB Video Class (UVC) webcam over USB HS (source/usb/,
#     USB_STREAM_DIAGNOSTIC_DISABLE below) - abandoned because SmartDMA
#     camera capture and the USB HS PHY require mutually exclusive DCDC
#     voltage levels on this chip, and this board's single USB connector is
#     hard-wired to the HS controller only (no way to use the alternative,
#     Mid-voltage-compatible USB FS module instead).
#
# Out-of-tree ("freestanding") MCUXpresso SDK application, same pattern as
# ../../touch_rgb and ../../wifi_sensing_npu: source lives here, outside the
# SDK checkout, and only refers back into it through ${SdkRootDirPath}.
#
# PROJECT_BOARD_PORT_PATH points at our own board_port/ folder, which holds
# the pin muxing, clock setup and app.h that would normally live under the
# SDK's examples/_boards/<board>/ tree. board.c/clock_config.c are NOT
# duplicated here - the SDK supplies those automatically for board=frdmmcxn947.

cmake_minimum_required(VERSION 3.22.0)

include(${SdkRootDirPath}/cmake/extension/mcux.cmake)

# The SDK resolves PROJECT_BOARD_PORT_PATH against ${SdkRootDirPath}, so it
# has to be a RELATIVE path — passing an absolute one silently produces
# "/path/to/sdk/home/you/..." and the build fails to find pin_mux.c.
file(RELATIVE_PATH BOARD_PORT_REL_PATH
     ${SdkRootDirPath} ${CMAKE_CURRENT_SOURCE_DIR}/board_port)

project(camera_ai_demo LANGUAGES C CXX ASM
        PROJECT_BOARD_PORT_PATH ${BOARD_PORT_REL_PATH})

include(${SdkRootDirPath}/CMakeLists.txt)

# The panel in hand only has an 8-bit data bus (LCD_D0..D7), not the 16-bit
# bus the J8 header's full FlexIO wiring supports. FLEXIO_MCULCD_DATA_BUS_WIDTH
# is a compile-time macro read by the SDK's own fsl_flexio_mculcd.c, so it has
# to be set here (globally) rather than in our own source.
mcux_add_macro(
    CC "-DFLEXIO_MCULCD_DATA_BUS_WIDTH=8"
)

# Which LCD connector/transport to build for - see WORKLOG.md "LCD history"
# for the full story. Default (ON): GPIO bit-bang on the Arduino header,
# the project's original design (requirement.md) and the one confirmed to
# display a correct image. Set OFF to instead build for J8 (abandoned -
# signal-integrity issues never fully resolved on the FlexIO hardware-bus
# path); LCD_BITBANG_DIAGNOSTIC then additionally selects J8 bit-bang
# (diagnostic-only, see source/display/lcd_bitbang.c) vs. J8 FlexIO.
option(LCD_ARDUINO_HEADER_BITBANG "Drive the TFT via GPIO bit-bang on the Arduino header (fallback from J8 - see WORKLOG.md)" ON)
if(LCD_ARDUINO_HEADER_BITBANG)
    set(LCD_BACKEND_SOURCE source/display/lcd_bitbang.c source/display/lcd_bitbang.h)
    mcux_add_macro(CC "-DDEMO_LCD_ARDUINO_HEADER=1 -DDEMO_LCD_BITBANG=1")
else()
    option(LCD_BITBANG_DIAGNOSTIC "Bit-bang the J8 LCD bus via GPIO instead of FlexIO (diagnostic)" OFF)
    if(LCD_BITBANG_DIAGNOSTIC)
        set(LCD_BACKEND_SOURCE source/display/lcd_bitbang.c source/display/lcd_bitbang.h)
        mcux_add_macro(CC "-DDEMO_LCD_ARDUINO_HEADER=0 -DDEMO_LCD_BITBANG=1")
    else()
        set(LCD_BACKEND_SOURCE source/display/lcd_flexio_mculcd.c source/display/lcd_flexio_mculcd.h)
        mcux_add_macro(CC "-DDEMO_LCD_ARDUINO_HEADER=0 -DDEMO_LCD_BITBANG=0")
    endif()
endif()

# USB streaming (UVC webcam over USB HS) is ABANDONED as of this entry - see
# WORKLOG.md "USB streaming pipeline abandoned" for the full reasoning. Root
# cause: SmartDMA camera capture only works with DCDC at Mid voltage, USB HS
# PHY only works at Overdrive, and FRDM-MCXN947's single USB Type-C connector
# (J11) is hard-wired to the HS controller only - the chip's separate USB FS
# module (which wouldn't need Overdrive) has no accessible connector on this
# board (confirmed in NXP's UM12018 board user manual, section 2.3), so
# there's no way to route around the conflict without a hardware rework. A
# software time-multiplex workaround (periodic Mid/Overdrive switching) was
# built and confirmed stable on real hardware, but the underlying conflict is
# a genuine chip/board limitation, not something fixable from this project's
# code - same status as the abandoned LCD path below (source/display/), kept
# in the tree in case NXP ever publishes a fix or this needs revisiting.
#
# ON by default now (camera+AI loop only, no USB) - this is the
# actively-used build. Set to OFF to opt back into the abandoned USB
# streaming path (still there, still builds, just not the default anymore).
option(USB_STREAM_DIAGNOSTIC_DISABLE "Skip USB bring-up entirely - camera+AI loop only, no USB (USB streaming is abandoned, see WORKLOG.md)" ON)
if(USB_STREAM_DIAGNOSTIC_DISABLE)
    mcux_add_macro(CC "-DDEMO_USB_STREAM_DISABLE=1")
else()
    mcux_add_macro(CC "-DDEMO_USB_STREAM_DISABLE=0")
endif()

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES source/main.c
            source/fault_handler.c
            source/camera/camera_capture.c
            source/camera/camera_capture.h
            source/display/lcd_display.h
            ${LCD_BACKEND_SOURCE}
            source/ai/model_runner.c
            source/ai/model_runner.h
            source/ai/model_data.h
            source/usb/usb_device_descriptor.c
            source/usb/usb_device_descriptor.h
            source/usb/usb_video_camera.c
            source/usb/usb_video_camera.h
)

# The whole SDK build defaults to -O0 (west's "debug" CMAKE_BUILD_TYPE,
# not overridable per-app without touching SDK-wide config - see
# WORKLOG.md). At -O0 every GPIO_PinWrite()/GPIO_PortSet() call in the LCD
# bit-bang driver is a real function call (SDK headers mark them
# "static inline", but -O0 mostly ignores that), so per-byte overhead is
# dominated by call/return overhead, not the actual register write.
#
# -O2 here (scoped to just this one file) was tried and confirmed WRONG on
# real hardware - the image came out glitched/noisy, i.e. with
# LCD_BITBANG_DELAY_US=0 (lcd_bitbang.c) the panel's bus timing margin
# comes entirely from GPIO_PinWrite()/GPIO_PortSet() instruction latency,
# and -O2's inlining made those too fast for this panel to latch data
# correctly. Left at -O0 (the SDK-wide default) instead - do not re-enable
# without also raising LCD_BITBANG_DELAY_US back off 0 to compensate, and
# re-test on hardware either way.
# if(LCD_ARDUINO_HEADER_BITBANG OR LCD_BITBANG_DIAGNOSTIC)
#     set_source_files_properties(
#         ${CMAKE_CURRENT_SOURCE_DIR}/source/display/lcd_bitbang.c
#         PROPERTIES COMPILE_OPTIONS "-O2"
#     )
# endif()

# So source/main.c can #include "camera_capture.h" etc. by simple name.
# NOTE: mcux_add_include() joins INCLUDES onto CMAKE_CURRENT_LIST_DIR itself
# when no BASE_PATH is given, so these must be relative (an absolute path
# here silently doubles into a nonexistent "<dir>/<dir>/source" path).
mcux_add_include(
    INCLUDES source
             source/camera
             source/display
             source/ai
             source/usb
)

# Produce a raw .bin next to the .elf so pyOCD can flash it directly.
mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
