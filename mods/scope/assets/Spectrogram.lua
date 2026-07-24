-- Spectrogram (2-ply). Base width; the 3/4/6-ply variants render the same spectrum
-- across a wider display for more horizontal detail.
local factory = require "scope.SpectrogramFactory"
return factory(2, "Spectrogram", "Sg")
