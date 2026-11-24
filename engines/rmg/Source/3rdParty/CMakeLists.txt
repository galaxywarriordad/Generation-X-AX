#
# 3rdParty CMakeLists.txt — Deterministic & Skylake/BMI2 Optimised
#
include(ExternalProject)
include(ProcessorCount)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NETPLAY "Enable netplay support via SDL3_net" OFF)
option(USE_CCACHE "Use ccache compiler launcher when available" ON)
option(USE_ANGRYLION "Build angrylion-plus software RDP plugin" OFF)
option(AX_ENABLE_SKYLAKELOCK "Lock x86_64 builds to -march=skylake -mbmi -mbmi2" ON)
option(AX_ENABLE_DETERMINISTIC "Enable deterministic build flags and prefix maps" ON)

ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 4)
endif()
if(NPROC GREATER 4)
    # Cap parallel make jobs at 4 to preserve L3 cache per core during heavy C/C++ builds
    set(NPROC 4)
endif()

set(M64P_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-core")
if(WIN32)
    set(SO_EXT "dll")
    set(GCA_LIB "mupen64plus_input_gca.${SO_EXT}")
    set(CORE_FILE "mupen64plus.dll")
else()
    set(SO_EXT "so")
    set(GCA_LIB "libmupen64plus_input_gca.${SO_EXT}")
    set(CORE_FILE "libmupen64plus.so")
endif()

set(AX_EXTRA_C_FLAGS "")
set(AX_EXTRA_CXX_FLAGS "")
set(AX_EXTRA_LD_FLAGS "")

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND AX_ENABLE_SKYLAKELOCK)
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        list(APPEND AX_EXTRA_C_FLAGS "/clang:-march=skylake" "/clang:-mbmi" "/clang:-mbmi2")
        list(APPEND AX_EXTRA_CXX_FLAGS "/clang:-march=skylake" "/clang:-mbmi" "/clang:-mbmi2")
    else()
        list(APPEND AX_EXTRA_C_FLAGS "-march=skylake" "-mbmi" "-mbmi2")
        list(APPEND AX_EXTRA_CXX_FLAGS "-march=skylake" "-mbmi" "-mbmi2")
    endif()
endif()

if(AX_ENABLE_DETERMINISTIC)
    if(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
        list(APPEND AX_EXTRA_C_FLAGS "/Brepro" "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
        list(APPEND AX_EXTRA_CXX_FLAGS "/Brepro" "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
        list(APPEND AX_EXTRA_LD_FLAGS "/Brepro")
    else()
        list(APPEND AX_EXTRA_C_FLAGS "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
        list(APPEND AX_EXTRA_CXX_FLAGS "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
        if(UNIX AND NOT APPLE)
            list(APPEND AX_EXTRA_LD_FLAGS "-Wl,--build-id=sha1")
        endif()
    endif()
endif()

list(JOIN AX_EXTRA_C_FLAGS " " AX_JOINED_EXTRA_C)
list(JOIN AX_EXTRA_CXX_FLAGS " " AX_JOINED_EXTRA_CXX)
list(JOIN AX_EXTRA_LD_FLAGS " " AX_JOINED_EXTRA_LD)

set(COMBINED_C_FLAGS "${CMAKE_C_FLAGS} ${AX_JOINED_EXTRA_C}")
set(COMBINED_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${AX_JOINED_EXTRA_CXX}")
string(STRIP "${COMBINED_C_FLAGS}" COMBINED_C_FLAGS)
string(STRIP "${COMBINED_CXX_FLAGS}" COMBINED_CXX_FLAGS)

set(BASE_CMAKE_CONFIGURE_ARGS
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_C_FLAGS=${COMBINED_C_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${COMBINED_CXX_FLAGS}"
)

if(USE_CCACHE)
    find_program(CCACHE_EXECUTABLE ccache)
    if(CCACHE_EXECUTABLE)
        set(CMAKE_CONFIGURE_ARGS
            ${BASE_CMAKE_CONFIGURE_ARGS}
            "-DCMAKE_C_COMPILER_LAUNCHER=${CCACHE_EXECUTABLE}"
            "-DCMAKE_CXX_COMPILER_LAUNCHER=${CCACHE_EXECUTABLE}"
        )
        set(MAKE_CC_COMPILER "${CCACHE_EXECUTABLE} ${CMAKE_C_COMPILER}")
        set(MAKE_CXX_COMPILER "${CCACHE_EXECUTABLE} ${CMAKE_CXX_COMPILER}")
    else()
        set(CMAKE_CONFIGURE_ARGS ${BASE_CMAKE_CONFIGURE_ARGS})
        set(MAKE_CC_COMPILER "${CMAKE_C_COMPILER}")
        set(MAKE_CXX_COMPILER "${CMAKE_CXX_COMPILER}")
    endif()
else()
    set(CMAKE_CONFIGURE_ARGS ${BASE_CMAKE_CONFIGURE_ARGS})
    set(MAKE_CC_COMPILER "${CMAKE_C_COMPILER}")
    set(MAKE_CXX_COMPILER "${CMAKE_CXX_COMPILER}")
endif()

if("${CMAKE_GENERATOR}" STREQUAL "Unix Makefiles" OR
   "${CMAKE_GENERATOR}" STREQUAL "MSYS Makefiles")
    set(MAKE_CMD "$(MAKE)")
else()
    find_program(MAKE_CMD NAMES gmake make)
    if(NOT MAKE_CMD)
        set(MAKE_CMD "make")
    endif()
endif()

if("${COMBINED_C_FLAGS}" STREQUAL "")
    set(MAKE_OPTFLAGS "-O3")
else()
    set(MAKE_OPTFLAGS "${COMBINED_C_FLAGS}")
endif()

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(MAKE_DEBUG "1")
else()
    set(MAKE_DEBUG "0")
    if("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
        set(MAKE_OPTFLAGS "${MAKE_OPTFLAGS} -g")
    endif()
endif()

set(SDL3NET_LDLIBS "")
set(SDL3NET_CFLAGS "")
set(EFFECTIVE_NETPLAY "0")

if(NETPLAY)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL3_net QUIET sdl3-net)
        if(SDL3_net_FOUND)
            set(EFFECTIVE_NETPLAY "1")
            string(JOIN " " SDL3NET_LDLIBS ${SDL3_net_LDFLAGS})
            set(SDL3NET_CFLAGS "-DUSE_SDL3NET")
            if(SDL3_net_CFLAGS)
                string(JOIN " " _sdl3net_inc ${SDL3_net_CFLAGS})
                set(SDL3NET_CFLAGS "${SDL3NET_CFLAGS} ${_sdl3net_inc}")
            endif()
        else()
            message(WARNING "NETPLAY requested, but sdl3-net was not found via pkg-config; building 3rdParty with NETPLAY=0.")
        endif()
    endif()
endif()

ExternalProject_Add(mupen64plus-core
    SOURCE_DIR "${M64P_CORE_DIR}/"
    CONFIGURE_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_COMMAND "${MAKE_CMD}" "-j${NPROC}" "all"
        "-f" "${M64P_CORE_DIR}/projects/unix/Makefile"
        "SRCDIR=${M64P_CORE_DIR}/src"
        "SUBDIR=${M64P_CORE_DIR}/subprojects"
        "OSD=0"
        "NEW_DYNAREC=1"
        "NO_ASM=$<BOOL:${NO_ASM}>"
        "KEYBINDINGS=0"
        "ACCURATE_FPU=1"
        "VULKAN=0"
        "NETPLAY=${EFFECTIVE_NETPLAY}"
        "TARGET=${CORE_FILE}"
        "DEBUG=${MAKE_DEBUG}"
        "CC=${MAKE_CC_COMPILER}"
        "CXX=${MAKE_CXX_COMPILER}"
        "OPTFLAGS=${MAKE_OPTFLAGS}"
        "SDLNET_CFLAGS=${SDL3NET_CFLAGS}"
        "SDLNET_LDLIBS=${SDL3NET_LDLIBS}"
    BUILD_IN_SOURCE FALSE
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-core"
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-core/${CORE_FILE}"
)

set(APIDIR "${CMAKE_SOURCE_DIR}/Source/3rdParty/mupen64plus-core/src/api")

ExternalProject_Add(mupen64plus-rsp-cxd4
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-rsp-cxd4/"
    CONFIGURE_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_COMMAND "${MAKE_CMD}" "-j${NPROC}" "all"
        "-f" "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-rsp-cxd4/projects/unix/Makefile"
        "SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-rsp-cxd4"
        "APIDIR=${APIDIR}"
        "DEBUG=${MAKE_DEBUG}"
        "POSTFIX="
        "CC=${MAKE_CC_COMPILER}"
        "CXX=${MAKE_CXX_COMPILER}"
        "OPTFLAGS=${MAKE_OPTFLAGS}"
    BUILD_IN_SOURCE FALSE
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-rsp-cxd4"
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-rsp-cxd4/mupen64plus-rsp-cxd4.${SO_EXT}"
)

ExternalProject_Add(mupen64plus-rsp-hle
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-rsp-hle/"
    CONFIGURE_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_COMMAND "${MAKE_CMD}" "-j${NPROC}" "all"
        "-f" "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-rsp-hle/projects/unix/Makefile"
        "SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-rsp-hle/src"
        "APIDIR=${APIDIR}"
        "DEBUG=${MAKE_DEBUG}"
        "CC=${MAKE_CC_COMPILER}"
        "CXX=${MAKE_CXX_COMPILER}"
        "OPTFLAGS=${MAKE_OPTFLAGS}"
    BUILD_IN_SOURCE FALSE
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-rsp-hle"
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-rsp-hle/mupen64plus-rsp-hle.${SO_EXT}"
)

ExternalProject_Add(mupen64plus-rsp-parallel
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-rsp-parallel"
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE FALSE
    CMAKE_ARGS -DUSE_IPO=OFF ${CMAKE_CONFIGURE_ARGS}
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-rsp-parallel"
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-rsp-parallel/mupen64plus-rsp-parallel.${SO_EXT}"
)

ExternalProject_Add(mupen64plus-input-raphnetraw
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-input-raphnetraw/"
    CONFIGURE_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_COMMAND "${MAKE_CMD}" "-j${NPROC}" "all"
        "-f" "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-input-raphnetraw/projects/unix/Makefile"
        "SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-input-raphnetraw/src"
        "APIDIR=${APIDIR}"
        "DEBUG=${MAKE_DEBUG}"
        "CC=${MAKE_CC_COMPILER}"
        "CXX=${MAKE_CXX_COMPILER}"
        "OPTFLAGS=${MAKE_OPTFLAGS}"
    BUILD_IN_SOURCE FALSE
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-input-raphnetraw"
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-input-raphnetraw/mupen64plus-input-raphnetraw.${SO_EXT}"
)

set(GLIDEN64_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-video-GLideN64")
set(GLIDEN64_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-GLideN64")
set(GLIDEN64_CUSTOM_INI "${GLIDEN64_DIR}/ini/GLideN64.custom.ini")

if("${CMAKE_BUILD_TYPE}" STREQUAL "Release" OR
   "${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel" OR
   "${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    set(GLIDEN64_BUILD_TYPE Release)
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(GLIDEN64_BUILD_TYPE Debug)
else()
    set(GLIDEN64_BUILD_TYPE Release)
endif()

if(USE_ANGRYLION)
    ExternalProject_Add(mupen64plus-video-angrylion-plus
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-video-angrylion-plus"
        INSTALL_COMMAND ""
        BUILD_IN_SOURCE FALSE
        CMAKE_ARGS -DBUILD_MUPEN64PLUS=ON -DBUILD_PROJECT64=OFF -DUSE_QT5=OFF -DUSE_IPO=OFF ${CMAKE_CONFIGURE_ARGS}
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-angrylion-plus"
        BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-angrylion-plus/mupen64plus-video-angrylion-plus.${SO_EXT}"
    )
endif()

ExternalProject_Add(mupen64plus-video-GLideN64
    SOURCE_DIR "${GLIDEN64_DIR}"
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE FALSE
    CMAKE_ARGS -DMUPENPLUSAPI=ON -DMUPENPLUSAPI_GLIDENUI=ON -DGLIDENUI_QT6=ON -DUSE_IPO=OFF -DUSE_SYSTEM_LIBS=ON ${CMAKE_CONFIGURE_ARGS}
    SOURCE_SUBDIR ./src/
    BINARY_DIR "${GLIDEN64_BUILD_DIR}"
    BUILD_BYPRODUCTS "${GLIDEN64_BUILD_DIR}/plugin/${GLIDEN64_BUILD_TYPE}/mupen64plus-video-GLideN64.${SO_EXT}"
)

ExternalProject_Add(mupen64plus-video-parallel
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-video-parallel"
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE FALSE
    CMAKE_ARGS -DCONFIG_GUI=ON -DUSE_IPO=OFF ${CMAKE_CONFIGURE_ARGS}
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-parallel"
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-parallel/mupen64plus-video-parallel.${SO_EXT}"
)

set(SOFTRDP_MAKE_OPTFLAGS "-fPIC ${MAKE_OPTFLAGS}")
string(STRIP "${SOFTRDP_MAKE_OPTFLAGS}" SOFTRDP_MAKE_OPTFLAGS)
ExternalProject_Add(mupen64plus-video-softrdp
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-video-softrdp/"
    CONFIGURE_COMMAND ""
    INSTALL_COMMAND ""
    BUILD_COMMAND "${MAKE_CMD}" "-j${NPROC}" "mupen64plus"
        "-f" "${CMAKE_CURRENT_SOURCE_DIR}/mupen64plus-video-softrdp/Makefile"
        "CC=${MAKE_CC_COMPILER}"
        "CXX=${MAKE_CXX_COMPILER}"
        "CFLAGS=${SOFTRDP_MAKE_OPTFLAGS}"
        "LDFLAGS=${SOFTRDP_MAKE_OPTFLAGS} ${AX_JOINED_EXTRA_LD}"
        "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-softrdp"
        "OBJ_DIR=${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-softrdp"
        "WINDOWS=0"
    BUILD_IN_SOURCE TRUE
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-softrdp"
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/mupen64plus-video-softrdp/mupen64plus-video-softrdp.${SO_EXT}"
)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui
    PUBLIC
        "${IMGUI_DIR}"
        "${IMGUI_DIR}/backends"
)

set(SDL_GAMECONTROLLERDB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDL_GameControllerDB")
set(SDL_GAMECONTROLLERDB_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/SDL_GameControllerDB")
set(SDL_GAMECONTROLLERDB_SOURCES_IN "${SDL_GAMECONTROLLERDB_DIR}/gamecontrollerdb.txt")
set(SDL_GAMECONTROLLERDB_SOURCES_OUT "${SDL_GAMECONTROLLERDB_BUILD_DIR}/gamecontrollerdb.txt")

ExternalProject_Add(SDL_GameControllerDB
    SOURCE_DIR "${SDL_GAMECONTROLLERDB_DIR}"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDL_GAMECONTROLLERDB_BUILD_DIR}"
    BUILD_COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SDL_GAMECONTROLLERDB_SOURCES_IN}" "${SDL_GAMECONTROLLERDB_SOURCES_OUT}"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${SDL_GAMECONTROLLERDB_SOURCES_OUT}"
)

ExternalProject_Get_property(mupen64plus-core BUILD_BYPRODUCTS)
set(MUPEN64PLUSCORE_LIB ${BUILD_BYPRODUCTS} PARENT_SCOPE)
set(MUPEN64PLUSCORE_INI "${M64P_CORE_DIR}/data/mupen64plus.ini" PARENT_SCOPE)

ExternalProject_Get_property(mupen64plus-rsp-cxd4 BUILD_BYPRODUCTS)
set(MUPEN64PLUS_PLUGIN_RSP_CXD4 ${BUILD_BYPRODUCTS} PARENT_SCOPE)

ExternalProject_Get_property(mupen64plus-rsp-hle BUILD_BYPRODUCTS)
set(MUPEN64PLUS_PLUGIN_RSP_HLE ${BUILD_BYPRODUCTS} PARENT_SCOPE)

ExternalProject_Get_property(mupen64plus-rsp-parallel BUILD_BYPRODUCTS)
set(MUPEN64PLUS_PLUGIN_RSP_PARALLEL ${BUILD_BYPRODUCTS} PARENT_SCOPE)

ExternalProject_Get_property(mupen64plus-input-raphnetraw BUILD_BYPRODUCTS)
set(MUPEN64PLUS_PLUGIN_INPUT_RAPHNET ${BUILD_BYPRODUCTS} PARENT_SCOPE)

if(USE_ANGRYLION)
    ExternalProject_Get_property(mupen64plus-video-angrylion-plus BUILD_BYPRODUCTS)
    set(MUPEN64PLUS_PLUGIN_GFX_ANGRYLION ${BUILD_BYPRODUCTS} PARENT_SCOPE)
endif()

ExternalProject_Get_property(mupen64plus-video-GLideN64 BUILD_BYPRODUCTS)
set(MUPEN64PLUS_PLUGIN_GFX_GLIDEN64 ${BUILD_BYPRODUCTS} PARENT_SCOPE)
set(MUPEN64PLUS_PLUGIN_GFX_GLIDEN64_DATA "${GLIDEN64_CUSTOM_INI}" PARENT_SCOPE)

ExternalProject_Get_property(mupen64plus-video-parallel BUILD_BYPRODUCTS)
set(MUPEN64PLUS_PLUGIN_GFX_PARALLEL ${BUILD_BYPRODUCTS} PARENT_SCOPE)

ExternalProject_Get_property(mupen64plus-video-softrdp BUILD_BYPRODUCTS)
set(MUPEN64PLUS_PLUGIN_GFX_SOFTRDP ${BUILD_BYPRODUCTS} PARENT_SCOPE)

set(IMGUI_DIR "${IMGUI_DIR}" PARENT_SCOPE)

ExternalProject_Get_property(SDL_GameControllerDB BUILD_BYPRODUCTS)
set(SDL_GAMECONTROLLERDB ${BUILD_BYPRODUCTS} PARENT_SCOPE)
