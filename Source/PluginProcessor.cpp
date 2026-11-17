#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum { kAttack, kSustain, kSpeed, kLength, kFocus, kXover, kLook, kClip, kMix, kOut };
const char* ids[] = { "attack", "sustain", "speed", "length", "focus", "xover", "lookahead", "clip", "mix", "output" };
constexpr int numP = 10;
inline float coef (double sr, float ms) { return 1.0f - std::exp (-1.0f / (float) (sr * ms * 0.001)); }
}

juce::AudioProcessorValueTreeState::ParameterLayout VegaTransientProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto pct = [] (float x, int) { return (x > 0 ? "+" : "") + String (roundToInt (x)) + "%"; };
    auto ms = [] (float x, int) { return String (roundToInt (x)) + " ms"; };
    auto F = [&] (int id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[id], 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (f))); };
    F (kAttack, "Attack", NormalisableRange<float> (-100, 100), 0, pct);
    F (kSustain, "Sustain", NormalisableRange<float> (-100, 100), 0, pct);
    F (kSpeed, "Attack Window", NormalisableRange<float> (2, 60, 0, 0.5f), 15, ms);
    F (kLength, "Sustain Length", NormalisableRange<float> (40, 800, 0, 0.5f), 200, ms);
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kFocus], 1 }, "Focus", StringArray { "Full Band", "Lows Only", "Highs Only" }, 0));
    F (kXover, "Focus Split", NormalisableRange<float> (60, 2000, 0, 0.4f), 200, [] (float x, int) { return x >= 1000 ? String (x / 1000, 1) + " kHz" : String (roundToInt (x)) + " Hz"; });
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kLook], 1 }, "Lookahead", true));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kClip], 1 }, "Soft Clip", false));
    F (kMix, "Mix", NormalisableRange<float> (0, 100), 100, [] (float x, int) { return String (roundToInt (x)) + "%"; });
    F (kOut, "Output", NormalisableRange<float> (-24, 12), 0, [] (float x, int) { return String (x, 1) + " dB"; });
    return { v.begin(), v.end() };
}

VegaTransientProcessor::VegaTransientProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    for (int i = 0; i < numP; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
}

bool VegaTransientProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return l.getMainInputChannelSet() == o && (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono());
}

void VegaTransientProcessor::prepareToPlay (double s, int block)
{
    sr = s;
    maxLook = (int) (s * 0.003) + 1;           // 3 ms lookahead
    for (auto& l : look) l.assign ((size_t) maxLook + 1, 0.0f);
    lpos = 0;
    lookN = p[kLook]->load() > 0.5f ? maxLook : 0;
    setLatencySamples (lookN);
    juce::dsp::ProcessSpec spec { s, (juce::uint32) block, 2 };
    lp.prepare (spec); hp.prepare (spec);
    lp.setType (juce::dsp::LinkwitzRileyFilterType::lowpass); hp.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    eFastA = eSlowA = eFastR = eSlowR = msDet = 0; gainS = 1;
    mixS.reset (s, 0.03); mixS.setCurrentAndTargetValue (p[kMix]->load() * 0.01f);
    scopeEvery = std::max (1, (int) (s * 2.0 / scopeLen));     // 2 s across the scope
}

void VegaTransientProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), nch = std::min (2, buffer.getNumChannels());
    if (n == 0 || nch == 0) return;
    const int wantLook = p[kLook]->load() > 0.5f ? maxLook : 0;
    if (wantLook != lookN) { lookN = wantLook; setLatencySamples (lookN); }

    const float atk = p[kAttack]->load() * 0.01f, sus = p[kSustain]->load() * 0.01f;
    const float win = p[kSpeed]->load(), len = p[kLength]->load();
    const int focus = (int) p[kFocus]->load();
    const bool clip = p[kClip]->load() > 0.5f;
    const float outG = juce::Decibels::decibelsToGain (p[kOut]->load());
    mixS.setTargetValue (p[kMix]->load() * 0.01f);
    if (focus > 0) { lp.setCutoffFrequency (p[kXover]->load()); hp.setCutoffFrequency (p[kXover]->load()); }

    // detector coefficients: attack pair (fast vs. slow rise), sustain pair (fast vs. slow fall)
    const float cFastA = coef (sr, 0.3f), cSlowA = coef (sr, win), cRelA = coef (sr, win * 4.0f);
    const float cAtkR = coef (sr, 1.0f), cFastR = coef (sr, 20.0f), cSlowR = coef (sr, len);
    const float smooth = coef (sr, 0.4f), cRms = coef (sr, 2.5f);
    constexpr float range = 18.0f;             // dB at 100%

    float* ch[2] { buffer.getWritePointer (0), buffer.getWritePointer (nch > 1 ? 1 : 0) };
    float pk[2] {}, aMax = 0, sMax = 0;
    for (int i = 0; i < n; ++i)
    {
        const float xl = ch[0][i], xr = ch[1][i];
        const float sq = std::max (xl * xl, xr * xr);                       // stereo-linked, RMS-smoothed detector
        msDet += cRms * (sq - msDet);
        const float det = std::sqrt (msDet);

        // attack: fast follower vs slow follower (both release slowly) -> ratio > 1 only at onsets
        eFastA += (det > eFastA ? cFastA : cRelA) * (det - eFastA);
        eSlowA += (det > eSlowA ? cSlowA : cRelA) * (det - eSlowA);
        // sustain: both attack fast; slow-release vs fast-release -> ratio > 1 in the tails
        eFastR += (det > eFastR ? cAtkR : cFastR) * (det - eFastR);
        eSlowR += (det > eSlowR ? cAtkR : cSlowR) * (det - eSlowR);
        const float floorLvl = 1.0e-4f;
        const float aDb = juce::jlimit (0.0f, 12.0f, 20.0f * std::log10 ((eFastA + floorLvl) / (eSlowA + floorLvl)) - 0.5f);
        const float sDb = juce::jlimit (0.0f, 12.0f, 20.0f * std::log10 ((eSlowR + floorLvl) / (eFastR + floorLvl)) - 0.5f);
        aMax = std::max (aMax, aDb); sMax = std::max (sMax, sDb);
        const float gDb = atk * range * (aDb / 12.0f) + sus * range * (sDb / 12.0f);
        const float gTarget = juce::Decibels::decibelsToGain (juce::jlimit (-30.0f, 18.0f, gDb));
        gainS += smooth * (gTarget - gainS);

        // delayed audio (lookahead lets the gain change land on the very first sample of the hit)
        float d[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = c == 0 ? xl : xr;
            if (lookN > 0) { look[c][(size_t) lpos] = x; int rp = lpos - lookN; if (rp < 0) rp += maxLook + 1; d[c] = look[c][(size_t) rp]; }
            else d[c] = x;
        }
        if (lookN > 0 && ++lpos > maxLook) lpos = 0;

        const float m = mixS.getNextValue();
        for (int c = 0; c < 2; ++c)
        {
            float y;
            if (focus == 0) y = d[c] * gainS;
            else
            {
                const float lo = lp.processSample (c, d[c]), hi = hp.processSample (c, d[c]);
                y = focus == 1 ? lo * gainS + hi : lo + hi * gainS;
                d[c] = lo + hi;   // keep dry in the same (allpass) phase
            }
            y = d[c] + (y - d[c]) * m;
            if (clip) y = std::tanh (y * 0.9f) / 0.9f;
            y *= outG;
            if (c < nch) { ch[c][i] = y; pk[c] = std::max (pk[c], std::abs (y)); }
        }

        scopePk = std::max (scopePk, std::abs (d[0]));
        scopeG = std::abs (gDb) > std::abs (scopeG) ? juce::Decibels::gainToDecibels (gainS) : scopeG;
        if (++scopeAcc >= scopeEvery)
        {
            const int sp = scopePos.load();
            scopeIn[(size_t) sp].store (scopePk); scopeGain[(size_t) sp].store (juce::Decibels::gainToDecibels (gainS));
            scopePos.store ((sp + 1) % scopeLen);
            scopeAcc = 0; scopePk = 0; scopeG = 0;
        }
    }
    outL.store (std::max (outL.load(), pk[0])); outR.store (std::max (outR.load(), nch > 1 ? pk[1] : pk[0]));
    uiAtk.store (aMax); uiSus.store (sMax);
}

void VegaTransientProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto s = apvts.copyState(); s.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto x = s.createXml()) copyXmlToBinary (*x, d);
}
void VegaTransientProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        auto s = juce::ValueTree::fromXml (*x);
        if (! s.hasType (apvts.state.getType())) return;
        uiScale.store ((float) s.getProperty ("uiScale", 1.0f));
        apvts.replaceState (s);
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaTransientProcessor(); }
