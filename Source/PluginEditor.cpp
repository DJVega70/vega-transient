#include "PluginEditor.h"

namespace
{
class Scope : public juce::Component
{
public:
    explicit Scope (VegaTransientProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        const int n = VegaTransientProcessor::scopeLen, start = proc.scopePos.load();
        const float mid = r.getCentreY(), half = r.getHeight() * 0.5f - 4, w = r.getWidth() / n;
        g.setColour (juce::Colour (0xff262626)); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
        juce::Path gainPath;
        for (int k = 0; k < n; ++k)
        {
            const int i = (start + k) % n;
            const float in = proc.scopeIn[(size_t) i].load(), gDb = proc.scopeGain[(size_t) i].load();
            const float lv = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (in, -48.0f) + 48.0f) / 48.0f);
            const float ov = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (in * juce::Decibels::decibelsToGain (gDb), -48.0f) + 48.0f) / 48.0f);
            const float x = r.getX() + k * w;
            g.setColour (juce::Colour (0xff3a3a3a)); g.fillRect (x, mid - lv * half, std::max (1.0f, w - 0.5f), 2 * lv * half);
            g.setColour (gDb >= 0 ? VegaColours::accent.withAlpha (0.65f) : VegaColours::gold.withAlpha (0.55f));
            g.fillRect (x, mid - ov * half, std::max (1.0f, w - 0.5f), 2 * ov * half);
            const float gy = mid - juce::jlimit (-18.0f, 18.0f, gDb) / 18.0f * half;
            if (k == 0) gainPath.startNewSubPath (x, gy); else gainPath.lineTo (x, gy);
        }
        g.setColour (juce::Colours::white.withAlpha (0.85f)); g.strokePath (gainPath, juce::PathStrokeType (1.4f));
        g.setFont (VegaFonts::ui (9.5f)); g.setColour (VegaColours::textDim);
        g.drawText ("grey = in   cyan = boosted   gold = cut   line = gain", r.toNearestInt().removeFromTop (12), juce::Justification::left);
        g.drawText ("+18 dB", r.toNearestInt().removeFromTop (12), juce::Justification::right);
        g.drawText ("-18 dB", r.toNearestInt().removeFromBottom (12), juce::Justification::right);
    }
private:
    VegaTransientProcessor& proc;
};
}

class VegaTransientEditor::Main : public juce::Component
{
public:
    explicit Main (VegaTransientProcessor& p) : proc (p), a (p.apvts), scope (p),
        attack (a, "attack", "ATTACK", true), sustain (a, "sustain", "SUSTAIN", true), speed (a, "speed", "Attack Window"), length (a, "length", "Sustain Length"),
        focus (a, "focus", "Focus"), xover (a, "xover", "Focus Split"), look (a, "lookahead", "Lookahead"), clip (a, "clip", "Soft Clip"), mix (a, "mix", "Mix"), output (a, "output", "Output")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &scope, &attack, &sustain, &speed, &length, &focus, &xover, &look, &clip, &mix, &output, &meter, &aLed, &sLed })
            addAndMakeVisible (c);
        aLed.title = "ATK"; sLed.title = "SUS"; aLed.maxDb = 12; sLed.maxDb = 12;
    }
    void refresh()
    {
        scope.repaint();
        meter.setLevels (proc.outL.exchange (0.0f), proc.outR.exchange (0.0f));
        aLed.setGr (proc.uiAtk.load()); sLed.setGr (proc.uiSus.load());
        xover.setEnabled (a.getRawParameterValue ("focus")->load() > 0.5f);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "TRANSIENT SHAPER  VP-14");
        vegaui::drawBody (g, { 0, 35, W, H - 65 });
        g.setColour (VegaColours::line); g.drawHorizontalLine (232, 0, (float) W);
        vegaui::drawSection (g, { 0, 236, 420, 274 }, "SHAPE", false);
        vegaui::drawSection (g, { 420, 236, 240, 274 }, "DETECTOR");
        vegaui::drawSection (g, { 660, 236, W - 660, 274 }, "OUTPUT");
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "ATTACK + MAKES KICKS AND CLAPS SNAP   -   SUSTAIN - TIGHTENS ROOMS AND BOOMY TAILS");
    }
    void resized() override
    {
        scope.setBounds (12, 44, 800, 182); meter.setBounds (824, 40, 64, 190);
        attack.setBounds (20, 254, 150, 180); sustain.setBounds (190, 254, 150, 180);
        aLed.setBounds (350, 262, 30, 150); sLed.setBounds (384, 262, 30, 150);
        speed.setBounds (432, 254, 100, 112); length.setBounds (540, 254, 110, 112);
        focus.setBounds (436, 384, 110, 46); xover.setBounds (550, 378, 100, 112);
        look.setBounds (668, 254, 70, 80); clip.setBounds (744, 254, 70, 80);
        mix.setBounds (668, 378, 100, 112); output.setBounds (776, 378, 100, 112);
    }
private:
    VegaTransientProcessor& proc; juce::AudioProcessorValueTreeState& a;
    Scope scope;
    vegaui::Knob attack, sustain, speed, length; vegaui::Choice focus; vegaui::Knob xover; vegaui::Toggle look, clip; vegaui::Knob mix, output;
    vegaui::StereoMeter meter; vegaui::GrColumn aLed, sLed;
};

VegaTransientEditor::VegaTransientEditor (VegaTransientProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaTransientEditor::~VegaTransientEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaTransientEditor::timerCallback() { main->refresh(); }
void VegaTransientEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
juce::AudioProcessorEditor* VegaTransientProcessor::createEditor() { return new VegaTransientEditor (*this); }
