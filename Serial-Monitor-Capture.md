# How the protocol capture works

This project talks to GX Works 2 over a protocol Mitsubishi does not publish.
Nearly every address in `fx_addr.h`, the parameter block in `plc_program.c`,
and the instruction encoding in `plc_exec.c` was recovered by **recording what
GX Works actually sent** and reading it back. This file explains the machinery
that does the recording, so you can use it to solve the next unknown yourself.

Recordings made this way are kept in [`captures/`](captures/).

---

## Why it lives in RAM

The obvious way to debug a serial protocol is to print each frame as it
arrives. That is impossible here: **USB CDC carries the protocol itself**.
A `printf` mid-session would inject bytes into the same stream GX Works is
reading and corrupt the frame in flight. There is also no second serial port
to spare — GPIO0/GPIO1 are the Modbus port.

So the tracer does the opposite. It captures frames into a RAM ring buffer
while the link is live and stays completely silent, then prints the whole
window **after** GX Works stops talking.

This is why the instructions always say: close GX Works, but **do not unplug
the Pico**. The capture lives in RAM. Power-cycling erases the evidence.

---

## Step 1 — reassembling frames from a byte stream

USB delivers bytes, not messages. `fx_protocol_task()` runs once per scan and
feeds each received byte through a small state machine:

```c
if (b == STX) { in_frame = true; rx_len = 0; }   /* 0x02 starts a frame */
if (!in_frame) continue;                          /* ignore stray bytes   */

if (rx_len >= FX_BUF_SIZE) {                      /* overlong: give up,   */
    in_frame = false; rx_len = 0; continue;       /* resync on next STX   */
}
rx[rx_len++] = b;

if (rx_len >= 4 && rx[rx_len - 3] == ETX) {       /* ETX + 2 sum chars    */
    uint32_t bad_before = stat_frames_bad;
    handle_frame();
    trace_record(rx, rx_len, stat_frames_bad == bad_before);
    in_frame = false; rx_len = 0;
}
```

Three things worth noticing:

- **Frames end two characters after ETX**, not at ETX — those two are the
  sum-check. That is why the end test looks back three bytes.
- **Resynchronising on STX** means a garbled frame costs one frame, not the
  session. There is no length field to trust, so this is the only recovery
  available.
- **The frame is handled first, traced second.** `handle_frame()` may increment
  the rejection counter, and comparing that counter before and after is how the
  tracer learns whether the frame parsed — without `handle_frame()` having to
  report anything.

Single bytes outside a frame (`ENQ`, 0x05 — the host's "are you there?" poll)
are recorded separately, which is why lone `05 |.|` entries appear in dumps.

---

## Step 2 — deciding what is worth keeping

The ring holds the last **12** frames, each truncated to its first **144**
bytes (`TRACE_FRAMES` and `TRACE_BYTES` in `src/fx_protocol.c`; entries that
were cut are marked `(truncated)` in the dump). Monitor Mode polls the same
addresses forever, so recording everything would fill the window with polling
and scroll away the one-off frame you are hunting. `trace_record()` therefore
drops three exchanges — but **only when they succeeded**:

| Dropped | What it is |
|---|---|
| `'0' ....` | identity polling of D8001/D8101 |
| `'E' "00" "0Exx"` | extended read of the D8000 block |
| `'E' "10" "14xx"` | Monitor Mode writing its watch list |

Everything else is kept: writes, forces, extended reads of unfamiliar
addresses, and anything unrecognised. **Rejected frames are always kept,
whatever they are** — an over-eager filter hides the exact frame being hunted.

The ring keeps the **tail**, not the head. A download runs far longer than the
buffer, and the frame that defeats us is the one where GX Works gives up — the
last one, not the first.

---

## Step 3 — the dump

Send a bare `?` and the firmware prints a report:

- PLC identity — D8001 / D8101, the registers that decide the model name
- memory capacity — D8002 / D8003 / D8102
- whether a program is loaded, and the unknown-opcode count with the last
  offending opcode
- a hex dump of program memory from 0x8000
- the frame ring: index, `ok` or `NAK`, raw hex, and an ASCII column

Any terminal works, on any baud rate — the rate is meaningless over USB CDC.
GX Works and a terminal cannot share the COM port, so disconnect GX Works
first, without unplugging the Pico.

**There is deliberately no automatic dump.** `fx_protocol_idle_dump()` is an
empty function, and the comment in it records why: GX Works closes and reopens
the COM port between download and Monitor Mode — a capture of `FSM_STL` showed
a 2.94 s gap — so an idle dump on a two-second timer queued diagnostic text in
the CDC transmit buffer. On reconnect GX Works sent `ENQ` and got that text
instead of `ACK`, and Monitor Mode never started. Protocol output must only be
emitted in response to protocol input.

---

## Reading a captured frame

The protocol is ASCII-hex inside binary framing, so the ASCII column carries
most of the meaning. Take a real capture from this project:

```
02 45 30 31 38 30 30 30 35 43 03 45 39  |.E0180005C.E9|
```

Split it:

| Bytes | Meaning |
|---|---|
| `02` | STX |
| `45` = `'E'` | extended command |
| `30 31` = `"01"` | subcommand: low nibble 1 = program memory, high nibble 0 = read |
| `38 30 30 30` = `"8000"` | address 0x8000 |
| `35 43` = `"5C"` | count — 0x5C = **92 bytes** |
| `03` | ETX |
| `45 39` = `"E9"` | sum-check: bytes from the command through ETX, mod 256 |

So: *read 92 bytes of program memory from 0x8000* — the parameter block. The
subcommand nibbles are worth memorising: low 0 = device memory, 1 = program
memory; high 0 = read, 1 = write. That gives `00`/`01` read and `10`/`11`
write.

Forces use a different shape — `02 'E' '7' <4 hex> 03 <sum>` for ON, `'8'` for
OFF — and the address is sent **byte-swapped**: `"2C01"` on the wire means
0x012C. That detail is why forcing Y0 and reading the captured address is the
test that settles whether the force bases in `fx_addr.h` are right.

---

## The subtlety that matters most

**The tracer records inbound frames only.** Replies are built and sent by
`send_read_response()` and are never entered into the ring, so they never
appear in a dump.

That makes `ok` narrower than it looks. `ok` means *we received this frame,
parsed it, and answered*. It says nothing about whether the host was satisfied
with the answer.

The two failure signatures are therefore quite different:

- **`NAK` entries** — we could not parse what arrived. The fault is in our
  parser or address map, and the frame in front of you is the specification of
  what to fix.
- **All `ok`, but the same short sequence repeating** — we answered everything
  and the host kept re-asking. The fault is in the *content or delivery* of a
  reply, which the trace cannot show you. Look at reply length, checksum, or
  the data being returned.

Both have occurred in this project, and telling them apart is the first thing
to do with any new dump.

---

## Limits

- Twelve frames, tail-kept, 144 bytes each. A long download overruns the ring;
  the useful part is the end.
- Monitor Mode's *result* reads (`E 00 17xx`, from `FX_MON_RESULT`) are **not**
  filtered, only its watch-list writes. A long monitoring session will still
  crowd the ring with polling, so keep captures short when hunting a one-off
  frame.
- RAM-resident. A power cycle or a replug erases it.
- GX Works and a terminal cannot share the COM port, so captures are read
  after the fact, never live.
- Replies are invisible, as described above.
