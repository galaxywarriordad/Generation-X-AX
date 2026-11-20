cmake_minimum_required(VERSION 3.22)
project(EDuke32AX LANGUAGES C CXX)

option(AX_EDUKE32_USE_OPENGL "Enable OpenGL/Polymost renderer in EDuke32" ON)
option(AX_EDUKE32_NATIVE_ISA "Pass CMake ISA/arch flags to GNUmakefile" ON)

set(_eduke_cflags "${CMAKE_C_FLAGS}")
set(_eduke_cxxflags "${CMAKE_CXX_FLAGS}")

if(AX_EDUKE32_NATIVE_ISA AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  string(APPEND _eduke_cflags " -march=skylake")
  string(APPEND _eduke_cxxflags " -march=skylake")
endif()

set(_eduke_make_args
  -C "${CMAKE_CURRENT_SOURCE_DIR}"
  "CFLAGS=${_eduke_cflags}"
  "CXXFLAGS=${_eduke_cxxflags}"
)

if(NOT AX_EDUKE32_USE_OPENGL)
  list(APPEND _eduke_make_args "USE_OPENGL=0")
endif()

add_custom_target(eduke32_build ALL
  COMMAND ${CMAKE_MAKE_PROGRAM} ${_eduke_make_args}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Building EDuke32 via GNUmakefile with CMake ISA flags"
)

if(TARGET ax_retro_bridge)
  add_dependencies(eduke32_build ax_retro_bridge)
endif()
