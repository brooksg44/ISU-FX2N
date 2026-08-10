# Rebuilding `ISU-FX2N.uf2`

This guide is intended to make the firmware reproducible after replacing a
computer or cloning the repository months later. CMake recreates
`build/CMakeFiles`, `CMakeCache.txt`, object files, and the other generated
files. They do not need to be backed up.

The repository keeps the finished student image at
[`build/ISU-FX2N.uf2`](build/ISU-FX2N.uf2), but the steps below rebuild it from
source.

## 1. Install the build tools

On Ubuntu or WSL:

```sh
sudo apt update
sudo apt install -y cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi git
```

Confirm that the important tools are available:

```sh
cmake --version
ninja --version
arm-none-eabi-gcc --version
git --version
```

## 2. Clone the project

For recovery on a new computer:

```sh
git clone https://github.com/brooksg44/ISU-FX2N.git
cd ISU-FX2N
```

If the project is already present, enter its root directory—the directory
containing `CMakeLists.txt`—and update it as appropriate before building.

## 3. Install the Raspberry Pi Pico SDK

This project has been verified with Pico SDK 2.3.0. Keep the SDK outside the
ISU-FX2N repository:

```sh
cd ..
git clone --branch 2.3.0 --depth 1 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init --recursive
cd ISU-FX2N
```

Set `PICO_SDK_PATH` to its absolute location. For example:

```sh
export PICO_SDK_PATH="$PWD/../pico-sdk"
```

The `export` applies to the current terminal. Add the same line, with a fixed
absolute path, to `~/.bashrc` if it should persist across new terminals.

Alternatively, the included `pico_sdk_import.cmake` can download the SDK while
configuring:

```sh
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico_w \
  -DPICO_SDK_FETCH_FROM_GIT=ON \
  -DPICO_SDK_FETCH_FROM_GIT_TAG=2.3.0
```

If this alternative succeeds, skip the configure command in the next section
and continue with its build command.

## 4. Configure and build the firmware

From the ISU-FX2N repository root:

```sh
cmake -S . -B build -G Ninja \
  -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH="$PICO_SDK_PATH"
cmake --build build
```

`pico_w` is the correct CMake board definition for the trainer's Pico WH; the
WH is a Pico W with headers fitted.

The main output is:

```text
build/ISU-FX2N.uf2
```

CMake also produces development/debug formats such as `.elf`, `.bin`, `.hex`,
and `.map` in the build directory.

## 5. Perform a clean rebuild

If an old build cache contains stale paths or settings, use a new build
directory instead of copying `CMakeFiles` from another computer:

```sh
cmake -S . -B build-fresh -G Ninja \
  -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH="$PICO_SDK_PATH"
cmake --build build-fresh
```

The clean output is `build-fresh/ISU-FX2N.uf2`. To update the student artifact
tracked by Git:

```sh
cp build-fresh/ISU-FX2N.uf2 build/ISU-FX2N.uf2
```

## 6. Run the host-side regression tests

The tests do not require a connected Pico:

```sh
./tests/run_tests.sh
```

All test groups should report zero failures before publishing firmware.

## 7. Install the UF2 on the trainer

1. Disconnect the Pico from USB.
2. Hold the Pico's **BOOTSEL** button.
3. Connect USB while continuing to hold **BOOTSEL**, then release it.
4. Open the `RPI-RP2` USB drive that appears.
5. Copy `build/ISU-FX2N.uf2` onto that drive.
6. The Pico automatically disconnects from bootloader storage and restarts
   with the new firmware.

## Common problems

### CMake cannot find the Pico SDK

The message usually says `SDK location was not specified`. Set an absolute SDK
path and configure again:

```sh
export PICO_SDK_PATH=/absolute/path/to/pico-sdk
cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w
```

### CMake retained an old SDK path or board selection

Configure into `build-fresh` as shown above. CMake caches settings in
`build/CMakeCache.txt`; the cache is disposable.

### `arm-none-eabi-gcc` is missing

Install the ARM embedded compiler package:

```sh
sudo apt install -y gcc-arm-none-eabi libnewlib-arm-none-eabi
```

### Ninja is missing

Install it with:

```sh
sudo apt install -y ninja-build
```

### Confirm which SDK and board CMake selected

After configuration:

```sh
grep -E 'PICO_BOARD:|PICO_SDK_PATH:' build/CMakeCache.txt
```

The board should be `pico_w`, and the SDK path should identify the intended
Pico SDK installation.

## Publishing a newly rebuilt student image

After the build and tests succeed:

```sh
git status --short
git add build/ISU-FX2N.uf2
git commit -m "Update student UF2 firmware"
git push origin main
```

Review `git status` before committing so unrelated GX Works projects, captures,
or local test files are not accidentally included.
