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

# Edge Impulse tensor arena (~90KB, see source/ai/ei_sramx_alloc.c) doesn't
# fit in m_data (already ~97% full from camera+LCD framebuffers) - this
# fragment adds an output section placing it in m_sramx instead (declared
# but otherwise unused by the SDK's own linker script). Additive only
# (INSERT AFTER) - does not replace/duplicate the SDK's board linker
# script, so must come after include(${SdkRootDirPath}/CMakeLists.txt)
# above, which is what actually selects the base -T script.
mcux_add_armgcc_linker_script(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    LINKER board_port/ei_sramx.ld
)

# Default stack (STACK_SIZE in the SDK's linker script) is only 2KB - fine
# for plain GPIO/UART firmware, but TFLite Micro/CMSIS-NN's call chain goes
# many frames deep, with individual frames that are themselves large (e.g.
# arm_nn_mat_mult_nt_t_s8's ~2.7KB of locals for its unrolled q31
# accumulators). First bumped to 4KB, which was NOT enough - see
# WORKLOG.md "Bug #2" / "root cause found" for the full diagnosis: the
# fault was misread as a CMSIS-NN pointer-alignment UsageFault (UFSR bit
# 8, UNALIGNED) when it was actually STKOF (UFSR bit 4, hardware stack-
# limit check - CFSR 0x00100000 is bit 20 of CFSR = bit 4 of UFSR, not bit
# 8), confirmed by disassembly: the fault PC was the function's prologue
# `subw sp, sp, #2780` itself, not a load/store. Bumped to 16KB now that
# s_aiScratchPool (main.c) was shrunk from 150KB to 16KB to make room in
# m_data (the tensor arena fits the primary 96KB m_sramx pool without
# needing a large overflow pool - see the comment there). The linker's own
# ASSERT(__StackLimit >= __HeapLimit) will fail the build rather than
# silently overflowing if this doesn't fit.
mcux_add_linker_symbol(
    SYMBOLS "__stack_size__=0x4000"
)

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

# Which AI inference backend to build: CPU+CMSIS-NN via the Edge Impulse
# SDK (model_runner.cpp, default) or this chip's Neutron NPU
# (model_runner_npu.cpp) via NXP's own middleware/eiq/tensorflow-lite +
# middleware/eiq/neutron, bypassing the Edge Impulse SDK entirely - see
# WORKLOG.md "NPU (Neutron) plan" for the full story (both paths implement
# the identical model_runner.h API, so main.c doesn't change either way,
# and both print the same DWT-based "total classifier time" line for a
# direct on/off comparison).
option(AI_MODEL_USE_NPU "Run inference on the Neutron NPU (model_runner_npu.cpp) instead of CPU+CMSIS-NN via the Edge Impulse SDK (model_runner.cpp) - see WORKLOG.md" OFF)
if(AI_MODEL_USE_NPU)
    set(AI_MODEL_RUNNER_SOURCE source/ai/model_runner_npu.cpp)
else()
    set(AI_MODEL_RUNNER_SOURCE source/ai/model_runner.cpp)
endif()

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES source/main.c
            source/fault_handler.c
            source/camera/camera_capture.c
            source/camera/camera_capture.h
            source/display/lcd_display.h
            source/display/bbox_overlay.c
            source/display/bbox_overlay.h
            ${LCD_BACKEND_SOURCE}
            ${AI_MODEL_RUNNER_SOURCE}
            source/ai/model_runner.h
            source/ai/model_data.h
            source/ai/ei_sramx_alloc.c
            source/ai/ei_debug_porting.c
            source/usb/usb_device_descriptor.c
            source/usb/usb_device_descriptor.h
            source/usb/usb_video_camera.c
            source/usb/usb_video_camera.h
)

if(NOT AI_MODEL_USE_NPU)
# Edge Impulse "C++ library" export (Test_Drowsy_NXP project, FOMO,
# closed_eye/open_eye/yawning). NOT using the export's own CMakeLists.txt
# (add_subdirectory) - it hardcodes `if(NOT TARGET app) message(FATAL_ERROR
# ...)`, but this SDK's west (non-find_package) build mode names its real
# target ${MCUX_SDK_PROJECT_NAME} (e.g. camera_ai_demo_cm33_core0), not
# literally "app" - add_executable() ALIASing doesn't apply here either
# (CMake only allows ALIAS for library targets). Instead, replicate the
# export's own glob logic (see source/ai/edge_impulse/edge-impulse-sdk/
# cmake/zephyr/CMakeLists.txt, the "arm" architecture branch) directly via
# this project's own mcux_add_source()/mcux_add_include(), which target
# ${MCUX_SDK_PROJECT_NAME} correctly.
set(EI_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/source/ai/edge_impulse)
file(GLOB_RECURSE EI_SOURCE_FILES
    ${EI_SDK_ROOT}/edge-impulse-sdk/*.cpp
    ${EI_SDK_ROOT}/edge-impulse-sdk/*.cc
    ${EI_SDK_ROOT}/edge-impulse-sdk/*.c
    ${EI_SDK_ROOT}/edge-impulse-sdk/*.s
    ${EI_SDK_ROOT}/tflite-model/*.cpp
)
list(REMOVE_DUPLICATES EI_SOURCE_FILES)

# mcux_add_source() always joins BASE_PATH onto every SOURCES entry (even
# already-absolute ones) - convert the glob's absolute paths to relative
# first, matching how every other mcux_add_source() call in this file
# works.
set(EI_SOURCE_FILES_REL "")
foreach(_ei_src ${EI_SOURCE_FILES})
    file(RELATIVE_PATH _ei_src_rel ${CMAKE_CURRENT_SOURCE_DIR} ${_ei_src})
    list(APPEND EI_SOURCE_FILES_REL ${_ei_src_rel})
endforeach()

mcux_add_source(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES ${EI_SOURCE_FILES_REL}
)

# The whole project builds with -Werror (see the -O0 comment further down)
# - that's appropriate for our own code, but Edge Impulse's vendored
# TFLite-Micro/CMSIS-NN sources aren't warning-clean under this toolchain's
# -Wall (e.g. a sign-compare in tensorflow/lite/micro/kernels/complex_abs.cc)
# and we don't control that code, so exempt just these files instead of
# disabling -Werror project-wide.
set_source_files_properties(${EI_SOURCE_FILES_REL} PROPERTIES COMPILE_OPTIONS "-Wno-error")

mcux_add_include(
    BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    INCLUDES source/ai/edge_impulse
             source/ai/edge_impulse/edge-impulse-sdk
)

# Needed by the SDK's CMSIS-NN kernels (edge-impulse-sdk/CMSIS/NN/) - same
# defines the export's own zephyr/CMakeLists.txt sets for ARCH=arm.
# SILENCE_EI_CLASSFIER_OBJECT_DETECTION_COUNT_WARNING avoids a #warning (->
# error under -Werror) in model-parameters/model_metadata.h that would
# otherwise also hit model_runner.cpp (one of our own -Werror-strict files).
# EI_PORTING_CLIB=1 selects the generic-libc porting backend
# (edge-impulse-sdk/porting/clib/ei_classifier_porting.cpp, which provides
# ei_malloc/ei_calloc/ei_free/ei_printf/ei_printf_float/etc as weak plain-C
# wrappers) - ei_classifier_porting.h auto-detects Zephyr/Arduino/ESP32/
# Mbed/POSIX/etc by macro, but has no bare-metal-MCU fallback, and every
# porting/*/ei_classifier_porting.cpp is entirely `#if EI_PORTING_<X> == 1`
# - without this, ALL of them compile to empty translation units and the
# link fails with "undefined reference to ei_printf" etc.
# EI_C_LINKAGE=1 is what actually made source/ai/ei_sramx_alloc.c and
# ei_debug_porting.c's overrides take effect: ei_classifier_porting.h only
# wraps ei_malloc/ei_calloc/ei_free/ei_printf/ei_printf_float in
# `extern "C" {}` when this is set (`#if defined(__cplusplus) &&
# EI_C_LINKAGE == 1`). Without it, C++ callers (all the .cpp/.cc SDK
# sources) expect C++-name-mangled symbols, but our overrides are plain .c
# files (always C linkage) - so nothing actually matched, both sets of
# symbols coexisted under different mangled names, and the weak
# C++-linkage clib version (real malloc()/calloc() against the nearly-full
# m_data heap) silently "won" instead of being overridden - the build
# link succeeded either way (no missing symbol), but at runtime
# tflite_learn_..._init()'s single big ei_aligned_calloc() call kept
# failing (EI_IMPULSE_TFLITE_ARENA_ALLOC_FAILED, error -6) with our sramx
# pool's high-water mark stuck at 0 - it was never actually being called.
# mcux_add_macro()'s C++ category is "CX", not "CXX" - CC alone only
# affects .c files, not the .cc/.cpp EI sources or model_runner.cpp.
mcux_add_macro(
    CC "-DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1 -DARM_MATH_LOOPUNROLL -DSILENCE_EI_CLASSFIER_OBJECT_DETECTION_COUNT_WARNING=1 -DEI_PORTING_CLIB=1 -DEI_C_LINKAGE=1"
    CX "-DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1 -DARM_MATH_LOOPUNROLL -DSILENCE_EI_CLASSFIER_OBJECT_DETECTION_COUNT_WARNING=1 -DEI_PORTING_CLIB=1 -DEI_C_LINKAGE=1"
)

else() # AI_MODEL_USE_NPU

# Neutron NPU path (model_runner_npu.cpp) - talks to NXP's own
# middleware/eiq/tensorflow-lite TFLM tree directly, NOT the Edge
# Impulse-vendored copy above (the two must not both be built into the
# same image - different TFLM snapshots, would risk ODR violations/symbol
# clashes if both were linked in). Precompiled lib/cm33/armgcc/libtflm.a
# covers the whole TFLM core + CMSIS-NN reference kernels; the Neutron
# custom-op kernel (tensorflow/lite/micro/kernels/neutron/neutron.cpp)
# isn't part of that prebuilt lib (see middleware/eiq/tensorflow-lite/
# CMakeLists.txt's own "neutron" component) so it's compiled from source
# here, same as micro_time.cpp (platform timer glue - also not in the
# prebuilt lib, per that same CMakeLists.txt always building it from
# source regardless of the "binary" component).
set(TFLM_ROOT ${SdkRootDirPath}/middleware/eiq/tensorflow-lite)
set(NEUTRON_ROOT ${SdkRootDirPath}/middleware/eiq/neutron)

mcux_add_source(
    BASE_PATH ${TFLM_ROOT}
    SOURCES tensorflow/lite/micro/kernels/neutron/neutron.cpp
            tensorflow/lite/micro/kernels/neutron/neutron.h
            tensorflow/lite/micro/micro_time.cpp
            tensorflow/lite/micro/debug_log.cpp
)

# Same reasoning as the Edge Impulse "-Wno-error" exemption above - this
# is vendored NXP code, not warning-clean under this toolchain's -Wall,
# and the whole project otherwise builds -Werror.
set_source_files_properties(
    ${TFLM_ROOT}/tensorflow/lite/micro/kernels/neutron/neutron.cpp
    ${TFLM_ROOT}/tensorflow/lite/micro/micro_time.cpp
    ${TFLM_ROOT}/tensorflow/lite/micro/debug_log.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-error"
)

# BASE_PATH required here - mcux_add_include() silently prepends
# CMAKE_CURRENT_LIST_DIR onto every INCLUDES entry when BASE_PATH is
# omitted, which mangles already-absolute paths like TFLM_ROOT (see the
# "So source/main.c can #include..." comment further down for the same
# gotcha noted for a different call).
mcux_add_include(
    BASE_PATH ${SdkRootDirPath}
    INCLUDES middleware/eiq/tensorflow-lite
             middleware/eiq/tensorflow-lite/third_party/flatbuffers/include
             middleware/eiq/tensorflow-lite/third_party/gemmlowp
             middleware/eiq/tensorflow-lite/third_party/ruy
             middleware/eiq/neutron/driver/include
             middleware/eiq/neutron/common/include
)

# TF_LITE_STATIC_MEMORY/KERNELS_OPTIMIZED_FOR_SPEED match
# middleware/eiq/tensorflow-lite/CMakeLists.txt's own flags for this
# component - needed for the prebuilt libtflm.a's ABI to line up with
# what neutron.cpp (built from source here) expects.
mcux_add_macro(
    CC "-DTF_LITE_STATIC_MEMORY -DKERNELS_OPTIMIZED_FOR_SPEED"
    CX "-DTF_LITE_STATIC_MEMORY -DKERNELS_OPTIMIZED_FOR_SPEED"
)

# libtflm.a: precompiled TFLM core for cm33/armgcc (no source needed/
# available for most of it - see middleware/eiq/tensorflow-lite/
# CMakeLists.txt's "binary" component, same file NXP's own mcxn947 NPU
# examples use). libNeutronDriver.a/libNeutronFirmware.a: the Neutron
# coprocessor's driver + firmware blob for the "mcxn" target family
# (covers MCXN947) - neutronInit() (called internally by neutron.cpp on
# first Prepare(), no manual call needed here) needs both.
target_link_libraries(${MCUX_SDK_PROJECT_NAME} PRIVATE
    ${TFLM_ROOT}/lib/cm33/armgcc/libtflm.a
    ${NEUTRON_ROOT}/mcxn/libNeutronDriver.a
    ${NEUTRON_ROOT}/mcxn/libNeutronFirmware.a
)

endif() # AI_MODEL_USE_NPU

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
