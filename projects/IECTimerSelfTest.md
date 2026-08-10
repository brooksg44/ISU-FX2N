# IEC timer self-test

Source: `IECTimerSelfTest.st`

Create an FX2N(C) Structured Project and add one ST program POU. Add these
POU-local labels with Class `VAR`:

| Label | Data type |
|---|---|
| `TON_1` | `TON_E` |
| `TOF_1` | `TOF_E` |
| `TP_1` | `TP_E` |

Paste the ST source into the POU and add only this POU to the scan task.

- Hold X0 ON for two seconds: Y0 turns ON.
- Turn X1 ON and then OFF: Y1 remains ON for two seconds after X1 turns OFF.
- Apply a rising edge to X2: Y2 turns ON for two seconds.

Monitor each instance's `IN`, `Q`, `ET`, and `PT` members. Capture **Write to
PLC** with DMS so the compiler-emitted FX2N bytecode and instance-memory layout
can be added to the trainer firmware.
