#!/bin/bash -e

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${ROOT_DIR}/.build-release
WX_INSTALL_DIR=${BUILD_DIR}/wxWidgets-install
MACOS_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET:-13.0}
OS_NAME="$(uname -s)"
FORCE_CMAKE=0
DO_PACKAGE=0

# Version used by the "package" target. Empty: use the default of each package.
GLYPHT_VERSION=${GLYPHT_VERSION:-}

# The wxWidgets tag to build. Override with WX_VERSION.
WX_VERSION=${WX_VERSION:-v3.3.4}

# Set USE_OPENGL=0 or USE_OPENGL=1 to override the CMake default (ON on
# macOS and Windows, OFF on Linux).
USE_OPENGL=${USE_OPENGL:-}

. "${ROOT_DIR}/scripts/functions.rc"

function jobs() {
  if [[ "${OS_NAME}" == "Darwin" ]]; then
    sysctl -n hw.physicalcpu
  else
    nproc
  fi
}

function check_prerequistes() {
  INFO "Checking build prerequisites"
  local missing=0
  local compiler

  if [[ "${OS_NAME}" == "Linux" ]]; then
    compiler="g++"
  else
    compiler="clang++"
  fi

  for tool in ${compiler} cmake git make; do
    if command -v "${tool}" >/dev/null 2>&1; then
      INFO "Found ${tool}: $(command -v ${tool})"
    else
      ERROR "Missing required tool: ${tool}"
      missing=1
    fi
  done

  if [ "${missing}" -ne 0 ]; then
    ERROR "Please install the missing prerequisites and try again"
    exit 1
  fi
}

# Clones wxWidgets into ${BUILD_DIR}/wxWidgets and leaves the shell in a new
# empty .build-release directory inside it.
function fetch_wx_widgets() {
  INFO "Fetching wxWidgets ${WX_VERSION}"
  mkdir -p "${BUILD_DIR}"
  cd "${BUILD_DIR}"
  rm -fr wxWidgets # in case we aborted earlier
  git clone --depth 1 --branch "${WX_VERSION}" \
    https://github.com/wxWidgets/wxWidgets.git
  cd wxWidgets
  git submodule update --init --depth 1
  mkdir .build-release
  cd .build-release
}

function build_wx_widgets_Linux() {
  if [ -x "${WX_INSTALL_DIR}/bin/wx-config" ]; then
    INFO "wxWidgets already built at ${WX_INSTALL_DIR}; skipping"
    return 0
  fi

  fetch_wx_widgets
  INFO "Building wxWidgets"
  ../configure --disable-debug_flag --with-gtk=3 --enable-stl \
    --prefix="${WX_INSTALL_DIR}" --disable-shared --enable-monolithic
  make -j"$(jobs)" install
  cd "${ROOT_DIR}"
}

function build_wx_widgets_macOS() {
  if [ -x "${WX_INSTALL_DIR}/bin/wx-config" ]; then
    INFO "wxWidgets already built at ${WX_INSTALL_DIR}; skipping"
    return 0
  fi

  fetch_wx_widgets
  INFO "Building wxWidgets"
  cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET}" \
    -DwxBUILD_DEBUG_LEVEL=0 \
    -DwxBUILD_MONOLITHIC=1 \
    -DwxBUILD_SAMPLES=OFF \
    -DwxUSE_SYS_LIBS=OFF \
    -DwxUSE_LUNASVG=OFF \
    -DCMAKE_INSTALL_PREFIX="${WX_INSTALL_DIR}"
  make -j"$(jobs)" install
  cd "${ROOT_DIR}"
}

function build_wx_widgets_MSW() {
  if ls "${WX_INSTALL_DIR}"/lib/clang*/wxmsw*.dll >/dev/null 2>&1; then
    INFO "wxWidgets DLLs already found in ${WX_INSTALL_DIR}; skipping build"
    return 0
  fi

  fetch_wx_widgets
  INFO "Building wxWidgets"
  cmake .. -G"MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release \
    -DwxBUILD_DEBUG_LEVEL=0 \
    -DwxBUILD_MONOLITHIC=1 -DwxBUILD_SAMPLES=OFF -DwxUSE_STL=ON \
    -DCMAKE_INSTALL_PREFIX="${WX_INSTALL_DIR}"
  make -j"$(jobs)" install
  cd "${ROOT_DIR}"
}

# Configures (when needed) and builds wxTerminalEmulator.
# Arguments: extra cmake arguments.
function build_glypht() {
  INFO "Building wxTerminalEmulator"
  mkdir -p "${BUILD_DIR}"
  cd "${BUILD_DIR}"

  local cmake_args=(-DCMAKE_BUILD_TYPE=Release "$@")
  if [ -n "${USE_OPENGL}" ]; then
    cmake_args+=(-DUSE_OPENGL="$([ "${USE_OPENGL}" = "1" ] && echo ON || echo OFF)")
  fi

  # Configure if the build tree has not been generated yet, or if
  # CMakeLists.txt is newer than the generated Makefile.
  if [ "${FORCE_CMAKE}" = "1" ] ||
    [ ! -f "${BUILD_DIR}/CMakeCache.txt" ] ||
    [ "${ROOT_DIR}/CMakeLists.txt" -nt "${BUILD_DIR}/Makefile" ]; then
    INFO "Configuring wxTerminalEmulator"
    cmake "${ROOT_DIR}" "${cmake_args[@]}"
  else
    INFO "wxTerminalEmulator already configured; skipping cmake"
  fi
  make -j"$(jobs)"
  INFO "wxTerminalEmulator built successfully"
  cd "${ROOT_DIR}"
}

# Prints the path of the wx-config to use. WX_CONFIG overrides the local build.
function wx_config_path() {
  local wx_config=${WX_CONFIG:-${WX_INSTALL_DIR}/bin/wx-config}
  if [ ! -x "${wx_config}" ]; then
    ERROR "wx-config not found at ${wx_config}"
    exit 1
  fi
  echo "${wx_config}"
}

function build_Linux() {
  if [ -z "${WX_CONFIG}" ]; then
    build_wx_widgets_Linux
  fi
  local wx_config
  wx_config=$(wx_config_path)
  INFO "Using wx-config: ${wx_config}"
  build_glypht -DwxWidgets_CONFIG_EXECUTABLE="${wx_config}"

  INFO ""
  INFO "To run GlyphT:"
  INFO "=============="
  INFO "${BUILD_DIR}/glypht"
  INFO ""
}

function build_macOS() {
  if [ -z "${WX_CONFIG}" ]; then
    build_wx_widgets_macOS
  fi
  local wx_config
  wx_config=$(wx_config_path)
  INFO "Using wx-config: ${wx_config}"
  build_glypht -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET}" \
    -DwxWidgets_CONFIG_EXECUTABLE="${wx_config}"

  INFO ""
  INFO "To run GlyphT:"
  INFO "=============="
  INFO "open ${BUILD_DIR}/glypht.app"
  INFO ""
  INFO "To copy it to the Desktop (close GlyphT first):"
  INFO "=============================================="
  INFO "${ROOT_DIR}/scripts/macos-update-glypht.sh"
  INFO ""
}

function build_MSW() {
  build_wx_widgets_MSW
  export WXWIN="${WX_INSTALL_DIR}"
  INFO "WXWIN is set to '${WXWIN}'"
  build_glypht -DWXWIN="${WXWIN}"

  INFO ""
  INFO "To run GlyphT:"
  INFO "=============="
  INFO "${BUILD_DIR}/install/bin/glypht.exe"
  INFO ""
}

# The "package" target. Each function runs after a successful build and leaves
# the package in ${BUILD_DIR}.

function package_MSW() {
  local cmake_args=()
  if [ -n "${GLYPHT_VERSION}" ]; then
    cmake_args+=(-DGLYPHT_VERSION="${GLYPHT_VERSION}")
    cmake "${BUILD_DIR}" "${cmake_args[@]}" >/dev/null
  fi
  # The "setup" target only exists when CMake found the Inno Setup compiler.
  if ! grep -q "^glypht_setup:" "${BUILD_DIR}/Makefile"; then
    ERROR "No 'setup' target: Inno Setup 6 (ISCC.exe) was not found by CMake"
    ERROR "Install it from https://jrsoftware.org/ and run: ./build.sh --cmake package"
    exit 1
  fi
  INFO "Building the Windows installer"
  cmake --build "${BUILD_DIR}" --target glypht_setup
  INFO "Package: $(ls -t "${BUILD_DIR}"/glypht-*-installer.exe | head -n 1)"
}

function package_macOS() {
  local version=${GLYPHT_VERSION:-1.0.0}
  local zip="${BUILD_DIR}/glypht-${version}-macos-$(uname -m).zip"
  INFO "Creating ${zip}"
  rm -f "${zip}"
  # ditto keeps the symlinks and the permissions inside the .app bundle
  ditto -c -k --keepParent "${BUILD_DIR}/glypht.app" "${zip}"
  INFO "Package: ${zip}"
}

function package_Linux() {
  local version=${GLYPHT_VERSION:-1.0.0}
  local tarball="${BUILD_DIR}/glypht-${version}-linux-$(uname -m).tar.gz"
  INFO "Creating ${tarball}"
  rm -f "${tarball}"
  tar -czf "${tarball}" -C "${BUILD_DIR}" glypht glypht.svg themes
  INFO "Package: ${tarball}"
}

function package() {
  if [[ "${OS_NAME}" == *MINGW* ]]; then
    package_MSW
  elif [[ "${OS_NAME}" == "Darwin" ]]; then
    package_macOS
  elif [[ "${OS_NAME}" == "Linux" ]]; then
    package_Linux
  fi
}

function clean() {
  if [ ! -f "${BUILD_DIR}/Makefile" ]; then
    INFO "Nothing to clean"
    return 0
  fi
  INFO "Cleaning build artifacts"
  make -C "${BUILD_DIR}" clean
  INFO "Clean complete"
}

function distclean() {
  INFO "Removing build directory: ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
  INFO "Distclean complete"
}

function usage() {
  echo "Usage: $(basename "$0") [--cmake] [target]"
  echo ""
  echo "Options:"
  echo "  --cmake     Force the cmake configure stage"
  echo ""
  echo "Targets:"
  echo "  (none)      Build GlyphT (default)"
  echo "  package     Build GlyphT, then create the package for this OS:"
  echo "                Windows: Inno Setup installer (needs Inno Setup 6)"
  echo "                macOS:   glypht-<version>-macos-<arch>.zip"
  echo "                Linux:   glypht-<version>-linux-<arch>.tar.gz"
  echo "  clean       Remove build artifacts (make clean)"
  echo "  distclean   Remove the entire build directory, including wxWidgets"
  echo "  -h, --help  Show this help message"
  echo ""
  echo "Environment:"
  echo "  USE_OPENGL=0|1              Choose the renderer (default: ON on macOS and Windows, OFF on Linux)"
  echo "  WX_VERSION=<tag>            wxWidgets tag to build (default: ${WX_VERSION})"
  echo "  WX_CONFIG=<path>            Use this wx-config and skip building wxWidgets (macOS, Linux)"
  echo "  MACOS_DEPLOYMENT_TARGET=<v> macOS deployment target (default: ${MACOS_DEPLOYMENT_TARGET})"
  echo "  GLYPHT_VERSION=<x.y.z>      Version used in the package name (default: 1.0.0)"
}

function build() {
  if [[ "${OS_NAME}" == *MINGW* ]]; then
    INFO "On Windows"
    build_MSW
  elif [[ "${OS_NAME}" == "Darwin" ]]; then
    INFO "On macOS"
    build_macOS
  elif [[ "${OS_NAME}" == "Linux" ]]; then
    INFO "On Linux"
    build_Linux
  else
    ERROR "Unsupported operating system: ${OS_NAME}"
    exit 1
  fi
}

if [ "${1}" = "--cmake" ]; then
  FORCE_CMAKE=1
  shift
fi

case "${1}" in
clean)
  clean
  exit 0
  ;;
distclean)
  distclean
  exit 0
  ;;
-h | --help)
  usage
  exit 0
  ;;
package)
  DO_PACKAGE=1
  ;;
"") ;;
*)
  ERROR "Unknown target: ${1}"
  usage
  exit 1
  ;;
esac

INFO "Building wxTerminalEmulator"
check_prerequistes
build
if [ "${DO_PACKAGE}" = "1" ]; then
  package
fi
