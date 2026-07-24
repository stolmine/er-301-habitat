-- Fade Mixer (4 inputs). Built from the shared FadeMixerFactory; unit-local
-- mute/solo across its own inputs (fixed the previously non-functional buttons).
local factory = require "biome.FadeMixerFactory"
return factory(4, "Fade Mixer", "FM")
