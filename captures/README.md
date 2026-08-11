# Protocol captures

The GX Works 2 programming protocol is not published. The device address map
in `fx_addr.h`, the parameter block in `plc_program.c`, the monitor watch list
in `fx_monitor.c` and the instruction encoding in `plc_exec.c` were all
recovered by recording what GX Works 2 actually sends and reading it back.

These are those recordings — Device Monitoring Studio captures of a real
GX Works 2 session against this firmware, all from 6 August 2026. They are
kept so the findings documented throughout the source can be checked against
the evidence rather than taken on trust.

Most are UTF-16 text exports with a timestamp, direction and hex/ASCII dump
per frame. `DMS-FXG.dmslog8` is Device Monitoring Studio's own binary format
and needs that tool to open.

| File | Recorded | What it holds |
|---|---|---|
| `ConnectionPacket.txt` | 13:40 | GX Works 2 opening a connection, from the PnP event onwards |
| `ReadFromPLC.txt` | 13:40 | An **Online → Read from PLC**. Contains the real 92-byte parameter block returned by `E01 8000 5C` |
| `JustWriteToPLC.txt` | 13:50 | An **Online → Write to PLC** download, including the `E41`/`E11` program transfer |
| `ISUTrainerConnectionPacket.txt` | 14:02 | Connection sequence recorded against the ISU trainer |
| `MonitoringJustPRG.txt` | 14:58 | GX Works 2 monitoring this firmware — carries both the watch lists it sends and the lengths it then reads |
| `MonitoringJustWatch.txt` | 15:02 | The same for the watch window |
| `DMSConnectionTest.txt` | — | A short plain-text frame log, readable without any tooling |
| `DMS-FXG.dmslog8` | — | Native DMS binary log, from the abandoned FX3G/FX3U identity experiments |

The two `Monitoring*` captures are the ones the watch-list protocol in
`src/fx_monitor.c` was solved from: it was decoded by transcription rather
than by iterating against the hardware, because these carry both halves of
the exchange.

To make a capture of your own, see the frame tracer described under
**Diagnostics** in the top-level [`README.md`](../README.md) — close GX Works
without unplugging the Pico, then send a bare `?`.
