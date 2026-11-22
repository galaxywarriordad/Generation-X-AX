cmake_minimum_required(VERSION 3.22)
project(GenerationXAX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AX_BUILD_RMG "Build RMG (Nintendo 64) engine" ON)
option(AX_BUILD_BLASTEM "Build BlastEm (Sega Mega Drive) engine" ON)
option(AX_BUILD_CANNONBALL "Build Cannonball (1986 OutRun) engine" ON)
option(AX_BUILD_EDUKE32 "Build EDuke32 FPS engine" ON)
option(AX_BUILD_FRONTIER "Build Frontier: Elite II engine" ON)
option(AX_BUILD_FLYCAST "Build Flycast (Dreamcast / NAOMI / Atomiswave) engine" ON)
option(AX_BUILD_HATARI "Build Hatari (Atari ST) engine" ON)
option(AX_FETCH_SDK "Fetch ax-sdk v1_36_1 if AX_SDK_DIR is not set" ON)

set(AX_SDK_DIR "" CACHE PATH "Path to local ax-sdk checkout")

if(NOT AX_SDK_DIR AND DEFINED ENV{AX_SDK_DIR} AND EXISTS "$ENV{AX_SDK_DIR}/CMakeLists.txt")
  set(AX_SDK_DIR "$ENV{AX_SDK_DIR}" CACHE PATH "Path to local ax-sdk checkout" FORCE)
elseif(NOT AX_SDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk/CMakeLists.txt")
  set(AX_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk" CACHE PATH "Path to local ax-sdk checkout" FORCE)
endif()

if(AX_SDK_DIR AND EXISTS "${AX_SDK_DIR}/CMakeLists.txt")
  add_subdirectory("${AX_SDK_DIR}" "${CMAKE_BINARY_DIR}/ax-sdk-build")
elseif(AX_FETCH_SDK)
  include(FetchContent)
  set(_ax_proto "https")
  set(_ax_host "github.com")
  set(_ax_repo "galaxywarriordad/ax-sdk.git")
  FetchContent_Declare(
    ax_sdk
    GIT_REPOSITORY "${_ax_proto}://${_ax_host}/${_ax_repo}"
    GIT_TAG "v1_36_1"
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(ax_sdk)
endif()

add_library(ax_retro_bridge STATIC
  shared/ax_retro_bridge.cpp
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

if(AX_BUILD_RMG AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/rmg/CMakeLists.txt")
  add_subdirectory(engines/rmg)
  if(TARGET rmg)
    target_link_libraries(rmg PRIVATE ax_retro_bridge)
  endif()
endif()

if(AX_BUILD_BLASTEM AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/blastem/CMakeLists.txt")
  add_subdirectory(engines/blastem)
  if(TARGET blastem)
    target_link_libraries(blastem PRIVATE ax_retro_bridge)
  endif()
endif()

if(AX_BUILD_CANNONBALL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/cannonball/cmake/CMakeLists.txt")
  add_subdirectory(engines/cannonball/cmake)
  if(TARGET cannonball)
    target_link_libraries(cannonball PRIVATE ax_retro_bridge)
  endif()
endif()

if(AX_BUILD_EDUKE32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/eduke32/CMakeLists.txt")
  add_subdirectory(engines/eduke32)
  if(TARGET eduke32)
    target_link_libraries(eduke32 PRIVATE ax_retro_bridge)
  endif()
endif()

if(AX_BUILD_FRONTIER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/frontier/CMakeLists.txt")
  add_subdirectory(engines/frontier)
  if(TARGET frontier)
    target_link_libraries(frontier PRIVATE ax_retro_bridge)
  endif()
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
  if(TARGET flycast)
    target_link_libraries(flycast PRIVATE ax_retro_bridge)
  endif()
endif()

if(AX_BUILD_HATARI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/hatari/CMakeLists.txt")
  set(ENABLE_HATARI ON CACHE BOOL "" FORCE)
  set(ENABLE_DSP_EMU ON CACHE BOOL "" FORCE)
  set(ENABLE_LIBRETRO OFF CACHE BOOL "" FORCE)
  set(ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
  set(ENABLE_MAN_PAGES OFF CACHE BOOL "" FORCE)
  set(ENABLE_SDL3 OFF CACHE BOOL "" FORCE)
  add_subdirectory(engines/hatari)
  if(TARGET hatari)
    target_link_libraries(hatari PRIVATE ax_retro_bridge)
  endif()
endif()
```[cite: 18]

<ElicitationsGroup message="Ready for #5 of 5 — shall we finish with Cemu-Latte-AX next?">
  <Elicitation label="5. Cemu-Latte CMake" query="next"/>
  <Elicitation label="Review Generation-X CMake" query="Let me know if any engine target names in Generation-X-AX differ from rmg/blastem/cannonball/eduke32/frontier."/>
  <Elicitation label="Check Piccolo spelling" query="Let's also note the Piccolo vs Picollo spelling check for your repos and scripts."/>
</ElicitationsGroup>
