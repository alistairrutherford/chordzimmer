#include "ParamControl.h"
#include "Theme.h"

namespace chordzimmer::ui
{
    void Bindings::set (const juce::String& id, float plainValue) const
    {
        if (auto* p = param (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
            p->endChangeGesture();
        }
    }

    //==========================================================================
    ParamControl::ParamControl (Bindings& b, const juce::String& id, const juce::String& label, bool show)
        : bindings (b), showLabel (show), accent (colours::chord)
    {
        caption.setJustificationType (juce::Justification::centredLeft);
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);
        bind (id, label);
    }

    ParamControl::~ParamControl()
    {
        comboAttachment.reset();
        buttonAttachment.reset();
        sliderAttachment.reset();
    }

    void ParamControl::setAccent (juce::Colour c)
    {
        accent = c;
        if (control != nullptr)
        {
            control->setColour (juce::TextButton::buttonOnColourId, accent);
            control->setColour (juce::Slider::trackColourId, accent.withAlpha (0.55f));
        }
    }

    void ParamControl::bind (const juce::String& id, const juce::String& label)
    {
        comboAttachment.reset();
        buttonAttachment.reset();
        sliderAttachment.reset();
        control.reset();
        paramId = id;

        auto* p = bindings.param (id);
        jassert (p != nullptr);
        if (p == nullptr)
            return;
        bindings.bound (id);
        const juce::String text = label.isNotEmpty() ? label : p->getName (64);
        caption.setText (text, juce::dontSendNotification);
        setTooltip (p->getName (64));

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (choice->choices, 1);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (bindings.state, id, *box);
            control = std::move (box);
            caption.setVisible (showLabel);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
        {
            auto button = std::make_unique<juce::TextButton> (text);
            button->setClickingTogglesState (true);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (bindings.state, id, *button);
            control = std::move (button);
            caption.setVisible (false);
        }
        else
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxAbove);
            slider->setVelocityBasedMode (false);
            // On the slider itself: it may be built before the editor's LookAndFeel is set.
            slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxTextColourId, colours::text);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (bindings.state, id, *slider);
            control = std::move (slider);
            caption.setVisible (showLabel);
        }
        addAndMakeVisible (*control);
        setAccent (accent);
        resized();
    }

    void ParamControl::resized()
    {
        auto r = getLocalBounds();
        if (caption.isVisible() && captionLeft > 0)
            caption.setBounds (r.removeFromLeft (captionLeft));
        else if (caption.isVisible())
            caption.setBounds (r.removeFromTop (juce::jmin (15, r.getHeight() / 2)));
        if (control != nullptr)
            control->setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 26)));
    }
}
