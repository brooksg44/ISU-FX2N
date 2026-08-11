# Instruction Set To Be Implemented

## Objective

Extend the ISU-FX2N firmware so the Structured Ladder teaching programs in
`projects/4LightsSeqLDv2.gxw` can be downloaded, executed, and monitored on the
Pico WH trainer.

The immediate goal is the instruction subset actually used by these teaching
programs. Implementing the entire published FX2N instruction catalog should be
a later compatibility project, after the required examples work end to end.

## Structured Ladder and the Pico

GX Works does not send the graphical Structured Ladder representation to the
PLC. It compiles the project into PLC instruction words and downloads those
words to program memory. Consequently, Structured Ladder can work with this
firmware when both of the following are true:

1. GX Works accepts the project as an FX2N/FX2NC project and connects to the
   emulated FX2N CPU.
2. `plc_exec.c` implements every instruction emitted by the compiler for the
   selected program.

The current UF2 satisfies the basic download, upload, RUN/STOP, STL, and
monitoring paths, but it does not yet implement every instruction needed by
`4LightsSeqLDv2.gxw`. The project may download successfully while still
executing incorrectly or incrementing the unknown-opcode counter.

If the GXW project targets a CPU family that GX Works will not convert to
FX2N/FX2NC, it must first be copied or recreated in an FX2N-compatible project.

## What Can Be Recovered From a GXW File

A `.gxw` file is a Microsoft Compound Document containing XML metadata and
embedded Access-style project databases. It is not a simple source-code file.

The following information can be extracted from the supplied project:

- project, task, POU, label, and function-block names;
- comments and teaching annotations;
- many instruction names;
- some operands and program relationships;
- embedded compiled and project-data streams.

The proprietary database layout does not currently provide a dependable way
to reconstruct every graphical network, wire, operand, and function-block
connection exactly. GX Works PDF exports are therefore the preferred readable
source for program intent.

For each example, export or print the following:

- every program and POU as a PDF;
- global and local label tables;
- program/task execution settings;
- PLC parameters;
- user-defined function and function-block definitions.

PDFs provide instruction semantics and circuit intent. Download captures
provide the exact instruction words GX Works generated. Both are needed.

## Programs Found in `4LightsSeqLDv2.gxw`

The project contains or references examples with names including:

- `FSM_STL`
- `FSM_Counter`
- `FSM_Counter_Decode`
- `FSM_Counter_D`
- `FSM_EQU`
- `FSM_EQU2`
- `FSM_SHL`
- `FSM_SR_NO_SC`
- `FSM_SR_SC`
- `FSM_PN`
- `FSM_DRUM`

These appear to demonstrate several finite-state-machine techniques rather
than one implementation of the four-light sequence.

## Instructions and Features Detected

The GXW container exposes references to at least the following:

- `STL`, `RET`, `SET`, and `RST`
- `ZRST`
- `MOVE_E`
- `EQ_E`
- `CTU`
- `DECO`
- `SFTL`
- rising-edge or one-scan behavior
- structured labels
- function or function-block instances
- `EN` and `ENO` execution flow

Names ending in `_E` generally indicate an enable-aware Structured Ladder
form. It must not be assumed that the PLC receives a unique `_E` opcode: GX
Works may compile it into ordinary FX instructions. The downloaded bytecode
must decide that question.

## Current Interpreter Coverage

The current runtime supports the following relevant core operations:

- `LD`, `LDI`, `AND`, `ANI`, `OR`, and `ORI`
- `OUT`, `SET`, and `RST` for supported bit devices
- `MPS`, `MRD`, `MPP`, `ANB`, and `ORB`
- `PLS`, whose rising edge is measured per program step rather than from the
  destination device (a device-derived edge made a held-true rung toggle the
  coil every scan instead of pulsing once)
- `PLF`, sharing that edge memory, on a captured `0x0009` prefix
- `STL`, `RET`, and deferred STL state transfers
- timer and counter coils with constant presets
- a limited `MOV` form using constants and D registers
- `DMOV`, its 32-bit form, at `0x0029`
- `END`

### The `PLF` encoding

`PLF` shares the `PLS` device word and differs only in its prefix. Captured by
downloading a single rung and reading program memory back:

```text
2401  LD X1
0009  PLF prefix
8801  PLF M1
```

That confirms the ordering the surrounding misc operands suggest: `0x05`
OUT S, `0x06` SET S, `0x07` RST S, `0x08` PLS, `0x09` PLF.

Note that `projects/TestCommsPLF.gxw` cannot be used to re-derive this
offline — it stores GX Works' own ladder format inside an Access database,
not the downloaded bytecode. The capture above came from reading program
memory off a running PLC, which is the only way to see what is actually sent.

A pulse prefix is honoured only when the following word is a pulse coil (op
nibble `8`, excluding the `0x80` constant marker). Anything else is counted
and reported as an unknown opcode rather than left pending, so a misread
prefix cannot attach itself to a genuine pulse coil further down the program.

### The `DMOV` encoding

Applied instructions sit at `0x10 + 2 * FNC`, so `MOV` (FNC 12) is `0x0028`
and its 32-bit form takes the odd slot at `0x0029`. `DIV`/`DDIV` repeat the
pattern at `0x003E`/`0x003F`, which is what makes the odd slot a rule rather
than a coincidence.

`0x0029` was first captured as the 32-bit `TIME` move inside the IEC timer
function blocks, but nothing in the encoding is `TIME`-specific: each 32-bit
operand is two typed operand pairs, low word first.

A Structured Ladder test with destinations D2000, D2002, and D1000 establishes
both the operand layout and the extended-D address bank. Its first rung is:

```text
2400              LD X0
0029              DMOV
80E7 8003         K999, low word
8000 8000         K999, high word
86D0 8807         D2000
8000 8000         padding
001C              FEND
```

Two details only the capture settles, and they failed in different ways:

**The destination's high type is `0x88`, not `0x80`.** A 16-bit destination
carries `OPERAND_CONST` there — `INC D28` is `8638 8000`. A 32-bit one carries
`OPERAND_POINTER`. `read_bit_operand()` already admits the same marker on bit
operands in structured POUs, so this is the established pattern rather than a
special case. Rejecting it made a downloaded `DMOV` do nothing at all, with the
destination staying at zero and `0x0029` reported as the last unknown opcode.

**The `0x88` type selects a D1000-based bank; the value is still a byte
offset.** The complete capture contains these destinations:

```text
86D0 8807         D2000 = D(1000 + 0x07D0 / 2)
86D4 8807         D2002 = D(1000 + 0x07D4 / 2)
8600 8800         D1000 = D(1000 + 0x0000 / 2)
```

Treating the encoded value as a direct register index happened to decode the
first example as D2000, but sent the other two writes to D2004 and D0. The
three-rung capture removes that coincidence and establishes the banked rule.

**The second half of the destination really is constant zero padding.** The
high register is never named, in either the IEC timer capture or this one. An
earlier revision also accepted `D(dst + 1)` there on the assumption that a
hand-written `DMOV` would name it; the capture disproves that, and the
speculation has been removed.

`DMOV` was originally wanted for the FX2NC-ENET-ADP parameter block, which
writes its IP settings to D1000–D1008 that way. That approach was dropped:
the Modbus TCP slave takes its address by DHCP, and students read it from the
`?` dump after provisioning the trainer. `DMOV` stays because it is an
ordinary FX instruction that teaching programs use in their own right.

The following are known or likely gaps for the supplied project:

- `ZRST` range reset
- `DECO` decode operation
- `SFTL` shift-left operation
- equality and other comparison forms emitted for `EQ_E`
- the exact compiled representation of `MOVE_E`
- IEC `CTU` function-block behavior and retained instance state
- `EN`/`ENO` propagation where it is not compiled into ordinary contacts
- structured POU and function-block calls, parameters, and instance memory
- array or indexed-label addressing used by the state-machine examples
- any additional edge, arithmetic, comparison, shift, or indirect operations
  revealed by the compiled downloads

The `FSM_PN` capture established that GX Works automatic BOOL variables in its
reported `M1425-M1535` VAR range use the normal M-device nibble extension.
For example, `0xCDFE` is `OUT M1534`, while `0x5DFE` and `0x6DFE` are contacts
of the same bit. M nibbles `8-D` and the Structured Ladder extended `OUT S`
form (`0x0005` followed by `0x80nn`) are now implemented.
The capture also confirms `0x001C` as the compiled program-body terminator
immediately before the erased separator and final `END`; execution stops there
without recording an unknown opcode.

The `FSM_SR_NO_SC` capture established the Structured Ladder extended `RST S`
form (`0x0007` followed by `0x80nn`). It also confirmed `ZRST Y0 Y3` as
`0x0060 0x8400 0x8005 0x8403 0x8005`. Both forms are implemented; the former
prevents set/reset state machines from accumulating every visited state.

The `FSM_EQU` capture established `EQ_E-2` as bytes `D0 01`, fetched by the
little-endian interpreter as word `0x01D0`, followed by two typed 16-bit
operands. Captured operands include D registers (`0x86nn 0x86nn`) and
integer constants (`0x82nn 0x80nn`). The comparison starts its Structured
Ladder network with the equality result. The existing `0x0028` MOVE form was
also confirmed for both constant-to-D and D-to-D moves.

The FSM_EQU monitor list established that ordinary D-register protocol byte
addresses begin at `0x4000`, not `0x1000`: D100 and D101 are listed as
`0x40C8` and `0x40CA`. The monitor/device mapping now uses that captured base.

The `FSM_SHL` capture established `SFTL` as `0x0056` followed by typed source
bit, destination bit, total-length, and shift-count operands. The observed
form is `SFTL M1523 S10 K9 K1`; it shifts S10-S18 toward the higher state
numbers and loads M1523 into S10. Program capacity is now 16000 steps, backed
by a 32000-byte instruction image and eight flash sectors of persistent
storage.

The `FSM_DRUM` capture established `ABSD` as `0x008C` followed by its D-table,
counter, first-output, and output-count operands. The observed
`ABSD D300 C0 Y0 K4` uses D300-D307 as four ON/OFF threshold pairs. The same
capture identifies the Structured Ladder counter reset as `0x000C` plus a
counter word, and the `OUT_C` enable wrapper as `0x01CA` plus its X input,
followed by the normal counter coil and constant preset.

The `FSM_Counter_Decode` capture established `DECO` as `0x0062` followed by
typed source, first-destination, and source-width operands. The observed
`DECO C0 S9 K4` reads the low four bits of C0 and selects one of 16 consecutive
state bits S9-S24 while clearing the other decoded destinations.

## Which UF2 To Use for DMS Captures

Use the current `build/ISU-FX2N.uf2` for DMS captures of GX Works downloads.

The purpose of this capture is to observe what GX Works sends, not to prove
that the downloaded program already executes correctly. The current firmware
supports program download and is the correct target even when the interpreter
later reports unknown opcodes.

The old UF2 is not required for instruction discovery and should not be used
to judge program behavior. Its known inability to execute these examples does
not prevent it from being a protocol reference, but the current UF2 already
implements the communication paths needed for this work.

The GX Works built-in simulator is used separately to determine the intended
runtime behavior. Simulator execution normally does not traverse the Pico's
serial connection, so Device Monitor Studio cannot capture PLC download
frames from a simulator-only run.

Use the two sources as follows:

| Source | What it establishes |
|---|---|
| GX Works PDF export | Network layout, operands, labels, and intended logic |
| GX Works simulator | Correct state changes, scan behavior, and edge cases |
| DMS capture while downloading to current UF2 | Exact FX instruction words and protocol framing |
| Current UF2 serial diagnostic dump | Unknown opcode, program bytes, and emulator runtime state |

## Capture and Implementation Procedure

Process one small program or one new instruction at a time.

1. Configure or recreate the example as an FX2N/FX2NC project.
2. Disable unrelated POUs so the downloaded program contains the smallest
   useful example.
3. Export the ladder, labels, and parameters to PDF.
4. Run the example in the GX Works simulator and record its expected behavior.
5. Flash the current `ISU-FX2N.uf2` onto the Pico.
6. Start a complete DMS capture before clicking **Write to PLC**.
7. Download the program to the Pico, then stop the capture after GX Works
   reports completion.
8. Close GX Works, open the serial terminal, and collect the firmware's `?`
   diagnostic dump, including the program-memory bytes and unknown opcode.
9. Store the mnemonic example, expected simulator results, downloaded words,
   and DMS frame together as a test fixture.
10. Implement the decoder and execution semantics in a host-testable module.
11. Add tests for true, false, boundary, repeated-scan, and interaction cases.
12. Rebuild the UF2 and compare the Pico's devices and outputs with the
    simulator.

The most useful DMS records are the `E11` program-memory writes at addresses
`0x8000` and above. Capturing the whole exchange is still preferable because
parameter data and program boundaries may explain how GX Works interprets the
compiled code.

### `FSM_STL` capture (2026-08-08)

The supplied Write-to-PLC capture proves that `FSM_STL` compiles and downloads
to the emulated FX2N. GX Works writes the 92-byte parameter header at `0x8000`,
then writes compiled program data beginning at `0x805C` in three 64-byte
`E11` blocks (`0x805C`, `0x809C`, and `0x80DC`). It fills the remaining program
area with `0xFF`, announces the 15-word compiled range with `E41 805C 0F00`,
and sends `E11 805E 02 0FB4` as a transfer check/finalization transaction.
That last value must be acknowledged but not stored as ladder code: doing so
overwrites the downloaded `0x0006` SET-S instruction, preventing the opening
`LD M8002` rung from setting `S10`.

The same capture identifies why GX Works could not enter Monitor Mode. After
the download it closes the serial port and reopens it about 2.94 seconds later.
The firmware's former automatic diagnostic dump fired after two idle seconds
and queued human-readable text on the protocol port. The reconnecting GX Works
sent `ENQ` (`0x05`) but read ASCII `0x32` (`'2'`) rather than `ACK` (`0x06`).
Automatic idle dumps are therefore disabled; disconnect GX Works, open a
terminal, and send a bare `?` to request diagnostics explicitly.

## Recommended Implementation Order

### Phase 1: Baseline and discovery

1. Confirm the GXW project's configured CPU and whether GX Works can convert
   it to FX2N/FX2NC.
2. Export all POUs and label tables to PDF.
3. Capture a minimal program using only already-supported contacts and coils.
4. Establish a repeatable method for converting each `E11` payload into
   little-endian 16-bit program words.
5. Add a fixture format that records mnemonic source, raw words, and expected
   device results.

### Phase 2: Common sequence operations

Implement these first because they are directly useful to several examples:

1. `ZRST` for M, S, T, C, and D ranges supported by the FX2N form.
2. Comparison contacts/blocks required by `EQ_E`, beginning with equality for
   16-bit integers and expanding according to captured operand types.
3. `DECO` with correct source width, destination device family, and number of
   result bits.
4. `SFTL` with correct rising-edge behavior, shift count, reset behavior, and
   destination range handling.
5. Any additional `MOV` forms emitted by `MOVE_E`, including device-to-device
   moves and indexed operands.

### Phase 3: Structured function blocks

1. Determine whether `CTU` is inlined into basic FX instructions or emitted as
   a callable block.
2. If it remains a runtime construct, implement function-block instance
   storage, `CU`, `RESET`, `PV`, `Q`, and `CV` behavior.
3. Determine and implement `EN`/`ENO` semantics only where the compiler leaves
   them in downloaded code.
4. Add POU/function calling, parameter passing, and retained local storage if
   the captured bytecode requires them.

### Phase 4: Remaining teaching examples

Work through the examples individually:

1. `FSM_STL`
2. `FSM_Counter`
3. `FSM_Counter_Decode`
4. `FSM_Counter_D`
5. `FSM_EQU` and `FSM_EQU2`
6. `FSM_SHL`
7. `FSM_SR_NO_SC` and `FSM_SR_SC`
8. `FSM_PN`
9. `FSM_DRUM`

For each example, completion means:

- download succeeds without rejected protocol frames;
- the unknown-opcode count remains zero;
- Pico device values match the GX Works simulator across multiple scans;
- physical trainer inputs and outputs behave correctly;
- ladder and watch-window monitoring display the relevant devices correctly;
- STOP forces physical outputs off and RUN restarts with documented state.

## Testing Requirements

Every newly implemented instruction should have host-side tests covering:

- false and true rung power;
- lowest and highest valid device numbers;
- malformed or truncated operands;
- execution over consecutive scans;
- interaction with STL inactive-step gating;
- pulse/rising-edge behavior where applicable;
- overlapping source and destination ranges;
- constants, direct devices, and any supported indexed operands;
- behavior in STOP and after returning to RUN where relevant.

Do not infer an opcode solely from its numerical pattern. Lock it down with a
minimal GX Works download capture. Likewise, do not infer execution semantics
solely from the manual when the GX Works simulator can provide an observable
reference result.

## Definition of Success

The required instruction subset is complete when every selected teaching POU
can be compiled for the emulated CPU, downloaded to the current UF2, executed
without unknown instructions, monitored in GX Works, and shown to produce the
same sequence as the GX Works simulator on the physical trainer.
