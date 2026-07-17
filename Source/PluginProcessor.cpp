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

    // Integrite : sample-and-hold (decimation) + passe-bas qui s'assombrit avec la
    // quantite + crunch de quantification (bitcrush, plus severe a bas niveau). L'etat
    // par canal est passe par reference pour pouvoir instancier plusieurs etages
    // independants (boucle echo ET bus d'effets) sans melanger leurs etats.
    inline float applyIntegrity (float x, float integrityAmt, float bitcrushAmt, float decimAmount,
                                 float& holdPhase, float& heldValue, float& decimLpState, double sampleRate)
    {
        if (integrityAmt <= 0.0001f)
            return x;

        // La macro pilote l'intensite globale ; chaque balance individuelle redose la
        // part de chaque mecanisme dans ce total.
        const float decimAmt = integrityAmt * decimAmount;
        const float crushAmt = integrityAmt * bitcrushAmt;

        const float holdSamples = 1.0f + decimAmt * 28.0f;
        holdPhase += 1.0f / holdSamples;
        if (holdPhase >= 1.0f)
        {
            holdPhase -= 1.0f;
            heldValue = x;
        }
        x = heldValue;

        // La decimation cree des harmoniques inharmoniques, percues comme plus dures/
        // fortes qu'un simple crunch a niveau egal. Un passe-bas qui s'assombrit avec la
        // quantite adoucit ce cote agressif sans reduire le volume ni le caractere.
        const float decimCutoffHz = 18000.0f - decimAmt * 14000.0f;
        const float decimLpCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * decimCutoffHz / (float) sampleRate);
        decimLpState += (x - decimLpState) * decimLpCoeff;
        x = decimLpState;

        const float absVal = std::abs (x);
        const float lowLevelBoost = juce::jlimit (0.0f, 1.0f, 1.0f - absVal * 4.0f) * crushAmt;
        const float bits = juce::jlimit (2.0f, 16.0f, 16.0f - crushAmt * 11.0f - lowLevelBoost * 4.0f);
        const float levels = std::pow (2.0f, bits);
        x = std::round (x * levels) / levels;

        return x;
    }

    // Magnetisme de l'Intervalle vers la grille chromatique. Le concept demande "un magnetisme
    // vers les intervalles justes" et veut que l'Intervalle parle le MEME LANGAGE que la Sync :
    // une accroche continue, pas une bascule. Sync aimante le TEMPS vers une grille rythmique,
    // l'Intervalle aimante la HAUTEUR vers une grille chromatique - le meme geste sur deux axes.
    // Ce n'est donc pas un snap : la courbe est continue et monotone, on peut toujours se poser
    // entre deux demi-tons en insistant, c'est juste que l'espace se resserre autour d'eux.
    // (Un simple "arrondi vers le plus proche, attenue" ferait un SAUT au milieu de l'intervalle
    // - a 7.49 on tomberait a 7.2 et a 7.51 a 7.8. D'ou la mise en forme de la fraction.)
    inline float magnetizeInterval (float semitones, float strength)
    {
        const float base = std::floor (semitones);
        const float frac = semitones - base;
        const float u = frac * 2.0f - 1.0f;                    // -1..1, 0 = milieu (le repulseur)
        const float p = 1.0f / (1.0f + strength * 3.0f);       // <1 -> pousse vers +-1
        const float shaped = (u < 0.0f ? -1.0f : 1.0f) * std::pow (std::abs (u), p);
        return base + (shaped + 1.0f) * 0.5f;
    }

    // Debordement : l'additionneur saturant qui lache. Voir le commentaire du parametre pour
    // le pourquoi ; ici le comment.
    //
    //  * Le plafond DESCEND avec le bouton (0 -> -24 dB) plutot que de pousser le signal
    //    dedans. Pousser transformerait le bouton en potard de volume deguise - l'erreur
    //    exacte qui a coute l'auto-gain au preampli. Descendre le plafond dit honnetement
    //    "la machine manque de nombres", et le niveau ne s'envole pas.
    //  * Plafond FIXE et non adaptatif : c'est le point. Un plafond qui suivrait l'enveloppe
    //    mangerait autant les passages doux que les forts ; ici seules les parties fortes le
    //    depassent - "une bande digitale qui ne mange que les passages forts".
    //  * La decision clip/dechirure se prend PAR EXCURSION (au franchissement), pas par
    //    echantillon. Tirer a chaque echantillon ferait alterner les deux au hasard le long
    //    d'une meme crete : du bruit, pas des dechirures. Un vrai debordement dure aussi
    //    longtemps que l'accumulateur reste au-dessus - donc un tirage, tenu jusqu'au retour.
    //  * La sortie est toujours dans [-plafond, +plafond], DONC BORNEE : sans danger dans la
    //    boucle de reinjection, meme si ca s'entend violemment.
    //  * Le tirage est le PREMIER alea du plugin. Bitcrush/Decimation/Derive sont tous
    //    deterministes - c'est ce qui laissait Cycle sans rien a figer. Le generateur est
    //    donc seme explicitement (cf. prepareToPlay) pour etre reproductible des maintenant :
    //    Cycle n'aura qu'a capturer/restaurer sa graine.
    inline float applyOverflow (float x, float amount, OverflowState& st)
    {
        if (amount <= 0.0001f)
            return x;

        const float ceiling = juce::Decibels::decibelsToGain (-24.0f * amount);
        const bool isOver = std::abs (x) > ceiling;

        // Front montant : la protection tient-elle pour CETTE excursion, et si non, comment
        // lache-t-elle ? Elle lache EN DEUX TEMPS - d'abord elle PLIE, et seulement en haut
        // de course elle CASSE :
        //   * Probabilite d'echec = amount^2 (dosage valide a l'oreille, on n'y touche pas).
        //   * Une fois qu'elle echoue, elle CASSE avec une probabilite qui ne demarre qu'a
        //     70% de course. En dessous, elle ne fait que plier.
        // Pourquoi : la CASSURE est une discontinuite (+plafond -> -plafond d'un echantillon
        // a l'autre), et une discontinuite EST un clic - mathematiquement, pas par accident.
        // Aucun lissage ne l'enleverait sans supprimer le mecanisme. Le PLI, lui, est continu
        // par construction (a la frontiere la sortie vaut exactement le plafond des deux
        // cotes) : le sommet se fait detruire pareil, harmoniques riches pareil, mais sans
        // aucune marche - donc sans clic.
        // Et le pli reste tout aussi impossible en analogique : il est NON MONOTONE (plus
        // d'entree donne MOINS de sortie), or un circuit de saturation tend toujours vers une
        // limite sans jamais redescendre.
        if (isOver && ! st.over)
        {
            if (st.rng.nextFloat() < amount * amount)
            {
                const float snapProb = juce::jmax (0.0f, (amount - 0.7f) / 0.3f);
                st.mode = st.rng.nextFloat() < snapProb ? OverflowState::snap
                                                       : OverflowState::bend;
            }
            else
            {
                st.mode = OverflowState::hold;
            }
        }
        st.over = isOver;

        if (! isOver || st.mode == OverflowState::hold)
            return isOver ? juce::jlimit (-ceiling, ceiling, x) : x;

        if (st.mode == OverflowState::bend)
        {
            // Repliement triangulaire : le sommet se reflechit sous le plafond. Periode 4*c
            // pour que les replis successifs s'enchainent proprement au lieu de partir a
            // l'infini quand le signal depasse largement (a 3*c, on ressort a -c).
            const float span = 4.0f * ceiling;
            float f = x - span * std::floor (x / span + 0.5f);
            if (f > ceiling)
                f = 2.0f * ceiling - f;
            else if (f < -ceiling)
                f = -2.0f * ceiling - f;
            return f;
        }

        // Cassure : debordement facon complement a deux, la valeur repasse par le bas.
        const float span = 2.0f * ceiling;
        return x - span * std::floor ((x + ceiling) / span);
    }

    // Preampli d'entree : seul etage volontairement analogique de la machine (cf. concept).
    // Applique EN ENTREE, donc colore tout le signal (sec, echo, reverbe) - a l'inverse de
    // l'Integrite qui epargne le sec. Chaine, dans l'ordre :
    //   pre-emphasis grave -> courbe du modele -> de-emphasis grave -> DC blocker -> auto-gain.
    //
    //  * Emphasis grave (facon Arturia Bus Force) : on booste les graves AVANT la saturation
    //    (ils generent alors plus d'harmoniques), et on annule ce boost APRES (de-emphasis) -
    //    les harmoniques restent (elles sont au-dessus du fondamental) mais l'equilibre du bas
    //    est retabli. Bas riche et chaud sans devenir boueux.
    //  * Modeles : Lampe (asymetrique -> harmoniques paires, chaleur), Transfo (symetrique
    //    plus ferme -> impaires, punch), Bande (doux + perte HF croissante -> chaleur sombre).
    //  * Auto-gain RMS : cale le RMS de sortie sur celui de l'entree (suiveurs d'enveloppe)
    //    plutot qu'une reference fixe - niveau percu constant quel que soit le materiel/drive.
    inline float preampProcess (float x, float amount, int type, float emphasis, PreampState& st, double sr)
    {
        if (amount <= 0.0001f)
            return x;

        const float in = x;
        const float drive = amount * 8.0f;

        // --- Pre-emphasis grave : shelf grave positif (one-pole ~200 Hz) ---
        const float shelfCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 200.0f / (float) sr);
        const float preGain = 1.0f + emphasis * 3.0f;          // jusqu'a ~+12 dB sur les graves
        st.preLp += (in - st.preLp) * shelfCoeff;
        float xe = (in - st.preLp) + st.preLp * preGain;       // aigus intacts, graves boostes

        // --- Courbe du modele, NORMALISEE a gain unite en petit signal (division par la
        // pente a l'origine, qui vaut ~drive). Sans ca, a bas drive la sortie s'effondre et
        // l'auto-gain borne ne rattrape pas -> "coupure" quand on monte la saturation depuis
        // 0. Ici la courbe tend vers l'identite a bas drive (pas de trou), et l'auto-gain ne
        // fait plus qu'une correction fine du RMS a mesure que la saturation s'installe.
        float s;
        if (type == 0)        // Lampe : asymetrie -> harmoniques paires
        {
            const float bias = 0.25f;
            const float sech2 = 1.0f / (std::cosh (bias) * std::cosh (bias));   // pente due au biais
            const float g0 = drive * sech2;
            s = (std::tanh (xe * drive + bias) - std::tanh (bias)) / g0;
        }
        else if (type == 1)   // Transfo : symetrique, genou plus ferme (arctan)
        {
            const float k = drive * 1.5f;
            s = std::atan (xe * k) / k;    // atan(kx)/k -> x quand k->0 : pente unite
        }
        else                  // Bande : douce + perte HF qui augmente avec le drive
        {
            const float k = drive * 0.8f;
            s = std::tanh (xe * k) / k;    // tanh(kx)/k -> x quand k->0 : pente unite
            const float hfCutoff = 18000.0f - amount * 12000.0f;    // 18k -> 6k
            const float hfCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hfCutoff / (float) sr);
            st.bandeLp += (s - st.bandeLp) * hfCoeff;
            s = st.bandeLp;
        }

        // --- De-emphasis grave : shelf grave inverse (retablit l'equilibre du bas) ---
        st.deLp += (s - st.deLp) * shelfCoeff;
        float sd = (s - st.deLp) + st.deLp * (1.0f / preGain);

        // --- DC blocker : l'asymetrie (Lampe) introduit un offset a retirer ---
        const float R = 0.9995f;
        const float dcOut = sd - st.dcX1 + R * st.dcY1;
        st.dcX1 = sd;
        st.dcY1 = dcOut;

        // --- Auto-gain RMS : sortie recalee sur le RMS d'entree (lisse ~50 ms), borne ---
        const float envCoeff = 1.0f - std::exp (-1.0f / (0.05f * (float) sr));
        st.inEnv  += (in * in - st.inEnv) * envCoeff;
        st.outEnv += (dcOut * dcOut - st.outEnv) * envCoeff;
        const float makeup = juce::jlimit (0.25f, 4.0f, std::sqrt (st.inEnv / (st.outEnv + 1.0e-9f)));

        return dcOut * makeup;
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
        juce::ParameterID { "wetGain", 1 }, "Echo volume",
        juce::NormalisableRange<float> (0.0f, 1.5f, 0.001f), 1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

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

    // Longueur (RT60) : separee de la Taille. Avant, "Taille" pilotait en fait la
    // decroissance et rien ne changeait les dimensions du tank - d'ou l'impression qu'il
    // manquait un controle de longueur.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbDecay", 1 }, "Reverbe longueur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbCharacter", 1 }, "Reverbe caractere",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbMix", 1 }, "Reverb volume",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 0.5f,
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

    // INTERVALLE — reinjection accordee (shimmer). Heritage Eventide H3000 / Publison.
    // Un transpositeur pose sur le chemin de REINJECTION du Moteur temps : chaque tour de
    // boucle revient transpose. C'est ce qui transforme le "pad psychedelique" de la
    // Reinjection croisee en vraie montee.
    // CHEMIN PARTAGE (promesse du concept, tenue) : la boucle croisee rentre elle aussi dans
    // la memoire du delay, donc UN SEUL transpositeur attrape les deux - les echos montent, et
    // la traine de reverbe qui revient monte aussi. Pas deux modules a ecrire.
    // Et il herite GRATUITEMENT de toute l'architecture temporelle : il transpose une fois par
    // tour, or la periode du tour est effectiveTimeMs - deja le resultat du melange que la Sync
    // opere entre temps libre et division choisie. Donc Sync a 0% = montee libre au rythme du
    // bouton Temps ; Sync a 100% = chaque marche de la montee tombe exactement sur la division.
    // Zero bouton ajoute pour ca : c'etait deja construit.
    // Consequence assumee (v1, decidee avec l'utilisateur) : la periode de montee EST le temps
    // de delay. Un echo long donne donc toujours des marches, jamais une nappe fluide. Le
    // decoupler demanderait au shimmer sa propre boucle et son propre reglage de vitesse - a
    // faire seulement si ce manque se fait sentir a l'usage.
    // 0 = neutre (transpositeur court-circuite, pas juste inaudible : a ratio 1 les deux tetes
    // liraient a des positions fixes differentes et feraient un filtre en peigne).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "interval", 1 }, "Intervalle",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dt").withStringFromValueFunction (
            [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1); })));

    // DEBORDEMENT — le knob "modde", celui que le constructeur n'avait pas prevu.
    // PAS une saturation : une saturation analogique PLIE (genou progressif), un nombre ne plie
    // pas. Un mot numerique monte jusqu'a sa valeur maximale et soit l'additionneur sature (le
    // sommet est rase net), soit il DEBORDE et la valeur repasse par le bas - +0.7 ressort a
    // -0.5, le son ne s'ecrase pas, il se dechire. Aucun circuit analogique ne fait ca.
    // Ce bouton fait LACHER LA PROTECTION : l'ecretage est ce qu'une machine fait pour EVITER
    // le debordement (les concepteurs virgule fixe mettaient des additionneurs saturants
    // expres). En montant, la protection echoue de plus en plus souvent. Ce n'est pas un
    // morphing arbitraire entre deux courbes, c'est une machine qui tombe en panne - la
    // definition meme de l'Integrite dans le concept ("une panne, pas un design").
    // Complementaire du Bitcrush : lui remonte le plancher (le bruit monte en s'eteignant),
    // celui-ci abaisse le plafond. Les deux referment la meme fenetre par ses deux bouts.
    // A zero par defaut ; volontairement violent en haut de course (a raboter apres ecoute).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "overflow", 1 }, "Debordement",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    // DISTANCE : equilibre reflexions primaires / queue diffuse. C'est l'indice qui place
    // l'auditeur dans la piece - beaucoup de primaires = source proche et definie, beaucoup
    // de queue = source lointaine et noyee. Aucun autre bouton ne couvre cet axe : Taille dit
    // la dimension, Longueur la duree, Caractere la couleur, mais rien ne disait OU on est.
    // 0.5 = neutre : c'est la Taille qui decide (grande piece = la queue prend le dessus,
    // ce qui est physiquement juste). Le bouton decale ce comportement automatique.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbDistance", 1 }, "Reverbe distance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    // Decroissance des graves dans le tank, en MULTIPLE de la Longueur (facon "Bass RT"
    // Lexicon) : l'amortissement du Caractere est un passe-bas, il ne mange que les aigus -
    // rien ne tenait les graves, qui s'empilaient tour apres tour (boueux sur Longueur haute).
    // Neutre a 1.0 = comportement d'avant, on ne change pas le son par surprise ; sous 1 les
    // graves meurent plus vite que les mediums (le reglage qui degage une voix), au-dessus ils
    // tiennent plus longtemps (grandes salles). Skew pour poser le neutre au centre de course.
    auto bassDecayRange = juce::NormalisableRange<float> (0.25f, 2.0f, 0.01f);
    bassDecayRange.setSkewForCentre (1.0f);
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbBassDecay", 1 }, "Reverbe graves",
        bassDecayRange, 1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("x").withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 2) + "x"; })));

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

    // Dry/Wet GLOBAL : dose le humide complet (echo + reverbe, chacun a son volume)
    // contre le sec. A 0% sec pur, a 100% humide pur.
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
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,   // 0 par defaut : le LFO est du caractere optionnel
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "width", 1 }, "Largeur",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 0.3f,
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

    // Defaut a 0 (2026-07-15) : le caractere est OPTIONNEL. Le plugin doit s'ouvrir propre
    // (delay + reverbe polyvalents), on ajoute la corruption par-dessus - pas l'inverse.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "integrity", 1 }, "Integrite",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    // Preampli d'entree (saturation analogique) : colore tout le signal en entree.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "saturation", 1 }, "Saturation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    // Modele de saturation : Lampe (paires/chaleur), Transfo (impaires/punch), Bande (sombre).
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "saturationType", 1 }, "Sat type",
        juce::StringArray { "Lampe", "Transfo", "Bande" }, 0));

    // Emphasis grave (facon Bus Force) : dose combien les graves attaquent la saturation.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "saturationEmphasis", 1 }, "Sat emphasis",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    // Reactivite interne : un suiveur d'enveloppe sur l'entree pousse l'Integrite selon le
    // jeu (fort = plus sale, doux = plus propre). Source unique, montant borne.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reactivity", 1 }, "Reactivite",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    // Ducking : les effets s'effacent quand la source joue et refleurissent dans les trous.
    // Reutilise le suiveur d'enveloppe de la Reactivite (deja la) - le classique qui fait
    // tenir un delay/reverbe derriere une voix sans la noyer.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "ducking", 1 }, "Ducking",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (
            [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })));

    // Filtre DANS LA BOUCLE du delay : chaque repetition le retraverse, donc les echos
    // s'assombrissent progressivement (comportement d'une bande). Sans lui, les repetitions
    // reviennent a pleine bande et s'empilent - dur sur une voix. Ouverts par defaut (neutre).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delayHighpass", 1 }, "Delay HP",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 0.01f, 0.3f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "delayLowpass", 1 }, "Delay LP",
        juce::NormalisableRange<float> (500.0f, 20000.0f, 0.01f, 0.3f), 20000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    // Modulation du tank de reverbe : UN SEUL knob (rythme et profondeur fixes en interne,
    // volontairement pas d'autres reglages exposes). Module legerement les lignes du tank ->
    // casse les modes stationnaires qui rendent une reverbe figee/metallique. C'est ce que
    // font les Lexicon de la lignee citee au concept. Un peu de modulation par defaut : ca
    // ne "crade" pas le son, ca le rend lush - c'est de la qualite de coeur, pas du caractere.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "reverbModulation", 1 }, "Reverbe modulation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f,
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

    // Marges genereuses : la Taille etire les lignes du tank (jusqu'a 1.8x) et l'ecart
    // stereo/modulation s'ajoute par-dessus - il faut de la place au-dela des temps de base.
    for (auto& d : fdnAllpassDelay)
    {
        d.setMaximumDelayInSamples ((int) (0.12 * sampleRate));
        d.prepare (spec);
        d.reset();
    }
    for (auto& d : fdnTankAllpass1)
    {
        d.setMaximumDelayInSamples ((int) (0.25 * sampleRate));
        d.prepare (spec);
        d.reset();
    }
    for (auto& d : fdnTankDelay1)
    {
        d.setMaximumDelayInSamples ((int) (0.5 * sampleRate));
        d.prepare (spec);
        d.reset();
    }
    for (auto& d : fdnTankAllpass2)
    {
        d.setMaximumDelayInSamples ((int) (0.25 * sampleRate));
        d.prepare (spec);
        d.reset();
    }
    for (auto& d : fdnTankDelay2)
    {
        d.setMaximumDelayInSamples ((int) (0.5 * sampleRate));
        d.prepare (spec);
        d.reset();
    }
    for (auto& row : fdnTankDampState)
        for (auto& v : row)
            v = 0.0f;
    for (auto& row : fdnTankBassState)
        for (auto& v : row)
            v = 0.0f;

    // Graines FIXES et distinctes par canal/etage. Fixes : le plugin doit se comporter
    // identiquement d'une session a l'autre, c'est le prerequis de Cycle (une graine stockee
    // regenere la meme trajectoire). Distinctes : avec la meme graine partout, les deux
    // canaux dechireraient exactement en meme temps et le debordement sortirait mono.
    for (int ch = 0; ch < 2; ++ch)
    {
        overflowInput[ch] = {};
        overflowLoop[ch]  = {};
        overflowBus[ch]   = {};
        overflowInput[ch].rng.setSeed (0x0D16E47A + ch);
        overflowLoop[ch].rng.setSeed  (0x0D16A7E0 + ch);
        overflowBus[ch].rng.setSeed   (0x0D16B05F + ch);
    }

    // Filtres 2 poles : boucle du delay + entree de la reverbe.
    delayHpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    delayLpFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    reverbHpFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    reverbLpFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    for (auto* f : { &delayHpFilter, &delayLpFilter, &reverbHpFilter, &reverbLpFilter })
    {
        f->prepare (spec);
        f->reset();
    }

    preDelayLine.setMaximumDelayInSamples ((int) (0.2 * sampleRate));
    preDelayLine.prepare (spec);
    preDelayLine.reset();

    // Fenetre du transpositeur : 80 ms. Compromis classique - plus court, le relais des tetes
    // revient trop souvent et le flottement devient un trémolo ; plus long, la latence de
    // reponse du shifter s'entend sur les transitoires.
    pitchShiftLine.setMaximumDelayInSamples ((int) (0.1 * sampleRate));
    pitchShiftLine.prepare (spec);
    pitchShiftLine.reset();
    pitchPhase[0] = pitchPhase[1] = 0.0f;

    // 0.15 s : la prise la plus tardive (53.7 ms) etiree par la Taille max (1.8x) fait ~97 ms.
    earlyDelayLine.setMaximumDelayInSamples ((int) (0.15 * sampleRate));
    earlyDelayLine.prepare (spec);
    earlyDelayLine.reset();

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
    saturationSmoothed.reset (sampleRate, smoothTime);
    saturationEmphasisSmoothed.reset (sampleRate, smoothTime);
    reactivitySmoothed.reset (sampleRate, smoothTime);
    duckingSmoothed.reset (sampleRate, smoothTime);
    delayHighpassSmoothed.reset (sampleRate, smoothTime);
    delayLowpassSmoothed.reset (sampleRate, smoothTime);
    bitcrushAmountSmoothed.reset (sampleRate, smoothTime);
    decimationAmountSmoothed.reset (sampleRate, smoothTime);
    wetGainSmoothed.reset (sampleRate, smoothTime);
    syncAmountSmoothed.reset (sampleRate, smoothTime);
    reverbSizeSmoothed.reset (sampleRate, smoothTime);
    reverbDecaySmoothed.reset (sampleRate, smoothTime);
    reverbCharacterSmoothed.reset (sampleRate, smoothTime);
    reverbMixSmoothed.reset (sampleRate, smoothTime);
    reverbModulationSmoothed.reset (sampleRate, smoothTime);
    reverbPreDelaySmoothed.reset (sampleRate, smoothTime);
    reverbTransientSmoothed.reset (sampleRate, smoothTime);
    reverbHighpassSmoothed.reset (sampleRate, smoothTime);
    reverbLowpassSmoothed.reset (sampleRate, smoothTime);
    reverbBrightnessSmoothed.reset (sampleRate, smoothTime);
    reverbBassDecaySmoothed.reset (sampleRate, smoothTime);
    reverbDistanceSmoothed.reset (sampleRate, smoothTime);
    overflowSmoothed.reset (sampleRate, smoothTime);
    intervalSmoothed.reset (sampleRate, smoothTime);
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
    saturationSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("saturation"));
    saturationEmphasisSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("saturationEmphasis"));
    reactivitySmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reactivity"));
    duckingSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("ducking"));
    delayHighpassSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("delayHighpass"));
    delayLowpassSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("delayLowpass"));
    bitcrushAmountSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("bitcrushAmount"));
    decimationAmountSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("decimationAmount"));
    wetGainSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("wetGain"));
    syncAmountSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("syncAmount"));
    reverbSizeSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbSize"));
    reverbDecaySmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbDecay"));
    reverbCharacterSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbCharacter"));
    reverbMixSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbMix"));
    reverbModulationSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbModulation"));
    reverbPreDelaySmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbPreDelay"));
    reverbTransientSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbTransient"));
    reverbHighpassSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbHighpass"));
    reverbLowpassSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbLowpass"));
    reverbBrightnessSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbBrightness"));
    reverbBassDecaySmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbBassDecay"));
    reverbDistanceSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("reverbDistance"));
    overflowSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("overflow"));
    intervalSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("interval"));
    crossFeedbackSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("crossFeedback"));
    outputGainSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("outputGain"));

    for (int ch = 0; ch < 2; ++ch)
    {
        transientFastEnv[ch] = 0.0f;
        transientSlowEnv[ch] = 0.0f;
        tiltLpState[ch] = 0.0f;
        crossFeedCompEnv[ch] = 0.0f;
    }

    driftPhase = 0.0f;
    steppedGlideL = 0.0f;
    steppedGlideR = 0.0f;
    widthChorusPhase = 0.0f;
    reactEnv = 0.0f;
    reverbModPhase = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        holdPhase[ch] = 0.0f;
        heldValue[ch] = 0.0f;
        decimLpState[ch] = 0.0f;
        holdPhaseBus[ch] = 0.0f;
        heldValueBus[ch] = 0.0f;
        decimLpStateBus[ch] = 0.0f;
        preampState[ch] = PreampState{};
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

    // Etage de mix : un Dry/Wet GLOBAL (bouton Mix) par-dessus deux volumes d'effet facon
    // Roland RE-201 - Echo volume (delay) et Reverb volume. Chaque effet a son volume dans
    // le humide, et le Mix global dose le humide complet contre le sec.
    //   sortie = sec*(1-Mix) + (echo*EchoVol + reverbe*ReverbVol)*Mix
    //
    // dryDelayBuf = sec (unite) + echo a son volume : c'est ce qui alimente le MOTEUR de
    // reverbe (jamais pondere par le Mix global, pour que le caractere de la reverbe ne
    // depende pas du dosage de sortie ; la reverbe travaille le sec immediatement, attaque
    // jamais repoussee par le temps de delay).
    juce::AudioBuffer<float> dryDelayBuf (2, numSamples);
    dryDelayBuf.clear();

    // directOutBuf = part NON-reverbe de la sortie, deja dosee par le Mix global
    // (sec*(1-Mix) + echo*Mix). Le terme reverbe s'ajoute dans la passe finale (pas pret
    // avant le moteur de reverbe), avec le meme dosage global.
    juce::AudioBuffer<float> directOutBuf (2, numSamples);
    directOutBuf.clear();

    // Valeur du Mix global par echantillon (identique aux deux canaux), relue dans la
    // passe finale ou le terme reverbe recoit le meme dosage.
    juce::AudioBuffer<float> globalMixBuf (1, numSamples);
    globalMixBuf.clear();

    // Enveloppe de la Reactivite interne par echantillon (mono), relue dans la passe finale
    // pour moduler aussi l'Integrite du bus reverbe (pas seulement la boucle echo).
    juce::AudioBuffer<float> reactEnvBuf (1, numSamples);
    reactEnvBuf.clear();

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
    saturationSmoothed.setTargetValue (*apvts.getRawParameterValue ("saturation"));
    saturationEmphasisSmoothed.setTargetValue (*apvts.getRawParameterValue ("saturationEmphasis"));
    reactivitySmoothed.setTargetValue (*apvts.getRawParameterValue ("reactivity"));
    duckingSmoothed.setTargetValue (*apvts.getRawParameterValue ("ducking"));
    const float duckingBus = *apvts.getRawParameterValue ("ducking");
    const float overflowBusAmt = *apvts.getRawParameterValue ("overflow");
    delayHighpassSmoothed.setTargetValue (*apvts.getRawParameterValue ("delayHighpass"));
    delayLowpassSmoothed.setTargetValue (*apvts.getRawParameterValue ("delayLowpass"));
    bitcrushAmountSmoothed.setTargetValue (*apvts.getRawParameterValue ("bitcrushAmount"));
    const int saturationType = (int) *apvts.getRawParameterValue ("saturationType");

    // Filtre de boucle du delay : cutoffs poses une fois par bloc, filtres 2 poles.
    delayHpFilter.setCutoffFrequency (delayHighpassSmoothed.getNextValue());
    delayLpFilter.setCutoffFrequency (delayLowpassSmoothed.getNextValue());

    // Reactivite interne : coeffs du suiveur d'enveloppe (attaque rapide, relache lente pour
    // suivre la dynamique de jeu) + seuil de declenchement (l'enveloppe doit le depasser).
    const float reactAttackCoeff  = 1.0f - std::exp (-1.0f / (0.010f * (float) currentSampleRate));
    const float reactReleaseCoeff = 1.0f - std::exp (-1.0f / (0.200f * (float) currentSampleRate));
    constexpr float reactThreshold = 0.18f;   // ~ -15 dBFS ; en dessous, aucune degradation ajoutee
    decimationAmountSmoothed.setTargetValue (*apvts.getRawParameterValue ("decimationAmount"));
    wetGainSmoothed.setTargetValue (*apvts.getRawParameterValue ("wetGain"));
    syncAmountSmoothed.setTargetValue (*apvts.getRawParameterValue ("syncAmount"));
    reverbSizeSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbSize"));
    reverbDecaySmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbDecay"));
    reverbCharacterSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbCharacter"));
    reverbMixSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbMix"));
    reverbModulationSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbModulation"));
    reverbPreDelaySmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbPreDelay"));
    reverbTransientSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbTransient"));
    reverbHighpassSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbHighpass"));
    reverbLowpassSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbLowpass"));
    reverbBrightnessSmoothed.setTargetValue (*apvts.getRawParameterValue ("reverbBrightness"));
    crossFeedbackSmoothed.setTargetValue (*apvts.getRawParameterValue ("crossFeedback"));
    const float crossFeedAmt = crossFeedbackSmoothed.getNextValue();
    const float crossFeedDelaySamples = 0.02f * (float) currentSampleRate;
    const float pitchWindowSamples = 0.08f * (float) currentSampleRate;
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
        const float globalMix   = mixSmoothed.getNextValue();   // Dry/Wet GLOBAL du plugin
        globalMixBuf.setSample (0, n, globalMix);
        const float rateHz      = driftRateSmoothed.getNextValue();
        const float depthAmt    = driftDepthSmoothed.getNextValue();
        const float widthAmt    = widthSmoothed.getNextValue();
        const float shapeAmt    = driftShapeSmoothed.getNextValue();
        const float integrityAmt = integritySmoothed.getNextValue();
        const float bitcrushAmt  = bitcrushAmountSmoothed.getNextValue();
        const float decimAmount  = decimationAmountSmoothed.getNextValue();
        const float wetGainLin   = wetGainSmoothed.getNextValue();  // Echo volume lineaire, 0 = coupe
        const float saturationAmt = saturationSmoothed.getNextValue();
        const float saturationEmph = saturationEmphasisSmoothed.getNextValue();
        const float reactivityAmt = reactivitySmoothed.getNextValue();
        const float duckingAmt   = duckingSmoothed.getNextValue();
        const float overflowAmt  = overflowSmoothed.getNextValue();

        // Intervalle : magnetise vers la grille chromatique, puis converti en rapport de
        // vitesse de lecture. Le magnetisme est cable a 0.7 pour la v1 (le concept le voulait
        // "optionnel" : ce serait un knob de plus, on verra si le besoin se fait sentir).
        const float intervalSemis = magnetizeInterval (intervalSmoothed.getNextValue(), 0.7f);
        const float intervalRatio = std::pow (2.0f, intervalSemis / 12.0f);
        const bool  intervalActive = std::abs (intervalSemis) > 0.01f;
        const float syncAmt      = syncAmountSmoothed.getNextValue();

        // Reactivite interne : suit l'enveloppe (mono, pic) de l'ENTREE brute et pousse
        // l'Integrite - jouer fort = plus sale, jouer doux = revient au niveau du bouton.
        float inLevel = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            inLevel = juce::jmax (inLevel, std::abs (buffer.getSample (ch, n)));
        reactEnv += (inLevel - reactEnv) * (inLevel > reactEnv ? reactAttackCoeff : reactReleaseCoeff);
        reactEnvBuf.setSample (0, n, reactEnv);

        // Réactivité façon "bande digitale qui ne mange que les passages forts" : l'enveloppe
        // doit dépasser un SEUIL pour dégrader ; en dessous, rien n'est ajouté (parties douces
        // propres, pas de bruit de fond). Au-dessus, la dégradation croît avec le dépassement.
        const float reactExcess = juce::jmax (0.0f, reactEnv - reactThreshold);
        const float reactDrive  = juce::jmin (1.0f, reactExcess / (1.0f - reactThreshold));
        const float effectiveIntegrity = juce::jlimit (0.0f, 1.0f, integrityAmt + reactDrive * reactivityAmt);

        // Ducking : le meme suiveur, mais SANS seuil (contrairement a la Reactivite) - on veut
        // que les effets reculent proportionnellement des que la source joue, pas seulement sur
        // les pics. A fond, une source forte (env ~0.7) fait reculer l'humide d'environ 10 dB,
        // et la traine revient dans les silences.
        const float duckGain = juce::jlimit (0.0f, 1.0f, 1.0f - reactEnv * duckingAmt);

        const float effectiveTimeMs = timeMs * (1.0f - syncAmt) + syncedTimeMs * syncAmt;

        driftPhase += rateHz / (float) currentSampleRate;
        if (driftPhase >= 1.0f)
            driftPhase -= 1.0f;

        // Largeur — une seule course continue de 0 a 200%, sans rupture a 100%. La version
        // precedente n'activait le mouvement (chorus) qu'au-dela de 100%, d'ou la sensation
        // de "deux courses". Ici deux decorrelations L/R montent ENSEMBLE, lineairement :
        //  (1) un dephasage statique du canal droit (0 -> opposition totale a 200%) ;
        //  (2) un leger chorus : un LFO lent (widthChorusPhase) module en opposition le temps
        //      de lecture des deux canaux, sa PROFONDEUR suivant le bouton.
        // Les deux valent 0 a Largeur nulle (mono parfait). Le LFO tourne toujours mais c'est
        // sa profondeur, pas une phase accumulee, qui suit le bouton -> retour a 0 sans residu.
        const float phaseOffset = widthAmt * 0.25f;             // 0..0.5 (opposition a 200%)
        const float chorusDepth = widthAmt * 0.15f;             // 0 a Largeur nulle
        widthChorusPhase += 0.5f / (float) currentSampleRate;   // LFO de chorus lent (~0.5 Hz)
        if (widthChorusPhase >= 1.0f)
            widthChorusPhase -= 1.0f;
        const float widthChorus = std::sin (widthChorusPhase * juce::MathConstants<float>::twoPi) * chorusDepth;

        float driftPhaseR = driftPhase + phaseOffset;
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

        // Le chorus de Largeur module les deux canaux en OPPOSITION (-L / +R) : image plus
        // large, centre stable. Nul a Largeur nulle -> pas de mouvement ajoute en mono.
        const float lfoL = lfoSmoothL * (1.0f - shapeAmt) + steppedGlideL * shapeAmt - widthChorus;
        const float lfoR = lfoSmoothR * (1.0f - shapeAmt) + steppedGlideR * shapeAmt + widthChorus;

        const float modDepthMs = depthAmt * effectiveTimeMs * 0.2f;
        const float delaySamplesL = juce::jmax (0.1f, effectiveTimeMs + lfoL * modDepthMs) * 0.001f * (float) currentSampleRate;
        const float delaySamplesR = juce::jmax (0.1f, effectiveTimeMs + lfoR * modDepthMs) * 0.001f * (float) currentSampleRate;
        const float channelDelaySamples[2] = { delaySamplesL, delaySamplesR };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            // Preampli d'entree : sature le signal des l'entree, avant de le repartir vers
            // le sec, l'echo et la reverbe - c'est l'etage analogique qui colore tout.
            float inputSample = preampProcess (data[n], saturationAmt, saturationType,
                                               saturationEmph, preampState[ch], currentSampleRate);

            // Debordement d'ENTREE, juste apres le preampli et AVANT la repartition : il touche
            // donc le sec, l'echo et la reverbe. C'est le seul etage, avec le preampli, a
            // colorer tout - et c'est volontaire : les deux sont au meme endroit parce qu'ils
            // disent la meme chose a deux niveaux (le nerf analogique qui sature, puis les
            // nombres qui manquent).
            //
            // Il N'obeit PAS a la doctrine "le sec reste propre" de l'Integrite, et ce n'est
            // pas une incoherence :
            //   - l'Integrite degrade la MEMOIRE (ce qui est stocke puis relu). La metaphore
            //     bande tient : l'enregistrement s'abime, pas le signal live. Epargner le sec
            //     est juste.
            //   - le Debordement est une panne d'ARITHMETIQUE. La machine manque de nombres,
            //     ca n'arrive pas a la memoire mais a tout ce qui traverse le circuit, live
            //     compris. Surcharge l'electronique d'entree d'une vraie machine : tout ce qui
            //     est en aval distord, y compris ce que tu entends en direct.
            //
            // Sans cet etage, le comportement etait a l'envers de l'intuition : on jouait fort,
            // le sec restait intact, et c'est un echo PLUS FAIBLE qui se dechirait plus tard.
            // La panne ne suivait pas le geste - d'ou un effet ressenti comme mecanique.
            // ⚠️ Consequence assumee : a Mix 0 (sec seul), Debordement pousse abime quand meme
            // la source. C'est un bouton "modde", il n'est pas cense etre sans danger.
            inputSample = applyOverflow (inputSample, overflowAmt, overflowInput[ch]);

            const float delayedSample = delayLine.popSample (ch, channelDelaySamples[ch]);

            // INTERVALLE : transpose ce qui repart dans la boucle. Deux tetes glissant a
            // vitesse constante, relayees en fondu a puissance constante (elles lisent la meme
            // fenetre a des positions decalees d'une demi-periode, donc quand l'une approche du
            // point de raccord l'autre est au milieu et domine).
            //   phase avance de (1 - ratio)/fenetre : pour monter (ratio > 1) il faut lire plus
            //   vite, donc rattraper l'ecriture, donc DIMINUER le retard - d'ou le signe.
            // On lit/pousse TOUJOURS, meme a Intervalle nul, pour que la fenetre reste fraiche :
            // sinon le premier geste sur le bouton ferait ressortir du vieux son.
            pitchPhase[ch] += (1.0f - intervalRatio) / pitchWindowSamples;
            while (pitchPhase[ch] >= 1.0f) pitchPhase[ch] -= 1.0f;
            while (pitchPhase[ch] <  0.0f) pitchPhase[ch] += 1.0f;

            float phaseB = pitchPhase[ch] + 0.5f;
            if (phaseB >= 1.0f)
                phaseB -= 1.0f;

            const float headA = pitchShiftLine.popSample (ch, juce::jmax (1.0f, pitchPhase[ch] * pitchWindowSamples), false);
            const float headB = pitchShiftLine.popSample (ch, juce::jmax (1.0f, phaseB * pitchWindowSamples), false);
            pitchShiftLine.popSample (ch, 1.0f, true);          // la seule lecture qui avance le pointeur
            pitchShiftLine.pushSample (ch, delayedSample);

            const float shifted = headA * std::sin (juce::MathConstants<float>::pi * pitchPhase[ch])
                                + headB * std::sin (juce::MathConstants<float>::pi * phaseB);

            float toPush = inputSample + (intervalActive ? shifted : delayedSample) * feedbackAmt;

            // Reinjection croisee : la traine humide de la Reverbe (avec ~20ms de retard
            // fixe, cf. commentaire du membre) revient se faire attraper par le Moteur
            // temps. A zero, chaine propre ; pousse, generateur ambiant qui s'auto-
            // entretient (delay et reverbe qui se parlent, facon Roland RE-201).
            toPush += crossFeedDelayLine.popSample (ch, crossFeedDelaySamples) * crossFeedAmt;

            // Filtre DANS la boucle : applique a CHAQUE passage, donc les repetitions
            // s'assombrissent progressivement (une bande perd des aigus a chaque tour).
            // Sans lui, les echos revenaient a pleine bande et s'empilaient - le defaut qui
            // rendait le delay dur sur une voix. Ouvert par defaut = neutre.
            toPush = delayHpFilter.processSample (ch, toPush);
            toPush = delayLpFilter.processSample (ch, toPush);

            // Integrite dans la boucle : dégrade ce qui entre dans la mémoire du delay,
            // pas le direct. Chaque passage réinjecte le résultat déjà dégradé, donc les
            // répétitions s'assalissent PROGRESSIVEMENT (accumulation) plutôt que d'un
            // coup. Un second étage sur le bus d'effets (cf. passe finale) dégrade en plus
            // la reverbe, pour que l'Intégrité soit un traitement général et pas delay-only.
            toPush = applyIntegrity (toPush, effectiveIntegrity, bitcrushAmt, decimAmount,
                                     holdPhase[ch], heldValue[ch], decimLpState[ch], currentSampleRate);

            // Debordement dans la boucle : chaque repetition redeborde, donc les echos se
            // dechirent PROGRESSIVEMENT au lieu d'un coup - meme logique d'accumulation que
            // l'Integrite juste au-dessus. Place AVANT la Securite : les deux ont des roles
            // opposes (l'une casse volontairement, l'autre protege de l'emballement) et la
            // Securite doit rester le dernier mot avant la memoire.
            toPush = applyOverflow (toPush, overflowAmt, overflowLoop[ch]);

            if (safetyOn)
                toPush = softSafetyCeiling (toPush, 1.2f);

            delayLine.pushSample (ch, toPush);

            // Echo (repetitions du delay) a son volume (Echo volume). Sec+echo alimente le
            // moteur de reverbe (dryDelayBuf, non pondere par le Mix global). La sortie
            // non-reverbe est deja dosee par le Mix global (sec*(1-Mix) + echo*Mix).
            const float delayWet = delayedSample * wetGainLin;

            // Le ducking ne touche QUE ce qu'on entend, jamais ce qui alimente la reverbe :
            // dryDelay reste intact, sinon on affamerait le moteur au lieu de baisser sa
            // traine - la queue se mettrait a pomper au rythme de la source.
            float dryDelay = inputSample + delayWet;
            if (safetyOn)
                dryDelay = softSafetyCeiling (dryDelay, 1.2f);
            dryDelayBuf.setSample (ch, n, dryDelay);

            float directOut = inputSample * (1.0f - globalMix) + delayWet * duckGain * globalMix;
            if (safetyOn)
                directOut = softSafetyCeiling (directOut, 1.2f);
            directOutBuf.setSample (ch, n, directOut);
        }
    }

    // --- Reverbe : reseau a retards rebouclés maison (FDN, cf. section tank plus bas) ---
    // Alimentee par le sec+echo (dryDelayBuf). Taille pilote la decroissance, Caractere
    // l'amortissement par ligne.
    const float reverbSizeVal   = reverbSizeSmoothed.getNextValue();
    const float reverbDecayVal  = reverbDecaySmoothed.getNextValue();
    const float reverbCharVal   = reverbCharacterSmoothed.getNextValue();
    const float reverbMixVal    = reverbMixSmoothed.getNextValue();
    const float preDelayMs      = reverbPreDelaySmoothed.getNextValue();
    const float transientAmt    = reverbTransientSmoothed.getNextValue();
    const float highpassHz      = reverbHighpassSmoothed.getNextValue();
    const float lowpassHz       = reverbLowpassSmoothed.getNextValue();
    const float brightnessVal   = reverbBrightnessSmoothed.getNextValue();

    // TAILLE : etire vraiment les lignes du tank (les dimensions de la piece). Avant, les
    // longueurs etaient figees et "Taille" ne faisait que la decroissance - d'ou l'impression
    // qu'il manquait un controle de longueur.
    const float fdnSizeFactor = juce::jmap (reverbSizeVal, 0.0f, 1.0f, 0.5f, 1.8f);

    // LONGUEUR : la decroissance (RT60), desormais son propre parametre, independant de la
    // taille. Le gain de chaque bras en decoule, compte tenu de la longueur reelle du tour.
    const float fdnDecaySeconds = juce::jmap (reverbDecayVal, 0.0f, 1.0f, 0.3f, 12.0f);

    // CARACTERE : axe structurel (cristallin -> sombre et moins dense), il pilote ENSEMBLE
    // l'amortissement ET la quantite de diffusion.
    // La plage de diffusion s'arrete a 0.45 et NON a 0.15 : en dessous de ~0.4 les allpass
    // cessent de diffuser, le tank redevient un reseau de delais nus et on entend des echos
    // discrets - percus comme des saccades dans la queue, pas comme un caractere. La
    // perceptibilite est portee par l'amortissement (16k -> 1.5k, tres audible) ; la
    // diffusion apporte le changement de densite sans jamais casser la queue.
    // Le "casse/glitche" extreme du concept est le travail de l'Integrite (le module de
    // corruption), pas du coeur de la reverbe, qui doit rester musical sur toute sa course.
    const float fdnDampCutoffHz = juce::jmap (reverbCharVal, 0.0f, 1.0f, 16000.0f, 1500.0f);
    const float fdnDampCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fdnDampCutoffHz / (float) currentSampleRate);
    const float fdnDiffusion = juce::jmap (reverbCharVal, 0.0f, 1.0f, 0.78f, 0.45f);

    // ECART STEREO par PRISES DIFFERENTES, et non par desaccord des lignes. La version
    // precedente allongeait les lignes d'un canal et raccourcissait celles de l'autre (+-5%) :
    // ca decorrelait, mais deux tanks de longueurs differentes ne sonnent pas a la meme
    // hauteur - L et R etaient legerement desaccordes l'un par rapport a l'autre.
    // Ici (approche Dattorro) les lignes des deux canaux sont IDENTIQUES et on lit chaque
    // canal a des positions differentes DANS sa ligne : deux signaux decorreles, meme
    // hauteur. A Largeur 0 / Mono les deux canaux lisent aux memes positions -> mono exact.
    const float reverbSpread = juce::jlimit (0.0f, 1.0f, monoEngaged ? 0.0f : *apvts.getRawParameterValue ("width") * 0.5f);

    // Pre-diffusion : 4 allpass en serie, temps espaces sans rapports simples entre eux.
    static constexpr float fdnAllpassTimesMs[4] = { 5.3f, 8.9f, 13.1f, 17.9f };
    const float fdnAllpassCoeff = fdnDiffusion;

    float fdnAllpassSamples[4];
    for (int i = 0; i < 4; ++i)
        fdnAllpassSamples[i] = fdnAllpassTimesMs[i] * 0.001f * (float) currentSampleRate * fdnSizeFactor;

    // Tank a 2 bras croises (figure-8, lignee Lexicon/EMT/Dattorro) : chaque bras a
    // DEUX etages allpass+delay en serie (pas un seul) - premier passage : diffuse ->
    // longue ligne 1 -> amortissement/gain (seul point de perte d'energie du bras) ->
    // second passage : diffuse a nouveau -> longue ligne 2 -> ressort vers le bras
    // oppose. Deux points de diffusion et deux longueurs par tour, plutot qu'un aller-
    // retour unique qui s'entend comme un delay/ping-pong.
    static constexpr float fdnTankAp1TimesMs[2]    = { 18.9f, 26.1f };
    static constexpr float fdnTankDelay1TimesMs[2] = { 118.6f, 105.1f };
    static constexpr float fdnTankAp2TimesMs[2]    = { 61.3f, 74.9f };
    static constexpr float fdnTankDelay2TimesMs[2] = { 89.7f, 71.3f };
    const float fdnTankAp1Coeff = fdnDiffusion;
    const float fdnTankAp2Coeff = fdnDiffusion * 0.75f;

    // Modulation du tank : un LFO lent, profondeur sub-milliseconde (plage classique facon
    // Dattorro/Lexicon). Assez pour desaccorder les modes stationnaires sans donner un chorus
    // audible. Rythme et profondeur max fixes en interne : un seul knob expose.
    const float reverbModVal = reverbModulationSmoothed.getNextValue();
    constexpr float reverbModRateHz = 0.6f;
    const float reverbModDepth = reverbModVal * 0.0005f * (float) currentSampleRate;   // jusqu'a ~0.5 ms

    // Toutes les longueurs du tank suivent la Taille. Le gain de reinjection est recalcule
    // sur le tour REEL (donc etire lui aussi) : la Longueur reste tenue quelle que soit la
    // Taille - agrandir la piece ne raccourcit pas la queue.
    // Graves du tank : le passe-bas de separation est fixe a 350 Hz (frontiere classique
    // grave/medium des reverbes a "Bass RT"), volontairement pas expose - la facade a deja
    // trop de boutons pour en ajouter un dont personne ne bouge la valeur.
    const float reverbBassMult = reverbBassDecaySmoothed.getNextValue();
    const float fdnBassCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 350.0f / (float) currentSampleRate);

    float fdnTankAp1Samples[2], fdnTankDelay1Samples[2], fdnTankAp2Samples[2], fdnTankDelay2Samples[2],
          fdnTankFeedbackGain[2], fdnTankBassGain[2];
    for (int a = 0; a < 2; ++a)
    {
        const float ap1Ms = fdnTankAp1TimesMs[a]    * fdnSizeFactor;
        const float d1Ms  = fdnTankDelay1TimesMs[a] * fdnSizeFactor;
        const float ap2Ms = fdnTankAp2TimesMs[a]    * fdnSizeFactor;
        const float d2Ms  = fdnTankDelay2TimesMs[a] * fdnSizeFactor;

        fdnTankAp1Samples[a]    = ap1Ms * 0.001f * (float) currentSampleRate;
        fdnTankDelay1Samples[a] = d1Ms  * 0.001f * (float) currentSampleRate;
        fdnTankAp2Samples[a]    = ap2Ms * 0.001f * (float) currentSampleRate;
        fdnTankDelay2Samples[a] = d2Ms  * 0.001f * (float) currentSampleRate;

        const float roundTripSeconds = (ap1Ms + d1Ms + ap2Ms + d2Ms) * 0.001f;
        fdnTankFeedbackGain[a] = std::pow (10.0f, -3.0f * roundTripSeconds / fdnDecaySeconds);

        // Gain propre a la bande grave. Le plafond a 0.995 n'est pas cosmetique : a Longueur
        // 12 s le gain nominal vaut deja ~0.85, un multiplicateur de 2 le porterait a 1.7 et
        // la bande grave partirait en emballement au lieu de decroitre. On borne donc juste
        // sous 1 - le grave peut tenir tres longtemps, jamais grossir.
        fdnTankBassGain[a] = juce::jmin (0.995f, fdnTankFeedbackGain[a] * reverbBassMult);
    }

    // Positions de prise, en fraction de chaque ligne. Le canal 0 fixe la reference ; le
    // canal 1 s'en ecarte a mesure que Largeur monte (a 0 il lit aux memes positions, donc
    // sortie strictement mono - pas de residu comme avec un desaccord qui ne revient jamais
    // vraiment a zero). Fractions sans rapports simples entre elles, pour que les prises ne
    // retombent pas sur les memes echos.
    static constexpr float tapFrac1[2][2] = { { 0.31f, 0.67f }, { 0.58f, 0.89f } };
    static constexpr float tapFrac2[2][2] = { { 0.43f, 0.79f }, { 0.71f, 0.37f } };

    // --- Reflexions primaires : motif d'arrivees, etire par la Taille ---
    // Temps DIFFERENTS par canal : c'est ainsi que la stereo des primaires fonctionne dans une
    // vraie piece (les rebonds n'atteignent pas les deux oreilles au meme instant), et c'est
    // bien plus fort comme indice d'espace qu'un simple panoramique. Signes alternes : deux
    // prises voisines ne se renforcent pas, ca evite au motif de sonner comme un peigne.
    static constexpr float erTimesMs[2][8] = {
        { 7.3f, 11.9f, 17.1f, 23.7f, 29.3f, 37.1f, 43.9f, 53.7f },
        { 9.1f, 13.7f, 19.3f, 21.9f, 31.7f, 34.3f, 47.1f, 51.3f }
    };
    static constexpr float erGains[8] = { 1.0f, -0.82f, 0.71f, -0.6f, 0.5f, -0.41f, 0.33f, -0.26f };
    constexpr float erNorm = 0.565f;   // 1/sqrt(somme des carres) : les prises s'additionnent en energie

    const float reverbDistanceVal = reverbDistanceSmoothed.getNextValue();

    // La Taille fixe l'equilibre de base : plus la piece est grande, plus la queue prend le
    // dessus sur les primaires - c'est ce qui rend le Taille reellement convaincant, sans
    // qu'on ait a toucher un bouton. Distance ne fait que DECALER ce comportement automatique.
    const float sizeTailBias = juce::jmap (reverbSizeVal, 0.0f, 1.0f, 0.35f, 0.65f);
    const float tailRatio = juce::jlimit (0.05f, 0.95f, sizeTailBias + (reverbDistanceVal - 0.5f) * 0.7f);

    // Fondu a puissance constante : les primaires et la queue sont decorrelees, leurs energies
    // s'additionnent. Un fondu lineaire creuserait un trou de niveau au milieu de la course.
    const float erLevel   = std::cos (tailRatio * juce::MathConstants<float>::halfPi);
    const float tailLevel = std::sin (tailRatio * juce::MathConstants<float>::halfPi);

    float erSamples[2][8];
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 8; ++i)
        {
            // Largeur ecarte les temps d'arrivee du canal droit ; a 0 les deux canaux lisent
            // le meme motif -> primaires strictement mono, comme le tank.
            const float t = erTimesMs[0][i] + (erTimesMs[c][i] - erTimesMs[0][i]) * reverbSpread;
            erSamples[c][i] = juce::jmax (1.0f, t * 0.001f * (float) currentSampleRate * fdnSizeFactor);
        }

    float tapD1[2][2], tapD2[2][2];
    for (int c = 0; c < 2; ++c)
        for (int a = 0; a < 2; ++a)
        {
            const float f1 = tapFrac1[0][a] + (tapFrac1[c][a] - tapFrac1[0][a]) * reverbSpread;
            const float f2 = tapFrac2[0][a] + (tapFrac2[c][a] - tapFrac2[0][a]) * reverbSpread;
            tapD1[c][a] = juce::jmax (1.0f, fdnTankDelay1Samples[a] * f1);
            tapD2[c][a] = juce::jmax (1.0f, fdnTankDelay2Samples[a] * f2);
        }


    // Etages d'entree : Transient (adoucit l'attaque courte pour une reverbe plus
    // ambiante) -> Input HP/LP (evite l'engorgement grave/aigu dans le reservoir) ->
    // Pre-delai. Tous independants du moteur, ne bougeront pas au remplacement par le FDN.
    const float transientFastCoeff = 1.0f - std::exp (-1.0f / (0.003f * (float) currentSampleRate));
    const float transientSlowCoeff = 1.0f - std::exp (-1.0f / (0.08f  * (float) currentSampleRate));
    reverbHpFilter.setCutoffFrequency (highpassHz);
    reverbLpFilter.setCutoffFrequency (lowpassHz);
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
            // Le moteur de reverbe lit dryDelayBuf (sec + repetitions) - la reverbe
            // travaille le sec immediatement, son attaque est independante du temps de delay.
            float x = dryDelayBuf.getSample (ch, n);

            const float absX = std::abs (x);
            transientFastEnv[ch] += (absX - transientFastEnv[ch]) * transientFastCoeff;
            transientSlowEnv[ch] += (absX - transientSlowEnv[ch]) * transientSlowCoeff;
            const float spike = juce::jmax (0.0f, transientFastEnv[ch] - transientSlowEnv[ch]);
            const float duck = juce::jlimit (0.0f, 1.0f, transientAmt * spike * 6.0f);
            x *= (1.0f - duck);

            const float preDelayed = preDelayLine.popSample (ch, preDelaySamples);
            preDelayLine.pushSample (ch, x);

            data[n] = preDelayed;
        }
    }

    for (int n = 0; n < numSamples; ++n)
    {
        // LFO de modulation du tank : une seule phase, 4 prises en quadrature pour que les
        // lignes ne bougent pas toutes ensemble (sinon elles resteraient correlees).
        reverbModPhase += reverbModRateHz / (float) currentSampleRate;
        if (reverbModPhase >= 1.0f)
            reverbModPhase -= 1.0f;

        float modOff[4];
        for (int i = 0; i < 4; ++i)
            modOff[i] = std::sin ((reverbModPhase + i * 0.25f) * juce::MathConstants<float>::twoPi) * reverbModDepth;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            float diffused = data[n];
            const int tapCh = juce::jmin (ch, 1);

            // Reflexions primaires, en parallele du tank et nourries par le meme signal
            // pre-retarde. Les 7 premieres prises ne deplacent pas le pointeur de lecture ;
            // la 8e le fait, et passe EN DERNIER pour que les 8 lisent toutes depuis la meme
            // position - si elle passait en premier, les 7 autres seraient decalees d'un
            // echantillon et le motif ne serait plus celui qu'on a dessine.
            float erOut = 0.0f;
            for (int i = 1; i < 8; ++i)
                erOut += earlyDelayLine.popSample (ch, erSamples[tapCh][i], false) * erGains[i];
            erOut += earlyDelayLine.popSample (ch, erSamples[tapCh][0], true) * erGains[0];
            erOut *= erNorm;
            earlyDelayLine.pushSample (ch, data[n]);

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

            // Tank a 2 bras croises, DEUX etages allpass+delay chacun (lignee
            // Lexicon/EMT/Dattorro) : lecture de la ligne 1 -> amortissement/gain
            // (seul point de perte d'energie du bras) -> diffusion 2 -> ligne 2 -> ce
            // qui ressort nourrit le bras OPPOSE. Le tap de sortie prend les DEUX
            // lignes (positions differentes dans la boucle) plutot qu'un seul point,
            // pour une densite d'echos que le simple aller-retour ne donnait pas.
            // Prises de sortie : lues AVANT que la boucle ne consomme les lignes, et surtout
            // sans deplacer le pointeur de lecture (updateReadPointer = false) - ce sont des
            // fenetres sur la ligne, elles ne doivent rien changer a la circulation du tank.
            float tapOut = 0.0f;

            float raw1[2], fed1[2], raw2[2];
            for (int a = 0; a < 2; ++a)
            {
                tapOut += fdnTankDelay1[a].popSample (ch, tapD1[tapCh][a], false);
                tapOut += fdnTankDelay2[a].popSample (ch, tapD2[tapCh][a], false);

                // Ligne longue modulee : c'est elle qui porte les modes stationnaires, donc
                // la desaccorder legerement est ce qui tue le metallique.
                raw1[a] = fdnTankDelay1[a].popSample (ch, juce::jmax (1.0f, fdnTankDelay1Samples[a] + modOff[a]));
                fdnTankDampState[a][ch] += (raw1[a] - fdnTankDampState[a][ch]) * fdnDampCoeff;
                const float damped = fdnTankDampState[a][ch];

                // Les graves prennent leur propre gain de reinjection : on separe la bande
                // basse du reste et on donne a chacune son taux de decroissance. Comme c'est
                // applique a chaque tour, l'ecart se creuse tout au long de la queue - c'est
                // exactement le comportement recherche (les graves s'effacent pendant que les
                // mediums tiennent, ou l'inverse). A 1.0x les deux gains sont egaux : neutre.
                fdnTankBassState[a][ch] += (damped - fdnTankBassState[a][ch]) * fdnBassCoeff;
                const float bass = fdnTankBassState[a][ch];
                fed1[a] = bass * fdnTankBassGain[a] + (damped - bass) * fdnTankFeedbackGain[a];

                raw2[a] = fdnTankDelay2[a].popSample (ch, juce::jmax (1.0f, fdnTankDelay2Samples[a]));
            }

            // Deuxieme diffusion, mi-parcours, avant la seconde ligne.
            for (int a = 0; a < 2; ++a)
            {
                const float wD2 = fdnTankAllpass2[a].popSample (ch, juce::jmax (1.0f, fdnTankAp2Samples[a]));
                const float w2  = fed1[a] + fdnTankAp2Coeff * wD2;
                fdnTankAllpass2[a].pushSample (ch, w2);
                const float diffused2 = -fdnTankAp2Coeff * w2 + wD2;
                fdnTankDelay2[a].pushSample (ch, diffused2);
            }

            // Chaque bras recoit la moitie du signal pre-diffuse + la sortie de la
            // ligne 2 du bras OPPOSE (figure-8), traverse sa premiere diffusion, part
            // dans sa premiere ligne.
            for (int a = 0; a < 2; ++a)
            {
                const float armIn = 0.5f * diffused + raw2[1 - a];
                // Diffuseur module aussi (le geste classique de Dattorro), en quadrature avec
                // la ligne longue pour decorreler les deux mouvements.
                const float wD1 = fdnTankAllpass1[a].popSample (ch, juce::jmax (1.0f, fdnTankAp1Samples[a] + modOff[2 + a]));
                const float w1  = armIn + fdnTankAp1Coeff * wD1;
                fdnTankAllpass1[a].pushSample (ch, w1);
                const float diffused1 = -fdnTankAp1Coeff * w1 + wD1;
                fdnTankDelay1[a].pushSample (ch, diffused1);
            }

            // Sortie = les 4 prises (positions propres au canal), et non plus les lectures de
            // la boucle : c'est ce qui porte desormais l'image stereo. Meme makeup (+6 dB) et
            // meme nombre de termes qu'avant, donc le niveau ne bouge pas.
            const float tail = 0.5f * tapOut;

            // Primaires + queue, fondu a puissance constante pilote par Taille et Distance.
            data[n] = erOut * erLevel + tail * tailLevel;
        }
    }

    // Brightness : tilt sur le humide seul (jamais le direct) - separation lisse/aigu
    // via un simple passe-bas, puis rebalance des deux bandes.
    const float tiltCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 900.0f / (float) currentSampleRate);
    const float lowGain  = 1.0f - brightnessVal * 0.6f;
    const float highGain = 1.0f + brightnessVal * 0.6f;

    // Etage Integrite du BUS d'effets : ici applique a la sortie reverbe, pour que la
    // degradation soit audible sur la reverbe et independante du delay (l'echo, lui, est
    // deja degrade dans la boucle). Valeurs lues au rythme du bloc (l'Integrite ne bouge
    // pas a l'echantillon pres) - etat par canal separe de celui de la boucle.
    const float integrityBus = *apvts.getRawParameterValue ("integrity");
    const float bitcrushBus  = *apvts.getRawParameterValue ("bitcrushAmount");
    const float decimBus     = *apvts.getRawParameterValue ("decimationAmount");
    const float reactivityBus = *apvts.getRawParameterValue ("reactivity");

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto* directData = directOutBuf.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n)
        {
            // Filtre de la reverbe : applique EN SORTIE, pas a l'entree du tank. En pre, il ne
            // touchait que ce qui entrait : la queue deja dans le tank gardait son ancien
            // spectre, donc bouger le filtre mettait plusieurs secondes a s'entendre (la
            // "latence" ressentie). En post, il agit sur toute la queue, immediatement.
            float wet = data[n];
            wet = reverbHpFilter.processSample (ch, wet);
            wet = reverbLpFilter.processSample (ch, wet);

            tiltLpState[ch] += (wet - tiltLpState[ch]) * tiltCoeff;
            const float lowBand  = tiltLpState[ch];
            const float highBand = wet - lowBand;
            wet = lowBand * lowGain + highBand * highGain;

            // Contribution reverbe = humide * Reverb volume. Puis étage Intégrité du bus
            // d'effets : la reverbe est dégradée ici (l'echo l'est déjà dans la boucle),
            // donc l'Intégrité s'entend sur la reverbe et reste active même sans delay.
            float reverbContribution = wet * reverbMixVal;
            // Reactivite : meme enveloppe et meme seuil pour le bus reverbe (ne mord que fort).
            const float envB = reactEnvBuf.getSample (0, n);
            const float drvB = juce::jmin (1.0f, juce::jmax (0.0f, envB - reactThreshold) / (1.0f - reactThreshold));
            const float effIntegBus = juce::jlimit (0.0f, 1.0f, integrityBus + drvB * reactivityBus);
            reverbContribution = applyIntegrity (reverbContribution, effIntegBus, bitcrushBus, decimBus,
                                                 holdPhaseBus[ch], heldValueBus[ch], decimLpStateBus[ch], currentSampleRate);
            reverbContribution = applyOverflow (reverbContribution, overflowBusAmt, overflowBus[ch]);

            // Sert aussi de source a la Reinjection croisee (capturee APRES le niveau reverbe,
            // pour que Reverb volume reste un levier naturel sur la boucle croisee). La boucle
            // croisee est independante du Mix global (circuit interne, pas dosage de sortie).

            // Compression locale (Option C) : controle ce qui entre dans la memoire de
            // la boucle des le depart, plutot qu'un plafond qui n'agit qu'a l'extreme.
            // Seuil/ratio/temps fixes en interne, pas exposes a l'utilisateur.
            const float compInputLevel = std::abs (reverbContribution);
            const float compCoeff = (compInputLevel > crossFeedCompEnv[ch]) ? compAttackCoeff : compReleaseCoeff;
            crossFeedCompEnv[ch] += (compInputLevel - crossFeedCompEnv[ch]) * compCoeff;

            float compGain = 1.0f;
            if (crossFeedCompEnv[ch] > compThreshold)
            {
                const float excessDb = juce::Decibels::gainToDecibels (crossFeedCompEnv[ch] / compThreshold);
                const float reductionDb = excessDb * (1.0f - 1.0f / compRatio);
                compGain = juce::Decibels::decibelsToGain (-reductionDb);
            }

            crossFeedDelayLine.pushSample (ch, reverbContribution * compGain);

            // Sortie = part non-reverbe deja dosee par le Mix global (directData) + terme
            // reverbe (a son volume) dose par le meme Mix global. A Mix=0 : sortie seche
            // pure ; a Mix=1 : sec efface, echo+reverbe a leurs volumes respectifs.
            // Ducking applique ICI et pas plus tot : la Reinjection croisee (capturee juste
            // au-dessus) est un circuit interne et garde la reverbe pleine, sinon la boucle
            // croisee pomperait au rythme de la source. Le ducking est un dosage de SORTIE.
            const float duckB = juce::jlimit (0.0f, 1.0f, 1.0f - reactEnvBuf.getSample (0, n) * duckingBus);
            const float gm = globalMixBuf.getSample (0, n);
            float mixed = directData[n] + reverbContribution * duckB * gm;
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
