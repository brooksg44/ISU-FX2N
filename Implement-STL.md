# Implementing FX STL Correctly

This document compares Chapter 3, **“STL Programming,”** of *FX Series
Programmable Controllers* with the runtime and records the implementation
requirements discovered during that review.

The `Fix-STL` work implements and tests the critical execution semantics below:
inactive-step isolation, the STL-local bus, deferred source reset during state
transfers, ordinary SET behavior outside STL, and consecutive-state merge.
GX Works ladder and watch monitoring of S devices has also been verified on the
Pico. Sections describing gaps are retained as the original analysis; opcode
coverage, `ZRST`, advanced instructions, and broader hardware validation remain
future compatibility work.

Page references below are the manual's printed Chapter 3 page numbers.

## Required behavior from Chapter 3

An STL block is more than an `LD S` contact:

- Each `STL Snn` starts an isolated step program. Instructions belonging to
  that step are active only while Snn is active (3-2, 3-7).
- The STL instruction creates a new logical bus bar. Contacts in the step are
  evaluated relative to that bus, but are still gated by the step's active
  state (3-2, 3-7).
- `RET` ends an STL region and returns execution to ordinary ladder. STL blocks
  may be embedded between ordinary ladder regions, and more than one STL
  region may exist (3-3 to 3-4).
- A transition normally uses `SET Sdest`. When it succeeds, the destination
  state is activated and the source STL state is reset automatically. A state
  set from ordinary ladder does not cause that ladder source to be reset
  (3-3, 3-5).
- `OUT Sdest` has the same state-transfer behavior, and is conventionally used
  for backward loops, large forward jumps, or transfers to a separate STL
  flow (3-6).
- The source and destination remain active together for the transfer scan. The
  source is absent on the following scan. This one-scan handover is observable
  and is why consecutive steps can conflict on outputs and timers (3-8 to
  3-9).
- Selective branches set one of several destination states. Parallel branches
  may set several destinations from one source. In both cases the source is
  reset after a successful transfer (3-11 to 3-12).
- A multiple-state merge is expressed by consecutive `STL` instructions,
  followed by transition logic. All listed source states and conditions must
  be true; all participating source states are then reset automatically
  (3-13).
- Up to eight branches may leave a branch point. Selective and parallel branch
  forms must not be mixed at the same branch point (3-14). These are primarily
  programming/validation rules; the runtime still needs to execute legal
  compiled forms.
- Ordinary coils may be repeated in separate, mutually exclusive steps.
  Consecutive states overlap for one scan, so the usual dual-coil ordering and
  interlocking caveats still apply (3-7 to 3-8).
- Most normal ladder instructions and applied instructions can occur inside a
  step. Chapter 3 specifically lists the basic instruction restrictions and
  warns against combining STL with `MC`/`MCR`; `FOR`/`NEXT`, subroutine,
  interrupt, `FEND`, and jump placement also have restrictions (3-10).
- Initial flows conventionally use S0-S9, commonly initialized on the M8002
  first-scan pulse. The examples also use `ZRST` to clear a state range before
  starting a sequence (3-4, 3-15 to 3-17).

## Gaps in the current implementation

### 1. An inactive step can execute — critical

`plc_exec_scan()` currently handles `STL Snn` by assigning
`result = plc_get_s(nn)`. A following `LD Xn` then replaces `result` with Xn.
Consequently this legal program can energize Y0 even when S20 is off:

```text
STL S20
LD  X0
OUT Y0
RET
```

This violates the rule that the whole step program is controlled by its state.
The interpreter needs a separate STL execution gate; the state must not be
represented only by the ordinary rung accumulator.

### 2. State transfers never reset their source

`SET Sdest`, the special `MISC_SET_S` form, and `OUT Sdest` currently only
write the destination bit. They do not know which STL state or states are the
transition sources, and no reset is queued. A normal sequence therefore
accumulates active S bits instead of advancing from one step to the next.

Resetting the source immediately would also be wrong: Chapter 3 requires the
source and destination to coexist for the remainder of the transfer scan.

### 3. Multiple-state merge is decoded incorrectly

Chapter 3's multiple merge uses consecutive STL instructions as a logical AND
of source states. The current code overwrites `result` for every `STL`, so only
the final source controls the merge. The runtime must retain the complete
source set and compute an AND gate for the transition.

### 4. STL context is not distinguished from ordinary ladder

The automatic-reset rule applies only when an S coil is driven as a transition
from an STL step. The same `SET Snn` in ordinary ladder must behave as an
ordinary latched SET. The current interpreter has no explicit `in_stl` context
and therefore cannot implement this distinction.

### 5. Inactive-step instruction behavior is not modeled

The implementation must define which instructions are evaluated with false
power and which are skipped when a step is inactive. This matters especially
for applied instructions, edge instructions, timers/counters, and clearing an
`OUT` coil that was driven by a step on the previous scan. A simple global
boolean gate is insufficient unless output and instruction side effects are
specified and tested.

### 6. Important Chapter 3 example instructions are missing

The simple worked example depends on `ZRST Sstart Send`, which is currently
listed as unsupported. Chapter 3 also calls out `PLF`, pulse contacts such as
`LDP`, and the optional `IST` applied instruction. Basic STL should not be
declared complete until `ZRST` and the transition-relevant pulse behavior are
available. `IST` can be a later compatibility milestone because it is an
advanced convenience instruction rather than the core STL mechanism.

### 7. Opcode coverage and S-number decoding are unverified

The current constants are based partly on observed downloads and partly on
inference. `OPCODE_STL` carries only an 8-bit S number, while the advertised
device range is S0-S999. `MISC_SET_S` also masks its following value to eight
bits. Downloads using S256-S999, `SET S`, `OUT S`, consecutive STL merge,
`ZRST`, and branch patterns must be captured from GX Developer/GX Works2 and
round-tripped before relying on the present nibble-extension assumptions.

## Recommended runtime design

Keep ordinary ladder power-flow state separate from STL control state. A small
per-scan execution context is sufficient:

```c
typedef struct {
    bool in_stl;
    bool step_gate;                 /* AND of the current STL source states */
    uint16_t sources[8];            /* states to reset after a transfer */
    uint8_t source_count;
    bool transfer_taken;
    bool reset_at_end[PLC_NUM_S];   /* or a compact bitset */
} stl_context_t;
```

The exact representation can differ, but the execution rules should be:

1. The first `STL Snn` enters STL mode, records Snn as a source, and establishes
   `step_gate = Snn` plus a fresh local bus.
2. A consecutive `STL Smm` before ordinary step instructions extends a merge:
   record Smm and AND its state into `step_gate`.
3. `LD` starts a rung under the local bus: `result = step_gate && contact`.
   `LDI`, branch-stack operations, direct coils, and applied instructions must
   preserve the same gate. A direct `OUT` immediately after `STL` uses
   `step_gate`.
4. When a powered `SET Sdest` or `OUT Sdest` is executed inside STL, set the
   destination now and queue every current source for reset at scan end. Do
   not reset sources immediately.
5. Apply queued source resets only after the entire downloaded program has
   executed for that scan. This preserves the documented one-scan overlap.
   Normal sequential instruction order still determines whether a destination
   block later in memory can execute in that scan or must wait for the next.
6. Do not queue automatic resets for S coils driven outside STL. Explicit
   `RST`/`ZRST` remains immediate according to normal instruction behavior.
7. `RET` clears all STL parsing/execution context and creates a clean ordinary
   ladder bus for instructions that follow it.
8. `END` commits deferred state resets before returning. Falling off program
   memory should use the same finalization path, while also reporting the
   malformed/missing `END` condition.

Do not infer step boundaries merely from `LD`: a step contains multiple rungs.
The boundary is the next `STL`, `RET`, or program terminator. Also keep STL
source tracking separate from the existing ANB/ORB and MPS/MRD/MPP stacks;
they express different structures.

### Output and side-effect policy

Before coding the gate, verify inactive-step behavior on a real FX2N or a
trusted simulator with three probes:

1. Activate a step that drives `OUT Y0`, transfer away, and observe exactly
   when Y0 clears.
2. Place `SET M0`, `PLS M1`, `OUT T0 K10`, and `MOV K1 D0` in an inactive step
   and observe which state changes occur.
3. Drive the same Y/M/T device in two mutually exclusive and then two
   one-scan-overlapping steps to establish instruction-order behavior.

Use those results to implement an explicit policy, not accidental behavior
from accumulator values. Likely categories are: normal `OUT` evaluates false
and clears; SET/RST and applied instructions have no side effect without
power; timers see a non-driven/false condition; edge-history is updated only
as FX behavior requires.

## Decoder and validation work

Create tiny GX projects, download them, and record the raw 16-bit words for:

- `STL`, `SET S`, `OUT S`, and `RST S` at S0, S9, S20, S255, S256, S511,
  S512, S767, S768, and S999;
- a normal transition, backward OUT jump, large forward jump, and transfer to
  a separate flow;
- two- and three-source multiple merges;
- selective and parallel branches with several destinations;
- `RET`, multiple embedded STL regions, `ZRST S21 S25`, `LDP`/`PLF`, and IST.

Store these byte sequences as test fixtures with their GX mnemonic source.
This will settle whether extended S numbers use opcode nibbles, extra words,
or another encoding, and whether the existing `0xF0`, `0xF7`, and
`MISC_SET_S` interpretations are complete.

Add a lightweight validation pass over downloaded code, preferably when a
download is completed rather than every scan. It should at least diagnose:

- an STL region without `RET` before `END`;
- unsupported/truncated multiword instructions;
- invalid S numbers or malformed consecutive-STL merges;
- runtime stack overflow/underflow rather than silently ignoring it;
- unsupported instructions within an STL block.

The manual's authoring restrictions (branch layout, MC/MCR combinations,
FOR/NEXT placement, and so on) can initially be warnings. Legal compiler output
must execute correctly first.

## Test plan

There are currently no unit tests for `plc_exec.c`. Add a host-buildable
interpreter test target with fake program storage and scan timing. At minimum,
cover:

1. **Inactive isolation:** X0 on under inactive S20 cannot energize Y0 or run
   a side-effecting instruction.
2. **Active local bus:** direct coils and multiple LD-based rungs in one active
   step are gated by S20.
3. **Basic transfer:** `STL S20; LD X0; SET S21` lets later-in-program S21
   execute during the transfer scan, preserves S20's effects for that scan,
   and begins the next scan with only S21 active. Instrument the transition or
   assert step outputs because an end-of-scan API cannot expose the transient
   S20+S21 state directly.
4. **Ordinary SET:** ladder `SET S20` does not reset any unrelated source.
5. **OUT transition:** backward, forward, and separate-flow OUT transitions
   perform the same deferred source reset.
6. **Selective branch:** exactly the powered destination is set and the source
   is reset once.
7. **Parallel branch:** several destination S bits can be set before the one
   source is reset.
8. **Multiple merge:** all source states and all contact conditions are
   required; a successful merge resets every source after the scan.
9. **Program order:** a destination after its source can execute in the
   transfer scan; a backward destination already passed by the interpreter
   begins on the next scan. Both cases reset the source at the scan boundary.
10. **RET embedding:** ordinary ladder before and after one or several STL
    regions is not gated by the last state.
11. **Outputs/timers/edge instructions:** transition and inactive-step behavior
    matches the hardware probes above.
12. **Extended S range:** fixtures prove correct operation through S999.
13. **Initialization example:** M8002 plus `ZRST`/`SET` establishes exactly the
    intended initial state on the first RUN scan.

Tests should assert device values both immediately after a scan and after the
following scan; otherwise the required handover behavior is easy to miss.

## Suggested implementation order

1. Capture and lock down opcode fixtures, especially S256-S999 and state-coil
   forms.
2. Add host-side `plc_exec` tests that expose inactive-step execution and the
   missing automatic reset.
3. Introduce explicit STL context, local bus gating, consecutive-STL merge,
   and end-of-scan deferred resets.
4. Define and implement inactive-step output/timer/applied-instruction behavior
   from hardware observations.
5. Implement `ZRST` for S ranges, then the pulse contact/instruction behavior
   needed by common transitions.
6. Add download-time structural validation and clear diagnostics.
7. Run the manual's simple flow (3-16/3-17), selective branch (3-18/3-19), a
   parallel branch, and a multiple merge end to end from GX software.
8. Treat `IST` and broader applied-instruction coverage as a later STL
   compatibility phase.

Core STL support should only be advertised after steps 1-7 pass. Until then,
the README's `Sequence: STL RET END` line should be qualified as decoding-only
or partial support.
