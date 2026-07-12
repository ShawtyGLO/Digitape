#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// Interface brute (sliders generiques) pour valider le DSP avant tout travail visuel.
class DigitapeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DigitapeAudioProcessorEditor (DigitapeAudioProcessor&);
    ~DigitapeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct KnobWithLabel
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    DigitapeAudioProcessor& audioProcessor;

    KnobWithLabel timeKnob, feedbackKnob, mixKnob, driftRateKnob, driftDepthKnob;

    void setupKnob (KnobWithLabel& knob, const juce::String& paramID, const juce::String& labelText);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitapeAudioProcessorEditor)
};
