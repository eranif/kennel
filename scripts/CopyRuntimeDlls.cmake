# Copies the DLLs that an executable needs from the MSYS2 folder to the folder of
# the installer (it is run by CMake as a step after the build, on Windows).
#
#   cmake -DEXE=<exe> -DMSYS2_BIN=<C:/msys64/clang64/bin> -DDEST=<folder>
#         -P CopyRuntimeDlls.cmake
#
# The list comes from ntldd, so it follows the packages: a DLL that is renamed
# (zlib1.dll -> libz-1.dll, libcrypto-3 -> libcrypto-4) or a new dependency needs
# no change here. Only the DLLs of MSYS2 are copied: the ones of Windows are on
# every machine, and the others (wxWidgets, ConPTY) are copied by CMakeLists.txt.

foreach(_var EXE MSYS2_BIN DEST)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "CopyRuntimeDlls: -D${_var}=... is missing")
  endif()
endforeach()

find_program(_NTLDD ntldd HINTS "${MSYS2_BIN}")
if(NOT _NTLDD)
  message(
    FATAL_ERROR
      "ntldd not found (install mingw-w64-<arch>-ntldd with pacman): it lists the DLLs that Kennel needs"
  )
endif()

# ntldd finds the DLLs the way Windows does: the folder of the program, then PATH
set(ENV{PATH} "${MSYS2_BIN};$ENV{PATH}")
execute_process(
  COMMAND "${_NTLDD}" -R "${EXE}"
  OUTPUT_VARIABLE _output
  RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "ntldd failed (${_result}) for ${EXE}")
endif()


# The names of the DLLs that ntldd lists, wherever it found them: it looks in the
# folder of the program first, so a DLL that is copied already is listed there.
string(REGEX REPLACE "\r?\n" ";" _lines "${_output}")
set(_dlls "")
foreach(_line ${_lines})
  # "<tab>libz-1.dll => C:\msys64\clang64\bin\libz-1.dll (0x...)"
  if(_line MATCHES "([^ \t]+\.[Dd][Ll][Ll]) *=>")
    # The ones of Windows have no copy in MSYS2 and are skipped
    if(EXISTS "${MSYS2_BIN}/${CMAKE_MATCH_1}")
      list(APPEND _dlls "${MSYS2_BIN}/${CMAKE_MATCH_1}")
    endif()
  endif()
endforeach()
list(REMOVE_DUPLICATES _dlls)

if(NOT _dlls)
  message(FATAL_ERROR "No DLL of ${MSYS2_BIN} found for ${EXE}")
endif()
foreach(_dll ${_dlls})
  message(STATUS "Copying Runtime DLL: ${_dll}")
  file(COPY "${_dll}" DESTINATION "${DEST}")
endforeach()
