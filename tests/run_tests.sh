#!/bin/sh
# Host-side test suite. These compile the portable parts of the firmware with
# the system compiler and run them on the PC - no Pico required.
#
# The portable/hardware split is deliberate: address mapping and device logic
# are where silent bugs hide, so they are kept free of SDK dependencies and
# tested here. Anything touching flash, USB or GPIO lives in files these tests
# do not compile.
set -e
cd "$(dirname "$0")"

CFLAGS="-Wall -Wextra -Werror -I../src"

echo "== FX protocol address mapping =="
cc $CFLAGS test_fx_addr.c ../src/fx_addr.c ../src/plc_memory.c \
    ../src/plc_program.c -o test_fx_addr
./test_fx_addr

echo "== Modbus address mapping =="
cc $CFLAGS test_modbus_map.c ../src/modbus_map.c ../src/plc_memory.c \
    -o test_modbus_map
./test_modbus_map

echo "All tests passed."
