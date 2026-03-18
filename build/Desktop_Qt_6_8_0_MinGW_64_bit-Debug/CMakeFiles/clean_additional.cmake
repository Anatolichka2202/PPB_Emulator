# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\PPB_Emulator_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\PPB_Emulator_autogen.dir\\ParseCache.txt"
  "PPB_Emulator_autogen"
  )
endif()
