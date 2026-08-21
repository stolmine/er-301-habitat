return {
  title = "House",
  author = "stolmine",
  name = "house",
  keyword = "reverb, room, hall, space, ambience, spatial",
  units = {
    { title = "kWoodRoom", moduleName = "KWoodRoom", category = "House", keywords = "reverb, room, wood, small, kwoodroom, airwindows" },
    { title = "WoodenBox", moduleName = "WoodenBox", category = "House", keywords = "reverb, room, wood, tone, box, woodenbox, airwindows" },
    { title = "CreamCoat", moduleName = "CreamCoat", category = "House", keywords = "reverb, ambience, bright, cream, lush, derez, undersample, predelay, creamcoat, airwindows" },
    { title = "BrightAmbience3", moduleName = "BrightAmbience3", category = "House", keywords = "reverb, ambience, bright, halo, gated, sparse, prime, brightambience3, brightambience, airwindows" },
    { title = "Verbity", moduleName = "Verbity", category = "House", keywords = "reverb, hall, plate, fdn, thunder, submix, verbity, airwindows" },
    { title = "Galactic", moduleName = "Galactic", category = "House", keywords = "reverb, hall, lush, big, modulated, vibrato, detune, predelay, galactic, airwindows" },
    -- XYZ parked 2026-06-05: too sin-heavy for Cortex-A8.
    -- Header + Lua + plan preserved on disk; revive when rpi
    -- successor lands.
    { title = "TickerTape", moduleName = "TickerTape", category = "House", keywords = "tape, saturate, console, drive, lo-fi, color, character, glue, tickertape" },
    { title = "Lacquer", moduleName = "Lacquer", category = "House", keywords = "lacquer, vinyl, tape, saturate, lo-fi, polished, downsample, oversample, cut, polish, character" },
    { title = "Parametric EQ", moduleName = "ParametricEq", category = "House", keywords = "eq, equalizer, parametric, filter, shelf, bell, band, tone, q, ssl, mixing" },
    { title = "Channel Strip", moduleName = "ChannelStrip", category = "House", keywords = "channel, strip, console, compressor, gate, eq, dynamics, mixing, bus" },
  }
}
