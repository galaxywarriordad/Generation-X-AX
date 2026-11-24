cmake_minimum_required(VERSION 3.22)
project(GenerationXAX VERSION 1.36.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AX_BUILD_RMG "Build RMG (Nintendo 64) engine" ON)
option(AX_BUILD_BLASTEM "Build BlastEm (Sega Mega Drive) engine" ON)
option(AX_BUILD_CANNONBALL "Build Cannonball (1986 OutRun) engine" ON)
option(AX_BUILD_EDUKE32 "Build EDuke32 FPS engine" ON)
option(AX_BUILD_FRONTIER "Build Frontier: Elite II engine" ON)
option(AX_BUILD_FLYCAST "Build Flycast (Dreamcast / NAOMI / Atomiswave) engine" ON)
option(AX_BUILD_HATARI "Build Hatari (Atari ST) engine" ON)
option(GENX_BUILD_SNES "Build Generation-X-AX SNES adapter (genx-snes-ax)" ON)
option(AX_FETCH_SDK "Fetch ax-sdk v1_36_2 if AX_SDK_DIR is not set" ON)
option(AX_ENABLE_SKYLAKELOCK "Lock x86_64 Release builds to -march=skylake -mbmi -mbmi2" ON)
option(AX_ENABLE_LTO "Enable ThinLTO / Interprocedural Optimization in Release builds" ON)
option(AX_ENABLE_MPK3_MIDI "Compile Akai MPK Mini MK3 ALSA/SPSC bridge into ax_retro_bridge" ON)
option(AX_PGO_GENERATE "Enable Stage 1 PGO instrumentation (-fprofile-instr-generate)" OFF)
option(AX_PGO_USE "Enable Stage 2 PGO profile consumption (set AX_PGO_PROFILE_FILE)" OFF)
set(AX_PGO_PROFILE_FILE "" CACHE FILEPATH "Merged .profdata file for Stage 2 PGO")

# Deterministic build flags (reproducible paths, archives, and builds)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    add_compile_definitions(
      _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH=1
      _DISABLE_VECTOR_ANNOTATION
    )
    add_compile_options(
      /bigobj
      /EHsc
      /Brepro
      -Wno-unused-command-line-argument
      "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
    )
    add_link_options(/Brepro)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND AX_ENABLE_SKYLAKELOCK)
      add_compile_options(/clang:-O3 /clang:-march=skylake /clang:-mbmi /clang:-mbmi2)
    endif()
    if(AX_ENABLE_LTO)
      add_compile_options($<$<CONFIG:Release>:-flto=thin> $<$<CONFIG:RelWithDebInfo>:-flto=thin>)
      add_link_options($<$<CONFIG:Release>:-flto=thin> $<$<CONFIG:RelWithDebInfo>:-flto=thin>)
    endif()
    if(AX_PGO_GENERATE)
      add_compile_options(-fprofile-instr-generate)
    elseif(AX_PGO_USE AND AX_PGO_PROFILE_FILE)
      add_compile_options("-fprofile-use=${AX_PGO_PROFILE_FILE}")
    endif()
  else()
    add_compile_options(
      -O3
      -fno-plt
      "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
      "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build"
    )
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND AX_ENABLE_SKYLAKELOCK)
      add_compile_options(-march=skylake -mbmi -mbmi2)
    endif()
    if(AX_ENABLE_LTO)
      add_compile_options($<$<CONFIG:Release>:-flto=thin> $<$<CONFIG:RelWithDebInfo>:-flto=thin>)
      add_link_options($<$<CONFIG:Release>:-flto=thin> $<$<CONFIG:RelWithDebInfo>:-flto=thin>)
    endif()
    if(UNIX AND NOT APPLE)
      add_link_options(-Wl,--build-id=sha1)
    endif()
    if(AX_PGO_GENERATE)
      add_compile_options(-fprofile-instr-generate)
      add_link_options(-fprofile-instr-generate)
    elseif(AX_PGO_USE AND AX_PGO_PROFILE_FILE)
      add_compile_options("-fprofile-use=${AX_PGO_PROFILE_FILE}")
      add_link_options("-fprofile-use=${AX_PGO_PROFILE_FILE}")
    endif()
  endif()
elseif(AX_ENABLE_LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

set(AX_SDK_DIR "" CACHE PATH "Path to local ax-sdk checkout")

if(NOT AX_SDK_DIR AND DEFINED ENV{AX_SDK_DIR} AND EXISTS "$ENV{AX_SDK_DIR}/CMakeLists.txt")
  set(AX_SDK_DIR "$ENV{AX_SDK_DIR}" CACHE PATH "Path to local ax-sdk checkout" FORCE)
elseif(NOT AX_SDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk/CMakeLists.txt")
  set(AX_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk" CACHE PATH "Path to local ax-sdk checkout" FORCE)
endif()

find_package(ax-sdk CONFIG QUIET)
if(NOT TARGET ax::core AND NOT TARGET ax_core)
  if(AX_SDK_DIR AND EXISTS "${AX_SDK_DIR}/CMakeLists.txt")
    add_subdirectory("${AX_SDK_DIR}" "${CMAKE_BINARY_DIR}/ax-sdk-build" EXCLUDE_FROM_ALL)
  elseif(AX_FETCH_SDK)
    include(FetchContent)
    set(_ax_proto "https")
    set(_ax_host "github.com")
    set(_ax_repo "galaxywarriordad/ax-sdk.git")
    FetchContent_Declare(
      ax_sdk
      GIT_REPOSITORY "${_ax_proto}://${_ax_host}/${_ax_repo}"
      GIT_TAG "v1_36_2"
      GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(ax_sdk)
  endif()
endif()

set(AX_RETRO_BRIDGE_SOURCES
  shared/ax_retro_bridge.cpp
)

if(AX_ENABLE_MPK3_MIDI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shared/ax_midi_mpk3.cpp")
  list(APPEND AX_RETRO_BRIDGE_SOURCES shared/ax_midi_mpk3.cpp)
endif()

add_library(ax_retro_bridge STATIC
  ${AX_RETRO_BRIDGE_SOURCES}
)

target_include_directories(ax_retro_bridge
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/shared"
)

if(TARGET ax::core)
  target_link_libraries(ax_retro_bridge PUBLIC ax::core)
  target_compile_definitions(ax_retro_bridge PUBLIC AX_HAS_CORE_TARGET=1)
elseif(TARGET ax_core)
  target_link_libraries(ax_retro_bridge PUBLIC ax_core)
  target_compile_definitions(ax_retro_bridge PUBLIC AX_HAS_CORE_TARGET=1)
endif()

if(AX_ENABLE_MPK3_MIDI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shared/ax_midi_mpk3.cpp")
  find_package(Threads REQUIRED)
  target_link_libraries(ax_retro_bridge PUBLIC Threads::Threads)
  if(UNIX AND NOT APPLE)
    find_package(ALSA QUIET)
    if(ALSA_FOUND)
      target_link_libraries(ax_retro_bridge PUBLIC ALSA::ALSA)
      target_compile_definitions(ax_retro_bridge PUBLIC AX_FRONTIER_HAS_ALSA=1)
    endif()
  endif()
endif()

macro(ax_bind_retro_engine _engine_target)
  if(TARGET ${_engine_target})
    target_link_libraries(${_engine_target} PRIVATE ax_retro_bridge)
    if(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND AX_PGO_GENERATE)
      target_link_options(${_engine_target} PRIVATE "/WHOLEARCHIVE:clang_rt.profile-x86_64.lib")
    endif()
  endif()
endmacro()

if(AX_BUILD_RMG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/rmg/CMakeLists.txt")
  add_subdirectory(engines/rmg)
  ax_bind_retro_engine(rmg)
  ax_bind_retro_engine(RMG)
endif()

if(AX_BUILD_BLASTEM AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/blastem/CMakeLists.txt")
  add_subdirectory(engines/blastem)
  ax_bind_retro_engine(blastem)
endif()

if(AX_BUILD_CANNONBALL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/cannonball/cmake/CMakeLists.txt")
  add_subdirectory(engines/cannonball/cmake)
  ax_bind_retro_engine(cannonball)
endif()

if(AX_BUILD_EDUKE32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/eduke32/CMakeLists.txt")
  add_subdirectory(engines/eduke32)
  ax_bind_retro_engine(eduke32)
endif()

if(AX_BUILD_FRONTIER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/frontier/CMakeLists.txt")
  add_subdirectory(engines/frontier)
  ax_bind_retro_engine(frontier)
endif()

if(AX_BUILD_FLYCAST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/flycast/CMakeLists.txt")
  set(USE_VULKAN ON CACHE BOOL "" FORCE)
  set(USE_OPENMP OFF CACHE BOOL "" FORCE)
  set(USE_PULSEAUDIO OFF CACHE BOOL "" FORCE)
  set(USE_LIBAO OFF CACHE BOOL "" FORCE)
  set(USE_DISCORD OFF CACHE BOOL "" FORCE)
  set(USE_BREAKPAD OFF CACHE BOOL "" FORCE)
  set(USE_HOST_SDL ON CACHE BOOL "" FORCE)
  set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    set(USE_ALSA ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(engines/flycast)
  ax_bind_retro_engine(flycast)
endif()

if(AX_BUILD_HATARI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/hatari/CMakeLists.txt")
  set(ENABLE_HATARI ON CACHE BOOL "" FORCE)
  set(ENABLE_DSP_EMU ON CACHE BOOL "" FORCE)
  set(ENABLE_LIBRETRO OFF CACHE BOOL "" FORCE)
  set(ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
  set(ENABLE_MAN_PAGES OFF CACHE BOOL "" FORCE)
  set(ENABLE_SDL3 OFF CACHE BOOL "" FORCE)
  add_subdirectory(engines/hatari)
  ax_bind_retro_engine(hatari)
endif()

if(GENX_BUILD_SNES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/snes/CMakeLists.txt")
  add_subdirectory(src/snes)
  add_executable(genx-snes-ax src/snes/genx_snes_main.cpp)
  target_link_libraries(genx-snes-ax PRIVATE ax_genx_snes)
  ax_bind_retro_engine(genx-snes-ax)
endif()
