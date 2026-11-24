#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class HardwareLookAndFeel;

// Dial that can be padlocked: a click on a locked dial opens the Pro panel instead of turning it.
class Dial : public juce::Slider
{
public:
    bool locked = false;
    std::function<void()> onLockedClick;
    void mouseDown (const juce::MouseEvent& e) override        { if (locked) { if (onLockedClick) onLockedClick(); return; } Slider::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override        { if (! locked) Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override          { if (! locked) Slider::mouseUp (e); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! locked) Slider::mouseDoubleClick (e); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (! locked) Slider::mouseWheelMove (e, w); }
};

// Upgrade panel: what Pro adds, start the 7-day trial, paste a key, or buy.
class ProPanel : public juce::Component
{
public:
    explicit ProPanel (AutoBuildProAudioProcessor& p);
    void show (const juce::String& reason);
    void licenceChanged();   // the processor's licence changed (another instance, trial expiry): redraw if open
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // click outside the card closes it
    std::function<void()> onLicenceChanged;
private:
    void refresh();
    juce::Rectangle<int> card() const;
    AutoBuildProAudioProcessor& proc;
    juce::String reason, message;
    bool messageIsError = false, busy = false;
    juce::TextEditor emailBox, keyBox;
    juce::ToggleButton subscribe { "Send me tips and offers" };
    juce::TextButton trialButton { "START 7-DAY FREE TRIAL" }, unlockButton { "UNLOCK" },
                     buyButton { "UPGRADE" }, closeButton { "CLOSE" };
};

// The hero display: the build curve over the bars, the playhead, the drop and what happens after it.
class BuildDisplay : public juce::Component
{
public:
    explicit BuildDisplay (AutoBuildProAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    AutoBuildProAudioProcessor& proc;
};

class AutoBuildProAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AutoBuildProAudioProcessorEditor (AutoBuildProAudioProcessor&);
    ~AutoBuildProAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void openPro (const juce::String& reason) { proPanel.show (reason); }

private:
    struct Knob
    {
        Dial slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::String label, paramId;
        juce::StringArray stepLabels;   // empty = continuous dial
        juce::String value;             // text under the dial
        bool big = false;
    };
    Knob& addKnob (const juce::String& id, const juce::String& label, juce::StringArray steps = {});
    Knob* knob (const juce::String& id);
    void renderBackground();
    void updateLocks();
    void applyStyle (int s);

    AutoBuildProAudioProcessor& proc;
    std::unique_ptr<HardwareLookAndFeel> laf;

    juce::OwnedArray<juce::TextButton> styleKeys;
    juce::TextButton manualButton { "MANUAL" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> manualAtt;
    BuildDisplay display;
    juce::TextButton licenceButton;
    ProPanel proPanel;
    juce::OwnedArray<Knob> knobs;

    juce::Image background;
    juce::Rectangle<int> ledArea, styleArea, secBuild, secLayers, secDrop, secOut;
    juce::String ledLabel, ledText, ledRightLabel, ledRightText;
    bool upgradeNudge = false;
    int seenLicenceVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoBuildProAudioProcessorEditor)
};
