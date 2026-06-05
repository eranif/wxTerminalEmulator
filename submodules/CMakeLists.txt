cmake_minimum_required(VERSION 3.10)
project(libtsm C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LIBTSM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libtsm)

# Generate config.h (BUILD_ENABLE_DEBUG off by default)
set(BUILD_ENABLE_DEBUG 0)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/libtsm_config.h.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

# Sources
set(TSM_SOURCES
  ${LIBTSM_DIR}/src/tsm/tsm-render.c
  ${LIBTSM_DIR}/src/tsm/tsm-screen.c
  ${LIBTSM_DIR}/src/tsm/tsm-selection.c
  ${LIBTSM_DIR}/src/tsm/tsm-unicode.c
  ${LIBTSM_DIR}/src/tsm/tsm-vte-charsets.c
  ${LIBTSM_DIR}/src/tsm/tsm-vte.c
  ${LIBTSM_DIR}/src/shared/shl-htable.c
  ${LIBTSM_DIR}/external/wcwidth/wcwidth.c
)

add_library(tsm STATIC ${TSM_SOURCES})

target_include_directories(tsm
  PUBLIC
    ${LIBTSM_DIR}/src/tsm
  PRIVATE
    ${LIBTSM_DIR}/src/shared
    ${LIBTSM_DIR}/external/wcwidth
    ${LIBTSM_DIR}/external
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(tsm PRIVATE
  _GNU_SOURCE
  _POSIX_C_SOURCE=200809L
)

# Force-include config.h like meson does
target_compile_options(tsm PRIVATE
  -include ${CMAKE_CURRENT_BINARY_DIR}/config.h
  -fvisibility=hidden
)

set_target_properties(tsm PROPERTIES POSITION_INDEPENDENT_CODE ON)
