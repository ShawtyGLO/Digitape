// ============================================================================
//  UILayout.h - GENERE depuis tools/layout.json par tools/generate.ps1
//  NE PAS EDITER A LA MAIN. Portee : panneaux peints (drawAdvPanel).
// ============================================================================
#pragma once

namespace UI
{
    struct Panel      { float x, y, w, h; const char* title; };
    struct Rect       { float x, y, w, h; };
    struct AnchorBR   { float right, bottom, w, h; }; // ancre bas-droite : x=getWidth()-right, y=getHeight()-bottom
    struct AnchorBottomBar { float bottom, h; };      // barre pleine largeur ancree en bas : y=getHeight()-bottom

    namespace panel
    {
    }

    namespace ctrl
    {
        namespace facade
        {
            constexpr Rect titleLabel             {   20.0f,   10.0f,  300.0f,   22.0f };
            constexpr Rect timeSlider             {   30.0f,   50.0f,   90.0f,   90.0f };
            constexpr Rect feedbackSlider         {  135.0f,   50.0f,   90.0f,   90.0f };
            constexpr Rect mixSlider              {  670.0f,    5.0f,   90.0f,   90.0f };
            constexpr Rect driftRateSlider        {  135.0f,  370.0f,   90.0f,   90.0f };
            constexpr Rect driftDepthSlider       {  238.0f,  370.0f,   90.0f,   90.0f };
            constexpr Rect driftShapeSlider       {  344.0f,  370.0f,   90.0f,   90.0f };
            constexpr Rect widthSlider            {  570.0f,  346.0f,   90.0f,   90.0f };
            constexpr Rect integritySlider        {  380.0f,  245.0f,   90.0f,   90.0f };
            constexpr Rect wetGainSlider          {   30.0f,  155.0f,   90.0f,   90.0f };
            constexpr Rect syncAmountSlider       {   30.0f,  266.0f,   90.0f,   90.0f };
            constexpr Rect reverbSizeSlider       {  275.0f,   50.0f,   90.0f,   90.0f };
            constexpr Rect reverbCharacterSlider  {  380.0f,   50.0f,   90.0f,   90.0f };
            constexpr Rect reverbMixSlider        {  275.0f,  155.0f,   90.0f,   90.0f };
            constexpr Rect monoBtn                {  570.0f,  436.0f,   90.0f,   24.0f };
            constexpr Rect safetyBtn              {  670.0f,  436.0f,   90.0f,   24.0f };
            constexpr Rect syncNoteCombo          {  580.0f,  120.0f,  140.0f,   28.0f };
            constexpr Rect syncFlavorCombo        {  580.0f,  160.0f,  140.0f,   28.0f };
        }
        namespace advanced
        {
            constexpr Rect bitcrushAmountSlider   {   40.0f,   50.0f,   90.0f,   90.0f };
            constexpr Rect decimationAmountSlider {  170.0f,   50.0f,   90.0f,   90.0f };
        }
    }

    namespace anchor
    {
    }
}
