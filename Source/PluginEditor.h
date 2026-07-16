#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Palette Digitape (maquette v5) : creme + rouille + brun, ecran rouille.
namespace Skin
{
    const juce::Colour panelHi   { 0xffddceac };
    const juce::Colour panel     { 0xffd0c09a };
    const juce::Colour panelLo   { 0xffb7a67f };
    const juce::Colour ink       { 0xff2b2318 };
    const juce::Colour inkDim    { 0xff6a5c46 };
    const juce::Colour rust      { 0xffc0562b };
    const juce::Colour rustHi    { 0xffdd7a4a };
    const juce::Colour rustLo    { 0xff8f3d1d };
    const juce::Colour knobBody  { 0xff2a231c };
    const juce::Colour knobEdge  { 0xff14100c };
    const juce::Colour pointer   { 0xffefe4cf };
    const juce::Colour scrBg     { 0xff120b06 };
    const juce::Colour scrInk    { 0xffe58a41 };
    const juce::Colour scrDim    { 0xff9c5c2e };
    const juce::Colour pcb       { 0xff1d4a39 };
    const juce::Colour pcbLo     { 0xff123125 };
    const juce::Colour copper    { 0xffcaa24e };
    const juce::Colour silk      { 0xffe6ecdf };
}

//==============================================================================
// LookAndFeel : knobs sombres a index creme, sur panneau creme.
class DigitapeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DigitapeLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

//==============================================================================
// Filtre HP + LP en UN SEUL controle (design Arturia) : une barre horizontale avec deux
// poignees glissables. Remplace deux knobs et montre la bande passante d'un coup d'oeil.
class FilterRangeSlider : public juce::Component
{
public:
    FilterRangeSlider (juce::AudioProcessorValueTreeState& state,
                       const juce::String& hpParamID, const juce::String& lpParamID,
                       const juce::String& titleText);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    // Les deux parametres ont des plages DIFFERENTES (HP 20-2k, LP 500-20k). Les poser
    // chacun sur sa propre normalisation 0-1 de la meme barre serait faux : leurs positions
    // ne correspondraient pas aux memes frequences. On les place donc sur un AXE LOG COMMUN
    // 20 Hz -> 20 kHz, ce qui rend la bande passante affichee reelle et le non-croisement naturel.
    static constexpr float axisLo = 20.0f, axisHi = 20000.0f;
    static float axisPos (float hz);
    static float axisHz  (float pos);

    juce::Rectangle<float> trackBounds() const;
    float xForHz (float hz) const;
    float hzForX (float x) const;
    int   nearestHandle (float x) const;

    juce::RangedAudioParameter* hpParam { nullptr };
    juce::RangedAudioParameter* lpParam { nullptr };
    std::unique_ptr<juce::ParameterAttachment> hpAttach, lpAttach;
    float hpHz { 20.0f }, lpHz { 20000.0f };
    int dragging { -1 };            // -1 = aucun, 0 = HP, 1 = LP
    juce::String title;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterRangeSlider)
};

//==============================================================================
// UI V01 : rangee par zones (Preampli / Delay / Reverbe / Master) d'apres la maquette v5.
// Le capot Advanced s'ouvre vers le HAUT et revele le circuit, sans masquer la facade.
class DigitapeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DigitapeAudioProcessorEditor (DigitapeAudioProcessor&);
    ~DigitapeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct KnobWithLabel
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (KnobWithLabel&, const juce::String& paramID, const juce::String& labelText);
    static void placeKnob (KnobWithLabel&, juce::Rectangle<int> area);
    void setShowingAdvanced (bool);
    void drawZone (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour accent) const;

    DigitapeAudioProcessor& audioProcessor;
    DigitapeLookAndFeel lnf;

    // --- Facade ---
    KnobWithLabel saturationKnob;                                              // Preampli
    KnobWithLabel timeKnob, feedbackKnob, wetGainKnob, syncAmountKnob;         // Delay
    KnobWithLabel reverbSizeKnob, reverbDecayKnob, reverbCharacterKnob, reverbMixKnob, reverbModulationKnob; // Reverbe
    KnobWithLabel mixKnob, outputGainKnob, integrityKnob, reactivityKnob, duckingKnob; // Master

    std::unique_ptr<FilterRangeSlider> delayFilter, reverbFilter;

    juce::ComboBox saturationTypeCombo;
    std::unique_ptr<ComboBoxAttachment> saturationTypeAttachment;
    juce::ToggleButton monoButton { "Mono" }, safetyButton { "Securite" };
    std::unique_ptr<ButtonAttachment> monoAttachment, safetyAttachment;

    // --- Capot (Advanced) ---
    KnobWithLabel driftRateKnob, driftDepthKnob, driftShapeKnob, widthKnob;    // Derive
    KnobWithLabel bitcrushAmountKnob, decimationAmountKnob;                    // Integrite
    KnobWithLabel saturationEmphasisKnob;                                      // Preampli
    KnobWithLabel reverbPreDelayKnob, reverbTransientKnob, reverbBrightnessKnob; // Reverbe
    KnobWithLabel crossFeedbackKnob;
    juce::ComboBox syncNoteCombo, syncFlavorCombo;
    std::unique_ptr<ComboBoxAttachment> syncNoteAttachment, syncFlavorAttachment;

    juce::TextButton advancedToggle { "Capot" };
    bool showingAdvanced { false };

    // Zones calculees dans resized(), relues dans paint().
    juce::Rectangle<int> hoodArea, faceArea, zPreamp, zDelay, zReverb, zMaster;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitapeAudioProcessorEditor)
};
