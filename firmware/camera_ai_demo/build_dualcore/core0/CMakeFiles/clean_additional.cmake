# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "debug")
  file(REMOVE_RECURSE
  "camera_ai_demo_cm33_core0.bin"
  "clean_files-NOTFOUND"
  )
endif()
