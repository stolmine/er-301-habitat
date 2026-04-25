-- AlembicRef: 4-op phase-mod matrix, Lua-graph listening reference for Alembic.
-- Ports ~/repos/Accents/Xxxxxx.lua (6-op) down to 4 ops. Structure is diff-close
-- to Xxxxxx per feedback_identical_means_identical. Two deliberate deviations,
-- both inline-commented: phase-mixer cascade and output-mixer cascade collapse
-- from 5-deep (sized for 6 inputs) to 3-deep (binary tree for 4 inputs).
-- Temporary unit; retired once the native C++ AlembicVoice A/B-verifies.

local app = app
local libcore = require "core.libcore"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local Task = require "Unit.MenuControl.Task"
local MenuHeader = require "Unit.MenuControl.Header"
local Encoder = require "Encoder"

local AlembicRef = Class{}
AlembicRef:include(Unit)

function AlembicRef:init(args)
    args.title = "AlembicRef"
    args.mnemonic = "AR"
    Unit.init(self, args)
end

function AlembicRef:onLoadGraph(channelCount)
    if channelCount == 2 then
        self:loadStereoGraph()
    else
        self:loadMonoGraph()
    end
end

function AlembicRef:loadMonoGraph()
    local tune = self:addObject("tune", app.ConstantOffset())
    local tuneRange = self:addObject("tuneRange", app.MinMax())
    local f0 = self:addObject("f0", app.GainBias())
    local f0Range = self:addObject("f0Range", app.MinMax())
    local vca = self:addObject("vca", app.Multiply())
    local level = self:addObject("level", app.GainBias())
    local levelRange = self:addObject("levelRange", app.MinMax())
    local sync = self:addObject("sync", app.Comparator())

    connect(tune, "Out", tuneRange, "In")
    connect(f0, "Out", f0Range, "In")
    connect(level, "Out", levelRange, "In")

    local localDSP = {}

    -- 4-op vs Xxxxxx's 6. Direct drop of ops E and F.
    local opNames = {"A", "B", "C", "D"}

    for i, name in ipairs(opNames) do
        localDSP["op" .. name] = self:addObject("op" .. name, libcore.SineOscillator())
        localDSP["op" .. name .. "ratio"] = self:addObject("op" .. name .. "ratio", app.GainBias())
        localDSP["op" .. name .. "ratioRange"] = self:addObject("op" .. name .. "ratioRange", app.MinMax())
        localDSP["op" .. name .. "ratioX"] = self:addObject("op" .. name .. "ratioX", app.Multiply())
        localDSP["op" .. name .. "outLevel"] = self:addObject("op" .. name .. "outLevel", app.GainBias())
        localDSP["op" .. name .. "outLevelRange"] = self:addObject("op" .. name .. "outLevelRange", app.MinMax())
        localDSP["op" .. name .. "outVCA"] = self:addObject("op" .. name .. "outVCA", app.Multiply())
        localDSP["op" .. name .. "tune"] = self:addObject("op" .. name .. "tune", app.GainBias())
        localDSP["op" .. name .. "tuneRange"] = self:addObject("op" .. name .. "tuneRange", app.MinMax())
        localDSP["op" .. name .. "tuneSum"] = self:addObject("op" .. name .. "tuneSum", app.Sum())
        localDSP["op" .. name .. "track"] = self:addObject("op" .. name .. "track", app.Comparator())
        localDSP["op" .. name .. "trackX"] = self:addObject("op" .. name .. "trackX", app.Multiply())
        localDSP["op" .. name .. "track"]:setToggleMode()
        localDSP["op" .. name .. "track"]:setOptionValue("State", 1.0)
        connect(localDSP["op" .. name .. "track"], "Out", localDSP["op" .. name .. "trackX"], "Left")
        connect(localDSP["op" .. name .. "ratio"], "Out", localDSP["op" .. name .. "ratioX"], "Left")
        connect(f0, "Out", localDSP["op" .. name .. "ratioX"], "Right")
        connect(localDSP["op" .. name .. "ratioX"], "Out", localDSP["op" .. name .. "tuneSum"], "Left")
        connect(localDSP["op" .. name .. "tune"], "Out", localDSP["op" .. name .. "tuneSum"], "Right")
        connect(localDSP["op" .. name .. "tuneSum"], "Out", localDSP["op" .. name], "Fundamental")
        connect(localDSP["op" .. name .. "tune"], "Out", localDSP["op" .. name .. "tuneRange"], "In")
        connect(localDSP["op" .. name .. "ratio"], "Out", localDSP["op" .. name .. "ratioRange"], "In")
        connect(tune, "Out", localDSP["op" .. name .. "trackX"], "Right")
        connect(localDSP["op" .. name .. "trackX"], "Out", localDSP["op" .. name], "V/Oct")
        connect(sync, "Out", localDSP["op" .. name], "Sync")
        connect(localDSP["op" .. name], "Out", localDSP["op" .. name .. "outVCA"], "Left")
        connect(localDSP["op" .. name .. "outLevel"], "Out", localDSP["op" .. name .. "outVCA"], "Right")
        connect(localDSP["op" .. name .. "outLevel"], "Out", localDSP["op" .. name .. "outLevelRange"], "In")
    end

    -- Phase-mod matrix: 4x4 = 16 scalars (Xxxxxx has 6x6 = 36).
    -- Per-entry structure matches Xxxxxx lines 90-101: GainBias + MinMax + Multiply.
    -- Diagonal Multiply (phaseX<n><n>) is created with inputs unconnected; diagonal
    -- self-feedback uses the SineOscillator's Feedback inlet instead.
    for i, name in ipairs(opNames) do
        for j, name2 in ipairs(opNames) do
            localDSP["phase" .. name .. name2] = self:addObject("phase" .. name .. name2, app.GainBias())
            localDSP["phaseRange" .. name .. name2] = self:addObject("phaseRange" .. name .. name2, app.MinMax())
            connect(localDSP["phase" .. name .. name2], "Out", localDSP["phaseRange" .. name .. name2], "In")
            localDSP["phaseX" .. name .. name2] = self:addObject("phaseX" .. name .. name2, app.Multiply())
            if name ~= name2 then
                connect(localDSP["phase" .. name .. name2], "Out", localDSP["phaseX" .. name .. name2], "Left")
                connect(localDSP["op" .. name], "Out", localDSP["phaseX" .. name .. name2], "Right")
            end
            self:addMonoBranch(name .. "to" .. name2, localDSP["phase" .. name .. name2], "In", localDSP["phase" .. name .. name2], "Out")
        end
        -- Diagonal self-feedback path (Xxxxxx line 102).
        connect(localDSP["phase" .. name .. name], "Out", localDSP["op" .. name], "Feedback")

        -- DEVIATION vs Xxxxxx: phase-mixer cascade is 3-deep binary tree for 4 inputs
        -- (Xxxxxx is 5-deep for 6 inputs). Shape changes, per-element intent identical.
        for j = 1, 3 do
            localDSP["phaseMixer" .. name .. j] = self:addObject("phaseMixer" .. name .. j, app.Sum())
        end
    end

    for i, name in ipairs(opNames) do
        -- 3-deep phase-mixer wiring (cf. Xxxxxx lines 109-121 at 5-deep).
        -- Diagonal phaseX<name><name> has unwired inputs so its Out is 0 here; the
        -- real self-feedback path is via op<name>'s Feedback inlet above.
        connect(localDSP["phaseXA" .. name], "Out", localDSP["phaseMixer" .. name .. "1"], "Left")
        connect(localDSP["phaseXB" .. name], "Out", localDSP["phaseMixer" .. name .. "1"], "Right")
        connect(localDSP["phaseXC" .. name], "Out", localDSP["phaseMixer" .. name .. "2"], "Left")
        connect(localDSP["phaseXD" .. name], "Out", localDSP["phaseMixer" .. name .. "2"], "Right")
        connect(localDSP["phaseMixer" .. name .. "1"], "Out", localDSP["phaseMixer" .. name .. "3"], "Left")
        connect(localDSP["phaseMixer" .. name .. "2"], "Out", localDSP["phaseMixer" .. name .. "3"], "Right")
        connect(localDSP["phaseMixer" .. name .. "3"], "Out", localDSP["op" .. name], "Phase")
    end

    -- DEVIATION vs Xxxxxx: output-mixer cascade is 3-deep binary tree for 4 inputs
    -- (Xxxxxx is 5-deep for 6 inputs). Shape changes, semantics identical.
    for i = 1, 3 do
        localDSP["outputMixer" .. i] = self:addObject("outputMixer" .. i, app.Sum())
    end
    connect(localDSP["opAoutVCA"], "Out", localDSP["outputMixer1"], "Left")
    connect(localDSP["opBoutVCA"], "Out", localDSP["outputMixer1"], "Right")
    connect(localDSP["opCoutVCA"], "Out", localDSP["outputMixer2"], "Left")
    connect(localDSP["opDoutVCA"], "Out", localDSP["outputMixer2"], "Right")
    connect(localDSP["outputMixer1"], "Out", localDSP["outputMixer3"], "Left")
    connect(localDSP["outputMixer2"], "Out", localDSP["outputMixer3"], "Right")

    connect(localDSP["outputMixer3"], "Out", vca, "Right")
    connect(level, "Out", vca, "Left")
    connect(vca, "Out", self, "Out1")

    self:addMonoBranch("level", level, "In", level, "Out")
    self:addMonoBranch("tune", tune, "In", tune, "Out")
    self:addMonoBranch("sync", sync, "In", sync, "Out")
    self:addMonoBranch("f0", f0, "In", f0, "Out")

    for i, name in ipairs(opNames) do
        self:addMonoBranch("ratio" .. name, localDSP["op" .. name .. "ratio"], "In", localDSP["op" .. name .. "ratio"], "Out")
        self:addMonoBranch("outLevel" .. name, localDSP["op" .. name .. "outLevel"], "In", localDSP["op" .. name .. "outLevel"], "Out")
        self:addMonoBranch("tune" .. name, localDSP["op" .. name .. "tune"], "In", localDSP["op" .. name .. "tune"], "Out")
        self:addMonoBranch("track" .. name, localDSP["op" .. name .. "track"], "In", localDSP["op" .. name .. "track"], "Out")
    end
end

function AlembicRef:loadStereoGraph()
    self:loadMonoGraph()
    connect(self.objects.vca, "Out", self, "Out2")
end

local views = {
    expanded = {"tune", "freq", "sync", "level"},
    outputs = {"outLevelA", "outLevelB", "outLevelC", "outLevelD"},
    ratios = {"ratioA", "ratioB", "ratioC", "ratioD"},
    tune = {"tuneA", "tuneB", "tuneC", "tuneD"},
    track = {"trackA", "trackB", "trackC", "trackD"},
    a = {"outLevelA", "ratioA", "tuneA", "phaseAA", "phaseAB", "phaseAC", "phaseAD", "trackA"},
    b = {"outLevelB", "ratioB", "tuneB", "phaseBA", "phaseBB", "phaseBC", "phaseBD", "trackB"},
    c = {"outLevelC", "ratioC", "tuneC", "phaseCA", "phaseCB", "phaseCC", "phaseCD", "trackC"},
    d = {"outLevelD", "ratioD", "tuneD", "phaseDA", "phaseDB", "phaseDC", "phaseDD", "trackD"},
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

function AlembicRef:onLoadViews(objects, branches)
    local controls = {}
    local opNames = {"A", "B", "C", "D"}

    for i, name in ipairs(opNames) do
        controls["outLevel" .. name] = GainBias {
            button = name .. " Out",
            description = name .. "to Output Lvl",
            branch = branches["outLevel" .. name],
            gainbias = objects["op" .. name .. "outLevel"],
            range = objects["op" .. name .. "outLevelRange"],
            biasMap = Encoder.getMap("[0,1]"),
            initialBias = 0.0
        }

        controls["ratio" .. name] = GainBias {
            button = name .. " Ratio",
            description = name .. "Freq Ratio",
            branch = branches["ratio" .. name],
            gainbias = objects["op" .. name .. "ratio"],
            range = objects["op" .. name .. "ratioRange"],
            biasMap = ratioMap,
            initialBias = 1.0
        }

        controls["tune" .. name] = GainBias {
            button = name .. " Fine",
            description = name .. " Fine Freq",
            branch = branches["tune" .. name],
            gainbias = objects["op" .. name .. "tune"],
            range = objects["op" .. name .. "tuneRange"],
            biasMap = Encoder.getMap("oscFreq"),
            biasUnits = app.unitHertz,
            initialBias = 0.0,
            gainMap = Encoder.getMap("freqGain"),
            scaling = app.octaveScaling
        }

        controls["track" .. name] = Gate {
            button = name .. "Track",
            description = name .. " Track V/Oct",
            branch = branches["track" .. name],
            comparator = objects["op" .. name .. "track"]
        }

        for j, name2 in ipairs(opNames) do
            controls["phase" .. name .. name2] = GainBias {
                button = name .. " to " .. name2,
                description = name .. " to " .. name2 .. "Phase Index",
                branch = branches[name .. "to" .. name2],
                gainbias = objects["phase" .. name .. name2],
                range = objects["phaseRange" .. name .. name2],
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
        range = objects.f0Range,
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
        range = objects.levelRange,
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
    "changeViewTrack",
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
function AlembicRef:changeView(view)
    currentView = view
    self:switchView(view)
end

function AlembicRef:onShowMenu(objects, branches)
    local controls = {}

    controls.title = MenuHeader {
        description = "AlembicRef - 4 op phase mod reference"
    }

    controls.operatorViews = MenuHeader {
        description = "Operator Views:"
    }

    controls.changeViewA = Task { description = "A", task = function() self:changeView("a") end }
    controls.changeViewB = Task { description = "B", task = function() self:changeView("b") end }
    controls.changeViewC = Task { description = "C", task = function() self:changeView("c") end }
    controls.changeViewD = Task { description = "D", task = function() self:changeView("d") end }

    controls.changeViewPMIndex = MenuHeader {
        description = "Phase Modulation Indices:"
    }

    controls.changeViewAIn = Task { description = "@A", task = function() self:changeView("aIn") end }
    controls.changeViewBIn = Task { description = "@B", task = function() self:changeView("bIn") end }
    controls.changeViewCIn = Task { description = "@C", task = function() self:changeView("cIn") end }
    controls.changeViewDIn = Task { description = "@D", task = function() self:changeView("dIn") end }

    controls.changeViews = MenuHeader {
        description = "Aggregate Views:"
    }

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

    controls.changeViewTrack = Task {
        description = "track",
        task = function() self:changeView("track") end
    }

    return controls, menu
end

return AlembicRef
