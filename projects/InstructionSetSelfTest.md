# GX Works2 instruction-set self-test

Source: `InstructionSetSelfTest.st`

## Import and run

1. Create a **Structured Project** for **FXCPU / FX2N(C)** in GX Works2.
2. Add a program POU whose language is **ST**.
3. Paste the contents of `InstructionSetSelfTest.st` into the POU.
4. Add these POU-local labels (Class `VAR`):

   | Label | Data type |
   |---|---|
   | `TON_1` | `TON_E` |
   | `TOF_1` | `TOF_E` |
   | `TP_1` | `TP_E` |

5. Add that POU to the scan task, then run **Rebuild All**.
6. Write the parameters and program to the ISU-FX2N.
7. Hold trainer inputs **X0 and X1 ON**, then use GX Works2 Remote Operation
   to enter RUN. They test `SFTR` and `SFTL`; X2-X4 exercise the timer tests
   separately after RUN begins.
8. Monitor `Y0-Y6`, `M190-M205`, and `D0-D71`.

`Y6` means every tested group passed. The group indicators are:

| Output | Relay | Group |
|---|---|---|
| Y0 | M200 | CMP, MOV, BMOV, FMOV |
| Y1 | M201 | Arithmetic and word logic |
| Y2 | M202 | ROR and ROL |
| Y3 | M203 | SFWR and SFRD FIFO |
| Y4 | M204 | ENCO and FLT |
| Y5 | M205 | All six inline comparisons |
| Y6 | — | Overall pass |

The arithmetic calls use the FX structured-project function names `ADD_E`,
`SUB_E`, `MUL_E`, and `DIV_E`. Plain `ADD`, `SUB`, `MUL`, and `DIV` are not
callable POU names in GX Works2 ST and produce C1028/C8011/C8040 errors.
`DIV_E` returns only the quotient: for `7 / 3`, D24 is 2 and D25 remains 0.
The raw ladder `DIV` instruction has a separate remainder result, but that
result is not exposed by the structured `DIV_E` function.

`ENCO` and `FLT` read their test values from D61 and D69. GX Works2's FX2N
compiler rejects immediate constants in these source positions with F0161,
so the program initializes those registers with `MOV` first.

The shift results are deliberately monitored separately because they depend on
physical inputs: with X0 and X1 held before RUN, `M103` and `M110` should be ON.

The IEC timers are also monitored separately from Y6:

- `TON_E`: hold X2 for two seconds; M190 then turns ON.
- `TOF_E`: turn X3 ON and then OFF; M191 remains ON for two seconds after OFF.
- `TP_E`: apply a rising edge to X4; M192 turns ON for a two-second pulse.

Monitor `TON_1.ET`, `TOF_1.ET`, and `TP_1.ET` in GX Works2 to verify elapsed
time. These timer calls execute every RUN scan; unlike the arithmetic tests,
they are not enabled by the one-scan M8002 pulse.

The instructions are enabled by `M8002`, so the destructive or cumulative
operations execute only on the first RUN scan. To repeat the test, switch to
STOP, hold X0/X1, and switch back to RUN.

If GX Works2 reports an argument-order error for an instruction, insert that
instruction from **View > Element Selection > Instruction**. GX Works2 then
shows the exact input/output pin order for the installed software revision.
