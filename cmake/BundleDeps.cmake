# BundleDeps.cmake — Post-build script that bundles shared library dependencies
# into the application so it can run standalone without requiring the user to
# install wxWidgets system-wide.
#
# Called as a post-build step via cmake -P with the following variables defined:
#   EXECUTABLE   - Full path to the built binary (inside .app on macOS)
#   BUNDLE_DIR   - On macOS: the .app bundle root. Others: directory of the exe.
#   TARGET_OS    - "macos", "linux", or "windows"
#   WX_LIB_DIR   - Directory containing wxWidgets shared libraries

cmake_minimum_required(VERSION 3.10)

if(NOT EXECUTABLE OR NOT BUNDLE_DIR OR NOT TARGET_OS)
  message(FATAL_ERROR "BundleDeps.cmake requires EXECUTABLE, BUNDLE_DIR, and TARGET_OS")
endif()

# Resolve a path through symlinks to the real file on disk.
function(resolve_realpath INPUT_PATH OUT_VAR)
  get_filename_component(_RESOLVED "${INPUT_PATH}" REALPATH)
  set(${OUT_VAR} "${_RESOLVED}" PARENT_SCOPE)
endfunction()

# Given an otool reference (absolute path, @rpath/..., or @loader_path/...),
# try to find the real dylib on disk. Sets OUT_REAL to the resolved path and
# OUT_ORIGINAL to the reference as otool printed it.  Returns empty string if
# the lib is a system library or cannot be found.
function(resolve_otool_ref REF SEARCH_DIRS OUT_REAL OUT_ORIGINAL)
  set(${OUT_REAL} "" PARENT_SCOPE)
  set(${OUT_ORIGINAL} "" PARENT_SCOPE)

  # Skip system libraries.
  if(REF MATCHES "^/System/" OR REF MATCHES "^/usr/lib/")
    return()
  endif()

  # Skip @executable_path refs (already relocated).
  if(REF MATCHES "^@executable_path/")
    return()
  endif()

  if(REF MATCHES "^@rpath/" OR REF MATCHES "^@loader_path/")
    # Strip the prefix to get the filename.
    string(REGEX REPLACE "^@[a-z_]+/" "" LIB_FILENAME "${REF}")
    # Search for it in provided directories.
    foreach(DIR ${SEARCH_DIRS})
      if(EXISTS "${DIR}/${LIB_FILENAME}")
        resolve_realpath("${DIR}/${LIB_FILENAME}" _REAL)
        set(${OUT_REAL} "${_REAL}" PARENT_SCOPE)
        set(${OUT_ORIGINAL} "${REF}" PARENT_SCOPE)
        return()
      endif()
    endforeach()
    return()
  endif()

  # Absolute path.
  if(REF MATCHES "^/.*\\.dylib")
    resolve_realpath("${REF}" _REAL)
    if(EXISTS "${_REAL}")
      set(${OUT_REAL} "${_REAL}" PARENT_SCOPE)
      set(${OUT_ORIGINAL} "${REF}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

if(TARGET_OS STREQUAL "macos")
  set(FRAMEWORKS_DIR "${BUNDLE_DIR}/Contents/Frameworks")
  file(MAKE_DIRECTORY "${FRAMEWORKS_DIR}")

  # Directories to search when resolving @rpath references.
  set(RPATH_SEARCH_DIRS "")
  if(WX_LIB_DIR)
    list(APPEND RPATH_SEARCH_DIRS "${WX_LIB_DIR}")
  endif()
  list(APPEND RPATH_SEARCH_DIRS "/usr/local/lib")

  # Discover all non-system dylibs the executable links against.
  execute_process(
    COMMAND otool -L "${EXECUTABLE}"
    OUTPUT_VARIABLE OTOOL_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  # Parse otool output into a list of references to bundle.
  # Each otool line looks like:
  #     /usr/local/lib/libfoo.dylib (compatibility ...)
  # or: @rpath/libfoo.dylib (compatibility ...)
  string(REPLACE "\n" ";" OTOOL_LINES "${OTOOL_OUTPUT}")
  # DEPS_ORIGINALS: the reference string as otool prints it (used for install_name_tool -change)
  # DEPS_REALS:     the resolved absolute path to the real file on disk
  # DEPS_NAMES:     the filename to use inside Frameworks/
  set(DEPS_ORIGINALS "")
  set(DEPS_REALS "")
  set(DEPS_NAMES "")
  set(_IS_FIRST_LINE TRUE)
  foreach(LINE ${OTOOL_LINES})
    string(STRIP "${LINE}" LINE)
    # Match lines that contain a dylib reference (absolute or @-prefixed).
    if(LINE MATCHES "(/[^ ]+\\.dylib|@[a-z_]+/[^ ]+\\.dylib)")
      string(REGEX REPLACE " \\(.*" "" REF "${LINE}")
      # The very first line of otool output is the file header (path with colon),
      # skip it.
      if(_IS_FIRST_LINE AND REF MATCHES ":$")
        set(_IS_FIRST_LINE FALSE)
        continue()
      endif()
      set(_IS_FIRST_LINE FALSE)

      resolve_otool_ref("${REF}" "${RPATH_SEARCH_DIRS}" _REAL _ORIG)
      if(_REAL)
        get_filename_component(_NAME "${_REAL}" NAME)
        list(FIND DEPS_REALS "${_REAL}" _IDX)
        if(_IDX EQUAL -1)
          list(APPEND DEPS_ORIGINALS "${_ORIG}")
          list(APPEND DEPS_REALS "${_REAL}")
          list(APPEND DEPS_NAMES "${_NAME}")
        endif()
      endif()
    endif()
  endforeach()

  # Scan each dependency for its own transitive non-system deps (one level deep).
  set(TRANS_ORIGINALS "")
  set(TRANS_REALS "")
  set(TRANS_NAMES "")
  foreach(DEP_REAL ${DEPS_REALS})
    if(NOT EXISTS "${DEP_REAL}")
      continue()
    endif()
    execute_process(
      COMMAND otool -L "${DEP_REAL}"
      OUTPUT_VARIABLE DEP_OTOOL
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\n" ";" DEP_LINES "${DEP_OTOOL}")
    set(_IS_FIRST_LINE TRUE)
    foreach(LINE ${DEP_LINES})
      string(STRIP "${LINE}" LINE)
      if(LINE MATCHES "(/[^ ]+\\.dylib|@[a-z_]+/[^ ]+\\.dylib)")
        string(REGEX REPLACE " \\(.*" "" REF "${LINE}")
        if(_IS_FIRST_LINE AND REF MATCHES ":$")
          set(_IS_FIRST_LINE FALSE)
          continue()
        endif()
        set(_IS_FIRST_LINE FALSE)

        resolve_otool_ref("${REF}" "${RPATH_SEARCH_DIRS}" _REAL _ORIG)
        if(_REAL)
          # Skip if already in the main deps list.
          list(FIND DEPS_REALS "${_REAL}" _IDX)
          if(_IDX EQUAL -1)
            list(FIND TRANS_REALS "${_REAL}" _IDX2)
            if(_IDX2 EQUAL -1)
              get_filename_component(_NAME "${_REAL}" NAME)
              list(APPEND TRANS_ORIGINALS "${_ORIG}")
              list(APPEND TRANS_REALS "${_REAL}")
              list(APPEND TRANS_NAMES "${_NAME}")
            endif()
          endif()
        endif()
      endif()
    endforeach()
  endforeach()

  # Merge transitive deps into the main lists.
  list(APPEND DEPS_ORIGINALS ${TRANS_ORIGINALS})
  list(APPEND DEPS_REALS ${TRANS_REALS})
  list(APPEND DEPS_NAMES ${TRANS_NAMES})

  # Copy each dependency into Frameworks/.
  list(LENGTH DEPS_REALS _DEP_COUNT)
  math(EXPR _DEP_LAST "${_DEP_COUNT} - 1")
  if(_DEP_COUNT GREATER 0)
    foreach(_I RANGE 0 ${_DEP_LAST})
      list(GET DEPS_REALS ${_I} _REAL)
      list(GET DEPS_NAMES ${_I} _NAME)
      set(DEST "${FRAMEWORKS_DIR}/${_NAME}")
      execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_REAL}" "${DEST}")
      execute_process(COMMAND chmod u+w "${DEST}")
      message(STATUS "Bundled: ${_NAME}")
    endforeach()
  endif()

  # Rewrite the executable's references to point into @executable_path/../Frameworks/
  if(_DEP_COUNT GREATER 0)
    foreach(_I RANGE 0 ${_DEP_LAST})
      list(GET DEPS_ORIGINALS ${_I} _ORIG)
      list(GET DEPS_NAMES ${_I} _NAME)
      set(DEST "${FRAMEWORKS_DIR}/${_NAME}")
      if(NOT EXISTS "${DEST}")
        continue()
      endif()
      execute_process(
        COMMAND install_name_tool -change "${_ORIG}"
                "@executable_path/../Frameworks/${_NAME}" "${EXECUTABLE}")
    endforeach()
  endif()

  # Rewrite each bundled dylib's own install name and its references to sibling libs.
  if(_DEP_COUNT GREATER 0)
    foreach(_I RANGE 0 ${_DEP_LAST})
      list(GET DEPS_NAMES ${_I} _NAME)
      set(BUNDLED "${FRAMEWORKS_DIR}/${_NAME}")
      if(NOT EXISTS "${BUNDLED}")
        continue()
      endif()
      # Fix its own id.
      execute_process(
        COMMAND install_name_tool -id "@executable_path/../Frameworks/${_NAME}" "${BUNDLED}")
      # Fix references to other bundled libs.
      foreach(_J RANGE 0 ${_DEP_LAST})
        list(GET DEPS_ORIGINALS ${_J} _OTHER_ORIG)
        list(GET DEPS_NAMES ${_J} _OTHER_NAME)
        execute_process(
          COMMAND install_name_tool -change "${_OTHER_ORIG}"
                  "@executable_path/../Frameworks/${_OTHER_NAME}" "${BUNDLED}")
      endforeach()
    endforeach()
  endif()

  message(STATUS "BundleDeps: macOS bundle complete (${FRAMEWORKS_DIR})")

elseif(TARGET_OS STREQUAL "linux")
  # On Linux, copy .so files to a lib/ directory beside the binary and rely on
  # RPATH ($ORIGIN/../lib) set at link time.
  get_filename_component(BIN_DIR "${EXECUTABLE}" DIRECTORY)
  set(LIB_DIR "${BIN_DIR}/../lib")
  file(MAKE_DIRECTORY "${LIB_DIR}")

  if(NOT WX_LIB_DIR OR NOT EXISTS "${WX_LIB_DIR}")
    message(WARNING "BundleDeps: WX_LIB_DIR not set or does not exist, skipping Linux bundling")
    return()
  endif()

  file(GLOB WX_LIBS "${WX_LIB_DIR}/libwx_*.so*")
  foreach(LIB ${WX_LIBS})
    get_filename_component(LIB_NAME "${LIB}" NAME)
    file(COPY "${LIB}" DESTINATION "${LIB_DIR}"
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    message(STATUS "Bundled: ${LIB_NAME}")
  endforeach()

  message(STATUS "BundleDeps: Linux bundle complete (${LIB_DIR})")

elseif(TARGET_OS STREQUAL "windows")
  # On Windows (MINGW), copy DLLs next to the executable.
  get_filename_component(BIN_DIR "${EXECUTABLE}" DIRECTORY)

  if(NOT WX_LIB_DIR OR NOT EXISTS "${WX_LIB_DIR}")
    message(WARNING "BundleDeps: WX_LIB_DIR not set or does not exist, skipping Windows bundling")
    return()
  endif()

  # wxWidgets DLLs are typically in the same directory or a sibling bin/ dir.
  set(SEARCH_DIRS "${WX_LIB_DIR}" "${WX_LIB_DIR}/../bin")
  foreach(SEARCH_DIR ${SEARCH_DIRS})
    file(GLOB WX_DLLS "${SEARCH_DIR}/wx*.dll")
    foreach(DLL ${WX_DLLS})
      get_filename_component(DLL_NAME "${DLL}" NAME)
      file(COPY "${DLL}" DESTINATION "${BIN_DIR}")
      message(STATUS "Bundled: ${DLL_NAME}")
    endforeach()
  endforeach()

  message(STATUS "BundleDeps: Windows bundle complete (${BIN_DIR})")
endif()
