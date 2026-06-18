# ---------------------------------------------------------------------------
# Locate wxWidgets: use WXWIN environment variable to find the installation. If
# WXWIN is not set, fall back to standard search paths. Automatically finds the
# latest wxWidgets version in lib/cmake/
# ---------------------------------------------------------------------------
if(WIN32)
  if(NOT WXWIN)
    message(FATAL_ERROR "Please pass WXWIN: -DWXWIN=/path/to/wxWidgets")
  endif()

  set(_WXWIN_PATH "${WXWIN}")
  message(STATUS "WXWIN is set to : ${_WXWIN_PATH}")

  # Look for wxWidgets cmake config directories under lib/cmake/
  file(GLOB _WXWIDGETS_CMAKE_DIRS "${_WXWIN_PATH}/lib/cmake/wxWidgets-*")

  if(_WXWIDGETS_CMAKE_DIRS)
    # Sort to get the latest version (assuming semantic versioning)
    list(SORT _WXWIDGETS_CMAKE_DIRS COMPARE NATURAL)
    # Get the last element (highest version) after sorting
    list(LENGTH _WXWIDGETS_CMAKE_DIRS _LIST_LEN)
    math(EXPR _LAST_IDX "${_LIST_LEN} - 1")
    list(GET _WXWIDGETS_CMAKE_DIRS ${_LAST_IDX} _LATEST_WXWIDGETS_DIR)

    message(STATUS "Found wxWidgets cmake directories:")
    foreach(_DIR ${_WXWIDGETS_CMAKE_DIRS})
      message(STATUS "  - ${_DIR}")
    endforeach()
    message(STATUS "Using latest: ${_LATEST_WXWIDGETS_DIR}")

    set(wxWidgets_DIR
        "${_LATEST_WXWIDGETS_DIR}"
        CACHE PATH "wxWidgets CMake directory" FORCE)
  else()
    message(
      WARNING
        "WXWIN set to ${_WXWIN_PATH}, but no wxWidgets cmake config found in lib/cmake/"
    )
    message(STATUS "Will attempt standard wxWidgets search...")
  endif()

  if(DEFINED wxWidgets_DIR)
    message(STATUS "wxWidgets_DIR is set to: ${wxWidgets_DIR}")

    # Verify the config file exists
    if(EXISTS "${wxWidgets_DIR}/wxWidgetsConfig.cmake")
      message(STATUS "  ✓ wxWidgetsConfig.cmake found")
    else()
      message(
        WARNING
          "  ✗ wxWidgetsConfig.cmake NOT found at ${wxWidgets_DIR}/wxWidgetsConfig.cmake"
      )
    endif()
  else()
    message(STATUS "wxWidgets_DIR not set; will use system default paths")
  endif()
endif()

# Add wxWidgets_DIR to CMAKE_PREFIX_PATH to ensure find_package finds it
if(DEFINED wxWidgets_DIR)
  list(APPEND CMAKE_PREFIX_PATH "${wxWidgets_DIR}")
  message(STATUS "Added ${wxWidgets_DIR} to CMAKE_PREFIX_PATH")

  # Debug: Show what CMake is looking for
  message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
endif()
