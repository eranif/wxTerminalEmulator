# StageInstaller.cmake — Collects the files for the Windows installer into a
# clean staging directory that installer.iss reads from.
#
# Called with cmake -P and these variables defined:
# BIN_DIR      - directory with glypht.exe and its bundled DLLs and themes
# STAGE_DIR    - staging directory (it is deleted and created again)
# WX_DLL       - full path of the wxWidgets DLL glypht.exe was linked with
# LICENSE_FILE - LICENSE file of the project
# ICON_FILE    - icon used by the setup program
#
# BIN_DIR can hold DLLs of older builds (for example another wxWidgets
# version). Every wxmsw*.dll except WX_DLL is therefore left out.

cmake_minimum_required(VERSION 3.10)

foreach(_VAR BIN_DIR STAGE_DIR WX_DLL LICENSE_FILE ICON_FILE)
  if(NOT ${_VAR})
    message(FATAL_ERROR "StageInstaller.cmake requires ${_VAR}")
  endif()
endforeach()

if(NOT EXISTS "${BIN_DIR}/glypht.exe")
  message(FATAL_ERROR "StageInstaller: ${BIN_DIR}/glypht.exe was not found")
endif()

get_filename_component(_WX_DLL_NAME "${WX_DLL}" NAME)

file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${STAGE_DIR}/bin" "${STAGE_DIR}/assets")

file(
  GLOB _ENTRIES
  RELATIVE "${BIN_DIR}"
  "${BIN_DIR}/*")
foreach(_ENTRY ${_ENTRIES})
  if(_ENTRY MATCHES "^wxmsw.*\\.dll$" AND NOT _ENTRY STREQUAL _WX_DLL_NAME)
    message(STATUS "StageInstaller: skipping stale ${_ENTRY}")
    continue()
  endif()
  # Works for files and for directories (the themes).
  file(COPY "${BIN_DIR}/${_ENTRY}" DESTINATION "${STAGE_DIR}/bin")
endforeach()

# The wxWidgets DLL may have been built outside of BIN_DIR.
if(NOT EXISTS "${STAGE_DIR}/bin/${_WX_DLL_NAME}")
  file(COPY "${WX_DLL}" DESTINATION "${STAGE_DIR}/bin")
endif()

file(COPY "${LICENSE_FILE}" DESTINATION "${STAGE_DIR}")
file(COPY "${ICON_FILE}" DESTINATION "${STAGE_DIR}/assets")

message(STATUS "StageInstaller: staged ${BIN_DIR} -> ${STAGE_DIR}")
