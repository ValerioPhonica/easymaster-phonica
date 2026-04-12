#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Colours_GB
{
    // === BACKGROUND HIERARCHY - deep blue-black tones, more contrast between layers ===
    inline const juce::Colour bg         { 0xff0a0c10 };   // deepest - main window
    inline const juce::Colour panel      { 0xff14171e };   // track row background
    inline const juce::Colour panel2     { 0xff1c2029 };   // raised panels / engine area
    inline const juce::Colour panel3     { 0xff242834 };   // elevated elements / buttons
    inline const juce::Colour border     { 0xff303644 };   // subtle borders
    inline const juce::Colour borderLight{ 0xff424a5a };   // hover/focus borders

    // === PRIMARY ACCENT - cyan/teal ===
    inline const juce::Colour accent     { 0xff00c8e0 };   // primary cyan
    inline const juce::Colour accentBright{ 0xff40e8ff };   // hover / active
    inline const juce::Colour accentDim  { 0xff0c2a32 };   // dim tinted bg
    inline const juce::Colour accentMid  { 0xff007888 };   // medium intensity
    inline const juce::Colour accentGlow { 0xff00a0c0 };   // glow effects

    // === SECONDARY ACCENTS ===
    inline const juce::Colour amber      { 0xfff0a020 };   // warm secondary
    inline const juce::Colour amberDim   { 0xff2a1800 };
    inline const juce::Colour amberMid   { 0xff6b4000 };
    inline const juce::Colour red        { 0xffef4444 };   // mute / warning
    inline const juce::Colour blue       { 0xff4090f0 };   // synth tracks
    inline const juce::Colour blueDim    { 0xff0e1832 };
    inline const juce::Colour green      { 0xff30d868 };   // gate / positive
    inline const juce::Colour cyan       { 0xff00d4e8 };   // p-lock / highlights
    inline const juce::Colour purple     { 0xff9060e0 };   // trigless

    // === LED / STEP ===
    inline const juce::Colour ledOff     { 0xff181c26 };   // step off - blue tint
    inline const juce::Colour ledPlay    { 0xff00e0f0 };   // playing step - bright cyan

    // === TEXT HIERARCHY ===
    inline const juce::Colour text       { 0xffd0d4e0 };   // primary text
    inline const juce::Colour textBright { 0xffecf0f8 };   // emphasis
    inline const juce::Colour textDim    { 0xff6e7688 };   // secondary text
    inline const juce::Colour textDark   { 0xff4a5268 };   // tertiary / disabled

    // === KNOB SPECIFIC ===
    inline const juce::Colour knobBody   { 0xff222630 };   // knob fill
    inline const juce::Colour knobRing   { 0xff2e3440 };   // outer ring
    inline const juce::Colour knobHi     { 0xff424858 };   // highlight crescent
    inline const juce::Colour knobTrack  { 0xff1a1e28 };   // arc track bg

    // === VELOCITY colour interpolation ===
    inline juce::Colour velColour (float normVel)
    {
        return accentDim.interpolatedWith (accent, normVel);
    }

    inline juce::Colour synthVelColour (float normVel)
    {
        return blueDim.interpolatedWith (blue, normVel);
    }
}

namespace Constants
{
    inline constexpr int kMaxSteps      = 128;
    inline constexpr int kNumDrumTracks = 9;
    inline constexpr int kNumSynthParts = 5;
    inline constexpr int kDefaultBPM    = 120;
    inline constexpr int kMinBPM        = 40;
    inline constexpr int kMaxBPM        = 300;
    inline constexpr int kStepsPerPage  = 16;
    inline constexpr float kLookAheadSec = 0.1f;
    inline constexpr int kScheduleIntervalMs = 10;
}
