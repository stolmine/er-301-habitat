-- Alembic (AlembicVoice): native 4-op phase-mod matrix voice.
-- A/B-equivalent to AlembicRef; see mods/spreadsheet/AlembicVoice.h for
-- the architecture notes. This Lua wiring is intentionally parallel to
-- AlembicRef.lua's view/menu layout so tester muscle memory transfers.

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Task = require "Unit.MenuControl.Task"
local MenuHeader = require "Unit.MenuControl.Header"
local Encoder = require "Encoder"

local AlembicVoice = Class{}
AlembicVoice:include(Unit)

function AlembicVoice:init(args)
    args.title = "Alembic"
    args.mnemonic = "Al"
    Unit.init(self, args)
end

function AlembicVoice:onLoadGraph(channelCount)
    local op = self:addObject("op", libspreadsheet.AlembicVoice())

    -- V/Oct: ConstantOffset directly to op.V/Oct. AlembicVoice applies
    -- the FULLSCALE_IN_VOLTS*ln2 scaling internally, matching libcore
    -- SineOscillator's convention (no external 10x ConstantGain).
    local tune = self:addObject("tune", app.ConstantOffset())
    local tuneRange = self:addObject("tuneRange", app.MinMax())
    connect(tune, "Out", op, "V/Oct")
    connect(tune, "Out", tuneRange, "In")
    self:addMonoBranch("tune", tune, "In", tune, "Out")

    -- Sync: Comparator in trigger mode feeds op.Sync; op detects the
    -- rising edge with a 0.5f threshold in process().
    local sync = self:addObject("sync", app.Comparator())
    sync:setTriggerMode()
    connect(sync, "Out", op, "Sync")
    self:addMonoBranch("sync", sync, "In", sync, "Out")

    -- Output (mono-dup stereo, matching AlembicRef).
    connect(op, "Out", self, "Out1")
    if channelCount > 1 then
        connect(op, "Out", self, "Out2")
    end

    -- ParameterAdapter helper. Each adapter exposes a Parameter as a
    -- CV-modulatable ply control via addMonoBranch.
    local function adapter(name, paramName, initial)
        local a = self:addObject(name, app.ParameterAdapter())
        a:hardSet("Bias", initial)
        tie(op, paramName, a, "Out")
        self:addMonoBranch(name, a, "In", a, "Out")
        return a
    end

    adapter("f0", "F0", 27.5)
    adapter("level", "Level", 0.5)

    local opNames = {"A", "B", "C", "D"}
    for _, n in ipairs(opNames) do
        adapter("ratio" .. n, "Ratio" .. n, 1.0)
        adapter("outLevel" .. n, "Level" .. n, 0.0)
        adapter("tune" .. n, "Detune" .. n, 0.0)
    end
    for _, i in ipairs(opNames) do
        for _, j in ipairs(opNames) do
            adapter("phase" .. i .. j, "Matrix" .. i .. j, 0.0)
        end
    end
end

local views = {
    expanded = {"tune", "freq", "sync", "level"},
    outputs = {"outLevelA", "outLevelB", "outLevelC", "outLevelD"},
    ratios = {"ratioA", "ratioB", "ratioC", "ratioD"},
    tune = {"tuneA", "tuneB", "tuneC", "tuneD"},
    a = {"outLevelA", "ratioA", "tuneA", "phaseAA", "phaseAB", "phaseAC", "phaseAD"},
    b = {"outLevelB", "ratioB", "tuneB", "phaseBA", "phaseBB", "phaseBC", "phaseBD"},
    c = {"outLevelC", "ratioC", "tuneC", "phaseCA", "phaseCB", "phaseCC", "phaseCD"},
    d = {"outLevelD", "ratioD", "tuneD", "phaseDA", "phaseDB", "phaseDC", "phaseDD"},
    aIn = {"phaseAA", "phaseBA", "phaseCA", "phaseDA"},
    bIn = {"phaseAB", "phaseBB", "phaseCB", "phaseDB"},
    cIn = {"phaseAC", "phaseBC", "phaseCC", "phaseDC"},
    dIn = {"phaseAD", "phaseBD", "phaseCD", "phaseDD"},
    collapsed = {}
}

local function linMap(min, max, superCoarse, coarse, fine, superFine)
    local map = app.LinearDialMap(min, max)
    map:setSteps(superCoarse, coarse, fine, superFine)
    return map
end

local ratioMap = linMap(0.0, 24.0, 1.0, 1.0, 0.1, 0.01)

function AlembicVoice:onLoadViews(objects, branches)
    local controls = {}
    local opNames = {"A", "B", "C", "D"}

    for i, name in ipairs(opNames) do
        controls["outLevel" .. name] = GainBias {
            button = name .. " Out",
            description = name .. "to Output Lvl",
            branch = branches["outLevel" .. name],
            gainbias = objects["outLevel" .. name],
            range = objects["outLevel" .. name],
            biasMap = Encoder.getMap("[0,1]"),
            initialBias = 0.0
        }

        controls["ratio" .. name] = GainBias {
            button = name .. " Ratio",
            description = name .. "Freq Ratio",
            branch = branches["ratio" .. name],
            gainbias = objects["ratio" .. name],
            range = objects["ratio" .. name],
            biasMap = ratioMap,
            initialBias = 1.0
        }

        controls["tune" .. name] = GainBias {
            button = name .. " Fine",
            description = name .. " Fine Freq",
            branch = branches["tune" .. name],
            gainbias = objects["tune" .. name],
            range = objects["tune" .. name],
            biasMap = Encoder.getMap("oscFreq"),
            biasUnits = app.unitHertz,
            initialBias = 0.0,
            gainMap = Encoder.getMap("freqGain"),
            scaling = app.octaveScaling
        }

        for j, name2 in ipairs(opNames) do
            controls["phase" .. name .. name2] = GainBias {
                button = name .. " to " .. name2,
                description = name .. " to " .. name2 .. "Phase Index",
                branch = branches["phase" .. name .. name2],
                gainbias = objects["phase" .. name .. name2],
                range = objects["phase" .. name .. name2],
                biasMap = Encoder.getMap("[0,1]"),
                initialBias = 0.0
            }
        end
    end

    controls.tune = Pitch {
        button = "V/oct",
        branch = branches.tune,
        description = "V/oct",
        offset = objects.tune,
        range = objects.tuneRange
    }

    controls.freq = GainBias {
        button = "f0",
        description = "Fundamental",
        branch = branches.f0,
        gainbias = objects.f0,
        range = objects.f0,
        biasMap = Encoder.getMap("oscFreq"),
        biasUnits = app.unitHertz,
        initialBias = 27.5,
        gainMap = Encoder.getMap("freqGain"),
        scaling = app.octaveScaling
    }

    controls.level = GainBias {
        button = "level",
        description = "Level",
        branch = branches.level,
        gainbias = objects.level,
        range = objects.level,
        biasMap = Encoder.getMap("[-1,1]"),
        initialBias = 0.5
    }

    controls.sync = Gate {
        button = "sync",
        description = "Sync",
        branch = branches.sync,
        comparator = objects.sync
    }

    return controls, views
end

local menu = {
    "title",
    "changeViews",
    "changeViewMain",
    "changeViewOutputs",
    "changeViewRatios",
    "changeViewTune",
    "operatorViews",
    "changeViewA",
    "changeViewB",
    "changeViewC",
    "changeViewD",
    "changeViewPMIndex",
    "changeViewAIn",
    "changeViewBIn",
    "changeViewCIn",
    "changeViewDIn",
    "infoHeader",
    "rename",
    "load",
    "save"
}

local currentView = "expanded"
function AlembicVoice:changeView(view)
    currentView = view
    self:switchView(view)
end

function AlembicVoice:onShowMenu(objects, branches)
    local controls = {}

    controls.title = MenuHeader {
        description = "Alembic - 4 op phase mod voice"
    }

    controls.operatorViews = MenuHeader { description = "Operator Views:" }
    controls.changeViewA = Task { description = "A", task = function() self:changeView("a") end }
    controls.changeViewB = Task { description = "B", task = function() self:changeView("b") end }
    controls.changeViewC = Task { description = "C", task = function() self:changeView("c") end }
    controls.changeViewD = Task { description = "D", task = function() self:changeView("d") end }

    controls.changeViewPMIndex = MenuHeader { description = "Phase Modulation Indices:" }
    controls.changeViewAIn = Task { description = "@A", task = function() self:changeView("aIn") end }
    controls.changeViewBIn = Task { description = "@B", task = function() self:changeView("bIn") end }
    controls.changeViewCIn = Task { description = "@C", task = function() self:changeView("cIn") end }
    controls.changeViewDIn = Task { description = "@D", task = function() self:changeView("dIn") end }

    controls.changeViews = MenuHeader { description = "Aggregate Views:" }
    controls.changeViewMain = Task {
        description = "main",
        task = function() self:changeView("expanded") end
    }
    controls.changeViewOutputs = Task {
        description = "outputs",
        task = function() self:changeView("outputs") end
    }
    controls.changeViewRatios = Task {
        description = "ratios",
        task = function() self:changeView("ratios") end
    }
    controls.changeViewTune = Task {
        description = "freqs",
        task = function() self:changeView("tune") end
    }

    return controls, menu
end

-- Serialization: per feedback_serialize_deserialize_pattern, round-trip
-- every user-facing ParameterAdapter's Bias plus the ConstantOffset's
-- Offset and the Comparator's Threshold.
local adapterBiases = {
    "f0", "level",
    "ratioA", "ratioB", "ratioC", "ratioD",
    "outLevelA", "outLevelB", "outLevelC", "outLevelD",
    "tuneA", "tuneB", "tuneC", "tuneD",
    "phaseAA", "phaseAB", "phaseAC", "phaseAD",
    "phaseBA", "phaseBB", "phaseBC", "phaseBD",
    "phaseCA", "phaseCB", "phaseCC", "phaseCD",
    "phaseDA", "phaseDB", "phaseDC", "phaseDD"
}

function AlembicVoice:serialize()
    local t = Unit.serialize(self)
    for _, name in ipairs(adapterBiases) do
        local obj = self.objects[name]
        if obj then
            t[name] = obj:getParameter("Bias"):target()
        end
    end
    if self.objects.tune then
        t.tuneOffset = self.objects.tune:getParameter("Offset"):target()
    end
    if self.objects.sync then
        t.syncThreshold = self.objects.sync:getParameter("Threshold"):target()
    end
    return t
end

function AlembicVoice:deserialize(t)
    Unit.deserialize(self, t)
    for _, name in ipairs(adapterBiases) do
        if t[name] ~= nil and self.objects[name] then
            self.objects[name]:hardSet("Bias", t[name])
        end
    end
    if t.tuneOffset ~= nil and self.objects.tune then
        self.objects.tune:hardSet("Offset", t.tuneOffset)
    end
    if t.syncThreshold ~= nil and self.objects.sync then
        self.objects.sync:hardSet("Threshold", t.syncThreshold)
    end
end

return AlembicVoice
