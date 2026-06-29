cmake_minimum_required(VERSION 3.10)
project(wxterminal_lib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/FindWxWidgetsMSYS.cmake)

option(BUILD_GLYPHT "Build the GlyphT Terminal Application" ON)

# Find OpenGL (required by wxWidgets)
if(WIN32)
  find_package(OpenGL REQUIRED)
endif()

# When ON, the terminal view renders with an OpenGL glyph-atlas renderer
# (wxGLCanvas). When OFF it uses the legacy wxDC backing-store renderer, so
# systems without usable OpenGL keep working unchanged. Default is ON on Windows
# and macOS, OFF on Linux. Can be set via -DUSE_OPENGL=ON/OFF or the environment
# variable USE_OPENGL=1/0.
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

add_compile_definitions(wxDEBUG_LEVEL=0)

# Build libtsm
add_subdirectory(libtsm)

# Source files
set(LIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/lib/terminal_core.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/lib/terminal_logger.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/lib/terminal_view.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/lib/terminal_event.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/lib/terminal_gl_renderer.cpp)

set(GLYPHT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/app_persistence.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/layout_persistence.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/MainFrame.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/wxTerminalUI.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/SettingsDlg.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/wxTerminalUI_wxterminalemulator_bitmaps.cpp
)

if(MINGW)
  list(APPEND GLYPHT_SOURCES wx.rc)
endif()

if(WIN32)
  list(APPEND LIB_SOURCES
       ${CMAKE_CURRENT_LIST_DIR}/src/lib/pty_backend_windows.cpp)
  list(APPEND HEADER_SOURCES
       ${CMAKE_CURRENT_LIST_DIR}/src/lib/pty_backend_windows.h)
else()
  list(APPEND LIB_SOURCES
       ${CMAKE_CURRENT_LIST_DIR}/src/lib/pty_backend_posix.cpp)
  list(APPEND HEADER_SOURCES
       ${CMAKE_CURRENT_LIST_DIR}/src/lib/pty_backend_posix.h)
endif()

# Common headers can be listed here if needed
set(HEADER_SOURCES)

# Create library
add_library(wxterminal_lib STATIC ${LIB_SOURCES} ${HEADER_SOURCES})
target_include_directories(wxterminal_lib PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(wxterminal_lib
                           PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/lib)

target_link_libraries(wxterminal_lib PUBLIC ${wxWidgets_LIBRARIES} tsm)
if(APPLE)
  add_compile_definitions(wxDEBUG_LEVEL=0)
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
if(BUILD_GLYPHT)
  if(MINGW)
    add_executable(glypht WIN32 ${GLYPHT_SOURCES})
  elseif(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE icon.icns)
    set(APP_ICON "${CMAKE_CURRENT_LIST_DIR}/assets/icon.icns")
    set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION
                                                       Resources)
    add_executable(glypht MACOSX_BUNDLE ${GLYPHT_SOURCES} ${APP_ICON})
    set_target_properties(
      glypht PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns
                        MACOSX_BUNDLE_BUNDLE_NAME "GlyphT")
  else()
    add_executable(glypht ${GLYPHT_SOURCES})
  endif()

  target_link_libraries(glypht PRIVATE wxterminal_lib)

  # Platform specific settings
  if(WIN32)
    target_compile_definitions(glypht PRIVATE __WXMSW__)
    set_target_properties(
      glypht
      PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/install/bin"
                 RUNTIME_OUTPUT_DIRECTORY_RELEASE
                 "${CMAKE_BINARY_DIR}/install/bin"
                 RUNTIME_OUTPUT_DIRECTORY_DEBUG
                 "${CMAKE_BINARY_DIR}/install/bin")
    set(_WIN_EXTRA_LIBS shell32 ole32 user32)

    # Copy SVG asset next to binary (Windows only)
    message(STATUS "Copying glypht-msw.svg asset file to binary directory...")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht-msw.svg"
         DESTINATION "${CMAKE_BINARY_DIR}/install/bin/")
    file(RENAME "${CMAKE_BINARY_DIR}/install/bin/glypht-msw.svg"
         "${CMAKE_BINARY_DIR}/install/bin/glypht.svg")

    if(USE_OPENGL)
      list(APPEND _WIN_EXTRA_LIBS opengl32)
    endif()
    target_link_libraries(glypht PRIVATE ${wxWidgets_LIBRARIES}
                                         ${_WIN_EXTRA_LIBS})
  elseif(NOT APPLE)
    # Linux needs libutil for forkpty
    target_link_libraries(glypht PRIVATE util ${wxWidgets_LIBRARIES})
    # Copy SVG asset next to binary (Linux)
    message(STATUS "Copying glypht.svg asset file to binary directory...")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.svg"
         DESTINATION "$<TARGET_FILE_DIR:glypht>")
  else()
    # Copy SVG asset next to binary (macOS bundle)
    message(
      STATUS
        "Copying glypht.svg asset file to ${CMAKE_BINARY_DIR}/glypht.app/Contents/Resources/"
    )
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.svg"
         DESTINATION "${CMAKE_BINARY_DIR}/glypht.app/Contents/Resources/")
  endif()

  # ---------------------------------------------------------------------------
  # Post-build: bundle shared library dependencies so the app is self-contained.
  # ---------------------------------------------------------------------------
  set(BUNDLE_DEPS_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundleDeps.cmake")

  if(APPLE)
    set(BUNDLE_TARGET_OS "macos")
    set(BUNDLE_DIR "$<TARGET_BUNDLE_DIR:glypht>")
    set(BUNDLE_EXE "$<TARGET_FILE:glypht>")
    # Determine wxWidgets library directory from the first library in the list.
    list(GET wxWidgets_LIBRARIES 0 _WX_FIRST_LIB)
    get_filename_component(_WX_LIB_DIR "${_WX_FIRST_LIB}" DIRECTORY)
    add_custom_command(
      TARGET glypht
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -DEXECUTABLE=${BUNDLE_EXE} -DBUNDLE_DIR=${BUNDLE_DIR}
        -DTARGET_OS=${BUNDLE_TARGET_OS} -DWX_LIB_DIR=${_WX_LIB_DIR} -P
        ${BUNDLE_DEPS_SCRIPT}
      COMMENT "Bundling dependencies into .app bundle"
      VERBATIM)
  elseif(NOT WIN32)
    # Linux: set RPATH so the binary finds libs in ../lib relative to itself.
    set_target_properties(glypht PROPERTIES INSTALL_RPATH "$ORIGIN/../lib"
                                            BUILD_RPATH "$ORIGIN/../lib")
    set(BUNDLE_TARGET_OS "linux")
    list(GET wxWidgets_LIBRARIES 0 _WX_FIRST_LIB)
    get_filename_component(_WX_LIB_DIR "${_WX_FIRST_LIB}" DIRECTORY)
    add_custom_command(
      TARGET glypht
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:glypht>
        -DBUNDLE_DIR=$<TARGET_FILE_DIR:glypht> -DTARGET_OS=${BUNDLE_TARGET_OS}
        -DWX_LIB_DIR=${_WX_LIB_DIR} -P ${BUNDLE_DEPS_SCRIPT}
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
      TARGET glypht
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:wx::wxmono>"
              "$<TARGET_FILE_DIR:glypht>/"
      COMMENT "Copying wxWidgets DLL next to glypht executable"
      VERBATIM)

    # Step 2: copy all transitive toolchain DLLs (MinGW runtime, GLEW, etc.) by
    # walking the import table of the exe against the toolchain bin dir.
    add_custom_command(
      TARGET glypht
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -DEXECUTABLE=$<TARGET_FILE:glypht>
        -DBUNDLE_DIR=${_WIN_INSTALL_BIN} -DTARGET_OS=${BUNDLE_TARGET_OS}
        -DCLANG64_BIN=${_TOOLCHAIN_BIN} -P ${BUNDLE_DEPS_SCRIPT}
      COMMENT "Bundling toolchain DLLs alongside executable"
      VERBATIM)
  endif()
endif()
