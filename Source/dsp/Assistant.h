#pragma once
// Sauce Assistant: turns measurements of the beat (and your vocal) into a starting preset plus beat-specific tweaks.
// Pure C++ (no JUCE) so it can be unit-tested. Parameter ids match Parameters.h.
#include "Analysis.h"
#include <map>
#include <string>
#include <functional>

namespace sauce
{
struct AssistantInput
{
    bool hasBeat = false, hasVocal = false;
    std::vector<float> beatSpec, vocalSpec;          // LogGrid per-Hz density (dB), from SpectrumAccumulator
    float beatRmsDb = -100, beatPeakDb = -100;       // over the listening window
    float vocalOutRmsDb = -100;                      // the plugin's processed vocal level while singing
    double beatSeconds = 0, vocalSeconds = 0;
    double bpm = 0;                                  // from the DAW (0 = unknown)
    bool keyValid = false; int key = 0; bool minor = false;
    bool beatFromFile = false;                       // beat measured from a dropped file instead of live audio
};

enum class Role { Lead = 0, Hook, Adlibs, Backgrounds, Harmonies, Doubles };
inline const char* roleName (Role r)
{
    static const char* n[] = { "Lead", "Hook", "Ad-libs", "Backgrounds", "Harmonies", "Doubles" };
    return n[(int) r];
}
/** Preset name in the factory bank for a genre + role. */
inline std::string rolePresetName (const std::string& genre, Role r)
{
    static const char* suffix[] = { "Lead", "Hook", "Ad-libs", "Backgrounds", "Harmonies (Wide)", "Doubles" };
    return genre + " - " + suffix[(int) r];
}

struct BeatFeatures
{
    float sub = 0, bass = 0, lowMid = 0, mid = 0, presence = 0, air = 0;  // dB relative to the beat's own 150 Hz-5 kHz average (pink-corrected)
    float crestDb = 0;
};

struct GenreGuess { std::string genre; float score = 0; float confidence = 0; std::string why; };

struct Plan
{
    std::vector<std::pair<std::string, float>> sets;   // parameter id -> real value
    std::vector<std::string> notes;                    // plain-language explanation, in order
    void set (const std::string& id, float v) { for (auto& s : sets) if (s.first == id) { s.second = v; return; } sets.push_back ({ id, v }); }
};

class Assistant
{
public:
    /** Pink-corrected density in dB at grid point i (so a "flat" mix reads level across the spectrum). */
    static float corrected (const std::vector<float>& spec, int i) { return spec[(size_t) i] + 10.0f * std::log10 (LogGrid::freq (i) / 1000.0f); }
    static float band (const std::vector<float>& spec, float lo, float hi)
    {
        double s = 0; int n = 0;
        for (int i = 0; i < LogGrid::kPoints; ++i) { const float f = LogGrid::freq (i); if (f >= lo && f <= hi) { s += std::pow (10.0, corrected (spec, i) / 10.0); ++n; } }
        return n ? 10.0f * std::log10 ((float) (s / n) + 1e-20f) : -120.0f;
    }
    static BeatFeatures features (const AssistantInput& in)
    {
        BeatFeatures f;
        if (! in.hasBeat || (int) in.beatSpec.size() != LogGrid::kPoints) return f;
        const float ref = band (in.beatSpec, 150, 5000);
        f.sub = band (in.beatSpec, 30, 60) - ref;      f.bass = band (in.beatSpec, 60, 150) - ref;
        f.lowMid = band (in.beatSpec, 200, 500) - ref; f.mid = band (in.beatSpec, 500, 2000) - ref;
        f.presence = band (in.beatSpec, 2000, 5000) - ref; f.air = band (in.beatSpec, 8000, 16000) - ref;
        f.crestDb = in.beatPeakDb - in.beatRmsDb;
        return f;
    }

    /** Best-guess genre from tempo and tonal balance. It's an estimate: the user's own choice always wins. */
    static std::vector<GenreGuess> guessGenre (const AssistantInput& in)
    {
        const auto f = features (in);
        const double bpm = in.bpm;
        auto inRange = [bpm] (double lo, double hi) { return bpm >= lo && bpm <= hi; };
        auto halfOr = [&] (double lo, double hi) { return inRange (lo, hi) || inRange (lo / 2, hi / 2); };
        auto c01 = [] (float v) { return clampv (v, 0.0f, 1.0f); };
        const float subHeavy = c01 (f.sub / 8.0f), bright = c01 ((f.air + 6.0f) / 6.0f);
        const float dark = c01 ((-f.air - 8.0f) / 6.0f), midHeavy = c01 ((f.mid + 1.0f) / 3.0f);
        std::vector<GenreGuess> g = {
            { "Trap",              (halfOr (128, 170) ? 2.0f : 0.0f) + 1.2f * subHeavy + 0.8f * bright - 1.2f * dark, 0, "fast half-time tempo, heavy 808s, crisp hats" },
            { "Drill",             (halfOr (138, 148) ? 2.2f : 0.0f) + 1.2f * subHeavy + 0.6f * dark,   0, "140-ish BPM with sliding 808s and a darker top" },
            { "Melodic Rap",       (halfOr (120, 160) ? 1.4f : 0.0f) + 0.8f * subHeavy + 0.4f * midHeavy, 0, "trap tempo with a more melodic, fuller middle" },
            { "Rap",               (inRange (86, 105) || halfOr (140, 160) ? 1.2f : 0.0f) + 0.6f * subHeavy, 0, "a mid-tempo hip-hop pocket" },
            { "Boom Bap",          (inRange (82, 98) ? 2.0f : 0.0f) + 1.0f * midHeavy + (f.sub < -6 ? 0.8f : 0.0f), 0, "boom-bap tempo, punchy mids, lighter sub" },
            { "R&B",               (inRange (58, 82) || inRange (90, 112) ? 1.6f : 0.0f) + 1.2f * dark + 0.4f * subHeavy, 0, "slower groove with a smooth, softer top end" },
            { "Trap-Soul",         (inRange (55, 80) || inRange (130, 150) ? 1.6f : 0.0f) + 1.2f * dark + 0.8f * subHeavy - 0.6f * bright, 0, "slow, dark and bass-heavy" },
            { "Afrobeats",         (inRange (98, 118) ? 2.0f : 0.0f) + 0.6f * midHeavy + 0.4f * bright, 0, "100-115 BPM bounce with busy percussion" },
            { "Dancehall",         (inRange (88, 106) ? 1.5f : 0.0f) + 0.5f * subHeavy + 0.5f * midHeavy, 0, "dancehall tempo with a strong bass line" },
            { "Latin / Reggaeton", (inRange (86, 100) ? 1.7f : 0.0f) + 0.8f * subHeavy, 0, "reggaeton tempo with a deep kick" },
            { "Pop",               (inRange (100, 130) ? 1.4f : 0.0f) + 1.0f * bright + 0.3f * midHeavy, 0, "bright, full-range pop balance" },
            { "EDM",               (inRange (120, 132) ? 2.0f : 0.0f) + 1.2f * bright + 0.6f * subHeavy, 0, "club tempo, very bright and full" },
            { "Lo-Fi",             (inRange (65, 92) ? 1.2f : 0.0f) + 1.4f * dark - 0.6f * subHeavy, 0, "relaxed tempo and a rolled-off, dusty top" },
            { "Rock",              (inRange (100, 170) ? 0.8f : 0.0f) + 1.4f * midHeavy + (f.sub < -8 ? 0.8f : 0.0f), 0, "guitar-heavy mids and little sub" },
        };
        std::sort (g.begin(), g.end(), [] (auto& a, auto& b) { return a.score > b.score; });
        const float total = std::max (0.01f, g[0].score + g[1].score);
        g[0].confidence = clampv (g[0].score / total, 0.0f, 1.0f);
        if (bpm <= 0) g[0].confidence *= 0.6f;
        return g;
    }

    /** Beat-specific tweaks applied on top of the chosen preset. get(id) returns the preset's current value. */
    static Plan tailor (const AssistantInput& in, const std::string& genre, Role role, const std::function<float (const std::string&)>& get)
    {
        Plan p;
        const auto f = features (in);
        char buf[256];
        const bool rapLike = genre == "Rap" || genre == "Trap" || genre == "Drill" || genre == "Melodic Rap" || genre == "Boom Bap";

        if (in.hasBeat)
        {
            // 1. high-pass vs the 808
            const float hpf = get ("eq1_freq");
            const float want = f.sub > 3 ? 135.0f : (f.sub > -2 ? 105.0f : hpf);
            if (get ("eq1_type") == 3.0f && want > hpf + 5)
            {
                p.set ("eq1_on", 1); p.set ("eq1_freq", want);
                std::snprintf (buf, sizeof buf, "Heavy low end in the beat: raised the vocal high-pass to %d Hz so it stays out of the 808's way.", (int) want);
                p.notes.push_back (buf);
            }
            // 2. low-mid clash: cut the vocal where the beat's low mids are thickest
            float worst = -100, at = 300;
            const float ref = band (in.beatSpec, 150, 5000);
            for (int i = 0; i < LogGrid::kPoints; ++i)
            {
                const float fr = LogGrid::freq (i);
                if (fr < 180 || fr > 600) continue;
                float score = corrected (in.beatSpec, i) - ref;
                if (in.hasVocal && (int) in.vocalSpec.size() == LogGrid::kPoints) score += 0.5f * (corrected (in.vocalSpec, i) - band (in.vocalSpec, 150, 5000));
                if (score > worst) { worst = score; at = fr; }
            }
            if (worst > 1.0f)
            {
                const float cut = -clampv (1.2f + 0.5f * worst, 1.5f, 4.5f);
                p.set ("eq3_on", 1); p.set ("eq3_type", 0); p.set ("eq3_freq", at); p.set ("eq3_gain", std::min (cut, get ("eq3_gain"))); p.set ("eq3_q", 1.2f);
                std::snprintf (buf, sizeof buf, "The beat is crowded around %d Hz: cut the vocal %.1f dB there so the two don't fight.", (int) (std::round (at / 10) * 10), -cut);
                p.notes.push_back (buf);
            }
            // 3. presence pocket: lift the vocal where the beat leaves room
            float best = 100, pocket = 3000;
            for (int i = 0; i < LogGrid::kPoints; ++i)
            {
                const float fr = LogGrid::freq (i);
                if (fr < 1500 || fr > 5000) continue;
                const float v = corrected (in.beatSpec, i) - ref;
                if (v < best) { best = v; pocket = fr; }
            }
            const float lift = clampv (1.0f + 0.35f * std::max (0.0f, f.presence - best), 1.0f, 3.0f);
            p.set ("eq4_on", 1); p.set ("eq4_type", 0); p.set ("eq4_freq", pocket); p.set ("eq4_q", 1.0f);
            p.set ("eq4_gain", std::min (5.0f, std::max (get ("eq4_gain"), 0.0f) + lift));
            std::snprintf (buf, sizeof buf, "The beat leaves a pocket near %.1f kHz: lifted the vocal %.1f dB there so the words cut through.", pocket / 1000.0f, lift);
            p.notes.push_back (buf);
            // 4. air vs the hats
            if (f.air > -4.0f)
            {
                p.set ("eq6_gain", std::min (8.0f, get ("eq6_gain") + 1.2f)); p.set ("eq5_gain", std::max (-8.0f, get ("eq5_gain") - 1.0f));
                p.notes.push_back ("Bright hi-hats: added a little more vocal air and tamed the S's so they don't stack with the hats.");
            }
            else if (f.air < -12.0f)
            {
                p.set ("eq6_gain", get ("eq6_gain") - 1.0f);
                p.notes.push_back ("Dark, smooth beat: pulled back the vocal's top end a touch so it blends instead of sticking out.");
            }
            // 5. compression vs density
            if (f.crestDb > 0 && f.crestDb < 9.0f)
            {
                p.set ("comp_ratio", std::min (12.0f, get ("comp_ratio") + 1.0f)); p.set ("comp_thresh", get ("comp_thresh") - 3.0f);
                p.set ("comp_makeup", std::min (18.0f, get ("comp_makeup") + 2.0f));
                p.notes.push_back ("Loud, dense beat: compressed the vocal a bit more so every word stays on top.");
            }
            else if (f.crestDb > 14.0f)
            {
                p.set ("comp_ratio", std::max (1.5f, get ("comp_ratio") - 0.5f));
                p.notes.push_back ("Open, dynamic beat: eased the compression to keep the vocal natural.");
            }
            // 6. busy mids: duck the space harder
            if (f.mid > 1.5f || (f.crestDb > 0 && f.crestDb < 9.0f))
            {
                p.set ("rev_duck", std::min (90.0f, get ("rev_duck") + 15)); p.set ("dly_duck", std::min (90.0f, get ("dly_duck") + 15));
                p.set ("rev_mix", get ("rev_mix") * 0.85f);
                p.notes.push_back ("Busy arrangement: more reverb/delay ducking so the words stay clear and the space blooms in the gaps.");
            }
        }

        // 6b. clash check: where your vocal and the beat are BOTH strong (masking), beyond what was fixed above
        if (in.hasBeat && in.hasVocal && (int) in.vocalSpec.size() == LogGrid::kPoints)
        {
            const float vref = band (in.vocalSpec, 150, 5000), bref = band (in.beatSpec, 150, 5000);
            struct Z { float lo, hi; const char* what; };
            static const Z zones[] = { { 150, 300, "body (150-300 Hz)" }, { 300, 700, "low mids / boxiness (300-700 Hz)" },
                                       { 700, 1500, "mids / nasal (0.7-1.5 kHz)" }, { 1500, 3500, "presence / words (1.5-3.5 kHz)" },
                                       { 3500, 7000, "bite / edge (3.5-7 kHz)" } };
            float worstScore = -100; const Z* worstZ = nullptr;
            for (auto& z : zones)
            {
                const float v = band (in.vocalSpec, z.lo, z.hi) - vref, b = band (in.beatSpec, z.lo, z.hi) - bref;
                const float score = std::min (v, b);          // both above their own average = they fight here
                if (score > worstScore) { worstScore = score; worstZ = &z; }
            }
            if (worstZ != nullptr && worstScore > -1.0f)
            {
                std::snprintf (buf, sizeof buf, "Clash: vocal and beat both peak in the %s. %s", worstZ->what,
                               worstZ->lo >= 1500 ? "Dip that range 2-3 dB on the beat's track."
                                                  : "Vocal cut there already; a 2 dB dip on the beat helps more.");
                p.notes.push_back (buf);
            }
            else p.notes.push_back ("Clash: none - your vocal and the beat share the space well.");
        }

        // 7. tempo-aware delay and reverb
        if (in.bpm > 30)
        {
            p.set ("dly_sync", 1);
            const float div = in.bpm >= 125 ? 2.0f : (in.bpm <= 95 ? 3.0f : get ("dly_div"));
            p.set ("dly_div", div);
            const float pre = clampv ((float) (60000.0 / in.bpm / 16.0), 8.0f, 60.0f);
            p.set ("rev_predelay", pre);
            const float bar = (float) (240.0 / in.bpm);
            const float cap = std::max (0.6f, bar * (rapLike && role == Role::Lead ? 0.6f : 1.0f));
            if (get ("rev_decay") > cap) p.set ("rev_decay", cap);
            std::snprintf (buf, sizeof buf, "Timed the delay (1/%d) and reverb (%.0f ms pre-delay, tail under %.1f s) to %.0f BPM.",
                           div == 2.0f ? 8 : 4, pre, std::min (cap, get ("rev_decay")), in.bpm);
            p.notes.push_back (buf);
        }
        // 8. key
        if (in.keyValid)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            p.set ("tune_key", (float) in.key); p.set ("tune_scale", in.minor ? 1.0f : 0.0f); p.set ("tune_autokey", 1);
            std::snprintf (buf, sizeof buf, "Set Auto Tune to %s %s (detected from the music).", names[in.key], in.minor ? "minor" : "major");
            p.notes.push_back (buf);
        }
        if (! in.hasBeat)
            p.notes.push_back ("No beat heard: tuned for your vocal only. Drop your beat file on the Assistant, link the beat track, or route it to the sidechain for beat-aware EQ and level.");
        return p;
    }

    /** Where the processed vocal should sit against the beat (RMS while singing, dB). */
    static float levelTarget (const std::string& genre, Role role)
    {
        const bool rapLike = genre == "Rap" || genre == "Trap" || genre == "Drill" || genre == "Melodic Rap" || genre == "Boom Bap";
        switch (role)
        {
            case Role::Adlibs:      return -5.0f;
            case Role::Backgrounds: return -7.0f;
            case Role::Harmonies:   return -6.0f;
            case Role::Doubles:     return -6.0f;
            case Role::Hook:        return rapLike ? 1.5f : 0.5f;
            default:                return rapLike ? 1.0f : 0.0f;
        }
    }

    enum class Nudge { MorePolished, MoreRaw, Wetter, Drier, Brighter, Darker, Louder, Quieter, HarderTune, SofterTune };
    /** One feedback step. Small, musical moves the user can repeat. */
    static Plan nudge (Nudge n, const std::function<float (const std::string&)>& get)
    {
        Plan p;
        switch (n)
        {
            case Nudge::MorePolished: p.set ("comp_ratio", std::min (12.0f, get ("comp_ratio") + 1)); p.set ("eq6_gain", get ("eq6_gain") + 1);
                                      p.set ("sat_mix", std::max (0.0f, get ("sat_mix") - 10)); p.set ("tune_on", 1);
                                      p.set ("tune_speed", std::max (0.0f, get ("tune_speed") * 0.6f)); p.notes.push_back ("More polished: tighter compression, more air, cleaner tone, firmer tuning."); break;
            case Nudge::MoreRaw:      p.set ("comp_ratio", std::max (1.5f, get ("comp_ratio") - 1)); p.set ("sat_on", 1); p.set ("sat_mix", std::min (100.0f, get ("sat_mix") + 12));
                                      p.set ("tune_speed", std::min (400.0f, get ("tune_speed") + 25)); p.set ("eq6_gain", get ("eq6_gain") - 1);
                                      p.notes.push_back ("More raw: looser compression, more grit, gentler tuning."); break;
            case Nudge::Wetter:       p.set ("rev_mix", std::min (100.0f, get ("rev_mix") + 6)); p.set ("dly_mix", std::min (100.0f, get ("dly_mix") + 5));
                                      p.set ("rev_on", 1); p.set ("dly_on", 1); p.notes.push_back ("Wetter: more reverb and delay."); break;
            case Nudge::Drier:        p.set ("rev_mix", std::max (0.0f, get ("rev_mix") - 6)); p.set ("dly_mix", std::max (0.0f, get ("dly_mix") - 5));
                                      p.notes.push_back ("Drier: less reverb and delay."); break;
            case Nudge::Brighter:     p.set ("eq6_gain", std::min (12.0f, get ("eq6_gain") + 1.5f)); p.set ("eq4_gain", std::min (8.0f, get ("eq4_gain") + 0.8f));
                                      p.notes.push_back ("Brighter: more air and presence."); break;
            case Nudge::Darker:       p.set ("eq6_gain", std::max (-12.0f, get ("eq6_gain") - 1.5f)); p.set ("eq4_gain", std::max (-8.0f, get ("eq4_gain") - 0.8f));
                                      p.notes.push_back ("Darker: less air and presence."); break;
            case Nudge::Louder:       p.set ("out_gain", std::min (24.0f, get ("out_gain") + 1.5f)); p.notes.push_back ("Vocal 1.5 dB louder."); break;
            case Nudge::Quieter:      p.set ("out_gain", std::max (-24.0f, get ("out_gain") - 1.5f)); p.notes.push_back ("Vocal 1.5 dB quieter."); break;
            case Nudge::HarderTune:   p.set ("tune_on", 1); p.set ("tune_speed", std::max (0.0f, get ("tune_speed") * 0.4f - 2)); p.set ("tune_amount", std::min (100.0f, get ("tune_amount") + 15));
                                      p.set ("tune_humanize", std::max (0.0f, get ("tune_humanize") - 15)); p.notes.push_back ("Harder tuning: faster retune, less humanize."); break;
            case Nudge::SofterTune:   p.set ("tune_speed", std::min (400.0f, get ("tune_speed") + 20)); p.set ("tune_amount", std::max (0.0f, get ("tune_amount") - 15));
                                      p.set ("tune_humanize", std::min (100.0f, get ("tune_humanize") + 15)); p.notes.push_back ("Softer tuning: slower retune, more humanize."); break;
        }
        return p;
    }
};
} // namespace sauce
