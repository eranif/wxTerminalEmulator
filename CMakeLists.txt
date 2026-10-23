cmake_minimum_required(VERSION 3.10)
project(wxterminal_lib)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/FindWxWidgetsMSYS.cmake)

option(BUILD_GLYPHT "Build the GlyphT Terminal Application" ON)
if (UNIX AND NOT APPLE)
  set(OpenGL_GL_PREFERENCE "GLVND")
endif ()

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

# find wxWidgets once
if(MINGW)
  find_package(wxWidgets CONFIG REQUIRED)

else()
  # On macOS the OpenGL canvas is part of the core library of the wxWidgets
  # that build.sh builds. Asking for "gl" there links a separate (and possibly
  # system) libwx_*_gl, which does not match the core library.
  if(USE_OPENGL AND NOT APPLE)
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
    ${CMAKE_CURRENT_LIST_DIR}/src/lib/terminal_gl_renderer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/lib/terminal_gauge.cpp)

if(APPLE)
  list(APPEND LIB_SOURCES
       ${CMAKE_CURRENT_LIST_DIR}/src/lib/keyboard_layout_mac.cpp)
elseif(WIN32)
  list(APPEND LIB_SOURCES
       ${CMAKE_CURRENT_LIST_DIR}/src/lib/keyboard_layout_windows.cpp)
endif()

set(GLYPHT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/app_persistence.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/layout_persistence.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/MainFrame.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/wxTerminalUI.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/SettingsDlg.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/TabSwitcherDlg.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/shell_locator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/glypht/theme_loader.cpp)

if(MINGW)
  list(APPEND GLYPHT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/glypht/glypht.rc")
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
  # Carbon is used to translate keys with the active keyboard layout, see
  # keyboard_layout_mac.cpp.
  target_link_libraries(
    wxterminal_lib PUBLIC "-framework CoreText" "-framework CoreGraphics"
                          "-framework Carbon")
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
    find_package(GLEW REQUIRED)
    target_link_libraries(wxterminal_lib PUBLIC GLEW::GLEW)
  endif()
else()
  target_compile_definitions(wxterminal_lib PUBLIC USE_OPENGL=0)
endif()

# Build library with PIC when required (useful when linking into other
# binaries/shared libs)
set_target_properties(wxterminal_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Finds the wxWidgets library directory. wxWidgets_LIBRARIES holds either
# "-L<dir>" flags followed by "-lwx_..." (wx-config) or full library paths
# (CONFIG mode). BundleDeps.cmake searches this directory for the libraries to
# bundle.
function(wx_find_lib_dir out_var)
  set(_dir "")
  foreach(_item ${wxWidgets_LIBRARIES})
    if(_item MATCHES "^-L(.+)")
      set(_dir "${CMAKE_MATCH_1}")
      break()
    elseif(EXISTS "${_item}")
      get_filename_component(_dir "${_item}" DIRECTORY)
      break()
    endif()
  endforeach()
  set(${out_var}
      "${_dir}"
      PARENT_SCOPE)
endfunction()

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
    set_target_properties(glypht PROPERTIES MACOSX_BUNDLE_ICON_FILE icon.icns
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
    message(STATUS "Copying glypht.svg asset file to binary directory...")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.svg"
         DESTINATION "${CMAKE_BINARY_DIR}/install/bin/")

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
         DESTINATION "${CMAKE_BINARY_DIR}")
  else()
    # Copy SVG asset next to binary (macOS bundle)
    message(
      STATUS
        "Copying glypht.svg asset file to ${CMAKE_BINARY_DIR}/glypht.app/Contents/Resources/"
    )
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.svg"
         DESTINATION "${CMAKE_BINARY_DIR}/glypht.app/Contents/Resources/")
  endif()

  # Copy the colour themes (*.toml). They are read at startup from
  # glypht.app/Contents/Resources/themes on macOS, and from a "themes" directory
  # next to the executable on Windows and Linux. This runs before the bundling
  # step below.
  if(APPLE)
    set(GLYPHT_THEMES_DIR
        "$<TARGET_BUNDLE_CONTENT_DIR:glypht>/Resources/themes")
  else()
    set(GLYPHT_THEMES_DIR "$<TARGET_FILE_DIR:glypht>/themes")
  endif()
  add_custom_command(
    TARGET glypht
    POST_BUILD
    # Start from an empty directory, so themes removed from assets/themes do not
    # stay in an old build tree.
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${GLYPHT_THEMES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/themes" "${GLYPHT_THEMES_DIR}"
    COMMENT "Copying colour themes"
    VERBATIM)

  # ---------------------------------------------------------------------------
  # Post-build: bundle shared library dependencies so the app is self-contained.
  # ---------------------------------------------------------------------------
  set(BUNDLE_DEPS_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundleDeps.cmake")

  if(APPLE)
    set(BUNDLE_TARGET_OS "macos")
    set(BUNDLE_DIR "$<TARGET_BUNDLE_DIR:glypht>")
    set(BUNDLE_EXE "$<TARGET_FILE:glypht>")
    # Determine wxWidgets library directory from the first library in the list.
    wx_find_lib_dir(_WX_LIB_DIR)
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
    # As a subproject (e.g. CodeLite) this is needed only for the DEB package
    # (MAKE_DEB is set by the parent), otherwise leave the RPATH to the parent.
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR MAKE_DEB)
      # Also search the install RPATH of the parent: that is where it installs
      # the shared wx libs.
      set(GLYPHT_INSTALL_RPATH "$ORIGIN/../lib")
      if(CMAKE_INSTALL_RPATH)
        list(APPEND GLYPHT_INSTALL_RPATH ${CMAKE_INSTALL_RPATH})
      endif()
      set_target_properties(
        glypht PROPERTIES INSTALL_RPATH "${GLYPHT_INSTALL_RPATH}"
                          BUILD_RPATH "$ORIGIN/../lib")
    endif()
    set(BUNDLE_TARGET_OS "linux")
    wx_find_lib_dir(_WX_LIB_DIR)
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

    # Copy the newer ConPTY (conpty.dll and OpenConsole.exe) next to the
    # executable. See assets/conpty/README.md.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
      set(_CONPTY_ARCH "arm64")
    else()
      set(_CONPTY_ARCH "x64")
    endif()
    add_custom_command(
      TARGET glypht
      POST_BUILD
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/conpty/${_CONPTY_ARCH}/conpty.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/conpty/${_CONPTY_ARCH}/OpenConsole.exe"
        "$<TARGET_FILE_DIR:glypht>/"
      COMMENT "Copying conpty.dll and OpenConsole.exe next to glypht executable"
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

  # ---------------------------------------------------------------------------
  # Windows: Inno Setup installer. Build it with: cmake --build . --target setup
  # The target stages the bundled application (see cmake/StageInstaller.cmake)
  # and compiles installer.iss, generated from installer.iss.in, with ISCC.
  # ---------------------------------------------------------------------------
  set(GLYPHT_VERSION
      "1.0.0"
      CACHE STRING "Version of the GlyphT packages (installer, DEB)")

  if(WIN32 AND MINGW)
    set(INSTALLER_SOURCE_DIR "${CMAKE_BINARY_DIR}/installer")

    # Architecture, used in the installer file name
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
      set(PROCESSOR_ARCHITECTURE "amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
      set(PROCESSOR_ARCHITECTURE "arm64")
    else()
      set(PROCESSOR_ARCHITECTURE "unknown")
    endif()

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/installer.iss.in
                   ${CMAKE_BINARY_DIR}/installer.iss @ONLY)

    find_program(
      INNO_SETUP_CMP
      NAMES "ISCC" "ISCC.exe"
      PATHS "c:/Program Files (x86)/Inno Setup 6"
            "c:/Program Files/Inno Setup 6")
    if(NOT INNO_SETUP_CMP)
      message(
        WARNING
          "Could not locate the Inno Setup compiler (ISCC). The \"setup\" target is not created. Install Inno Setup from https://jrsoftware.org/"
      )
    else()
      message(STATUS "Inno Setup compiler is set to: ${INNO_SETUP_CMP}")
      add_custom_target(
        glypht_setup
        COMMAND
          ${CMAKE_COMMAND} -DBIN_DIR=$<TARGET_FILE_DIR:glypht>
          -DSTAGE_DIR=${INSTALLER_SOURCE_DIR} -DWX_DLL=$<TARGET_FILE:wx::wxmono>
          -DLICENSE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
          -DICON_FILE=${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.ico -P
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StageInstaller.cmake
        COMMAND "${INNO_SETUP_CMP}" ${CMAKE_BINARY_DIR}/installer.iss
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Building the Windows installer"
        VERBATIM)
      add_dependencies(glypht_setup glypht)
    endif()
  endif()

  # ---------------------------------------------------------------------------
  # Linux: install rules and a DEB package. Build it with: cpack -G DEB
  # GlyphT reads glypht.svg and themes/ from the directory of the executable, so
  # they are installed together into lib/glypht. bin/glypht is a launcher script
  # that runs that executable.
  # ---------------------------------------------------------------------------
  if(UNIX AND NOT APPLE)
    set(GLYPHT_INSTALL_DIR "lib/glypht")
    install(TARGETS glypht RUNTIME DESTINATION "${GLYPHT_INSTALL_DIR}")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.svg"
            DESTINATION "${GLYPHT_INSTALL_DIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/themes"
            DESTINATION "${GLYPHT_INSTALL_DIR}")
    # bin/glypht is a launcher script that sets GTK_OVERLAY_SCROLLING=0 and runs
    # the executable (see assets/glypht.sh).
    install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.sh"
            DESTINATION "bin" RENAME glypht)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.desktop"
            DESTINATION "share/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/glypht.svg"
            DESTINATION "share/icons/hicolor/scalable/apps")

    # Only package when GlyphT is the top-level project. As a subproject the parent project owns CPack.
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
      # The DEB package needs dpkg-shlibdeps (package dpkg-dev), which exists on
      # Debian based systems. Elsewhere there is no "package" target.
      find_program(DPKG_SHLIBDEPS_EXECUTABLE dpkg-shlibdeps)
      if(DPKG_SHLIBDEPS_EXECUTABLE)
        set(CPACK_GENERATOR "DEB")
        set(CPACK_PACKAGE_NAME "glypht")
        set(CPACK_PACKAGE_VERSION "${GLYPHT_VERSION}")
        set(CPACK_PACKAGE_CONTACT "Eran Ifrah <eran@codelite.org>")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
            "GlyphT, a terminal emulator built with wxWidgets")
        set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
        set(CPACK_STRIP_FILES ON)
        # Without this the directories get the permissions of the umask (775).
        set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
        set(CPACK_DEBIAN_PACKAGE_HOMEPAGE
            "https://github.com/eranif/wxTerminalEmulator")
        # The package dependencies (GTK 3, GLEW, ...) are found by dpkg-shlibdeps
        # from the libraries the executable links. wxWidgets is linked statically.
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        include(CPack)
      else()
        message(STATUS "dpkg-shlibdeps not found: no DEB package target")
      endif()
    endif()
  endif()
endif()
