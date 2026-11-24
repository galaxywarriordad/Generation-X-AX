option(GENX_SNES_ENABLE_ARES_CORE "Enable ares SNES core backend hook (GENX_SNES_HAS_ARES_CORE)" OFF)
set(GENX_SNES_ARES_SOURCE_DIR "" CACHE PATH "Optional path to ares source tree for SNES core integration")

add_library(ax_genx_snes STATIC
    ax_snes_adapter.cpp
    snes_core_host.cpp
)

target_include_directories(ax_genx_snes
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(GENX_SNES_ENABLE_ARES_CORE)
    target_compile_definitions(ax_genx_snes PUBLIC GENX_SNES_HAS_ARES_CORE=1)
    if(GENX_SNES_ARES_SOURCE_DIR AND EXISTS "${GENX_SNES_ARES_SOURCE_DIR}")
        target_include_directories(ax_genx_snes PRIVATE "${GENX_SNES_ARES_SOURCE_DIR}")
    endif()
endif()

if(TARGET ax::core)
    target_link_libraries(ax_genx_snes PUBLIC ax::core)
    target_compile_definitions(ax_genx_snes PUBLIC GENX_SNES_HAS_AX_SDK=1)
elseif(TARGET ax_core)
    target_link_libraries(ax_genx_snes PUBLIC ax_core)
    target_compile_definitions(ax_genx_snes PUBLIC GENX_SNES_HAS_AX_SDK=1)
endif()

target_compile_features(ax_genx_snes PUBLIC cxx_std_20)
