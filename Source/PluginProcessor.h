#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class VegaTransientProcessor : public juce::AudioProcessor
{
public:
    VegaTransientProcessor();
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions Transient Shaper"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    juce::AudioProcessorValueTreeState apvts;

    // scope: per-column peak of input and the applied gain (dB), written by audio, read by UI
    static constexpr int scopeLen = 400;
    std::array<std::atomic<float>, scopeLen> scopeIn {}, scopeGain {};
    std::atomic<int> scopePos { 0 };
    std::atomic<float> outL { 0 }, outR { 0 }, uiAtk { 0 }, uiSus { 0 };
    std::atomic<float> uiScale { 1.0f };

private:
    double sr = 48000; int lookN = 0, maxLook = 1;
    float msDet = 0, eFastA = 0, eSlowA = 0, eFastR = 0, eSlowR = 0, gainS = 1;
    std::vector<float> look[2]; int lpos = 0;
    juce::dsp::LinkwitzRileyFilter<float> lp, hp;
    int scopeAcc = 0, scopeEvery = 1; float scopePk = 0, scopeG = 0;
    juce::SmoothedValue<float> mixS;
    std::atomic<float>* p[12] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaTransientProcessor)
};
