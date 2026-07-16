-- Plenum -- a feedback-delay-network reverb (working name). Phase 1
-- scaffold: an 8-line Householder FDN with per-line HF damping and a
-- Schroeder input diffuser, wrapped for audition. Internal-stereo (the
-- shared tank + decorrelated L/R taps live in the C++ atom FDNTank), so
-- the Lua wiring just maps In1/In2 -> In L/In R and Out L/Out R ->
-- Out1/Out2 (the Fabula pattern).
--
-- Four controls for now: Size (room), Decay (RT60), Damp (dark tail),
-- Mix (equal-power dry/wet). Diffusion / predelay / spectral flavor and
-- the NEON pass are later phases -- see planning/fdn-reverb-design.md.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local GainBias = require "Unit.ViewControl.GainBias"

-- Normalized 0..1 map with the house coarse step (0.01), per
-- feedback_control_step_standards.
local function normMap(min, max)
  local map = app.LinearDialMap(min, max)
  map:setSteps(0.1, 0.01, 0.001, 0.0001)
  return map
end

local zeroOneMap = normMap(0, 1)

local Plenum = Class {}
Plenum:include(Unit)

function Plenum:init(args)
  args.title = "Plenum"
  args.mnemonic = "Pl"
  Unit.init(self, args)
end

function Plenum:onLoadGraph(channelCount)
  local op = self:addObject("op", libspreadsheet.FDNTank())

  connect(self, "In1", op, "In L")
  connect(op, "Out L", self, "Out1")
  if channelCount > 1 then
    connect(self, "In2", op, "In R")
    connect(op, "Out R", self, "Out2")
  end

  local size = self:addObject("size", app.ParameterAdapter())
  size:hardSet("Bias", 0.5)
  tie(op, "Size", size, "Out")
  self:addMonoBranch("size", size, "In", size, "Out")

  local decay = self:addObject("decay", app.ParameterAdapter())
  decay:hardSet("Bias", 0.6)
  tie(op, "Decay", decay, "Out")
  self:addMonoBranch("decay", decay, "In", decay, "Out")

  local damp = self:addObject("damp", app.ParameterAdapter())
  damp:hardSet("Bias", 0.3)
  tie(op, "Damp", damp, "Out")
  self:addMonoBranch("damp", damp, "In", damp, "Out")

  local mix = self:addObject("mix", app.ParameterAdapter())
  mix:hardSet("Bias", 0.35)
  tie(op, "Mix", mix, "Out")
  self:addMonoBranch("mix", mix, "In", mix, "Out")
end

function Plenum:onLoadViews()
  return {
    size = GainBias {
      button = "size",
      description = "Size",
      branch = self.branches.size,
      gainbias = self.objects.size,
      range = self.objects.size,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.5
    },
    decay = GainBias {
      button = "dcay",
      description = "Decay",
      branch = self.branches.decay,
      gainbias = self.objects.decay,
      range = self.objects.decay,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.6
    },
    damp = GainBias {
      button = "damp",
      description = "Damp",
      branch = self.branches.damp,
      gainbias = self.objects.damp,
      range = self.objects.damp,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.3
    },
    mix = GainBias {
      button = "mix",
      description = "Dry/Wet",
      branch = self.branches.mix,
      gainbias = self.objects.mix,
      range = self.objects.mix,
      biasMap = zeroOneMap,
      biasUnits = app.unitNone,
      biasPrecision = 2,
      initialBias = 0.35
    }
  }, {
    expanded = { "size", "decay", "damp", "mix" },
    collapsed = {}
  }
end

return Plenum
