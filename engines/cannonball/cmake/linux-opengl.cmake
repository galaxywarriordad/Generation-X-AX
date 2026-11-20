# Linux OpenGL Target for CannonBall (Piccolo-AX / Generation-X-AX)
set(OPENGL ON)
set(OPENGLES OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=skylake -mtune=skylake")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -march=skylake -mtune=skylake")

find_package(OpenGL REQUIRED)

set(platform_link_dirs "")
set(platform_link_libs
    OpenGL::GL
    pthread
    dl
)
