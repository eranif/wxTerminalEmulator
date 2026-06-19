cmake_minimum_required(VERSION 3.10)
project(wxterminal_lib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/FindWxWidgetsMSYS.cmake)

option(BUILD_WXTERMINAL_DEMO "Build the wxterminal demo application" ON)

# Find OpenGL (required by wxWidgets)
if(WIN32)
  find_package(OpenGL REQUIRED)
endif()

# When ON, the terminal view renders with an OpenGL glyph-atlas renderer
# (wxGLCanvas). When OFF it uses the legacy wxDC backing-store renderer, so
# systems without usable OpenGL keep working unchanged. Default is ON on Windows
# and macOS, OFF on Linux. Can be set via -DUSE_OPENGL=ON/OFF or the
# environment variable USE_OPENGL=1/0.
if(DEFINED ENV{USE_OPENGL} AND NOT DEFINED USE_OPENGL)
  set(USE_OPENGL
      "$ENV{USE_OPENGL}"
      CACHE BOOL "Render the terminal with OpenGL (wxGLCanvas)")
else()
  if(WIN32 OR APPLE)
    option(USE_OPENGL "Render the terminal with OpenGL (wxGLCanvas)" ON)
  else()
    option(USE_OPENGL "Render the terminal with OpenGL (wxGLCanvas)" OFF)
  endif()
endif()

if(USE_OPENGL
   AND UNIX
   AND NOT APPLE)
  message(FATAL_ERROR "OpenGL is not supported on Linux")
endif()

# find wxWidgets once
if(MINGW)
  find_package(wxWidgets CONFIG REQUIRED)

else()
  if(USE_OPENGL
     AND UNIX
     AND NOT APPLE)
    set(WX_COMPONENTS base core net aui gl xrc)
  else()
    set(WX_COMPONENTS base core net aui xrc)
  endif()
  find_package(
    wxWidgets
    COMPONENTS ${WX_COMPONENTS}
    REQUIRED)
  include(${wxWidgets_USE_FILE})
  # Find wxWidgets
  find_package(wxWidgets REQUIRED COMPONENTS ${WX_COMPONENTS})
endif()

if(CMAKE_BUILD_TYPE MATCHES Release)
  add_compile_definitions(-DwxDEBUG_LEVEL=0)
endif()

# Build libtsm
add_subdirectory(libtsm)

# Source files
set(LIB_SOURCES terminal_core.cpp terminal_logger.cpp terminal_view.cpp
                terminal_event.cpp terminal_gl_renderer.cpp)

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
target_link_libraries(wxterminal_lib PUBLIC ${wxWidgets_LIBRARIES} tsm)
if(APPLE)
  add_compile_definitions(-DwxDEBUG_LEVEL=0)
  target_link_libraries(wxterminal_lib PUBLIC "-framework CoreText"
                                              "-framework CoreGraphics")
endif()

# OpenGL rendering path: define USE_OPENGL and link the platform GL libraries.
if(USE_OPENGL)
  message(STATUS "🚀 OpenGL rendering enabled (wxGLCanvas glyph-atlas renderer)")
else()
  message(STATUS "🎨 Using legacy wxDC renderer (OpenGL disabled)")
endif()
if(USE_OPENGL)
  target_compile_definitions(wxterminal_lib PUBLIC USE_OPENGL=1)
  if(APPLE)
    target_link_libraries(wxterminal_lib PUBLIC "-framework OpenGL")
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(wxterminal_lib PUBLIC OpenGL::GL)
    if(WIN32)
      find_package(GLEW REQUIRED)
      target_link_libraries(wxterminal_lib PUBLIC GLEW::GLEW)
    endif()
  endif()
else()
  target_compile_definitions(wxterminal_lib PUBLIC USE_OPENGL=0)
endif()

# Build library with PIC when required (useful when linking into other
# binaries/shared libs)
set_target_properties(wxterminal_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Create example program
if(BUILD_WXTERMINAL_DEMO)
  if(MINGW)
    add_executable(wxterminal WIN32 ${EXAMPLE_SOURCES})
  elseif(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE icon.icns)
    set(APP_ICON "${CMAKE_CURRENT_LIST_DIR}/assets/icon.icns")
    set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION
                                                       Resources)
    add_executable(wxterminal MACOSX_BUNDLE ${EXAMPLE_SOURCES} ${APP_ICON})
    set_target_properties(
      wxterminal PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns
                            MACOSX_BUNDLE_BUNDLE_NAME "wxTerminalEmulator")
  else()
    add_executable(wxterminal ${EXAMPLE_SOURCES})
  endif()

  target_link_libraries(wxterminal PRIVATE wxterminal_lib)

  # Platform specific settings
  if(WIN32)
    target_compile_definitions(wxterminal PRIVATE __WXMSW__)
    set_target_properties(
      wxterminal
      PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/install/bin"
                 RUNTIME_OUTPUT_DIRECTORY_RELEASE
                 "${CMAKE_BINARY_DIR}/install/bin"
                 RUNTIME_OUTPUT_DIRECTORY_DEBUG
                 "${CMAKE_BINARY_DIR}/install/bin")
    set(_WIN_EXTRA_LIBS shell32 ole32 user32)
    if(USE_OPENGL)
      list(APPEND _WIN_EXTRA_LIBS opengl32)
    endif()
    target_link_libraries(wxterminal PRIVATE ${wxWidgets_LIBRARIES}
                                             ${_WIN_EXTRA_LIBS})
  elseif(NOT APPLE)
    # Linux needs libutil for forkpty
    target_link_libraries(wxterminal PRIVATE util ${wxWidgets_LIBRARIES})
  endif()

  # ---------------------------------------------------------------------------
  # Post-build: bundle shared library dependencies so the app is self-contained.
  # ---------------------------------------------------------------------------
  set(BUNDLE_DEPS_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundleDeps.cmake")

  if(APPLE)
    set(BUNDLE_TARGET_OS "macos")
    set(BUNDLE_DIR "$<TARGET_BUNDLE_DIR:wxterminal>")
    set(BUNDLE_EXE "$<TARGET_FILE:wxterminal>")
    # Determine wxWidgets library directory from the first library in the list.
    list(GET wxWidgets_LIBRARIES 0 _WX_FIRST_LIB)
    get_filename_component(_WX_LIB_DIR "${_WX_FIRST_LIB}" DIRECTORY)
    add_custom_command(
      TARGET wxterminal
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -DEXECUTABLE=${BUNDLE_EXE} -DBUNDLE_DIR=${BUNDLE_DIR}
        -DTARGET_OS=${BUNDLE_TARGET_OS} -DWX_LIB_DIR=${_WX_LIB_DIR} -P
        ${BUNDLE_DEPS_SCRIPT}
      COMMENT "Bundling dependencies into .app bundle"
      VERBATIM)
  elseif(NOT WIN32)
    # Linux: set RPATH so the binary finds libs in ../lib relative to itself.
    set_target_properties(wxterminal PROPERTIES INSTALL_RPATH "$ORIGIN/../lib"
                                                BUILD_RPATH "$ORIGIN/../lib")
    set(BUNDLE_TARGET_OS "linux")
    list(GET wxWidgets_LIBRARIES 0 _WX_FIRST_LIB)
    get_filename_component(_WX_LIB_DIR "${_WX_FIRST_LIB}" DIRECTORY)
    add_custom_command(
      TARGET wxterminal
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:wxterminal>
        -DBUNDLE_DIR=$<TARGET_FILE_DIR:wxterminal>
        -DTARGET_OS=${BUNDLE_TARGET_OS} -DWX_LIB_DIR=${_WX_LIB_DIR} -P
        ${BUNDLE_DEPS_SCRIPT}
      COMMENT "Bundling dependencies alongside binary"
      VERBATIM)
  else()
    # Windows/MINGW: copy DLLs next to the .exe.
    set(BUNDLE_TARGET_OS "windows")
    # Derive the MSYS2 toolchain bin directory from the compiler location so the
    # bundler can resolve MinGW runtime and GLEW DLLs automatically.
    get_filename_component(_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_WIN_INSTALL_BIN "${CMAKE_BINARY_DIR}/install/bin")

    add_custom_command(
      TARGET wxterminal
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:wx::wxmono>"
              "$<TARGET_FILE_DIR:wxterminal>/"
      COMMENT "Copying wxWidgets DLL next to wxterminal executable"
      VERBATIM)

    # Step 2: copy all transitive toolchain DLLs (MinGW runtime, GLEW, etc.) by
    # walking the import table of the exe against the toolchain bin dir.
    add_custom_command(
      TARGET wxterminal
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:wxterminal>
        -DBUNDLE_DIR=${_WIN_INSTALL_BIN} -DTARGET_OS=${BUNDLE_TARGET_OS}
        -DCLANG64_BIN=${_TOOLCHAIN_BIN} -P ${BUNDLE_DEPS_SCRIPT}
      COMMENT "Bundling toolchain DLLs alongside executable"
      VERBATIM)
  endif()
endif()
