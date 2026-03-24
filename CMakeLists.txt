cmake_minimum_required(VERSION 3.21)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pi_pico_sbw C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

add_executable(pi_pico_sbw
    src/main.c
    src/sbw_hw.c
    src/sbw_jtag.c
    src/sbw_transport_gpio.c
)

target_include_directories(pi_pico_sbw PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(pi_pico_sbw PRIVATE
    pico_stdlib
    hardware_gpio
    hardware_timer
)

set(SBW_LTO_FLAGS
    -flto=auto
    -fno-fat-lto-objects
)

set_source_files_properties(
    src/sbw_hw.c
    src/sbw_jtag.c
    src/sbw_transport_gpio.c
    PROPERTIES COMPILE_OPTIONS "${SBW_LTO_FLAGS}"
)

target_link_options(pi_pico_sbw PRIVATE
    "${SBW_LTO_FLAGS}"
)

pico_enable_stdio_usb(pi_pico_sbw 1)
pico_enable_stdio_uart(pi_pico_sbw 0)

pico_add_extra_outputs(pi_pico_sbw)
