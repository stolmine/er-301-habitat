return {
  title = "Hills",
  author = "Émilie Gillet & Tim Churches / ER-301 port by stolmine",
  name = "peaks",
  keyword = "drums, modulation, envelope, sequencer",
  units = {
    -- Peaks
    { title = "Tap LFO",       moduleName = "TapLfo",       category = "Hills", keywords = "lfo, modulation, sync" },
    { title = "Bass Drum",     moduleName = "BassDrum",     category = "Hills", keywords = "drum, percussion, 808" },
    { title = "Snare Drum",    moduleName = "SnareDrum",    category = "Hills", keywords = "drum, percussion, 808" },
    { title = "High Hat",      moduleName = "HighHat",      category = "Hills", keywords = "drum, percussion, 808" },
    { title = "FM Drum",       moduleName = "FmDrum",       category = "Hills", keywords = "drum, percussion, fm" },
    { title = "Bouncing Ball", moduleName = "BouncingBall", category = "Hills", keywords = "envelope, modulation, physics" },
    { title = "Mini Sequencer",moduleName = "MiniSequencer",category = "Hills", keywords = "sequencer, cv" },
    { title = "Number Station",moduleName = "NumberStation",category = "Hills", keywords = "noise, generator, weird" },
    -- Dead Man's Catch
    { title = "Randomised Envelope", moduleName = "RandomisedEnvelope", category = "Dead Man's Catch", keywords = "envelope, random, modulation" },
    { title = "Mod Sequencer",       moduleName = "ModSequencer",       category = "Dead Man's Catch", keywords = "sequencer, cv, modulation" },
    { title = "FM LFO",              moduleName = "FmLfo",              category = "Dead Man's Catch", keywords = "lfo, fm, modulation" },
    { title = "WSM LFO",             moduleName = "WsmLfo",             category = "Dead Man's Catch", keywords = "lfo, waveshape, modulation" },
    { title = "PLO",                 moduleName = "Plo",                category = "Dead Man's Catch", keywords = "oscillator, pll, sync" },
    { title = "ByteBeats",           moduleName = "ByteBeats",          category = "Dead Man's Catch", keywords = "bytebeat, noise, generator" }
  }
}
