#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;

namespace
{
    constexpr int kW = OttAudioProcessorEditor::kBaseWidth, kH = OttAudioProcessorEditor::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 600;

    // Main view panels
    const Rectangle<float> mainPanel  { 8.0f, 40.0f,  504.0f, 150.0f };
    const Rectangle<float> bandsPanel { 8.0f, 196.0f, 504.0f, 252.0f };
    const Rectangle<float> shapePanel { 8.0f, 454.0f, 504.0f, 140.0f };

    constexpr int kBandRowY[ott::NumBands] = { 220, 296, 372 };   // High, Mid, Low
    constexpr int kBandRowH = 64;
    const Rectangle<int> displayBounds { 76, 214, 340, 228 };

    // Advanced page (coordinates inside the page, which starts below the top strip)
    constexpr int kAdvTop = kTopStripH;
    const Rectangle<float> advPanel { 8.0f, 6.0f, 504.0f, 554.0f };
    constexpr int kAdvRowY[ott::NumBands] = { 30, 182, 334 };
    constexpr int kAdvGlobalY = 486;
    constexpr int kAdvCol[3] = { 104, 238, 372 };
    constexpr int kAdvColW = 128;

    const float kScales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };

    void drawSeparator (Graphics& g, float y, float x0, float x1)
    {
        g.setColour (Theme::bevelDark);
        g.drawHorizontalLine ((int) y, x0, x1);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) y + 1, x0, x1);
    }
}

//==============================================================================
OttAudioProcessorEditor::OttAudioProcessorEditor (OttAudioProcessor& proc)
    : AudioProcessorEditor (&proc),
      processor (proc),
      display (proc)
{
    // Read the saved size first: setting the resize limits below resizes the editor.
    const float savedScale = jlimit (0.6f, 2.0f, processor.editorScale.load());

    setLookAndFeel (&lnf);
    content.painter = [this] (Graphics& g) { paintContent (g); };
    content.setOpaque (true);
    addAndMakeVisible (content);
    content.setBounds (0, 0, kW, kH);

    auto status = [this] (const String& s) { setStatus (s); };
    auto db1 = [] (float v) { return String (v, 1); };
    auto db  = [] (float v) { return ott::formatDb (v); };

    // Top strip ----------------------------------------------------------------------
    const auto& presets = ott::getFactoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        presetBox.addItem (String (i + 1).paddedLeft ('0', 2) + "  " + presets[(size_t) i].name, i + 1);
    presetBox.setTooltip ("Factory presets");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram (index);
        lastShownProgram = index;
    };
    content.addAndMakeVisible (presetBox);
    setupToggle (bypassButton, ott::ParamIDs::bypass, "Bypass (keeps the latency, so nothing shifts in time)", content);

    // Main knobs -----------------------------------------------------------------------
    auto makeKnob = [&] (std::unique_ptr<OttKnob>& knob, const char* id, const String& title, bool bipolar, Component& parent)
    {
        knob = std::make_unique<OttKnob> (p (id), title, bipolar);
        knob->onStatus = status;
        if (bipolar)
        {
            knob->formatter = db;
            knob->resetValue = 0.0f;   // double-click = 0 dB
        }
        parent.addAndMakeVisible (*knob);
    };
    makeKnob (depthKnob,    ott::ParamIDs::mix,      "Depth",    false, content);
    makeKnob (timeKnob,     ott::ParamIDs::time,     "Time",     false, content);
    makeKnob (inGainKnob,   ott::ParamIDs::inGain,   "In Gain",  true,  content);
    makeKnob (outGainKnob,  ott::ParamIDs::output,   "Out Gain", true,  content);
    makeKnob (upwardKnob,   ott::ParamIDs::upward,   "Upward",   false, content);
    makeKnob (downwardKnob, ott::ParamIDs::downward, "Downward", false, content);
    inGainKnob->onUserChange = [this] (float oldValue, float newValue) { linkInputToOutput (oldValue, newValue); };

    setupToggle (linkButton, ott::ParamIDs::inOutLink,
                 "Link: when on, raising IN GAIN lowers OUT GAIN by the same amount (and the other way round)", content);

    // Band rows: output gain readout, display lane, output gain knob
    for (int b = 0; b < ott::NumBands; ++b)
    {
        auto& param = p (ott::bandParam (b, ott::BandIDs::output));
        bandGainFields[(size_t) b] = std::make_unique<ValueField> (param, ValueField::Style::Text);
        bandGainKnobs[(size_t) b]  = std::make_unique<OttKnob> (param, String(), true);
        bandGainKnobs[(size_t) b]->showValue = false;
        bandGainFields[(size_t) b]->formatter = db1;
        bandGainKnobs[(size_t) b]->formatter = db;
        for (ParamControl* c : { (ParamControl*) bandGainFields[(size_t) b].get(), (ParamControl*) bandGainKnobs[(size_t) b].get() })
        {
            c->resetValue = 0.0f;
            c->onStatus = status;
            content.addAndMakeVisible (*c);
        }
    }

    display.onStatus = status;
    content.addAndMakeVisible (display);

    setupToggle (linearPhaseButton, ott::ParamIDs::linearPhase,
                 "Linear phase crossover: no phase shift between the bands, ~43 ms latency. Off = zero latency", content);
    setupToggle (softKneeButton, ott::ParamIDs::softKnee, "Soft knee: rounds the corners of both thresholds", content);
    setupToggle (rmsButton, ott::ParamIDs::rms, "RMS detection (on) or peak detection (off)", content);

    advancedButton.setTooltip ("Split points, band input, attack / release, thresholds, ratios, solo and auto gain");
    advancedButton.onClick = [this] { showAdvanced (! advanced.isVisible()); };
    content.addAndMakeVisible (advancedButton);

    // Bottom bar ---------------------------------------------------------------------
    for (int i = 0; i < (int) std::size (kScales); ++i)
        sizeBox.addItem (String (roundToInt (kScales[i] * 100.0f)) + " %", i + 1);
    sizeBox.setTooltip ("Window size (you can also drag the bottom-right corner)");
    sizeBox.onChange = [this]
    {
        const int index = sizeBox.getSelectedId() - 1;
        if (isPositiveAndBelow (index, (int) std::size (kScales)))
            setScale (kScales[index]);
    };
    content.addAndMakeVisible (sizeBox);

    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    content.addAndMakeVisible (helpButton);

    // Advanced page --------------------------------------------------------------------
    advanced.painter = [this] (Graphics& g) { paintAdvanced (g); };
    advanced.setOpaque (true);
    content.addChildComponent (advanced);

    setupToggle (highOnButton, ott::ParamIDs::highOn, "High band on/off. Off = the mid band reaches all the way up", advanced);
    setupToggle (lowOnButton,  ott::ParamIDs::lowOn,  "Low band on/off. Off = the mid band reaches all the way down", advanced);
    highFreqField = std::make_unique<ValueField> (p (ott::ParamIDs::highFreq), ValueField::Style::Lcd);
    lowFreqField  = std::make_unique<ValueField> (p (ott::ParamIDs::lowFreq),  ValueField::Style::Lcd);
    amountField   = std::make_unique<ValueField> (p (ott::ParamIDs::amount),   ValueField::Style::Lcd);
    highFreqField->formatter = lowFreqField->formatter = [] (float v) { return ott::formatHz (v); };
    for (auto* f : { highFreqField.get(), lowFreqField.get(), amountField.get() })
    {
        f->onStatus = status;
        advanced.addAndMakeVisible (*f);
    }

    for (int b = 0; b < ott::NumBands; ++b)
    {
        const size_t i = (size_t) b;
        const String name = ott::bandName (b);
        setupToggle (powerButtons[i], ott::bandParam (b, ott::BandIDs::active),
                     name + " band on. Off passes the band through untouched", advanced);
        soloButtons[i].setButtonText ("S");
        setupToggle (soloButtons[i], ott::bandParam (b, ott::BandIDs::solo), "Solo the " + name.toLowerCase() + " band", advanced);
        autoGainButtons[i].setButtonText ("Auto Gain");
        setupToggle (autoGainButtons[i], ott::bandParam (b, ott::BandIDs::autoGain),
                     "Auto gain: keeps the " + name.toLowerCase() + " band as loud after compression as it was "
                     "before its Input gain. The band's output knob still trims on top", advanced);

        makeKnob (inputKnobs[i], ott::bandParam (b, ott::BandIDs::input).toRawUTF8(), "Input", true, advanced);

        auto field = [&] (std::unique_ptr<ValueField>& f, const char* id, std::function<String (float)> fmt)
        {
            f = std::make_unique<ValueField> (p (ott::bandParam (b, id)), ValueField::Style::Lcd);
            f->formatter = std::move (fmt);
            f->onStatus = status;
            advanced.addAndMakeVisible (*f);
        };
        auto ms = [] (float v) { return ott::formatMs (v); };
        field (attackFields[i],      ott::BandIDs::attack,      ms);
        field (releaseFields[i],     ott::BandIDs::release,     ms);
        field (aboveThreshFields[i], ott::BandIDs::aboveThresh, [] (float v) { return ott::formatDb (v, 1); });
        field (belowThreshFields[i], ott::BandIDs::belowThresh, [] (float v) { return ott::formatDb (v, 1); });
        field (aboveRatioFields[i],  ott::BandIDs::aboveRatio,  [] (float v) { return ott::formatRatio (v) + " : 1"; });
        field (belowRatioFields[i],  ott::BandIDs::belowRatio,  [] (float v) { return "1 : " + ott::formatRatio (v); });
    }

    backButton.setTooltip ("Back to the main view");
    backButton.onClick = [this] { showAdvanced (false); };
    advanced.addAndMakeVisible (backButton);

    content.addChildComponent (helpOverlay);

    layoutContent();

    // Resizing: corner + SIZE menu, fixed aspect ratio.
    setResizable (true, true);
    setResizeLimits (roundToInt (kW * 0.6f), roundToInt (kH * 0.6f), kW * 2, kH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kW / (double) kH);
    setScale (savedScale);

    lastTimerMs = Time::getMillisecondCounterHiRes();
    timerCallback();
    startTimerHz (30);
}

OttAudioProcessorEditor::~OttAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void OttAudioProcessorEditor::setupToggle (Button& button, const String& paramId, const String& tooltip, Component& parent)
{
    button.setClickingTogglesState (true);
    button.setTooltip (tooltip);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.apvts, paramId, button));
    parent.addAndMakeVisible (button);
}

void OttAudioProcessorEditor::linkInputToOutput (float oldIn, float newIn)
{
    if (! isOn (ott::ParamIDs::inOutLink))
        return;

    auto& out = p (ott::ParamIDs::output);
    const auto range = out.getNormalisableRange();
    const float current = out.convertFrom0to1 (out.getValue());
    const float target = jlimit (range.start, range.end, current - (newIn - oldIn));

    out.beginChangeGesture();
    out.setValueNotifyingHost (out.convertTo0to1 (target));
    out.endChangeGesture();
}

void OttAudioProcessorEditor::showAdvanced (bool shouldShow)
{
    advanced.setVisible (shouldShow);
    advancedButton.setToggleState (shouldShow, dontSendNotification);
    if (shouldShow)
    {
        advanced.toFront (false);
        helpOverlay.toFront (false);
    }
}

void OttAudioProcessorEditor::setScale (float scale)
{
    setSize (roundToInt (kW * scale), roundToInt (kH * scale));
}

void OttAudioProcessorEditor::setStatus (const String& s)
{
    if (s != statusText)
    {
        statusText = s;
        content.repaint (0, kBottomBarY, kW, kH - kBottomBarY);
    }
}

//==============================================================================
void OttAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) kW;
    content.setTransform (AffineTransform::scale (scale));
    processor.editorScale.store (scale);

    // Tick the matching SIZE entry, if there is one.
    int match = 0;
    for (int i = 0; i < (int) std::size (kScales); ++i)
        if (std::abs (kScales[i] - scale) < 0.01f)
            match = i + 1;
    sizeBox.setSelectedId (match, dontSendNotification);
    if (match == 0)
        sizeBox.setText (String (roundToInt (scale * 100.0f)) + " %", dontSendNotification);
}

void OttAudioProcessorEditor::layoutContent()
{
    // top strip
    bypassButton.setBounds (300, 8, 60, 19);
    presetBox.setBounds (368, 7, 144, 21);

    // MAIN: four knobs, values in a strip underneath
    const int mainCentres[4] = { 71, 197, 323, 449 };
    OttKnob* mainKnobs[4] = { depthKnob.get(), timeKnob.get(), inGainKnob.get(), outGainKnob.get() };
    for (int i = 0; i < 4; ++i)
        mainKnobs[i]->setBounds (mainCentres[i] - 42, 62, 84, 108);
    linkButton.setBounds (364, 106, 44, 18);

    // BANDS
    display.setBounds (displayBounds);
    std::array<Rectangle<float>, 3> lanes;
    for (int b = 0; b < ott::NumBands; ++b)
    {
        const int r = kBandRowY[b];
        bandGainFields[(size_t) b]->setBounds (16, r + 20, 56, 24);
        bandGainKnobs[(size_t) b]->setBounds (420, r + 4, 56, 56);
        lanes[(size_t) b] = { 8.0f, (float) (r - displayBounds.getY() + 8), 324.0f, (float) (kBandRowH - 16) };
    }
    display.setLaneBounds (lanes, {});

    // UPWARD / DOWNWARD and options
    upwardKnob->setBounds (200 - 42, 474, 84, 108);
    downwardKnob->setBounds (320 - 42, 474, 84, 108);
    linearPhaseButton.setBounds (20, 484, 112, 24);
    softKneeButton.setBounds (20, 520, 112, 24);
    rmsButton.setBounds (388, 484, 112, 24);
    advancedButton.setBounds (388, 520, 112, 24);

    // bottom bar
    sizeBox.setBounds (390, kBottomBarY + 7, 72, 20);
    helpButton.setBounds (470, kBottomBarY + 7, 20, 20);
    helpOverlay.setBounds (0, 0, kW, kH);

    // ADVANCED page
    advanced.setBounds (0, kAdvTop, kW, kBottomBarY - kAdvTop);
    for (int b = 0; b < ott::NumBands; ++b)
    {
        const size_t i = (size_t) b;
        const int r = kAdvRowY[b];
        powerButtons[i].setBounds (196, r + 4, 28, 22);
        soloButtons[i].setBounds (228, r + 4, 30, 22);
        autoGainButtons[i].setBounds (372, r + 4, 128, 22);

        inputKnobs[i]->setBounds (16, r + 34, 76, 104);
        attackFields[i]->setBounds      (kAdvCol[0], r + 50, kAdvColW, 24);
        aboveThreshFields[i]->setBounds (kAdvCol[1], r + 50, kAdvColW, 24);
        aboveRatioFields[i]->setBounds  (kAdvCol[2], r + 50, kAdvColW, 24);
        releaseFields[i]->setBounds     (kAdvCol[0], r + 104, kAdvColW, 24);
        belowThreshFields[i]->setBounds (kAdvCol[1], r + 104, kAdvColW, 24);
        belowRatioFields[i]->setBounds  (kAdvCol[2], r + 104, kAdvColW, 24);
    }
    highOnButton.setBounds (16, kAdvRowY[ott::BandHigh] + 4, 60, 22);
    lowOnButton.setBounds (16, kAdvRowY[ott::BandLow] + 4, 60, 22);
    highFreqField->setBounds (82, kAdvRowY[ott::BandHigh] + 3, 106, 24);
    lowFreqField->setBounds (82, kAdvRowY[ott::BandLow] + 3, 106, 24);

    amountField->setBounds (kAdvCol[0], kAdvGlobalY + 32, kAdvColW, 24);
    backButton.setBounds (kAdvCol[2], kAdvGlobalY + 30, kAdvColW, 28);
}

//==============================================================================
void OttAudioProcessorEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = (float) jlimit (0.0, 0.2, (now - lastTimerMs) * 0.001);
    lastTimerMs = now;

    if (! advanced.isVisible())
        display.updateMeters (dt);

    const int program = processor.getCurrentProgram();
    if (program != lastShownProgram)
    {
        presetBox.setSelectedId (program + 1, dontSendNotification);
        lastShownProgram = program;
    }

    // Controls of a switched-off outer band are dimmed.
    const bool highOn = isOn (ott::ParamIDs::highOn), lowOn = isOn (ott::ParamIDs::lowOn);
    for (int b = 0; b < ott::NumBands; ++b)
    {
        const size_t i = (size_t) b;
        const bool on = b == ott::BandHigh ? highOn : (b == ott::BandLow ? lowOn : true);
        for (Component* c : { (Component*) bandGainFields[i].get(), (Component*) bandGainKnobs[i].get(),
                              (Component*) inputKnobs[i].get(), (Component*) attackFields[i].get(), (Component*) releaseFields[i].get(),
                              (Component*) aboveThreshFields[i].get(), (Component*) aboveRatioFields[i].get(),
                              (Component*) belowThreshFields[i].get(), (Component*) belowRatioFields[i].get(),
                              (Component*) &powerButtons[i], (Component*) &soloButtons[i], (Component*) &autoGainButtons[i] })
            if (c->isEnabled() != on)
                c->setEnabled (on);
    }
    highFreqField->setEnabled (highOn);
    lowFreqField->setEnabled (lowOn);

    if (advanced.isVisible())
        advanced.repaint (82, kAdvRowY[ott::BandMid] + 3, 106, 24);   // mid range text
    else
        content.repaint (0, kBottomBarY, kW, kH - kBottomBarY);       // latency text follows Linear Phase
}

//==============================================================================
void OttAudioProcessorEditor::paint (Graphics&)
{
    // everything is drawn by the scaled content component
}

void OttAudioProcessorEditor::drawBackground (Graphics& g)
{
    // The brushed metal is cached at the current physical resolution so it stays sharp at every size.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isNull() || std::abs (scale - backgroundScale) > 0.01f)
    {
        backgroundScale = scale;
        background = Image (Image::RGB, std::max (1, roundToInt (kW * scale)), std::max (1, roundToInt (kH * scale)), true);
        Graphics bg (background);
        bg.addTransform (AffineTransform::scale (scale));

        bg.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kH, false));
        bg.fillAll();

        Random rng (0x7a11);
        for (int y = 0; y < kH; ++y)
        {
            bg.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
            bg.fillRect (0, y, kW, 1);
        }
        for (int i = 0; i < 2000; ++i)
        {
            const int x = rng.nextInt (kW), y = rng.nextInt (kH), len = 20 + rng.nextInt (140);
            bg.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
            bg.fillRect (x, y, len, 1);
        }

        ColourGradient vignette (Colours::transparentBlack, kW * 0.5f, kH * 0.45f,
                                 Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
        bg.setGradientFill (vignette);
        bg.fillAll();
    }

    g.drawImageTransformed (background, AffineTransform::scale (1.0f / backgroundScale));
}

void OttAudioProcessorEditor::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Chrome "ott", blue "multiband", and a swoosh (as on VIRUS DELAY).
    const float baseline = area.getBottom() - 7.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "ott", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "multiband", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (Theme::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (Theme::blue.withAlpha (0.0f), sx, sy, Theme::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void OttAudioProcessorEditor::paintContent (Graphics& g)
{
    drawBackground (g);

    // --- top strip ----------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kW, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kW);
    g.setColour (Theme::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kW);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kW);

    drawWordmark (g, { 14.0f, 0.0f, 260.0f, (float) kTopStripH });

    // --- panels ---------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, mainPanel,  "Multiband Up / Down Compressor");
    CustomLookAndFeel::drawPanel (g, bandsPanel, "Bands");
    CustomLookAndFeel::drawPanel (g, shapePanel, "Upward / Downward");

    // value strips under the knobs (like the OTT readouts)
    CustomLookAndFeel::drawInsetBox (g, { 18.0f, 150.0f, 484.0f, 22.0f });
    CustomLookAndFeel::drawInsetBox (g, { 146.0f, 562.0f, 228.0f, 22.0f });

    // band rows: separators and H / M / L
    for (int b = 0; b < ott::NumBands; ++b)
    {
        const float r = (float) kBandRowY[b];
        if (b > 0)
            drawSeparator (g, r - 7.0f, bandsPanel.getX() + 8.0f, bandsPanel.getRight() - 8.0f);

        static const char* letters[] = { "H", "M", "L" };
        g.setFont (CustomLookAndFeel::uiFont (14.0f, true));
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawText (letters[b], Rectangle<float> (480.0f, r + 1.0f, 26.0f, (float) kBandRowH), Justification::centred);
        g.setColour (Theme::headerText);
        g.drawText (letters[b], Rectangle<float> (480.0f, r, 26.0f, (float) kBandRowH), Justification::centred);
    }

    // --- bottom bar -----------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kW, (float) (kH - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kW);

    auto tab = Rectangle<float> (8.0f, bar.getY() + 8.0f, 52.0f, 18.0f);
    g.setGradientFill (ColourGradient (Theme::headerTop, 0.0f, tab.getY(), Theme::headerBottom, 0.0f, tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (Theme::bevelDark);
    g.drawRoundedRectangle (tab, 3.0f, 1.0f);
    g.setColour (Theme::headerText);
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    g.drawText ("OTT", tab, Justification::centred);

    auto inset = Rectangle<float> (68.0f, bar.getY() + 7.0f, 314.0f, 20.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    String text = statusText;
    if (text.isEmpty())
    {
        const float latency = processor.latencyMs.load();
        text = latency > 0.0f ? "LINEAR PHASE  -  LATENCY " + String (latency, 1) + " ms"
                              : "MINIMUM PHASE  -  ZERO LATENCY";
    }
    g.setColour (statusText.isNotEmpty() ? Theme::blue : Colour (0xffc8cdd2));
    g.drawFittedText (text, inset.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.8f);
}

void OttAudioProcessorEditor::paintAdvanced (Graphics& g)
{
    // page background continues the brushed metal look
    g.setGradientFill (ColourGradient (Colour (0xff33363a), 0.0f, 0.0f, Colour (0xff1f2124), 0.0f, (float) advanced.getHeight(), false));
    g.fillAll();
    CustomLookAndFeel::drawPanel (g, advPanel, "Advanced");

    const char* rowA[] = { "Attack", "Above Threshold", "Above Ratio" };
    const char* rowB[] = { "Release", "Below Threshold", "Below Ratio" };

    for (int b = 0; b < ott::NumBands; ++b)
    {
        const float r = (float) kAdvRowY[b];
        if (b > 0)
            drawSeparator (g, r - 6.0f, advPanel.getX() + 8.0f, advPanel.getRight() - 8.0f);

        for (int c = 0; c < 3; ++c)
        {
            CustomLookAndFeel::drawSmallLabel (g, rowA[c], { (float) kAdvCol[c], r + 36.0f, (float) kAdvColW, 12.0f }, Justification::centredLeft);
            CustomLookAndFeel::drawSmallLabel (g, rowB[c], { (float) kAdvCol[c], r + 90.0f, (float) kAdvColW, 12.0f }, Justification::centredLeft);
        }
    }

    // Mid row: label and its frequency range
    {
        const float r = (float) kAdvRowY[ott::BandMid];
        CustomLookAndFeel::drawInsetBox (g, { 16.0f, r + 4.0f, 60.0f, 22.0f });
        g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
        g.setColour (Colour (0xffdfe3e6));
        g.drawText ("Mid", Rectangle<float> (16.0f, r + 4.0f, 60.0f, 22.0f), Justification::centred);

        auto& apvts = processor.apvts;
        const String from = isOn (ott::ParamIDs::lowOn)  ? ott::formatHz (apvts.getRawParameterValue (ott::ParamIDs::lowFreq)->load())  : String ("0 Hz");
        const String to   = isOn (ott::ParamIDs::highOn) ? ott::formatHz (apvts.getRawParameterValue (ott::ParamIDs::highFreq)->load()) : String ("top");
        const auto rangeArea = Rectangle<float> (82.0f, r + 3.0f, 106.0f, 24.0f);
        CustomLookAndFeel::drawInsetBox (g, rangeArea);
        g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
        g.setColour (Theme::labelText);
        g.drawFittedText (from + " - " + to, rangeArea.toNearestInt(), Justification::centred, 1, 0.8f);
    }

    // global row
    drawSeparator (g, (float) kAdvGlobalY - 6.0f, advPanel.getX() + 8.0f, advPanel.getRight() - 8.0f);
    CustomLookAndFeel::drawSmallLabel (g, "Amount", { (float) kAdvCol[0], (float) kAdvGlobalY + 18.0f, (float) kAdvColW, 12.0f },
                                       Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Global", { 16.0f, (float) kAdvGlobalY + 32.0f, 76.0f, 24.0f }, Justification::centredLeft);
}
