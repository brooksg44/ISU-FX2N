cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(ISU-FX2N C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(ISU-FX2N
    src/main.c
    src/board.c
    src/plc_memory.c
    src/plc_scan.c
    src/fx_protocol.c
    src/fx_addr.c
    src/fx_monitor.c
    src/plc_program.c
    src/plc_storage.c
    src/plc_exec.c
    src/modbus.c
    src/modbus_map.c
)

target_include_directories(ISU-FX2N PRIVATE src)
target_link_libraries(ISU-FX2N pico_stdlib hardware_adc hardware_flash hardware_uart)

# GX Works 2 talks to the PLC over USB CDC, so stdio must not claim UART0 -
# GPIO0/1 are reserved for the Modbus RTU port.
pico_enable_stdio_usb(ISU-FX2N 1)
pico_enable_stdio_uart(ISU-FX2N 0)

# The SDK gates BOTH CDC read and write on DTR being asserted. Terminal
# programs assert it; GX Works 2 does not, which makes the PLC look dead
# ("Cannot communicate with the specified CPU", ES:01808201). Treat the
# connection as always valid instead.
target_compile_definitions(ISU-FX2N PRIVATE
    PICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1

    # Consequence of the above: with the connection always assumed valid, a
    # write with no host reading spins until this timeout before discarding.
    # The default is 500 ms, which would stall the PLC scan - unacceptable in
    # a controller. GX Works performs some upload reads one byte at a time and
    # can leave the CDC endpoint full for more than 20 ms between phases. Give
    # a live programming connection enough time to consume the entire frame.
    # This wait occurs only while USB still reports connected but its FIFO is
    # full; normal PLC scans do not enter the output path.
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=500000
)

# Produces ISU-FX2N.uf2 in the build directory.
pico_add_extra_outputs(ISU-FX2N)
