#!/bin/bash -e

ROOT_DIR=$(dirname $(readlink -f $0))
BUILD_DIR=${ROOT_DIR}/.build-release
MACOS_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET:-13.0}
OS_NAME="$(uname -s)"

. ${ROOT_DIR}/scripts/functions.rc

INFO "Building Kennel"

function check_prerequistes() {
  INFO "Checking build prerequisites"
  local missing=0
  for tool in clang++ cmake git; do
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

function build_wx_widgets_macOS() {
  local wx_install_dir=${BUILD_DIR}/wxWidgets-install
  if [ -x "${wx_install_dir}/bin/wx-config" ]; then
    INFO "wxWidgets already built at ${wx_install_dir}; skipping"
    export PATH="${wx_install_dir}/bin":$PATH
    return 0
  fi

  INFO "Building wxWidgets"
  mkdir -p ${BUILD_DIR}
  cd $_
  git clone --depth 1 https://github.com/wxWidgets/wxWidgets.git
  cd wxWidgets
  git submodule update --init --depth 1
  mkdir .build-release
  cd .build-release
  cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET} \
    -DwxBUILD_DEBUG_LEVEL=0 \
    -DwxBUILD_MONOLITHIC=1 \
    -DwxBUILD_SAMPLES=OFF \
    -DwxUSE_SYS_LIBS=OFF \
    -DwxUSE_LUNASVG=OFF \
    -DCMAKE_INSTALL_PREFIX=${BUILD_DIR}/wxWidgets-install
  make -j$(sysctl -n hw.physicalcpu) install
  export PATH="${wx_install_dir}/bin":$PATH
  cd ${ROOT_DIR}
}

function build_wx_widgets_MSW() {
  local wx_install_dir=${BUILD_DIR}/wxWidgets-install

  if ls ${wx_install_dir}/lib/clang*/wxmsw*.dll >/dev/null 2>&1; then
    INFO "wxWidgets DLLs already found in ${wx_install_dir}; skipping build"
    return 0
  fi

  INFO "Building wxWidgets"
  mkdir -p ${BUILD_DIR}
  cd $_
  rm -fr wxWidgets # in case we aborted earlier
  git clone --depth 1 https://github.com/wxWidgets/wxWidgets.git
  cd wxWidgets
  git submodule update --init --depth 1
  mkdir .build-release
  cd .build-release
  cmake .. -G"MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release \
    -DwxBUILD_DEBUG_LEVEL=0 \
    -DwxBUILD_MONOLITHIC=1 -DwxBUILD_SAMPLES=OFF -DwxUSE_STL=ON \
    -DCMAKE_TLS_VERIFY=OFF \
    -DCMAKE_INSTALL_PREFIX=${BUILD_DIR}/wxWidgets-install

  make -j$(nproc) install
  export WXWIN="${BUILD_DIR}/wxWidgets-install"
  INFO "WXWIN is set to '${WXWIN}'"
  cd ${ROOT_DIR}
}

function build_kennel_macOS() {
  INFO "Building Kennel"
  local wx_install_dir=${BUILD_DIR}/wxWidgets-install
  local wx_config=${wx_install_dir}/bin/wx-config
  if [ ! -x "${wx_config}" ]; then
    ERROR "Local wx-config not found at ${wx_config}"
    exit 1
  fi
  INFO "Using local wx-config: ${wx_config}"

  local kennel_build_dir=${ROOT_DIR}/.build-release
  mkdir -p ${kennel_build_dir}
  cd ${kennel_build_dir}
  # Configure if the build tree has not been generated yet, or if
  # CMakeLists.txt is newer than the generated cache.
  if [ ! -f "${kennel_build_dir}/CMakeCache.txt" ] ||
    [ "${ROOT_DIR}/CMakeLists.txt" -nt "${kennel_build_dir}/Makefile" ] ||
    ! grep -q "^CMAKE_OSX_DEPLOYMENT_TARGET:STRING=${MACOS_DEPLOYMENT_TARGET}$" "${kennel_build_dir}/CMakeCache.txt"; then
    INFO "Configuring Kennel"
    cmake ${ROOT_DIR} -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_DEPLOYMENT_TARGET} \
      -DwxWidgets_CONFIG_EXECUTABLE=${wx_config}
  else
    INFO "Kennel already configured; skipping cmake"
  fi
  make -j$(sysctl -n hw.physicalcpu)
  INFO "Kennel built successfully"
  cd ${ROOT_DIR}

  INFO ""
  INFO "To run Kennel:"
  INFO "=============="
  INFO "open ${BUILD_DIR}/kennel.app"
  INFO ""
}

function build_kennel_MSW() {
  INFO "Building Kennel"
  local kennel_build_dir=${ROOT_DIR}/.build-release
  mkdir -p ${kennel_build_dir}
  cd ${kennel_build_dir}
  # Configure if the build tree has not been generated yet, or if
  # CMakeLists.txt is newer than the generated cache.
  if [ ! -f "${kennel_build_dir}/CMakeCache.txt" ] ||
    [ "${ROOT_DIR}/CMakeLists.txt" -nt "${kennel_build_dir}/Makefile" ]; then
    INFO "Configuring Kennel"
    cmake ${ROOT_DIR} -DCMAKE_BUILD_TYPE=Release -DWXWIN="${BUILD_DIR}/wxWidgets-install"
  else
    INFO "Kennel already configured; skipping cmake"
  fi
  make -j$(nproc)
  INFO "Kennel built successfully"
  cd ${ROOT_DIR}

  INFO ""
  INFO "To run Kennel:"
  INFO "=============="
  INFO ""
  INFO "${BUILD_DIR}/installer/bin/kennel.exe"
  INFO ""
}

function clean() {
  if [ ! -d "${BUILD_DIR}" ]; then
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
  echo "Usage: $(basename $0) [target]"
  echo ""
  echo "Targets:"
  echo "  (none)      Build Kennel (default)"
  echo "  clean       Remove build artifacts (make clean)"
  echo "  distclean   Remove the entire build directory"
  echo "  package     Create an installer suitable for the current platform"
  echo "  -h, --help  Show this help message"
}

function build() {
  if [[ "${OS_NAME}" == *MINGW* ]]; then
    INFO "On Windows"
    build_wx_widgets_MSW
    build_kennel_MSW
  fi

  if [[ "${OS_NAME}" == "Darwin" ]]; then
    INFO "On macOS"
    build_wx_widgets_macOS
    build_kennel_macOS
  fi
}

function package() {
  build
  if [[ "${OS_NAME}" == *MINGW* ]]; then
    cd ${BUILD_DIR}
    make -j$(nproc) setup/fast
  fi
 
  if [[ "${OS_NAME}" == "Darwin" ]]; then
    cd ${BUILD_DIR}

    if [ ! -d "kennel.app" ]; then
      ERROR "kennel.app not found in ${BUILD_DIR}"
      exit 1
    fi

    if [ -z "$KENNEL_PASSWORD" ]; then
      ERROR "KENNEL_PASSWORD environment variable is not set"
      exit 1
    fi

    rm -f *.zip
    ../macos-sign-app.sh --notarize --password $KENNEL_PASSWORD kennel.app
  fi

  # macOS bundle is always created.
}

case "${1}" in
clean)
  clean
  exit 0
  ;;
distclean)
  distclean
  exit 0
  ;;
package)
  package
  exit 0
  ;;
-h | --help)
  usage
  exit 0
  ;;
"") ;;
*)
  ERROR "Unknown target: ${1}"
  usage
  exit 1
  ;;
esac

check_prerequistes
build
