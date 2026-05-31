#
# Copyright (c) 2024-2025 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

# The sample uses a separate directory for configuration files.
#set(APPLICATION_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/configuration")
set(APPLICATION_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(locator_tag_1)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/factory_reset.c
  src/fp_adv.c
)

zephyr_linker_sources(SECTIONS src/factory_reset_callbacks.ld)

zephyr_linker_sources(DATA_SECTIONS src/fp_adv_trigger.ld)

if (CONFIG_APP_DFU)
  target_sources(app PRIVATE src/dfu.c)
endif()

if (NOT CONFIG_BT_FAST_PAIR_FMDN_RING_COMP_NONE)
  target_sources(app PRIVATE src/ring.c)
endif()

target_include_directories(app PRIVATE include)

add_subdirectory(src/ui)
target_link_libraries(app PRIVATE app_ui)

add_subdirectory(src/battery)
target_link_libraries(app PRIVATE app_battery)

#if (CONFIG_BT_FAST_PAIR_FMDN_DULT_MOTION_DETECTOR)
  add_subdirectory(src/motion_detector)
  target_link_libraries(app PRIVATE app_motion_detector)
#endif()

#target_sources(app PRIVATE src/nvs_storage.c)
target_sources(app PRIVATE src/vision_advt.c)
target_sources(app PRIVATE src/vision_custom_services.c)
target_sources(app PRIVATE src/vision_production.c)

# NORDIC SDK APP END
