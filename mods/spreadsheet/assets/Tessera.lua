-- Tessera - analog-style "building block" drum voice.
-- Built from the profiled Modbap Trinity BLOCK algorithm
-- (~/repos/trinity-midi-harness/findings-block.md). Triangle->sine->fold core
-- (Character), 2nd-oscillator overlay (Shape), noise + env-shortening (Grit),
-- pitch env (Sweep depth / Time rate), hold + decay amp env.
local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Pitch = require "Unit.ViewControl.Pitch"
local Encoder = require "Encoder"

local Tessera = Class {}
Tessera:include(Unit)

function Tessera:init(args)
  args.title = "Tessera"
  args.mnemonic = "Te"
  Unit.init(self, args)
end

local PARAMS = { "pitch", "character", "shape", "grit", "sweep", "time", "hold", "decay", "level" }
local PARAMNAME = { pitch = "Pitch", character = "Character", shape = "Shape", grit = "Grit",
                    sweep = "Sweep", time = "Time", hold = "Hold", decay = "Decay", level = "Level" }
local DEFAULT = { pitch = 0.4, character = 0.2, shape = 0.0, grit = 0.0,
                  sweep = 0.3, time = 0.3, hold = 0.1, decay = 0.4, level = 0.8 }

function Tessera:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.Tessera())

  local trig = self:addObject("trig", app.Comparator())
  trig:setTriggerMode()
  connect(trig, "Out", op, "Trigger")
  self:addMonoBranch("trig", trig, "In", trig, "Out")

  local tune = self:addObject("tune", app.ConstantOffset())
  local tuneRange = self:addObject("tuneRange", app.MinMax())
  connect(tune, "Out", tuneRange, "In")
  connect(tune, "Out", op, "V/Oct")
  self:addMonoBranch("tune", tune, "In", tune, "Out")

  for _, k in ipairs(PARAMS) do
    local o = self:addObject(k, app.ParameterAdapter())
    o:hardSet("Bias", DEFAULT[k])
    tie(op, PARAMNAME[k], o, "Out")
    self:addMonoBranch(k, o, "In", o, "Out")
  end

  connect(op, "Out", self, "Out1")
  if channelCount > 1 then
    connect(op, "Out", self, "Out2")
  end
end

function Tessera:onLoadViews()
  local views = {
    trig = Gate {
      button = "trig",
      description = "Trigger",
      branch = self.branches.trig,
      comparator = self.objects.trig
    },
    tune = Pitch {
      button = "V/oct",
      description = "V/oct",
      branch = self.branches.tune,
      offset = self.objects.tune,
      range = self.objects.tuneRange
    }
  }
  for _, k in ipairs(PARAMS) do
    views[k] = GainBias {
      button = k,
      description = PARAMNAME[k],
      branch = self.branches[k],
      gainbias = self.objects[k],
      range = self.objects[k],
      biasMap = Encoder.getMap("[0,1]"),
      biasPrecision = 2,
      initialBias = DEFAULT[k]
    }
  end
  return views, {
    expanded = { "trig", "tune", "pitch", "character", "shape", "grit", "sweep", "time", "hold", "decay", "level" },
    collapsed = {}
  }
end

return Tessera
