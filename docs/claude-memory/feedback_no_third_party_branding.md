---
name: No third-party branding in unit descriptions
description: When porting or adapting external drum/synth voices, never reference source product or company names (Trinity, Modbap, BeatPPL, Mutable Instruments product names outside the MI ports themselves, etc.) in todo entries, planning docs, or unit-facing text. Use generic descriptive names.
type: feedback
originSessionId: 0063d5be-4d30-4c1e-a7b3-e687f2cae19d
---
When porting a unit concept from an external source (drum voice, synth engine, effect), refer to it by a **generic functional name** (e.g. "Analog Macro Drum Voice" instead of "Trinity Block", "Modbap Block", "BeatPPL Block"). Do not mention the source product, company, or brand in:

- todo.md entries
- planning/ docs
- READMEs, release notes, commit messages
- anything user-facing

**Why:** the user does not want shipped work to read as a clone of a specific commercial product. Generic naming keeps descriptions about the sound/function and avoids implicit attribution or comparison that could read as derivative.

**How to apply:** when a user hands over a manual, demo, or spec from an external product and asks for a unit based on it, extract the *behavior* and rename. Mention the source only in private reference trails if strictly needed for implementation (e.g. "see original manual pp.X for signal flow") -- and even then prefer a path to a local file over a product name. When in doubt, ask for the generic name the user wants.

Distinct from the MI-ports rule (which already ships under Mutable Instruments product names like Plaits/Clouds) -- that's a separate, explicitly-credited lineage. This rule covers *new* ports where no such precedent exists.
