#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Interface rangee via Plugin Factory (tools/plugin-factory.html -> tools/layout.json
// -> Source/Generated/UILayout.h). Toujours provisoire visuellement (pas de skin), mais
// les positions viennent desormais du rangement choisi plutot que d'une grille automatique.
class DigitapeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DigitapeAudioProcessorEditor (DigitapeAudioProcessor&);
    ~DigitapeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct KnobWithLabel
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    DigitapeAudioProcessor& audioProcessor;

    juce::Label titleLabel;

    // Facade
    KnobWithLabel timeKnob, feedbackKnob, mixKnob, driftRateKnob, driftDepthKnob, widthKnob, driftShapeKnob,
                  integrityKnob, wetGainKnob, syncAmountKnob, reverbSizeKnob, reverbCharacterKnob, reverbMixKnob;
    juce::ToggleButton monoButton { "Mono" };
    std::unique_ptr<ButtonAttachment> monoAttachment;
    juce::ToggleButton safetyButton { "Securite" };
    std::unique_ptr<ButtonAttachment> safetyAttachment;

    // Advanced (deplie via advancedToggle)
    KnobWithLabel bitcrushAmountKnob, decimationAmountKnob;
    juce::ComboBox syncNoteCombo, syncFlavorCombo;
    std::unique_ptr<ComboBoxAttachment> syncNoteAttachment, syncFlavorAttachment;

    juce::TextButton advancedToggle { "Advanced" };
    bool showingAdvanced { false };
    void setShowingAdvanced (bool shouldShow);

    void setupKnob (KnobWithLabel& knob, const juce::String& paramID, const juce::String& labelText);
    static void placeKnob (KnobWithLabel& knob, float x, float y, float w, float h);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitapeAudioProcessorEditor)
};
