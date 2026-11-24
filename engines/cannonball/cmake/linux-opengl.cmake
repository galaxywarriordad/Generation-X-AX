# Linux OpenGL Target for CannonBall (Piccolo-AX / Generation-X-AX)
set(OPENGL ON)
set(OPENGLES OFF)

option(CANNONBALL_SKYLAKELOCK "Enable -march=skylake -mbmi -mbmi2 on 64-bit Linux" ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CANNONBALL_SKYLAKELOCK)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=skylake -mtune=skylake -mbmi -mbmi2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=skylake -mtune=skylake -mbmi -mbmi2")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

find_package(OpenGL REQUIRED)

set(platform_link_dirs "")
set(platform_link_libs
    OpenGL::GL
    pthread
    dl
)
