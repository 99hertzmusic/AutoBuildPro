#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

AutoBuildProAudioProcessor::AutoBuildProAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AutoBuildProState", createParameterLayout())
{
    pBuild  = apvts.getRawParameterValue ("build");
    pBars   = apvts.getRawParameterValue ("bars");
    pManual = apvts.getRawParameterValue ("manual");
    pWait   = apvts.getRawParameterValue ("wait");
    pOut    = apvts.getRawParameterValue ("output");
    pCurve  = apvts.getRawParameterValue ("curve");
    for (int l = 0; l < ab::numLayers; ++l) pLayer[l] = apvts.getRawParameterValue (ab::layerId (l));
    pImpact = apvts.getRawParameterValue ("impact");
    pDown   = apvts.getRawParameterValue ("downlifter");
    setLicenceInfo (licence.check());
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoBuildProAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    auto pct = AudioParameterFloatAttributes().withLabel ("%");
    static const char* layerNames[] = { "Filter", "Noise", "Pitch", "Stutter", "Wash" };

    l.add (std::make_unique<AudioParameterFloat> (id ("build"), "Build", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f, pct));
    l.add (std::make_unique<AudioParameterChoice> (id ("bars"), "Bars", StringArray { "1", "2", "4", "8", "16", "32" }, ab::defaultBarStep));
    l.add (std::make_unique<AudioParameterBool> (id ("manual"), "Manual", false));
    l.add (std::make_unique<AudioParameterChoice> (id ("wait"), "Wait", StringArray { "Off", "1", "2", "4", "8", "16", "32" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (id ("output"), "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
               AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (id ("curve"), "Curve", NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    for (int k = 0; k < ab::numLayers; ++k)
        l.add (std::make_unique<AudioParameterFloat> (id (ab::layerId (k)), layerNames[k],
                   NormalisableRange<float> (0.0f, 100.0f, 0.1f), ab::style (ab::classic).amount[k], pct));
    l.add (std::make_unique<AudioParameterFloat> (id ("impact"), "Impact", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (id ("downlifter"), "Downlifter", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pct));
    return l;
}

// ── Resolve: Lite/Pro gate -> the numbers that actually run ───────────────────
AutoBuildProAudioProcessor::Effective AutoBuildProAudioProcessor::resolve (bool pro) const
{
    Effective e;
    e.build   = juce::jlimit (0.0f, 1.0f, pBuild->load() / 100.0f);
    e.barStep = juce::jlimit (0, ab::numBarSteps - 1, (int) pBars->load());
    e.manual  = pManual->load() > 0.5f;
    e.waitStep = (pro || ! ab::gate::proWait) ? juce::jlimit (0, ab::numWaitSteps - 1, (int) pWait->load()) : ab::gate::liteWaitStep;
    e.outDb   = juce::jlimit (-12.0f, 12.0f, pOut->load());
    e.curve   = (pro || ! ab::gate::proCurve) ? juce::jlimit (-100.0f, 100.0f, pCurve->load()) : ab::gate::liteCurve;
    for (int l = 0; l < ab::numLayers; ++l)
        e.layer[l] = ((pro || ! ab::gate::proLayers) ? juce::jlimit (0.0f, 100.0f, pLayer[l]->load()) : ab::gate::liteLayer (l)) / 100.0f;
    e.impact     = ((pro || ! ab::gate::proImpact)     ? juce::jlimit (0.0f, 100.0f, pImpact->load()) : ab::gate::liteImpact) / 100.0f;
    e.downlifter = ((pro || ! ab::gate::proDownlifter) ? juce::jlimit (0.0f, 100.0f, pDown->load())   : ab::gate::liteDownlifter) / 100.0f;
    return e;
}

float AutoBuildProAudioProcessor::curveShape (float p, float curve)
{
    p = juce::jlimit (0.0f, 1.0f, p);
    if (std::abs (curve) < 1.0e-3f) return p;
    return std::pow (p, std::exp2 (curve / 50.0f));   // +100: slow start, late rush; -100: fast start
}

int AutoBuildProAudioProcessor::matchingStyle (const Effective& e) const
{
    for (int s = 0; s < ab::numStyles; ++s)
    {
        bool same = true;
        for (int l = 0; l < ab::numLayers; ++l) same = same && std::abs (e.layer[l] * 100.0f - ab::style (s).amount[l]) < 0.6f;
        if (same) return s;
    }
    return -1;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void AutoBuildProAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    cFast  = ab::onePoleCoef (sr, 0.004);    // the drop (2 poles: ~10 ms)
    cParam = ab::onePoleCoef (sr, 0.015);    // everything else (control moves, start/stop)
    cLayer = ab::onePoleCoef (sr, 0.02);
    cSlow  = ab::onePoleCoef (sr, 0.05);
    cRun   = ab::onePoleCoef (sr, 0.02);
    cPw    = ab::onePoleCoef (sr, 0.4);
    noisePwCoef = ab::onePoleCoef (sr, 0.03);

    for (auto& r : echo) r.prepare ((int) (sr * 3.2));     // 1/8 note down to 20 BPM
    shifter.prepare (sr);
    for (auto& v : stutRec) v.assign ((size_t) (sr * 3.1) + 4, 0.0f);   // one beat at 20 BPM
    impact.prepare (sr);
    downlifter.prepare (sr);
    for (int c = 0; c < 2; ++c) noiseGen[c].seed (c == 0 ? 0x2545f491u : 0x5bd1e995u);
    verb.setSampleRate (sr);
    fxVerb.setSampleRate (sr);
    {
        juce::Reverb::Parameters p; p.roomSize = 0.4f; p.damping = 0.35f; p.wetLevel = 1.0f / 3.0f; p.dryLevel = 0.0f; p.width = 1.0f;
        verb.setParameters (p);
        juce::Reverb::Parameters q; q.roomSize = 0.88f; q.damping = 0.45f; q.wetLevel = 1.0f / 3.0f; q.dryLevel = 0.0f; q.width = 1.0f;
        fxVerb.setParameters (q);
    }
    reset();
}

void AutoBuildProAudioProcessor::reset()
{
    for (auto& f : hp) { f.reset(); f.set (sr, 20.0f, 0.5f); }
    for (auto& f : noiseBp) { f.reset(); f.set (sr, 400.0f, 1.2f); }
    for (auto& r : echo) r.clear();
    shifter.reset();
    for (auto& v : stutRec) std::fill (v.begin(), v.end(), 0.0f);
    verb.reset(); fxVerb.reset();
    impact.active = false; downlifter.active = false;

    const auto e = resolve (proUnlocked.load());
    sBuild = sBuild1 = e.manual ? e.build : 0.0f;
    for (int l = 0; l < ab::numLayers; ++l) sLayer[l] = sLayer1[l] = e.layer[l];
    sOut = sOut1 = juce::Decibels::decibelsToGain (e.outDb);
    run = 0.0f;
    delaySmp = -1.0f;
    prevC = -1.0f; prevBarStep = -1; prevWaitStep = -1; prevBpb = 0.0;
    freePpq = 0.0;
    inPw = chainPw = 0.0f; levelGain = levelGain1 = 1.0f;
    noisePw = 0.02f;
    coefCount = 0; sinceDrop = 1 << 30;
}

bool AutoBuildProAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

// ── Audio ─────────────────────────────────────────────────────────────────────
void AutoBuildProAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getTotalNumInputChannels(), getTotalNumOutputChannels(), buffer.getNumChannels());
    for (int c = juce::jmax (0, numCh); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (numCh <= 0 || n <= 0) return;

    // Locked features fall back to their Lite values, so automation or an old session can't bypass the lock.
    const bool pro = proUnlocked.load();
    const Effective e = resolve (pro);

    // ── Transport ──
    double bpm = ab::standaloneBpm, ppq0 = 0.0, bpb = 4.0;
    bool playing = false, freeRun = false;
    if (wrapperType == wrapperType_Standalone) freeRun = true;     // no DAW: free-running clock at 128 BPM
    else if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto ts = pos->getTimeSignature()) if (ts->numerator > 0 && ts->denominator > 0) bpb = ts->numerator * 4.0 / ts->denominator;
            if (auto p = pos->getPpqPosition()) { ppq0 = *p; playing = pos->getIsPlaying(); }
            else freeRun = true;
        }
        else freeRun = true;
    }
    else freeRun = true;
    bpm = juce::jlimit (20.0, 400.0, bpm);
    bpb = juce::jlimit (1.0, 32.0, bpb);
    if (freeRun)
    {
        if (wrapperType == wrapperType_Standalone) bpm = ab::standaloneBpm;
        playing = true; ppq0 = freePpq;
        freePpq += n * bpm / (60.0 * sr);
    }
    const double bps = bpm / (60.0 * sr);
    const double buildBars = ab::barSteps[e.barStep], waitBars = ab::waitSteps[e.waitStep];
    const double cycleBeats = (buildBars + waitBars) * bpb;
    const double spb = 60.0 / bpm * sr;    // samples per beat
    const float delayTarget = (float) juce::jlimit (2.0, (double) echo[0].mask - 4.0, 0.5 * spb);   // 1/8 note echo
    if (delaySmp < 0.0f) delaySmp = delayTarget;
    const float outTarget = juce::Decibels::decibelsToGain (e.outDb);
    const int stutLen = (int) stutRec[0].size();

    {
        juce::Reverb::Parameters p; p.damping = 0.35f; p.wetLevel = 1.0f / 3.0f; p.dryLevel = 0.0f; p.width = 1.0f;
        p.roomSize = 0.4f + 0.5f * juce::jlimit (0.0f, 1.0f, sBuild * sLayer[ab::wash]);
        verb.setParameters (p);
    }

    float* data[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numCh > 1 ? 1 : 0) };
    float blkIn = 0.0f, blkOut = 0.0f, lastP = -1.0f, lastC = -1.0f;
    int dropAt = -1;

    for (int start = 0; start < n; start += sub)
    {
        const int m = juce::jmin (sub, n - start);

        // ── Pass A: position, stutter, pitch, filter, echo, generators ──
        for (int j = 0; j < m; ++j)
        {
            const int i = start + j;
            const float xL = data[0][i], xR = numCh > 1 ? data[1][i] : xL;
            blkIn = juce::jmax (blkIn, std::abs (xL), std::abs (xR));
            inPw += cPw * (0.5f * (xL * xL + xR * xR) - inPw);

            // Position in the build: recomputed from the host's PPQ every sample (bar 1 = PPQ 0).
            const double ppq = ppq0 + (double) i * bps;
            float p = -1.0f, cf = -1.0f;      // p: position in the build (0 while waiting), cf: position in wait + build
            if (playing)
            {
                double cyc = ppq / cycleBeats + 1.0e-9;
                cyc -= std::floor (cyc);
                cf = (float) cyc;
                const double inBars = cyc * (buildBars + waitBars) - waitBars;
                p = inBars <= 0.0 ? 0.0f : (float) (inBars / buildBars);
            }
            const bool drop = playing && ! e.manual && prevC >= 0.0f && e.barStep == prevBarStep && e.waitStep == prevWaitStep
                              && std::abs (bpb - prevBpb) < 1.0e-9 && prevC > 0.75f && cf < 0.25f;
            prevC = cf; prevBarStep = e.barStep; prevWaitStep = e.waitStep; prevBpb = bpb;
            lastC = cf;
            lastP = p;

            if (drop)
            {
                sinceDrop = 0;
                const float rms = std::sqrt (inPw);
                if (e.impact > 0.0f) impact.trigger (e.impact * juce::jlimit (0.06f, 0.3f, 1.25f * rms));
                if (e.downlifter > 0.0f)
                    downlifter.trigger (e.downlifter * juce::jlimit (0.03f, 0.18f, 0.7f * rms),
                                        juce::jmin (6.0, juce::jmin (2.0, (double) ab::barSteps[e.barStep]) * bpb * 60.0 / bpm));
                dropAt = i;
                if (recordDrops) dropSamples.push_back (samplesProcessed + i);
            }
            if (progressTrace != nullptr) progressTrace->push_back (p);

            const float target = e.manual ? e.build : (playing ? curveShape (p, e.curve) * e.build : 0.0f);
            {
                const float cb = sinceDrop < (int) (0.05 * sr) ? cFast : cParam;
                sBuild1 += cb * (target - sBuild1);
                sBuild  += cb * (sBuild1 - sBuild);
            }
            if (sinceDrop < (1 << 30)) ++sinceDrop;
            for (int l = 0; l < ab::numLayers; ++l) { sLayer1[l] += cLayer * (e.layer[l] - sLayer1[l]); sLayer[l] += cLayer * (sLayer1[l] - sLayer[l]); }
            run += cRun * ((playing ? 1.0f : 0.0f) - run);
            delaySmp += cSlow * (delayTarget - delaySmp);
            const float s = sBuild;

            float yL = xL, yR = xR;

            // STUTTER: snare-roll repeats that speed up 1/4 -> 1/8 -> 1/16 -> 1/32 over the build.
            const float stutMix = sLayer[ab::stutter] * run;
            {
                const float pStep = e.manual ? e.build : juce::jmax (0.0f, p);
                const double stepBeats = pStep < 0.5f ? 1.0 : pStep < 0.75f ? 0.5 : pStep < 0.875f ? 0.25 : 0.125;
                const double beatFrac = ppq - std::floor (ppq);
                const int jsub = (int) std::floor (beatFrac / stepBeats + 1.0e-9);
                const double stepPos = (beatFrac - jsub * stepBeats) * spb, stepLen = stepBeats * spb;
                float srcL = xL, srcR = xR;
                if (jsub == 0)
                {
                    const int idx = juce::jlimit (0, stutLen - 1, (int) (beatFrac * spb));
                    stutRec[0][(size_t) idx] = xL; stutRec[1][(size_t) idx] = xR;
                }
                else
                {
                    const int idx = juce::jlimit (0, stutLen - 1, (int) stepPos);
                    srcL = stutRec[0][(size_t) idx]; srcR = stutRec[1][(size_t) idx];
                }
                if (stutMix > 1.0e-5f)
                {
                    const float depth = 0.2f + 0.7f * juce::jmin (1.0f, s * 1.2f);
                    const double duty = stepBeats < 1.0 ? 0.55 : 0.75;
                    const double rel = 0.004 * sr;
                    const double over = stepPos - duty * stepLen;
                    float g = over <= 0.0 ? 1.0f : 1.0f - depth * (float) juce::jmin (1.0, over / rel);
                    if (stepBeats < 1.0)
                    {
                        const double dc = 0.0015 * sr;
                        g *= (float) juce::jlimit (0.0, 1.0, juce::jmin (stepPos / dc, (stepLen - stepPos) / dc));
                    }
                    yL += (srcL * g - yL) * stutMix;
                    yR += (srcR * g - yR) * stutMix;
                }
            }

            // PITCH: the whole thing rises up to an octave.
            {
                const float sP = s * sLayer[ab::pitch];
                float pl = yL, pr = yR;
                shifter.process (pl, pr, std::exp2 (sP));
                const float mx = ab::smoothStep (0.0f, 0.4f, sP);
                yL += (pl - yL) * mx; yR += (pr - yR) * mx;
            }

            // FILTER: the low-cut sweep (the original AutoBuild core). NOISE band follows the build up.
            const float sF = s * sLayer[ab::filter];
            const float sN = s * sLayer[ab::noise];
            {
                const float hz = 20.0f * std::pow (400.0f, sF);
                const float q = 0.5f + 0.3f * sF;
                for (auto& f : hp) f.set (sr, hz, q);
                const float nhz = 400.0f * std::pow (30.0f, s);
                for (auto& f : noiseBp) f.set (sr, nhz, 1.2f);
            }
            float band, low;
            yL = hp[0].process (yL, band, low);
            yR = hp[1].process (yR, band, low);

            // ECHO: tempo-synced 1/8 note, feeding back harder as the build rises.
            const float sW = s * sLayer[ab::wash];
            {
                const float mixE = sW * 0.5f, fb = 0.2f + 0.65f * sW;
                const float dL = echo[0].read (delaySmp), dR = echo[1].read (delaySmp);
                echo[0].push (yL + dL * fb); echo[1].push (yR + dR * fb);
                yL = yL * (1.0f - mixE) + dL * mixE;
                yR = yR * (1.0f - mixE) + dR * mixE;
            }
            data[0][i] = yL; if (numCh > 1) data[1][i] = yR;
            wetL[j] = yL; wetR[j] = yR;
            washAmt[j] = sW;

            // NOISE riser (generated), level follows the input loudness.
            float nL, nR;
            {
                float b0, b1, lo;
                noiseBp[0].process (noiseGen[0].next(), b0, lo);
                noiseBp[1].process (noiseGen[1].next(), b1, lo);
                noisePw += noisePwCoef * (0.5f * (b0 * b0 + b1 * b1) - noisePw);
                const float norm = juce::jmin (30.0f, 1.0f / std::sqrt (noisePw + 1.0e-6f));
                const float lvl = sN * s * juce::jlimit (0.02f, 0.2f, 0.6f * std::sqrt (inPw)) * norm;
                nL = b0 * lvl; nR = b1 * lvl;
            }

            // IMPACT + DOWNLIFTER after the drop
            float iL, iR, iSL, iSR, dL, dR;
            impact.process (iL, iR, iSL, iSR);
            downlifter.process (dL, dR);
            dirL[j] = nL + iL + dL; dirR[j] = nR + iR + dR;
            fxL[j] = iSL + 0.5f * dL + 0.3f * nL; fxR[j] = iSR + 0.5f * dR + 0.3f * nR;
        }

        // ── Reverbs (sub-block) ──
        verb.processStereo (wetL, wetR, m);
        fxVerb.processStereo (fxL, fxR, m);

        // ── Pass B: reverb blend, width, level safety, generators, output ──
        for (int j = 0; j < m; ++j)
        {
            const int i = start + j;
            const float sW = washAmt[j];
            float yL = data[0][i], yR = numCh > 1 ? data[1][i] : yL;
            const float mixR = ab::smoothStep (0.4f, 1.0f, sW) * 0.4f;
            yL = yL * (1.0f - mixR * 0.5f) + wetL[j] * mixR;
            yR = yR * (1.0f - mixR * 0.5f) + wetR[j] * mixR;

            // WIDTH (mid/side, mono-safe)
            const float wd = sW * 0.6f;
            if (wd > 1.0e-5f)
            {
                const float mid = 0.5f * (yL + yR), side = 0.5f * (yL - yR) * 2.5f;
                yL += (mid + side - yL) * wd;
                yR += (mid - side - yR) * wd;
            }

            // Level safety: the build never ends up more than +2 dB over the input.
            chainPw += cPw * (0.5f * (yL * yL + yR * yR) - chainPw);
            const float lt = juce::jmin (1.0f, 1.26f * std::sqrt ((inPw + 1.0e-9f) / (chainPw + 1.0e-9f)));
            levelGain1 += cSlow * (lt - levelGain1);
            levelGain  += cSlow * (levelGain1 - levelGain);

            sOut1 += cSlow * (outTarget - sOut1);
            sOut  += cSlow * (sOut1 - sOut);
            float o[2] = { (yL * levelGain + dirL[j] + fxL[j]) * sOut,
                           (yR * levelGain + dirR[j] + fxR[j]) * sOut };
            for (int c = 0; c < numCh; ++c)
            {
                // Safety ceiling: soft above -1 dBFS, never past 0 dBFS.
                float v = numCh == 1 ? 0.5f * (o[0] + o[1]) : o[c];
                const float a = std::abs (v), t = 0.891f;
                if (a > t) v = std::copysign (t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)), v);
                data[c][i] = v;
                blkOut = juce::jmax (blkOut, std::abs (v));
            }
        }
    }

    samplesProcessed += n;
    dispProgress.store (lastP);
    dispCycle.store (lastC);
    dispLevel.store (sBuild);
    dispBpm.store (bpm);
    dispBeatsPerBar.store (bpb);
    dispPlaying.store (playing);
    dispSinceDrop.store (dropAt >= 0 ? (float) (n - dropAt) / (float) sr : dispSinceDrop.load() + (float) n / (float) sr);
    inPeak.store (blkIn); outPeak.store (blkOut);
}

// ── State ─────────────────────────────────────────────────────────────────────
void AutoBuildProAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AutoBuildProAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (! xml || ! xml->hasTagName (apvts.state.getType())) return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* AutoBuildProAudioProcessor::createEditor() { return new AutoBuildProAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoBuildProAudioProcessor(); }
