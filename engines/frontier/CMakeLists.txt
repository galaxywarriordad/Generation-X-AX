cmake_minimum_required(VERSION 3.22)
project(frontier_ax LANGUAGES C CXX)

find_package(SDL2 QUIET)
find_package(OpenGL QUIET)

file(GLOB FRONTIER_C_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

if(FRONTIER_C_SOURCES)
  add_executable(frontier
    ${FRONTIER_C_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/ax_frontier_bridge.cpp"
  )
else()
  add_executable(frontier
    "${CMAKE_CURRENT_SOURCE_DIR}/ax_frontier_bridge.cpp"
  )
  target_compile_definitions(frontier PRIVATE AX_FRONTIER_STUB_MAIN=1)
endif()

set_target_properties(frontier PROPERTIES
  OUTPUT_NAME "frontier-fe2"
)

target_include_directories(frontier
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/shared"
)

if(TARGET ax_retro_bridge)
  target_link_libraries(frontier PRIVATE ax_retro_bridge)
endif()

if(TARGET SDL2::SDL2)
  target_link_libraries(frontier PRIVATE SDL2::SDL2)
endif()

if(TARGET OpenGL::GL)
  target_link_libraries(frontier PRIVATE OpenGL::GL)
endif()

if(UNIX AND NOT APPLE)
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    target_link_libraries(frontier PRIVATE ALSA::ALSA)
    target_compile_definitions(frontier PRIVATE AX_FRONTIER_HAS_ALSA=1)
  endif()
endif()
