# ISU-FX2N — Improvement Suggestions

A review of the repository as of 2026-08-10 (branch `main`, HEAD `6180820`).

The codebase is in good shape: the portable/hardware split is real and enforced
by `tests/run_tests.sh`, protocol findings are documented at the point of use,
and every device accessor range-checks its index. All 197 host-side checks pass.
What follows is what a careful second pass turns up, ordered by how much it
costs a student who hits it.

Findings are separated into things that are **wrong** (verified defects),
things that are **missing** (gaps with real consequences), and things that are
**stale** (documentation that no longer matches the code). Each item names the
file and line so it can be checked rather than taken on trust.

---

## 1. Correctness defects

### 1.1 `PLS` oscillates instead of pulsing — FIXED

*Resolved. The edge is now measured per program step by `pulse_rising_edge()`
in `src/plc_exec.c`, discarded on the first scan after RUN, and covered by 14
new checks in `tests/test_plc_exec.c`. The account below is kept as the record
of what was wrong.*

`OP_PLS` derived its edge state from the destination bit rather than from the
rung:

```c
write_bit(dev, operand, result && !read_bit(dev, operand));
```

With the rung held true, this produces a square wave at scan rate, not a
one-scan pulse. Verified on the host with a four-word program
(`LD X0 ; PLS-prefix ; PLS M0 ; END`) and X0 held on:

```
scan 1: M0=1     scan 3: M0=1     scan 5: M0=1
scan 2: M0=0     scan 4: M0=0     scan 6: M0=0
```

The comment immediately above the line asserts the opposite ("a repeat scan
with the rung still true will not re-pulse"), so this reads as correct on
inspection — which is exactly why it survived.

A student using `PLS` to increment a counter once per button press will instead
increment it on every other scan for as long as the button is held. Because
`M8002`-driven initialisation is often written with `PLS`, the failure can also
look like a program that re-initialises itself continuously.

**Fix applied.** One bit of previous-rung state per program step (2000 bytes for
the full 16000-step space), indexed by the instruction's own offset, so every
`PLS` has an independent history with no collisions. The same table is what
`PLF` and the pulse (`P`) variants of the applied instructions will need.

Note that `tests/test_plc_exec.c` had 20 test functions and none exercised
`PLS` — the encoding was described in `plc_exec.h` but never executed by the
suite, which is why a comment asserting the wrong behaviour went unchallenged.

### 1.2 Modbus handlers read fields the frame may not contain — `src/modbus.c:230`

`modbus_task()` validates only `len < 4` and the CRC before dispatching. Every
handler in `handle_frame()` then reads `rx[2]` through `rx[5]` unconditionally.
A four-byte frame `[addr][func][crc][crc]` passes both checks, and
`handle_read_words()` (`src/modbus.c:91`) reads its start address and count out
of the CRC bytes and whatever the *previous* frame left in the static `rx`
buffer. The result is a well-formed reply describing devices the master never
asked for.

The two multi-write handlers go further and read past the buffer:

- FC 0x0F (`src/modbus.c:152`): `bytes` is a `uint8_t`, so `bytes == (count+7)/8`
  admits `count` up to 2040. The loop then evaluates `rx[7 + i/8]` up to
  `rx[261]`, six bytes past `rx[256]`.
- FC 0x10 (`src/modbus.c:172`): `bytes == count * 2` admits `count` up to 127,
  and `be16(&rx[7 + i*2])` reads up to `rx[260]`.

These are out-of-bounds reads of adjacent statics, not writes, so they will not
crash — they will silently write 2040 coils from garbage. On a trainer wired to
real outputs that is a safety-relevant behaviour, not just a tidiness issue.

**Fix.** Add a per-function minimum-length table checked before dispatch, and
bound `count` against the bytes actually received (`len`), not only against the
declared byte count:

```c
static const uint8_t min_len[] = { [0x01]=8, [0x02]=8, [0x03]=8, [0x04]=8,
                                   [0x05]=8, [0x06]=8, [0x0F]=9, [0x10]=9 };
```

and for 0x0F/0x10 require `len >= 9 + bytes`. Return
`MB_EX_ILLEGAL_VALUE` otherwise.

### 1.3 Broadcast writes are documented as acted on, but are dropped — `src/modbus.c:239`

```c
/* Address 0 is a broadcast: act on it, but never reply. */
if (rx[0] != slave_addr && rx[0] != 0) return;
stat_frames++;
if (rx[0] == 0) return;   /* broadcast writes are not acknowledged */
handle_frame();
```

The second `return` fires before `handle_frame()`, so a broadcast is counted and
then discarded. Either the comment is wrong or the code is; the Modbus spec says
a broadcast write must be performed silently, which makes the code wrong.

**Fix.** For broadcast, call `handle_frame()` with replies suppressed (a
`silent` flag consulted by `send()` and `send_exception()`), or state plainly in
the comment that broadcast is unsupported and drop the misleading first
comment.

### 1.4 Block stack saturates rather than wrapping — `src/plc_exec.c:984`

```c
if (block_sp < 16) block[block_sp++] = result;
```

The comment at `src/plc_exec.c:583` says "entries nobody pops are simply
discarded when the stack wraps". It does not wrap — it saturates, discarding the
*newest* entries. A rung with more than 16 parallel branches will therefore
combine `ANB`/`ORB` against the wrong operand instead of losing an old,
irrelevant one. `block_sp` resets at every coil, so this needs a genuinely wide
rung to trigger, but the failure is silent when it does.

**Fix.** Either make the discard explicit and count it through
`bad_instruction()`, or raise the depth. A real FX allows 11 levels of `MPS`
nesting; matching that and rejecting deeper programs is more honest than
silently mis-evaluating them.

### 1.5 Flash commit waits on the wrong signal — `src/plc_storage.c:87`

```c
/* Hold off until both the program has settled and the link is quiet, so a
 * commit never lands in the middle of a multi-frame download. */
if (now - dirty_at_ms < 2000) return;
```

`dirty_at_ms` is stamped by `plc_storage_mark_dirty()`, which
`fx_protocol.c:419` calls only on writes to program memory. The timer therefore
measures silence *on program writes*, not silence on the link. Two seconds after
the last program byte, GX Works is typically in Monitor Mode and polling — and
`commit()` disables interrupts for the duration of an 8-sector erase plus a
32 KB program, on the order of a quarter second, during which USB is dead and
polls are lost.

**Fix.** Gate the commit on the last *frame* timestamp rather than the last
program write, or simply refuse to commit while `plc_scan_get_mode() ==
PLC_MODE_RUN` — a download always leaves the PLC stopped, so the natural moment
to persist is before the operator presses RUN.

---

## 2. Missing safeguards

### 2.1 The watchdog is cosmetic

`D8000` is initialised to `FX2N_WATCHDOG_MS` at `src/plc_scan.c:81` and reported
in the diagnostic dump, but nothing enforces it and `hardware/watchdog` is not
linked (`grep -rn watchdog src/` returns only that assignment). A real FX faults
and stops on a scan-time overrun; here, a pathological program simply produces a
large `D8010` that nobody reads.

This matters more than it looks. Two paths in the interpreter can make a scan
arbitrarily long: `find_subroutine()` linear-scanning 32 KB per `CALL`
(see §3.1), and any future jump instruction. A hardware watchdog is ~10 lines
and converts "the trainer is frozen and the student does not know why" into a
reboot with `sts1` lit.

**Suggestion.** `watchdog_enable(D8000 * 4, true)` in `main()` plus
`watchdog_update()` once per scan, and a check in `plc_scan_end()` that trips
`M8009`-style error reporting when `tenths` exceeds the configured limit.

### 2.2 The scan cycle has no automated tests

`src/plc_scan.c` — timers, counters, clock pulses, the STOP→RUN transition,
scan-time accounting — is the file most likely to produce a surprising result
for a student, and it is the one file with logic that no test compiles.
`tests/run_tests.sh` cannot include it because it pulls in `pico/stdlib.h` and
`board.h`.

The consequence is visible in `tests/test_plc_exec.c:26-49`, which **reimplements**
`plc_timer_drive` and `plc_counter_drive` as stubs. So the 104 execution checks
validate the interpreter against a second, simpler counter implementation, not
against the one that ships. The stub does not implement the 32-bit
bidirectional path at all, so `C200`–`C255` behaviour is untested end to end.

**Suggestion.** This is the highest-value structural change in the review.
Introduce a two-function time source:

```c
/* plc_time.h */
uint64_t plc_time_us(void);      /* time_us_64() on target, settable on host */
```

and replace the three `time_us_64()` calls in `plc_scan.c` with it. Split the
board I/O out of `plc_scan_begin`/`plc_scan_end` behind the existing `board.h`
interface (a host stub is ~20 lines). `plc_scan.c` then joins the portable
column of the table in `README.md:202`, and tests can assert things that
currently cannot be checked at all: that a 100 ms timer takes 100 ms, that a
retentive timer survives a coil drop, that `M8013` is a symmetric square wave,
that `C200` counts down when `M8200` is set.

### 2.3 No continuous integration

There is no `.github/` directory. `tests/run_tests.sh` is fast, has no
dependencies beyond `cc`, and already uses `-Wall -Wextra -Werror`. A ten-line
workflow running it on push, plus a firmware build against a pinned Pico SDK,
would catch regressions in a project whose correctness is otherwise established
by reading.

The build is already documented as reproducible in `Build-ISU-FX2N-uf2.md`,
which makes the CI job mostly a transcription exercise.

---

## 3. Performance

Neither item below is currently causing a visible problem, but both scale with
program size and both are cheap to fix.

### 3.1 `find_subroutine()` rescans program memory on every `CALL` — `src/plc_exec.c:226`

Each `CALL` executed walks up to 16000 words looking for the `0xB0nn` marker.
A program with several subroutines called every scan pays that cost every scan.
`FSM_Counter` in `TestedPrg.md` uses `CALL`, so this is on the live path.

**Fix.** Build a 256-entry pointer table on first scan after a program write
(the same event that sets the storage dirty flag), and invalidate it there.

### 3.2 `plc_exec_has_program()` runs every scan — `src/main.c:98`, `src/plc_exec.c:551`

The main loop asks "is a program present?" once per scan, and the answer is
computed by scanning from `PLC_CODE_OFFSET` until an `END` is found. With a
program loaded that is roughly a second pass over the whole program body; with
no program loaded it is a full 32 KB sweep, every scan, forever — which is
precisely the state a freshly flashed trainer sits in while running
`demo_ladder`.

**Fix.** Cache the result. It can only change when program memory is written,
which is already a single choke point (`plc_program_write`).

---

## 4. Documentation drift

The comments in this repo are unusually good, which makes the stale ones
unusually costly — they are written with enough authority to be believed.

### 4.1 `README.md` contradicts itself about RUN/STOP, three ways — FIXED

*Resolved. All three passages now say the same thing: RUN/STOP is controlled
only from Online → Remote Operation, and no trainer input is reserved for it.
The account below is kept as the record of what was wrong.*

- `README.md:48` — "**I9 toggles RUN/STOP.** … so I9 stands in."
- `README.md:81` — "All ten physical trainer inputs remain available to the user
  program; in particular, I9 maps to FX device X11 and is **not** reserved for
  local RUN/STOP control."
- `README.md:243` — "**Remote RUN is not implemented.** A download issues remote
  STOP correctly; use I9 to return to RUN."

The code agrees with the middle one. There is no I9 handling anywhere —
`plc_scan_set_mode()` is called from exactly three places
(`main.c:91` at startup, and `fx_protocol.c:353`/`355`/`452`), none of which
reads an input. Remote RUN *is* implemented, via the M8035/M8036/M8037 force
sequence.

So the Quick start instruction "press **I9** on the trainer to put the PLC in
RUN" (`README.md:21`) does not work, and a student following it will conclude
the firmware is broken. This is the single most user-visible problem in the
repository.

**Fix.** Delete the two stale claims, and correct the Quick start to use
Online → Remote Operation → RUN. If local RUN/STOP is still wanted, implementing
it is ~5 lines in `main.c` — but note it would then conflict with `README.md:81`,
so pick one and say so.

### 4.2 `README.md` describes an automatic diagnostic dump that was removed — FIXED

*Resolved. The Diagnostics section is rewritten around the bare `?` command and
says why there is no automatic dump. The empty `fx_protocol_idle_dump()` and its
`main.c` call site are still there and can still go.*

`README.md:167` — "When the link has been idle for 2 s, the firmware prints a
diagnostic report over USB CDC every 3 s". `fx_protocol_idle_dump()`
(`src/fx_protocol.c:575`) is an empty function whose comment explains at length
why the behaviour was deliberately removed, and `README.md:234` correctly states
it is request-only. The Diagnostics section should be rewritten around the bare
`?` command; the empty function and its `main.c:113` call site can then go too.

### 4.3 `src/main.c:1-16` still describes Milestone 6

> "The user program is still hardcoded in C below (demo_ladder) because the
> instruction decoder does not exist yet - that is the next milestone, at which
> point this function is replaced by the interpreter"

The interpreter exists, is 1055 lines, and is already the default path
(`main.c:98`). This is the first file a student opens.

Related: `demo_ladder()` (`main.c:33`) is now a fallback for unprogrammed
devices. Worth keeping — it gives a bare trainer something to do — but the
header should say that rather than describing it as a placeholder.

### 4.4 `src/plc_program.h` says persistence is not implemented

> "This is plain RAM for now. Persisting it to flash so a program survives a
> power cycle is the next milestone"

`plc_storage.c` has done this since before the current HEAD, and `README.md:24`
advertises it.

### 4.5 Debug scaffolding for one specific test program is in the shipped dump

`src/fx_protocol.c:550-561` prints `FSM_Counter`-specific state (M1486, M1487,
M1503, D995, D997, D998) and hex dumps of two fixed program offsets (`+005C`,
`+019C`) on every `?`. These were clearly invaluable while decoding that
program; they are noise for anyone else, and they make the dump longer than
some terminals retain — which the comment at `src/fx_protocol.c:540` notes as a
problem it is simultaneously worsening.

**Suggestion.** Replace with a generic, parameterised form: `?` for the summary,
and something like `?<hex-offset>` to dump 32 bytes anywhere in program memory.

---

## 5. Repository hygiene

- **`tests/test_plc_memory` is an untracked binary.** The other four test
  binaries are listed in `.gitignore:9-12`; this one was added later and missed,
  so `git status` currently shows a 16 KB executable as untracked. Add it, or
  better, replace the four explicit lines with `tests/test_*` and a
  `!tests/test_*.c` exception.
- **`CodexDMS.txt` (306 KB) is untracked and unexplained.** Either ignore it or
  document what it is.
- **Instruction documentation is spread over four files** —
  `Add-To-Instruction-Set.md`, `Instruction-Set-To-Be-Implemented.md`,
  `Implement-STL.md`, and the "Supported instructions" section of `README.md` —
  with overlapping content and no statement of which is authoritative. The
  README list is the one that is accurate; the others read as working notes.
  Consider folding them into a single `INSTRUCTIONS.md` with three columns
  (implemented / captured but unimplemented / not attempted), and having the
  README link to it.
- **`TestSTL1/` and `TestSTL2/` are untracked GX Works project directories** at
  the repository root while every other project lives in `projects/`. Move or
  ignore them.

---

## 6. Design suggestions for future work

These are opinions rather than defects, offered in the order they would pay off.

### 6.1 Give the interpreter a dispatch table

`plc_exec_scan()` is a 490-line function containing a chain of `if (operand ==
MISC_xxx)` blocks, several of which are 40+ lines of inline operand decoding
(`MISC_ABSD`, `MISC_DECO`, `MISC_SFTL`). `execute_applied()` already
demonstrates the better pattern for the uniform instructions.

The reason this matters is the roadmap: `Instruction-Set-To-Be-Implemented.md`
lists a substantial set still to add, and each one currently means another
branch in an already-long function. A table of
`{ opcode, operand_word_count, handler }` would let each new instruction be a
self-contained function with a self-contained test, and would let the "consume
the whole instruction even when malformed" rule — currently repeated by hand at
every site, and easy to get wrong — be enforced in one place by the dispatcher.

### 6.2 Import the latch ranges that are already being downloaded

`README.md:245` lists configurable latch ranges as not imported. But
`plc_program.c:18` shows the parameter block *is* being received and stored, and
its comment identifies "+30 latched device range boundaries". The bytes at
offsets 0x30–0x3F (`F4 09 FF 0B F4 01 E7 03 64 0E C7 0E DC 0E FF 0E`) decode
plausibly as little-endian pairs — 2548/3071, 500/999, 3684/3783, 3804/3839 —
which look like M, S, and two more range pairs. Decoding them would let
`plc_memory_reset_nonretentive()` (`src/plc_memory.c:9`) honour what the student
set in GX Works instead of the hardcoded policy, and it is a satisfying exercise
because the evidence is already in the repository.

### 6.3 Deduplicate the two ASCII-hex helpers

`hex_digit`/`hex_value`/`parse_hex` in `fx_protocol.c:116-144` are general-purpose
and would be the natural home for a small `hexutil.h` — but they are used by only
one file today, so this is worth doing only if a second protocol arrives (the
Modbus ASCII variant, say). Noted so it is a deliberate choice rather than an
oversight.

### 6.4 Modbus master, as `To-Do.md` proposes

The existing note is sound and the constraint it identifies is the real one: the
UART cannot be both roles at once. Worth adding to that analysis — the Pico has
a second UART, and GPIO4/GPIO5 are marked "reserve" in the pin table
(`README.md:42`). Running the master on `uart1` at GPIO4/5 would sidestep the
role-switching question entirely and let a trainer be master and slave
simultaneously, which is what a two-trainer teaching exercise actually wants.

---

## Summary

| # | Item | Severity | Effort |
|---|---|---|---|
| 1.1 | ~~`PLS` oscillates instead of pulsing~~ **fixed** | High — wrong results | done |
| 4.1 | ~~README's I9 RUN/STOP instructions do not work~~ **fixed** | High — blocks quick start | done |
| 4.2 | ~~README describes a removed automatic dump~~ **fixed** | Low | done |
| 1.2 | Modbus reads unvalidated / out-of-bounds frame fields | Medium — can drive outputs from garbage | Low |
| 2.2 | Scan cycle (timers/counters) has no tests | Medium — hides future regressions | Medium |
| 1.5 | Flash commit can interrupt a live Monitor session | Medium | Low |
| 2.1 | Watchdog declared but not enforced | Medium | Low |
| 1.3 | Broadcast writes silently dropped | Low | Low |
| 1.4 | Block stack saturates on wide rungs | Low | Low |
| 3.1, 3.2 | Per-scan linear scans of program memory | Low | Low |
| 4.3–4.5 | Stale comments and leftover debug output | Low | Low |
| 2.3 | No CI | Low | Low |

Both of the items a student hits on their first afternoon with the trainer —
**1.1** and **4.1** — are now fixed.
