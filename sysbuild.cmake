#
# Copyright (c) 2024-2025 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# ---------------------------------------------------------------------------
# Auto-sync BT_FAST_PAIR_FMDN_DULT firmware version from the VERSION file.
# This removes the need to manually update the conf files when bumping versions.
# ---------------------------------------------------------------------------
file(STRINGS "${APP_DIR}/VERSION" _version_lines)
foreach(_line ${_version_lines})
  if(_line MATCHES "^VERSION_MAJOR[ \t]*=[ \t]*([0-9]+)")
    set(_ver_major ${CMAKE_MATCH_1})
  elseif(_line MATCHES "^VERSION_MINOR[ \t]*=[ \t]*([0-9]+)")
    set(_ver_minor ${CMAKE_MATCH_1})
  elseif(_line MATCHES "^PATCHLEVEL[ \t]*=[ \t]*([0-9]+)")
    set(_ver_patch ${CMAKE_MATCH_1})
  endif()
endforeach()

if(NOT DEFINED _ver_major OR NOT DEFINED _ver_minor OR NOT DEFINED _ver_patch)
  message(FATAL_ERROR "Failed to parse VERSION file at ${APP_DIR}/VERSION")
endif()

message(STATUS "App version: ${_ver_major}.${_ver_minor}.${_ver_patch} — syncing DULT Kconfig")
set_config_int(${DEFAULT_IMAGE} CONFIG_BT_FAST_PAIR_FMDN_DULT_FIRMWARE_VERSION_MAJOR    ${_ver_major})
set_config_int(${DEFAULT_IMAGE} CONFIG_BT_FAST_PAIR_FMDN_DULT_FIRMWARE_VERSION_MINOR    ${_ver_minor})
set_config_int(${DEFAULT_IMAGE} CONFIG_BT_FAST_PAIR_FMDN_DULT_FIRMWARE_VERSION_REVISION ${_ver_patch})

if(SB_CONFIG_APP_DFU)
  set_config_bool(${DEFAULT_IMAGE} CONFIG_APP_DFU y)

  if(DEFINED SB_CONFIG_NETCORE_IMAGE_NAME AND NOT (SB_CONFIG_NETCORE_IMAGE_NAME STREQUAL ""))
    set(dfu_speedup_kconfig_fragment
        ${APP_DIR}/sysbuild/common/dfu_speedup_fragment.conf
    )

    if (EXISTS ${dfu_speedup_kconfig_fragment})
      add_overlay_config(
          ${SB_CONFIG_NETCORE_IMAGE_NAME}
          ${dfu_speedup_kconfig_fragment}
      )
    else()
      message(WARNING "DFU speedup fragment for netcore not found")
    endif()
  endif()
else()
  set_config_bool(${DEFAULT_IMAGE} CONFIG_APP_DFU n)
endif()
