cmake_minimum_required(VERSION 3.25)
project(Generation-X-AX VERSION 1.36.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GENX_AX_USE_AX_SDK "Link ax::core from ax-sdk" ON)

if(GENX_AX_USE_AX_SDK AND NOT TARGET ax::core)
    include(FetchContent)
    set(AX_SDK_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk")
    if(EXISTS "${AX_SDK_LOCAL_DIR}/CMakeLists.txt")
        add_subdirectory("${AX_SDK_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/_deps/ax_sdk-build" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(
            ax_sdk
            GIT_REPOSITORY https://github.com/galaxywarriordad/ax-sdk.git
            GIT_TAG v1_36_2
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(ax_sdk)
    endif()
endif()

set(_genx_ax_persist_src "${CMAKE_CURRENT_SOURCE_DIR}/src/ax_retro_save_callers.cpp")
if(EXISTS "${_genx_ax_persist_src}")
    add_library(genx_ax_persist STATIC
        "${_genx_ax_persist_src}"
    )
    target_compile_features(genx_ax_persist PRIVATE cxx_std_20)
    target_include_directories(genx_ax_persist PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    if(GENX_AX_USE_AX_SDK AND TARGET ax::core)
        target_link_libraries(genx_ax_persist PUBLIC ax::core)
    endif()

    foreach(_genx_target IN ITEMS generation_x_ax genx_ax rm_g blastem cannonball flycast hatari frontier_ax)
        if(TARGET "${_genx_target}")
            target_link_libraries("${_genx_target}" PRIVATE genx_ax_persist)
        endif()
    endforeach()
endif()

# If your main Generation-X-AX target uses a custom name, link it here:
# if(TARGET <your_genx_target> AND TARGET genx_ax_persist)
#     target_link_libraries(<your_genx_target> PRIVATE genx_ax_persist)
# endif()
```[cite: 26]

Do you have `src/ax_retro_save.hpp` or `src/ax_retro_save_callers.cpp` for `Generation-X-AX` to check next, or are you moving on to `Amiberry-AX`?
