#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Transparent sous le seuil, sature en douceur au-dessus (jamais de clip dur) -
    // borne mathematiquement a ceiling + 1.0 quelle que soit l'ampleur de l'emballement.
    inline float softSafetyCeiling (float x, float ceiling)
    {
        const float absX = std::abs (x);
        if (absX <= ceiling)
            return x;
        const float sign = x < 0.0f ? -1.0f : 1.0f;
        return sign * (ceiling + std::tanh (absX - ceiling));
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

            // Niveau des repetitions : trim independant du Mix, applique seulement a la
            // sortie (jamais a ce qui repart dans la boucle) pour ne pas interferer avec
            // la stabilite du feedback ni avec la Securite.
            float outSample = inputSample * (1.0f - mixAmt) + delayedSample * wetGainLin * mixAmt;
            if (safetyOn)
                outSample = softSafetyCeiling (outSample, 1.2f);
            data[n] = outSample;
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
