# Install script for directory: /home/nguyenhoangtrieu/embedded/NPX_Workspace/mcuxsdk/mcuxsdk/components

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/arm-none-eabi-objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/assert/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/audio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/aws_iot/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/button/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/common_task/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/coremark/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/crc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/debug_console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/debug_console_lite/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/display/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/exception_handling/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/internal_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/led/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/lists/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/log/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/mem_manager/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/messaging/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/misc_utilities/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/osa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/panic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/pmic/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/power_manager/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/pwm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/reset/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/reset1/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/rng/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/rpmsg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/rtc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/sensor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/serial_manager/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/shell/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/spi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/str/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/timer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/timer_manager/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/time_stamp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/touch/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/video/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/phy/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/IS42SM16800H/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/adc_sensor/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/conn_fwloader/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/ele_crypto/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/ele_hseb/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/i3c_bus/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/imx_sm_crc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/imu_adapter/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/mt48lc2m32b2/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/mt48lc4m16a2/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/mx25_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/mx25l_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/mx25r_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/rtt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/scmi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/sdu/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/slcd_engine/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/smt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/srtm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/sx1502/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/wifi_bt_module/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/smbus/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/debug_console_rtt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/notifier/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/ele_base_api/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/mpi_loader/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/pinctrl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/clock/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/silicon_id/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/sm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/systick_timer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/timer_lptmr/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/unity/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/power/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/codec/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/expander/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/edgefast_wifi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/eeprom_emulation/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/eeprom_emulation_k4/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/storage/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/debug/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/gen_hal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/lce/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_dspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_ecspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_enet/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_flexcomm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_ii2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_iuart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpc_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpc_i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpc_vspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpc_vusart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpi2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpsci/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_lpuart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_spi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/cmsis_drivers/cmsis_uart/cmake_install.cmake")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/nguyenhoangtrieu/embedded/NPX_Workspace/Camera_AI_Test1/firmware/camera_ai_demo/build_dualcore/core0/components/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
