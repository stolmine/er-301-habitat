---
name: unitOutputNames table caps multi-out fan-out; baked into firmware
description: For multi-out units with channelCount > N where N is the table's cap, the xroot Lua table must extend that high — and since xroot is baked into the emu/hardware firmware at build time, fw must be rebuilt to pick up extensions.
type: feedback
originSessionId: bd738562-37e5-4b2a-9bdc-85a9feb561af
---
`xroot/boot/globals-setup.lua`'s `unitOutputNames` table maps Lua-side connect names ("Out1", "Out2", ...) to C++ pUnit channel indices. **Lua's `connect(obj, port, self, "OutN")` fails silently to wire when "OutN" isn't in the table** — the unit gets the silent-output bug, with the symptom that downstream consumers of the missed-mapping outlet see no audio.

**Why:** xroot is BAKED INTO firmware at build time (both linux emu and am335x hardware). Changing the Lua table on disk does NOT propagate to a running emu/firmware unless the emu/fw binary is rebuilt. This bit during JF Phase 3/4 development: voices 3N..6N (sub-outs with framework slot names Out5..Out8) silently produced no audio because the running emu was built before the table extended past Out4.

**How to apply:**
- For any new multi-out unit with `channelCount > N`, ensure `unitOutputNames` covers up to "Out{N}".
- Stolmine fw post-2026-04-30: table is auto-generated for Out1..Out99 (architectural ceiling per the multi-out spec — limit of the picker's X/Y indicator at 2 digits per side). All future habitat/stolmine unit authors are covered without touching the table.
- For testing on am335x hardware: any unit with channelCount > 4 requires firmware rebuilt with the extended (or now loop-generated) table baked in.
- For testing on linux emu: same — emu binary must be rebuilt to pick up xroot table extensions.
- Symptom signature: the unit declares N outlets, picker shows N labels in M6 cycle, picker reports correct `Source.Internal{channel=N}` on subscription, but downstream consumer's miniscope shows no audio for sub-outs with framework slot names beyond the table's cap. Other (within-cap) sub-outs work normally.
