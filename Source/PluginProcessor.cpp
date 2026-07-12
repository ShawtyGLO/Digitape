#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DigitapeAudioProcessor::DigitapeAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

DigitapeAudioProcessor::~DigitapeAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout DigitapeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "time", 1 }, "Temps",
        juce::NormalisableRange<float> (1.0f, 2000.0f, 0.01f, 0.3f), 250.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "feedback", 1 }, "Reinjection",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "driftRate", 1 }, "Derive freq",
        juce::NormalisableRange<float> (0.02f, 8.0f, 0.001f, 0.3f), 0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "driftDepth", 1 }, "Derive profondeur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String DigitapeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DigitapeAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool DigitapeAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool DigitapeAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double DigitapeAudioProcessor::getTailLengthSeconds() const
{
    return 2.0;
}

int DigitapeAudioProcessor::getNumPrograms()
{
    return 1;
}

int DigitapeAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DigitapeAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String DigitapeAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void DigitapeAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void DigitapeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 2;

    delayLine.setMaximumDelayInSamples ((int) (3.0 * sampleRate));
    delayLine.prepare (spec);
    delayLine.reset();

    const double smoothTime = 0.02;
    timeSmoothed.reset (sampleRate, smoothTime);
    feedbackSmoothed.reset (sampleRate, smoothTime);
    mixSmoothed.reset (sampleRate, smoothTime);
    driftRateSmoothed.reset (sampleRate, smoothTime);
    driftDepthSmoothed.reset (sampleRate, smoothTime);

    timeSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("time"));
    feedbackSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("feedback"));
    mixSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mix"));
    driftRateSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("driftRate"));
    driftDepthSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("driftDepth"));

    driftPhase = 0.0f;
}

void DigitapeAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DigitapeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void DigitapeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    // Moteur temps : ligne a retard modulee par la Derive, avec reinjection et mix.
    // La derive fait osciller le temps de lecture autour de la valeur cible : c'est
    // ce mouvement lui-meme qui produit le wow/flutter, pas un effet ajoute a part.
    for (int n = 0; n < numSamples; ++n)
    {
        const float timeMs      = timeSmoothed.getNextValue();
        const float feedbackAmt = feedbackSmoothed.getNextValue();
        const float mixAmt      = mixSmoothed.getNextValue();
        const float rateHz      = driftRateSmoothed.getNextValue();
        const float depthAmt    = driftDepthSmoothed.getNextValue();

        driftPhase += rateHz / (float) currentSampleRate;
        if (driftPhase >= 1.0f)
            driftPhase -= 1.0f;
        const float lfo = std::sin (driftPhase * juce::MathConstants<float>::twoPi);

        const float modDepthMs = depthAmt * timeMs * 0.2f;
        const float modulatedTimeMs = juce::jmax (0.1f, timeMs + lfo * modDepthMs);
        const float delaySamples = modulatedTimeMs * 0.001f * (float) currentSampleRate;

        delayLine.setDelay (delaySamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float inputSample = data[n];

            const float delayedSample = delayLine.popSample (ch);
            const float toPush = inputSample + delayedSample * feedbackAmt;
            delayLine.pushSample (ch, toPush);

            data[n] = inputSample * (1.0f - mixAmt) + delayedSample * mixAmt;
        }
    }
}

//==============================================================================
bool DigitapeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* DigitapeAudioProcessor::createEditor()
{
    return new DigitapeAudioProcessorEditor (*this);
}

//==============================================================================
void DigitapeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void DigitapeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DigitapeAudioProcessor();
}
