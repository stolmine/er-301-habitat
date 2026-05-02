#!/usr/bin/env bash
# Scan package C++ for out-of-line virtual function definitions on classes
# that subclass framework types (od::Graphic, od::Object, etc.).
#
# Background: this anti-pattern triggers GCC's "key function" rule, which
# emits the class's vtable in non-COMDAT linkage in a single .o. On
# Cortex-A8 + gcc 4.9.3, that vtable layout mis-resolves across firmware
# rebuilds and hard-faults on virtual dispatch from firmware-compiled
# code into the package-allocated instance (e.g. addChild, attach).
# DrumCubeGraphic ate this bug repeatedly until the inline-only fix.
#
# Rule: any class that subclasses a framework type must have ALL virtual
# overrides defined inline in its header. No .cpp file with method bodies.
#
# See memory: feedback_no_out_of_line_virtuals.md
#
# Usage: tools/check-graphic-virtual-defs.sh [path...]
# Exit code: 0 on clean scan, non-zero on any hit.
#
# CI hook: add this to pre-release / pre-merge checks.

set -euo pipefail

paths=("${@:-mods}")
hits=0

# Pattern: any line starting with `<type> Class::method(` that's a virtual
# override candidate. We catch the common framework virtual names.
virtual_methods=(
  "draw"
  "notifyVisible"
  "notifyHidden"
  "notifyContentsChanged"
  "setSize"
  "setPosition"
)

# Build alternation regex: \b(draw|notifyVisible|...)\b
methods_re="\\b("$(IFS='|'; echo "${virtual_methods[*]}")")\\b"

for path in "${paths[@]}"; do
  if [[ ! -e "$path" ]]; then
    echo "warn: $path does not exist, skipping" >&2
    continue
  fi

  while IFS= read -r -d '' f; do
    # Match: optional return-type + ClassName::virtualName( opening paren
    # Examples that flag:
    #   void DrumCubeGraphic::draw(od::FrameBuffer &fb) {
    #   void AlembicSphereGraphic::draw(od::FrameBuffer &fb)
    matches=$(grep -nE "^[[:space:]]*[A-Za-z_][A-Za-z0-9_:&* ]*[[:space:]]+[A-Za-z_][A-Za-z0-9_]*::${methods_re}[[:space:]]*\\(" "$f" 2>/dev/null || true)
    if [[ -n "$matches" ]]; then
      while IFS= read -r line; do
        echo "FAIL  $f:$line"
        hits=$((hits + 1))
      done <<< "$matches"
    fi
  done < <(find "$path" -type f \( -name '*.cpp' -o -name '*.cc' \) -print0)
done

if [[ $hits -gt 0 ]]; then
  cat <<'EOF' >&2

Out-of-line virtual function definitions on package classes are an ABI-
fragility anti-pattern. They will hard-fault on hardware after firmware
rebuilds. Move all virtual overrides inline into the header.

See ~/.claude/projects/-home-sure-repos-er-301-habitat/memory/feedback_no_out_of_line_virtuals.md
EOF
  exit 1
fi

echo "OK: no out-of-line virtuals found in: ${paths[*]}"
exit 0
