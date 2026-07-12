#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DigitapeAudioProcessorEditor::DigitapeAudioProcessorEditor (DigitapeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setupKnob (timeKnob,       "time",       "Temps");
    setupKnob (feedbackKnob,   "feedback",   "Reinjection");
    setupKnob (mixKnob,        "mix",        "Mix");
    setupKnob (driftRateKnob,  "driftRate",  "Derive freq");
    setupKnob (driftDepthKnob, "driftDepth", "Derive prof.");
    setupKnob (widthKnob,      "width",      "Largeur");
    setupKnob (driftShapeKnob, "driftShape", "Derive forme");
    setupKnob (integrityKnob,  "integrity",  "Integrite");
    setupKnob (bitcrushAmountKnob,   "bitcrushAmount",   "Bitcrush (adv)");
    setupKnob (decimationAmountKnob, "decimationAmount", "Decimation (adv)");

    addAndMakeVisible (monoButton);
    monoAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "mono", monoButton);

    addAndMakeVisible (safetyButton);
    safetyAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, "safety", safetyButton);

    setSize (1040, 220);
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
    addAndMakeVisible (knob.label);
}

//==============================================================================
void DigitapeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText ("Digitape - Moteur temps (v0)", getLocalBounds().removeFromTop (24),
                       juce::Justification::centred, 1);
}

void DigitapeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop (28).reduced (12);

    KnobWithLabel* knobs[] = { &timeKnob, &feedbackKnob, &mixKnob, &driftRateKnob, &driftDepthKnob, &widthKnob, &driftShapeKnob,
                               &integrityKnob, &bitcrushAmountKnob, &decimationAmountKnob };
    const int numKnobs = (int) std::size (knobs);
    const int columnWidth = area.getWidth() / numKnobs;

    for (int i = 0; i < numKnobs; ++i)
    {
        auto column = area.removeFromLeft (columnWidth);
        knobs[i]->label.setBounds (column.removeFromTop (20));

        if (knobs[i] == &widthKnob)
            monoButton.setBounds (column.removeFromBottom (24));
        else if (knobs[i] == &integrityKnob)
            safetyButton.setBounds (column.removeFromBottom (24));

        knobs[i]->slider.setBounds (column.reduced (6));
    }
}
