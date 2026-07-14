#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Transparent sous le seuil, sature en douceur au-dessus (jamais de clip dur) -
    // borne mathematiquement a ceiling + 1.0 quelle que soit l'ampleur de l'emballement.
    // Au-dela du seuil, un sursaut de crunch de quantification s'ajoute a la saturation,
    // proportionnel a l'ampleur du depassement : la saturation est ce qui limite
    // reellement (le bitcrush seul ne plafonne pas l'amplitude), le crunch lui donne
    // la personnalite de la machine plutot qu'un son de limiteur generique.
    inline float softSafetyCeiling (float x, float ceiling)
    {
        const float absX = std::abs (x);
        if (absX <= ceiling)
            return x;

        const float excess = absX - ceiling;
        const float sign = x < 0.0f ? -1.0f : 1.0f;
        const float saturated = ceiling + std::tanh (excess);

        const float crushAmount = juce::jlimit (0.0f, 1.0f, excess * 3.0f);
        const float bits = juce::jlimit (3.0f, 16.0f, 16.0f - crushAmount * 13.0f);
        const float levels = std::pow (2.0f, bits);
        const float crushed = std::round (saturated * levels) / levels;

        return sign * crushed;
    }
}

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
        juce::NormalisableRange<float> (1.0f, 6000.0f, 0.01f, 0.3f), 250.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "wetGain", 1 }, "Niveau repetitions",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "syncAmount", 1 }, "Sync accroche",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "syncNoteValue", 1 }, "Sync division",
        juce::StringArray { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" }, 2));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "syncFlavor", 1 }, "Sync saveur",
        juce::StringArray { "Droit", "Pointe", "Triolet" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbSize", 1 }, "Reverbe taille",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbCharacter", 1 }, "Reverbe caractere",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbMix", 1 }, "Reverbe mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbPreDelay", 1 }, "Reverbe predelai",
        juce::NormalisableRange<float> (0.0f, 150.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbTransient", 1 }, "Reverbe transient",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbHighpass", 1 }, "Reverbe HP",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 0.01f, 0.3f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbLowpass", 1 }, "Reverbe LP",
        juce::NormalisableRange<float> (500.0f, 20000.0f, 0.01f, 0.3f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbBrightness", 1 }, "Reverbe brightness",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "crossFeedback", 1 }, "Reinjection croisee",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outputGain", 1 }, "Volume sortie",
        juce::NormalisableRange<float> (-36.0f, 6.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

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

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "width", 1 }, "Largeur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "mono", 1 }, "Mono", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "safety", 1 }, "Securite", true));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bitcrushAmount", 1 }, "Bitcrush",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "decimationAmount", 1 }, "Decimation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "driftShape", 1 }, "Derive forme",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "integrity", 1 }, "Integrite",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f,
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
    return 8.0;
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

    delayLine.setMaximumDelayInSamples ((int) (8.0 * sampleRate));
    delayLine.prepare (spec);
    delayLine.reset();

    for (auto& d : fdnAllpassDelay)
    {
        d.setMaximumDelayInSamples ((int) (0.05 * sampleRate));
        d.prepare (spec);
        d.reset();
    }
    for (auto& d : fdnTankDelay)
    {
        d.setMaximumDelayInSamples ((int) (0.3 * sampleRate));
        d.prepare (spec);
        d.reset();
    }
    for (auto& row : fdnTankDampState)
        for (auto& v : row)
            v = 0.0f;

    preDelayLine.setMaximumDelayInSamples ((int) (0.2 * sampleRate));
    preDelayLine.prepare (spec);
    preDelayLine.reset();

    crossFeedDelayLine.setMaximumDelayInSamples ((int) (0.05 * sampleRate));
    crossFeedDelayLine.prepare (spec);
    crossFeedDelayLine.reset();

    const double smoothTime = 0.02;
    timeSmoothed.reset (sampleRate, smoothTime);
    feedbackSmoothed.reset (sampleRate, smoothTime);
    mixSmoothed.reset (sampleRate, smoothTime);
    driftRateSmoothed.reset (sampleRate, smoothTime);
    driftDepthSmoothed.reset (sampleRate, smoothTime);
    widthSmoothed.reset (sampleRate, smoothTime);
    driftShapeSmoothed.reset (sampleRate, smoothTime);
    integritySmoothed.reset (sampleRate, smoothTime);
    bitcrushAmountSmoothed.reset (sampleRate, smoothTime);
    decimationAmountSmoothed.reset (sampleRate, smoothTime);
    wetGainSmoothed.reset (sampleRate, smoothTime);
    syncAmountSmoothed.reset (sampleRate, smoothTime);
    reverbSizeSmoothed.reset (sampleRate, smoothTime);
    reverbCharacterSmoothed.reset (sampleRate, smoothTime);
    reverbMixSmoothed.reset (sampleRate, smoothTime);
    reverbPreDelaySmoothed.reset (sampleRate, smoothTime);
    reverbTransientSmoothed.reset (sampleRate, smoothTime);
    reverbHighpassSmoothed.reset (sampleRate, smoothTime);
    reverbLowpassSmoothed.reset (sampleRate, smoothTime);
    reverbBrightnessSmoothed.reset (sampleRate, smoothTime);
    crossFeedbackSmoothed.reset (sampleRate, smoothTime);
    outputGainSmoothed.reset (sampleRate, smoothTime);

    timeSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("time"));
    feedbackSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("feedback"));
    mixSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mix"));
    driftRateSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("driftRate"));
    driftDepthSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("driftDepth"));
    widthSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("width"));
    driftShapeSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("driftShape"));
    integritySmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("integrity"));
    bitcrushAmountSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("bitcrushAmount"));
    decimationAmountSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("decimationAmount"));
    wetGainSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("wetGain"));
    syncAmountSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("syncAmount"));
    reverbSizeSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbSize"));
    reverbCharacterSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbCharacter"));
    reverbMixSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbMix"));
    reverbPreDelaySmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbPreDelay"));
    reverbTransientSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbTransient"));
    reverbHighpassSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbHighpass"));
    reverbLowpassSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbLowpass"));
    reverbBrightnessSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbBrightness"));
    crossFeedbackSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("crossFeedback"));
    outputGainSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("outputGain"));

    for (int ch = 0; ch < 2; ++ch)
    {
        transientFastEnv[ch] = 0.0f;
        transientSlowEnv[ch] = 0.0f;
        reverbHpState[ch] = 0.0f;
        reverbLpState[ch] = 0.0f;
        tiltLpState[ch] = 0.0f;
        crossFeedCompEnv[ch] = 0.0f;
    }

    driftPhase = 0.0f;
    steppedGlideL = 0.0f;
    steppedGlideR = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        holdPhase[ch] = 0.0f;
        heldValue[ch] = 0.0f;
        decimLpState[ch] = 0.0f;
    }
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

    // Ecart introduit par Niveau repetitions (wetGain), isole de ce qui alimente la
    // Reverbe - rempli dans la boucle Moteur temps, rajoute apres la Reverbe.
    juce::AudioBuffer<float> wetGainCorrection (2, numSamples);
    wetGainCorrection.clear();

    // Les sliders ne changent que la valeur cible de l'APVTS : sans ces lignes,
    // les SmoothedValue restent figes sur l'etat du tout premier prepareToPlay
    // et ignorent tout ce que l'utilisateur regle ensuite dans l'interface.
    timeSmoothed.setTargetValue       (*apvts.getRawParameterValue ("time"));
    feedbackSmoothed.setTargetValue   (*apvts.getRawParameterValue ("feedback"));
    mixSmoothed.setTargetValue        (*apvts.getRawParameterValue ("mix"));
    driftRateSmoothed.setTargetValue  (*apvts.getRawParameterValue ("driftRate"));
    driftDepthSmoothed.setTargetValue (*apvts.getRawParameterValue ("driftDepth"));

    // Mono verrouille en amont : il annule l'effet de Largeur plutot que de sommer
    // la sortie apres coup, donc rien a annuler et Largeur garde sa position propre.
    const bool monoEngaged = *apvts.getRawParameterValue ("mono") > 0.5f;
    const bool safetyOn = *apvts.getRawParameterValue ("safety") > 0.5f;
    widthSmoothed.setTargetValue (monoEngaged ? 0.0f : *apvts.getRawParameterValue ("width"));
    driftShapeSmoothed.setTargetValue (*apvts.getRawParameterValue ("driftShape"));
    integritySmoothed.setTargetValue (*apvts.getRawParameterValue ("integrity"));
    bitcrushAmountSmoothed.setTargetValue (*apvts.getRawParameterValue ("bitcrushAmount"));
    decimationAmountSmoothed.setTargetValue (*apvts.getRawParameterValue ("decimationAmount"));
    wetGainSmoothed.setTargetValue (*apvts.getRawParameterValue ("wetGain"));
    syncAmountSmoothed.setTargetValue (*apvts.getRawParameterValue ("syncAmount"));
    reverbSizeSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbSize"));
    reverbCharacterSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbCharacter"));
    reverbMixSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbMix"));
    reverbPreDelaySmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbPreDelay"));
    reverbTransientSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbTransient"));
    reverbHighpassSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbHighpass"));
    reverbLowpassSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbLowpass"));
    reverbBrightnessSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbBrightness"));
    crossFeedbackSmoothed.setTargetValue (*apvts.getRawParameterValue ("crossFeedback"));
    const float crossFeedAmt = crossFeedbackSmoothed.getNextValue();
    const float crossFeedDelaySamples = 0.02f * (float) currentSampleRate;
    outputGainSmoothed.setTargetValue (*apvts.getRawParameterValue ("outputGain"));

    // Sync tempo : calculee une fois par bloc (le tempo hote ne bouge pas a l'echantillon
    // pres), verrouillage direct sur la division+saveur choisie plutot qu'une recherche
    // de la plus proche - a 100% d'accroche, c'est un vrai delay synchronise classique.
    double hostBpm = 120.0;
    if (auto* hostPlayHead = getPlayHead())
        if (auto position = hostPlayHead->getPosition())
            if (auto bpm = position->getBpm())
                hostBpm = *bpm;

    static constexpr float noteBeats[6] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    const int noteIndex = juce::jlimit (0, 5, (int) *apvts.getRawParameterValue ("syncNoteValue"));
    const int flavorIndex = (int) *apvts.getRawParameterValue ("syncFlavor");

    const float quarterNoteMs = (float) (60000.0 / juce::jmax (hostBpm, 1.0));
    float syncedTimeMs = quarterNoteMs * noteBeats[noteIndex];
    if (flavorIndex == 1)
        syncedTimeMs *= 1.5f;
    else if (flavorIndex == 2)
        syncedTimeMs *= (2.0f / 3.0f);
    syncedTimeMs = juce::jlimit (1.0f, 6000.0f, syncedTimeMs);

    // Moteur temps : ligne a retard modulee par la Derive, avec reinjection et mix.
    // La derive fait osciller le temps de lecture autour de la valeur cible : c'est
    // ce mouvement lui-meme qui produit le wow/flutter, pas un effet ajoute a part.
    // Largeur decorrele les phases gauche/droite (dephasage jusqu'a l'opposition totale
    // a 0.5 cycle) : une vraie bande n'a qu'un seul defilement, donc au repos (Largeur=0
    // ou Mono actif) les deux canaux restent parfaitement synchrones.
    // Derive forme : melange entre glissement lisse (sinus) et pitch a paliers (phase
    // quantifiee sur quelques crans, facon AMS DMX/Publison) - les deux pales du meme
    // mouvement plutot que deux mecanismes separes.
    constexpr int driftSteps = 6;

    for (int n = 0; n < numSamples; ++n)
    {
        const float timeMs      = timeSmoothed.getNextValue();
        const float feedbackAmt = feedbackSmoothed.getNextValue();
        const float mixAmt      = mixSmoothed.getNextValue();
        const float rateHz      = driftRateSmoothed.getNextValue();
        const float depthAmt    = driftDepthSmoothed.getNextValue();
        const float widthAmt    = widthSmoothed.getNextValue();
        const float shapeAmt    = driftShapeSmoothed.getNextValue();
        const float integrityAmt = integritySmoothed.getNextValue();
        const float bitcrushAmt  = bitcrushAmountSmoothed.getNextValue();
        const float decimAmount  = decimationAmountSmoothed.getNextValue();
        const float wetGainDb    = wetGainSmoothed.getNextValue();
        const float wetGainLin   = juce::Decibels::decibelsToGain (wetGainDb);
        const float syncAmt      = syncAmountSmoothed.getNextValue();

        const float effectiveTimeMs = timeMs * (1.0f - syncAmt) + syncedTimeMs * syncAmt;

        driftPhase += rateHz / (float) currentSampleRate;
        if (driftPhase >= 1.0f)
            driftPhase -= 1.0f;

        float driftPhaseR = driftPhase + widthAmt * 0.5f;
        if (driftPhaseR >= 1.0f)
            driftPhaseR -= 1.0f;

        const float lfoSmoothL = std::sin (driftPhase  * juce::MathConstants<float>::twoPi);
        const float lfoSmoothR = std::sin (driftPhaseR * juce::MathConstants<float>::twoPi);

        // Cible a paliers : la valeur vers laquelle on glisse, jamais utilisee brute -
        // sinon le temps de delay saute d'un coup et ca clique au lieu de faire un palier.
        const float quantPhaseL = std::floor (driftPhase  * driftSteps) / (float) driftSteps;
        const float quantPhaseR = std::floor (driftPhaseR * driftSteps) / (float) driftSteps;
        const float targetSteppedL = std::sin (quantPhaseL * juce::MathConstants<float>::twoPi);
        const float targetSteppedR = std::sin (quantPhaseR * juce::MathConstants<float>::twoPi);

        const float stepDurationSamples = (float) currentSampleRate / juce::jmax (rateHz * (float) driftSteps, 0.001f);
        const float glideTimeSamples = juce::jmax (1.0f, stepDurationSamples * 0.15f);
        const float glideCoeff = 1.0f - std::exp (-1.0f / glideTimeSamples);

        steppedGlideL += (targetSteppedL - steppedGlideL) * glideCoeff;
        steppedGlideR += (targetSteppedR - steppedGlideR) * glideCoeff;

        const float lfoL = lfoSmoothL * (1.0f - shapeAmt) + steppedGlideL * shapeAmt;
        const float lfoR = lfoSmoothR * (1.0f - shapeAmt) + steppedGlideR * shapeAmt;

        const float modDepthMs = depthAmt * effectiveTimeMs * 0.2f;
        const float delaySamplesL = juce::jmax (0.1f, effectiveTimeMs + lfoL * modDepthMs) * 0.001f * (float) currentSampleRate;
        const float delaySamplesR = juce::jmax (0.1f, effectiveTimeMs + lfoR * modDepthMs) * 0.001f * (float) currentSampleRate;
        const float channelDelaySamples[2] = { delaySamplesL, delaySamplesR };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const float inputSample = data[n];

            const float delayedSample = delayLine.popSample (ch, channelDelaySamples[ch]);
            float toPush = inputSample + delayedSample * feedbackAmt;

            // Reinjection croisee : la traine humide de la Reverbe (avec ~20ms de retard
            // fixe, cf. commentaire du membre) revient se faire attraper par le Moteur
            // temps. A zero, chaine propre ; pousse, generateur ambiant qui s'auto-
            // entretient (delay et reverbe qui se parlent, facon Roland RE-201).
            toPush += crossFeedDelayLine.popSample (ch, crossFeedDelaySamples) * crossFeedAmt;

            // Integrite : dégrade ce qui entre dans la mémoire de la boucle, pas le
            // direct. Chaque passage réinjecte le résultat déjà dégradé, donc les
            // répétitions s'assalissent progressivement plutôt que d'un coup.
            if (integrityAmt > 0.0001f)
            {
                // La macro pilote l'intensite globale ; chaque balance individuelle
                // (panneau Advanced) redose la part de chaque mecanisme dans ce total.
                const float decimAmt = integrityAmt * decimAmount;
                const float crushAmt = integrityAmt * bitcrushAmt;

                const float holdSamples = 1.0f + decimAmt * 28.0f;
                holdPhase[ch] += 1.0f / holdSamples;
                if (holdPhase[ch] >= 1.0f)
                {
                    holdPhase[ch] -= 1.0f;
                    heldValue[ch] = toPush;
                }
                toPush = heldValue[ch];

                // La decimation cree des harmoniques inharmoniques, percues comme
                // plus dures/fortes qu'un simple crunch a niveau egal. Un passe-bas
                // qui s'assombrit avec la quantite adoucit ce cote agressif sans
                // reduire le volume ni attenuer le caractere lui-meme.
                const float decimCutoffHz = 18000.0f - decimAmt * 14000.0f;
                const float decimLpCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * decimCutoffHz / (float) currentSampleRate);
                decimLpState[ch] += (toPush - decimLpState[ch]) * decimLpCoeff;
                toPush = decimLpState[ch];

                const float absVal = std::abs (toPush);
                const float lowLevelBoost = juce::jlimit (0.0f, 1.0f, 1.0f - absVal * 4.0f) * crushAmt;
                const float bits = juce::jlimit (2.0f, 16.0f, 16.0f - crushAmt * 11.0f - lowLevelBoost * 4.0f);
                const float levels = std::pow (2.0f, bits);
                toPush = std::round (toPush * levels) / levels;
            }

            if (safetyOn)
                toPush = softSafetyCeiling (toPush, 1.2f);

            delayLine.pushSample (ch, toPush);

            // Niveau des repetitions : NE doit affecter que la sortie finale, pas ce qui
            // alimente la Reverbe (routage serial) - sinon impossible de doser Moteur
            // temps et Reverbe independamment. data[n] reste neutre (sans wetGain) pour
            // que la Reverbe recoive toujours le meme signal ; l'ecart introduit par
            // wetGain est isole et rajoute seulement apres la Reverbe, tout a la fin.
            const float neutralOut = inputSample * (1.0f - mixAmt) + delayedSample * mixAmt;
            wetGainCorrection.setSample (ch, n, delayedSample * mixAmt * (wetGainLin - 1.0f));

            float outSample = neutralOut;
            if (safetyOn)
                outSample = softSafetyCeiling (outSample, 1.2f);
            data[n] = outSample;
        }
    }

    // --- Reverbe (v0 : moteur JUCE integre en attendant le reseau custom) ---
    // Recoit la sortie du Moteur temps (routage serial, coherent avec la future
    // Reinjection croisee). Caractere n'est qu'une version simplifiee (damping seul)
    // du futur axe structurel du reseau maison - assume comme placeholder honnete.
    const float reverbSizeVal   = reverbSizeSmoothed.getNextValue();
    const float reverbCharVal   = reverbCharacterSmoothed.getNextValue();
    const float reverbMixVal    = reverbMixSmoothed.getNextValue();
    const float preDelayMs      = reverbPreDelaySmoothed.getNextValue();
    const float transientAmt    = reverbTransientSmoothed.getNextValue();
    const float highpassHz      = reverbHighpassSmoothed.getNextValue();
    const float lowpassHz       = reverbLowpassSmoothed.getNextValue();
    const float brightnessVal   = reverbBrightnessSmoothed.getNextValue();

    // FDN : Caractere pilote l'amortissement (cristallin=aigus preserves, casse=sombre) ;
    // Taille/Decroissance pilote un temps de decroissance (RT60-like) commun aux 4 lignes,
    // chacune avec le gain de reinjection qui lui correspond compte tenu de sa longueur.
    const float fdnDampCutoffHz = juce::jmap (reverbCharVal, 0.0f, 1.0f, 16000.0f, 1500.0f);
    const float fdnDampCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fdnDampCutoffHz / (float) currentSampleRate);
    const float fdnDecaySeconds = juce::jmap (reverbSizeVal, 0.0f, 1.0f, 0.3f, 8.0f);

    // Temps espaces sans rapports simples entre eux (evite les recoupements qui
    // creaient une resonance isolee trop marquee) et allonges (plus de densite
    // modale, moins de flutter-echo audible, sensation de piece plus grande).
    static constexpr float fdnAllpassTimesMs[4] = { 5.3f, 8.9f, 13.1f, 17.9f };
    static constexpr float fdnTankTimesMs[4]    = { 53.3f, 67.1f, 79.7f, 96.9f };
    constexpr float fdnAllpassCoeff = 0.6f;

    float fdnAllpassSamples[4], fdnTankSamples[4], fdnFeedbackGain[4];
    for (int i = 0; i < 4; ++i)
    {
        fdnAllpassSamples[i] = fdnAllpassTimesMs[i] * 0.001f * (float) currentSampleRate;
        fdnTankSamples[i]    = fdnTankTimesMs[i]    * 0.001f * (float) currentSampleRate;
        const float lineTimeSeconds = fdnTankTimesMs[i] * 0.001f;
        fdnFeedbackGain[i] = std::pow (10.0f, -3.0f * lineTimeSeconds / fdnDecaySeconds);
    }

    juce::AudioBuffer<float> reverbDry;
    reverbDry.makeCopyOf (buffer, true);

    // Etages d'entree : Transient (adoucit l'attaque courte pour une reverbe plus
    // ambiante) -> Input HP/LP (evite l'engorgement grave/aigu dans le reservoir) ->
    // Pre-delai. Tous independants du moteur, ne bougeront pas au remplacement par le FDN.
    const float transientFastCoeff = 1.0f - std::exp (-1.0f / (0.003f * (float) currentSampleRate));
    const float transientSlowCoeff = 1.0f - std::exp (-1.0f / (0.08f  * (float) currentSampleRate));
    const float hpCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * highpassHz / (float) currentSampleRate);
    const float lpCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * lowpassHz  / (float) currentSampleRate);
    const float preDelaySamples = preDelayMs * 0.001f * (float) currentSampleRate;

    // Compresseur local de la Reinjection croisee (Option C, reglages internes, pas de
    // parametre utilisateur separe). Seuil/ratio evoluent avec le bouton sur toute sa
    // course (plus sensible/doux a 0%, jusqu'aux valeurs de reference a 70%, puis la
    // MEME rampe continue au-dela au lieu de figer - hypothese testee le 2026-07-13 a
    // la place d'une modulation ajoutee : prolonger la tendance deja validee de 0-70%
    // plutot que d'ajouter un mecanisme separe pour la zone haute).
    const float compRampPos = crossFeedAmt / 0.7f;
    const float compThreshold = juce::jmap (compRampPos, 0.0f, 1.0f, 0.15f, 0.4f);
    const float compRatio     = juce::jmap (compRampPos, 0.0f, 1.0f, 2.5f, 8.0f);
    const float compAttackCoeff  = 1.0f - std::exp (-1.0f / (0.003f * (float) currentSampleRate));
    const float compReleaseCoeff = 1.0f - std::exp (-1.0f / (0.1f   * (float) currentSampleRate));

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            float x = data[n];

            const float absX = std::abs (x);
            transientFastEnv[ch] += (absX - transientFastEnv[ch]) * transientFastCoeff;
            transientSlowEnv[ch] += (absX - transientSlowEnv[ch]) * transientSlowCoeff;
            const float spike = juce::jmax (0.0f, transientFastEnv[ch] - transientSlowEnv[ch]);
            const float duck = juce::jlimit (0.0f, 1.0f, transientAmt * spike * 6.0f);
            x *= (1.0f - duck);

            reverbHpState[ch] += (x - reverbHpState[ch]) * hpCoeff;
            x = x - reverbHpState[ch];

            reverbLpState[ch] += (x - reverbLpState[ch]) * lpCoeff;
            x = reverbLpState[ch];

            const float preDelayed = preDelayLine.popSample (ch, preDelaySamples);
            preDelayLine.pushSample (ch, x);

            data[n] = preDelayed;
        }
    }

    for (int n = 0; n < numSamples; ++n)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            float diffused = data[n];

            // Pre-diffusion : 4 allpass Schroeder en serie, brouille les premieres
            // reflexions sans colorer le spectre (structure allpass = gain unite a
            // toute frequence par construction).
            for (int i = 0; i < 4; ++i)
            {
                const float wD = fdnAllpassDelay[i].popSample (ch, fdnAllpassSamples[i]);
                const float w  = diffused + fdnAllpassCoeff * wD;
                fdnAllpassDelay[i].pushSample (ch, w);
                diffused = -fdnAllpassCoeff * w + wD;
            }

            // Reservoir : 4 lignes rebouclees via une matrice de Householder (H = I -
            // (2/N)*ones(N,N)) - preserve l'energie, stable par construction tant que
            // chaque gain de ligne reste <1 (garanti par la formule de decroissance).
            float tankRaw[4], tankFed[4];
            for (int i = 0; i < 4; ++i)
            {
                tankRaw[i] = fdnTankDelay[i].popSample (ch, fdnTankSamples[i]);
                fdnTankDampState[i][ch] += (tankRaw[i] - fdnTankDampState[i][ch]) * fdnDampCoeff;
                tankFed[i] = fdnTankDampState[i][ch] * fdnFeedbackGain[i];
            }
            const float tankSum = tankFed[0] + tankFed[1] + tankFed[2] + tankFed[3];
            for (int i = 0; i < 4; ++i)
                fdnTankDelay[i].pushSample (ch, tankFed[i] - 0.5f * tankSum + diffused);

            // Tap de sortie pris apres l'amortissement (pas le signal brut de la ligne) :
            // chaque echo hérite du filtrage au lieu de rester plein-spectre, ce qui
            // attenue le cote "cloche/metallique" percu sur les premieres reflexions.
            data[n] = 0.25f * (fdnTankDampState[0][ch] + fdnTankDampState[1][ch]
                              + fdnTankDampState[2][ch] + fdnTankDampState[3][ch]);
        }
    }

    // Brightness : tilt sur le humide seul (jamais le direct) - separation lisse/aigu
    // via un simple passe-bas, puis rebalance des deux bandes.
    const float tiltCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 900.0f / (float) currentSampleRate);
    const float lowGain  = 1.0f - brightnessVal * 0.6f;
    const float highGain = 1.0f + brightnessVal * 0.6f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* dryData = reverbDry.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n)
        {
            float wet = data[n];
            tiltLpState[ch] += (wet - tiltLpState[ch]) * tiltCoeff;
            const float lowBand  = tiltLpState[ch];
            const float highBand = wet - lowBand;
            wet = lowBand * lowGain + highBand * highGain;

            // Reinjection croisee ponderee par Reverbe mix (Option A) : capturee APRES
            // la mise a l'echelle par reverbMixVal plutot qu'avant, pour que ce bouton
            // agisse aussi comme un levier naturel sur la boucle croisee - le baisser
            // calme la reinjection aussi, pas seulement ce qu'on entend en direct.
            const float wetForMix = wet * reverbMixVal;

            // Compression locale (Option C) : controle ce qui entre dans la memoire de
            // la boucle des le depart, plutot qu'un plafond qui n'agit qu'a l'extreme.
            // Seuil/ratio/temps fixes en interne, pas exposes a l'utilisateur.
            const float compInputLevel = std::abs (wetForMix);
            const float compCoeff = (compInputLevel > crossFeedCompEnv[ch]) ? compAttackCoeff : compReleaseCoeff;
            crossFeedCompEnv[ch] += (compInputLevel - crossFeedCompEnv[ch]) * compCoeff;

            float compGain = 1.0f;
            if (crossFeedCompEnv[ch] > compThreshold)
            {
                const float excessDb = juce::Decibels::gainToDecibels (crossFeedCompEnv[ch] / compThreshold);
                const float reductionDb = excessDb * (1.0f - 1.0f / compRatio);
                compGain = juce::Decibels::decibelsToGain (-reductionDb);
            }

            crossFeedDelayLine.pushSample (ch, wetForMix * compGain);

            float mixed = dryData[n] * (1.0f - reverbMixVal) + wetForMix
                        + wetGainCorrection.getSample (ch, n);
            if (safetyOn)
                mixed = softSafetyCeiling (mixed, 1.2f);

            // Volume de sortie : tout en fin de chaine, apres la Securite - controle
            // d'ecoute pur, independant de ce qui protege contre l'emballement interne.
            const float outputGainLin = juce::Decibels::decibelsToGain (outputGainSmoothed.getNextValue());
            data[n] = mixed * outputGainLin;
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
