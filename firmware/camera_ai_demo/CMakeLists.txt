# FRDM-MCXN947 — Camera_AI_Test1: OV7670 (J9) -> AI hook -> J8 FlexIO TFT
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

# Diagnostic switch: bypass the FlexIO peripheral and bit-bang the same J8
# pins with plain GPIO instead - see source/display/lcd_bitbang_j8.c and
# WORKLOG.md "Have not tried reverting to bit-banged GPIO on J8's pins" for
# why this exists. Off by default (normal FlexIO path).
option(LCD_BITBANG_DIAGNOSTIC "Bit-bang the J8 LCD bus via GPIO instead of FlexIO (diagnostic)" OFF)
if(LCD_BITBANG_DIAGNOSTIC)
    set(LCD_BACKEND_SOURCE source/display/lcd_bitbang_j8.c source/display/lcd_bitbang_j8.h)
    mcux_add_macro(CC "-DDEMO_LCD_BITBANG=1")
else()
    set(LCD_BACKEND_SOURCE source/display/lcd_flexio_mculcd.c source/display/lcd_flexio_mculcd.h)
    mcux_add_macro(CC "-DDEMO_LCD_BITBANG=0")
endif()

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES source/main.c
            source/camera/camera_capture.c
            source/camera/camera_capture.h
            source/display/lcd_display.h
            ${LCD_BACKEND_SOURCE}
            source/ai/model_runner.c
            source/ai/model_runner.h
            source/ai/model_data.h
)

# So source/main.c can #include "camera_capture.h" etc. by simple name.
# NOTE: mcux_add_include() joins INCLUDES onto CMAKE_CURRENT_LIST_DIR itself
# when no BASE_PATH is given, so these must be relative (an absolute path
# here silently doubles into a nonexistent "<dir>/<dir>/source" path).
mcux_add_include(
    INCLUDES source
             source/camera
             source/display
             source/ai
)

# Produce a raw .bin next to the .elf so pyOCD can flash it directly.
mcux_convert_binary(BINARY ${APPLICATION_BINARY_DIR}/${MCUX_SDK_PROJECT_NAME}.bin)
