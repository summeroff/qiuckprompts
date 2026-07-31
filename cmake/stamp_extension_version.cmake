# Stamp Chrome MV3 companion version to match app PE (X.Y.Z).
# Chrome requires 1–4 dot-separated integers — no SemVer pre-release suffix.
#
# Usage:
#   cmake -DQP_EXT_DIR=... -DQP_EXT_VERSION=1.0.2 -P cmake/stamp_extension_version.cmake

if(NOT DEFINED QP_EXT_DIR OR QP_EXT_DIR STREQUAL "")
  message(FATAL_ERROR "stamp_extension_version: QP_EXT_DIR required")
endif()
if(NOT DEFINED QP_EXT_VERSION OR QP_EXT_VERSION STREQUAL "")
  message(FATAL_ERROR "stamp_extension_version: QP_EXT_VERSION required")
endif()

# Accept full SemVer and keep leading X.Y.Z only (Chrome-safe).
set(_ver "${QP_EXT_VERSION}")
if(_ver MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)")
  set(_ver "${CMAKE_MATCH_1}")
elseif(_ver MATCHES "^([0-9]+\\.[0-9]+)")
  set(_ver "${CMAKE_MATCH_1}.0")
elseif(_ver MATCHES "^([0-9]+)")
  set(_ver "${CMAKE_MATCH_1}.0.0")
else()
  message(FATAL_ERROR "stamp_extension_version: bad version '${QP_EXT_VERSION}'")
endif()

set(_mf "${QP_EXT_DIR}/manifest.json")
if(NOT EXISTS "${_mf}")
  message(FATAL_ERROR "stamp_extension_version: missing ${_mf}")
endif()

file(READ "${_mf}" _json)
if(NOT _json MATCHES "\"version\"[ \t]*:[ \t]*\"[^\"]*\"")
  message(FATAL_ERROR
    "stamp_extension_version: no \"version\" field in ${_mf} (format changed?)")
endif()

string(REGEX REPLACE "\"version\"[ \t]*:[ \t]*\"[^\"]*\""
                     "\"version\": \"${_ver}\""
                     _json "${_json}")

if(NOT _json MATCHES "\"version\"[ \t]*:[ \t]*\"${_ver}\"")
  message(FATAL_ERROR
    "stamp_extension_version: failed to set version to '${_ver}' in ${_mf}")
endif()

file(WRITE "${_mf}" "${_json}")
message(STATUS "Stamped extension version ${_ver} → ${_mf}")
