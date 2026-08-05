# ISU-FX2N — To Do

Outstanding work, in the order it was prioritised. Each item records what is
already known so the next session can start from evidence rather than
rediscovering it.

---

## 1. Modbus master as well as slave

Currently `modbus.c` implements a **slave only** (address 1, 9600 8N1, on
GPIO0/GPIO1). Add master capability so two trainers can talk to each other.

- Needs a way for the ladder program to initiate a transaction. On an FX3U this
  is the `ADPRW` instruction; we have no applied-instruction mechanism for it
  yet, so decide between an `ADPRW`-style instruction and a special-register
  interface (write request into D registers, poll a completion flag).
- Master and slave cannot both own the UART at once — decide whether the role
  is a build-time option, a parameter, or switched at runtime.
- **The user has example projects where two trainers communicated**; use those
  to fix the register conventions rather than inventing them.
- The slave side is untested against real hardware. Verify with `mbpoll` or
  Modbus Poll and a USB-RS485/TTL adapter before building master on top:
  reading coil 0 should return Y0.

## 2. Online monitoring

Neither the ladder code window nor the watch window updates live. This is the
largest remaining gap and the most valuable for teaching.

What is already known:

- GX Works does **not** read the X/Y device areas during monitoring. It uses a
  watch-list protocol:
  1. writes a list of devices to watch at `0x1400` (`E1 0 1400 <n>` frames)
  2. expects the PLC to sample them each scan into a result buffer
  3. reads packed results from `0x1790` (`E0 0 1790 <n>`)
- The watch list decodes cleanly as 16-bit little-endian device addresses in
  the same flat space the force commands use — see `fx_addr.h`. Verified
  entries include Y0–Y4, X0–X7, M300 and the M8061/M8064–M8066 error flags.
- **The unknown is the result packing.** Read lengths do not line up with a
  simple one-bit or one-byte per device scheme (13 devices produced an 11-byte
  read). This needs the same capture-and-iterate approach that solved the
  download.
- Related bug: the watch list is currently stored as ordinary D registers, so
  **Monitor Mode overwrites D512–D525**. The watch area must get its own
  storage that is not part of the D register file.
- Also unexplained: writing `1` to a coil in the watch window turns it on, but
  writing `0` does not turn it off. Force ON lands, force OFF does not — likely
  a different command or address for the off case.

## 3. Remote RUN/STOP, with the input switch optional

Remote STOP already works (the `B` command). Remote RUN has never been
observed, so a download stops the PLC and it can only be restarted from the
trainer.

- Capture what GX Works sends for **Online → Remote Operation → RUN**.
- Suspect it rides on the force mechanism: the unexplained force address
  `0x760E` = 3702 decodes to **M8118** in the special-relay block, and remote
  run/stop on an FX conventionally forces `M8035`/`M8036`/`M8037`.
- **Make the hardware switch optional and configurable.** The I9 input (X11 in
  octal) toggling RUN/STOP is wanted, but as a choice rather than the only
  mechanism. Consider a parameter or build option selecting: input switch only,
  remote only, or both. GPIO4/GPIO5 are reserved on the trainer and would suit
  a dedicated switch better than consuming an input.

## 4. Instruction coverage against real sample projects

Review the user's other Mitsubishi projects and implement whatever they use.

- Known missing: `PLF`, `MC`/`MCR`, `INV`, comparison contacts (`LD=`, `LD>`,
  `AND=`, `OR<` …), and applied instructions beyond `MOV` — `CMP`, `ZCP`,
  `ADD`, `SUB`, `MUL`, `DIV`, `INC`, `DEC`, `ZRST`, shifts, and the drum
  sequencers (`ABSD`/`INCD`) used by `4LightsStructLAD_DRUM`.
- The interpreter reports unknown opcodes in the diagnostic dump, so the
  fastest route is: download a project, read the reported opcode, add it.
- The encoding is regular — `opcode = (operation << 4) | device_type` — so new
  instructions usually only need a table entry. See `plc_exec.h`.
- Operand limits worth confirming: devices above 255 use the low nibble as an
  extension (`M300` → nibble 9). Only `+256` has been observed; `+512`/`+768`
  are assumed.

## 5. Verify Simple Project and Structured Project both work

The user will test this.

- Expectation: both should work, because GX Works compiles either down to the
  same FX instruction list before download — labels, comments and function
  blocks stay in the `.gxw` on the PC and never reach the PLC.
- The real risk is **instruction coverage**, not project type. Structured
  projects emit a wider set (indexed addressing, more applied instructions,
  subroutine calls), so item 4 gates this.
- Test set: `4LightsSimpleLAD_STL`, `4LightsStructLAD_PN`, `_EQU`, `_DRUM`,
  `_SR_SC`, `_STL_SR`. Each needs **Project → Change PLC Type → FX2N** first,
  as they are saved as FX1N.

---

## Smaller items

- Modbus slave has never been exercised against a real master (see item 1).
- Force works for X, Y, M and M8000-range devices. S, T and C have not been
  located in the force address space and are currently ignored rather than
  guessed at.
- `PICOTOOL_FETCH_FROM_GIT_PATH` is unset, so a clean build re-downloads
  picotool. Point it at a shared location so students do not each wait on it.
- STL does not implement the automatic reset of the previous state on
  transition. Correct for the sequential-step programs tested so far, but not
  fully faithful.
