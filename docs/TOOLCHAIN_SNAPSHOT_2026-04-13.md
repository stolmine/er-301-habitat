# Build toolchain snapshot — 2026-04-13

Documenting the exact toolchain present on the dev machine while the Tomograph
viz regression (see `HABITAT_TOOLCHAIN_VIZ_REGRESSION.md`) is reproducible.
Intent: preserve this for later comparison if the user's last known-good
build environment differed.

## Host machine

| Item                | Value                                                     |
|---------------------|-----------------------------------------------------------|
| Host                | `sure-macbookpro91` (dev machine)                         |
| OS                  | EndeavourOS (Arch Linux derivative), rolling              |
| Kernel              | `6.18.21-1-lts`, x86_64                                   |
| Kernel build date   | 2026-04-02                                                |

## Host-native toolchain (used for emu + SWIG wrapper + Lua syntax checks)

| Item              | Version                                 | Source       |
|-------------------|-----------------------------------------|--------------|
| `gcc`             | 15.2.1 (20260209 build)                 | Arch pkg     |
| `make`            | GNU Make 4.4.1                          | Arch pkg     |
| `swig`            | 4.4.1                                   | Arch pkg     |
| `lua`             | 5.5.0                                   | Arch pkg `lua` |
| (alt)             | `lua54` 5.4.8, `luajit` 2.1.1774896198  | Arch pkgs    |
| `zip`             | 3.0                                     | Arch pkg     |
| `python3`         | default system                          |              |
| `binutils`        | (Arch default, matches gcc 15.2)        |              |

## ARM cross toolchain (used for am335x / Cortex-A8)

The Makefile at `er-301-habitat/scripts/env.mk` + `er-301/scripts/am335x.mk`
explicitly pins the TI-shipped GCC 4.9.3 (2015q3), **not** the Arch-provided
`arm-none-eabi-gcc 14.2.0` that also lives on the system at `/usr/bin/`.
`gcc_install_dir := $(TI_INSTALL_DIR)/gcc-arm-none-eabi-4_9-2015q3` forces
this. All builds in this session confirmed to invoke the TI 4.9.3 compiler.

| Item                       | Value                                                   |
|----------------------------|---------------------------------------------------------|
| Primary cross GCC          | `~/ti/gcc-arm-none-eabi-4_9-2015q3/bin/arm-none-eabi-gcc` |
| `arm-none-eabi-gcc --version` | 4.9.3 20150529 (release) [ARM/embedded-4_9-branch revision 227977] |
| `arm-none-eabi-ld --version`  | GNU ld (GNU Tools for ARM Embedded Processors) 2.24.0.20150921 |
| Binary mtime               | 2015-09-21 12:59:15                                     |
| `gcc` md5                  | `2eda7a25553c6a56ee738f2fcd111414`                      |
| `g++` md5                  | `721e14dd5fe4ebc6da32a35fc5da96c0`                      |
| Newlib                     | (bundled in 4_9-2015q3 installation)                    |

### Alt ARM toolchain (present but unused by habitat build)

| Item                       | Value                                                   |
|----------------------------|---------------------------------------------------------|
| `/usr/bin/arm-none-eabi-gcc` | 14.2.0 (Arch Repository)                              |
| `arm-none-eabi-binutils`   | 2.43-2                                                  |
| `arm-none-eabi-newlib`     | 4.5.0.20241231-2                                        |
| Arch pkg installed         | 2026-02-02                                              |

### Key am335x CFLAGS (from `mods/spreadsheet/mod.mk`)

```
CFLAGS.am335x = -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -mabi=aapcs \
                -Dfar= -D__DYNAMIC_REENT__
CFLAGS.speed  = -O3 -ftree-vectorize -ffast-math
CFLAGS.swig   = $(CFLAGS.common) $(CFLAGS.$(ARCH)) $(CFLAGS.size)   # -Os
```

`-ffast-math` is in the speed profile (release/testing builds), not in the
SWIG wrapper. The wrapper is compiled with `-Os` instead.

### Key other TI bundle pieces (unchanged, baseline reference)

```
bios_6_46_05_55
cg_xml
edma3_lld_2_12_05_29
ndk_2_25_01_11
pdk_am335x_1_0_8
processor_sdk_rtos_am335x_4_01_00_06
ti-cgt-pru_2.1.5
xdctools_3_32_01_22_core
```

## Repo state at time of snapshot

| Repo                                          | HEAD                                               |
|-----------------------------------------------|----------------------------------------------------|
| `~/repos/er-301-stolmine` (SDK, via symlink)  | `0df7a03` (2026-04-09), `v0.7.0-stolmine.9.0.0`    |
| `~/repos/er-301-habitat`                      | `52b3213` (2026-04-12), `v2.1.0`                   |

Habitat SDK symlink: `er-301-habitat/er-301 -> /home/sure/repos/er-301-stolmine`.

Firmware binary present in `er-301/release/am335x/app/app.bin`:

```
md5  3a4d9cc883db9a438788510116a70be2
```

No `/mnt/ER-301/firmware/` mounted at snapshot time (SD card not currently
attached in read mode).

## What's most likely different from the last known-good build environment

Ordered by how plausibly a change there would break ARM/NEON rendering
while not breaking the emu x86 build:

1. **Kernel 6.18.21 (released April 2026)** — very new. Any regression in
   the TTY/console path or USB-serial flashing path could matter only for
   deployment, not runtime rendering. Probably not the culprit.
2. **Host GCC 15.2.1 (Feb 2026)** — compiles the **SWIG wrapper** for the
   host-side build only. The ARM `.so` is compiled by the pinned TI 4.9.3,
   which hasn't changed. Emu+SWIG mismatch could produce weird behavior
   only in emu; hardware is unaffected.
3. **SWIG 4.4.1** — could generate subtly different wrapper code than
   older versions. Generated `.cpp` is compiled by TI 4.9.3 for ARM. If
   SWIG 4.4 generates C++17 constructs the 4.9.3 compiler silently
   miscompiles, that would produce ARM-only runtime bugs. Worth checking:
   was the last known-good build from SWIG 4.2 or earlier?
4. **Lua 5.5.0** — affects host-side Lua syntax checks only; the ER-301
   firmware ships its own embedded Lua (5.4.x, not the host's `lua`
   binary). No runtime impact.
5. **TI GCC 4.9.3 (2015)** — binary byte-identical to a 2015 release,
   mtime 2015-09-21. If this is the exact tarball from TI, it's unchanged.
   The md5s above can be compared against a known-good machine.
6. **Arch's 14.2 arm-none-eabi present at `/usr/bin/`** — installed
   2026-02-02. The habitat build path never invokes this, but its presence
   on `$PATH` is worth noting in case someone overrides `TI_INSTALL_DIR`
   and accidentally picks this up. `which arm-none-eabi-gcc` resolves to
   `/usr/bin/arm-none-eabi-gcc`; any dependent tooling that invokes
   `arm-none-eabi-gcc` without the full `~/ti/…` path will land on 14.2.

## What to diff against, if the last known-good machine is available

- ARM `gcc --version` and `ld --version` string exactly.
- `md5sum` of `~/ti/gcc-arm-none-eabi-4_9-2015q3/bin/arm-none-eabi-gcc`.
- Host `swig -version`, `lua -v`, `gcc --version`.
- The output of `objdump -d testing/am335x/libspreadsheet.so` restricted
  to `FilterResponseGraphic::draw` function to check if the actual emitted
  ARM instructions differ — that's the closest we can get to a byte-level
  comparison without a VM.

## Notes taken during the investigation

- Clean rebuild (`make spreadsheet-clean && make spreadsheet ARCH=am335x`)
  did not change the behavior, so any stale `.o` files are not the cause.
- Habitat source in `mods/spreadsheet/FilterResponseGraphic.h` is
  byte-identical to `v2.0.0` aside from the in-progress diagnostic edits
  (which strip it down to pure `fb.pixel()` calls at a constant radius,
  and the symptom still reproduces).
- Diagnostic path that reproduces on hardware *and* on vanilla firmware:
  72 inline `fb.pixel(WHITE, cx + cosf(a)*fixedR, cy + sinf(a)*fixedR)`
  calls at fixed radius. Should draw a perfect circle; instead shows
  "distended bottom" shape with an extra line-like extension. No `fb.line`
  or `fb.circle` calls involved. If there's a firmware/toolchain
  interaction producing this, it's in the `fb.pixel` path or in the
  trigonometry/cast pipeline.

---

# Comparison snapshot — 2026-04-13 (second machine)

Captured on a different dev machine the same day to compare against the
baseline above. Same repos, same TI ARM toolchain by md5, but several
host-side tools differ. Documented in the same format for direct diff.

## Host machine

| Item                | Value                                                     |
|---------------------|-----------------------------------------------------------|
| Host                | `bram-macbookpro102`                                      |
| OS                  | EndeavourOS (Arch Linux derivative), rolling              |
| Kernel              | `6.19.8-arch1-1`, x86_64                                  |
| Kernel build date   | 2026-03-20 (from `/boot/vmlinuz-linux` mtime)             |

## Host-native toolchain (used for emu + SWIG wrapper + Lua syntax checks)

| Item              | Version                                                     | Source       |
|-------------------|-------------------------------------------------------------|--------------|
| `gcc`             | 15.2.1 (`gcc 15.2.1+r604+g0b99615a8aef-1`)                  | Arch pkg     |
| `make`            | GNU Make 4.4.1 (`make 4.4.1-2`)                             | Arch pkg     |
| `swig`            | **4.4.0** (`swig 4.4.0-2`)                                  | Arch pkg     |
| `lua`             | 5.4.8 (`lua 5.4.8-2`) -- note: `lua` is 5.4 here, not 5.5   | Arch pkg `lua` |
| (alt)             | `lua54` not installed; `luajit` 2.1.1772619647              | Arch pkg     |
| `zip`             | 3.0 (`zip 3.0-11`)                                          | Arch pkg     |
| `python3`         | 3.14.3 (`python 3.14.3-1`)                                  | Arch pkg     |
| `binutils`        | 2.46-1                                                      | Arch pkg     |

## ARM cross toolchain (used for am335x / Cortex-A8)

Same pinned TI 4.9.3 (2015q3) at the same path. md5s and mtime are
**byte-identical** to the baseline machine, so the ARM code generator
itself is not a variable between the two hosts.

| Item                       | Value                                                   |
|----------------------------|---------------------------------------------------------|
| Primary cross GCC          | `~/ti/gcc-arm-none-eabi-4_9-2015q3/bin/arm-none-eabi-gcc` |
| `arm-none-eabi-gcc --version` | 4.9.3 20150529 (release) [ARM/embedded-4_9-branch revision 227977] |
| `arm-none-eabi-ld --version`  | GNU ld (GNU Tools for ARM Embedded Processors) 2.24.0.20150921 |
| Binary mtime               | 2015-09-21 12:59:15                                     |
| `gcc` md5                  | `2eda7a25553c6a56ee738f2fcd111414` (matches baseline)   |
| `g++` md5                  | `721e14dd5fe4ebc6da32a35fc5da96c0` (matches baseline)   |

### Alt ARM toolchain

Not installed on this machine. No `/usr/bin/arm-none-eabi-*` present;
`pacman -Q arm-none-eabi-*` returns not-found for all three
(`gcc`, `binutils`, `newlib`). There is no Arch 14.2 fallback on `$PATH`,
so any accidental bare `arm-none-eabi-gcc` invocation will fail outright
instead of silently picking up a different compiler.

### TI bundle contents

Same list as baseline. All subdirs present under `~/ti/`:

```
bios_6_46_05_55
cg_xml
edma3_lld_2_12_05_29
gcc-arm-none-eabi-4_9-2015q3
ndk_2_25_01_11
pdk_am335x_1_0_8
processor_sdk_rtos_am335x_4_01_00_06
ti-cgt-pru_2.1.5
xdctools_3_32_01_22_core
```

## Repo state at time of snapshot

| Repo                                          | HEAD                                               |
|-----------------------------------------------|----------------------------------------------------|
| `~/repos/er-301-stolmine` (SDK, via symlink)  | `d022c69` (2026-04-08), `v0.7.0-txo.8.6.7-2-gd022c69` |
| `~/repos/er-301-habitat`                      | `0f4e362` (2026-04-13), one commit past `v2.1.0`   |

Habitat SDK symlink: `er-301-habitat/er-301 -> /home/sure/repos/er-301-stolmine`
(same as baseline).

Firmware binary present in `er-301/release/am335x/app/app.bin`:

```
md5  9c357cafdc286042416d00b8aa28a90e
```

SD card not mounted at snapshot time (`/mnt/ER-301/firmware/` absent).

## Deltas vs baseline machine

| Item                     | Baseline (`sure-macbookpro91`)        | This machine (`bram-macbookpro102`)    |
|--------------------------|---------------------------------------|----------------------------------------|
| Kernel                   | 6.18.21-1-lts (2026-04-02)            | 6.19.8-arch1-1 (2026-03-20)            |
| `swig`                   | **4.4.1**                             | **4.4.0**                              |
| `lua` (default binary)   | 5.5.0                                 | 5.4.8                                  |
| `lua54` alt pkg          | present                               | not installed                          |
| `luajit`                 | 2.1.1774896198                        | 2.1.1772619647                         |
| `python3`                | unspecified                           | 3.14.3                                 |
| `binutils` (host)        | (gcc 15.2 default)                    | 2.46-1                                 |
| Arch `arm-none-eabi-*`   | installed (14.2.0, 2026-02-02)        | **not installed**                      |
| SDK HEAD                 | `0df7a03` (v0.7.0-stolmine.9.0.0)     | `d022c69` (v0.7.0-txo.8.6.7-2-gd022c69) |
| Habitat HEAD             | `52b3213` (v2.1.0)                    | `0f4e362` (v2.1.0 + toolchain doc)     |
| `app.bin` md5            | `3a4d9cc883db9a438788510116a70be2`    | `9c357cafdc286042416d00b8aa28a90e`     |
| TI ARM gcc md5           | `2eda7a25...`                         | `2eda7a25...` (match)                  |
| TI ARM g++ md5           | `721e14dd...`                         | `721e14dd...` (match)                  |

Items most likely to affect the Tomograph viz regression if the baseline
machine reproduces it and this one does not (or vice versa):

1. **SWIG 4.4.0 vs 4.4.1.** Generated wrapper C++ is fed to the same TI
   4.9.3 compiler, so any codegen delta between 4.4.0 and 4.4.1 could
   surface as an ARM-only runtime bug. First thing to diff if behavior
   differs between machines.
2. **SDK HEAD differs.** `d022c69` here is on a `txo.8.6.7` tag line;
   the baseline was on `stolmine.9.0.0`. `app.bin` md5 differs, which is
   consistent with either an SDK commit delta or a different build of the
   same source. Worth confirming which SDK branch the known-good build
   came from before attributing the regression to habitat or toolchain.
3. **Lua 5.4 vs 5.5 on host.** Syntax-check only; shouldn't affect the
   ARM `.so`. Non-factor for the rendering regression, noted for
   completeness.
4. **Kernel and Arch `arm-none-eabi` presence.** Neither touches the
   build path given the pinned TI toolchain. Pure environment noise.
