#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

/** Track Link menu (shared by the Routing panel, the Sauce Assistant and Match EQ). */
namespace LinkMenu
{
    inline void show (SauceProcessor& proc, juce::Component* target, std::function<void()> after = {})
    {
        using namespace juce;
        PopupMenu m;
        m.addSectionHeader ("Listen to another track (no sidechain needed)");
        const auto senders = SauceLink::get().senders();
        m.addItem (1, "None (use the DAW sidechain)", true, proc.getLinkSource().isEmpty());
        int id = 10;
        for (auto& s : senders)
        {
            const String label = String (s.kind == SauceLink::Reference ? "Reference vocal: " : "Beat: ") + s.name;
            m.addItem (id++, label, proc.getLinkSend() == SauceProcessor::LinkOff, proc.getLinkSource() == s.name);
        }
        if (senders.empty())
            m.addItem (2, "No tracks are sending yet - put The Sauce Suite on your beat track and choose 'Send' there", false, false);
        m.addSeparator();
        m.addSectionHeader ("This track");
        m.addItem (3, "Normal (process my vocal)", true, proc.getLinkSend() == SauceProcessor::LinkOff);
        m.addItem (4, "Send this track as the BEAT (passes it through untouched)", true, proc.getLinkSend() == SauceProcessor::LinkSendBeat);
        m.addItem (5, "Send this track as a REFERENCE VOCAL (for Match EQ)", true, proc.getLinkSend() == SauceProcessor::LinkSendReference);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (target), [&proc, senders, after] (int r)
        {
            if (r == 1) proc.setLinkSource ({});
            else if (r >= 10 && r - 10 < (int) senders.size())
            {
                const auto& s = senders[(size_t) (r - 10)];
                proc.setLinkSource (s.name);
                proc.setParam (ParamIDs::scRole, s.kind == SauceLink::Reference ? 1.0f : 0.0f);
            }
            else if (r == 3) proc.setLinkSend (SauceProcessor::LinkOff);
            else if (r == 4) proc.setLinkSend (SauceProcessor::LinkSendBeat);
            else if (r == 5) proc.setLinkSend (SauceProcessor::LinkSendReference);
            if (r != 0 && after) after();
        });
    }
}
