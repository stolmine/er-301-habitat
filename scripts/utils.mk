# A recursive version of the wildcard function.
# $(call rwildcard,directory,pattern)
export rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

# Post-link NEON alignment-hint lint (am335x only; advisory, never fails build).
# Scans the linked .so for vld1/vst1 ops with `:64`/`:128` alignment hints
# that Cortex-A8 can trap on (per feedback_neon_intrinsics_drumvoice and
# feedback_neon_hint_surfaces). Prints a one-line "clean" summary or a
# per-symbol report of suspects.
# Usage in mod.mk's LIB_FILE recipe: $(call neon_hint_check,$@)
neon_hint_check = @if [ "$(ARCH)" = "am335x" ] && [ -x tools/check-neon-hints.sh ]; then tools/check-neon-hints.sh $(1) || true; fi