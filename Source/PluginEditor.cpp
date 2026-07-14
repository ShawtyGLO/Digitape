#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Generated/UILayout.h"
#include "Generated/ThemeColors.g.h"

//==============================================================================
DigitapeAudioProcessorEditor::DigitapeAudioProcessorEditor (DigitapeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    titleLabel.setText ("DIGITAPE", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, Theme::textPrimary);
    addAndMakeVisible (titleLabel);

    setupKnob (timeKnob,       "time",       "Temps");
    setupKnob (feedbackKnob,   "feedback",   "Reinjection");
    setupKnob (mixKnob,        "mix",        "Mix");
    setupKnob (driftRateKnob,  "driftRate",  "Derive freq");
    setupKnob (driftDepthKnob, "driftDepth", "Derive prof.");
    setupKnob (widthKnob,      "width",      "Largeur");
    setupKnob (driftShapeKnob, "driftShape", "Derive forme");
    setupKnob (integrityKnob,  "integrity",  "Integrite");
    setupKnob (wetGainKnob,    "wetGain",    "Echo volume");
    setupKnob (syncAmountKnob, "syncAmount", "Sync accroche");
    setupKnob (reverbSizeKnob,      "reverbSize",      "Reverbe taille");
    setupKnob (reverbCharacterKnob, "reverbCharacter", "Reverbe caract.");
    setupKnob (reverbMixKnob,       "reverbMix",       "Reverb volume");
    setupKnob (crossFeedbackKnob,   "crossFeedback",   "Reinjection croisee");
    setupKnob (outputGainKnob,      "outputGain",      "Volume sortie");

    addAndMakeVisible (monoButton);
    monoAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "mono", monoButton);

    addAndMakeVisible (safetyButton);
    safetyAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "safety", safetyButton);

    setupKnob (bitcrushAmountKnob,   "bitcrushAmount",   "Bitcrush");
    setupKnob (decimationAmountKnob, "decimationAmount", "Decimation");
    setupKnob (reverbPreDelayKnob,   "reverbPreDelay",   "Reverbe predelai");
    setupKnob (reverbTransientKnob,  "reverbTransient",  "Reverbe transient");
    setupKnob (reverbHighpassKnob,   "reverbHighpass",   "Reverbe HP");
    setupKnob (reverbLowpassKnob,    "reverbLowpass",    "Reverbe LP");
    setupKnob (reverbBrightnessKnob, "reverbBrightness", "Reverbe bright.");

    syncNoteCombo.addItemList ({ "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, 1);
    addAndMakeVisible (syncNoteCombo);
    syncNoteAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, "syncNoteValue", syncNoteCombo);

    syncFlavorCombo.addItemList ({ "Droit", "Pointe", "Triolet" }, 1);
    addAndMakeVisible (syncFlavorCombo);
    syncFlavorAttachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, "syncFlavor", syncFlavorCombo);

    addAndMakeVisible (advancedToggle);
    advancedToggle.setClickingTogglesState (true);
    advancedToggle.onClick = [this] { setShowingAdvanced (advancedToggle.getToggleState()); };

    setShowingAdvanced (false);
    setSize (760, 460);
}

DigitapeAudioProcessorEditor::~DigitapeAudioProcessorEditor()
{
}

void DigitapeAudioProcessorEditor::setupKnob (KnobWithLabel& knob, const juce::String& paramID, const juce::String& labelText)
{
    addAndMakeVisible (knob.slider);
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, paramID, knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setFont (juce::FontOptions (11.0f));
    knob.label.setColour (juce::Label::textColourId, Theme::textMuted);
    addAndMakeVisible (knob.label);
}

void DigitapeAudioProcessorEditor::placeKnob (KnobWithLabel& knob, float x, float y, float w, float h)
{
    // Legende integree dans le bas du rectangle du rangement : reste toujours dans
    // ses bornes quelle que soit sa position sur la facade (pres du bord haut/bas).
    const int ix = (int) x, iy = (int) y, iw = (int) w, ih = (int) h;
    const int labelH = 16;
    knob.slider.setBounds (ix, iy, iw, ih - labelH);
    knob.label.setBounds (ix, iy + ih - labelH, iw, labelH);
}

void DigitapeAudioProcessorEditor::setShowingAdvanced (bool shouldShow)
{
    showingAdvanced = shouldShow;
    advancedToggle.setButtonText (shouldShow ? "< Facade" : "Advanced");

    timeKnob.slider.setVisible (!shouldShow);       timeKnob.label.setVisible (!shouldShow);
    feedbackKnob.slider.setVisible (!shouldShow);   feedbackKnob.label.setVisible (!shouldShow);
    mixKnob.slider.setVisible (!shouldShow);        mixKnob.label.setVisible (!shouldShow);
    driftRateKnob.slider.setVisible (!shouldShow);  driftRateKnob.label.setVisible (!shouldShow);
    driftDepthKnob.slider.setVisible (!shouldShow); driftDepthKnob.label.setVisible (!shouldShow);
    widthKnob.slider.setVisible (!shouldShow);      widthKnob.label.setVisible (!shouldShow);
    driftShapeKnob.slider.setVisible (!shouldShow); driftShapeKnob.label.setVisible (!shouldShow);
    integrityKnob.slider.setVisible (!shouldShow);  integrityKnob.label.setVisible (!shouldShow);
    crossFeedbackKnob.slider.setVisible (!shouldShow); crossFeedbackKnob.label.setVisible (!shouldShow);
    outputGainKnob.slider.setVisible (!shouldShow);    outputGainKnob.label.setVisible (!shouldShow);
    wetGainKnob.slider.setVisible (!shouldShow);    wetGainKnob.label.setVisible (!shouldShow);
    syncAmountKnob.slider.setVisible (!shouldShow); syncAmountKnob.label.setVisible (!shouldShow);
    reverbSizeKnob.slider.setVisible (!shouldShow);      reverbSizeKnob.label.setVisible (!shouldShow);
    reverbCharacterKnob.slider.setVisible (!shouldShow); reverbCharacterKnob.label.setVisible (!shouldShow);
    reverbMixKnob.slider.setVisible (!shouldShow);       reverbMixKnob.label.setVisible (!shouldShow);
    monoButton.setVisible (!shouldShow);
    safetyButton.setVisible (!shouldShow);
    titleLabel.setVisible (!shouldShow);
    syncNoteCombo.setVisible (!shouldShow);
    syncFlavorCombo.setVisible (!shouldShow);

    bitcrushAmountKnob.slider.setVisible (shouldShow);   bitcrushAmountKnob.label.setVisible (shouldShow);
    decimationAmountKnob.slider.setVisible (shouldShow); decimationAmountKnob.label.setVisible (shouldShow);
    reverbPreDelayKnob.slider.setVisible (shouldShow);   reverbPreDelayKnob.label.setVisible (shouldShow);
    reverbTransientKnob.slider.setVisible (shouldShow);  reverbTransientKnob.label.setVisible (shouldShow);
    reverbHighpassKnob.slider.setVisible (shouldShow);   reverbHighpassKnob.label.setVisible (shouldShow);
    reverbLowpassKnob.slider.setVisible (shouldShow);    reverbLowpassKnob.label.setVisible (shouldShow);
    reverbBrightnessKnob.slider.setVisible (shouldShow); reverbBrightnessKnob.label.setVisible (shouldShow);

    setSize (760, shouldShow ? 280 : 460);
}

//==============================================================================
void DigitapeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bgDeep);
}

void DigitapeAudioProcessorEditor::resized()
{
    advancedToggle.setBounds (getWidth() - 100, 8, 80, 22);

    titleLabel.setBounds ((int) UI::ctrl::facade::titleLabel.x, (int) UI::ctrl::facade::titleLabel.y,
                           (int) UI::ctrl::facade::titleLabel.w, (int) UI::ctrl::facade::titleLabel.h);

    placeKnob (timeKnob,       UI::ctrl::facade::timeSlider.x,       UI::ctrl::facade::timeSlider.y,       UI::ctrl::facade::timeSlider.w,       UI::ctrl::facade::timeSlider.h);
    placeKnob (feedbackKnob,   UI::ctrl::facade::feedbackSlider.x,   UI::ctrl::facade::feedbackSlider.y,   UI::ctrl::facade::feedbackSlider.w,   UI::ctrl::facade::feedbackSlider.h);
    placeKnob (mixKnob,        UI::ctrl::facade::mixSlider.x,        UI::ctrl::facade::mixSlider.y,        UI::ctrl::facade::mixSlider.w,        UI::ctrl::facade::mixSlider.h);
    placeKnob (driftRateKnob,  UI::ctrl::facade::driftRateSlider.x,  UI::ctrl::facade::driftRateSlider.y,  UI::ctrl::facade::driftRateSlider.w,  UI::ctrl::facade::driftRateSlider.h);
    placeKnob (driftDepthKnob, UI::ctrl::facade::driftDepthSlider.x, UI::ctrl::facade::driftDepthSlider.y, UI::ctrl::facade::driftDepthSlider.w, UI::ctrl::facade::driftDepthSlider.h);
    placeKnob (widthKnob,      UI::ctrl::facade::widthSlider.x,      UI::ctrl::facade::widthSlider.y,      UI::ctrl::facade::widthSlider.w,      UI::ctrl::facade::widthSlider.h);
    placeKnob (driftShapeKnob, UI::ctrl::facade::driftShapeSlider.x, UI::ctrl::facade::driftShapeSlider.y, UI::ctrl::facade::driftShapeSlider.w, UI::ctrl::facade::driftShapeSlider.h);
    placeKnob (integrityKnob,  UI::ctrl::facade::integritySlider.x,  UI::ctrl::facade::integritySlider.y,  UI::ctrl::facade::integritySlider.w,  UI::ctrl::facade::integritySlider.h);
    placeKnob (crossFeedbackKnob, UI::ctrl::facade::crossFeedbackSlider.x, UI::ctrl::facade::crossFeedbackSlider.y, UI::ctrl::facade::crossFeedbackSlider.w, UI::ctrl::facade::crossFeedbackSlider.h);
    placeKnob (outputGainKnob, UI::ctrl::facade::outputGainSlider.x, UI::ctrl::facade::outputGainSlider.y, UI::ctrl::facade::outputGainSlider.w, UI::ctrl::facade::outputGainSlider.h);
    placeKnob (wetGainKnob,    UI::ctrl::facade::wetGainSlider.x,    UI::ctrl::facade::wetGainSlider.y,    UI::ctrl::facade::wetGainSlider.w,    UI::ctrl::facade::wetGainSlider.h);
    placeKnob (syncAmountKnob, UI::ctrl::facade::syncAmountSlider.x, UI::ctrl::facade::syncAmountSlider.y, UI::ctrl::facade::syncAmountSlider.w, UI::ctrl::facade::syncAmountSlider.h);
    placeKnob (reverbSizeKnob,      UI::ctrl::facade::reverbSizeSlider.x,      UI::ctrl::facade::reverbSizeSlider.y,      UI::ctrl::facade::reverbSizeSlider.w,      UI::ctrl::facade::reverbSizeSlider.h);
    placeKnob (reverbCharacterKnob, UI::ctrl::facade::reverbCharacterSlider.x, UI::ctrl::facade::reverbCharacterSlider.y, UI::ctrl::facade::reverbCharacterSlider.w, UI::ctrl::facade::reverbCharacterSlider.h);
    placeKnob (reverbMixKnob,       UI::ctrl::facade::reverbMixSlider.x,       UI::ctrl::facade::reverbMixSlider.y,       UI::ctrl::facade::reverbMixSlider.w,       UI::ctrl::facade::reverbMixSlider.h);

    monoButton.setBounds   ((int) UI::ctrl::facade::monoBtn.x,   (int) UI::ctrl::facade::monoBtn.y,   (int) UI::ctrl::facade::monoBtn.w,   (int) UI::ctrl::facade::monoBtn.h);
    safetyButton.setBounds ((int) UI::ctrl::facade::safetyBtn.x, (int) UI::ctrl::facade::safetyBtn.y, (int) UI::ctrl::facade::safetyBtn.w, (int) UI::ctrl::facade::safetyBtn.h);

    placeKnob (bitcrushAmountKnob,   UI::ctrl::advanced::bitcrushAmountSlider.x,   UI::ctrl::advanced::bitcrushAmountSlider.y,   UI::ctrl::advanced::bitcrushAmountSlider.w,   UI::ctrl::advanced::bitcrushAmountSlider.h);
    placeKnob (decimationAmountKnob, UI::ctrl::advanced::decimationAmountSlider.x, UI::ctrl::advanced::decimationAmountSlider.y, UI::ctrl::advanced::decimationAmountSlider.w, UI::ctrl::advanced::decimationAmountSlider.h);
    placeKnob (reverbPreDelayKnob,   UI::ctrl::advanced::reverbPreDelaySlider.x,   UI::ctrl::advanced::reverbPreDelaySlider.y,   UI::ctrl::advanced::reverbPreDelaySlider.w,   UI::ctrl::advanced::reverbPreDelaySlider.h);
    placeKnob (reverbTransientKnob,  UI::ctrl::advanced::reverbTransientSlider.x,  UI::ctrl::advanced::reverbTransientSlider.y,  UI::ctrl::advanced::reverbTransientSlider.w,  UI::ctrl::advanced::reverbTransientSlider.h);
    placeKnob (reverbHighpassKnob,   UI::ctrl::advanced::reverbHighpassSlider.x,   UI::ctrl::advanced::reverbHighpassSlider.y,   UI::ctrl::advanced::reverbHighpassSlider.w,   UI::ctrl::advanced::reverbHighpassSlider.h);
    placeKnob (reverbLowpassKnob,    UI::ctrl::advanced::reverbLowpassSlider.x,    UI::ctrl::advanced::reverbLowpassSlider.y,    UI::ctrl::advanced::reverbLowpassSlider.w,    UI::ctrl::advanced::reverbLowpassSlider.h);
    placeKnob (reverbBrightnessKnob, UI::ctrl::advanced::reverbBrightnessSlider.x, UI::ctrl::advanced::reverbBrightnessSlider.y, UI::ctrl::advanced::reverbBrightnessSlider.w, UI::ctrl::advanced::reverbBrightnessSlider.h);

    syncNoteCombo.setBounds   ((int) UI::ctrl::facade::syncNoteCombo.x,   (int) UI::ctrl::facade::syncNoteCombo.y,   (int) UI::ctrl::facade::syncNoteCombo.w,   (int) UI::ctrl::facade::syncNoteCombo.h);
    syncFlavorCombo.setBounds ((int) UI::ctrl::facade::syncFlavorCombo.x, (int) UI::ctrl::facade::syncFlavorCombo.y, (int) UI::ctrl::facade::syncFlavorCombo.w, (int) UI::ctrl::facade::syncFlavorCombo.h);
}
