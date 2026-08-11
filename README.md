# ISU-FX2N

Student-ready firmware: [download `ISU-FX2N.uf2`](build/ISU-FX2N.uf2).

An open-source, FX2N-compatible PLC for the Raspberry Pi Pico WH, programmable
from **Mitsubishi GX Works 2** or GX Developer.

Built for the Idaho State University PLC trainer so students can read, modify
and rebuild every layer of the stack — from the pin map up through the ladder
interpreter to the wire protocol.

---

## Quick start

1. Hold **BOOTSEL**, plug the Pico into USB, and copy **`build/ISU-FX2N.uf2`**
   onto the drive that appears.
2. In GX Works 2, create a project of type **FXCPU / FX2N(C)**.
3. **Connection Destination** → PC side **Serial USB**, RS-232C, and pick the
   COM port the Pico enumerated as. Baud rate is ignored (see below).
4. **Online → Write to PLC** to download, then **Online → Remote Operation →
   RUN** to start your program. A download always stops the PLC.

`sts0` (GPIO2) lit means RUN. Your program is saved to flash and survives a
power cycle.

---

## Hardware

Pin assignments follow `FX1N-Trainer-Pins.pdf`. Note that inputs ascend and
outputs *descend* — this is a property of the trainer wiring, not a mistake.

| Function | GPIO | Notes |
|---|---|---|
| Inputs I0–I9 | 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 | X0–X7, X10, X11 (octal) |
| Outputs O0–O6 | 22, 21, 20, 19, 18, 17, 16 | Y0–Y6, descending |
| Analog AI0–AI2 | 28, 27, 26 | D8030–D8032, raw 0–4095 |
| sts0 | 2 | RUN indicator |
| sts1 | 3 | communications diagnostic |
| Modbus TX/RX | 0, 1 | Modbus RTU slave |
| reserve | 4, 5 | unused |

**Both banks are active-HIGH.** A pushbutton connects 3.3 V to an input pin;
an LED cathode is grounded, so a high output pin lights it. Inputs use internal
pull-downs so an open contact reads as 0.

**There is no RUN/STOP switch.** A real FX2N has a physical one and the
trainer has no pin for it, so RUN/STOP is controlled entirely from GX Works 2
under **Online → Remote Operation**. All ten trainer inputs stay available to
your program.

---

## Device model

Matches a real FX2N.

| Device | Range | Notes |
|---|---|---|
| X | X0–X377 (octal), 256 points | 10 wired on the trainer |
| Y | Y0–Y377 (octal), 256 points | 7 wired on the trainer |
| M | M0–M3071 | plus M8000–M8255 special |
| S | S0–S999 | state relays for STL |
| T | T0–T255 | T0–199 = 100 ms, T200–245 = 10 ms, T246–249 = 1 ms retentive, T250–255 = 100 ms retentive |
| C | C0–C255 | C0–199 16-bit up, C200–255 32-bit bidirectional |
| D | D0–D7999 | plus D8000–D8255 special |

Special devices maintained by the runtime:

| Device | Meaning |
|---|---|
| M8000 | RUN monitor |
| M8002 | initial pulse, first scan only |
| M8011–M8014 | 10 ms / 100 ms / 1 s / 1 min clock pulses |
| D8000 | watchdog timer, ms |
| D8001 / D8101 | PLC type and version — both `24100` |
| D8002 / D8102 | memory capacity, 16 = 16K steps |
| D8010–D8012 | scan time current / minimum / maximum, 0.1 ms units |
| D8030–D8032 | analog inputs AI0–AI2 |

GX Works 2 **Online → Remote Operation** controls RUN/STOP using the captured
FX special-relay sequence (`M8035`–`M8037`). This is the only way to change
mode: all ten physical trainer inputs remain available to the user program,
so none is reserved as a RUN/STOP switch. I9 is an ordinary input mapping to
FX device X11.

---

## Supported instructions

- **Contacts:** `LD`, `LDI`, `AND`, `ANI`, `OR`, and `ORI`
- **Coils:** `OUT`, `SET`, `RST`, `PLS`, and `PLF` for supported bit devices,
  including the captured extended `OUT S`, `SET S`, and `RST S` forms
- **Stack and blocks:** `MPS`, `MRD`, `MPP`, `ANB`, and `ORB`
- **Subroutines:** captured Structured Text `CALL P`, subroutine markers, and
  `SRET`
- **Sequence:** `STL`, `RET`, deferred STL state transfer, `END`, and the
  compiled program-body terminator
- **Timers and counters:** `OUT T`, `OUT C`, and Structured Ladder `OUT_C`
  with constant presets; `RST T`, `RST C`, and the captured Structured Ladder
  counter-reset form; compiler-inlined `CTU` execution used by `FSM_Counter`;
  and captured IEC `TON`, `TON_E`, `TOF`, `TOF_E`, `TP`, and `TP_E` forms
  with `TIME` conversion, D-register presets, `Q`, and `ET`
- **Data movement:** `MOV`/`MOVE_E`, 32-bit `DMOV`, `BMOV`, and `FMOV`
- **Comparison:** `CMP`; signed 16-bit `LD`, `AND`, and `OR` forms of `=`, `>`,
  `<`, `<>`, `<=`, and `>=`; plus compatible captured `EQ_E`/`NE_E` forms
- **Arithmetic and word logic:** `ADD`, `SUB`, `MUL`, `DIV`, `INC`, `DEC`,
  `WAND`, `WOR`, `WXOR`, and `NEG`
- **Rotation and shift:** `ROR`, `ROL`, `SFTR`, `SFTL`, `SFWR`, and `SFRD`
- **Data conversion and selection:** `ENCO`, `BON` (including captured
  D28-indexed structured BOOL arrays), and signed-integer `FLT`
- **Range and decode:** `ZRST` bit ranges and captured
  `DECO C0 S9 K4` counter-to-one-hot-state decoding
- **Drum sequencing:** captured `ABSD D300 C0 Y0 K4` absolute-drum tables

These forms have been exercised by the GX Works teaching programs listed in
[`TestedPrg.md`](TestedPrg.md). Support is based on the exact instruction words
observed in downloads; similarly named variants with different operand types
may still require additional decoding.

Not yet implemented includes `MC`/`MCR`, `INV`, `ZCP`, pulse variants of the
applied instructions, and generic IEC function-block execution beyond the
compiler-inlined forms observed in validated downloads. `DMOV` is the only
double-word form decoded so far.

An unimplemented instruction does not fail silently — it is counted and
reported in the diagnostic dump, naming the opcode.

---

## Modbus RTU

A Modbus RTU slave runs on **GPIO0/GPIO1** at 9600 8N1, address 1. It is
independent of the GX Works link, which uses USB.

A real FX2N has no Modbus at all — Mitsubishi added it with the FX3U's `ADPRW`
instruction — so this mapping is **this project's own convention**, chosen so
that each Modbus table holds the FX devices that behave the way that table
does.

| Table | Function codes | Range | Devices |
|---|---|---|---|
| Coils | 01, 05, 15 | 0–255 | Y0–Y255 |
| | | 1000–4071 | M0–M3071 |
| | | 5000–5999 | S0–S999 |
| Discrete inputs | 02 | 0–255 | X0–X255 |
| | | 1000–1255 | T contacts |
| | | 2000–2255 | C contacts |
| Holding registers | 03, 06, 16 | 0–7999 | D0–D7999 |
| | | 8000–8255 | D8000–D8255 |
| Input registers | 04 | 0–255 | T current values |
| | | 1000–1255 | C current values |
| | | 2000–2002 | AI0–AI2 |

Addresses outside these ranges return an *illegal data address* exception
rather than silently reading zero.

---

## Modbus TCP

The same device map is served over Wi-Fi on **port 502**, so a client can use
either transport without knowing which. Only the framing differs — a length
field and a transaction id instead of a checksum and a silent gap — and both
share one request handler.

Provision the trainer once with its network, over the same USB console the
`?` dump uses:

```text
wifi <ssid> <key>
```

The credentials are stored in flash and survive a power cycle. The address is
obtained by DHCP; send `?` to see it, along with the link state and the
connection and request counts. There is no ladder-side network configuration
to get right — provisioning and reading back the address happen in the same
console session.

Reading inputs and writing outputs have been verified against a real client on
the trainer.

Until a trainer is provisioned the radio never starts, so the published
firmware costs nothing to anyone not using the network.

**There is no authentication.** Modbus TCP has none to offer: anything that can
reach port 502 can write any coil or register, which on a trainer means it can
switch outputs. Keep these on a lab network.

---

## Diagnostics

**sts1 (GPIO3)** is the programming-protocol error indicator:

| sts1 | Meaning |
|---|---|
| dark | no protocol error |
| 0.5 s pulse | a frame was rejected |

**Serial dump.** Send a bare `?` over USB CDC and the firmware prints a
diagnostic report: PLC identity, memory capacity, whether a program is loaded,
unknown-opcode count, a program memory hex dump, and the last 12 non-routine
protocol frames.

GX Works and a terminal cannot share the COM port. To read the dump, close
GX Works first — **without unplugging the Pico**, since the trace lives in RAM.
Any terminal works; baud rate is irrelevant over USB CDC.

The dump happens only when asked for. There is no automatic one: GX Works
briefly closes and reopens the COM port between download and Monitor Mode, and
text queued during that gap arrives where the next handshake expects `ACK`,
which stops Monitor Mode from starting.

[`Serial-Monitor-Capture.md`](Serial-Monitor-Capture.md) explains the frame
tracer in full — what it keeps, what it discards, and how to read a captured
frame. Recordings made with it are in [`captures/`](captures/).

**Wi-Fi provisioning.** Credentials are not compiled in. Send one line over the
same USB console:

```text
wifi <ssid> <key>
```

for example `wifi PLCLAB idahostate`. The key must be 8–63 characters, and
neither field may contain spaces. It is stored in flash alongside the user
program and survives a power cycle, so each trainer is provisioned once.

The `?` dump reports the stored SSID and confirms a key is present, but never
prints the key itself — dumps get pasted into bug reports and kept in
`captures/`.

Nothing consumes these credentials yet; the network stack is not built. This
exists so that when it is, no passphrase ever has to live in the firmware
image or in this repository.

---

## Building

Requires the Pico SDK, CMake, Ninja and `arm-none-eabi-gcc`.

```sh
PICO_SDK_PATH=/path/to/pico-sdk cmake -S . -B build -G Ninja -DPICO_BOARD=pico_w
cmake --build build
```

Produces `build/ISU-FX2N.uf2`.

Host-side tests need no hardware:

```sh
./tests/run_tests.sh
```

---

## Source layout

Files are split so that everything testable stays free of SDK dependencies.
Hardware-specific code is confined to as few files as possible.

| File | Responsibility | Portable? |
|---|---|---|
| `board.c/.h` | pin map, I/O polarity, ADC, status LEDs | no |
| `plc_memory.c/.h` | the FX2N device model | **yes** |
| `plc_scan.c/.h` | scan cycle, timers, counters, special relays | no |
| `plc_program.c/.h` | user program storage and the parameter block | **yes** |
| `plc_storage.c/.h` | persisting the program to flash | no |
| `plc_exec.c/.h` | the ladder interpreter | **yes** |
| `fx_addr.c/.h` | protocol address ↔ device mapping | **yes** |
| `fx_protocol.c/.h` | GX Works framing over USB CDC | no |
| `modbus_map.c/.h` | Modbus address ↔ device mapping | **yes** |
| `modbus.c/.h` | Modbus RTU framing over UART0 | no |
| `main.c` | wiring and the scan loop | no |

---

## How it works

A PLC does not run like an ordinary program. It repeats a fixed cycle:

1. read every physical input into the X image
2. execute the user program against that snapshot
3. write the Y image out to the physical outputs

Because step 2 works on a snapshot, an input that changes mid-scan is not seen
until the next cycle. That is what makes ladder logic deterministic, and it is
the single most important idea to take from this source. See `plc_scan.h`.

---

## Known limitations

- **Diagnostics are request-only**, and need GX Works disconnected first — see
  [Diagnostics](#diagnostics) above.
- **Live ladder monitoring refreshes slowly.** GX Works polls the packed
  monitor buffer about once every 2.8 seconds over this serial connection, so
  X, Y, S and M changes animate correctly but are not immediate. The monitor
  list is stored separately and does not overwrite D registers.
- **RUN/STOP is remote-only.** The trainer has no RUN/STOP switch, so a PLC
  left in STOP can only be started from GX Works 2.
- **Configurable latch ranges are not imported yet.** On STOP to RUN, Y, S,
  general M, general D, T0-T245, and C0-C99 are cleared. Inputs, special
  devices, T246-T255, and C100-C255 are retained. This gives `M8002` a clean
  first scan for program initialization while preserving the fixed retentive
  timer and counter ranges.
- **Force** works for X, Y, M, S and M8000-range devices. T and C have not been
  located in the force address space and are ignored rather than guessed at.
- Applied-instruction support is otherwise limited to the ordinary 16-bit
  forms listed above; indexed and pulse variants remain future work, and
  `DMOV` is the only double-word form decoded.

---

## Provenance

The runtime design — scan cycle, device model, instruction semantics — follows
an STM32F103 FX1N emulator, used as reference only; none of its code is copied,
and its own programming protocol is proprietary and unrelated to Mitsubishi's.

The GX Works protocol was **not** available in any published source. Framing
and the device address map came from public documentation of the FX programming
port; everything else — the PLC identity registers, memory-cassette registers,
the extended command set, the parameter block, the instruction encoding and the
force address space — was derived by capturing what GX Works 2 actually sends
and reading it back. Each finding is documented at the point in the source
where it is used, together with what was observed versus inferred.

---

## License

MIT — see `LICENSE`.

Mitsubishi, FX2N, GX Works and GX Developer are trademarks of Mitsubishi
Electric. This project is an independent, unaffiliated reimplementation; the
vendor reference manuals it was developed against are not redistributed here.
