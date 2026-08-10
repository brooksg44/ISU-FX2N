# Validated GX Works Programs

This file tracks GX Works programs that have been tested end to end against
the ISU-FX2N firmware. Add a program only after it compiles, downloads, runs,
and can be monitored on the Pico trainer.

## Validation checklist

- GX Works project targets `FX2N/FX2NC`.
- Program compiles without errors.
- Write to PLC completes without rejected protocol frames.
- STOP to RUN clears the expected non-retentive devices.
- `M8002` first-scan initialization works.
- Program executes without unknown opcodes that affect its behavior.
- GX Works device and ladder monitoring works.
- Physical outputs match the intended sequence.

## Validated programs

### `FSM_STL`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Initialization:** `M8002` sets initial state `S10` on STOP to RUN.
- **State behavior:** Stale `S` states and `Y` outputs clear on STOP to RUN.
- **Communication:** Program download and post-download Monitor Mode reconnect
  succeed.
- **Monitoring:** GX Works ladder and device monitoring validated.
- **Physical behavior:** Four-light STL sequence validated on the Pico trainer.

GX Works reports compile/check warnings for grid-only networks and duplicate
coil usage in the teaching project. These warnings do not prevent `FSM_STL`
from compiling, downloading, executing, or being monitored.

### `FSM_PN`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Automatic variables:** GX Works BOOL variables in the `M1425-M1535` VAR
  range execute correctly using the captured M-device nibble extension.
- **State behavior:** Structured Ladder extended `OUT S` state equations
  execute correctly.
- **Communication:** Program download and monitoring succeed.
- **Physical behavior:** Four-light previous/next state sequence validated on
  the Pico trainer.

### `FSM_SR_NO_SC`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **State behavior:** Extended `SET S` and `RST S` transitions maintain one
  active state instead of accumulating previously visited states.
- **Initialization:** Captured `ZRST Y0 Y3` clears the output range correctly.
- **Monitoring:** ISU-FX2N state and output values match the GX Works
  simulator.
- **Physical behavior:** Four-light set/reset sequence without scan control
  validated on the Pico trainer.

### `FSM_SR_SC`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Scan control:** Automatic relay `M1535` allows only one state transition
  per scan so action logic executes for each intermediate state.
- **Automatic variables:** Transition relays in the `M1425-M1433` range are
  decoded and executed correctly.
- **Monitoring:** ISU-FX2N state and output values match the GX Works
  simulator.
- **Physical behavior:** Four-light set/reset sequence with scan control
  validated on the Pico trainer.

### `FSM_EQU`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Data operations:** Structured Ladder `MOVE_E` correctly moves constants
  and D-register values.
- **Comparison:** Captured `EQ_E-2` equality comparisons execute without
  unknown opcodes and drive the expected state transitions.
- **Monitoring:** GX Works correctly displays D100/D101 through the captured
  ordinary D-register protocol base at `0x4000`.
- **Physical behavior:** X0 turns on PL1 through PL4 in order; X1 turns them
  off in reverse order on the Pico trainer.

### `FSM_EQU2`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Data operations:** Structured Ladder `MOVE_E` and `EQ_E-2` execute
  correctly.
- **Monitoring:** GX Works data, transition, and output monitoring validated.
- **Physical behavior:** X0 turns on PL1 through PL4 in order; X1 turns them
  off in reverse order on the Pico trainer.

### `FSM_SHL`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Shift operation:** Captured `SFTL M1523 S10 K9 K1` correctly shifts the
  S10-S18 state range and loads the new state from M1523.
- **Capacity:** Validated with the emulator configured for GX Works 2's
  16000-step maximum.
- **Monitoring:** GX Works state and output monitoring validated.
- **Physical behavior:** X0 turns on PL1 through PL4 in order; X1 turns them
  off in reverse order on the Pico trainer.

### `FSM_DRUM`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Initialization:** Structured Text initialization correctly loads the
  D300-D307 absolute-drum threshold table.
- **Drum operation:** Captured `ABSD D300 C0 Y0 K4`, counter reset, and
  `OUT_C C0 K9` forms execute correctly.
- **Monitoring:** Counter, table, and output values match the GX Works 2
  simulator.
- **Physical behavior:** The absolute-drum light sequence differs from the
  other FSM examples by design and matches the GX Works 2 simulator on the
  Pico trainer.

### `FSM_Counter_Decode`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-08
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Decode operation:** Captured `DECO C0 S9 K4` correctly converts the C0
  value into a one-hot state across S9-S24.
- **Counter behavior:** The captured counter coil and reset forms advance and
  recycle the state count correctly.
- **Monitoring:** GX Works counter, state, and output monitoring validated.
- **Physical behavior:** The four-light sequence matches the GX Works 2
  simulator on the Pico trainer.

### `FSM_Counter`

- **Project:** `projects/4LightsSeqLDv2.gxw`
- **Validated:** 2026-08-10
- **Status:** Working
- **CPU target:** `FX2N/FX2NC`
- **Structured execution:** Captured `CALL P`/`SRET` subroutines and the
  compiler-inlined CTU sequence execute correctly.
- **Array operation:** `A4` structured BOOL-array operands use the compiler's
  implicit D28 index, matching GX Works 2 for `Get_Step` and `SR_Step`.
- **Monitoring:** Step index, counter value, transition arrays, and output
  values match the GX Works 2 simulator.
- **Physical behavior:** X0/X1 advance the four-light sequence correctly on
  the Pico trainer.

## Programs awaiting validation

- `FSM_Counter_D`
