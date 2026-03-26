cmake_minimum_required(VERSION 3.10)
project(EmbeddedCmdDemo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Source files
set(SOURCES
  main.cpp
  terminal_core.cpp
  terminal_panel.cpp
)

set(HEADER_SOURCES
  # Common headers can be listed here if needed
)

if(WIN32)
  list(APPEND SOURCES pty_backend_windows.cpp)
  list(APPEND HEADER_SOURCES pty_backend_windows.h)
else()
  list(APPEND SOURCES pty_backend_posix.cpp)
  list(APPEND HEADER_SOURCES pty_backend_posix.h)
endif()

list(APPEND SOURCES ${HEADER_SOURCES})

if(MINGW)
  list(APPEND SOURCES wx.rc)
endif()

# Create executable
if(MINGW)
  add_executable(${PROJECT_NAME} WIN32 ${SOURCES})
else()
  add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# Link wxWidgets
target_link_libraries(${PROJECT_NAME} ${wxWidgets_LIBRARIES})

# Windows specific settings
if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE __WXMSW__)
  target_link_libraries(${PROJECT_NAME} shell32 ole32 user32)
endif()
