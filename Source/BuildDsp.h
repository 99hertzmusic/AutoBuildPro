#pragma once
// AutoBuild Pro DSP building blocks. Everything here is allocation-free after prepare().
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include <cstdint>

namespace ab
{
static constexpr float pi = 3.14159265358979f;

// 0 below a, 1 above b, smooth (zero slope) in between: no kinks in a moving control = no clicks.
inline float smoothStep (float a, float b, float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

inline float onePoleCoef (double sr, double seconds) { return (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0e-5, seconds) * sr))); }

// Deterministic white noise (same seed = same output: Lite/Pro bit-identical tests rely on it).
struct Noise
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) { s = v ? v : 1u; }
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

// Zero-delay-feedback state-variable filter (Cytomic / Simper), one channel.
struct Svf
{
    float a1 = 1, a2 = 0, a3 = 0, k = 1.4f, ic1 = 0, ic2 = 0;
    void set (double sr, float hz, float q)
    {
        hz = juce::jlimit (5.0f, (float) (sr * 0.45), hz);
        const float g = std::tan (pi * hz / (float) sr);
        k = 1.0f / juce::jmax (0.1f, q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns high-pass; band / low in out params
    inline float process (float v0, float& band, float& low)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        band = v1; low = v2;
        return v0 - k * v1 - v2;
    }
};

// Ring buffer with fractional (linear) reads.
struct Ring
{
    std::vector<float> d; int mask = 0, w = 0;
    void prepare (int minSize)
    {
        int n = 1; while (n < minSize + 4) n <<= 1;
        d.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (d.begin(), d.end(), 0.0f); w = 0; }
    inline void push (float x) { d[(size_t) w] = x; w = (w + 1) & mask; }
    // delay in samples behind the most recent push (>= 1)
    inline float read (float delay) const
    {
        const float pos = (float) w - delay;
        const float fl = std::floor (pos);
        const int i0 = (int) fl;
        const float fr = pos - fl;
        const float x0 = d[(size_t) (i0 & mask)], x1 = d[(size_t) ((i0 + 1) & mask)];
        return x0 + (x1 - x0) * fr;
    }
};

// Two-tap granular pitch shifter (delay-line "doppler" taps with sin^2 crossfades). Upward only.
struct PitchShifter
{
    Ring r[2]; float win = 2400.0f, ph = 0.0f;
    void prepare (double sr) { win = (float) (0.05 * sr); for (auto& x : r) x.prepare ((int) win + 8); ph = 0.0f; }
    void reset() { for (auto& x : r) x.clear(); ph = 0.0f; }
    inline void process (float& l, float& rr, float ratio)
    {
        r[0].push (l); r[1].push (rr);
        ph += (ratio - 1.0f) / win;
        ph -= std::floor (ph);
        float outL = 0, outR = 0;
        for (int t = 0; t < 2; ++t)
        {
            float f = ph + 0.5f * (float) t; f -= std::floor (f);
            const float delay = 2.0f + win * (1.0f - f);
            const float s = std::sin (pi * f); const float g = s * s;
            outL += g * r[0].read (delay); outR += g * r[1].read (delay);
        }
        l = outL; rr = outR;
    }
};

// Generated drop impact: sub boom (pitch-swept sine) + noise crack. No samples.
struct Impact
{
    bool active = false; double sr = 48000; int t = 0, len = 0;
    float level = 0, phase = 0, hpL = 0, hpR = 0, prevL = 0, prevR = 0;
    Noise n;
    void prepare (double rate) { sr = rate; active = false; n.seed (0x1234567u); }
    void trigger (float peak) { active = true; t = 0; len = (int) (2.5 * sr); level = peak; phase = 0; hpL = hpR = prevL = prevR = 0; n.seed (0x1234567u); }
    inline void process (float& l, float& r, float& sendL, float& sendR)
    {
        l = r = sendL = sendR = 0.0f;
        if (! active) return;
        const float ts = (float) t / (float) sr;
        const float f = 42.0f + 78.0f * std::exp (-ts / 0.055f);
        phase += f / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
        const float atk = juce::jmin (1.0f, ts / 0.0015f);
        float boom = std::sin (2.0f * pi * phase) * std::exp (-ts / 0.42f) * atk;
        boom = std::tanh (1.6f * boom) / std::tanh (1.6f);
        // crack: high-passed noise, very short
        const float nl = n.next(), nr = n.next();
        hpL = 0.82f * (hpL + nl - prevL); prevL = nl;
        hpR = 0.82f * (hpR + nr - prevR); prevR = nr;
        const float crackEnv = std::exp (-ts / 0.035f) * atk * 0.45f;
        // fade the very end so the tail never cuts
        const float endFade = juce::jlimit (0.0f, 1.0f, (float) (len - t) / (float) (0.2 * sr));
        const float g = level * endFade;
        l = (boom + hpL * crackEnv) * g;
        r = (boom + hpR * crackEnv) * g;
        sendL = (0.5f * boom + hpL * crackEnv * 1.5f) * g;
        sendR = (0.5f * boom + hpR * crackEnv * 1.5f) * g;
        if (++t >= len) active = false;
    }
};

// After-drop downlifter: band-passed noise falling from 10 kHz to 150 Hz over `len`.
struct Downlifter
{
    bool active = false; double sr = 48000; int t = 0, len = 1;
    float level = 0, pw = 1.0e-4f, pwCoef = 0.001f;
    Svf f[2]; Noise n[2];
    void prepare (double rate) { sr = rate; active = false; pwCoef = onePoleCoef (rate, 0.03); }
    void trigger (float rms, double seconds)
    {
        active = true; t = 0; len = juce::jmax (1, (int) (seconds * sr)); level = rms; pw = 0.0f;
        n[0].seed (0xabcdef1u); n[1].seed (0x7654321u);
        for (auto& x : f) { x.reset(); x.set (sr, 10000.0f, 1.1f); }
        // settle the filters + loudness normaliser on noise first, so the start has no overshoot
        const int pre = (int) (0.05 * sr); float b0, b1, lo;
        for (int i = 0; i < pre; ++i)
        {
            f[0].process (n[0].next(), b0, lo); f[1].process (n[1].next(), b1, lo);
            pw += 0.01f * (0.5f * (b0 * b0 + b1 * b1) - pw);
        }
    }
    inline void process (float& l, float& r)
    {
        l = r = 0.0f;
        if (! active) return;
        const float u = (float) t / (float) len;
        if ((t & 7) == 0)
        {
            const float hz = 10000.0f * std::pow (150.0f / 10000.0f, u);
            for (auto& x : f) x.set (sr, hz, 1.1f);
        }
        float b0, b1, lo;
        f[0].process (n[0].next(), b0, lo);
        f[1].process (n[1].next(), b1, lo);
        pw += pwCoef * (0.5f * (b0 * b0 + b1 * b1) - pw);
        const float norm = juce::jmin (30.0f, 1.0f / std::sqrt (pw + 1.0e-6f));
        const float env = juce::jmin (1.0f, (float) t / (float) (0.006 * sr)) * std::pow (1.0f - u, 1.6f);
        const float g = level * norm * env;
        l = b0 * g; r = b1 * g;
        if (++t >= len) active = false;
    }
};
} // namespace ab
