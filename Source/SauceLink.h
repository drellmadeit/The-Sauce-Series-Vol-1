#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

/** Track Link: lets one The Sauce Suite instance hear another track WITHOUT DAW sidechain routing.
    Put The Sauce Suite on the beat (or a reference vocal) and set it to "Send". It passes that track through untouched
    and streams its audio into a shared ring buffer. Any other instance in the same DAW process can pick it as its
    input in the Routing panel, and everything that uses the sidechain (spectrum, Vocal Coach, Sauce Assistant,
    Auto Key, Match EQ reference) then works from it.
    Lock-free on the audio side: the sender writes samples + an atomic write position; receivers keep their own
    read position. Names are only touched on the message thread. */
class SauceLink
{
public:
    static constexpr int kSlots = 16, kRing = 1 << 17;          // ~2.7 s at 48 kHz per sender
    enum Kind { Beat = 1, Reference = 2 };

    static SauceLink& get() { static SauceLink l; return l; }

    struct Sender { int slot = -1; juce::String name; int kind = Beat; };

    // ---- message thread ----
    int openSlot (const juce::String& name, int kind)
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        for (int i = 0; i < kSlots; ++i)
            if (! slots[(size_t) i].used.load())
            {
                auto& s = slots[(size_t) i];
                s.writePos.store (0); s.kind.store (kind); s.name = name; s.generation.fetch_add (1);
                s.used.store (true);
                return i;
            }
        return -1;
    }
    void closeSlot (int slot)
    {
        if (slot < 0 || slot >= kSlots) return;
        const juce::SpinLock::ScopedLockType sl (nameLock);
        slots[(size_t) slot].used.store (false); slots[(size_t) slot].generation.fetch_add (1);
    }
    void rename (int slot, const juce::String& name, int kind)
    {
        if (slot < 0 || slot >= kSlots) return;
        const juce::SpinLock::ScopedLockType sl (nameLock);
        slots[(size_t) slot].name = name; slots[(size_t) slot].kind.store (kind);
    }
    std::vector<Sender> senders (int excludeSlot = -1) const
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        std::vector<Sender> r;
        for (int i = 0; i < kSlots; ++i)
            if (i != excludeSlot && slots[(size_t) i].used.load()) r.push_back ({ i, slots[(size_t) i].name, slots[(size_t) i].kind.load() });
        return r;
    }
    int findByName (const juce::String& name) const
    {
        const juce::SpinLock::ScopedLockType sl (nameLock);
        for (int i = 0; i < kSlots; ++i) if (slots[(size_t) i].used.load() && slots[(size_t) i].name == name) return i;
        return -1;
    }
    bool isAlive (int slot) const { return slot >= 0 && slot < kSlots && slots[(size_t) slot].used.load(); }

    // ---- audio thread: sender ----
    void write (int slot, const float* const* ch, int numCh, int start, int n) noexcept
    {
        if (! isAlive (slot) || numCh <= 0) return;
        auto& s = slots[(size_t) slot];
        uint64_t w = s.writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            float v = 0; for (int c = 0; c < numCh; ++c) v += ch[c][start + i];
            s.ring[(size_t) ((w + (uint64_t) i) & (kRing - 1))] = v / (float) numCh;
        }
        s.writePos.store (w + (uint64_t) n, std::memory_order_release);
    }

    // ---- audio thread: receiver ----
    struct Reader { int slot = -1; uint64_t pos = 0; uint32_t generation = 0; bool synced = false; };
    /** Copies the sender's newest audio into dest (n samples). Returns false (and silence) when nothing is linked. */
    bool read (Reader& r, float* dest, int n) noexcept
    {
        if (! isAlive (r.slot)) { std::fill (dest, dest + n, 0.0f); r.synced = false; return false; }
        auto& s = slots[(size_t) r.slot];
        const uint32_t gen = s.generation.load();
        const uint64_t w = s.writePos.load (std::memory_order_acquire);
        if (! r.synced || gen != r.generation || r.pos > w || w - r.pos > (uint64_t) (kRing / 2))
        {
            r.pos = w >= (uint64_t) n ? w - (uint64_t) n : 0; r.generation = gen; r.synced = true;
        }
        const int avail = (int) std::min<uint64_t> ((uint64_t) n, w - r.pos);
        const int pad = n - avail;                              // sender hasn't produced this block yet: fill the front with silence
        std::fill (dest, dest + pad, 0.0f);
        for (int i = 0; i < avail; ++i) dest[pad + i] = s.ring[(size_t) ((r.pos + (uint64_t) i) & (kRing - 1))];
        r.pos += (uint64_t) avail;
        return true;
    }

private:
    struct Slot
    {
        std::atomic<bool> used { false };
        std::atomic<uint64_t> writePos { 0 };
        std::atomic<int> kind { Beat };
        std::atomic<uint32_t> generation { 0 };
        juce::String name;
        std::array<float, kRing> ring {};
    };
    std::array<Slot, kSlots> slots;
    mutable juce::SpinLock nameLock;
};
