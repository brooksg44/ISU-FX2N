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
- `PLS` and `PLF` pulse behavior currently implemented by the decoder
- `STL`, `RET`, and deferred STL state transfers
- timer and counter coils with constant presets
- a limited `MOV` form using constants and D registers
- `END`

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
