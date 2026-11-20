cmake_minimum_required(VERSION 3.22)
project(EDuke32AX LANGUAGES C CXX)

option(AX_EDUKE32_USE_OPENGL "Enable OpenGL/Polymost renderer in EDuke32" ON)
option(AX_EDUKE32_NATIVE_ISA "Pass CMake ISA/arch flags to GNUmakefile" ON)

find_program(EDUKE32_MAKE_EXE NAMES gmake make)
if(NOT EDUKE32_MAKE_EXE)
  set(EDUKE32_MAKE_EXE "${CMAKE_MAKE_PROGRAM}")
endif()

set(_eduke_cflags "${CMAKE_C_FLAGS}")
set(_eduke_cxxflags "${CMAKE_CXX_FLAGS}")
set(_eduke_cppflags "")
set(_eduke_ldflags "")

if(AX_EDUKE32_NATIVE_ISA AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  string(APPEND _eduke_cflags " -march=skylake")
  string(APPEND _eduke_cxxflags " -march=skylake")
endif()

if(TARGET ax_retro_bridge)
  get_target_property(_bridge_inc ax_retro_bridge INTERFACE_INCLUDE_DIRECTORIES)
  if(_bridge_inc)
    foreach(_inc IN LISTS _bridge_inc)
      string(APPEND _eduke_cppflags " -I${_inc}")
    endforeach()
  endif()
endif()

if(DEFINED AX_SDK_INCLUDE_DIRS)
  foreach(_inc IN LISTS AX_SDK_INCLUDE_DIRS)
    string(APPEND _eduke_cppflags " -I${_inc}")
  endforeach()
endif()

set(_eduke_make_args
  -C "${CMAKE_CURRENT_SOURCE_DIR}"
  eduke32
  WITHOUT_GTK=1
  USE_LIBVPX=0
  "CFLAGS=${_eduke_cflags}"
  "CXXFLAGS=${_eduke_cxxflags}"
)

if(NOT "${_eduke_cppflags}" STREQUAL "")
  list(APPEND _eduke_make_args "CPPFLAGS=${_eduke_cppflags}")
endif()

if(NOT "${_eduke_ldflags}" STREQUAL "")
  list(APPEND _eduke_make_args "LDFLAGS=${_eduke_ldflags}")
endif()

if(TARGET ax_retro_bridge)
  list(APPEND _eduke_make_args "AX_RETRO_BRIDGE_LIB=$<TARGET_FILE:ax_retro_bridge>")
endif()

if(NOT AX_EDUKE32_USE_OPENGL)
  list(APPEND _eduke_make_args "USE_OPENGL=0")
endif()

include(ProcessorCount)
ProcessorCount(NPROC)
if(NPROC EQUAL 0)
  set(NPROC 4)
endif()

add_custom_target(eduke32_build ALL
  COMMAND "${EDUKE32_MAKE_EXE}" -j${NPROC} ${_eduke_make_args}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Building EDuke32 via GNUmakefile with AX SDK and CMake ISA flags"
)

if(TARGET ax_retro_bridge)
  add_dependencies(eduke32_build ax_retro_bridge)
endif()
