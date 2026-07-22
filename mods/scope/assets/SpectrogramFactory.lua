-- Shared builder for the Spectrogram family. The plies argument sets how many display
-- sections wide the spectrum is drawn (2/3/4/6) AND selects the FFT size (2/3-ply = 256-pt,
-- 4/6-ply = 512-pt) so the wider units are backed by real bins, not interpolation.
--
-- Sub-display (apes the Scope units' ScopeView pattern):
--   S1 "freq"  - frequency axis:  log (EQ-style, default) / lin
--   S2 "amp"   - vertical mapping: log-dB (default) / lin / exp
--   S3 "peak"  - read-only readout: greatest-energy frequency + level (dB)
local app = app
local libscope = require "scope.libscope"
local Class = require "Base.Class"
local Unit = require "Unit"
local ViewControl = require "Unit.ViewControl"
local Encoder = require "Encoder"
local ply = app.SECTION_PLY

local col1 = app.BUTTON1_CENTER
local col2 = app.BUTTON2_CENTER
local col3 = app.BUTTON3_CENTER
local center3 = app.GRID5_CENTER3

local FREQ_LABELS = { "log", "lin" }        -- graphic freq-mode 0,1
local AMP_LABELS  = { "log", "lin", "exp" } -- graphic amp-mode 0,1,2

return function(plies, title, mnemonic)
  local Spectrogram = Class {}
  Spectrogram:include(Unit)

  function Spectrogram:init(args)
    args.title = title
    args.mnemonic = mnemonic
    Unit.init(self, args)
  end

  function Spectrogram:onLoadGraph(channelCount)
    local op = self:addObject("op", libscope.Spectrogram())
    op:hardSet("FFT Size", plies >= 4 and 512 or 256)
    if channelCount > 1 then
      connect(self, "In1", op, "In L")
      connect(self, "In2", op, "In R")
      connect(op, "Out L", self, "Out1")
      connect(op, "Out R", self, "Out2")
    else
      connect(self, "In1", op, "In L")
      connect(op, "Out L", self, "Out1")
    end
  end

  function Spectrogram:onLoadViews()
    local view = Class {}
    view:include(ViewControl)

    function view:init(vargs)
      ViewControl.init(self)
      self:setClassName("Scope.SpectrogramView")
      local width = vargs.width

      local graphic = app.Graphic(0, 0, width, 64)
      self:setMainCursorController(graphic)
      self:setControlGraphic(graphic)
      for i = 1, (width // ply) do
        self:addSpotDescriptor{ center = (i - 0.5) * ply }
      end

      local spectrum = libscope.SpectrogramGraphic(0, 0, width, 64)
      spectrum:follow(vargs.dspObject)
      graphic:addChild(spectrum)
      self.spectrum = spectrum

      -- Sub-display: S1 freq / S2 amp selectors (dotted/solid boxes) + S3 peak readout.
      self.subGraphic = app.Graphic(0, 0, 128, 64)

      self.freqBox = libscope.ScopeControlBox(0, 0, 22, 16)
      self.subGraphic:addChild(self.freqBox)
      self.freqLabel = app.Label(FREQ_LABELS[1], 12)
      self.freqLabel:setJustification(app.justifyCenter)
      self.freqLabel:setForegroundColor(app.WHITE)
      self.subGraphic:addChild(self.freqLabel)

      self.ampBox = libscope.ScopeControlBox(0, 0, 22, 16)
      self.subGraphic:addChild(self.ampBox)
      self.ampLabel = app.Label(AMP_LABELS[1], 12)
      self.ampLabel:setJustification(app.justifyCenter)
      self.ampLabel:setForegroundColor(app.WHITE)
      self.subGraphic:addChild(self.ampLabel)

      local readout = libscope.SpectrogramReadout(0, 0, 44, 24)
      readout:follow(vargs.dspObject)
      readout:setCenter(col3, center3)
      self.subGraphic:addChild(readout)

      self.subGraphic:addChild(app.SubButton("freq", 1))
      self.subGraphic:addChild(app.SubButton("amp", 2))
      self.subGraphic:addChild(app.SubButton("peak", 3))

      self.freqIdx = 1  -- 1=log(default), 2=lin
      self.ampIdx = 1   -- 1=log(default), 2=lin, 3=exp
      self.focusedSlot = "freq"
      self.encoderState = Encoder.Coarse

      self:applyFreq()
      self:applyAmp()
    end

    local function sizeBoxToLabel(box, label, col, row)
      label:fitToText(4)
      box:setSize(label.mWidth + 4, label.mHeight + 4)
      box:setCenter(col, row)
      label:setCenter(col, row)
    end

    function view:applyFreq()
      self.spectrum:setFreqMode(self.freqIdx - 1)
      self.freqLabel:setText(FREQ_LABELS[self.freqIdx])
      sizeBoxToLabel(self.freqBox, self.freqLabel, col1, center3)
    end

    function view:applyAmp()
      self.spectrum:setAmpMode(self.ampIdx - 1)
      self.ampLabel:setText(AMP_LABELS[self.ampIdx])
      sizeBoxToLabel(self.ampBox, self.ampLabel, col2, center3)
    end

    function view:bumpFreq(delta)
      local n = self.freqIdx + (delta > 0 and 1 or -1)
      if n < 1 then n = 1 end
      if n > #FREQ_LABELS then n = #FREQ_LABELS end
      if n ~= self.freqIdx then self.freqIdx = n; self:applyFreq() end
    end

    function view:bumpAmp(delta)
      local n = self.ampIdx + (delta > 0 and 1 or -1)
      if n < 1 then n = 1 end
      if n > #AMP_LABELS then n = #AMP_LABELS end
      if n ~= self.ampIdx then self.ampIdx = n; self:applyAmp() end
    end

    function view:refreshFocusVisual()
      local active = self.focused == true
      self.freqBox:setFocused(active and self.focusedSlot == "freq")
      self.ampBox:setFocused(active and self.focusedSlot == "amp")
    end

    function view:onFocused() self:refreshFocusVisual() end
    function view:onUnfocused() self:refreshFocusVisual() end

    function view:subReleased(i, shifted)
      if shifted then return false end
      if i == 1 then
        self.focusedSlot = "freq"
      elseif i == 2 then
        self.focusedSlot = "amp"
      else
        return true  -- S3 is a read-only readout
      end
      if not self:hasFocus("encoder") then self:focus() end
      self:refreshFocusVisual()
      return true
    end

    function view:encoder(change, shifted)
      if self.focusedSlot == "amp" then
        self:bumpAmp(change)
      else
        self:bumpFreq(change)
      end
      return true
    end

    function view:onCursorEnter(spot)
      ViewControl.onCursorEnter(self, spot)
      self:refreshFocusVisual()
    end

    function view:onCursorLeave(spot)
      self.freqBox:setFocused(false)
      self.ampBox:setFocused(false)
      ViewControl.onCursorLeave(self, spot)
    end

    function view:setFreqIdx(idx)
      if idx and idx >= 1 and idx <= #FREQ_LABELS then self.freqIdx = idx; self:applyFreq() end
    end
    function view:setAmpIdx(idx)
      if idx and idx >= 1 and idx <= #AMP_LABELS then self.ampIdx = idx; self:applyAmp() end
    end

    function view:serialize()
      local t = ViewControl.serialize(self)
      t.freqIdx = self.freqIdx
      t.ampIdx = self.ampIdx
      return t
    end
    function view:deserialize(t)
      ViewControl.deserialize(self, t)
      if t.freqIdx then self:setFreqIdx(t.freqIdx) end
      if t.ampIdx then self:setAmpIdx(t.ampIdx) end
    end

    local specView = view {
      width = plies * ply,
      dspObject = self.objects.op
    }

    return {
      spectrum = specView
    }, {
      expanded = {"spectrum"},
      collapsed = {}
    }
  end

  return Spectrogram
end
