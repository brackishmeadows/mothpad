cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Install pico-sdk and set PICO_SDK_PATH before configuring.")
endif()

include("$ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(mothpad_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(PICOCALC_SD_BOOT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../toolchains/PicoCalc-upstream/Code/pico_multi_booter/sd_boot"
    CACHE PATH "Path to ClockworkPi PicoCalc SD boot sources")
set(PICOCALC_HELLOWORLD_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../toolchains/PicoCalc-upstream/Code/picocalc_helloworld"
    CACHE PATH "Path to ClockworkPi PicoCalc hello-world sources for legacy diagnostics only")
set(PICO_VFS_DIR
    "${PICOCALC_SD_BOOT_DIR}/lib/pico-vfs"
    CACHE PATH "Path to pico-vfs sources")

add_subdirectory("${PICO_VFS_DIR}" pico-vfs)

add_executable(mothpad_pico
    ../src/mothpad.c
    mothpad_pico_main.c
    mothpad_picocalc_platform.c
)

target_include_directories(mothpad_pico PRIVATE
    ../src
    ${PICOCALC_SD_BOOT_DIR}
)

target_link_libraries(mothpad_pico
    pico_stdlib
    hardware_spi
    hardware_i2c
    blockdevice_sd
    filesystem_fat
    filesystem_vfs
)

pico_enable_stdio_usb(mothpad_pico 1)
pico_enable_stdio_uart(mothpad_pico 0)

pico_add_extra_outputs(mothpad_pico)

add_executable(mothpad_pico_legacy
    ../src/mothpad.c
    mothpad_pico_main.c
    mothpad_picocalc_platform_legacy.c
    ${PICOCALC_HELLOWORLD_DIR}/lcdspi/lcdspi.c
    ${PICOCALC_HELLOWORLD_DIR}/i2ckbd/i2ckbd.c
)

target_include_directories(mothpad_pico_legacy PRIVATE
    ../src
    ${PICOCALC_SD_BOOT_DIR}
    ${PICOCALC_HELLOWORLD_DIR}/lcdspi
    ${PICOCALC_HELLOWORLD_DIR}/i2ckbd
)

target_link_libraries(mothpad_pico_legacy
    pico_stdlib
    hardware_spi
    hardware_i2c
    blockdevice_sd
    filesystem_fat
    filesystem_vfs
)

pico_enable_stdio_usb(mothpad_pico_legacy 1)
pico_enable_stdio_uart(mothpad_pico_legacy 0)

pico_add_extra_outputs(mothpad_pico_legacy)

add_executable(mothpad_diag
    mothpad_pico_diag.c
    mothpad_picocalc_platform.c
)

target_include_directories(mothpad_diag PRIVATE
    ../src
)

target_link_libraries(mothpad_diag
    pico_stdlib
    hardware_spi
    hardware_i2c
)

pico_enable_stdio_usb(mothpad_diag 1)
pico_enable_stdio_uart(mothpad_diag 0)

pico_add_extra_outputs(mothpad_diag)

add_executable(mothpad_legacy_diag
    mothpad_pico_legacy_diag.c
    ${PICOCALC_HELLOWORLD_DIR}/lcdspi/lcdspi.c
    ${PICOCALC_HELLOWORLD_DIR}/i2ckbd/i2ckbd.c
)

target_include_directories(mothpad_legacy_diag PRIVATE
    ${PICOCALC_HELLOWORLD_DIR}/lcdspi
    ${PICOCALC_HELLOWORLD_DIR}/i2ckbd
)

target_link_libraries(mothpad_legacy_diag
    pico_stdlib
    hardware_spi
    hardware_i2c
)

pico_enable_stdio_usb(mothpad_legacy_diag 1)
pico_enable_stdio_uart(mothpad_legacy_diag 0)

pico_add_extra_outputs(mothpad_legacy_diag)
