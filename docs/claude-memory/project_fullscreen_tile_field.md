---
name: Full-screen tile field graphic effect
description: Colmatage BSP tile field accidentally rendered across the full 128x64 display surface instead of the ply — looked striking. Worth revisiting for a screensaver, scope-style unit, or full-width overview graphic.
type: project
originSessionId: b5f87646-88b8-4a6d-8557-2224c6b447ce
---
During Colmatage viz development, the 2x2 tile field (BSP influence map + Perlin noise breathing + phrase progress diagonal sweep) was accidentally rendered at 128x64 (full screen) instead of the 42x64 ply bounds. The effect was visually compelling — organic brightness clusters forming and dissolving across the entire unit surface.

**Why:** Grid dimensions were hardcoded to 64x32 tiles (128x64 px) instead of being derived from the widget's actual width.

**How to apply:** Use a full-width graphic (128x64) as the control graphic for an overview ply, or render the tile field as a background behind multiple plies. The BSP + tile field system at full resolution (64x32 tiles) is cheap enough for real-time (~8K pixel writes per frame).

**Candidates:** Screensaver mode, Scope-style passthrough viz, any unit that wants a full-width animated overview (Rauschen phase space, Petrichor tap landscape, future generative viz units).
