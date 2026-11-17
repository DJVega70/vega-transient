#pragma once
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaTransientEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaTransientEditor (VegaTransientProcessor&);
    ~VegaTransientEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 900, H = 540;
    class Main;
private:
    void timerCallback() override;
    VegaTransientProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
