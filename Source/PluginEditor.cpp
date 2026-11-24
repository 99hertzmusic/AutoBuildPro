// AutoBuild Pro – UI (99Hertz hardware look: brushed panel, knurled dials, lit keys, LED screens)
#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using APP = AutoBuildProAudioProcessor;

namespace Pal
{
    static const juce::Colour panelTop  { 0xff202124 };
    static const juce::Colour panelBot  { 0xff141517 };
    static const juce::Colour screen    { 0xff060707 };
    static const juce::Colour silk      { 0xffe4e5e7 };
    static const juce::Colour silkMid   { 0xffb4b7bc };
    static const juce::Colour silkLo    { 0xff7d8086 };
    static const juce::Colour lampOff   { 0xff3a3b3f };
    static const juce::Colour accent    { 0xfff4f1e6 };   // AutoBuild warm white LED
    static const juce::Colour warn      { 0xffff9a2e };
    static const juce::Colour dim       { 0xff8a8778 };   // unlit LED segments / secondary curves
}

// ── Fonts (embedded, OFL: Barlow Condensed + Share Tech Mono) ────────────────
namespace Fonts
{
    static juce::Typeface::Ptr load (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); }
    static juce::Typeface::Ptr bold()  { static auto t = load (BinaryData::BarlowCondensedBold_ttf,     BinaryData::BarlowCondensedBold_ttfSize);     return t; }
    static juce::Typeface::Ptr semi()  { static auto t = load (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize); return t; }
    static juce::Typeface::Ptr med()   { static auto t = load (BinaryData::BarlowCondensedMedium_ttf,   BinaryData::BarlowCondensedMedium_ttfSize);   return t; }
    static juce::Typeface::Ptr mono()  { static auto t = load (BinaryData::ShareTechMonoRegular_ttf,    BinaryData::ShareTechMonoRegular_ttfSize);    return t; }
    static juce::Font make (juce::Typeface::Ptr t, float size, float spacing = 0.0f)
    {
        return juce::Font (juce::FontOptions (t).withPointHeight (size).withKerningFactor (spacing));
    }
    static juce::Font silkBold (float s, float sp = 0.2f)  { return make (bold(), s, sp); }
    static juce::Font silkSemi (float s, float sp = 0.16f) { return make (semi(), s, sp); }
    static juce::Font silkMed  (float s, float sp = 0.14f) { return make (med(), s, sp); }
    static juce::Font led      (float s, float sp = 0.06f) { return make (mono(), s, sp); }
}

static float textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga; ga.addLineOfText (f, t, 0, 0);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

static void glowText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setColour (c.withAlpha (0.18f));
    for (auto d : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
        g.drawText (t, r.translated (d.x, d.y), j, false);
    g.setColour (c);
    g.drawText (t, r, j, false);
}

static void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w)
{
    g.setColour (c.withAlpha (0.12f)); g.strokePath (p, juce::PathStrokeType (w * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (w * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);                   g.strokePath (p, juce::PathStrokeType (w,        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

static void drawScreen (juce::Graphics& g, juce::Rectangle<float> r, float rad = 5.0f)
{
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.fillRoundedRectangle (r.translated (0, 1.0f), rad);
    g.setColour (Pal::screen);
    g.fillRoundedRectangle (r, rad);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 10.0f, false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, rad, 1.0f);
}

static void drawScrew (juce::Graphics& g, juce::Point<float> c, float angle)
{
    const float R = 7.0f;
    juce::ColourGradient gr (juce::Colour (0xff7a7c81), c.x - 1.5f, c.y - 2.5f, juce::Colour (0xff1c1d1f), c.x + R, c.y + R, true);
    gr.addColour (0.6, juce::Colour (0xff3b3c40));
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (c.x - R, c.y - R + 1.0f, 2 * R, 2 * R);
    g.setGradientFill (gr);
    g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    juce::Path slot;
    slot.addRoundedRectangle (-5.0f, -1.0f, 10.0f, 2.0f, 1.0f);
    g.setColour (juce::Colour (0xff101012));
    g.fillPath (slot, juce::AffineTransform::rotation (angle).translated (c));
}

static void padlock (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    auto body = r.removeFromBottom (r.getHeight() * 0.58f);
    juce::Path shackle;
    shackle.addCentredArc (body.getCentreX(), body.getY(), body.getWidth() * 0.3f, r.getHeight() * 0.85f, 0,
                           -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.setColour (c);
    g.strokePath (shackle, juce::PathStrokeType (1.4f));
    g.fillRoundedRectangle (body, 1.5f);
}

static constexpr float dialStart = juce::MathConstants<float>::pi * 1.25f, dialEnd = juce::MathConstants<float>::pi * 2.75f;

// ── LookAndFeel: knurled dials + hardware keys ───────────────────────────────
class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HardwareLookAndFeel() { setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme()); }

    const juce::Image& knobBody (int D)
    {
        auto& img = bodies[D];
        if (img.isValid()) return img;
        const float sc = 2.0f;
        img = juce::Image (juce::Image::ARGB, (int) (D * sc), (int) (D * sc) + (int) (D * 0.2f * sc), true);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (sc));
        const float R = D * 0.5f;
        const juce::Point<float> c (R, R);
        const float rs = R * 0.705f, rc = R * 0.564f;
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            const float e = rs + i * 1.2f;
            g.fillEllipse (c.x - e, c.y - e + R * 0.14f, 2 * e, 2 * e);
        }
        juce::Path skirt; skirt.addEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs);
        g.setColour (juce::Colour (0xff0d0d0f)); g.fillPath (skirt);
        g.setColour (juce::Colour (0xff303135));
        for (int k = 0; k < 60; ++k)
        {
            juce::Path w;
            const float a0 = k * juce::MathConstants<float>::twoPi / 60.0f, a1 = a0 + juce::MathConstants<float>::twoPi / 120.0f;
            w.addPieSegment (c.x - rs, c.y - rs, 2 * rs, 2 * rs, a0, a1, 0.0f);
            g.fillPath (w);
        }
        g.setColour (juce::Colour (0xff050506)); g.drawEllipse (c.x - rs, c.y - rs, 2 * rs, 2 * rs, 1.0f);
        juce::ColourGradient sh (juce::Colours::white.withAlpha (0.16f), 0, c.y - rs, juce::Colours::black.withAlpha (0.42f), 0, c.y + rs, false);
        sh.addColour (0.38, juce::Colours::transparentWhite);
        g.setGradientFill (sh); g.fillPath (skirt);
        juce::ColourGradient cg (juce::Colour (0xff404145), c.x, c.y - rc * 0.48f, juce::Colour (0xff17181a), c.x, c.y + rc * 1.1f, true);
        cg.addColour (0.52, juce::Colour (0xff26272a));
        g.setGradientFill (cg);
        g.fillEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawEllipse (c.x - rc + 0.5f, c.y - rc + 0.5f, 2 * rc - 1.0f, 2 * rc - 1.0f, 0.6f);
        g.setColour (juce::Colour (0xff08080a));
        g.drawEllipse (c.x - rc, c.y - rc, 2 * rc, 2 * rc, 1.0f);
        return img;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const int D = juce::jmin (w, h);
        const float R = D * 0.5f;
        const juce::Point<float> c ((float) x + w * 0.5f, (float) y + R);
        const auto& props = s.getProperties();
        if (props.contains ("displayPos")) pos = (float) props["displayPos"];
        const int steps = (int) props.getWithDefault ("steps", 0);
        const bool locked = (bool) props.getWithDefault ("locked", false);
        const float angle = startAngle + juce::jlimit (0.0f, 1.0f, pos) * (endAngle - startAngle);
        if (locked) g.beginTransparencyLayer (0.55f);

        const int ticks = steps > 1 ? steps - 1 : 10;
        for (int k = 0; k <= ticks; ++k)
        {
            const float a = startAngle + k * (endAngle - startAngle) / ticks;
            const float sx = std::sin (a), sy = -std::cos (a);
            const bool strong = steps > 1 || k == 0 || k == ticks;
            g.setColour (juce::Colour (0xffd4d6da).withAlpha (strong ? 0.9f : 0.55f));
            g.drawLine (c.x + sx * R * 0.86f, c.y + sy * R * 0.86f, c.x + sx * R * 0.99f, c.y + sy * R * 0.99f, steps > 1 ? 1.6f : 1.3f);
        }
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, R * 0.79f, R * 0.79f, 0, startAngle, angle, true);
            g.setColour (Pal::accent.withAlpha (0.25f));
            g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Pal::accent);
            g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto& body = knobBody (D);
        g.drawImage (body, juce::Rectangle<float> (c.x - R, c.y - R, (float) D, (float) body.getHeight() / 2.0f));

        const float rc = R * 0.564f;
        const float sx = std::sin (angle), sy = -std::cos (angle);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f + 1.0f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f + 1.0f, D * 0.045f);
        g.setColour (juce::Colour (0xfff4f4f1));
        g.drawLine (c.x + sx * rc * 0.2f, c.y + sy * rc * 0.2f, c.x + sx * rc * 0.88f, c.y + sy * rc * 0.88f, D * 0.045f);
        if (locked)
        {
            g.endTransparencyLayer();
            // padlock badge on the cap
            auto badge = juce::Rectangle<float> (c.x - 9.0f, c.y - 10.0f, 18.0f, 20.0f);
            g.setColour (juce::Colour (0xff17181a).withAlpha (0.85f));
            g.fillEllipse (c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f);
            padlock (g, badge.reduced (3.5f, 3.0f), Pal::silk);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            const bool warnCol = (bool) b.getProperties()["warn"];
            const auto c = warnCol ? Pal::warn : Pal::accent;
            g.setColour (c.withAlpha (over ? 0.22f : 0.12f));
            g.fillRoundedRectangle (r, r.getHeight() / 2);
            g.setColour (c.withAlpha (0.7f));
            g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
            return;
        }
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
        const bool on = b.getToggleState() || down;
        if (on) r.translate (0.0f, 2.0f);
        const float rad = 6.0f;
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (r.translated (0.0f, on ? 1.0f : 3.0f), rad);
        g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff34353a : 0xff2e2f33), 0, r.getY(),
                                                 juce::Colour (0xff1c1d20), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colour (0xff050506));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        if (b.getProperties().contains ("pill"))
        {
            const bool warnCol = (bool) b.getProperties()["warn"];
            g.setFont (Fonts::led (11.5f, 0.1f));
            g.setColour (warnCol ? Pal::warn : Pal::accent);
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
            return;
        }
        const bool locked = (bool) b.getProperties().getWithDefault ("locked", false);
        const bool on  = b.getToggleState() && ! locked;
        const bool lit = on || ((bool) b.getProperties().getWithDefault ("lit", false) && ! locked);
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
        if (on || down) r.translate (0.0f, 2.0f);
        const bool small = r.getHeight() < 34.0f;

        const float lw = small ? 20.0f : 30.0f, lh = small ? 3.0f : 4.0f;
        auto lamp = juce::Rectangle<float> (r.getCentreX() - lw / 2, r.getY() + (small ? 6.0f : 9.0f), lw, lh);
        if (lit)
        {
            g.setColour (Pal::accent.withAlpha (0.25f));
            g.fillRoundedRectangle (lamp.expanded (3.0f, 2.5f), 3.0f);
            g.setColour (Pal::accent);
        }
        else g.setColour (Pal::lampOff);
        g.fillRoundedRectangle (lamp, lh / 2);

        if (locked)
            padlock (g, { r.getRight() - 17.0f, r.getY() + 6.0f, 9.0f, 11.0f }, Pal::silkLo);
        g.setFont (Fonts::silkSemi (small ? 12.0f : 14.5f, 0.14f));
        g.setColour (on ? juce::Colour (0xfff2f2ef) : Pal::silkMid.withAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawText (b.getButtonText(), r.withTrimmedTop (lamp.getBottom() - r.getY()), juce::Justification::centred);
    }

private:
    std::map<int, juce::Image> bodies;
};

// ── ProPanel ──────────────────────────────────────────────────────────────────
static void styleBox (juce::TextEditor& t, const juce::String& placeholder)
{
    t.setColour (juce::TextEditor::backgroundColourId, Pal::screen);
    t.setColour (juce::TextEditor::textColourId, Pal::accent);
    t.setColour (juce::TextEditor::outlineColourId, Pal::lampOff);
    t.setColour (juce::TextEditor::focusedOutlineColourId, Pal::accent);
    t.setColour (juce::CaretComponent::caretColourId, Pal::accent);
    t.setFont (Fonts::led (15.0f, 0.02f));
    t.setTextToShowWhenEmpty (placeholder, Pal::silkLo);
    t.setIndents (10, 10);
    t.setSelectAllWhenFocused (true);
}

ProPanel::ProPanel (AutoBuildProAudioProcessor& p) : proc (p)
{
    styleBox (emailBox, "you@email.com");
    styleBox (keyBox, "99HZ-...");
    subscribe.setColour (juce::ToggleButton::textColourId, Pal::silkMid);
    subscribe.setColour (juce::ToggleButton::tickColourId, Pal::accent);
    subscribe.setColour (juce::ToggleButton::tickDisabledColourId, Pal::silkLo);
    for (auto* c : std::initializer_list<juce::Component*> { &emailBox, &keyBox, &subscribe, &trialButton, &unlockButton, &buyButton, &closeButton })
        addAndMakeVisible (c);
    buyButton.getProperties().set ("lit", true);
    trialButton.getProperties().set ("lit", true);

    closeButton.onClick = [this] { setVisible (false); };
    buyButton.onClick = []
    {
        juce::URL ("https://99hertz.com/autobuild-pro?utm_source=plugin&utm_medium=autobuildpro&utm_campaign=upgrade").launchInDefaultBrowser();
    };
    unlockButton.onClick = [this]
    {
        const auto info = proc.licence.apply (keyBox.getText());
        if (info.unlocked())
        {
            proc.setLicenceInfo (info);
            message = info.kind == hz::LicenceInfo::pro ? "PRO UNLOCKED. THANK YOU!" : "PRO TRIAL UNLOCKED";
            messageIsError = false;
            keyBox.clear();
            if (onLicenceChanged) onLicenceChanged();
        }
        else
        {
            message = info.kind == hz::LicenceInfo::trialExpired ? "THAT TRIAL KEY HAS ENDED" : "THAT KEY ISN'T VALID FOR AUTOBUILD PRO";
            messageIsError = true;
        }
        refresh();
    };
    trialButton.onClick = [this]
    {
        const auto email = emailBox.getText().trim();
        if (! email.containsChar ('@') || ! email.fromFirstOccurrenceOf ("@", false, false).containsChar ('.'))
        {
            message = "ENTER YOUR EMAIL TO START THE TRIAL"; messageIsError = true; refresh(); return;
        }
        busy = true; message = "STARTING YOUR TRIAL..."; messageIsError = false; refresh();
        juce::Component::SafePointer<ProPanel> safe (this);
        proc.licence.requestTrial (email, subscribe.getToggleState(), [safe] (hz::LicenceInfo info, juce::String error)
        {
            if (safe == nullptr) return;
            safe->busy = false;
            if (info.unlocked())
            {
                safe->proc.setLicenceInfo (info);
                safe->message = "PRO UNLOCKED FOR 7 DAYS. YOUR KEY IS ON ITS WAY BY EMAIL.";
                safe->messageIsError = false;
                if (safe->onLicenceChanged) safe->onLicenceChanged();
            }
            else { safe->message = error.toUpperCase(); safe->messageIsError = true; }
            safe->refresh();
        });
    };
}

void ProPanel::show (const juce::String& why)
{
    reason = why; message = {}; messageIsError = false;
    refresh();
    setVisible (true);
    toFront (true);
}

juce::Rectangle<int> ProPanel::card() const { return getLocalBounds().withSizeKeepingCentre (600, 456); }

void ProPanel::refresh()
{
    const auto k = proc.licenceInfo.kind;
    const bool canTrial = k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid;
    const bool isPro = k == hz::LicenceInfo::pro;
    emailBox.setVisible (canTrial); subscribe.setVisible (canTrial); trialButton.setVisible (canTrial);
    keyBox.setVisible (! isPro); unlockButton.setVisible (! isPro); buyButton.setVisible (! isPro);
    trialButton.setEnabled (! busy);
    trialButton.setButtonText (busy ? "STARTING..." : "START 7-DAY FREE TRIAL");
    resized();
    repaint();
}

void ProPanel::resized()
{
    const auto c = card();
    const int x = c.getX() + 36, w = c.getWidth() - 72;
    emailBox.setBounds (x, c.getY() + 222, w - 232, 38);
    trialButton.setBounds (x + w - 220, c.getY() + 218, 220, 46);
    subscribe.setBounds (x - 4, c.getY() + 264, 260, 24);
    keyBox.setBounds (x, c.getY() + 320, w - 232, 38);
    unlockButton.setBounds (x + w - 220, c.getY() + 316, 220, 46);
    buyButton.setBounds (x, c.getBottom() - 62, 220, 46);
    closeButton.setBounds (x + w - 120, c.getBottom() - 62, 120, 46);
}

void ProPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition())) setVisible (false);
}

void ProPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.62f));
    const auto c = card().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (c.translated (0, 8.0f).expanded (4.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (Pal::panelTop.brighter (0.04f), 0, c.getY(), Pal::panelBot, 0, c.getBottom(), false));
    g.fillRoundedRectangle (c, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);

    const float x = c.getX() + 36.0f, w = c.getWidth() - 72.0f;
    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", juce::Rectangle<float> (x, c.getY() + 24.0f, w, 14.0f), juce::Justification::centredLeft);
    const auto titleFont = Fonts::silkBold (28.0f, 0.06f);
    g.setFont (titleFont);
    g.setColour (Pal::silk);
    g.drawText ("AUTOBUILD", juce::Rectangle<float> (x, c.getY() + 38.0f, w, 34.0f), juce::Justification::centredLeft);
    g.setColour (Pal::accent);
    g.drawText ("PRO", juce::Rectangle<float> (x + textWidth (titleFont, "AUTOBUILD") + 9.0f, c.getY() + 38.0f, 80.0f, 34.0f), juce::Justification::centredLeft);

    const auto k = proc.licenceInfo.kind;
    juce::String sub = reason;
    if (k == hz::LicenceInfo::trial)             sub = "PRO TRIAL: " + juce::String (proc.licenceInfo.daysLeft()) + (proc.licenceInfo.daysLeft() == 1 ? " DAY LEFT" : " DAYS LEFT");
    else if (k == hz::LicenceInfo::trialExpired) sub = "YOUR FREE TRIAL HAS ENDED";
    else if (k == hz::LicenceInfo::pro)          sub = "UNLOCKED FOR " + proc.licenceInfo.email.toUpperCase();
    else if (sub.isEmpty())                      sub = "TRY EVERY PRO FEATURE FREE FOR 7 DAYS";
    g.setFont (Fonts::led (14.0f, 0.08f));
    glowText (g, sub, { x, c.getY() + 78.0f, w, 18.0f }, juce::Justification::centredLeft, k == hz::LicenceInfo::trialExpired ? Pal::warn : Pal::accent);

    const char* feats[] = { "Five more build styles: riser, pitch, stutter, wash, mega",
                            "Mix every layer yourself and shape the curve",
                            "Impact: a hit generated right on the drop",
                            "Downlifter: a falling sweep after the drop" };
    g.setFont (Fonts::silkMed (16.0f, 0.03f));
    for (int i = 0; i < 4; ++i)
    {
        const float y = c.getY() + 106.0f + i * 22.0f;
        g.setColour (Pal::accent);
        g.fillEllipse (x + 1.0f, y + 7.0f, 6.0f, 6.0f);
        g.setColour (Pal::silk);
        g.drawText (feats[i], juce::Rectangle<float> (x + 16.0f, y, w - 16.0f, 20.0f), juce::Justification::centredLeft);
    }

    g.setFont (Fonts::silkSemi (12.5f, 0.16f));
    g.setColour (Pal::silkMid);
    if (k == hz::LicenceInfo::none || k == hz::LicenceInfo::invalid)
        g.drawText ("NO CARD NEEDED. AFTER 7 DAYS IT GOES BACK TO FREE.", juce::Rectangle<float> (x, c.getY() + 200.0f, w, 14.0f), juce::Justification::centredLeft);
    if (k != hz::LicenceInfo::pro)
        g.drawText ("ALREADY BOUGHT IT? PASTE YOUR KEY", juce::Rectangle<float> (x, c.getY() + 302.0f, w, 14.0f), juce::Justification::centredLeft);

    if (message.isNotEmpty())
    {
        g.setFont (Fonts::led (13.0f, 0.04f));
        g.setColour (messageIsError ? Pal::warn : Pal::accent);
        g.drawFittedText (message, juce::Rectangle<int> ((int) x, (int) c.getBottom() - 86, (int) w, 18), juce::Justification::centredLeft, 1);
    }
}

// ── Build display ─────────────────────────────────────────────────────────────
using ABP = AutoBuildProAudioProcessor;

void BuildDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    drawScreen (g, area);
    const bool pro = proc.proUnlocked.load();
    const auto e = proc.resolve (pro);
    const int bars = ab::barSteps[e.barStep];
    const bool playing = proc.dispPlaying.load();
    const float prog = proc.dispProgress.load();
    const float level = proc.dispLevel.load();
    const double bpm = proc.dispBpm.load(), bpb = proc.dispBeatsPerBar.load();

    const int waitBars = e.manual ? 0 : ab::waitSteps[e.waitStep];
    const float cycleNow = proc.dispCycle.load() * (float) (bars + waitBars);

    // header line
    auto head = area.reduced (16.0f, 9.0f).withHeight (14.0f);
    g.setFont (Fonts::led (12.0f, 0.14f));
    glowText (g, e.manual ? "MANUAL BUILD" : "BUILD  " + juce::String (bars) + (bars == 1 ? " BAR" : " BARS"), head, juce::Justification::centredLeft, Pal::accent.withAlpha (0.6f));
    juce::String right;
    if (e.manual) right = "BUILD " + juce::String (juce::roundToInt (level * 100.0f)) + " %";
    else if (! playing || prog < 0.0f) right = "PRESS PLAY";
    else
    {
        const double barsLeft = (double) (bars + waitBars) - (double) cycleNow;
        right = "DROP IN " + juce::String (barsLeft, 1) + (std::abs (barsLeft - 1.0) < 0.05 ? " BAR" : " BARS");
    }
    glowText (g, right, head, juce::Justification::centredRight, Pal::accent.withAlpha (0.75f));

    // plot: build region + after-drop region
    auto plot = area.withTrimmedTop (30.0f).reduced (16.0f, 0.0f).withTrimmedBottom (22.0f);
    const float afterW = juce::roundToInt (plot.getWidth() * 0.17f);
    auto buildR = plot.withTrimmedRight (afterW);
    auto afterR = plot.withLeft (buildR.getRight());
    juce::Rectangle<float> waitR;
    if (waitBars > 0)
    {
        waitR = buildR.removeFromLeft (buildR.getWidth() * juce::jmin (0.4f, (float) waitBars / (float) (waitBars + bars)));
        g.setColour (Pal::accent.withAlpha (0.035f));
        g.fillRect (waitR.withTrimmedRight (6.0f));
        g.setColour (Pal::accent.withAlpha (0.35f));
        g.drawHorizontalLine ((int) waitR.getBottom() - 1, waitR.getX(), waitR.getRight());
        g.setFont (Fonts::led (11.0f, 0.12f));
        g.setColour (Pal::accent.withAlpha (0.4f));
        g.drawText ("WAIT " + juce::String (waitBars) + (waitBars == 1 ? " BAR" : " BARS"), waitR.reduced (6.0f).toNearestInt(), juce::Justification::centred, false);
    }
    const float x0 = buildR.getX(), wB = buildR.getWidth(), yb = buildR.getBottom(), hB = buildR.getHeight();

    // bar grid + numbers
    const int every = bars <= 8 ? 1 : bars <= 16 ? 2 : 4;
    g.setFont (Fonts::led (10.0f, 0.04f));
    for (int b = 0; b <= bars; ++b)
    {
        const float x = x0 + wB * (float) b / (float) bars;
        const bool major = b % every == 0;
        g.setColour (Pal::accent.withAlpha (major ? 0.09f : 0.04f));
        g.drawVerticalLine ((int) x, buildR.getY(), yb);
        if (major && b < bars)
        {
            g.setColour (Pal::accent.withAlpha (0.38f));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 3.0f, yb + 4.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
        }
    }
    for (float f : { 0.5f, 1.0f })
    {
        g.setColour (Pal::accent.withAlpha (0.05f));
        g.drawHorizontalLine ((int) (yb - f * hB), x0, buildR.getRight());
    }

    // stutter roll ticks (how the repeats speed up)
    const float stut = e.layer[ab::stutter];
    if (stut > 0.001f && ! e.manual)
    {
        g.setColour (Pal::accent.withAlpha (0.12f + 0.3f * stut));
        const double totalBeats = bars * bpb;
        double beat = 0.0;
        while (beat < totalBeats - 1.0e-6)
        {
            const float u = (float) (beat / totalBeats);
            const double step = u < 0.5f ? 1.0 : u < 0.75f ? 0.5 : u < 0.875f ? 0.25 : 0.125;
            const float x = x0 + wB * u;
            g.fillRect (x, yb - 7.0f, 1.0f, 6.0f);
            beat += step;
        }
    }

    // the build curve
    juce::Path curve, fill;
    const int N = 160;
    for (int k = 0; k <= N; ++k)
    {
        const float u = (float) k / N;
        const float v = e.manual ? e.build : ABP::curveShape (u, e.curve) * e.build;
        const float x = x0 + wB * u, y = yb - v * hB;
        if (k == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, yb); fill.lineTo (x, y); }
        else { curve.lineTo (x, y); fill.lineTo (x, y); }
    }
    fill.lineTo (buildR.getRight(), yb); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Pal::accent.withAlpha (0.20f), 0, buildR.getY(), Pal::accent.withAlpha (0.02f), 0, yb, false));
    g.fillPath (fill);
    glowPath (g, curve, Pal::accent, 1.6f);

    // drop marker
    const float since = proc.dispSinceDrop.load();
    const float flash = e.manual ? 0.0f : juce::jlimit (0.0f, 1.0f, 1.0f - since / 0.5f);
    {
        const float x = buildR.getRight();
        g.setColour (Pal::accent.withAlpha (0.35f + 0.6f * flash));
        g.fillRect (x - 0.75f, area.getY() + 28.0f, 1.5f, yb - area.getY() - 28.0f);
        g.setFont (Fonts::led (11.0f, 0.12f));
        glowText (g, "DROP", juce::Rectangle<float> (x + 6.0f, yb + 3.0f, 60.0f, 14.0f), juce::Justification::centredLeft, Pal::accent.withAlpha (0.55f + 0.45f * flash));
    }

    // after the drop: impact spike + downlifter fall
    {
        const float ax = afterR.getX() + 2.0f, aw = afterR.getWidth() - 4.0f;
        if (e.downlifter > 0.001f)
        {
            juce::Path dl;
            for (int k = 0; k <= 40; ++k)
            {
                const float u = (float) k / 40.0f;
                const float y = yb - e.downlifter * 0.75f * hB * std::pow (1.0f - u, 1.6f);
                if (k == 0) dl.startNewSubPath (ax, y); else dl.lineTo (ax + aw * u, y);
            }
            glowPath (g, dl, Pal::accent.withAlpha (0.7f), 1.2f);
        }
        if (e.impact > 0.001f)
        {
            juce::Path sp;
            sp.startNewSubPath (ax, yb);
            sp.lineTo (ax, yb - e.impact * hB);
            for (int k = 1; k <= 24; ++k)
            {
                const float u = (float) k / 24.0f;
                sp.lineTo (ax + aw * 0.45f * u, yb - e.impact * hB * std::exp (-u * 4.0f));
            }
            g.setColour (Pal::accent.withAlpha (0.16f));
            juce::Path f2 (sp); f2.lineTo (ax, yb); f2.closeSubPath(); g.fillPath (f2);
            glowPath (g, sp, Pal::accent, 1.3f);
        }
        if (e.impact <= 0.001f && e.downlifter <= 0.001f)
        {
            g.setFont (Fonts::led (10.0f, 0.1f));
            g.setColour (Pal::accent.withAlpha (0.25f));
            g.drawFittedText ("NO TAIL", afterR.reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1);
        }
    }

    // playhead
    if (! e.manual && playing && prog >= 0.0f && waitBars > 0 && cycleNow < (float) waitBars)
    {
        const float x = waitR.getX() + waitR.getWidth() * cycleNow / (float) waitBars;
        g.setColour (Pal::accent.withAlpha (0.55f));
        g.fillRect (x - 0.5f, waitR.getY(), 1.0f, waitR.getHeight());
        g.setColour (Pal::accent); g.fillEllipse (x - 3.5f, yb - 3.5f, 7.0f, 7.0f);
    }
    else if (! e.manual && playing && prog >= 0.0f)
    {
        const float x = x0 + wB * prog;
        g.setColour (Pal::accent.withAlpha (0.55f));
        g.fillRect (x - 0.5f, buildR.getY(), 1.0f, hB);
        const float y = yb - level * hB;
        g.setColour (Pal::accent.withAlpha (0.25f)); g.fillEllipse (x - 7.0f, y - 7.0f, 14.0f, 14.0f);
        g.setColour (Pal::accent); g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
    }
    else if (e.manual)
    {
        const float y = yb - level * hB;
        g.setColour (Pal::accent.withAlpha (0.25f)); g.fillEllipse (buildR.getRight() - 7.0f, y - 7.0f, 14.0f, 14.0f);
        g.setColour (Pal::accent); g.fillEllipse (buildR.getRight() - 3.5f, y - 3.5f, 7.0f, 7.0f);
    }
    juce::ignoreUnused (bpm);
}

// ── Formatting ────────────────────────────────────────────────────────────────

static constexpr int W = 1160, H = 640, padX = 44;

static juce::String pctText (float v01) { return juce::String (juce::roundToInt (v01 * 100.0f)) + " %"; }
static juce::String curveText (float c)
{
    if (std::abs (c) < 2.0f) return "STRAIGHT";
    return (c > 0 ? "LATE " : "EARLY ") + juce::String (juce::roundToInt (std::abs (c)));
}

// ── Editor ────────────────────────────────────────────────────────────────────
AutoBuildProAudioProcessorEditor::AutoBuildProAudioProcessorEditor (AutoBuildProAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), proPanel (p)
{
    laf.reset (new HardwareLookAndFeel());
    setLookAndFeel (laf.get());

   #if JucePlugin_Build_Standalone
    // It's an effect: in the standalone app, pass the input through (JUCE mutes it by default).
    if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    addAndMakeVisible (display);
    for (int s = 0; s < ab::numStyles; ++s)
    {
        auto* b = styleKeys.add (new juce::TextButton (ab::style (s).name));
        b->onClick = [this, s] { applyStyle (s); };
        addAndMakeVisible (b);
    }

    manualButton.setClickingTogglesState (true);
    manualButton.setTooltip ("Off: the build runs by itself, locked to your bars. On: BUILD is the position, for automating by hand.");
    manualAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "manual", manualButton);
    addAndMakeVisible (manualButton);

    licenceButton.getProperties().set ("pill", true);
    licenceButton.onClick = [this] { openPro ({}); };
    addAndMakeVisible (licenceButton);
    proPanel.onLicenceChanged = [this] { updateLocks(); repaint(); };

    addKnob ("build", "BUILD").big = true;
    addKnob ("bars", "BARS", { "1", "2", "4", "8", "16", "32" });
    addKnob ("wait", "WAIT", { "0", "1", "2", "4", "8", "16", "32" });
    addKnob ("curve", "CURVE");
    for (int l = 0; l < ab::numLayers; ++l) addKnob (ab::layerId (l), ab::layerName (l));
    addKnob ("impact", "IMPACT");
    addKnob ("downlifter", "DOWNLIFTER");
    addKnob ("output", "OUTPUT");
    knob ("curve")->slider.onLockedClick = [this] { openPro ("SHAPING THE CURVE IS A PRO FEATURE"); };
    knob ("wait")->slider.onLockedClick  = [this] { openPro ("WAITING DRY BARS BEFORE THE BUILD IS A PRO FEATURE"); };
    for (int l = 0; l < ab::numLayers; ++l)
        knob (ab::layerId (l))->slider.onLockedClick = [this] { openPro ("MIXING THE LAYERS YOURSELF IS A PRO FEATURE"); };
    knob ("impact")->slider.onLockedClick     = [this] { openPro ("THE IMPACT ON THE DROP IS A PRO FEATURE"); };
    knob ("downlifter")->slider.onLockedClick = [this] { openPro ("THE DOWNLIFTER AFTER THE DROP IS A PRO FEATURE"); };

    addChildComponent (proPanel);   // last, so it sits on top
    setSize (W, H);
    updateLocks();
    timerCallback();
    startTimerHz (30);
}

AutoBuildProAudioProcessorEditor::~AutoBuildProAudioProcessorEditor() { setLookAndFeel (nullptr); }

void AutoBuildProAudioProcessorEditor::applyStyle (int s)
{
    if (! ab::gate::styleAllowed (s, proc.proUnlocked.load()))
    {
        openPro (juce::String (ab::style (s).name) + " IS A PRO BUILD STYLE");
        return;
    }
    for (int l = 0; l < ab::numLayers; ++l)
    {
        auto* prm = proc.apvts.getParameter (ab::layerId (l));
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (ab::style (s).amount[l]));
        prm->endChangeGesture();
    }
    timerCallback();
}

AutoBuildProAudioProcessorEditor::Knob& AutoBuildProAudioProcessorEditor::addKnob (const juce::String& id, const juce::String& label, juce::StringArray steps)
{
    auto* k = knobs.add (new Knob());
    k->label = label; k->paramId = id; k->stepLabels = steps;
    auto& s = k->slider;
    s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters (dialStart, dialEnd, true);
    auto* prm = proc.apvts.getParameter (id);
    s.setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    if (steps.size() > 1)
    {
        s.getProperties().set ("steps", steps.size());
        s.setMouseDragSensitivity (40 * steps.size());   // one step per ~40 px: feels like a detented switch
    }
    addAndMakeVisible (s);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s);
    s.onValueChange = [this] { timerCallback(); };
    return *k;
}

AutoBuildProAudioProcessorEditor::Knob* AutoBuildProAudioProcessorEditor::knob (const juce::String& id)
{
    for (auto* k : knobs) if (k->paramId == id) return k;
    return nullptr;
}

void AutoBuildProAudioProcessorEditor::resized()
{
    ledArea = { 340, 30, 440, 66 };
    manualButton.setBounds (W - padX - 130, 36, 130, 54);

    display.setBounds (padX, 122, W - 2 * padX, 196);

    styleArea = { padX, 330, W - 2 * padX, 70 };
    {
        const int kx = padX + 96, kw = W - 2 * padX - 96, gap = 10;
        const int w = (kw - gap * (ab::numStyles - 1)) / ab::numStyles;
        for (int i = 0; i < styleKeys.size(); ++i) styleKeys[i]->setBounds (kx + i * (w + gap), 332, w, 50);
    }

    // Cell widths: the stepped dials (BARS, WAIT) need room for their legends; OUTPUT for its title.
    const int top = 420, cy = 494;
    const int wBuild = 96, wStep = 104, wCurve = 90, wOutSec = 110;
    const int cell = (W - 2 * padX - wBuild - 2 * wStep - wCurve - wOutSec) / 7;
    secBuild  = { padX, top, wBuild + 2 * wStep + wCurve, 196 };
    secLayers = { secBuild.getRight(), top, 5 * cell, 196 };
    secDrop   = { secLayers.getRight(), top, 2 * cell, 196 };
    secOut    = { secDrop.getRight(), top, W - padX - secDrop.getRight(), 196 };
    auto place = [&] (const juce::String& id, int x0, int w)
    {
        auto* k = knob (id);
        const int D = k->big ? 70 : 60;
        k->slider.setBounds (x0 + (w - D) / 2, cy - D / 2, D, D);
    };
    place ("build", secBuild.getX(), wBuild);
    place ("bars", secBuild.getX() + wBuild, wStep);
    place ("wait", secBuild.getX() + wBuild + wStep, wStep);
    place ("curve", secBuild.getX() + wBuild + 2 * wStep, wCurve);
    for (int l = 0; l < ab::numLayers; ++l) place (ab::layerId (l), secLayers.getX() + l * cell, cell);
    place ("impact", secDrop.getX(), cell);
    place ("downlifter", secDrop.getX() + cell, cell);
    place ("output", secOut.getX(), secOut.getWidth());

    proPanel.setBounds (getLocalBounds());
    renderBackground();
    updateLocks();
}

void AutoBuildProAudioProcessorEditor::renderBackground()
{
    const float sc = 2.0f;
    background = juce::Image (juce::Image::ARGB, (int) (W * sc), (int) (H * sc), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (sc));

    auto panel = juce::Rectangle<float> (0, 0, (float) W, (float) H);
    g.setColour (juce::Colour (0xff0a0a0b)); g.fillAll();
    g.setGradientFill (juce::ColourGradient (Pal::panelTop, 0, 0, Pal::panelBot, 0, (float) H, false));
    g.fillRoundedRectangle (panel, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.022f));
    for (int x = 0; x < W; x += 3) g.fillRect ((float) x, 0.0f, 1.0f, (float) H);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (10.0f, 0.5f, W - 10.0f, 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine (10.0f, H - 1.0f, W - 10.0f, H - 1.0f, 2.0f);

    drawScrew (g, { 19.0f, 19.0f }, 0.49f);
    drawScrew (g, { W - 19.0f, 19.0f }, -0.91f);
    drawScrew (g, { 19.0f, H - 19.0f }, 1.29f);
    drawScrew (g, { W - 19.0f, H - 19.0f }, -0.21f);

    // Logo
    g.setFont (Fonts::silkSemi (12.0f, 0.34f));
    g.setColour (juce::Colour (0xffa9adb3));
    g.drawText ("99HERTZ", padX, 34, 240, 16, juce::Justification::centredLeft);
    const auto logo = Fonts::silkBold (31.0f, 0.06f);
    g.setFont (logo);
    g.setColour (Pal::silk);
    g.drawText ("AUTOBUILD", padX, 50, 240, 36, juce::Justification::centredLeft);
    g.setColour (Pal::accent);
    g.drawText ("PRO", padX + (int) textWidth (logo, "AUTOBUILD") + 10, 50, 80, 36, juce::Justification::centredLeft);

    // Dividers
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.fillRect ((float) padX, 110.0f, (float) (W - 2 * padX), 1.0f);
    g.fillRect ((float) padX, 410.0f, (float) (W - 2 * padX), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (auto s : { secLayers, secDrop, secOut })
        g.fillRect ((float) s.getX() - 0.5f, (float) s.getY(), 1.0f, (float) s.getHeight() - 26.0f);

    // Style notes under the keys
    g.setFont (Fonts::silkMed (11.5f, 0.12f));
    g.setColour (Pal::silkLo);
    for (int i = 0; i < styleKeys.size(); ++i)
    {
        const auto kb = styleKeys[i]->getBounds();
        g.drawText (ab::style (i).note, kb.getX(), kb.getBottom() + 3, kb.getWidth(), 14, juce::Justification::centred);
    }

    // Step legends around the stepped dials
    for (auto* k : knobs)
    {
        if (k->stepLabels.size() < 2) continue;
        const auto b = k->slider.getBounds().toFloat();
        const auto c = b.getCentre();
        const int n = k->stepLabels.size();
        g.setFont (Fonts::led (10.5f, 0.0f));
        for (int s = 0; s < n; ++s)
        {
            const float a = dialStart + s * (dialEnd - dialStart) / (n - 1);
            const float rr = b.getWidth() * 0.5f + 11.0f;
            const juce::Point<float> pt (c.x + std::sin (a) * rr, c.y - std::cos (a) * rr);
            g.setColour (Pal::silkMid);
            g.drawText (k->stepLabels[s], juce::Rectangle<float> (pt.x - 16.0f, pt.y - 6.0f, 32.0f, 12.0f), juce::Justification::centred);
        }
    }
}

void AutoBuildProAudioProcessorEditor::updateLocks()
{
    const bool pro = proc.proUnlocked.load();
    for (int s = 0; s < styleKeys.size(); ++s)
    {
        const bool l = ! ab::gate::styleAllowed (s, pro);
        auto* b = styleKeys[s];
        if ((bool) b->getProperties().getWithDefault ("locked", false) != l) { b->getProperties().set ("locked", l); b->repaint(); }
    }
    auto lock = [&] (const juce::String& id, bool proOnly)
    {
        auto* k = knob (id);
        const bool l = proOnly && ! pro;
        if (k->slider.locked != l)
        {
            k->slider.locked = l;
            k->slider.getProperties().set ("locked", l);
            k->slider.setMouseCursor (l ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
            k->slider.repaint();
        }
    };
    lock ("curve", ab::gate::proCurve);
    lock ("wait", ab::gate::proWait);
    for (int l = 0; l < ab::numLayers; ++l) lock (ab::layerId (l), ab::gate::proLayers);
    lock ("impact", ab::gate::proImpact);
    lock ("downlifter", ab::gate::proDownlifter);

    const auto& li = proc.licenceInfo;
    juce::String t; bool warnCol = false;
    switch (li.kind)
    {
        case hz::LicenceInfo::trial:        t = "PRO TRIAL: " + juce::String (li.daysLeft()) + (li.daysLeft() == 1 ? " DAY LEFT" : " DAYS LEFT"); break;
        case hz::LicenceInfo::trialExpired: t = "TRIAL ENDED: GET PRO"; warnCol = true; break;
        case hz::LicenceInfo::pro:          t = "PRO"; break;
        default:                            t = "FREE: TRY PRO 7 DAYS"; break;
    }
    if (licenceButton.getButtonText() != t || (bool) licenceButton.getProperties()["warn"] != warnCol || licenceButton.getWidth() == 0)
    {
        licenceButton.setButtonText (t);
        licenceButton.getProperties().set ("warn", warnCol);
        const int w = (int) std::ceil (textWidth (Fonts::led (11.5f, 0.1f), t)) + 24;
        licenceButton.setBounds (padX, 88, w, 20);
        licenceButton.repaint();
    }
}

void AutoBuildProAudioProcessorEditor::timerCallback()
{
    const bool pro = proc.proUnlocked.load();
    const auto e = proc.resolve (pro);
    updateLocks();
    const int st = proc.matchingStyle (e);
    for (int s = 0; s < styleKeys.size(); ++s)
        if (styleKeys[s]->getToggleState() != (s == st)) styleKeys[s]->setToggleState (s == st, juce::dontSendNotification);

    // Locked dials show what actually runs (CLASSIC amounts / straight / off).
    auto setShown = [&] (const juce::String& id, float pos01)
    {
        auto* k = knob (id);
        if (k->slider.locked) k->slider.getProperties().set ("displayPos", pos01);
        else                  k->slider.getProperties().remove ("displayPos");
    };
    setShown ("curve", (e.curve + 100.0f) / 200.0f);
    setShown ("wait", (float) e.waitStep / (float) (ab::numWaitSteps - 1));
    for (int l = 0; l < ab::numLayers; ++l) setShown (ab::layerId (l), e.layer[l]);
    setShown ("impact", e.impact);
    setShown ("downlifter", e.downlifter);

    knob ("build")->value = pctText (e.build);
    knob ("bars")->value  = juce::String (ab::barSteps[e.barStep]) + (e.barStep == 0 ? " BAR" : " BARS");
    knob ("curve")->value = curveText (e.curve);
    {
        const int w = ab::waitSteps[e.waitStep];
        knob ("wait")->value = w == 0 ? juce::String ("OFF") : juce::String (w) + (w == 1 ? " BAR" : " BARS");
    }
    for (int l = 0; l < ab::numLayers; ++l) knob (ab::layerId (l))->value = e.layer[l] < 0.005f ? juce::String ("OFF") : pctText (e.layer[l]);
    knob ("impact")->value     = e.impact < 0.005f ? juce::String ("OFF") : pctText (e.impact);
    knob ("downlifter")->value = e.downlifter < 0.005f ? juce::String ("OFF") : pctText (e.downlifter);
    knob ("output")->value     = (e.outDb > 0.04f ? "+" : "") + juce::String (e.outDb, 1) + " DB";

    // LED
    const juce::String styleName = st >= 0 ? juce::String (ab::style (st).name) : juce::String ("CUSTOM");
    const bool playing = proc.dispPlaying.load();
    const float prog = proc.dispProgress.load();
    const int bars = ab::barSteps[e.barStep];
    const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    if (e.manual)
    {
        ledLabel = "MANUAL" + dot + styleName;
        ledText = "BUILD " + pctText (proc.dispLevel.load());
    }
    else if (! playing || prog < 0.0f)
    {
        ledLabel = "STOPPED" + dot + styleName;
        ledText = "PRESS PLAY";
    }
    else
    {
        const int waitBars = ab::waitSteps[e.waitStep];
        const float cyc = proc.dispCycle.load() * (float) (bars + waitBars);
        if (cyc < (float) waitBars)
        {
            ledLabel = "WAIT " + juce::String (juce::jlimit (1, waitBars, (int) std::floor (cyc) + 1)) + " OF " + juce::String (waitBars) + dot + styleName;
            ledText = "BUILD IN " + juce::String ((float) waitBars - cyc, 1) + " BARS";
        }
        else
        {
            const int bar = juce::jlimit (1, bars, (int) std::floor (prog * bars) + 1);
            ledLabel = "BAR " + juce::String (bar) + " OF " + juce::String (bars) + dot + styleName;
            ledText = "BUILD " + pctText (proc.dispLevel.load());
        }
    }
    ledRightLabel = "TEMPO";
    ledRightText = juce::String (juce::roundToInt (proc.dispBpm.load()));

    display.repaint();
    repaint (ledArea.expanded (12));
    repaint (secBuild.getUnion (secLayers).getUnion (secDrop).getUnion (secOut));
}

void AutoBuildProAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());
    const bool pro = proc.proUnlocked.load();

    // ── LED display ──
    {
        auto r = ledArea.toFloat();
        drawScreen (g, r);
        auto in = r.reduced (20.0f, 11.0f);
        auto right = in.removeFromRight (76.0f);
        g.setFont (Fonts::led (12.0f, 0.14f));
        glowText (g, ledLabel, in.removeFromTop (16.0f), juce::Justification::centredLeft, Pal::accent.withAlpha (0.75f));
        float fs = 21.0f;
        while (fs > 14.0f && textWidth (Fonts::led (fs, 0.04f), ledText) > in.getWidth() - 6.0f) fs -= 1.0f;
        g.setFont (Fonts::led (fs, 0.04f));
        glowText (g, ledText, in, juce::Justification::centredLeft, Pal::accent);
        g.setFont (Fonts::led (12.0f, 0.14f));
        glowText (g, ledRightLabel, right.removeFromTop (16.0f), juce::Justification::centredRight, Pal::accent.withAlpha (0.75f));
        g.setFont (Fonts::led (21.0f, 0.04f));
        glowText (g, ledRightText, right, juce::Justification::centredRight, Pal::accent);
    }

    // ── Section titles ──
    auto title = [&] (juce::Rectangle<int> s, const juce::String& t, bool on, int inset, bool lockedSection)
    {
        auto r = s.toFloat().withTrimmedLeft ((float) inset).withHeight (20.0f);
        if (on) { g.setColour (Pal::accent.withAlpha (0.3f)); g.fillEllipse (r.getX() - 2.5f, r.getCentreY() - 6.0f, 12.0f, 12.0f); }
        g.setColour (on ? Pal::accent : Pal::lampOff);
        g.fillEllipse (r.getX() + 1.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
        const auto f = Fonts::silkBold (16.0f, 0.22f);
        g.setFont (f);
        g.setColour (Pal::silk);
        g.drawText (t, r.withTrimmedLeft (17.0f), juce::Justification::centredLeft);
        if (lockedSection)
            padlock (g, { r.getX() + 17.0f + textWidth (f, t) + 8.0f, r.getY() + 3.0f, 10.0f, 13.0f }, Pal::silkLo);
    };
    title ({ padX, 347, 96, 20 }, "STYLE", true, 0, false);
    title (secBuild, "BUILD", true, 0, false);
    title (secLayers, "LAYERS", pro, 18, ! pro);
    title (secDrop, "DROP", pro, 18, ! pro);
    title (secOut, "OUTPUT", true, 14, false);

    // ── Dial labels + values ──
    for (auto* k : knobs)
    {
        const auto sb = k->slider.getBounds();
        auto cell = juce::Rectangle<float> ((float) sb.getCentreX() - 42.0f, (float) sb.getBottom() + 12.0f, 84.0f, 16.0f);
        g.setFont (Fonts::silkSemi (13.0f, 0.16f));
        g.setColour (Pal::silk.withAlpha (k->slider.locked ? 0.6f : 1.0f));
        g.drawFittedText (k->label, cell.toNearestInt(), juce::Justification::centred, 1, 0.9f);
        g.setFont (Fonts::led (13.0f, 0.04f));
        g.setColour (Pal::accent.withAlpha (k->slider.locked ? 0.7f : 1.0f));
        g.drawFittedText (k->value, cell.translated (0, 17.0f).toNearestInt(), juce::Justification::centred, 1, 0.9f);
    }
    if (! pro)
    {
        g.setFont (Fonts::silkMed (12.0f, 0.06f));
        g.setColour (Pal::silkLo);
        g.drawText ("Lite runs the CLASSIC build. Click a padlock for more styles, the impact and the downlifter.",
                    juce::Rectangle<int> (secLayers.getX(), secLayers.getBottom() - 22, secLayers.getWidth() + secDrop.getWidth(), 16),
                    juce::Justification::centred);
    }
}
