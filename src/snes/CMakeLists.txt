add_library(ax_genx_snes STATIC
    ax_snes_adapter.cpp
)

target_include_directories(ax_genx_snes
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(TARGET ax::core)
    target_link_libraries(ax_genx_snes PUBLIC ax::core)
    target_compile_definitions(ax_genx_snes PUBLIC GENX_SNES_HAS_AX_SDK=1)
endif()

target_compile_features(ax_genx_snes PUBLIC cxx_std_20)
