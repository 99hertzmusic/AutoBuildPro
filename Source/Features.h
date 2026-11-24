#pragma once
// AutoBuild Pro: build layers, styles, bar lengths and THE Lite/Pro split.
// Everything about what is free and what needs Pro lives in this file. To move a feature,
// flip its flag below: the processor (audio), the editor (padlocks) and the tests all read it.

namespace ab
{
// The layers a build is made of. Each has an amount (0-100 %) in Pro.
enum Layer { filter = 0, noise, pitch, stutter, wash, numLayers };

inline const char* layerId (int l)
{
    static const char* n[] = { "filter", "noise", "pitch", "stutter", "wash" };
    return n[l >= 0 && l < numLayers ? l : 0];
}
inline const char* layerName (int l)
{
    static const char* n[] = { "FILTER", "NOISE", "PITCH", "STUTTER", "WASH" };
    return n[l >= 0 && l < numLayers ? l : 0];
}

// Styles are presets of the five layer amounts (FILTER, NOISE, PITCH, STUTTER, WASH).
enum Style { classic = 0, riser, pitchUp, stutterRoll, washOut, mega, numStyles };
struct StyleDef { const char* name; const char* note; float amount[numLayers]; };
inline const StyleDef& style (int s)
{
    static const StyleDef d[numStyles] = {
        { "CLASSIC", "FILTER + ECHO",     { 100.0f,   0.0f,   0.0f,   0.0f, 100.0f } },   // the original AutoBuild
        { "RISER",   "NOISE SWEEP",       {  70.0f, 100.0f,   0.0f,   0.0f,  40.0f } },
        { "PITCH",   "PITCH RISE",        {  45.0f,   0.0f, 100.0f,   0.0f,  45.0f } },
        { "STUTTER", "SNARE-ROLL REPEAT", {  60.0f,   0.0f,   0.0f, 100.0f,  25.0f } },
        { "WASH",    "REVERB BLOOM",      {  30.0f,  35.0f,   0.0f,   0.0f, 100.0f } },
        { "MEGA",    "EVERYTHING",        {  80.0f,  80.0f,  60.0f,  70.0f,  70.0f } },
    };
    return d[s >= 0 && s < numStyles ? s : 0];
}

// BARS dial (the build length). The build repeats every BARS bars from bar 1; the drop is the bar line.
inline constexpr int barSteps[] = { 1, 2, 4, 8, 16, 32 };
inline constexpr int numBarSteps = 6, defaultBarStep = 3;
// WAIT dial: dry bars before each build (wait, build, drop, repeat). 0 = the build starts again right after the drop.
inline constexpr int waitSteps[] = { 0, 1, 2, 4, 8, 16, 32 };
inline constexpr int numWaitSteps = 7;

inline constexpr double standaloneBpm = 128.0;   // standalone app: free-running clock

// ── THE LITE / PRO SPLIT ──────────────────────────────────────────────────────
// true = needs Pro (licence key or 7-day trial). false = free in Lite.
// Always free: BUILD, BARS, MANUAL, OUTPUT, host tempo sync, the build display.
namespace gate
{
    inline constexpr bool proStyle[numStyles] = {
        false,  // CLASSIC (the original AutoBuild sound)
        true,   // RISER
        true,   // PITCH
        true,   // STUTTER
        true,   // WASH
        true    // MEGA
    };
    inline constexpr bool proLayers     = true;   // Lite: CLASSIC amounts
    inline constexpr bool proCurve      = true;   // Lite: straight line (0)
    inline constexpr bool proWait       = true;   // Lite: no wait (the build repeats every BARS, like the original)
    inline constexpr bool proImpact     = true;   // Lite: off
    inline constexpr bool proDownlifter = true;   // Lite: off

    // What a locked control falls back to in Lite.
    inline constexpr int liteWaitStep = 0;
    inline constexpr float liteCurve = 0.0f, liteImpact = 0.0f, liteDownlifter = 0.0f;
    inline float liteLayer (int l) { return style (classic).amount[l]; }

    inline bool styleAllowed (int s, bool pro) { return pro || ! proStyle[s >= 0 && s < numStyles ? s : 0]; }
}
} // namespace ab
