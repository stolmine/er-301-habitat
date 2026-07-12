# Anamnesis: data-abort on insert (audio ISR near-null deref)

**Status:** open bug, needs fix + ledger item (file it when you pick this up).
**Severity:** high. Inserting Anamnesis crashes the ER-301 within ~0.2s.
**Where:** the bug is in `mods/anamnesis` (a memory-corruption on insert). The
crash *surfaces* in firmware kernel code, but the *cause* is Anamnesis.

## What happens

On am335x hardware, inserting Anamnesis into a chain produces a **data-abort**
about 0.2s later, in the audio interrupt handler. The device warm-reboots. This
was captured by the stolmine crash-diagnostics facility (fw 9.5.2.56, crash
diagnostics armed) with a full flight-recorder trigger + symbolized fault.

The user perceived this as a "hang on insert"; it is actually a trap (the
exception hook caught it, not the hang monitor).

## The captured report (verbatim)

```
---CRASH REPORT BEGIN
Schema: 2
Kind: data-abort
Time Since Boot: 15.160s
Firmware Version: 0.7.0-stolmine.9.5.2.56
Thread: hwi (handle=0x0)
--- Registers ---
 pc=803d9c0c lr=81a5aaa4 sp=803d9c0c psr=819a4668
 dfsr=00000005 ifsr=00000000 dfar=00000062 ifar=00000000
 r0=0000004a r1=00000001 r2=00000001 r3=8050e884 r4=805370e8 r5=805370ec r6=a000011f
 r7=00000001 r8=49002070 r9=00000000 r10=00000001 r11=819a4668 r12=8000011f
--- Module Map ---
 kernel                   text=80000000..804f2d50
 0:/v0.7/libs/anamnesis/libanamnesis.so text=80e31600..80e3ca7c  data=80e21e00..80e23b64
 0:/v0.7/libs/teletype/libteletype.so text=80df2f80..80dfa2bc  data=80dd5380..80dd6074
 0:/v0.7/libs/txo/libtxo.so text=80dfa300..80e003b4  data=80dd7640..80dd8324
--- Fault Resolution ---
 pc in kernel + 0x803d9c0c
 lr in ?
--- Flight Recorder ---
   14.939s  insert Anamnesis
--- Recent Log ---
 (not captured C-side; see flight recorder above)
---CRASH REPORT END
```

## Symbolization (against the 9.5.2.56 firmware app.elf)

```
pc=0x803d9c0c -> ti_sysbios_knl_Event_post  (Event.c:285)
```

`Event.c:285` is, inside `Event_post`:
```c
elem = (Event_PendElem *)Queue_head(pendQ);          // pendQ = &event->pendQ
elem->matchingEvents = Event_checkEvents(event, elem->andMask, elem->orMask);  // <- faults here
```

- `dfar=0x00000062`, `dfsr=0x5` = **translation fault on a near-null address**
  (a null-ish pointer dereferenced at field offset 0x62). This is a genuine
  bad-pointer deref, NOT an alignment/NEON `:64` fault (that would be `dfsr=0x1`).
- `Thread: hwi`, `r8=0x49002070` = the AM335x **EDMA** register block, i.e. this
  is the **audio EDMA completion interrupt**.

## Root-cause model

The firmware audio driver (`arch/am335x/hal/audio.c` in er-301-stolmine) drives
audio with a SYS/BIOS `Event`: the EDMA completion ISR calls
`Event_post(local.hEvents, pingDone)` to wake the audio task. That `Event` object
(and its pend queue) is a small, long-lived allocation created once at boot.

Inserting Anamnesis **corrupts memory that overlaps that Event object or its pend
queue** (a stray write leaves the pend-queue pointer chain near-null). The very
next audio interrupt calls `Event_post`, walks the corrupted queue, dereferences a
near-null element, and traps.

The fault landing in kernel `Event_post` rather than in `libanamnesis.so` is the
signature of **heap / adjacent-memory corruption**: the crash surfaces in whoever
next touches the clobbered region, not in the code that clobbered it. The
flight-recorder `insert Anamnesis` 0.2s prior ties the corruption to Anamnesis's
insert path (its constructor and/or its first `process()`).

So: this is an **out-of-bounds write / bad-pointer store in Anamnesis**, most
likely during construction (which runs at insert) or the first process block.

## Where to look

Anamnesis is a large DSP atom with many fixed-size buffers initialized in its
constructor, which is exactly the code that runs on insert:

- `mods/anamnesis/atoms/Anamnesis.h`
  - **Constructor (~L138-224):** a long run of `memset`/loop-initializers over
    fixed arrays: `mLoopBuf`, `mLine`, `mAp`, `mApWr`, `mTapBuf`, `mHannLut`,
    `mGrain*`, `mDrop*`, `mBub*`, `mBandZ`, the pan/tap/FDN LFO tables. Audit every
    loop bound against its array's declared size. A single loop that runs past the
    end of one array writes into the adjacent object (which, in the DSP heap, can
    be the audio Event / another unit / the allocator's own bookkeeping).
    - Checked one lead already: the `mBandZ` Fisher-Yates init uses `nb =
      kStreamN/2 = 7` and looks in-bounds, so start elsewhere.
  - **`process()` (~L356+):** interpolated reads/writes into `mLoopBuf`, `mLine`,
    `mTapBuf` via computed indices (`mLoopBuf[i0..i3]`, `mLine[i][...]`). Confirm
    every computed index is clamped to `[0, kLen)` on the WRITE side. A write index
    that can go negative or past the buffer on the first block is a prime suspect.
- `mods/anamnesis/AnamNoise.h` — `perm[512]` and the `static float L[kLUT*kLUT]`
  LUT build; confirm `perm[perm[x]+y]` indices stay < 512 and the LUT fill is in
  bounds.
- `mods/anamnesis/AnamField.h`, `mods/anamnesis/anamnesis.cpp.swig` — any buffer
  sized from a parameter (Length/Density/etc.) that a first-block `process()`
  writes before it is fully sized.

Favor the **constructor and first-`process()` write paths** (the crash fires at
insert, ~0.2s in), and favor **write** indices over reads (a bad read would fault
inside Anamnesis, not corrupt a neighbor).

## How to reproduce / verify (emu first, per habitat convention)

1. **Insert Anamnesis in the emulator.** A logic OOB write reproduces on x86, so it
   may crash or misbehave there too. If it does, this is diagnosable without
   hardware. Build the mod with a sanitizer if the emu build allows
   (`-fsanitize=address`) and insert Anamnesis: ASan should name the exact
   out-of-bounds store. Even without ASan, the emu + a code audit of the write
   paths above should find it.
2. If the emu is clean but hardware still traps, it is am335x-specific (alignment /
   NEON / SWIG size), but `dfar=0x62` (a null-ish deref, `dfsr=0x5`) argues for a
   plain logic OOB that should reproduce off-hardware.
3. **Confirm the fix on am335x hardware:** with crash diagnostics armed (Admin >
   System Settings > "Enable crash diagnostics?"), insert Anamnesis repeatedly and
   confirm no `data-abort in Event_post`. Suggested ledger verify (file when you
   pick this up, area `dsp`, tag `[hab:<id>]`): "insert Anamnesis in the emu
   (ASan-clean if available) AND repeatedly on am335x hardware with crash
   diagnostics armed produces no corruption / no Event_post data-abort."

## Update 2026-07-12: ASan disproves the index-OOB model (current code is clean)

Built `libanamnesis.so` with `-fsanitize=address`, ran the headless emu under
`LD_PRELOAD=libasan`, and drove a full insert (unit landed in the chain -
`Chain.getChain(1):length()==1`; `process()` ran - bubbles spawned and rendered
as metaballs; 300+ frames post-insert). **ASan reported nothing.** Plus a manual
audit of every member-array WRITE in the ctor and `process()`:

- `mLoopBuf[mLoopWr]` - `Lint` clamped to `kLoopBufLen` (L661). in-bounds.
- `mTapBuf[mTapWr]` - `mTapWr` wraps at `kTapBufLen`. in-bounds.
- `mAp[k][idx]` - `idx` wraps at `kApLen[k]`; `kApLen={113,211,337,449}` all
  `<= kApMax=449`. in-bounds.
- `mLine[i][mWr]` - `mWr` wraps at `kFdnBufLen`. in-bounds.
- bubble/drop/grain spawns - every `slot`/`g`/`parent` is found in range or
  `% kVizMax*`. in-bounds. ctor init loops all bounded.
- `buildFieldFrame` `mFcGrid[L*kFieldGW*kFieldGH + j*kFieldGW+i]` - max index
  6071 vs size 6072. in-bounds.

**Conclusion: no logic OOB in the current code (0.2.0.83).** The "index overrun
corrupts the Event" model above is disproven for the current build. Also:
anamnesis is **single-TU** (only the SWIG wrapper `.o`), so a stale wrapper is
self-consistent (no sizeof MISMATCH) - that mechanism can't produce an OOB here
either.

So the hardware data-abort is **not** a plain logic OOB that reproduces on x86.
Remaining candidates, most-likely first:

1. **am335x task-stack overflow (invisible to x86 ASan).** The viz draw path runs
   ~2.4 KB of stack-local arrays on the display task - `buildFieldFrame`:
   `sbX/sbY/sbR/sbAmp/sbLvl[96]` (1.9 KB) + `ptX/ptY[28]` + `bX/bY/bR/bSeed/bLvl[12]`.
   On a small SYS/BIOS task stack this can overflow into adjacent memory;
   `dfsr=0x5` (near-null deref of a *corrupted pointer*) fits a corrupted-neighbour
   scenario better than an index overrun (which ASan would have caught). **Fix:**
   promote those arrays to `Anamnesis` class members (heap), per the codebase's
   am335x class-member-storage rule. NOTE: this stack usage **predates Item 1** -
   the per-ply `draw()` held the same arrays inline - so it is a latent am335x bug,
   not an Item-1 regression. `dfsr=0x5` is NOT the NEON `:64` alignment trap
   (`dfsr=0x1`), so this is plain stack pressure, not a vld1 hint.
2. **The 9.5.2.56 capture was a stale/older build** (its anamnesis version is
   unrecorded). The clean `0.2.0.83` (force-cleaned wrapper) may not reproduce it.

**NEXT:** re-capture on am335x hardware with the CLEAN `0.2.0.83` + crash
diagnostics armed. If it still traps -> apply fix #1 (class-member the viz arrays)
and re-verify. If it does not -> the original was a bad build.

## Update 2026-07-12 (fw 9.5.2.58, per-task STACK instrumentation): NOT a stack overflow. A wild WRITE clobbers a Task_Object.

The stolmine harness gained per-task + ISR stack high-water + canary reporting and
was re-benched against the frozen `0.2.0.83` insert. It caught + rebooted + flushed
a data-abort (same `Event_post` / `Event.c:285`, `Thread: hwi`, `dfar=0x000000b5`,
flight recorder `insert Anamnesis`). The new `--- Stacks ---` section:

```
 log   used=360   (8%)   canary=ok
 app   used=13852 (42%)  canary=ok      <- the main/display task, HEALTHY
 usb   used=388   (9%)   canary=ok
 adc   used=748   (36%)  canary=ok
       base=0000001c size=167 canary=BLOWN   <- a CORRUPTED Task_Object (blank name)
 isr   used=1024  (25%)  canary=ok
```

**This DISPROVES the stack-overflow hypothesis (#1 above).** Every real task stack
is comfortably in bounds with an intact canary; the `app` (main/display) task, the
one that runs the viz `draw()`, is at 42 percent. The ~2.4 KB viz stack arrays are
NOT overflowing. (So promoting them to class members is not the fix for THIS crash,
though it may still be worth doing on general principle.)

**What it actually is: a stray / wild WRITE.** The `--- Stacks ---` section surfaced
a real Task_Object whose fields were overwritten with garbage: `stack` pointer ->
`0x0000001c`, `stackSize` -> `167`, and its env/name pointer clobbered (blank name).
The enumeration then STOPPED right after it (only 5 of ~9 tasks listed) because that
Task_Object's list-link `next` pointer was clobbered too. The fault's `r4/r5` =
`0x80538150/54`, sitting just above the `adc` task stack top (`0x80538128`). So a
stray write lands in the runtime task-object / heap region around **`0x80538xxx`**
and clobbers BOTH a Task_Object AND the audio `Event`'s pend queue; the next audio
EDMA interrupt walks the wrecked queue and traps in `Event_post`.

**Why ASan did not catch it:** this is a wild-pointer / bad-address store, not an
index overrun of a known allocation. ASan flags out-of-bounds access relative to a
tracked object; a write through an **uninitialized, dangling, or wrongly-computed
pointer** whose target address happens to be valid (unshadowed) on x86 but collides
with the task/Event region only on the am335x memory map will pass ASan clean. That
matches the evidence (ASan clean, but hardware corrupts a specific low-DDR region).

**Where to look now (in `mods/anamnesis`):** a WRITE through a pointer that is not
an in-object array index but a raw/derived pointer. Prime suspects:
- an **uninitialized or default-constructed pointer member** written in the ctor or
  first `process()` (the crash fires ~0.2s after insert);
- a pointer produced by **cast / arithmetic / reinterpret** (SWIG-boundary object,
  a `void*`, an `od::` handle) that is wrong on the target;
- a **use-after-free / dangling** handle to an object whose address only aliases the
  task/Event region on am335x;
- a write to a **near-absolute or offset-from-tiny-base** address (the clobbered
  Task_Object now holds `stack=0x1c`, `size=167` and the fault has `r0=0x9d`,
  i.e. small integers 28 / 167 / 157 were written -- grep Anamnesis for a small
  struct/array of counts or indices being stored through a pointer, and check that
  pointer's provenance).

The stolmine side is building an object-guard (`crashdiag-object-guard-event`) to
trap the write AT the store and record the writing `pc`, which would hand you the
exact instruction. In the meantime, the target region (`~0x80538xxx`) and the
written values (small ints `0x1c`/`0xa7`/`0x9d`) are the fingerprints to hunt.

## Update 2026-07-12 (habitat static hunt for the wild write): eliminations, no source-level culprit found

Following the wild-write / clobbered-Task_Object finding, audited `mods/anamnesis`
for a source-level bad-address store. Eliminated:

- **No pointer members** in `Anamnesis` (op) or `AnamFieldGraphic` (only `mpOp`,
  which is `= 0`-initialized and always `if (mpOp)`-guarded). So no uninitialized/
  default-constructed pointer member to write through.
- **No raw-pointer / `memcpy` / `reinterpret_cast` / cast-to-pointer writes** anywhere
  in the op.
- **No computed-index array writes** (`arr[a+b] = ...`): every buffer write uses a
  single index VARIABLE that is wrap/clamp-bounded - `mLoopBuf[mLoopWr]` (Lint
  clamped), `mTapBuf[mTapWr]`, `mAp[k][idx]` (idx<kApLen<=kApMax), `mLine[i][mWr]`,
  `mApWr[k]`, `mBandZ[i]`, spawn `slot`/`g`/`parent`, and the field cache
  `mFcGrid[L*kFieldGW*kFieldGH + j*kFieldGW+i]` (float `+=`, max 6071 < 6072).
- The written values 167/157 are **not** anamnesis delay/size constants (`kTapBase`
  = {960,1597,...}, `kApLen`={113,211,337,449}, `kFdnBase`={1669,...}); 28 == kNumPoints
  but kNumPoints is only ever an array SIZE / loop bound, never stored through a pointer.
- `mBubRng`/`mDropRng` are used in the ctor Fisher-Yates before being seeded
  (indeterminate), but they are `uint32_t` RNG state, not pointers -> a value bug at
  most, not a wild store.

**So the corruptor is not an obvious source-level pointer deref or index overrun.**
Combined with the prior eliminations (ASan-clean, stack canaries intact, `dfsr=0x5`
not the NEON trap, single-TU so no sizeof mismatch), the remaining space is a store
that is invisible at the C++ source level: a compiler codegen artifact, a framework
(`od::Object` registration / SWIG-wrapper marshalling) interaction with this unusually
large (~850 KB) object, or a genuinely non-obvious derived pointer. The **decisive
tool is the stolmine object-guard** (`crashdiag-object-guard-event`): trapping the
write AT the store and reporting the writing `pc` maps straight to the instruction.
The habitat static hunt has narrowed WHERE that pc is likely to land (not the plain
DSP index writes) - hand the captured `pc` back here to symbolize against the am335x
`.o` (`arm-none-eabi-addr2line` on `testing/am335x/mods/anamnesis/*.o`).

## Update 2026-07-12 (LEADING ROOT CAUSE): 843 KB object collides with the am335x kernel heap; it is CM4-only

The written-value fingerprint cracks it. The stray writes were small ints
**28 / 167 / 157**. Those are exactly plausible values of **`mApWr`** - the 4
allpass write-positions - each valid against its ring length `kApLen={113,211,337,449}`
(28<113, 167<211, 157<337). `mApWr[k]=idx` runs **every process block** (`atoms/Anamnesis.h:813`).

Converging facts:
- **`sizeof(Anamnesis) ~= 843 KB`** per instance (`mLoopBuf` 375 KB + `mLine` 281 KB +
  `mTapBuf` 152 KB + `mFcGrid` 23 KB + `mAp`/`mHannLut`/... ). Enormous for one unit.
- am335x allocates unit objects from the **SYS/BIOS `HeapMem` kernel heap**
  (`arch/am335x/sysbios/sbl.cfg`: `__kernel_heap_start__..__kernel_heap_end__`) - the
  **same region** that holds task stacks and the audio `Event`.
- The reported corruption is a handful of words at `~0x80538xxx` (one Task_Object +
  the Event), NOT a broad range - consistent with a specific member (`mApWr`, near the
  object tail) landing on top of a live kernel object, not a runaway memset.

**Model: this is a memory-budget collision, not a logic bug.** The 843 KB object does
not fit the am335x kernel-heap model the way it does on CM4; its placement overlaps
live task/Event memory, so Anamnesis's own bounded, ASan-clean member writes
(`mApWr` = 28/167/157, and the per-block `mLine`/`mTapBuf` stores) corrupt the kernel
objects. ASan on x86 is blind because there the object is properly, non-overlappingly
allocated in a huge address space. **This is exactly why Anamnesis is flagged CM4-ONLY**
(`project_spatial_glitch_cm4_unit`): it was never sized for am335x.

**Practical conclusion:** the fix is NOT to hunt a bug in correct DSP code. It is to
**stop Anamnesis from being inserted on am335x** (enforce CM4-only: gate the build /
toc so it does not load on am335x), OR - if am335x support is wanted - cut the
footprint ~10x with an am335x profile (the `mLoopBuf`/`mLine`/`mTapBuf` ~808 KB of
buffers are the bulk; they would need to shrink drastically).

**Confirm with:** (a) the object-guard `pc` (`crashdiag-object-guard-event`) - it
should land on a member store such as `mApWr[k]=` in `atoms/Anamnesis.h`; and (b) a
heap check at insert (free bytes in the kernel `HeapMem` vs the 843 KB request, and
whether the returned block's extent overlaps the task/Event region). Either confirms
the budget-collision model directly.

## Provenance

Captured by the er-301-stolmine crash-diagnostics facility (fw 9.5.2.56 for the
first capture, fw 9.5.2.58 for the stacks capture, normal build,
`enableCrashDiagnostics` armed). The exception hook snapshotted the fault
context + module map + flight recorder into the warm-reboot-surviving panic buffer;
next boot flushed it to `front/crash.log`; symbolized offline via
`tools/symbolize_crash.py` against the matching `app.elf`. The `sp==pc` and bogus
`psr` in the register dump are a known SYS/BIOS-6.46 A8 trap-frame limitation
(tracked in stolmine as `crashdiag-fix-partial-register-capture`); `pc`, `dfar`,
`dfsr`, the module map, and the flight recorder are the load-bearing evidence and
are all correct.
