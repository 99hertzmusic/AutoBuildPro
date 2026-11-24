// AutoBuild Pro offline test app.
// Usage: AutoBuildProTest <outdir> [stemsDir] [name=key ...]
//  - the build follows host tempo / position exactly (start, length in bars, drop sample), any block size,
//    time signatures, loops, WAIT bars, free-running clock with no transport, stop/start
//  - impact starts on the drop sample, the filter lets go on the drop
//  - save -> new instance -> restore
//  - clicks on start / stop / every control / Pro on-off / the drop itself
//  - output level per style (no clipping, gain-safe), old AutoBuild 1.x for comparison
//  - Lite can't use Pro features (automation, saved session, expired trial): bit-identical
//  - licence keys, sound examples (<outdir>/sounds), UI snapshots
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cmath>
#include <map>
#include "LicenceTests.h"

using APP = AutoBuildProAudioProcessor;
using Buf = juce::AudioBuffer<float>;
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}
static const double twoPi = juce::MathConstants<double>::twoPi;

// ── Host simulation ──────────────────────────────────────────────────────────
struct FakeHost : juce::AudioPlayHead
{
    double sr = 48000, bpm = 120, startPpq = 0; int num = 4, den = 4;
    bool playing = true, havePos = true;
    double loopStart = -1, loopEnd = -1;   // ppq, loop when set
    long long pos = 0;                     // transport samples since start
    long long audioPos = 0;                // input audio position (keeps running when stopped)
    double ppqNow() const
    {
        double p = startPpq + (double) ((long double) pos * (long double) bpm / (60.0L * (long double) sr));
        if (loopEnd > loopStart && loopStart >= 0)
            while (p >= loopEnd - 1.0e-12) p -= (loopEnd - loopStart);
        return p;
    }
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (! havePos) return {};
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppqNow()); p.setIsPlaying (playing);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { num, den });
        return p;
    }
};

static void setParam (juce::AudioProcessor& p, const char* id, float plain)
{
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (r->getParameterID() == id) { r->setValueNotifyingHost (r->convertTo0to1 (plain)); return; }
    std::printf ("  !! no parameter %s\n", id);
}

static void setPro (APP& p, int kind)
{
    hz::LicenceInfo li; li.kind = (hz::LicenceInfo::Kind) kind; li.email = "buyer@example.com";
    li.expires = kind == hz::LicenceInfo::trial ? juce::Time::currentTimeMillis() / 1000 + 5 * 86400 - 60 : 0;
    p.setLicenceInfo (li);
}

static void setStyle (APP& p, int s) { for (int l = 0; l < ab::numLayers; ++l) setParam (p, ab::layerId (l), ab::style (s).amount[l]); }
static int barIndex (int bars) { for (int i = 0; i < ab::numBarSteps; ++i) if (ab::barSteps[i] == bars) return i; return 3; }
static int waitIndex (int w) { for (int i = 0; i < ab::numWaitSteps; ++i) if (ab::waitSteps[i] == w) return i; return 0; }

// Process `in` (looped) for len samples with the host's transport advancing.
static Buf run (juce::AudioProcessor& p, FakeHost& h, const Buf& in, int len, int bs, std::function<void (int pos)> beforeBlock = nullptr)
{
    Buf out (2, len), blk (2, bs);
    juce::MidiBuffer m;
    for (int pos = 0; pos < len; pos += bs)
    {
        const int n = juce::jmin (bs, len - pos);
        if (beforeBlock) beforeBlock (pos);
        blk.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) blk.setSample (c, i, in.getSample (c, (int) ((h.audioPos + i) % in.getNumSamples())));
        p.processBlock (blk, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
        if (h.playing) h.pos += n;
        h.audioPos += n;
    }
    return out;
}

static std::unique_ptr<APP> makeProc (int licKind, FakeHost& h, int bs)
{
    auto p = std::make_unique<APP>();
    setPro (*p, licKind);
    p->setPlayHead (&h);
    p->setRateAndBufferSizeDetails (h.sr, bs);
    p->prepareToPlay (h.sr, bs);
    return p;
}

static void writeWav (const juce::File& f, const Buf& b, double rate)
{
    f.deleteFile();
    juce::WavAudioFormat w;
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release()))
        if (auto wr = std::unique_ptr<juce::AudioFormatWriter> (w.createWriterFor (os.get(), rate, 2, 24, {}, 0)))
        { os.release(); wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}

static Buf tone (double secs, double sr, double hz = 80.0, float amp = 0.4f)
{
    const int n = (int) (secs * sr);
    Buf b (2, n);
    for (int i = 0; i < n; ++i) { const float v = amp * (float) std::sin (twoPi * hz * i / sr); b.setSample (0, i, v); b.setSample (1, i, v); }
    return b;
}

// Synthetic music (if no stems): kick on the beat, bass, chord, hats at 128.
static Buf synthMusic (double secs, double sr, double bpm = 128)
{
    const int n = (int) (secs * sr);
    Buf b (2, n); b.clear();
    juce::Random rnd (7);
    const double beat = 60.0 / bpm;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr, tb = std::fmod (t, beat), t16 = std::fmod (t, beat / 4);
        const double kick = tb < 0.3 ? std::sin (twoPi * (48 * tb + 120 * (1 - std::exp (-30 * tb)) / 30)) * std::exp (-tb / 0.1) : 0.0;
        const double bass = 0.25 * std::sin (twoPi * 55 * t) * (tb > 0.1 ? 1.0 : tb / 0.1);
        const double chord = 0.08 * (std::sin (twoPi * 261.6 * t) + std::sin (twoPi * 311.1 * t) + std::sin (twoPi * 392 * t));
        const double hat = (rnd.nextFloat() * 2 - 1) * 0.12 * std::exp (-t16 / 0.015);
        b.setSample (0, i, (float) (0.6 * kick + bass + chord + hat * 0.7));
        b.setSample (1, i, (float) (0.6 * kick + bass + chord * 0.9 + hat));
    }
    return b;
}

static double rmsDb (const Buf& b, int from, int to)
{
    from = juce::jmax (0, from); to = juce::jmin (b.getNumSamples(), to);
    double s = 0; for (int c = 0; c < 2; ++c) for (int i = from; i < to; ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
    return 10 * std::log10 (s / juce::jmax (1, 2 * (to - from)) + 1e-20);
}
static double peakDb (const Buf& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    float pk = 0; for (int c = 0; c < 2; ++c) for (int i = from; i < to; ++i) pk = juce::jmax (pk, std::abs (b.getSample (c, i)));
    return juce::Decibels::gainToDecibels (pk, -150.0f);
}
static float maxDiff (const Buf& a, const Buf& b)
{
    float d = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i) d = juce::jmax (d, std::abs (a.getSample (c, i) - b.getSample (c, i)));
    return d;
}

// ── 1. Timing ────────────────────────────────────────────────────────────────
struct TimingCase { const char* name; double bpm, sr, startPpq; int bars, wait, num, den, bs; double cycles; };

static void timingTests (const Buf& music48)
{
    std::printf ("\n== Build follows the host: position, length, drop sample ==\n");
    const TimingCase cases[] = {
        { "120 BPM 48k 4/4, 8 bars, bs 256",          120.0, 48000, 0.0,   8, 0, 4, 4, 256, 2.3 },
        { "130 BPM 44.1k 4/4, 4 bars, mid-song, bs 37", 130.0, 44100, 10.37, 4, 0, 4, 4, 37,  3.2 },
        { "174 BPM 96k 7/8, 2 bars, bs 512",          174.0, 96000, 3.0,   2, 0, 7, 8, 512, 4.5 },
        { "90 BPM 48k 3/4, 1 bar, bs 1",              90.0,  48000, 0.0,   1, 0, 3, 4, 1,   3.4 },
        { "124 BPM 48k 4/4, WAIT 4 + 4 bars, bs 128", 124.0, 48000, 2.0,   4, 4, 4, 4, 128, 2.6 },
        { "128 BPM 48k 4/4, 32 bars, bs 1024",        128.0, 48000, 0.0,  32, 0, 4, 4, 1024, 1.2 },
    };
    for (auto& tc : cases)
    {
        FakeHost h; h.sr = tc.sr; h.bpm = tc.bpm; h.startPpq = tc.startPpq; h.num = tc.num; h.den = tc.den;
        auto p = makeProc (hz::LicenceInfo::pro, h, tc.bs);
        setParam (*p, "bars", (float) barIndex (tc.bars));
        setParam (*p, "wait", (float) waitIndex (tc.wait));
        p->prepareToPlay (h.sr, tc.bs);
        const double bpb = tc.num * 4.0 / tc.den, cycleBeats = (tc.bars + tc.wait) * bpb;
        const long double bps = (long double) tc.bpm / (60.0L * tc.sr);
        const int len = (int) (tc.cycles * cycleBeats / (double) bps);
        std::vector<float> trace; trace.reserve ((size_t) len);
        p->progressTrace = &trace; p->recordDrops = true;
        Buf silent (2, 4096); silent.clear();
        run (*p, h, silent, len, tc.bs);
        p->progressTrace = nullptr;

        double maxErr = 0; int startIdx = -1;
        std::vector<long long> expectDrops;
        for (int i = 0; i < len; ++i)
        {
            const long double ppq = tc.startPpq + (long double) i * bps;
            long double c = ppq / cycleBeats; c -= std::floor (c);
            const long double inBars = c * (tc.bars + tc.wait) - tc.wait;
            const double want = inBars <= 0 ? 0.0 : (double) (inBars / tc.bars);
            const double dist = std::min ((double) c, 1.0 - (double) c) * cycleBeats / (double) bps;   // samples to a wrap
            if (dist > 2.0) maxErr = std::max (maxErr, std::abs (want - (double) trace[(size_t) i]));
            if (startIdx < 0 && i > 0 && trace[(size_t) i] > 0.0f && trace[(size_t) i - 1] == 0.0f) startIdx = i;
        }
        for (long long k = (long long) std::floor (tc.startPpq / cycleBeats) + 1;; ++k)
        {
            const long double s = std::ceil (((long double) k * cycleBeats - tc.startPpq) / bps);
            if (s >= len) break;
            expectDrops.push_back ((long long) s);
        }
        long long worst = 0;
        bool sameCount = expectDrops.size() == p->dropSamples.size();
        for (size_t k = 0; k < juce::jmin (expectDrops.size(), p->dropSamples.size()); ++k)
            worst = std::max (worst, std::abs (expectDrops[k] - (long long) p->dropSamples[k]));
        std::printf ("  %-44s drops %zu/%zu, worst drop offset %lld samples, max position error %.2e\n",
                     tc.name, p->dropSamples.size(), expectDrops.size(), worst, maxErr);
        check (sameCount && worst == 0, juce::String (tc.name) + ": every drop on the exact bar-line sample");
        check (maxErr < 2.0e-5, juce::String (tc.name) + ": build position = host position");
        if (tc.wait > 0)
        {
            // build starts exactly WAIT bars after each drop
            const long long d0 = p->dropSamples.empty() ? -1 : p->dropSamples[0];
            const long long expectStart = d0 + (long long) std::llround ((double) (tc.wait * bpb) / (double) bps);
            long long firstRise = -1;
            for (long long i = d0 + 1; i < len; ++i) if (trace[(size_t) i] > 0.0f) { firstRise = i; break; }
            std::printf ("     build starts %lld samples after the drop (expect %lld)\n", firstRise - d0, expectStart - d0);
            check (std::llabs (firstRise - expectStart) <= 1, "WAIT: build starts exactly 4 bars after the drop");
        }
    }

    // Host loop: bars 1-8 looped, playback from bar 5: the loop jump is the drop.
    {
        FakeHost h; h.sr = 48000; h.bpm = 120; h.startPpq = 16.0; h.loopStart = 0.0; h.loopEnd = 32.0;
        auto p = makeProc (hz::LicenceInfo::none, h, 256);
        p->recordDrops = true;
        Buf silent (2, 4096); silent.clear();
        run (*p, h, silent, 48000 * 40, 256);
        const bool ok = p->dropSamples.size() >= 2 && p->dropSamples[0] == 384000 && p->dropSamples[1] == 384000 + 768000;
        std::printf ("  host loop bars 1-8 from bar 5: drops at %lld, %lld (expect 384000, 1152000)\n",
                     p->dropSamples.size() > 0 ? (long long) p->dropSamples[0] : -1LL, p->dropSamples.size() > 1 ? (long long) p->dropSamples[1] : -1LL);
        check (ok, "host loop: drop on the loop jump");
    }
    // Relocating backwards mid-build (bar 3 -> bar 1) is not a drop.
    {
        FakeHost h; h.sr = 48000; h.bpm = 120;
        auto p = makeProc (hz::LicenceInfo::pro, h, 256);
        p->recordDrops = true;
        Buf silent (2, 4096); silent.clear();
        run (*p, h, silent, 48000 * 5, 256);
        h.pos = 0;
        run (*p, h, silent, 48000 * 2, 256);
        check (p->dropSamples.empty(), "moving the playhead back to bar 1 from bar 3 is not a drop");
    }
    // No transport at all: free-running clock at 128 BPM (same clock as the standalone app).
    {
        FakeHost h; h.sr = 48000; h.havePos = false;
        auto p = makeProc (hz::LicenceInfo::none, h, 512);
        p->recordDrops = true;
        Buf silent (2, 4096); silent.clear();
        run (*p, h, silent, 48000 * 31, 512);
        const bool ok = p->dropSamples.size() == 2 && p->dropSamples[0] == 720000 && p->dropSamples[1] == 1440000;
        std::printf ("  no transport: drops at %lld, %lld (8 bars at 128 BPM = 720000)\n",
                     p->dropSamples.size() > 0 ? (long long) p->dropSamples[0] : -1LL, p->dropSamples.size() > 1 ? (long long) p->dropSamples[1] : -1LL);
        check (ok, "no transport: free-running 128 BPM clock, drop every 8 bars");
    }
    // Impact starts on the drop sample; filter lets go on the drop.
    {
        FakeHost h; h.sr = 48000; h.bpm = 120;
        auto p = makeProc (hz::LicenceInfo::pro, h, 256);
        setParam (*p, "bars", (float) barIndex (2)); setParam (*p, "impact", 100); setParam (*p, "downlifter", 0);
        setStyle (*p, ab::classic);
        p->recordDrops = true;
        Buf silent (2, 4096); silent.clear();
        const auto out = run (*p, h, silent, 48000 * 10, 256);
        int first = -1; for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 1.0e-6f) { first = i; break; }
        const long long d = p->dropSamples.empty() ? -1 : p->dropSamples[0];
        std::printf ("  impact: drop at %lld, first impact sample %d\n", d, first);
        check (d == 192000 && first >= d && first <= d + 1, "impact starts on the drop sample (2 bars at 120 = 192000)");
    }
    {
        FakeHost h; h.sr = 48000; h.bpm = 120;
        auto p = makeProc (hz::LicenceInfo::none, h, 256);
        setParam (*p, "build", 100);
        const auto in = tone (20, 48000, 80);
        const auto out = run (*p, h, in, 48000 * 18, 256);
        const int d = 768000;
        const double before = rmsDb (out, d - 4800, d), after = rmsDb (out, d + 480, d + 5280), inDb = rmsDb (in, 0, 48000);
        std::printf ("  80 Hz tone, BUILD 100: last 100 ms before drop %.1f dB, 10-110 ms after %.1f dB (input %.1f dB)\n", before, after, inDb);
        check (after - before > 20.0 && std::abs (after - inDb) < 1.0, "the drop lets the low end straight back in");
    }
    // PITCH layer: the tone rises to an octave at the top of the build.
    {
        FakeHost h; h.sr = 48000; h.bpm = 120;
        auto p = makeProc (hz::LicenceInfo::pro, h, 256);
        setParam (*p, "bars", (float) barIndex (2)); setParam (*p, "build", 100);
        for (int l = 0; l < ab::numLayers; ++l) setParam (*p, ab::layerId (l), l == ab::pitch ? 100.0f : 0.0f);
        setParam (*p, "impact", 0); setParam (*p, "downlifter", 0);
        const auto in = tone (5, 48000, 300, 0.3f);
        const auto out = run (*p, h, in, 192000, 256);
        auto zc = [&] (int a, int b) { int z = 0; for (int i = a + 1; i < b; ++i) if ((out.getSample (0, i - 1) < 0) != (out.getSample (0, i) < 0)) ++z; return z * 0.5 * 48000.0 / (b - a); };
        const double f0 = zc (4800, 24000), fMid = zc (96000 - 4800, 96000 + 4800), fTop = zc (192000 - 14400, 192000 - 2400);
        std::printf ("  PITCH layer on a 300 Hz tone: %.0f Hz at the start, %.0f Hz halfway, %.0f Hz at the top\n", f0, fMid, fTop);
        check (std::abs (f0 - 300) < 10 && fTop > 540 && fTop < 610, "PITCH rises to about an octave by the drop");
    }
    // Stop: back to dry, start again: picks up the right bar.
    {
        FakeHost h; h.sr = 48000; h.bpm = 120;
        auto p = makeProc (hz::LicenceInfo::none, h, 256);
        setParam (*p, "build", 100);
        const auto in = tone (30, 48000, 200);
        run (*p, h, in, 48000 * 13, 256);              // bar 7.5 of 8
        h.playing = false;
        const auto out = run (*p, h, in, 48000, 256);
        const double dry = rmsDb (in, 0, 48000), stopped = rmsDb (out, 12000, 48000);
        std::printf ("  stopped mid-build: output %.2f dB vs input %.2f dB after 250 ms\n", stopped, dry);
        check (std::abs (stopped - dry) < 0.5, "stopping the transport returns to dry");
    }
}

// ── 2. State ─────────────────────────────────────────────────────────────────
static void stateTests (const Buf& music)
{
    std::printf ("\n== Save -> new instance -> restore ==\n");
    FakeHost h; h.sr = 48000; h.bpm = 128;
    auto a = makeProc (hz::LicenceInfo::pro, h, 256);
    setParam (*a, "build", 83.0f); setParam (*a, "bars", 2); setParam (*a, "wait", 3); setParam (*a, "curve", 37.0f);
    setParam (*a, "noise", 55.0f); setParam (*a, "pitch", 22.0f); setParam (*a, "stutter", 66.0f); setParam (*a, "wash", 11.0f); setParam (*a, "filter", 44.0f);
    setParam (*a, "impact", 31.0f); setParam (*a, "downlifter", 77.0f); setParam (*a, "output", -3.5f); setParam (*a, "manual", 1.0f);
    juce::MemoryBlock mb; a->getStateInformation (mb);
    FakeHost h1, h2; h1.sr = h2.sr = 48000; h1.bpm = h2.bpm = 128;
    auto b = std::make_unique<APP>(); setPro (*b, hz::LicenceInfo::pro); b->setPlayHead (&h1);
    b->setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (auto* prm : a->getParameters())
    {
        auto* ra = dynamic_cast<juce::RangedAudioParameter*> (prm);
        const float va = ra->getValue(), vb = b->apvts.getParameter (ra->getParameterID())->getValue();
        if (std::abs (va - vb) > 1e-6f) { same = false; std::printf ("  %s differs: %f vs %f\n", ra->getParameterID().toRawUTF8(), va, vb); }
    }
    check (same, "every parameter survives save/restore (" + juce::String (a->getParameters().size()) + " params)");
    auto c = std::make_unique<APP>(); setPro (*c, hz::LicenceInfo::pro); c->setPlayHead (&h2);
    c->setStateInformation (mb.getData(), (int) mb.getSize());
    setParam (*b, "manual", 0); setParam (*c, "manual", 0);
    b->setRateAndBufferSizeDetails (48000, 256); b->prepareToPlay (48000, 256);
    c->setRateAndBufferSizeDetails (48000, 256); c->prepareToPlay (48000, 256);
    const auto o1 = run (*b, h1, music, 48000 * 12, 256), o2 = run (*c, h2, music, 48000 * 12, 256);
    check (maxDiff (o1, o2) == 0.0f, "two restored instances sound identical");
    { std::unique_ptr<juce::AudioProcessorEditor> ed (b->createEditor()); }
    check (std::abs (b->apvts.getRawParameterValue ("build")->load() - 83.0f) < 0.05f, "opening the window changes nothing");
}

// ── 3. Clicks ────────────────────────────────────────────────────────────────
struct HF { juce::dsp::IIR::Filter<float> a, b; HF (double sr) { a.coefficients = b.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 2500.0f); } float operator() (float x) { return b.processSample (a.processSample (x)); } };

static void clickTests()
{
    std::printf ("\n== Clicks (80 Hz bass tone; HF > 2.5 kHz just after each change vs the rest) ==\n");
    struct Cat { const char* name; std::function<void (APP&, FakeHost&, int)> change; std::function<void (APP&)> setup; };
    auto noGen = [] (APP& p) { setParam (p, "impact", 0); setParam (p, "downlifter", 0); setParam (p, "noise", 0); };
    std::vector<Cat> cats = {
        { "start/stop",     [] (APP&, FakeHost& h, int k) { h.playing = k % 2 == 0; }, noGen },
        { "build 0/100",    [] (APP& p, FakeHost&, int k) { setParam (p, "build", k % 2 ? 0.0f : 100.0f); }, noGen },
        { "bars",           [] (APP& p, FakeHost&, int k) { setParam (p, "bars", (float) (k % 6)); }, noGen },
        { "wait",           [] (APP& p, FakeHost&, int k) { setParam (p, "wait", (float) (k % 7)); }, noGen },
        { "curve",          [] (APP& p, FakeHost&, int k) { setParam (p, "curve", k % 2 ? -100.0f : 100.0f); }, noGen },
        { "manual on/off",  [] (APP& p, FakeHost&, int k) { setParam (p, "manual", (float) (k % 2)); }, noGen },
        { "style",          [] (APP& p, FakeHost&, int k) { int s = k % 5; if (s >= ab::riser) ++s; setStyle (p, s); setParam (p, "noise", 0); }, noGen },
        { "pitch 0/100",    [] (APP& p, FakeHost&, int k) { setParam (p, "pitch", k % 2 ? 0.0f : 100.0f); }, noGen },
        { "stutter 0/100",  [] (APP& p, FakeHost&, int k) { setParam (p, "stutter", k % 2 ? 0.0f : 100.0f); }, noGen },
        { "wash 0/100",     [] (APP& p, FakeHost&, int k) { setParam (p, "wash", k % 2 ? 0.0f : 100.0f); }, noGen },
        { "filter 0/100",   [] (APP& p, FakeHost&, int k) { setParam (p, "filter", k % 2 ? 0.0f : 100.0f); }, noGen },
        { "output",         [] (APP& p, FakeHost&, int k) { setParam (p, "output", k % 2 ? -12.0f : 6.0f); }, noGen },
        { "Pro on/off",     [] (APP& p, FakeHost&, int k) { setPro (p, k % 2 ? hz::LicenceInfo::none : hz::LicenceInfo::pro); },
                            [] (APP& p) { setStyle (p, ab::pitchUp); setParam (p, "curve", 80); setParam (p, "impact", 0); setParam (p, "downlifter", 0); } },
    };
    const double SR = 48000; const int BS = 256;
    const double secs = 8.0; const int total = (int) (secs * SR);
    const auto in = tone (secs + 1, SR, 80, 0.4f);
    std::printf ("%-16s %12s %12s %8s\n", "change", "steady dB", "change dB", "click");
    for (auto& cat : cats)
    {
        FakeHost h; h.sr = SR; h.bpm = 120;
        auto p = makeProc (hz::LicenceInfo::pro, h, BS);
        setParam (*p, "bars", 1);
        if (cat.setup) cat.setup (*p);
        p->prepareToPlay (SR, BS);
        HF hf (SR); float steady = 0, sw = 0;
        const int every = (int) (0.25 * SR) / BS * BS, after = (int) (0.06 * SR);
        int k = 0, lastChange = -1000000;
        Buf blk (2, BS); juce::MidiBuffer m;
        for (int pos = 0; pos < total; pos += BS)
        {
            if (pos >= (int) (1.0 * SR) && pos % every == 0) { cat.change (*p, h, ++k); lastChange = pos; }
            for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, in, c, pos, BS);
            p->processBlock (blk, m);
            if (h.playing) h.pos += BS;
            for (int i = 0; i < BS; ++i)
            {
                const float y = std::abs (hf (blk.getSample (0, i)));
                if (pos + i < (int) (0.8 * SR)) continue;
                if (pos + i - lastChange < after) sw = juce::jmax (sw, y);
                else steady = juce::jmax (steady, y);
            }
        }
        const float sDb = juce::Decibels::gainToDecibels (steady, -150.0f), wDb = juce::Decibels::gainToDecibels (sw, -150.0f);
        const bool ok = wDb < sDb + 6.0f || wDb < -70.0f;
        std::printf ("%-16s %12.1f %12.1f %8s\n", cat.name, sDb, wDb, ok ? "none" : "CLICK");
        check (ok, juce::String ("no click: ") + cat.name);
    }

    // The drop itself (every style): HF just after the drop vs. the 2 s before it.
    std::printf ("\n  the drop (80 Hz tone, 1-bar build at 120, BUILD 100, no impact/downlifter):\n");
    const char* exp = std::getenv ("ABP_DROPEXP");
    for (int s = 0; s < ab::numStyles + (exp ? 6 : 0); ++s)
    {
        if (exp && s < ab::numStyles) continue;
        FakeHost h; h.sr = SR; h.bpm = 120;
        auto p = makeProc (hz::LicenceInfo::pro, h, BS);
        setParam (*p, "bars", 1); setParam (*p, "build", 100); setStyle (*p, s % ab::numStyles); setParam (*p, "noise", 0);
        setParam (*p, "impact", 0); setParam (*p, "downlifter", 0);
        if (s >= ab::numStyles)
        {
            const float v[6][5] = { { 30, 0, 0, 0, 0 }, { 0, 0, 0, 0, 100 }, { 0, 0, 100, 0, 0 }, { 100, 0, 0, 0, 0 }, { 30, 0, 0, 0, 100 }, { 0, 0, 0, 0, 40 } };
            for (int l = 0; l < 5; ++l) setParam (*p, ab::layerId (l), v[s - ab::numStyles][l]);
        }
        p->recordDrops = true;
        const auto out = run (*p, h, in, (int) (6.5 * SR), BS);
        HF hf (SR); float steady = 0, sw = 0;
        std::vector<int> drops; for (auto d : p->dropSamples) drops.push_back ((int) d);
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const float y = std::abs (hf (out.getSample (0, i)));
            if (i < (int) (0.5 * SR)) continue;
            bool near = false; for (int d : drops) near = near || (i >= d && i < d + (int) (0.03 * SR));
            if (near) sw = juce::jmax (sw, y); else steady = juce::jmax (steady, y);
        }
        const float sDb = juce::Decibels::gainToDecibels (steady, -150.0f), wDb = juce::Decibels::gainToDecibels (sw, -150.0f);
        const bool ok = wDb < sDb + 6.0f || wDb < -70.0f;
        std::printf ("  %-10s steady %7.1f dB, at the drop %7.1f dB  %s\n", s < ab::numStyles ? ab::style (s).name : "EXP", sDb, wDb, ok ? "none" : "CLICK");
        check (ok, juce::String ("no click at the drop: ") + ab::style (s % ab::numStyles).name);
    }
}

// ── 4. Lite gating ───────────────────────────────────────────────────────────
static void liteTests (const Buf& music)
{
    std::printf ("\n== Lite can't use Pro features ==\n");
    const double SR = 48000; const int BS = 256, len = (int) (SR * 20);
    auto render = [&] (int kind, std::function<void (APP&)> setup, const juce::MemoryBlock* state = nullptr)
    {
        FakeHost h; h.sr = SR; h.bpm = 128;
        auto p = std::make_unique<APP>(); setPro (*p, kind); p->setPlayHead (&h);
        if (state) p->setStateInformation (state->getData(), (int) state->getSize());
        if (setup) setup (*p);
        p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
        return run (*p, h, music, len, BS);
    };
    auto proSettings = [] (APP& p)
    {
        setStyle (p, ab::mega); setParam (p, "curve", 100); setParam (p, "impact", 100); setParam (p, "downlifter", 100); setParam (p, "wait", 3);
    };
    const auto ref = render (hz::LicenceInfo::none, [] (APP& p) { setParam (p, "impact", 0); setParam (p, "downlifter", 0); });
    const auto refDefault = render (hz::LicenceInfo::none, nullptr);
    check (maxDiff (ref, refDefault) == 0.0f, "Lite with default Pro values = Lite with them at 0");
    const auto lite = render (hz::LicenceInfo::none, proSettings);
    check (maxDiff (ref, lite) == 0.0f, "Lite + automation to MEGA / curve 100 / impact 100 / downlifter 100 / wait 8 = plain Lite (max diff " + juce::String (maxDiff (ref, lite)) + ")");
    for (int s = 1; s < ab::numStyles; ++s)
    {
        const auto o = render (hz::LicenceInfo::none, [s] (APP& p) { setStyle (p, s); });
        check (maxDiff (ref, o) == 0.0f, juce::String ("Lite + ") + ab::style (s).name + " amounts = CLASSIC");
    }
    juce::MemoryBlock mb;
    {
        FakeHost h; auto pro = std::make_unique<APP>(); setPro (*pro, hz::LicenceInfo::pro); proSettings (*pro); pro->getStateInformation (mb);
    }
    check (maxDiff (ref, render (hz::LicenceInfo::none, nullptr, &mb)) == 0.0f, "Pro session opened in Lite falls back to plain Lite");
    check (maxDiff (ref, render (hz::LicenceInfo::trialExpired, nullptr, &mb)) == 0.0f, "expired trial = Lite");
    check (maxDiff (ref, render (hz::LicenceInfo::invalid, nullptr, &mb)) == 0.0f, "invalid key = Lite");
    const auto proOut = render (hz::LicenceInfo::pro, nullptr, &mb);
    check (maxDiff (ref, proOut) > 0.05f, "sanity: the same session DOES change the sound in Pro");
    check (maxDiff (ref, render (hz::LicenceInfo::trial, nullptr, &mb)) > 0.05f, "sanity: trial = Pro");
    check (maxDiff (ref, render (hz::LicenceInfo::none, [] (APP& p) { setParam (p, "bars", 2); setParam (p, "build", 100); })) > 0.01f,
           "BARS and BUILD work in Lite");
}

// ── 5. Levels + old AutoBuild ────────────────────────────────────────────────
struct LevelStats { double inRms, outRms, maxShortRise, peak; };
static LevelStats levelStats (const Buf& in, const Buf& out, double sr)
{
    const int win = (int) (0.4 * sr);
    LevelStats st { rmsDb (in, 0, out.getNumSamples()), rmsDb (out, 0, out.getNumSamples()), -100, peakDb (out) };
    for (int i = 0; i + win <= out.getNumSamples(); i += win / 2)
        st.maxShortRise = std::max (st.maxShortRise, rmsDb (out, i, i + win) - std::max (rmsDb (in, i, i + win), st.inRms - 6.0));   // quiet input bits: vs the song level
    return st;
}

static void levelTests (const Buf& music, double sr, double bpm)
{
    std::printf ("\n== Output level: 2 x 8-bar builds of the music, BUILD 100 ==\n");
    std::printf ("  %-26s %8s %8s %16s %9s\n", "", "in RMS", "out RMS", "max 400ms rise", "peak");
    const int len = (int) (2 * 32 * 60.0 / bpm * sr) + (int) (2 * sr);
    for (int r = -1; r < 2 * ab::numStyles; ++r)
    {
        const int s = r < 0 ? -1 : r % ab::numStyles;
        const bool gens = r >= ab::numStyles;
        FakeHost h; h.sr = sr; h.bpm = bpm;
        auto p = makeProc (s < 0 ? hz::LicenceInfo::none : hz::LicenceInfo::pro, h, 512);
        setParam (*p, "build", 100);
        if (s >= 0) { setStyle (*p, s); setParam (*p, "impact", gens ? 100.0f : 0.0f); setParam (*p, "downlifter", gens ? 100.0f : 0.0f); }
        const auto out = run (*p, h, music, len, 512);
        Buf inAligned (2, len); for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) inAligned.setSample (c, i, music.getSample (c, i % music.getNumSamples()));
        const auto st = levelStats (inAligned, out, sr);
        const juce::String name = s < 0 ? juce::String ("Lite CLASSIC") : juce::String ("Pro ") + ab::style (s).name + (gens ? " +impact+down 100" : "");
        std::printf ("  %-26s %8.1f %8.1f %13.1f dB %6.1f dB\n", name.toRawUTF8(), st.inRms, st.outRms, st.maxShortRise, st.peak);
        check (st.peak <= 0.0, name + ": never clips (peak " + juce::String (st.peak, 2) + " dBFS)");
        const double lim = gens ? 6.5 : (s < 0 ? 2.5 : 3.0);
        check (st.maxShortRise < lim, name + ": never more than " + juce::String (lim, 1) + " dB louder than the input (400 ms windows)");
    }
    {
        FakeHost h; h.sr = sr; h.bpm = bpm;
        auto p = makeProc (hz::LicenceInfo::pro, h, 512);
        setParam (*p, "build", 100); setStyle (*p, ab::mega); setParam (*p, "impact", 100); setParam (*p, "downlifter", 100); setParam (*p, "output", 12);
        Buf hot (music); hot.applyGain (4.0f);
        const auto out = run (*p, h, hot, len, 512);
        check (peakDb (out) <= 0.0, "hot input (+12 dB) and OUTPUT +12: still never past 0 dBFS (" + juce::String (peakDb (out), 2) + ")");
    }

    // The released free AutoBuild (VST3) on the same music, same transport, its defaults with INTENSITY 100.
    const auto old = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Plug-Ins/VST3/AutoBuild.vst3");
    if (! old.exists()) { std::printf ("  (old AutoBuild.vst3 not installed: skipped)\n"); return; }
    juce::AudioPluginFormatManager fm; fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    juce::KnownPluginList kl;
    kl.scanAndAddFile (old.getFullPathName(), true, descs, *fm.getFormat (0));
    if (descs.isEmpty()) { std::printf ("  (couldn't load old AutoBuild)\n"); return; }
    juce::String err;
    auto inst = fm.createPluginInstance (*descs[0], sr, 512, err);
    if (! inst) { std::printf ("  (couldn't instantiate old AutoBuild: %s)\n", err.toRawUTF8()); return; }
    FakeHost h; h.sr = sr; h.bpm = bpm;
    inst->setPlayHead (&h);
    inst->setRateAndBufferSizeDetails (sr, 512);
    inst->prepareToPlay (sr, 512);
    for (auto* prm : inst->getParameters())
        if (prm->getName (32) == "Intensity") prm->setValueNotifyingHost (1.0f);
    const auto out = run (*inst, h, music, len, 512);
    Buf inAligned (2, len); for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) inAligned.setSample (c, i, music.getSample (c, i % music.getNumSamples()));
    const auto st = levelStats (inAligned, out, sr);
    std::printf ("  %-26s %8.1f %8.1f %13.1f dB %6.1f dB   (for comparison, not a test)\n", "OLD AutoBuild 1.x", st.inRms, st.outRms, st.maxShortRise, st.peak);
    inst->releaseResources();
}

// ── 6. Sound examples ────────────────────────────────────────────────────────
static void examples (const Buf& music, double sr, double bpm, const juce::File& dir)
{
    std::printf ("\n== Sound examples (%s) ==\n", dir.getFullPathName().toRawUTF8());
    dir.createDirectory();
    const double barSecs = 4 * 60.0 / bpm;
    writeWav (dir.getChildFile ("00-dry-loop.wav"), [&] { Buf b (2, (int) (12 * barSecs * sr)); for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, music.getSample (c, i % music.getNumSamples())); return b; }(), sr);
    struct Ex { const char* file; int lic; int style; float impact, down; int wait; };
    const Ex ex[] = {
        { "01-lite-classic.wav",           hz::LicenceInfo::none, ab::classic,     0, 0,  0 },
        { "02-pro-classic-impact-down.wav", hz::LicenceInfo::pro, ab::classic,    70, 60, 4 },
        { "03-pro-riser.wav",              hz::LicenceInfo::pro, ab::riser,       70, 60, 4 },
        { "04-pro-pitch.wav",              hz::LicenceInfo::pro, ab::pitchUp,     70, 60, 4 },
        { "05-pro-stutter.wav",            hz::LicenceInfo::pro, ab::stutterRoll, 70, 60, 4 },
        { "06-pro-wash.wav",               hz::LicenceInfo::pro, ab::washOut,     70, 60, 4 },
        { "07-pro-mega.wav",               hz::LicenceInfo::pro, ab::mega,        80, 70, 4 },
    };
    for (auto& e : ex)
    {
        FakeHost h; h.sr = sr; h.bpm = bpm;
        auto p = makeProc (e.lic, h, 512);
        setParam (*p, "build", 85);
        if (e.lic == hz::LicenceInfo::pro) { setStyle (*p, e.style); setParam (*p, "impact", e.impact); setParam (*p, "downlifter", e.down); setParam (*p, "wait", (float) waitIndex (e.wait)); }
        const int bars = e.wait + 8 + 4;     // wait, 8-bar build, drop, 4 bars after
        const auto out = run (*p, h, music, (int) (bars * barSecs * sr), 512);
        writeWav (dir.getChildFile (e.file), out, sr);
        std::printf ("  %-34s %2d bars (%s8-bar build, drop at bar %d), peak %.1f dBFS\n", e.file, bars,
                     e.wait > 0 ? "4 bars dry, " : "", e.wait + 9, peakDb (out));
    }
}

// ── 7. UI ────────────────────────────────────────────────────────────────────
static void snapshots (const juce::File& outDir, const Buf& music)
{
    std::printf ("\n== UI snapshots ==\n");
    auto snap = [&] (const char* name, int licKind, const char* panel, std::function<void (APP&, FakeHost&)> setup, double playSecs = 9.3)
    {
        FakeHost h; h.sr = 48000; h.bpm = 128;
        auto p = makeProc (licKind, h, 512);
        if (setup) setup (*p, h);
        p->prepareToPlay (48000, 512);
        run (*p, h, music, (int) (playSecs * 48000), 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) for (int i = 0; i < 3; ++i) t->timerCallback();
        if (panel != nullptr)
            for (auto* c : ed->getChildren())
                if (auto* pp = dynamic_cast<ProPanel*> (c)) pp->show (panel);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream fo (outDir.getChildFile (name));
        fo.setPosition (0); fo.truncate();
        juce::PNGImageFormat().writeImageToStream (img, fo);
        std::printf ("  %s\n", name);
    };
    snap ("ui-1-lite.png", hz::LicenceInfo::none, nullptr, nullptr);
    snap ("ui-2-lite-pro-panel.png", hz::LicenceInfo::none, "STUTTER IS AN UPGRADE BUILD STYLE", nullptr);
    snap ("ui-3-trial.png", hz::LicenceInfo::trial, nullptr, [] (APP& p, FakeHost&) { setStyle (p, ab::riser); });
    snap ("ui-4-trial-ended.png", hz::LicenceInfo::trialExpired, nullptr, nullptr);
    snap ("ui-5-trial-ended-panel.png", hz::LicenceInfo::trialExpired, "", nullptr);
    snap ("ui-6-pro.png", hz::LicenceInfo::pro, nullptr, [] (APP& p, FakeHost&)
          { setStyle (p, ab::mega); setParam (p, "curve", 45); setParam (p, "wait", 3); setParam (p, "build", 90); setParam (p, "impact", 80); setParam (p, "downlifter", 65); }, 22.0);
    snap ("ui-7-pro-custom-stutter.png", hz::LicenceInfo::pro, nullptr, [] (APP& p, FakeHost&)
          { setStyle (p, ab::stutterRoll); setParam (p, "wash", 50); setParam (p, "bars", 2); setParam (p, "curve", -40); setParam (p, "impact", 0); }, 3.3);
    snap ("ui-8-pro-panel.png", hz::LicenceInfo::pro, "", nullptr);
    snap ("ui-9-manual.png", hz::LicenceInfo::none, nullptr, [] (APP& p, FakeHost&) { setParam (p, "manual", 1); setParam (p, "build", 55); });
    snap ("ui-10-stopped.png", hz::LicenceInfo::none, nullptr, [] (APP&, FakeHost& h) { h.playing = false; });
    // Licence states (Launch prep renders)
    snap ("licence-1-free.png", hz::LicenceInfo::none, nullptr, nullptr);
    snap ("licence-2-free-panel.png", hz::LicenceInfo::none, "", nullptr);
    snap ("licence-3-trial.png", hz::LicenceInfo::trial, nullptr, [] (APP& p, FakeHost&) { setStyle (p, ab::riser); });
    snap ("licence-4-trial-panel.png", hz::LicenceInfo::trial, "", nullptr);
    snap ("licence-5-trial-ended.png", hz::LicenceInfo::trialExpired, nullptr, nullptr);
    snap ("licence-6-trial-ended-panel.png", hz::LicenceInfo::trialExpired, "", nullptr);
    snap ("licence-7-licensed.png", hz::LicenceInfo::pro, nullptr, [] (APP& p, FakeHost&) { setStyle (p, ab::mega); });
    snap ("licence-8-licensed-panel.png", hz::LicenceInfo::pro, "", nullptr);
    snap ("licence-9-saved-upgrade-settings.png", hz::LicenceInfo::trialExpired, nullptr, [] (APP& p, FakeHost&)
          { setStyle (p, ab::riser); setParam (p, "curve", 45); setParam (p, "wait", 2); setParam (p, "impact", 85); });
    snap ("ui-11-pro-32-bars.png", hz::LicenceInfo::pro, nullptr, [] (APP& p, FakeHost&) { setParam (p, "bars", 5); setParam (p, "wait", 6); setStyle (p, ab::stutterRoll); }, 100.0);
}

static Buf loadStems (const juce::File& dir, double& sr)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    struct S { const char* name; float gain, pan; };
    const S stems[] = { { "groove.wav", 1.0f, 0.0f }, { "bass.wav", 1.0f, 0.0f }, { "synth.wav", 0.9f, -0.15f }, { "hat.wav", 0.8f, 0.25f } };
    Buf mix;
    for (auto& s : stems)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (dir.getChildFile (s.name)));
        if (! r) continue;
        sr = r->sampleRate;
        const int len = (int) r->lengthInSamples;
        Buf x (2, len); r->read (&x, 0, len, 0, true, true);
        if (mix.getNumSamples() == 0) { mix.setSize (2, len); mix.clear(); }
        const float gl = s.gain * std::sqrt (0.5f * (1.0f - s.pan)) * 1.41421f, gr = s.gain * std::sqrt (0.5f * (1.0f + s.pan)) * 1.41421f;
        mix.addFrom (0, 0, x, 0, 0, juce::jmin (len, mix.getNumSamples()), gl);
        mix.addFrom (1, 0, x, r->numChannels > 1 ? 1 : 0, 0, juce::jmin (len, mix.getNumSamples()), gr);
    }
    if (mix.getNumSamples() > 0) mix.applyGain (juce::Decibels::decibelsToGain (-6.0f) / juce::jmax (1e-9f, mix.getMagnitude (0, mix.getNumSamples())));
    return mix;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    double sr = 44100, bpm = 130;
    Buf music;
    if (argc > 2 && ! juce::String (argv[2]).containsChar ('=')) music = loadStems (juce::File (juce::String (argv[2])), sr);
    if (music.getNumSamples() == 0) { std::printf ("(no stems: synthetic music)\n"); sr = 48000; bpm = 128; music = synthMusic (15, sr); }
    else std::printf ("music: %s, %.0f Hz, %.2f s, %.0f BPM\n", argv[2], sr, music.getNumSamples() / sr, bpm);
    const auto music48 = synthMusic (15, 48000);

    timingTests (music48);
    stateTests (music48);
    clickTests();
    liteTests (music48);
    levelTests (music, sr, bpm);
    examples (music, sr, bpm, outDir.getChildFile ("sounds"));
    snapshots (outDir, music48);

    std::printf ("\n== Licence keys ==\n");
    for (int a = 2; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (! arg.containsChar ('=')) continue;
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false);
        juce::String key = arg.fromFirstOccurrenceOf ("=", false, false);
        if (juce::File::isAbsolutePath (key) && juce::File (key).existsAsFile()) key = juce::File (key).loadFileAsString();
        const auto info = hz::Licence::verify (key, "autobuildpro");
        const char* kinds[] = { "none", "trial", "pro", "trialExpired", "invalid" };
        std::printf ("  key %-14s -> %-12s daysLeft=%d\n", name.toRawUTF8(), kinds[(int) info.kind], info.daysLeft());
        hz::LicenceInfo::Kind want = hz::LicenceInfo::invalid;
        if (name == "pro" || name == "all") want = hz::LicenceInfo::pro;
        else if (name == "trial") want = hz::LicenceInfo::trial;
        else if (name == "expired") want = hz::LicenceInfo::trialExpired;
        check (info.kind == want, "key " + name + " -> " + kinds[(int) info.kind]);
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);

    hztest::runLicenceTests<APP> ("autobuildpro", [] (APP& p) { return p.proUnlocked.load(); });
    hztest::runSavedSettingsTests<APP> ([] (APP& p)
    {
        setStyle (p, ab::stutterRoll); setParam (p, "curve", -40); setParam (p, "wait", 3); setParam (p, "impact", 90); setParam (p, "downlifter", 10);
    });
    {
        // each upgrade-only control is flagged on its own, and plain Lite controls never are
        auto flagged = [] (std::function<void (APP&)> f) { APP p; p.setLicenceInfo ({}); f (p); return p.usesUpgradeSettings(); };
        hztest::expect (flagged ([] (APP& p) { setParam (p, "curve", 30); }), "saved CURVE is flagged");
        hztest::expect (flagged ([] (APP& p) { setParam (p, "wait", 2); }), "saved WAIT is flagged");
        hztest::expect (flagged ([] (APP& p) { setStyle (p, ab::riser); }), "saved RISER style is flagged");
        hztest::expect (flagged ([] (APP& p) { setParam (p, "impact", 10); }), "saved IMPACT is flagged");
        hztest::expect (! flagged ([] (APP& p) { setParam (p, "build", 30); setParam (p, "bars", 1); setParam (p, "output", -3); setParam (p, "manual", 1); }),
                        "Lite controls (BUILD, BARS, OUTPUT, MANUAL) aren't flagged");
    }
    const int lic = hztest::finish();
    return (failures == 0 && lic == 0) ? 0 : 1;
}
