cmake_minimum_required(VERSION 3.16)
set(PROJECT_VERSION 1.2.0)
project(
  kennel
  VERSION "${PROJECT_VERSION}"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Debug
      CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# wxTerminalEmulator (provides the wxterminal_lib static library). Fetched via
# FetchContent; pinned to a release tag.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
  wxTerminalEmulator
  GIT_REPOSITORY https://github.com/eranif/wxTerminalEmulator.git
  GIT_TAG v1.0.0)

set(BUILD_WXTERMINAL_DEMO
    OFF
    CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(wxTerminalEmulator)

# ---------------------------------------------------------------------------
# Locate wxWidgets: use WXWIN environment variable to find the installation. If
# WXWIN is not set, fall back to standard search paths. Automatically finds the
# latest wxWidgets version in lib/cmake/
# ---------------------------------------------------------------------------
if(WIN32)
  if(NOT WXWIN)
    message(FATAL_ERROR "Please pass WXWIN: -DWXWIN=/path/to/wxWidgets")
  endif()

  if(NOT DEFINED ENV{MSYS2_BASE})
    message(
      FATAL_ERROR
        "Set the environment variable MSYS2_BASE to point to the installation of your MSYS2. e.g. export MSYS2_BASE=C:/msys64"
    )
  endif()
  set(MSYS2_BASE $ENV{MSYS2_BASE})
  string(TOLOWER "$ENV{MSYSTEM}" MSYSTEM)

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

# Find OpenGL (required by wxWidgets)
if(WIN32)
  find_package(OpenGL REQUIRED)
endif()

# ---------------------------------------------------------------------------
# wxWidgets (3.2.x+). The emulator already locates it, but Kennel's own UI code
# needs the include dirs/libs too.
# ---------------------------------------------------------------------------
if(WIN32)
  find_package(wxWidgets CONFIG REQUIRED)
else()
  find_package(
    wxWidgets 3.2
    COMPONENTS core
               base
               aui
               stc
               xrc
               html
               adv)
endif()

# The installed wxWidgets is built with wxDEBUG_LEVEL=0; match it so our code
# does not reference assert symbols (wxOnAssert, wxTrap, ...) absent from the
# library. The emulator submodule applies the same workaround.
add_compile_definitions(wxDEBUG_LEVEL=0)

# ---------------------------------------------------------------------------
# kennel_core: UI-agnostic, testable logic (domain, adapters, persistence).
# Starts as a thin library holding shared constants; grows in later steps.
# ---------------------------------------------------------------------------
add_library(
  kennel_core STATIC
  src/core/Version.cpp
  src/core/AppManager.cpp
  src/core/AppPaths.cpp
  src/core/Bitmaps.cpp
  src/core/Config.cpp
  src/core/ConfigStore.cpp
  src/core/ClientAdapter.cpp
  src/core/AdapterRegistry.cpp
  src/core/ActivityMonitor.cpp
  src/core/JsonUtil.cpp
  src/core/WorkspaceStore.cpp
  src/core/WorkspaceManager.cpp
  src/core/UiPrefsStore.cpp
  src/core/Logger.cpp
  src/core/SshConfigParser.cpp
  src/core/HostsStore.cpp
  src/core/Process.cpp
  src/core/Helpers.cpp
  src/core/KennelRemote.cpp
  src/core/CrashHandler.cpp)

target_include_directories(kennel_core PUBLIC src ${wxWidgets_INCLUDE_DIRS})
target_compile_definitions(kennel_core PUBLIC ${wxWidgets_DEFINITIONS})
target_compile_features(kennel_core PUBLIC cxx_std_20)
# kennel_core uses only wxBase types (wxFileName, wxString); link the wx libs so
# it stays GUI-free yet self-contained.
target_link_libraries(kennel_core PUBLIC ${wxWidgets_LIBRARIES})

# ---------------------------------------------------------------------------
# kennel: the wxWidgets desktop application.
# ---------------------------------------------------------------------------
set(KENNEL_APP_SOURCES
    src/app/KennelApp.cpp
    src/app/AcceleratorInterceptor.cpp
    src/app/MainFrame.cpp
    src/app/MainView.cpp
    src/app/SessionPage.cpp
    src/app/StartAgentDialog.cpp
    src/app/AssetBootstrap.cpp
    src/app/AboutDialog.cpp
    src/app/EditFileDlg.cpp
    src/app/EditAgentsDlg.cpp
    src/app/EditAgentDlg.cpp
    src/app/Editor.cpp
    src/app/SettingsDlg.cpp
    src/app/EditHosts.cpp
    src/app/ThemeLoader.cpp
    src/app/ThemeManager.cpp
    src/app/UI.cpp
    src/app/UI_app_bitmaps.cpp
    src/app/NewEnvVarDlg.cpp
    src/app/AppUIGlobals.cpp
    src/app/NewAgentWizard.cpp
    src/app/SessionGroup.cpp
    src/app/FileBrowserDlg.cpp)

if(APPLE)
  list(APPEND KENNEL_APP_SOURCES src/app/AuiTabArtMac.cpp)
else()
  list(APPEND KENNEL_APP_SOURCES src/app/AuiTabArtDefault.cpp)
endif()

# Built-in assets shipped with the app: client icons (SVG) and terminal colour
# themes (TOML). SVGs are copied into ~/.kennel/assets on first run so users can
# edit/replace them; TOML themes are read in place (not copied). Both are
# shipped into the same on-disk assets location.
file(GLOB KENNEL_ASSET_FILES "${CMAKE_SOURCE_DIR}/assets/*.svg")

file(GLOB KENNEL_THEME_FILES "${CMAKE_SOURCE_DIR}/assets/themes/*.toml")

# Include LICENSE file in the bundle
list(APPEND KENNEL_ASSET_FILES "${CMAKE_SOURCE_DIR}/LICENSE")

if(APPLE)
  # Bundle the SVGs under Kennel.app/Contents/Resources/assets. Bundle LICENSE
  # at the root of Resources. Bundle the TOMLs under
  # Kennel.app/Contents/Resources/assets/themes
  set_source_files_properties(
    ${KENNEL_ASSET_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets")
  set_source_files_properties(
    ${KENNEL_THEME_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION
                                     "Resources/assets/themes")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/LICENSE"
                              PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

  # App icon: a pre-generated .icns committed in assets/ (regenerate from the
  # SVG with assets/regen-icns.sh when the icon changes). Bundle it into
  # Resources/ and point CFBundleIconFile at it via the generated Info.plist.
  set(KENNEL_ICNS "${CMAKE_SOURCE_DIR}/assets/kennel.icns")
  if(EXISTS "${KENNEL_ICNS}")
    set_source_files_properties("${KENNEL_ICNS}"
                                PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set(MACOSX_BUNDLE_ICON_FILE "kennel.icns")
    list(APPEND KENNEL_ASSET_FILES "${KENNEL_ICNS}")
  endif()

  add_executable(kennel MACOSX_BUNDLE ${KENNEL_APP_SOURCES}
                                      ${KENNEL_ASSET_FILES}
                                      ${KENNEL_THEME_FILES})
  set_target_properties(
    kennel
    PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.eranif.kennel"
               MACOSX_BUNDLE_BUNDLE_NAME "Kennel"
               MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
               MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
               MACOSX_BUNDLE_COPYRIGHT "Copyright © Eran Ifrah")
elseif(WIN32)
  # Include Windows resource file for version info and icon
  set(KENNEL_RC_FILE "${CMAKE_SOURCE_DIR}/src/app/kennel.rc")
  if(EXISTS "${KENNEL_RC_FILE}")
    list(APPEND KENNEL_APP_SOURCES "${KENNEL_RC_FILE}")
    message(STATUS "Including Windows resource file: ${KENNEL_RC_FILE}")
  endif()

  add_executable(kennel WIN32 ${KENNEL_APP_SOURCES})
else()
  add_executable(kennel ${KENNEL_APP_SOURCES})
endif()

# Non-bundle platforms: stage the assets next to the executable so a freshly
# built binary finds them when run in-tree (ShippedAssetsDir() checks here).
if(NOT APPLE)
  # Copy assets (SVGs and themes only, not LICENSE)
  file(GLOB KENNEL_ASSET_FILES_ONLY "${CMAKE_SOURCE_DIR}/assets/*.svg")
  file(GLOB KENNEL_ASSET_TOML_FILES_ONLY
       "${CMAKE_SOURCE_DIR}/assets/themes/*.toml")
  add_custom_command(
    TARGET kennel
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:kennel>/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:kennel>/assets/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${KENNEL_ASSET_FILES_ONLY}
            "$<TARGET_FILE_DIR:kennel>/assets"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different ${KENNEL_ASSET_TOML_FILES_ONLY}
      "$<TARGET_FILE_DIR:kennel>/assets/themes"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE"
            "$<TARGET_FILE_DIR:kennel>/LICENSE"
    COMMENT "Staging assets and license next to the kennel executable"
    VERBATIM)

  # Windows: copy wxWidgets DLLs next to the executable
  if(WIN32)
    add_custom_command(
      TARGET kennel
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:wx::wxmono>"
              "$<TARGET_FILE_DIR:kennel>/"
      COMMENT "Copying wxWidgets DLL next to kennel executable"
      VERBATIM)
  endif()
endif()

# Install rules. On Linux the icons land in <prefix>/share/kennel/assets (e.g.
# /usr/share/kennel/assets for a system package), which ShippedAssetsDir()
# resolves from the executable location.
include(GNUInstallDirs)
if(APPLE)
  install(TARGETS kennel BUNDLE DESTINATION .)
else()
  install(TARGETS kennel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # Assets (SVGs and themes)
  file(GLOB KENNEL_ASSET_FILES_ONLY "${CMAKE_SOURCE_DIR}/assets/*.svg")
  file(GLOB KENNEL_ASSET_THEME_FILES_ONLY
       "${CMAKE_SOURCE_DIR}/assets/themes/*.toml")
  install(FILES ${KENNEL_ASSET_FILES_ONLY}
          DESTINATION ${CMAKE_INSTALL_DATADIR}/kennel/assets)
  install(FILES ${KENNEL_ASSET_FILES_ONLY}
          DESTINATION ${CMAKE_INSTALL_DATADIR}/kennel/assets/themes)
  # License
  install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
          DESTINATION ${CMAKE_INSTALL_DATADIR}/kennel)
endif()

target_link_libraries(kennel PRIVATE kennel_core wxterminal_lib
                                     ${wxWidgets_LIBRARIES})
target_include_directories(kennel PRIVATE src src/app
                                          ${wxterminalemulator_SOURCE_DIR})

# ---------------------------------------------------------------------------
# macOS: bundle the wxWidgets dylib inside the .app so the app is
# self-contained. Copies the resolved dylib into Contents/Frameworks/, then
# rewrites the install name in both the dylib and the app binary to use
# @executable_path.
# ---------------------------------------------------------------------------
if(APPLE)
  # Resolve the wx dylib path and its embedded install name at configure time.
  # wx-config --libs gives us -L<dir> and -l<soname>; the symlink target reveals
  # the versioned file name that is baked into the dylib's LC_ID_DYLIB.
  execute_process(
    COMMAND wx-config --libs
    OUTPUT_VARIABLE _WX_LINK_FLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX MATCH "-L([^ ;]+)" _ "${_WX_LINK_FLAGS}")
  set(_WX_LIB_DIR "${CMAKE_MATCH_1}")
  string(REGEX MATCH "-l(wx_[^ ;]+)" _ "${_WX_LINK_FLAGS}")
  set(_WX_SONAME "lib${CMAKE_MATCH_1}.dylib") # e.g. libwx_osx_cocoau-3.3.dylib
  set(_WX_DYLIB_LINK "${_WX_LIB_DIR}/${_WX_SONAME}") # may be a symlink

  # Read the install name embedded in the dylib's LC_ID_DYLIB — this is the
  # exact token the linker records in the app binary (e.g.
  # @rpath/libwx_osx_cocoau-3.3.3.dylib)
  execute_process(
    COMMAND otool -D "${_WX_DYLIB_LINK}"
    OUTPUT_VARIABLE _OTOOL_OUT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  # otool -D prints "<path>:\n<install-name>"; grab the second line
  string(REGEX MATCH "\n([^\n]+)$" _ "${_OTOOL_OUT}")
  set(_WX_EMBEDDED_ID "${CMAKE_MATCH_1}") # @rpath/libwx_osx_cocoau-3.3.3.dylib
  # Strip "@rpath/" prefix to get the bare filename used inside the bundle
  string(REPLACE "@rpath/" "" _WX_BUNDLE_NAME "${_WX_EMBEDDED_ID}"
  )# libwx_osx_cocoau-3.3.3.dylib

  # Resolve the symlink so cmake -E copy gets the real file (not the symlink)
  execute_process(
    COMMAND readlink -f "${_WX_DYLIB_LINK}"
    OUTPUT_VARIABLE _WX_DYLIB_REAL
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  set(_FRAMEWORKS_DIR "$<TARGET_BUNDLE_CONTENT_DIR:kennel>/Frameworks")
  set(_WX_DYLIB_DST "${_FRAMEWORKS_DIR}/${_WX_BUNDLE_NAME}")
  # @executable_path -> .app/Contents/MacOS; Frameworks is ../Frameworks from
  # there
  set(_WX_NEW_NAME "@executable_path/../Frameworks/${_WX_BUNDLE_NAME}")

  add_custom_command(
    TARGET kennel
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_FRAMEWORKS_DIR}"
    # Copy the real (resolved) dylib under the canonical bundle name
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_WX_DYLIB_REAL}"
            "${_WX_DYLIB_DST}"
    # Rewrite the dylib's own install name
    COMMAND install_name_tool -id "${_WX_NEW_NAME}" "${_WX_DYLIB_DST}"
    # Rewrite the reference inside the app binary (matches LC_ID_DYLIB token)
    COMMAND install_name_tool -change "${_WX_EMBEDDED_ID}" "${_WX_NEW_NAME}"
            "$<TARGET_FILE:kennel>"
    COMMENT "Bundling ${_WX_BUNDLE_NAME} into .app/Contents/Frameworks/"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Linux: Set rpath and stage wxWidgets libraries for a self-contained app
# Creates install structure: install/bin, install/lib,
# install/share/kennel/assets
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
  # Get the wxWidgets library path and copy shared libraries
  execute_process(
    COMMAND wx-config --libs
    OUTPUT_VARIABLE _WX_LINK_FLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REGEX MATCH "-L([^ ;]+)" _ "${_WX_LINK_FLAGS}")
  set(_WX_LIB_DIR "${CMAKE_MATCH_1}")

  # Resolve the wx shared lib symlink to get the real filename
  file(GLOB _WX_LIBS "${_WX_LIB_DIR}/libwx_gtk3u*.so*")

  if(_WX_LIBS)
    # Create install directory structure
    set(_INSTALL_LIB_DIR "${CMAKE_BINARY_DIR}/install/lib")
    set(_INSTALL_BIN_DIR "${CMAKE_BINARY_DIR}/install/bin")
    set(_INSTALL_SHARE_DIR "${CMAKE_BINARY_DIR}/install/share/kennel/assets")

    add_custom_command(
      TARGET kennel
      POST_BUILD
      # Create directories
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_INSTALL_LIB_DIR}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_INSTALL_BIN_DIR}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_INSTALL_SHARE_DIR}"
      # Copy wxWidgets libraries
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_WX_LIBS}
              "${_INSTALL_LIB_DIR}/"
      # Copy kennel binary
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kennel>"
              "${_INSTALL_BIN_DIR}/"
      # Copy assets
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets"
              "${_INSTALL_SHARE_DIR}/"
      # Copy LICENSE
      COMMAND
        ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_BINARY_DIR}/install/share/kennel/"
      # Set rpath on the executable in install/bin to find libraries relative to
      # installation
      COMMAND patchelf --set-rpath "\$ORIGIN/../lib"
              "${_INSTALL_BIN_DIR}/kennel"
      # Also set rpath on the executable in the build directory
      COMMAND patchelf --set-rpath "\$ORIGIN/../wxWidgets-install/lib"
              "$<TARGET_FILE:kennel>"
      COMMENT "Setting up self-contained Linux install directory with rpath"
      VERBATIM)
  endif()
endif()

# ---------------------------------------------------------------------------
# Copy the generated compile_commands.json to the workspace root after build so
# editors/tooling at the repo root pick it up.
# ---------------------------------------------------------------------------
add_custom_command(
  TARGET kennel
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_SOURCE_DIR}/compile_commands.json"
  COMMENT "Copying compile_commands.json to workspace root"
  VERBATIM)

# ---------------------------------------------------------------------------
# Windows: Setup installer directory structure (in build tree for InnoSetup)
# This creates .build-*/installer/ with bin/, assets/, and all necessary files
# Automatically resolves and copies DLL dependencies using cygcheck
# ---------------------------------------------------------------------------
if(WIN32)
  set(_INSTALLER_DIR "${CMAKE_BINARY_DIR}/installer")
  list(APPEND SYSTEM_DLLS libc++.dll libomp.dll glew32.dll libunwind.dll)

  add_custom_command(
    TARGET kennel
    POST_BUILD
    # Create directory structure
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_INSTALLER_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_INSTALLER_DIR}/assets"
    # Copy kennel executable
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:kennel>"
            "${_INSTALLER_DIR}/bin/"
    # Copy wxWidgets DLL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:wx::wxmono>"
            "${_INSTALLER_DIR}/bin/"
    # Copy LICENSE
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE"
            "${_INSTALLER_DIR}/"
    # Copy all assets (themes and icons)
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets"
            "${_INSTALLER_DIR}/assets"
    COMMENT
      "Setting up installer directory with dependencies in build tree for InnoSetup"
    VERBATIM)

  foreach(DLL ${SYSTEM_DLLS})
    add_custom_command(
      TARGET kennel
      POST_BUILD
      # Copy wxWidgets DLL
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${MSYS2_BASE}/${MSYSTEM}/bin/${DLL}" "${_INSTALLER_DIR}/bin/"
      COMMENT "Copying Runtime DLL: ${MSYS2_BASE}/${MSYSTEM}/bin/${DLL}"
      VERBATIM)
  endforeach()

  set(INSTALLER_SOURCE_DIR "${CMAKE_BINARY_DIR}/installer")

  # Detect processor architecture for installer filename
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(PROCESSOR_ARCHITECTURE "amd64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
    set(PROCESSOR_ARCHITECTURE "arm64")
  else()
    set(PROCESSOR_ARCHITECTURE "unknown")
  endif()

  configure_file(${CMAKE_SOURCE_DIR}/installer.iss.in
                 ${CMAKE_BINARY_DIR}/installer.iss)

  find_program(
    INNO_SETUP_CMP
    NAMES "ISCC" "ISCC.exe"
    PATHS "c:/Program Files (x86)/Inno Setup 6")
  if(NOT INNO_SETUP_CMP)
    message(
      WARNING
        "\n** Could not locate ISCC compiler! \"setup\" target is not created **"
    )
    message(
      WARNING
        "\n** You can install InnoSetup compiler from here: https://jrsoftware.org/ **\n"
    )
  else()
    message(STATUS "InnoSetup compiler is set to: ${INNO_SETUP_CMP}")

    # fake target that executes "install". we then use this target as the
    # dependency for the `setup` target
    add_custom_target(
      kennel-install COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}"
                             --target install)

    add_custom_target(
      setup
      COMMAND "${INNO_SETUP_CMP}" ${CMAKE_BINARY_DIR}/installer.iss
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
      DEPENDS kennel-install)
  endif()
endif()
