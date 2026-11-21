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
option(AX_FETCH_SDK "Fetch ax-sdk v1_36_1 if AX_SDK_DIR is not set" ON)

set(AX_SDK_DIR "" CACHE PATH "Path to local ax-sdk checkout")

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
    GIT_SHALLOW TRUE
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
endif()

if(AX_BUILD_BLASTEM AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/blastem/CMakeLists.txt")
  add_subdirectory(engines/blastem)
endif()

if(AX_BUILD_CANNONBALL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/cannonball/cmake/CMakeLists.txt")
  add_subdirectory(engines/cannonball/cmake)
endif()

if(AX_BUILD_EDUKE32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/eduke32/CMakeLists.txt")
  add_subdirectory(engines/eduke32)
endif()

if(AX_BUILD_FRONTIER AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/frontier/CMakeLists.txt")
  add_subdirectory(engines/frontier)
endif()

if(AX_BUILD_FLYCAST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/engines/flycast/CMakeLists.txt")
  set(USE_VULKAN ON CACHE BOOL "" FORCE)
  set(USE_OPENMP OFF CACHE BOOL "" FORCE)
  set(USE_PULSEAUDIO OFF CACHE BOOL "" FORCE)
  set(USE_DISCORD OFF CACHE BOOL "" FORCE)
  set(USE_BREAKPAD OFF CACHE BOOL "" FORCE)
  set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
  set(ENABLE_LTO ON CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    set(USE_ALSA ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(engines/flycast)
  if(TARGET flycast)
    target_link_libraries(flycast PRIVATE ax_retro_bridge)
  endif()
endif()
