#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaTransientProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
const double fs = 48000; const int blk = 480;

// a drum hit every 0.5 s: 2 ms click + decaying 60 Hz body (300 ms)
static float drum (int i)
{
    const int t = i % 24000;
    const float body = 0.5f * std::exp (-t / (0.12f * 48000.0f)) * (float) std::sin (2 * juce::MathConstants<double>::pi * 60 * t / fs);
    const float click = t < 96 ? 0.4f * (1.0f - t / 96.0f) * ((t * 7919) % 13 / 6.5f - 1.0f) : 0.0f;
    return body + click;
}
static std::vector<float> run (VegaTransientProcessor& p, int n, float (*gen) (int))
{
    std::vector<float> out ((size_t) n); juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m;
    for (int pos = 0; pos < n; pos += blk)
    {
        for (int i = 0; i < blk; ++i) { const float x = gen (pos + i); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
        for (int i = 0; i < blk; ++i) out[(size_t) (pos + i)] = b.getSample (0, i);
    }
    return out;
}
// energy of the first 10 ms vs. 100..300 ms after each hit (from the 3rd hit on), given latency
static void measure (const std::vector<float>& o, int lat, double& early, double& late)
{
    early = late = 0;
    for (int h = 2; h < 6; ++h)
    {
        const int s = h * 24000 + lat;
        for (int i = 0; i < 480; ++i) early += o[(size_t) (s + i)] * o[(size_t) (s + i)];
        for (int i = 4800; i < 14400; ++i) late += o[(size_t) (s + i)] * o[(size_t) (s + i)];
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto make = [&] { auto* p = new VegaTransientProcessor(); p->setPlayConfigDetails (2, 2, fs, blk); p->prepareToPlay (fs, blk); return std::unique_ptr<VegaTransientProcessor> (p); };
    const int N = 24000 * 6;
    auto ref = make(); auto r = run (*ref, N, drum);
    const int lat = ref->getLatencySamples();
    {
        double err = 0; for (int i = lat; i < N; ++i) err = std::max (err, (double) std::abs (r[(size_t) i] - drum (i - lat)));
        check (err < 1e-6, "neutral settings are transparent (latency " + juce::String (lat) + " samples, error " + juce::String (err, 8) + ")");
    }
    double e0, l0; measure (r, lat, e0, l0);
    {
        auto p = make(); set (*p, "attack", 100); auto o = run (*p, N, drum); double e, l; measure (o, lat, e, l);
        const double de = 10 * std::log10 (e / e0), dl = 10 * std::log10 (l / l0);
        check (de > 4 && std::abs (dl) < 1.5, "attack +100: hit louder (" + juce::String (de, 1) + " dB), tail ~unchanged (" + juce::String (dl, 1) + " dB)");
    }
    {
        auto p = make(); set (*p, "attack", -100); auto o = run (*p, N, drum); double e, l; measure (o, lat, e, l);
        check (10 * std::log10 (e / e0) < -3, "attack -100 softens the hit (" + juce::String (10 * std::log10 (e / e0), 1) + " dB)");
    }
    {
        auto p = make(); set (*p, "sustain", -100); auto o = run (*p, N, drum); double e, l; measure (o, lat, e, l);
        const double de = 10 * std::log10 (e / e0), dl = 10 * std::log10 (l / l0);
        check (dl < -4 && std::abs (de) < 1.5, "sustain -100: tail shorter (" + juce::String (dl, 1) + " dB), hit kept (" + juce::String (de, 1) + " dB)");
    }
    {
        auto p = make(); set (*p, "sustain", 100); auto o = run (*p, N, drum); double e, l; measure (o, lat, e, l);
        check (10 * std::log10 (l / l0) > 3, "sustain +100 lengthens the tail (" + juce::String (10 * std::log10 (l / l0), 1) + " dB)");
    }
    {   // a steady tone is left alone even with extreme settings
        auto p = make(); set (*p, "attack", 100); set (*p, "sustain", 100);
        auto o = run (*p, 48000, [] (int i) { return 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440 * i / fs); });
        float mn = 1, mx = 0; for (int i = 24000; i < 48000; ++i) { mx = std::max (mx, std::abs (o[(size_t) i])); }
        mn = mx; check (std::abs (juce::Decibels::gainToDecibels (mx / 0.3f)) < 0.5f, "steady sine unaffected (" + juce::String (juce::Decibels::gainToDecibels (mx / 0.3f), 2) + " dB)");
    }
    {   // lows-only focus leaves highs untouched
        auto p = make(); set (*p, "focus", 1); set (*p, "attack", -100);
        auto o = run (*p, N, [] (int i) { const int t = i % 24000; return t < 2400 ? 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * 5000 * t / fs) * (1.0f - t / 2400.0f) : 0.0f; });
        double e = 0, e2 = 0; for (int h = 2; h < 6; ++h) for (int i = 0; i < 480; ++i) { const int s = h * 24000 + lat + i; e += o[(size_t) s] * o[(size_t) s]; const float x = 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * 5000 * i / fs) * (1.0f - i / 2400.0f); e2 += x * x; }
        check (std::abs (10 * std::log10 (e / e2)) < 1.0, "lows-only focus leaves a 5 kHz hit alone (" + juce::String (10 * std::log10 (e / e2), 2) + " dB)");
    }
    {
        auto p = make(); set (*p, "attack", 100); set (*p, "sustain", 100); set (*p, "clip", 1);
        auto o = run (*p, N, drum); bool fin = true; float pk = 0; for (float v : o) { fin &= std::isfinite (v); pk = std::max (pk, std::abs (v)); }
        check (fin && pk < 1.2f, "soft clip keeps boosted peaks in check (" + juce::String (pk, 2) + ")");
    }
    {
        auto p = make(); set (*p, "attack", 33); juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q->apvts.getRawParameterValue ("attack")->load() - 33) < 0.5f, "state round trip");
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
