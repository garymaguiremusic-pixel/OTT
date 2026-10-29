#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;

namespace
{
    constexpr int kW = OttAudioProcessorEditor::kBaseWidth, kH = OttAudioProcessorEditor::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 402;
    constexpr int kRowY[ott::NumBands] = { 70, 168, 266 };   // High, Mid, Low
    constexpr int kRowH = 94, kOptionsRowY = 366;

    const Rectangle<float> splitPanel   { 8.0f,   40.0f, 156.0f, 356.0f };
    const Rectangle<float> inputPanel   { 170.0f, 40.0f, 96.0f,  356.0f };
    const Rectangle<float> dynPanel     { 272.0f, 40.0f, 444.0f, 356.0f };
    const Rectangle<float> outputPanel  { 722.0f, 40.0f, 96.0f,  356.0f };
    const Rectangle<float> globalPanel  { 824.0f, 40.0f, 128.0f, 356.0f };

    const Rectangle<int> displayBounds { 280, 62, 360, 330 };

    const float kScales[] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
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
    addAndMakeVisible (content);
    content.setBounds (0, 0, kW, kH);

    auto status = [this] (const String& s) { setStatus (s); };

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

    setupToggle (bypassButton, ott::ParamIDs::bypass, "Bypass (keeps the latency, so nothing shifts in time)");

    // Split / band buttons ---------------------------------------------------------
    setupToggle (highOnButton, ott::ParamIDs::highOn, "High band on/off. Off = the mid band reaches all the way up");
    setupToggle (lowOnButton,  ott::ParamIDs::lowOn,  "Low band on/off. Off = the mid band reaches all the way down");
    highFreqField = std::make_unique<ValueField> (p (ott::ParamIDs::highFreq), ValueField::Style::Lcd);
    lowFreqField  = std::make_unique<ValueField> (p (ott::ParamIDs::lowFreq),  ValueField::Style::Lcd);
    for (auto* f : { highFreqField.get(), lowFreqField.get() })
    {
        f->formatter = [] (float v) { return ott::formatHz (v); };
        f->onStatus = status;
        content.addAndMakeVisible (*f);
    }

    for (int b = 0; b < ott::NumBands; ++b)
    {
        const String name = ott::bandName (b);
        setupToggle (powerButtons[(size_t) b], ott::bandParam (b, ott::BandIDs::active),
                     name + " band on. Off passes the band through untouched");
        soloButtons[(size_t) b].setButtonText ("S");
        setupToggle (soloButtons[(size_t) b], ott::bandParam (b, ott::BandIDs::solo), "Solo the " + name.toLowerCase() + " band");
        autoGainButtons[(size_t) b].setButtonText ("Auto Gain");
        setupToggle (autoGainButtons[(size_t) b], ott::bandParam (b, ott::BandIDs::autoGain),
                     "Auto gain: keeps the " + name.toLowerCase() + " band as loud after compression as it was "
                     "before its Input gain. Output still trims on top");

        auto db = [] (float v) { return ott::formatDb (v); };
        inputKnobs[(size_t) b]  = std::make_unique<OttKnob> (p (ott::bandParam (b, ott::BandIDs::input)),  String(), true);
        outputKnobs[(size_t) b] = std::make_unique<OttKnob> (p (ott::bandParam (b, ott::BandIDs::output)), String(), true);
        attackFields[(size_t) b]  = std::make_unique<ValueField> (p (ott::bandParam (b, ott::BandIDs::attack)),  ValueField::Style::Text);
        releaseFields[(size_t) b] = std::make_unique<ValueField> (p (ott::bandParam (b, ott::BandIDs::release)), ValueField::Style::Text);

        for (auto* k : { inputKnobs[(size_t) b].get(), outputKnobs[(size_t) b].get() })
        {
            k->formatter = db;
            k->onStatus = status;
            content.addAndMakeVisible (*k);
        }
        for (auto* f : { attackFields[(size_t) b].get(), releaseFields[(size_t) b].get() })
        {
            f->formatter = [] (float v) { return ott::formatMs (v); };
            f->onStatus = status;
            content.addAndMakeVisible (*f);
        }
    }

    setupToggle (softKneeButton, ott::ParamIDs::softKnee, "Soft knee: rounds the corners of both thresholds");
    setupToggle (rmsButton, ott::ParamIDs::rms, "RMS detection (on) or peak detection (off)");

    // Global knobs -------------------------------------------------------------------
    auto makeKnob = [&] (std::unique_ptr<OttKnob>& knob, const char* id, const String& title, bool bipolar)
    {
        knob = std::make_unique<OttKnob> (p (id), title, bipolar);
        knob->onStatus = status;
        content.addAndMakeVisible (*knob);
    };
    makeKnob (outputKnob,   ott::ParamIDs::output,   "Output",   true);
    makeKnob (timeKnob,     ott::ParamIDs::time,     "Time",     false);
    makeKnob (amountKnob,   ott::ParamIDs::amount,   "Amount",   false);
    makeKnob (upwardKnob,   ott::ParamIDs::upward,   "Upward",   false);
    makeKnob (downwardKnob, ott::ParamIDs::downward, "Downward", false);
    makeKnob (mixKnob,      ott::ParamIDs::mix,      "Mix",      false);
    outputKnob->formatter = [] (float v) { return ott::formatDb (v); };

    // Display ----------------------------------------------------------------------
    display.onStatus = status;
    content.addAndMakeVisible (display);

    const char* modeNames[] = { "T", "B", "A" };
    const char* modeTips[] = { "Show and edit the above (top, downward) thresholds",
                               "Show and edit the below (bottom, upward) thresholds",
                               "Show and edit all thresholds" };
    for (int i = 0; i < 3; ++i)
    {
        auto& button = displayModeButtons[(size_t) i];
        button.setButtonText (modeNames[i]);
        button.setTooltip (modeTips[i]);
        button.onClick = [this, i]
        {
            auto& param = p (ott::ParamIDs::displayMode);
            param.beginChangeGesture();
            param.setValueNotifyingHost (param.convertTo0to1 ((float) i));
            param.endChangeGesture();
        };
        content.addAndMakeVisible (button);
    }

    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    content.addAndMakeVisible (helpButton);
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

void OttAudioProcessorEditor::setupToggle (Button& button, const String& paramId, const String& tooltip)
{
    button.setClickingTogglesState (true);
    button.setTooltip (tooltip);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.apvts, paramId, button));
    content.addAndMakeVisible (button);
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
    sizeBox.setBounds (560, 7, 72, 21);
    bypassButton.setBounds (644, 8, 64, 19);
    presetBox.setBounds (760, 7, 192, 21);

    for (int b = 0; b < ott::NumBands; ++b)
    {
        const int r = kRowY[b];
        const size_t i = (size_t) b;

        // SPLIT FREQ
        powerButtons[i].setBounds (94, r + 4, 28, 22);
        soloButtons[i].setBounds (126, r + 4, 30, 22);
        autoGainButtons[i].setBounds (16, r + 66, 140, 22);

        // INPUT / OUTPUT
        inputKnobs[i]->setBounds (176, r, 84, 92);
        outputKnobs[i]->setBounds (728, r, 84, 92);

        // ATT / REL
        attackFields[i]->setBounds (644, r + 14, 64, 22);
        releaseFields[i]->setBounds (644, r + 46, 64, 22);
    }
    highOnButton.setBounds (16, kRowY[ott::BandHigh] + 4, 72, 22);
    lowOnButton.setBounds (16, kRowY[ott::BandLow] + 4, 72, 22);
    highFreqField->setBounds (16, kRowY[ott::BandHigh] + 34, 140, 26);
    lowFreqField->setBounds (16, kRowY[ott::BandLow] + 34, 140, 26);

    softKneeButton.setBounds (16, kOptionsRowY + 1, 140, 24);
    rmsButton.setBounds (178, kOptionsRowY + 1, 80, 24);

    // display: lanes line up with the band rows
    display.setBounds (displayBounds);
    std::array<Rectangle<float>, 3> lanes;
    for (int b = 0; b < ott::NumBands; ++b)
        lanes[(size_t) b] = { 10.0f, (float) (kRowY[b] - displayBounds.getY() + 4), 334.0f, (float) (kRowH - 10) };
    display.setLaneBounds (lanes, { 10.0f, (float) (kOptionsRowY - displayBounds.getY()), 334.0f, 24.0f });

    for (int i = 0; i < 3; ++i)
        displayModeButtons[(size_t) i].setBounds (648 + i * 22, kOptionsRowY + 2, 20, 22);

    // GLOBAL: two columns of three
    outputKnob->setBounds   (828, kRowY[0], 58, 92);
    timeKnob->setBounds     (828, kRowY[1], 58, 92);
    amountKnob->setBounds   (828, kRowY[2], 58, 92);
    upwardKnob->setBounds   (890, kRowY[0], 58, 92);
    downwardKnob->setBounds (890, kRowY[1], 58, 92);
    mixKnob->setBounds      (890, kRowY[2], 58, 92);

    helpButton.setBounds (904, kBottomBarY + 7, 20, 20);
    helpOverlay.setBounds (0, 0, kW, kH);
}

//==============================================================================
void OttAudioProcessorEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = (float) jlimit (0.0, 0.2, (now - lastTimerMs) * 0.001);
    lastTimerMs = now;

    display.updateMeters (dt);

    const int program = processor.getCurrentProgram();
    if (program != lastShownProgram)
    {
        presetBox.setSelectedId (program + 1, dontSendNotification);
        lastShownProgram = program;
    }

    const int mode = (int) processor.apvts.getRawParameterValue (ott::ParamIDs::displayMode)->load();
    for (int i = 0; i < 3; ++i)
        displayModeButtons[(size_t) i].setToggleState (i == mode, dontSendNotification);

    // Controls of a switched-off outer band are dimmed.
    const bool highOn = processor.apvts.getRawParameterValue (ott::ParamIDs::highOn)->load() >= 0.5f;
    const bool lowOn  = processor.apvts.getRawParameterValue (ott::ParamIDs::lowOn)->load() >= 0.5f;
    for (int b = 0; b < ott::NumBands; ++b)
    {
        const bool on = b == ott::BandHigh ? highOn : (b == ott::BandLow ? lowOn : true);
        for (Component* c : { (Component*) inputKnobs[(size_t) b].get(), (Component*) outputKnobs[(size_t) b].get(),
                              (Component*) attackFields[(size_t) b].get(), (Component*) releaseFields[(size_t) b].get(),
                              (Component*) &powerButtons[(size_t) b], (Component*) &soloButtons[(size_t) b],
                              (Component*) &autoGainButtons[(size_t) b] })
            if (c->isEnabled() != on)
                c->setEnabled (on);
    }
    highFreqField->setEnabled (highOn);
    lowFreqField->setEnabled (lowOn);

    // the mid band's range text in the split panel
    content.repaint (16, kRowY[ott::BandMid] + 34, 140, 26);
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
        for (int i = 0; i < 2600; ++i)
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

    drawWordmark (g, { 14.0f, 0.0f, 300.0f, (float) kTopStripH });
    CustomLookAndFeel::drawSmallLabel (g, "Size",   { 512.0f, 8.0f, 44.0f, 19.0f }, Justification::centredRight);
    CustomLookAndFeel::drawSmallLabel (g, "Preset", { 712.0f, 8.0f, 44.0f, 19.0f }, Justification::centredRight);

    // --- panels ---------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, splitPanel,  "Split Freq");
    CustomLookAndFeel::drawPanel (g, inputPanel,  "Input");
    CustomLookAndFeel::drawPanel (g, dynPanel,    "Dynamics");
    CustomLookAndFeel::drawPanel (g, outputPanel, "Output");
    CustomLookAndFeel::drawPanel (g, globalPanel, "Global");

    // band row separators
    for (int b = 1; b < ott::NumBands; ++b)
    {
        const int y = kRowY[b] - 4;
        for (auto panel : { splitPanel, inputPanel, outputPanel, globalPanel })
        {
            g.setColour (Theme::bevelDark);
            g.drawHorizontalLine (y, panel.getX() + 8.0f, panel.getRight() - 8.0f);
            g.setColour (Colours::white.withAlpha (0.07f));
            g.drawHorizontalLine (y + 1, panel.getX() + 8.0f, panel.getRight() - 8.0f);
        }
    }
    for (auto panel : { splitPanel, inputPanel, dynPanel, outputPanel })
    {
        g.setColour (Theme::bevelDark);
        g.drawHorizontalLine (kOptionsRowY - 4, panel.getX() + 8.0f, panel.getRight() - 8.0f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine (kOptionsRowY - 3, panel.getX() + 8.0f, panel.getRight() - 8.0f);
    }

    // Mid row: label and its frequency range
    {
        const int r = kRowY[ott::BandMid];
        CustomLookAndFeel::drawInsetBox (g, { 16.0f, (float) r + 4.0f, 72.0f, 22.0f });
        g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
        g.setColour (Colour (0xffdfe3e6));
        g.drawText ("Mid", Rectangle<float> (16.0f, (float) r + 4.0f, 72.0f, 22.0f), Justification::centred);

        auto& apvts = processor.apvts;
        const bool lowOn  = apvts.getRawParameterValue (ott::ParamIDs::lowOn)->load() >= 0.5f;
        const bool highOn = apvts.getRawParameterValue (ott::ParamIDs::highOn)->load() >= 0.5f;
        const String from = lowOn  ? ott::formatHz (apvts.getRawParameterValue (ott::ParamIDs::lowFreq)->load())  : String ("0 Hz");
        const String to   = highOn ? ott::formatHz (apvts.getRawParameterValue (ott::ParamIDs::highFreq)->load()) : String ("Nyquist");
        const auto rangeArea = Rectangle<float> (16.0f, (float) r + 34.0f, 140.0f, 26.0f);
        CustomLookAndFeel::drawInsetBox (g, rangeArea);
        g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
        g.setColour (Theme::labelText);
        g.drawText (from + " - " + to, rangeArea, Justification::centred);
    }

    CustomLookAndFeel::drawSmallLabel (g, "Att / Rel", { 640.0f, 60.0f, 70.0f, 10.0f }, Justification::centredRight);

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

    auto inset = Rectangle<float> (70.0f, bar.getY() + 7.0f, 820.0f, 20.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (12.5f, true));
    if (statusText.isNotEmpty())
    {
        g.setColour (Theme::blue);
        g.drawText (statusText, inset, Justification::centred);
    }
    else
    {
        g.setColour (Colour (0xffc8cdd2));
        g.drawText ("OTT MULTIBAND   -   LINEAR PHASE   -   LATENCY " + String (processor.latencyMs.load(), 1) + " ms",
                    inset, Justification::centred);
    }
}
