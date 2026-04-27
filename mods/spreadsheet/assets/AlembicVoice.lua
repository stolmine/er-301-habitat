-- Alembic (AlembicVoice): scan-driven 4-op phase-mod matrix voice.
-- Phase 3a: user controls scan position; per-block the C++ unit blends a
-- K-weighted window of preset slots into the live voice state. The 29
-- Phase 2 user-direct Parameters (ratios, levels, detunes, matrix) stay
-- on the C++ class but are dropped from the Lua UI; Phase 7 will
-- re-expose them as user-bias adapters wired into the group fades.
--
-- AlembicRef remains alongside as the Lua-graph reference voice (still
-- has its 16 user-direct matrix knobs).

local app = app
local libspreadsheet = require "spreadsheet.libspreadsheet"
local Class = require "Base.Class"
local Unit = require "Unit"
local Pitch = require "Unit.ViewControl.Pitch"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local AlembicScanControl = require "spreadsheet.AlembicScanControl"
local Encoder = require "Encoder"
local SamplePool = require "Sample.Pool"
local SamplePoolInterface = require "Sample.Pool.Interface"
local SampleEditor = require "Sample.Editor"
local Task = require "Unit.MenuControl.Task"
local MenuHeader = require "Unit.MenuControl.Header"

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
    -- SineOscillator's convention.
    local tune = self:addObject("tune", app.ConstantOffset())
    local tuneRange = self:addObject("tuneRange", app.MinMax())
    connect(tune, "Out", op, "V/Oct")
    connect(tune, "Out", tuneRange, "In")
    self:addMonoBranch("tune", tune, "In", tune, "Out")

    -- Sync: rising-edge gate, op detects with >0.5f threshold.
    local sync = self:addObject("sync", app.Comparator())
    sync:setTriggerMode()
    connect(sync, "Out", op, "Sync")
    self:addMonoBranch("sync", sync, "In", sync, "Out")

    -- Output (mono-dup stereo, matching AlembicRef).
    connect(op, "Out", self, "Out1")
    if channelCount > 1 then
        connect(op, "Out", self, "Out2")
    end

    local function adapter(name, paramName, initial)
        local a = self:addObject(name, app.ParameterAdapter())
        a:hardSet("Bias", initial)
        tie(op, paramName, a, "Out")
        self:addMonoBranch(name, a, "In", a, "Out")
        return a
    end

    adapter("f0", "F0", 27.5)
    adapter("level", "Level", 0.5)
    adapter("scanPos", "ScanPos", 0.0)
    adapter("scanK", "ScanK", 4.0)
end

local views = {
    expanded = {"tune", "freq", "sync", "scan", "level"},
    scan = {"scan"},
    collapsed = {}
}

function AlembicVoice:onLoadViews(objects, branches)
    local controls = {}

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

    controls.sync = Gate {
        button = "sync",
        description = "Sync",
        branch = branches.sync,
        comparator = objects.sync
    }

    controls.scan = AlembicScanControl {
        button = "scan",
        description = "Scan Position",
        branch = branches.scanPos,
        gainbias = objects.scanPos,
        range = objects.scanPos,
        biasMap = Encoder.getMap("[0,1]"),
        biasUnits = app.unitNone,
        biasPrecision = 3,
        initialBias = 0.0,
        kParam = objects.scanK:getParameter("Bias"),
        op = objects.op
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

    return controls, views
end

-- Serialize: 4 adapter biases + tune Offset + sync Threshold per
-- feedback_serialize_deserialize_pattern. The 29 hidden Phase 2
-- Parameters are inert in Phase 3 and not persisted.
local adapterBiases = { "f0", "level", "scanPos", "scanK" }

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
    if self.sample then
        t.sample = SamplePool.serializeSample(self.sample)
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
    if t.sample then
        local sample = SamplePool.deserializeSample(t.sample, self.chain)
        if sample then
            self:setSample(sample)
        else
            local Utils = require "Utils"
            app.logError("%s:deserialize: failed to load sample.", self)
            Utils.pp(t.sample)
        end
    end
end

-- Sample-pool slot. Mirrors SampleScanner.lua:93-127 with an explicit
-- detach-before-attach sequence: C++ setSample(nullptr) is fired first
-- (resets analyzer state, restores placeholder presets) before the
-- new pointer is set. Without this two-step transition, swapping
-- directly between two non-null samples can leave analyzer state
-- straddling the old and new buffer (observed: scan stops responding
-- on second sample load if the buffer wasn't detached first).
function AlembicVoice:setSample(sample)
    -- Step 1: detach existing sample (if any).
    if self.sample then
        self.objects.op:setSample(nil)
        self.sample:release(self)
        self.sample = nil
    end

    -- Step 2: attach new sample (if non-nil).
    if sample then
        sample:claim(self)
        self.sample = sample
        if sample:getChannelCount() > 0 then
            -- Mono analysis only -- channel 0 of any sample. Stereo will
            -- be summed to mono in the C++ analyzer.
            self.objects.op:setSample(sample.pSample)
        end
    end

    if self.sampleEditor then
        self.sampleEditor:setSample(sample)
    end
    self:notifyControls("setSample", sample)
end

function AlembicVoice:doAttachSampleFromCard()
    local task = function(sample)
        if sample then
            local Overlay = require "Overlay"
            Overlay.flashMainMessage("Attached sample: %s", sample.name)
            self:setSample(sample)
        end
    end
    SamplePool.chooseFileFromCard(self.loadInfo.id, task)
end

function AlembicVoice:doAttachSampleFromPool()
    local chooser = SamplePoolInterface(self.loadInfo.id, "choose")
    chooser:setDefaultChannelCount(self.channelCount)
    chooser:highlight(self.sample)
    local task = function(sample)
        if sample then
            local Overlay = require "Overlay"
            Overlay.flashMainMessage("Attached sample: %s", sample.name)
            self:setSample(sample)
        end
    end
    chooser:subscribe("done", task)
    chooser:show()
end

function AlembicVoice:doDetachSample()
    local Overlay = require "Overlay"
    Overlay.flashMainMessage("Sample detached.")
    self:setSample()
end

function AlembicVoice:showSampleEditor()
    if self.sample then
        if self.sampleEditor == nil then
            self.sampleEditor = SampleEditor(self, self.objects.op)
            self.sampleEditor:setSample(self.sample)
        end
        self.sampleEditor:show()
    else
        local Overlay = require "Overlay"
        Overlay.flashMainMessage("You must first select a sample.")
    end
end

local menu = {
    "sampleHeader",
    "selectFromCard",
    "selectFromPool",
    "detachBuffer",
    "editSample"
}

function AlembicVoice:onShowMenu(objects, branches)
    local controls = {}

    controls.sampleHeader = MenuHeader { description = "Sample Menu" }

    controls.selectFromCard = Task {
        description = "Select from Card",
        task = function() self:doAttachSampleFromCard() end
    }

    controls.selectFromPool = Task {
        description = "Select from Pool",
        task = function() self:doAttachSampleFromPool() end
    }

    controls.detachBuffer = Task {
        description = "Detach Buffer",
        task = function() self:doDetachSample() end
    }

    controls.editSample = Task {
        description = "Edit Buffer",
        task = function() self:showSampleEditor() end
    }

    local sub = {}
    if self.sample then
        sub[1] = {
            position = app.GRID5_LINE1,
            justify = app.justifyLeft,
            text = "Attached Sample:"
        }
        sub[2] = {
            position = app.GRID5_LINE2,
            justify = app.justifyLeft,
            text = "+ " .. self.sample:getFilenameForDisplay(24)
        }
        sub[3] = {
            position = app.GRID5_LINE3,
            justify = app.justifyLeft,
            text = "+ " .. self.sample:getDurationText()
        }
        sub[4] = {
            position = app.GRID5_LINE4,
            justify = app.justifyLeft,
            text = string.format("+ %s %s %s",
                self.sample:getChannelText(),
                self.sample:getSampleRateText(),
                self.sample:getMemorySizeText())
        }
    else
        sub[1] = {
            position = app.GRID5_LINE3,
            justify = app.justifyCenter,
            text = "No sample attached."
        }
    end

    return controls, menu, sub
end

function AlembicVoice:onRemove()
    self:setSample(nil)
    Unit.onRemove(self)
end

return AlembicVoice
