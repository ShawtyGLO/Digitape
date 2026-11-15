// ============================================================================
//  ThemeColors.g.h - GENERE depuis tools/theme.json par tools/generate.ps1
//  NE PAS EDITER A LA MAIN.
// ============================================================================
#pragma once

#include <JuceHeader.h>

namespace Theme
{
    const juce::Colour accent         = juce::Colour(0xFF13ADD6); // cyan principal
    const juce::Colour teal           = juce::Colour(0xFF1BB89E); // teal secondaire
    const juce::Colour textMuted      = juce::Colour(0xFF94A5A2); // gris-vert labels
    const juce::Colour textPrimary    = juce::Colour(0xFFCDDDD9); // texte clair sur fond sombre
    const juce::Colour textDark       = juce::Colour(0xFF26332F); // texte sombre sur fond clair
    const juce::Colour textSecondary  = juce::Colour(0xFF4A6660); // texte discret (desc. tooltip)
    const juce::Colour ink            = juce::Colour(0xFF060514); // quasi-noir (texte sur boutons accent)
    const juce::Colour danger         = juce::Colour(0xFFFF3333); // rouge bypass/off
    const juce::Colour dangerSoft     = juce::Colour(0xFFE05050); // rouge adouci
    const juce::Colour amberWarn      = juce::Colour(0xFFE8A000); // ambre alerte
    const juce::Colour gold           = juce::Colour(0xFFF0C040); // doré
    const juce::Colour surfaceLight   = juce::Colour(0xFFE4F0EE); // panneau/preset clair
    const juce::Colour surfaceLighter = juce::Colour(0xFFF0FAFC); // surface très claire
    const juce::Colour surfaceBar     = juce::Colour(0xFFD4E8E4); // barre preset/tooltip
    const juce::Colour surfaceBlue    = juce::Colour(0xFFDDEAF0); // surface bleutée
    const juce::Colour surfaceSky     = juce::Colour(0xFFEBF4F7); // ciel clair
    const juce::Colour bgDeep         = juce::Colour(0xFF0D1A1E); // fond profond
    const juce::Colour panelDark      = juce::Colour(0xFF1E3330); // panneau sombre
    const juce::Colour black          = juce::Colour(0xFF000000); // noir
    const juce::Colour white          = juce::Colour(0xFFFFFFFF); // blanc
    const juce::Colour scrim          = juce::Colour(0x22000000); // noir translucide (overlay/ombre)
}
