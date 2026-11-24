cmake_minimum_required(VERSION 3.22)
project(EDuke32AX LANGUAGES C CXX)

option(AX_EDUKE32_USE_OPENGL "Enable OpenGL/Polymost renderer in EDuke32" ON)
option(AX_EDUKE32_NATIVE_ISA "Pass Skylake/BMI2 ISA flags to GNUmakefile" ON)
option(AX_EDUKE32_DETERMINISTIC "Pass deterministic prefix and build-id flags" ON)
option(AX_EDUKE32_LTO "Enable LTO/ThinLTO in Release/RelWithDebInfo builds" ON)

find_program(EDUKE32_MAKE_EXE NAMES gmake make)
if(NOT EDUKE32_MAKE_EXE)
  set(EDUKE32_MAKE_EXE "${CMAKE_MAKE_PROGRAM}")
endif()

set(_eduke_cflags "${CMAKE_C_FLAGS}")
set(_eduke_cxxflags "${CMAKE_CXX_FLAGS}")
set(_eduke_cppflags "")
set(_eduke_ldflags "${CMAKE_EXE_LINKER_FLAGS}")

if(AX_EDUKE32_NATIVE_ISA AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    string(APPEND _eduke_cflags " -O3 -march=skylake -mbmi -mbmi2")
    string(APPEND _eduke_cxxflags " -O3 -march=skylake -mbmi -mbmi2")
  else()
    string(APPEND _eduke_cflags " -O3")
    string(APPEND _eduke_cxxflags " -O3")
  endif()
endif()

if(AX_EDUKE32_DETERMINISTIC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  string(APPEND _eduke_cflags " -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=build")
  string(APPEND _eduke_cxxflags " -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=build")
  if(UNIX AND NOT APPLE)
    string(APPEND _eduke_ldflags " -Wl,--build-id=sha1")
  endif()
endif()

if(AX_EDUKE32_LTO AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo)$")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    string(APPEND _eduke_cflags " -flto=thin")
    string(APPEND _eduke_cxxflags " -flto=thin")
    string(APPEND _eduke_ldflags " -flto=thin")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND _eduke_cflags " -flto")
    string(APPEND _eduke_cxxflags " -flto")
    string(APPEND _eduke_ldflags " -flto")
  endif()
endif()

if(TARGET ax_retro_bridge)
  get_target_property(_bridge_inc ax_retro_bridge INTERFACE_INCLUDE_DIRECTORIES)
  if(_bridge_inc)
    foreach(_inc IN LISTS _bridge_inc)
      string(APPEND _eduke_cppflags " -I${_inc}")
    endforeach()
  endif()
endif()

if(TARGET ax::core)
  get_target_property(_ax_inc ax::core INTERFACE_INCLUDE_DIRECTORIES)
  if(_ax_inc)
    foreach(_inc IN LISTS _ax_inc)
      string(APPEND _eduke_cppflags " -I${_inc}")
    endforeach()
  endif()
elseif(TARGET ax_sdk)
  get_target_property(_ax_inc ax_sdk INTERFACE_INCLUDE_DIRECTORIES)
  if(_ax_inc)
    foreach(_inc IN LISTS _ax_inc)
      string(APPEND _eduke_cppflags " -I${_inc}")
    endforeach()
  endif()
endif()

if(DEFINED AX_SDK_INCLUDE_DIRS)
  foreach(_inc IN LISTS AX_SDK_INCLUDE_DIRS)
    string(APPEND _eduke_cppflags " -I${_inc}")
  endforeach()
endif()

string(STRIP "${_eduke_cflags}" _eduke_cflags)
string(STRIP "${_eduke_cxxflags}" _eduke_cxxflags)
string(STRIP "${_eduke_cppflags}" _eduke_cppflags)
string(STRIP "${_eduke_ldflags}" _eduke_ldflags)

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
elseif(NPROC GREATER 4)
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
if(TARGET ax::core)
  add_dependencies(eduke32_build ax::core)
elseif(TARGET ax_sdk)
  add_dependencies(eduke32_build ax_sdk)
endif()

