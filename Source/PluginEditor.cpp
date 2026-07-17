#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
//  LookAndFeel
//==============================================================================
DigitapeLookAndFeel::DigitapeLookAndFeel()
{
    setColour (juce::Label::textColourId,            Skin::ink);
    setColour (juce::ComboBox::backgroundColourId,   Skin::scrBg);
    setColour (juce::ComboBox::textColourId,         Skin::scrInk);
    setColour (juce::ComboBox::outlineColourId,      Skin::ink.withAlpha (0.4f));
    setColour (juce::ComboBox::arrowColourId,        Skin::scrDim);
    setColour (juce::PopupMenu::backgroundColourId,  Skin::scrBg);
    setColour (juce::PopupMenu::textColourId,        Skin::scrInk);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Skin::rustLo);
    setColour (juce::PopupMenu::highlightedTextColourId,       Skin::panelHi);
    setColour (juce::ToggleButton::textColourId,     Skin::ink);
    setColour (juce::ToggleButton::tickColourId,     Skin::rust);
    setColour (juce::ToggleButton::tickDisabledColourId, Skin::ink.withAlpha (0.35f));
    setColour (juce::TextButton::buttonColourId,     Skin::panelLo);
    setColour (juce::TextButton::buttonOnColourId,   Skin::rust);
    setColour (juce::TextButton::textColourOffId,    Skin::ink);
    setColour (juce::TextButton::textColourOnId,     Skin::panelHi);
}

void DigitapeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                            juce::Slider&)
{
    // Un knob se dessine dans un CARRE centre : sinon l'ellipse s'etire des que la cellule
    // n'est pas carree (c'etait le cas, d'ou les knobs deformes en soucoupe).
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto side = juce::jmin (area.getWidth(), area.getHeight());
    const auto bounds = juce::Rectangle<float> (side, side).withCentre (area.getCentre());

    const auto radius = side * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Piste de fond + arc de valeur (rouille) : on lit la position d'un coup d'oeil.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (Skin::ink.withAlpha (0.18f));
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour (Skin::rust);
    g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Corps du knob : sombre, legere lumiere en haut-gauche.
    const auto body = bounds.reduced (radius * 0.26f);
    g.setGradientFill (juce::ColourGradient (Skin::knobBody.brighter (0.35f), body.getX() + body.getWidth() * 0.35f,
                                             body.getY() + body.getHeight() * 0.28f,
                                             Skin::knobEdge, body.getRight(), body.getBottom(), true));
    g.fillEllipse (body);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (body, 1.0f);

    // Index creme.
    juce::Path ind;
    const auto r = body.getWidth() * 0.5f;
    ind.addRoundedRectangle (-1.2f, -r * 0.92f, 2.4f, r * 0.62f, 1.2f);
    ind.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (Skin::pointer);
    g.fillPath (ind);
}

//==============================================================================
//  FilterRangeSlider — HP + LP en un seul controle
//==============================================================================
FilterRangeSlider::FilterRangeSlider (juce::AudioProcessorValueTreeState& state,
                                      const juce::String& hpParamID, const juce::String& lpParamID,
                                      const juce::String& titleText)
    : title (titleText)
{
    hpParam = state.getParameter (hpParamID);
    lpParam = state.getParameter (lpParamID);
    jassert (hpParam != nullptr && lpParam != nullptr);

    hpAttach = std::make_unique<juce::ParameterAttachment> (*hpParam,
        [this] (float v) { hpHz = v; repaint(); });
    lpAttach = std::make_unique<juce::ParameterAttachment> (*lpParam,
        [this] (float v) { lpHz = v; repaint(); });

    hpAttach->sendInitialUpdate();
    lpAttach->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

float FilterRangeSlider::axisPos (float hz)
{
    return std::log (juce::jlimit (axisLo, axisHi, hz) / axisLo) / std::log (axisHi / axisLo);
}

float FilterRangeSlider::axisHz (float pos)
{
    return axisLo * std::pow (axisHi / axisLo, juce::jlimit (0.0f, 1.0f, pos));
}

juce::Rectangle<float> FilterRangeSlider::trackBounds() const
{
    // Bande centrale, en laissant la place au titre en dessous et aux poignees sur les cotes.
    return getLocalBounds().toFloat().withTrimmedBottom (14.0f).reduced (9.0f, 0.0f)
                           .withSizeKeepingCentre ((float) getWidth() - 18.0f, 8.0f);
}

float FilterRangeSlider::xForHz (float hz) const
{
    const auto t = trackBounds();
    return t.getX() + axisPos (hz) * t.getWidth();
}

float FilterRangeSlider::hzForX (float x) const
{
    const auto t = trackBounds();
    return axisHz ((x - t.getX()) / juce::jmax (1.0f, t.getWidth()));
}

int FilterRangeSlider::nearestHandle (float x) const
{
    return std::abs (x - xForHz (hpHz)) <= std::abs (x - xForHz (lpHz)) ? 0 : 1;
}

void FilterRangeSlider::paint (juce::Graphics& g)
{
    const auto t = trackBounds();
    const auto hpX = xForHz (hpHz), lpX = xForHz (lpHz);

    // Piste + bande passante (ce qui passe entre HP et LP).
    g.setColour (Skin::ink.withAlpha (0.22f));
    g.fillRoundedRectangle (t, t.getHeight() * 0.5f);
    g.setColour (Skin::rust);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (hpX, lpX), t.getY(),
                                                    std::abs (lpX - hpX), t.getHeight()),
                            t.getHeight() * 0.5f);

    // Poignees.
    for (auto hx : { hpX, lpX })
    {
        const juce::Rectangle<float> h (hx - 6.5f, t.getCentreY() - 6.5f, 13.0f, 13.0f);
        g.setColour (Skin::panelHi);
        g.fillEllipse (h);
        g.setColour (Skin::ink.withAlpha (0.7f));
        g.drawEllipse (h, 1.2f);
    }

    // Titre + valeurs, en Hz lisibles (pas "20000.00").
    auto asHz = [] (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " k"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    };

    auto footer = getLocalBounds().removeFromBottom (13);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.setColour (Skin::inkDim);
    g.drawText (asHz (hpHz), footer.removeFromLeft (50), juce::Justification::centredLeft, false);
    g.drawText (asHz (lpHz), footer.removeFromRight (50), juce::Justification::centredRight, false);
    g.setColour (Skin::ink);
    g.drawText (title, footer, juce::Justification::centred, false);
}

void FilterRangeSlider::mouseDown (const juce::MouseEvent& e)
{
    dragging = nearestHandle ((float) e.x);
    (dragging == 0 ? hpAttach : lpAttach)->beginGesture();
    mouseDrag (e);
}

void FilterRangeSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;

    // Les poignees ne peuvent pas se croiser : on borne chacune par sa propre plage ET par
    // l'autre poignee, en gardant un ecart minimal (sinon HP > LP = bande inversee).
    constexpr float minRatio = 1.15f;
    auto  hz = hzForX ((float) e.x);
    auto* p  = (dragging == 0 ? hpParam : lpParam);
    const auto range = p->getNormalisableRange();

    if (dragging == 0)
        hz = juce::jlimit (range.start, juce::jmin (range.end, lpHz / minRatio), hz);
    else
        hz = juce::jlimit (juce::jmax (range.start, hpHz * minRatio), range.end, hz);

    (dragging == 0 ? hpAttach : lpAttach)->setValueAsPartOfGesture (hz);
}

void FilterRangeSlider::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;

    (dragging == 0 ? hpAttach : lpAttach)->endGesture();
    dragging = -1;
}

//==============================================================================
//  Editor
//==============================================================================
DigitapeAudioProcessorEditor::DigitapeAudioProcessorEditor (DigitapeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lnf);

    // Facade
    setupKnob (saturationKnob,       "saturation",       "Saturation");
    setupKnob (timeKnob,             "time",             "Temps");
    setupKnob (feedbackKnob,         "feedback",         "Reinjection");
    setupKnob (wetGainKnob,          "wetGain",          "Echo vol");
    setupKnob (syncAmountKnob,       "syncAmount",       "Sync");
    setupKnob (reverbSizeKnob,       "reverbSize",       "Taille");
    setupKnob (reverbDecayKnob,      "reverbDecay",      "Longueur");
    setupKnob (reverbCharacterKnob,  "reverbCharacter",  "Caractere");
    setupKnob (reverbMixKnob,        "reverbMix",        "Reverb vol");
    setupKnob (reverbModulationKnob, "reverbModulation", "Modulation");
    setupKnob (mixKnob,              "mix",              "Mix");
    setupKnob (outputGainKnob,       "outputGain",       "Volume");
    setupKnob (integrityKnob,        "integrity",        "Integrite");
    setupKnob (reactivityKnob,       "reactivity",       "Reactivite");
    setupKnob (duckingKnob,          "ducking",          "Ducking");
    setupKnob (overflowKnob,         "overflow",         "Debordement");

    delayFilter  = std::make_unique<FilterRangeSlider> (audioProcessor.apvts, "delayHighpass",  "delayLowpass",  "Filtre delay");
    reverbFilter = std::make_unique<FilterRangeSlider> (audioProcessor.apvts, "reverbHighpass", "reverbLowpass", "Filtre reverbe");
    addAndMakeVisible (*delayFilter);
    addAndMakeVisible (*reverbFilter);

    saturationTypeCombo.addItemList ({ "Lampe", "Transfo", "Bande" }, 1);
    addAndMakeVisible (saturationTypeCombo);
    saturationTypeAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, "saturationType", saturationTypeCombo);

    addAndMakeVisible (monoButton);
    monoAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "mono", monoButton);
    addAndMakeVisible (safetyButton);
    safetyAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "safety", safetyButton);

    // Capot
    setupKnob (driftRateKnob,          "driftRate",          "Derive freq");
    setupKnob (driftDepthKnob,         "driftDepth",         "Derive prof");
    setupKnob (driftShapeKnob,         "driftShape",         "Derive forme");
    setupKnob (widthKnob,              "width",              "Largeur");
    setupKnob (bitcrushAmountKnob,     "bitcrushAmount",     "Bitcrush");
    setupKnob (decimationAmountKnob,   "decimationAmount",   "Decimation");
    setupKnob (saturationEmphasisKnob, "saturationEmphasis", "Sat emphasis");
    setupKnob (reverbPreDelayKnob,     "reverbPreDelay",     "Pre-delai");
    setupKnob (reverbTransientKnob,    "reverbTransient",    "Transient");
    setupKnob (reverbBrightnessKnob,   "reverbBrightness",   "Brightness");
    setupKnob (reverbBassDecayKnob,    "reverbBassDecay",    "Graves");
    setupKnob (reverbDistanceKnob,     "reverbDistance",     "Distance");
    setupKnob (crossFeedbackKnob,      "crossFeedback",      "Croisee");

    syncNoteCombo.addItemList ({ "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, 1);
    addAndMakeVisible (syncNoteCombo);
    syncNoteAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, "syncNoteValue", syncNoteCombo);
    syncFlavorCombo.addItemList ({ "Droit", "Pointe", "Triolet" }, 1);
    addAndMakeVisible (syncFlavorCombo);
    syncFlavorAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, "syncFlavor", syncFlavorCombo);

    // Les knobs du capot sont sur le circuit vert : leurs legendes doivent etre claires,
    // pas de l'encre sombre comme sur la facade creme.
    for (auto* k : { &driftRateKnob, &driftDepthKnob, &driftShapeKnob, &widthKnob,
                     &bitcrushAmountKnob, &decimationAmountKnob, &saturationEmphasisKnob,
                     &reverbPreDelayKnob, &reverbTransientKnob, &reverbBrightnessKnob,
                     &reverbBassDecayKnob, &reverbDistanceKnob, &crossFeedbackKnob })
        k->label.setColour (juce::Label::textColourId, Skin::silk);

    addAndMakeVisible (advancedToggle);
    advancedToggle.setClickingTogglesState (true);
    advancedToggle.onClick = [this] { setShowingAdvanced (advancedToggle.getToggleState()); };

    setShowingAdvanced (false);
}

DigitapeAudioProcessorEditor::~DigitapeAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void DigitapeAudioProcessorEditor::setupKnob (KnobWithLabel& knob, const juce::String& paramID, const juce::String& labelText)
{
    addAndMakeVisible (knob.slider);
    knob.slider.setColour (juce::Slider::textBoxTextColourId, Skin::ink);
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramID, knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    addAndMakeVisible (knob.label);
}

void DigitapeAudioProcessorEditor::placeKnob (KnobWithLabel& knob, juce::Rectangle<int> area)
{
    auto labelRow = area.removeFromBottom (13);
    knob.slider.setBounds (area);
    knob.label.setBounds (labelRow);
}

void DigitapeAudioProcessorEditor::setShowingAdvanced (bool shouldShow)
{
    showingAdvanced = shouldShow;
    advancedToggle.setButtonText (shouldShow ? "Fermer" : "Capot");

    for (auto* k : { &driftRateKnob, &driftDepthKnob, &driftShapeKnob, &widthKnob,
                     &bitcrushAmountKnob, &decimationAmountKnob, &saturationEmphasisKnob,
                     &reverbPreDelayKnob, &reverbTransientKnob, &reverbBrightnessKnob,
                     &reverbBassDecayKnob, &reverbDistanceKnob, &crossFeedbackKnob })
    {
        k->slider.setVisible (shouldShow);
        k->label.setVisible (shouldShow);
    }
    syncNoteCombo.setVisible (shouldShow);
    syncFlavorCombo.setVisible (shouldShow);

    // Le capot s'ouvre vers le HAUT : la fenetre grandit, la facade ne bouge pas.
    // Hauteurs calees sur le CONTENU reel (bandeau 34 + marges 20 + zone la plus haute 220,
    // le Master avec ses deux rangees + interrupteurs) - sinon les zones sont a moitie vides.
    // 1220 : le Master est passe a 4 colonnes (Debordement rejoint la rangee des moddes, les
    // interrupteurs remontent en haut). A 1140 les cellules tombaient a 83 px et les libelles
    // se faisaient couper.
    setSize (1220, shouldShow ? 200 + 280 : 280);
}

void DigitapeAudioProcessorEditor::drawZone (juce::Graphics& g, juce::Rectangle<int> area,
                                             const juce::String& title, juce::Colour accent) const
{
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (accent.withAlpha (0.45f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);

    auto head = area.reduced (10, 6).removeFromTop (12);
    g.setColour (accent);
    g.fillRoundedRectangle ((float) head.getX(), (float) head.getCentreY() - 2.5f, 5.0f, 5.0f, 2.5f);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.setColour (Skin::ink);
    g.drawText (title, head.withTrimmedLeft (10), juce::Justification::centredLeft, false);
}

void DigitapeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Capot ouvert : circuit imprime au-dessus de la facade.
    if (showingAdvanced && ! hoodArea.isEmpty())
    {
        g.setGradientFill (juce::ColourGradient (Skin::pcb, 0.0f, (float) hoodArea.getY(),
                                                 Skin::pcbLo, 0.0f, (float) hoodArea.getBottom(), false));
        g.fillRect (hoodArea);

        g.setColour (Skin::copper.withAlpha (0.10f));
        for (int gx = hoodArea.getX() + 24; gx < hoodArea.getRight(); gx += 48)
            g.fillRect (gx, hoodArea.getY(), 1, hoodArea.getHeight());

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (Skin::copper);
        g.drawText ("CIRCUIT INTERNE  -  reglages fins", hoodArea.reduced (16, 8).removeFromTop (14),
                    juce::Justification::centredLeft, false);

        // Charniere.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (hoodArea.getX(), hoodArea.getBottom() - 3, hoodArea.getWidth(), 3);
    }

    // Facade creme.
    g.setGradientFill (juce::ColourGradient (Skin::panelHi, 0.0f, (float) faceArea.getY(),
                                             Skin::panelLo, 0.0f, (float) faceArea.getBottom(), false));
    g.fillRect (faceArea);

    // Bandeau creme + filet rouille (option 2 de la maquette).
    auto band = faceArea.withHeight (34);
    g.setColour (Skin::rust);
    g.fillRect (band.getX(), band.getBottom() - 2, band.getWidth(), 2);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.setColour (Skin::ink);
    g.drawText ("DIGI", band.reduced (16, 0).removeFromLeft (44), juce::Justification::centredLeft, false);
    g.setColour (Skin::rust);
    g.drawText ("TAPE", band.reduced (16, 0).withTrimmedLeft (44), juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    g.setColour (Skin::inkDim);
    g.drawText ("ECHO . REVERBE NUMERIQUE", band.reduced (150, 0), juce::Justification::centredLeft, false);

    drawZone (g, zPreamp, "PREAMPLI", Skin::rust);
    drawZone (g, zDelay,  "DELAY",    Skin::rustLo);
    drawZone (g, zReverb, "REVERBE",  Skin::rustLo);
    drawZone (g, zMaster, "MASTER",   Skin::inkDim);
}

void DigitapeAudioProcessorEditor::resized()
{
    auto all = getLocalBounds();

    hoodArea = showingAdvanced ? all.removeFromTop (200) : juce::Rectangle<int>();
    faceArea = all;

    // ---- Capot : trois groupes de reglages fins ----
    if (showingAdvanced)
    {
        auto hood = hoodArea.reduced (16, 0).withTrimmedTop (26).withTrimmedBottom (12);
        // 8 colonnes : la rangee 2 (preampli + reverbe + reinjection) en compte exactement 8 ;
        // la rangee 1 en garde 6 et laisse deux cellules libres a droite.
        const int cellW = hood.getWidth() / 8;

        auto row1 = hood.removeFromTop (74);
        placeKnob (driftRateKnob,        row1.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (driftDepthKnob,       row1.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (driftShapeKnob,       row1.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (widthKnob,            row1.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (bitcrushAmountKnob,   row1.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (decimationAmountKnob, row1.removeFromLeft (cellW).reduced (6, 0));

        hood.removeFromTop (8);
        auto row2 = hood.removeFromTop (74);
        placeKnob (saturationEmphasisKnob, row2.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (reverbPreDelayKnob,     row2.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (reverbTransientKnob,    row2.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (reverbBrightnessKnob,   row2.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (reverbBassDecayKnob,    row2.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (reverbDistanceKnob,     row2.removeFromLeft (cellW).reduced (6, 0));
        placeKnob (crossFeedbackKnob,      row2.removeFromLeft (cellW).reduced (6, 0));

        auto combos = row2.removeFromLeft (cellW).reduced (6, 8);
        syncNoteCombo.setBounds (combos.removeFromTop (24));
        combos.removeFromTop (5);
        syncFlavorCombo.setBounds (combos.removeFromTop (24));
    }

    // ---- Facade ----
    auto face = faceArea;
    auto band = face.removeFromTop (34);
    advancedToggle.setBounds (band.removeFromRight (90).reduced (8, 5));

    face = face.reduced (12, 10);

    zPreamp = face.removeFromLeft (124);
    face.removeFromLeft (8);
    zDelay  = face.removeFromLeft (286);
    face.removeFromLeft (8);
    zReverb = face.removeFromLeft (350);   // 5 knobs : Taille, Longueur, Caractere, Modulation, Vol
    face.removeFromLeft (8);
    zMaster = face;

    // Preampli : saturation + type.
    {
        auto z = zPreamp.reduced (10, 0).withTrimmedTop (26).withTrimmedBottom (10);
        placeKnob (saturationKnob, z.removeFromTop (76));
        z.removeFromTop (10);
        saturationTypeCombo.setBounds (z.removeFromTop (24));
    }

    // Delay : 4 knobs + le filtre en un seul controle.
    {
        auto z = zDelay.reduced (10, 0).withTrimmedTop (26).withTrimmedBottom (10);
        auto knobs = z.removeFromTop (76);
        const int w = knobs.getWidth() / 4;
        placeKnob (timeKnob,       knobs.removeFromLeft (w).reduced (4, 0));
        placeKnob (feedbackKnob,   knobs.removeFromLeft (w).reduced (4, 0));
        placeKnob (wetGainKnob,    knobs.removeFromLeft (w).reduced (4, 0));
        placeKnob (syncAmountKnob, knobs.removeFromLeft (w).reduced (4, 0));
        z.removeFromTop (10);
        delayFilter->setBounds (z.removeFromTop (34));
    }

    // Reverbe : 5 knobs (Taille et Longueur sont deux choses distinctes) + le filtre.
    {
        auto z = zReverb.reduced (10, 0).withTrimmedTop (26).withTrimmedBottom (10);
        auto knobs = z.removeFromTop (76);
        const int w = knobs.getWidth() / 5;
        placeKnob (reverbSizeKnob,       knobs.removeFromLeft (w).reduced (3, 0));
        placeKnob (reverbDecayKnob,      knobs.removeFromLeft (w).reduced (3, 0));
        placeKnob (reverbCharacterKnob,  knobs.removeFromLeft (w).reduced (3, 0));
        placeKnob (reverbModulationKnob, knobs.removeFromLeft (w).reduced (3, 0));
        placeKnob (reverbMixKnob,        knobs.removeFromLeft (w).reduced (3, 0));
        z.removeFromTop (10);
        reverbFilter->setBounds (z.removeFromTop (34));
    }

    // Master : deux rangees de 4 colonnes alignees.
    // La rangee du BAS est celle des knobs "moddes" - Integrite, Debordement, Reactivite : les
    // trois que le constructeur n'avait pas prevus, ajoutes apres coup par quelqu'un qui a
    // ouvert la machine. Les poser ensemble sur leur propre rangee est deja un debut de
    // famille visuelle ; leur donner un vrai look de mod (capuchon depareille, serigraphie a
    // la main) viendra avec le skin. Rangee du haut = les controles d'origine.
    // Les interrupteurs occupent la 4e cellule du haut plutot qu'une rangee a eux : la facade
    // fait 280 de haut, il n'y a la place que pour deux rangees de knobs.
    {
        auto z = zMaster.reduced (10, 0).withTrimmedTop (26).withTrimmedBottom (10);
        const int w = z.getWidth() / 4;
        auto r1 = z.removeFromTop (76);
        placeKnob (mixKnob,        r1.removeFromLeft (w).reduced (4, 0));
        placeKnob (outputGainKnob, r1.removeFromLeft (w).reduced (4, 0));
        placeKnob (duckingKnob,    r1.removeFromLeft (w).reduced (4, 0));
        auto btns = r1.removeFromLeft (w).reduced (4, 12);
        monoButton.setBounds (btns.removeFromTop (24));
        btns.removeFromTop (4);
        safetyButton.setBounds (btns.removeFromTop (24));
        z.removeFromTop (6);
        auto r2 = z.removeFromTop (76);
        placeKnob (integrityKnob,  r2.removeFromLeft (w).reduced (4, 0));
        placeKnob (overflowKnob,   r2.removeFromLeft (w).reduced (4, 0));
        placeKnob (reactivityKnob, r2.removeFromLeft (w).reduced (4, 0));
    }
}
