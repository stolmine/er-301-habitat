# RPi Dev Rig — Build, Install, and Emu Procedures

Practical reference for working with the ER-301 emulator on the RPi 4
dev rig from a separate dev workstation. Captures the patterns and
gotchas accumulated through the parallel-DSP MVP work (April 2026).

Companion to:
- [`docs/planning/redesign/16-rt-audio-stack.md`](planning/redesign/16-rt-audio-stack.md) — RT audio stack architecture
- [`docs/planning/redesign/17-rpi-bench-bringup.md`](planning/redesign/17-rpi-bench-bringup.md) — initial bring-up
- [`docs/planning/redesign/18-parallelization.md`](planning/redesign/18-parallelization.md) — parallel-DSP MVP (includes diagnostic findings, xrun mitigations, libmvec linking)
- [`tools/audio-test.sh`](../tools/audio-test.sh) — audio-stack reset script

---

## Rig topology

| Role | Host | User | Repo path | Notes |
|---|---|---|---|---|
| Dev workstation | local | `sure` | `~/repos/er-301-stolmine` | builds locally for x86_64 dev/preview, pushes to GitHub |
| Dev rig | `192.168.1.73` | `ok` | `~/repos/er-301-stolmine` | builds aarch64 emu, runs against HiFiBerry DAC2 ADC Pro |
| Origin | GitHub | — | `git@github.com:stolmine/er-301-stolmine.git` | rpidev branch is the active dev branch |

Both hosts run Linux. SSH key auth from dev → rig (no password prompts).
The rig's repo follows `rpidev` branch and tracks origin/rpidev.

---

## Standard sync workflow

The simplest mental model: **dev pushes via GitHub, rig pulls.** No
direct file transfer between dev and rig — origin is the source of
truth. Avoids sshfs gotchas, parallel-edit collisions, and lets either
host build/run independently.

```bash
# On dev workstation
cd ~/repos/er-301-stolmine
git add -A && git commit -m "..."
git push origin rpidev

# On rig (or via ssh from dev)
ssh ok@192.168.1.73 'cd ~/repos/er-301-stolmine && git pull --ff-only'
```

Use `--ff-only` so a divergence on the rig (manual edits, debug code)
fails loudly instead of silently merging. If divergence is intentional,
commit it on the rig first, push to a debug branch, then switch back.

---

## Building the emu

### On the dev workstation (x86_64)

```bash
cd ~/repos/er-301-stolmine
make emu -j4
```

Output: `testing/linux-x86_64/emu/emu.elf`. Useful for:
- Catching compile errors before pushing to the rig
- Quick smoke-test that the build links (run locally with `./testing/linux-x86_64/emu/emu.elf`)
- Inspecting symbols (`nm -D ./testing/linux-x86_64/emu/emu.elf`)

x86_64 build has full feature parity with aarch64 except:
- HiFiBerry codec hardware obviously absent — defaults to pulseaudio
- Different SIMD path (SSE4 vs Neon) — auto-vectorization output differs but functional behavior matches

### On the rig (aarch64)

```bash
ssh ok@192.168.1.73 'cd ~/repos/er-301-stolmine && make emu -j2'
```

`-j2` not `-j4` — RPi 4 is memory-constrained for the SWIG-glue
compilation step. `-j4` works but risks OOM on full rebuilds. `-j2` is
the safe default; bump to `-j3` if your RPi has 8 GB.

Output: `testing/linux-aarch64/emu/emu.elf`.

### Clean rebuild

If you've changed `scripts/env.mk`, `scripts/emu.mk`, or any other
build-config file — make won't reliably detect the dependency. Force a
clean rebuild:

```bash
ssh ok@192.168.1.73 'cd ~/repos/er-301-stolmine && rm -rf testing/linux-aarch64/emu && make emu -j2'
```

Same locally with `testing/linux-x86_64/emu`.

We learned this the hard way in MVP Phase 7 — flag changes (`symbols
+= BUILDOPT_PARALLEL_DSP`) caused subtle ABI mismatches because some
.o files were stale. Clean rebuild is cheap (~3-5 minutes on RPi 4),
so prefer it when in doubt.

---

## Running the emu

### Quick start (for live testing)

```bash
ssh ok@192.168.1.73 'cd ~/repos/er-301-stolmine && env DISPLAY=:0 SDL_AUDIODRIVER=pipewire PIPEWIRE_LATENCY=128/48000 setsid -f bash -c "exec ./testing/linux-aarch64/emu/emu.elf > /tmp/emu.log 2>&1"'
```

Breakdown of why each env var matters:

| Var | Purpose | If omitted |
|---|---|---|
| `DISPLAY=:0` | SDL window goes to the rig's local display | "could not open display" — emu exits at startup |
| `SDL_AUDIODRIVER=pipewire` | Force pipewire backend (matches our wireplumber config) | SDL falls back to pulseaudio, bypasses our HiFiBerry tuning |
| `PIPEWIRE_LATENCY=128/48000` | Tells pipewire the desired quantum/rate | pipewire uses defaults, may not match emu's frame size |
| `setsid -f` | Detach from SSH session so emu survives connection close | Emu dies when SSH disconnects |
| `> /tmp/emu.log 2>&1` | Capture stdout + stderr to file | Debug output lost |

### Verifying it started

```bash
# Wait briefly then check
sleep 4 && ssh ok@192.168.1.73 'pgrep -a emu.elf; grep -i WorkerPool /tmp/emu.log'
```

Expected:
- `<pid> /home/ok/repos/er-301-stolmine/testing/linux-aarch64/emu/emu.elf`
- `WorkerPool: 2 workers initialized at SCHED_FIFO 88` (if BUILDOPT_PARALLEL_DSP build)

If pgrep returns nothing, the emu crashed at boot — see "Investigating
crashes" below.

### Stopping the emu

```bash
# Graceful (SIGTERM, allows save-on-exit)
ssh ok@192.168.1.73 'pkill -TERM -f emu.elf; true'

# Force (SIGKILL, no save)
ssh ok@192.168.1.73 'pkill -KILL -f emu.elf; true'
```

The trailing `; true` matters — `pkill` returns 1 if no process
matched, and ssh propagates that as an error (exit 255). The `true`
keeps the ssh return clean. If you need to verify the kill landed,
chain a check:

```bash
ssh ok@192.168.1.73 'pkill -TERM -f emu.elf; true'
ssh ok@192.168.1.73 'sleep 2; pgrep emu.elf > /dev/null && echo still || echo stopped'
```

The two-step (separate ssh sessions) is more reliable than chaining
in one — pkill exit codes can interfere with the verification step.

### Restart with a fresh log

The most common pattern during a debug cycle:

```bash
ssh ok@192.168.1.73 'pkill -KILL -f emu.elf; true'
ssh ok@192.168.1.73 'sleep 1; cd ~/repos/er-301-stolmine && env DISPLAY=:0 SDL_AUDIODRIVER=pipewire PIPEWIRE_LATENCY=128/48000 setsid -f bash -c "exec ./testing/linux-aarch64/emu/emu.elf > /tmp/emu.log 2>&1"'
sleep 4
ssh ok@192.168.1.73 'pgrep -a emu.elf; grep -i WorkerPool /tmp/emu.log'
```

---

## Audio configuration

### Three layers must agree

ER-301 audio runs through three independently-configured layers:

1. **emu** — reads `~/.od/emu.config` for `SAMPLERATE` and `FRAMELENGTH`
2. **pipewire** — reads `~/.config/pipewire/pipewire.conf.d/10-lowlatency.conf` for `default.clock.rate` and `default.clock.quantum`
3. **wireplumber → ALSA** — reads `~/.config/wireplumber/wireplumber.conf.d/10-alsa-lowlatency.conf` for the HiFiBerry sink's `api.alsa.period-size` and `node.latency`

**They must all match.** When they don't, pipewire silently does
on-the-fly sample-rate conversion to bridge the gap. Under sustained
load this produces audible artifacts — clicks, crackling, "almost like
xruns but no actual buffer underruns at the kernel level." This was the
root cause of the multi-chain xrun mystery in the parallel-DSP MVP.

### Standard configurations

| Use case | Sample rate | Frame length | Frame budget |
|---|---|---|---|
| Default / safe baseline | 48000 | 128 | 2.67 ms |
| Tight buffer | 48000 | 64 | 1.33 ms |
| High rate | 96000 | 64 | 0.67 ms |
| Extreme (full RT lockdown only) | 96000 | 32 | 0.33 ms |

Build defaults are 48 k/128, which match the HiFiBerry hardware default
— this is the safest combo when in doubt.

### Setting all three layers

The script [`tools/audio-test.sh`](../tools/audio-test.sh) is the
intended way:

```bash
ssh ok@192.168.1.73 'bash ~/repos/er-301-stolmine/tools/audio-test.sh 48000 128'
```

It writes all three configs in lockstep, restarts pipewire/wireplumber,
kills and relaunches the emu, and prints a verification block showing
the negotiated rate/quantum at each layer.

**Known limitation**: the script's write to `~/.od/emu.config` doesn't
always propagate to the running emu. Manual edits to `~/.od/emu.config`
work; the script's writes don't. Workaround: set `~/.od/emu.config` by
hand once, leave it, drive only pipewire/wireplumber via the script.
Or just accept that emu may run at the build's default (48 k/128)
regardless of what the script writes — match your script args to that
to keep things consistent.

### Manual emu.config edit

```bash
ssh ok@192.168.1.73 'sed -i "s/^SAMPLERATE.*/SAMPLERATE 48000/; s/^FRAMELENGTH.*/FRAMELENGTH 128/" ~/.od/emu.config'
```

Or just edit it interactively via SSH.

### Verifying all layers actually agree

After setting up, restart the emu and check:

```bash
ssh ok@192.168.1.73 '
  echo === emu.config ===
  grep -E "^SAMPLERATE|^FRAMELENGTH" ~/.od/emu.config
  echo === pipewire ===
  pw-metadata -n settings 2>/dev/null | grep -E "clock.rate|clock.quantum"
  echo === ALSA HiFiBerry ===
  for f in /proc/asound/card*/pcm*p/sub0/hw_params; do
    if grep -q "RUNNING\|SETUP" "${f%hw_params}status" 2>/dev/null; then
      echo "$f"; cat "$f" | grep -E "rate|period_size|buffer_size"
    fi
  done
  echo === emu boot log ===
  grep -E "SAMPLERATE|FRAMELENGTH|Audio Specs" /tmp/emu.log | head -5
'
```

All three should show the same rate. If they don't, you'll get
resampling artifacts under load.

---

## Diagnostics

### Logs

Multiple log destinations exist; know which one to check:

| Path | Contents | Written by |
|---|---|---|
| `/tmp/emu.log` | Full emu stdout/stderr | Direct nohup launches |
| `/tmp/emu-test.log` | Same, but written by the audio-test.sh wrapper | `tools/audio-test.sh` |
| `~/.od/front/ER-301/logs/<package>.log` | **Per-package error reports** with full stack traceback | The engine itself, on unit-load failures |
| `dmesg` | Kernel-level audio errors (real ALSA xruns) | Kernel |
| `journalctl --user -u pipewire` | Pipewire-side issues | systemd user session |

**The per-package error log is the canonical place for unit-load
failures** — separate from stdout/stderr. When a unit fails to
instantiate, the engine writes a structured ERROR REPORT to
`~/.od/front/ER-301/logs/<package>.log`, including the firmware
version, time-since-boot, error message, full Lua stack traceback, and
recent log messages. Always check there first when a unit silently
fails to load.

```bash
# Find recent unit-load errors across all packages
ssh ok@192.168.1.73 'ls -lat ~/.od/front/ER-301/logs/'
ssh ok@192.168.1.73 'tac ~/.od/front/ER-301/logs/<package>.log | sed "/ERROR REPORT BEGIN/q" | tac | head -25'
```

### CPU and thermal under load

```bash
ssh ok@192.168.1.73 '
  PID=$(pgrep -x emu.elf | head -1)
  echo === thermal ===
  vcgencmd measure_temp
  vcgencmd get_throttled  # 0x80000 = soft thermal hit historically; 0x4 = currently throttling
  echo === CPU freq ===
  for c in 0 1 2 3; do
    echo -n "cpu$c: "; cat /sys/devices/system/cpu/cpu$c/cpufreq/scaling_cur_freq
  done
  echo === threads ===
  ps -L -o tid,pri,rtprio,policy,psr,pcpu,comm -p $PID | head -20
'
```

What to look for:
- Workers should be at SCHED_FIFO 88 (POL=FF, PRI=128, RTPRIO=88) on CPUs 1 and 3
- SDL audio coordinator (`SDLAudioP*`) at SCHED_FIFO 88 on CPU 2
- Temp under 80°C; freq at max (1500000 = 1.5 GHz default, may show 1.8 GHz if overclocked)
- `get_throttled` = 0 means clean; 0x4 means actively throttling now; 0x80000 means soft thermal hit at some point since boot

### Audio path health

```bash
ssh ok@192.168.1.73 '
  echo === ALSA HiFiBerry status ===
  cat /proc/asound/card2/pcm0p/sub0/status
  echo === pipewire xrun stats ===
  timeout 5 pw-top -b -n 1 2>&1 | head -20
  echo === kernel xruns ===
  dmesg | grep -iE "xrun|underrun|overrun" | tail -10
'
```

What to look for:
- ALSA `state: RUNNING` — good
- pw-top should show no ERR > 0 on the audio nodes
- dmesg empty for xrun lines — kernel sees no underruns

If audible glitches appear but `dmesg` is empty AND ALSA shows no
xruns, the engine is fine — the artifact is in the pipewire/ALSA
path. Most likely cause: sample-rate mismatch between layers (see
"Audio configuration" above).

### Investigating crashes

If the emu segfaults at boot, the SSH session sees:

```
bash: line 1: <pid> Segmentation fault (core dumped)
```

Capture exit code (139 = SIGSEGV, 134 = SIGABRT, 124 = timeout SIGTERM):

```bash
ssh ok@192.168.1.73 '
  cd ~/repos/er-301-stolmine
  timeout 8 ./testing/linux-aarch64/emu/emu.elf 2>&1 | tail -10
  echo "exit=$?"
'
```

For a real backtrace, run under gdb from the repo dir:

```bash
ssh ok@192.168.1.73 '
  cd ~/repos/er-301-stolmine
  gdb --batch \
      -ex "run" \
      -ex "bt 30" \
      -ex "info threads" \
      -ex "thread apply all bt 12" \
      ./testing/linux-aarch64/emu/emu.elf 2>&1
'
```

Caveat: gdb changes timing, so race-induced crashes may not reproduce
under it. Heisenbug-style.

For the cases where gdb hides the crash, install a SIGSEGV handler
directly in `emu/emu.cpp` that prints the faulting PC, LR, SP, and
calls `backtrace_symbols_fd()`. Pattern at the bottom of this doc.

---

## Worked examples

### Full workflow: edit → push → rebuild rig → boot test → drive UI

```bash
# 1. Edit on dev workstation
cd ~/repos/er-301-stolmine
$EDITOR od/tasks/WorkerPool.cpp
make emu -j4 && echo "local build clean"

# 2. Commit and push
git add -u
git commit -m "..."
git push origin rpidev

# 3. Build on rig
ssh ok@192.168.1.73 'cd ~/repos/er-301-stolmine && git pull --ff-only && make emu -j2 2>&1 | tail -5'

# 4. Boot test (5 runs, check for clean exits)
ssh ok@192.168.1.73 '
  for i in 1 2 3 4 5; do
    out=$(timeout 8 ./testing/linux-aarch64/emu/emu.elf 2>&1)
    echo "run $i: exit=$? graceful=$(echo "$out" | grep -c Exiting)"
  done
'

# 5. Spin up for live driving
ssh ok@192.168.1.73 '
  pkill -KILL -f emu.elf; true
  sleep 1
  cd ~/repos/er-301-stolmine
  env DISPLAY=:0 SDL_AUDIODRIVER=pipewire PIPEWIRE_LATENCY=128/48000 \
      setsid -f bash -c "exec ./testing/linux-aarch64/emu/emu.elf > /tmp/emu.log 2>&1"
'
sleep 4
ssh ok@192.168.1.73 'pgrep -a emu.elf; grep -i WorkerPool /tmp/emu.log'
# Now interact with the rig directly via its display + keyboard
```

### Reset audio stack to known-good and restart

```bash
ssh ok@192.168.1.73 'bash ~/repos/er-301-stolmine/tools/audio-test.sh 48000 128 2>&1 | tail -20'
```

The script handles everything: writes configs, restarts pipewire stack,
kills+relaunches emu, prints verification.

### Heavy-patch xrun investigation

```bash
# With emu running and a heavy patch loaded, capture state
ssh ok@192.168.1.73 '
  PID=$(pgrep -x emu.elf | head -1)
  echo "=== thermal ===";        vcgencmd measure_temp; vcgencmd get_throttled
  echo "=== CPU freq ===";       for c in 0 1 2 3; do echo -n "cpu$c: "; cat /sys/devices/system/cpu/cpu$c/cpufreq/scaling_cur_freq; done
  echo "=== threads ===";        ps -L -o tid,pri,rtprio,policy,psr,pcpu,comm -p $PID | head -20
  echo "=== ALSA status ===";    cat /proc/asound/card2/pcm0p/sub0/status
  echo "=== kernel xruns ===";   dmesg | grep -iE "xrun|underrun|overrun" | tail -5
  echo "=== package errors ==="; ls -lat ~/.od/front/ER-301/logs/ | head -5
'
```

Decision tree from the readout:
- Engine wallclock < 100% AND ALSA state RUNNING AND no kernel xruns → the audible artifact is dev-rig audio path (sample-rate mismatch most likely)
- Workers near 100% AND coordinator near 100% → genuine engine saturation; consider larger frame size
- Temp at or near 80°C OR `get_throttled` showing 0x4 → thermal throttling is killing performance; cool the RPi or reduce load
- Per-package error logs show recent timestamps → unit-load failure in flight

---

## Snapshot pattern: SIGSEGV handler in emu.cpp

When you need to capture a real backtrace and gdb won't reproduce the
crash, drop this into `emu/emu.cpp` temporarily (revert before
shipping):

```cpp
#include <execinfo.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdio.h>

static void debug_segv_handler(int sig, siginfo_t *info, void *uctx) {
  ucontext_t *uc = (ucontext_t *)uctx;
  char buf[256];
  int n = snprintf(buf, sizeof(buf),
                   "\n=== SIGSEGV ===\n  fault_addr=%p\n",
                   info ? info->si_addr : (void *)0);
  write(2, buf, n);
#if defined(__aarch64__)
  unsigned long pc = uc->uc_mcontext.pc;
  unsigned long lr = uc->uc_mcontext.regs[30];
  n = snprintf(buf, sizeof(buf), "  pc=0x%lx\n  lr=0x%lx\n", pc, lr);
  write(2, buf, n);
#endif
  void *frames[40];
  int nf = backtrace(frames, 40);
  static const char hdr[] = "  --- backtrace ---\n";
  write(2, hdr, sizeof(hdr) - 1);
  backtrace_symbols_fd(frames, nf, 2);
  signal(sig, SIG_DFL);
  raise(sig);
}

// In main():
//   struct sigaction sa{};
//   sa.sa_sigaction = debug_segv_handler;
//   sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
//   sigemptyset(&sa.sa_mask);
//   sigaction(SIGSEGV, &sa, nullptr);
//   sigaction(SIGBUS, &sa, nullptr);
```

After capturing the PC, resolve it with addr2line:

```bash
ssh ok@192.168.1.73 'addr2line -fpe ~/repos/er-301-stolmine/testing/linux-aarch64/emu/emu.elf 0x<pc-value>'
```

This pattern caught the Phase 7 vtable-load segfault — PC resolved to
inside libstdc++ typeinfo, which immediately pointed at the package
ABI break (vanilla packages inheriting `od::Task` had vtables without
the new virtual slot).

---

## Common pitfalls reference

| Symptom | Cause | Fix |
|---|---|---|
| Emu segfaults at boot, but boots fine under gdb | Race-condition crash; gdb timing hides it | Install in-process SIGSEGV handler (above), check fault PC |
| Emu doesn't start, no error in log | DISPLAY not set when launching via ssh | Add `env DISPLAY=:0` to the launch command |
| Audio plays but with constant clicks/crackling | Sample-rate mismatch between emu, pipewire, ALSA | Run `audio-test.sh 48000 128` to normalize all three |
| Unit fails to instantiate silently | Per-package log has the real error | `tail ~/.od/front/ER-301/logs/<pkg>.log` |
| `pkill` returns nonzero, ssh fails with exit 255 | pkill exits 1 when no process matches | Add `; true` after pkill |
| Stdout buffered, no log output during operation | emu uses default stdio buffering | Live log only flushes at exit; restart and capture with `> /tmp/emu.log 2>&1` if unbuffered output needed, prepend with `stdbuf -oL` |
| Build artifacts stale after env.mk change | make doesn't track env.mk dep on every .o | `rm -rf testing/linux-aarch64/emu` then rebuild |
| Workers spawn but all dispatch goes to coordinator | `BUILDOPT_PARALLEL_DSP` not set in env.mk | Check `grep BUILDOPT_PARALLEL_DSP scripts/env.mk` |
| Package fails to dlopen with `undefined symbol: _ZGV*` | Package was built with auto-vectorized math; emu wasn't linked with libmvec | Already fixed (commit `67b02a4`) — `-Wl,--no-as-needed -lmvec` in Linux LFLAGS |

---

## Cross-reference

- The full architectural context for the parallel-DSP work lives in
  [`docs/planning/redesign/18-parallelization.md`](planning/redesign/18-parallelization.md)
  including the diagnostic-finding pattern (what to look at when a
  patch xruns), xrun mitigation strategies, and the libmvec linking
  decision.
- Unit-author guidance for taking advantage of parallelism is in
  [`docs/planning/redesign/19-unit-authoring-parallelism.md`](planning/redesign/19-unit-authoring-parallelism.md).
- ABI compatibility rules (vtable, struct layout, runtime symbols)
  live in `~/.claude/projects/<...>/memory/project_abi_compatibility.md`
  on the dev workstation; mirror to repo if the team grows beyond
  one author.
