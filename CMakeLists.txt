cmake_minimum_required(VERSION 3.10)
project(wxterminal_lib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_WXTERMINAL_DEMO "Build the wxterminal demo application" ON)

# find wxWidgets once
if(MINGW)
  if(NOT WXWIN)
    message(FATAL_ERROR "Missing -DWXWIN=<wxWidgets/root/install/dir>")
  endif()
  if(NOT WXCFG)
    set(WXCFG "clang_x64_dll/mswu")
  endif()

  message(STATUS "WXWIN is set to ${WXWIN}")
  message(STATUS "WXCFG is set to ${WXCFG}")

  execute_process(
    COMMAND wx-config --cmake --prefix=${WXWIN} --wxcfg=${WXCFG}
    OUTPUT_VARIABLE wxWidgets_USE_FILE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  message(STATUS "wxWidgets_USE_FILE is set to: ${wxWidgets_USE_FILE}")
  include(${CMAKE_BINARY_DIR}/wxWidgets.cmake)
else()
  find_package(
    wxWidgets
    COMPONENTS base core net
    REQUIRED)
  include(${wxWidgets_USE_FILE})
  # Find wxWidgets
  find_package(wxWidgets REQUIRED COMPONENTS core base)
endif()
add_compile_definitions(-DwxDEBUG_LEVEL=0)
# Source files
set(LIB_SOURCES terminal_core.cpp terminal_logger.cpp terminal_view.cpp
                terminal_event.cpp)

set(EXAMPLE_SOURCES main.cpp app_persistence.cpp)
if(MINGW)
  list(APPEND EXAMPLE_SOURCES wx.rc)
endif()

if(WIN32)
  list(APPEND LIB_SOURCES pty_backend_windows.cpp)
  list(APPEND HEADER_SOURCES pty_backend_windows.h)
else()
  list(APPEND LIB_SOURCES pty_backend_posix.cpp)
  list(APPEND HEADER_SOURCES pty_backend_posix.h)
endif()

# Common headers can be listed here if needed
set(HEADER_SOURCES)

# Create library
add_library(wxterminal_lib STATIC ${LIB_SOURCES} ${HEADER_SOURCES})
target_link_libraries(wxterminal_lib PUBLIC ${wxWidgets_LIBRARIES})
if(APPLE)
  target_link_libraries(wxterminal_lib PUBLIC "-framework CoreText"
                                              "-framework CoreGraphics")
endif()

# Build library with PIC when required (useful when linking into other
# binaries/shared libs)
set_target_properties(wxterminal_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Create example program
if(BUILD_WXTERMINAL_DEMO)
  if(MINGW)
    add_executable(wxterminal WIN32 ${EXAMPLE_SOURCES})
  elseif(APPLE)
    add_executable(wxterminal MACOSX_BUNDLE ${EXAMPLE_SOURCES})
  else()
    add_executable(wxterminal ${EXAMPLE_SOURCES})
  endif()

  target_link_libraries(wxterminal PRIVATE wxterminal_lib)

  # Platform specific settings
  if(WIN32)
    target_compile_definitions(wxterminal PRIVATE __WXMSW__)
    target_link_libraries(wxterminal PRIVATE shell32 ole32 user32
                                             ${wxWidgets_LIBRARIES})
  elseif(NOT APPLE)
    # Linux needs libutil for forkpty
    target_link_libraries(wxterminal PRIVATE util ${wxWidgets_LIBRARIES})
  endif()
endif()
