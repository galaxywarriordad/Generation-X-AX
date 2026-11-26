cmake_minimum_required(VERSION 3.22)

project(pcsx2_ax
    VERSION 0.1.0
    DESCRIPTION "PCSX2_AX shared core and mode adapters (Prototype Phase)"
    LANGUAGES C CXX
)

option(PCSX2_AX_PLANNING_ONLY "Keep PCSX2_AX in Prototype Phase guardrails" ON)
option(PCSX2_AX_ENABLE_VULKAN_1_3 "Enable Vulkan 1.3 GS offload hooks" ON)
option(PCSX2_AX_BUILD_HARNESS "Build CLI prototype harness" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Locate ax-sdk v1_36_2 (in-tree under Generation-X-AX or via AX_SDK_DIR)
if(TARGET ax_sdk)
    set(PCSX2_AX_SDK_TARGET ax_sdk)
elseif(DEFINED AX_SDK_DIR AND EXISTS "${AX_SDK_DIR}/CMakeLists.txt")
    add_subdirectory("${AX_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ax_sdk_build" EXCLUDE_FROM_ALL)
    set(PCSX2_AX_SDK_TARGET ax_sdk)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../ax-sdk/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../ax-sdk" "${CMAKE_CURRENT_BINARY_DIR}/ax_sdk_build" EXCLUDE_FROM_ALL)
    set(PCSX2_AX_SDK_TARGET ax_sdk)
else()
    message(WARNING "ax-sdk (v1_36_2) target not found; building standalone prototype stubs only.")
endif()

# ---------------------------------------------------------------------------
# 1. Shared Deduplicated Core: libax_pcsx2_core (engines/pcsx2_ax/core)
# ---------------------------------------------------------------------------
set(PCSX2_AX_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core")

set(PCSX2_AX_CORE_HEADERS
    "${PCSX2_AX_CORE_DIR}/CDVDRingSlab.hpp"
    "${PCSX2_AX_CORE_DIR}/IHardwareModeAdapter.hpp"
    "${PCSX2_AX_CORE_DIR}/VMManager_Internal.h"
)

set(PCSX2_AX_CORE_SOURCES
    "${PCSX2_AX_CORE_DIR}/CDVDcommon.cpp"
    "${PCSX2_AX_CORE_DIR}/CDVDdiscThread.cpp"
    "${PCSX2_AX_CORE_DIR}/CDVDisoReader.cpp"
    "${PCSX2_AX_CORE_DIR}/GS.cpp"
    "${PCSX2_AX_CORE_DIR}/R5900OpcodeImpl.cpp"
    "${PCSX2_AX_CORE_DIR}/SPR.cpp"
    "${PCSX2_AX_CORE_DIR}/SPU2.cpp"
    "${PCSX2_AX_CORE_DIR}/SaveState.cpp"
    "${PCSX2_AX_CORE_DIR}/Sif.cpp"
    "${PCSX2_AX_CORE_DIR}/Sif0.cpp"
    "${PCSX2_AX_CORE_DIR}/Sif1.cpp"
    "${PCSX2_AX_CORE_DIR}/StateWrapper.cpp"
    "${PCSX2_AX_CORE_DIR}/VMManager.cpp"
    "${PCSX2_AX_CORE_DIR}/VMManager_Config.cpp"
    "${PCSX2_AX_CORE_DIR}/VMManager_Media.cpp"
    "${PCSX2_AX_CORE_DIR}/VMManager_SaveState.cpp"
    "${PCSX2_AX_CORE_DIR}/VU0.cpp"
    "${PCSX2_AX_CORE_DIR}/VU0micro.cpp"
    "${PCSX2_AX_CORE_DIR}/VU0microInterp.cpp"
    "${PCSX2_AX_CORE_DIR}/VU1micro.cpp"
    "${PCSX2_AX_CORE_DIR}/VU1microInterp.cpp"
    "${PCSX2_AX_CORE_DIR}/VUflags.cpp"
    "${PCSX2_AX_CORE_DIR}/VUmicro.cpp"
    "${PCSX2_AX_CORE_DIR}/VUmicroMem.cpp"
    "${PCSX2_AX_CORE_DIR}/VUops.cpp"
    "${PCSX2_AX_CORE_DIR}/Vif.cpp"
    "${PCSX2_AX_CORE_DIR}/Vif0_Dma.cpp"
    "${PCSX2_AX_CORE_DIR}/Vif1_Dma.cpp"
    "${PCSX2_AX_CORE_DIR}/Vif1_MFIFO.cpp"
    "${PCSX2_AX_CORE_DIR}/Vif_Transfer.cpp"
    "${PCSX2_AX_CORE_DIR}/Vif_Unpack.cpp"
    "${PCSX2_AX_CORE_DIR}/sif2.cpp"
    "${PCSX2_AX_CORE_DIR}/vtlb.cpp"
)

add_library(ax_pcsx2_core STATIC ${PCSX2_AX_CORE_SOURCES} ${PCSX2_AX_CORE_HEADERS})
add_library(PCSX2_AX::core ALIAS ax_pcsx2_core)
set_target_properties(ax_pcsx2_core PROPERTIES OUTPUT_NAME "ax_pcsx2_core")

target_include_directories(ax_pcsx2_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PCSX2_AX_CORE_DIR}"
        "${PCSX2_AX_CORE_DIR}/pcsx2"
        "${PCSX2_AX_CORE_DIR}/common"
        "${PCSX2_AX_CORE_DIR}/CDVD"
)

target_compile_definitions(ax_pcsx2_core
    PUBLIC
        PCSX2_AX_SDK_VERSION_TARGET="v1_36_2"
        PCSX2_AX_SHM_SYNC_DAEMON="K01piccolo-sync"
        PCSX2_AX_VRAM_PRUNE_GIB=5.00
        PCSX2_AX_VRAM_CEILING_GIB=5.50
        $<$<BOOL:${PCSX2_AX_PLANNING_ONLY}>:PCSX2_AX_PLANNING_ONLY=1>
        $<$<BOOL:${PCSX2_AX_ENABLE_VULKAN_1_3}>:PCSX2_AX_HAS_VULKAN_1_3=1>
)

if(UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ax_pcsx2_core PRIVATE
        -O2
        -Wall
        -Wextra
        -Wpedantic
        $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>:-march=skylake>
    )
elseif(MSVC)
    target_compile_options(ax_pcsx2_core PRIVATE /O2 /W4)
endif()

if(DEFINED PCSX2_AX_SDK_TARGET AND TARGET ${PCSX2_AX_SDK_TARGET})
    target_link_libraries(ax_pcsx2_core PUBLIC ${PCSX2_AX_SDK_TARGET})
endif()

# ---------------------------------------------------------------------------
# 2. Retail PS2 Mode Adapter: Ps2ConsoleAdapter (engines/pcsx2_ax/adapters/ps2)
# ---------------------------------------------------------------------------
set(PCSX2_AX_PS2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/adapters/ps2")

set(PCSX2_AX_PS2_SOURCES
    "${PCSX2_AX_PS2_DIR}/BiosTools.cpp"
    "${PCSX2_AX_PS2_DIR}/CDVD.cpp"
    "${PCSX2_AX_PS2_DIR}/CDVDdiscReader.cpp"
    "${PCSX2_AX_PS2_DIR}/IopBios.cpp"
    "${PCSX2_AX_PS2_DIR}/MemoryCardFile.cpp"
    "${PCSX2_AX_PS2_DIR}/MemoryCardFolder.cpp"
    "${PCSX2_AX_PS2_DIR}/MemoryCardProtocol.cpp"
    "${PCSX2_AX_PS2_DIR}/Pad.cpp"
    "${PCSX2_AX_PS2_DIR}/PadBase.cpp"
    "${PCSX2_AX_PS2_DIR}/PadDualshock1.cpp"
    "${PCSX2_AX_PS2_DIR}/PadDualshock2.cpp"
    "${PCSX2_AX_PS2_DIR}/PsxBios.cpp"
    "${PCSX2_AX_PS2_DIR}/Sio2.cpp"
)

add_library(ax_pcsx2_adapter_ps2 STATIC ${PCSX2_AX_PS2_SOURCES})
add_library(PCSX2_AX::ps2 ALIAS ax_pcsx2_adapter_ps2)

target_include_directories(ax_pcsx2_adapter_ps2
    PUBLIC
        "${PCSX2_AX_PS2_DIR}"
)

target_compile_definitions(ax_pcsx2_adapter_ps2
    PUBLIC
        PCSX2_AX_SAVE_ROOT_PS2="/userdata/saves/ps2/"
)

target_link_libraries(ax_pcsx2_adapter_ps2
    PUBLIC
        PCSX2_AX::core
)

# ---------------------------------------------------------------------------
# 3. COH-H / System 246 & 256 Adapter: CohHArcadeAdapter (engines/pcsx2_ax/adapters/coh_h)
# ---------------------------------------------------------------------------
set(PCSX2_AX_COHH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/adapters/coh_h")

set(PCSX2_AX_COHH_SOURCES
    "${PCSX2_AX_COHH_DIR}/ACATA.cpp"
    "${PCSX2_AX_COHH_DIR}/ACATAPI.cpp"
    "${PCSX2_AX_COHH_DIR}/ACATA_IO.cpp"
    "${PCSX2_AX_COHH_DIR}/ACATA_IO_CHD.cpp"
    "${PCSX2_AX_COHH_DIR}/ACCORE.cpp"
    "${PCSX2_AX_COHH_DIR}/ACJV.cpp"
    "${PCSX2_AX_COHH_DIR}/ACRAM.cpp"
    "${PCSX2_AX_COHH_DIR}/ACSRAM.cpp"
    "${PCSX2_AX_COHH_DIR}/ACUART.cpp"
    "${PCSX2_AX_COHH_DIR}/ACUART_GLUE.cpp"
)

add_library(ax_pcsx2_adapter_coh_h STATIC ${PCSX2_AX_COHH_SOURCES})
add_library(PCSX2_AX::coh_h ALIAS ax_pcsx2_adapter_coh_h)

target_include_directories(ax_pcsx2_adapter_coh_h
    PUBLIC
        "${PCSX2_AX_COHH_DIR}"
)

target_compile_definitions(ax_pcsx2_adapter_coh_h
    PUBLIC
        PCSX2_AX_SAVE_ROOT_COHH="/userdata/saves/pcsx2x6/"
)

target_link_libraries(ax_pcsx2_adapter_coh_h
    PUBLIC
        PCSX2_AX::core
)

# ---------------------------------------------------------------------------
# 4. Minimal CLI Prototype Harness
# ---------------------------------------------------------------------------
if(PCSX2_AX_BUILD_HARNESS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pcsx2_ax_harness.cpp")
    add_executable(pcsx2_ax_harness
        "${CMAKE_CURRENT_SOURCE_DIR}/pcsx2_ax_harness.cpp"
    )
    target_link_libraries(pcsx2_ax_harness
        PRIVATE
            PCSX2_AX::ps2
            PCSX2_AX::coh_h
    )
endif()
