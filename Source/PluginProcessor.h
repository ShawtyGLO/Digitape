#pragma once

#include <JuceHeader.h>

// Digitape — Moteur temps (v0) : ligne a retard a vitesse modulee, avec reinjection et derive.
class DigitapeAudioProcessor : public juce::AudioProcessor
{
public:
    DigitapeAudioProcessor();
    ~DigitapeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    double currentSampleRate { 44100.0 };
    float driftPhase { 0.0f };
    float steppedGlideL { 0.0f }, steppedGlideR { 0.0f };

    // Integrite : crunch de quantification (+ severe a bas niveau) et decimation
    // sample-and-hold, appliques a ce qui entre dans la memoire de la boucle.
    // holdPhase est un accumulateur continu (pas un compteur entier) pour eviter
    // que la duree de maintien saute par paliers quand le parametre varie.
    float holdPhase[2] { 0.0f, 0.0f };
    float heldValue[2] { 0.0f, 0.0f };
    float decimLpState[2] { 0.0f, 0.0f };

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 192000 };

    // Reverbe v0 : moteur JUCE integre, remplace plus tard par le reseau FDN maison.
    juce::dsp::Reverb reverb;
    juce::SmoothedValue<float> reverbSizeSmoothed, reverbCharacterSmoothed, reverbMixSmoothed;

    // Etages autour du moteur de reverbe (Transient, Input filter, Pre-delai, Brightness) -
    // ne touchent jamais le moteur lui-meme, survivront tels quels au reseau FDN.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayLine { 96000 };
    float transientFastEnv[2] { 0.0f, 0.0f };
    float transientSlowEnv[2] { 0.0f, 0.0f };
    float reverbHpState[2]    { 0.0f, 0.0f };
    float reverbLpState[2]    { 0.0f, 0.0f };
    float tiltLpState[2]      { 0.0f, 0.0f };

    juce::SmoothedValue<float> reverbPreDelaySmoothed, reverbTransientSmoothed,
                                reverbHighpassSmoothed, reverbLowpassSmoothed, reverbBrightnessSmoothed;

    // Reinjection croisee : la sortie humide de la Reverbe (post-Brightness, avant le
    // melange Mix) revient dans la boucle du Moteur temps. Une vraie ligne a retard a
    // delai fixe (20ms) plutot qu'un tampon indexe par bloc : robuste a toute taille de
    // bloc variable de l'hote (l'indexation par bloc causait des clics des que la taille
    // changeait d'un appel a l'autre - notamment en Standalone hors ASIO).
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> crossFeedDelayLine { 4800 };
    juce::SmoothedValue<float> crossFeedbackSmoothed;

    juce::SmoothedValue<float> timeSmoothed, feedbackSmoothed, mixSmoothed, driftRateSmoothed, driftDepthSmoothed, widthSmoothed, driftShapeSmoothed, integritySmoothed;

    // Panneau Advanced (v0 en sliders plats, pas encore de repli visuel) : balance
    // individuelle entre mecanismes de l'Integrite, la macro pilotant toujours
    // l'intensite globale.
    juce::SmoothedValue<float> bitcrushAmountSmoothed, decimationAmountSmoothed;

    juce::SmoothedValue<float> wetGainSmoothed;

    // Sync tempo : accroche continue vers une division rythmique choisie (pas une
    // recherche de la plus proche) - a 100%, le temps se verrouille purement sur
    // cette division ; entre 0 et 100%, glissement entre le temps libre et la cible.
    juce::SmoothedValue<float> syncAmountSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DigitapeAudioProcessor)
};
