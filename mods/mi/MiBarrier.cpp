// Diagnostic-only: an external no-op function used to force GCC to
// honor the full ARM AAPCS calling convention at the call site —
// in particular spilling all caller-saved NEON registers (Q0-Q3,
// Q8-Q15) to stack and reloading after. The function itself does
// nothing. The intent is to isolate "function call ABI side effects"
// from "entering firmware-resident code" as the masking mechanism
// for the mi engine-switch crash bug.

extern "C" __attribute__((noinline)) void mi_barrier_noop() {
  // intentionally empty
}
