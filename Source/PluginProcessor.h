#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "Features.h"
#include "BuildDsp.h"
#include "Licence/Licence.h"

// AutoBuild Pro: the automatic build-up / riser, locked to the DAW's bars.
//  Lite (free): BUILD, BARS, MANUAL, OUTPUT. The CLASSIC build (filter sweep + echo + reverb).
//  Pro (key or 7-day trial): STYLES (riser, pitch, stutter, wash, mega), per-layer amounts, CURVE,
//  IMPACT on the drop and DOWNLIFTER after it. The split itself lives in Features.h (ab::gate).
class AutoBuildProAudioProcessor : public juce::AudioProcessor
{
public:
    AutoBuildProAudioProcessor();
    ~AutoBuildProAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AutoBuild Pro"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ── Licence ─────────────────────────────────────────────────────────────
    hz::Licence licence { "autobuildpro" };
    hz::LicenceInfo licenceInfo;                 // message thread
    std::atomic<bool> proUnlocked { false };     // audio thread reads this
    void setLicenceInfo (const hz::LicenceInfo& i) { licenceInfo = i; proUnlocked.store (i.unlocked()); }

    // ── What actually runs, after the Lite/Pro gate ─────────────────────────
    struct Effective
    {
        float build = 0.7f;          // 0..1
        int   barStep = ab::defaultBarStep;
        int   waitStep = 0;
        bool  manual = false;
        float outDb = 0.0f;
        float curve = 0.0f;          // -100..100
        float layer[ab::numLayers] = {};   // 0..1
        float impact = 0.0f, downlifter = 0.0f;   // 0..1
    };
    Effective resolve (bool pro) const;
    static float curveShape (float p, float curve);   // p 0..1 -> 0..1
    int matchingStyle (const Effective& e) const;      // -1 = custom

    // ── Live state for the editor ──────────────────────────────────────────
    std::atomic<float> dispProgress { 0.0f }, dispLevel { 0.0f };   // position in the build 0..1 (-1 stopped), applied build 0..1
    std::atomic<float> dispCycle { 0.0f };                          // position in wait + build 0..1
    std::atomic<double> dispBpm { 128.0 };
    std::atomic<double> dispBeatsPerBar { 4.0 };
    std::atomic<bool> dispPlaying { false };
    std::atomic<float> dispSinceDrop { 1.0e9f };    // seconds since the last drop
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    // ── Test hooks (not real-time safe when set) ───────────────────────────
    std::vector<float>* progressTrace = nullptr;     // raw build position per sample (-1 = stopped)
    std::vector<int64_t> dropSamples;                // sample index of every drop (test only)
    bool recordDrops = false;
    int64_t samplesProcessed = 0;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* pBuild = nullptr; std::atomic<float>* pBars = nullptr; std::atomic<float>* pManual = nullptr; std::atomic<float>* pWait = nullptr;
    std::atomic<float>* pOut = nullptr;   std::atomic<float>* pCurve = nullptr;
    std::atomic<float>* pLayer[ab::numLayers] = {};
    std::atomic<float>* pImpact = nullptr; std::atomic<float>* pDown = nullptr;

    double sr = 48000.0;
    int maxBlock = 512;

    // transport
    double freePpq = 0.0;
    float prevC = -1.0f; int prevBarStep = -1, prevWaitStep = -1; double prevBpb = 0.0;

    // smoothing
    float sBuild = 0.0f, sBuild1 = 0.0f, sLayer[ab::numLayers] = {}, sLayer1[ab::numLayers] = {}, sOut = 1.0f, sOut1 = 1.0f;   // 2-pole smoothing: no kinks = no clicks
    float cFast = 0.01f, cParam = 0.001f, cLayer = 0.001f, cSlow = 0.0001f, cRun = 0.001f;
    int sinceDrop = 1 << 30;                         // samples since the last drop (fast release right after it)
    float run = 0.0f;                                // 1 while playing, fades to 0 when stopped
    float delaySmp = 1000.0f;

    // DSP
    ab::Svf hp[2]; int coefCount = 0;
    ab::Ring echo[2];
    juce::Reverb verb, fxVerb;
    ab::PitchShifter shifter;
    ab::Svf noiseBp[2]; ab::Noise noiseGen[2]; float noisePw = 1.0e-4f, noisePwCoef = 0.001f;
    ab::Impact impact; ab::Downlifter downlifter;
    std::vector<float> stutRec[2];
    float inPw = 0.0f, chainPw = 0.0f, levelGain = 1.0f, levelGain1 = 1.0f, cPw = 0.0001f;

    // sub-block scratch
    static constexpr int sub = 32;
    float wetL[sub], wetR[sub], fxL[sub], fxR[sub], dirL[sub], dirR[sub], washAmt[sub];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoBuildProAudioProcessor)
};
