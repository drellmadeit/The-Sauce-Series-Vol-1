// Factory presets. Values are in real parameter units (dB, Hz, ms, %, choice index, 0/1).
// Anything not listed falls back to the parameter default.
// Artist-inspired presets are UNOFFICIAL starting points built from how these artists' released
// vocals commonly sound; they are not affiliated with or endorsed by the artists, and they will
// never sound like the artist's voice - they shape YOUR vocal's processing.
#include "PresetManager.h"
#include "Parameters.h"

using namespace juce;

namespace
{
    enum { Bell = 0, LoShelf, HiShelf, LoCut, HiCut, Notch };
    enum { d32 = 0, d16, d8, d4, d2, d1 };
    enum { Straight = 0, Dotted, Triplet };
    enum { Room = 0, Plate, Hall };

    struct Builder
    {
        PresetInfo p;
        Builder (const String& name, const String& genre, const String& artist = {}) { p.name = name; p.genre = genre; p.artist = artist; }
        Builder& set (const String& id, float v) { p.values[id] = v; return *this; }
        Builder& band (int b, int type, float f, float g, float q, bool on = true)
        {
            using namespace ParamIDs;
            return set (ParamIDs::band (b, "type"), (float) type).set (ParamIDs::band (b, "freq"), f).set (ParamIDs::band (b, "gain"), g)
                  .set (ParamIDs::band (b, "q"), q).set (ParamIDs::band (b, "on"), on ? 1.0f : 0.0f);
        }
        Builder& gate (float thr, float rangeDb = -30, float hold = 60, float rel = 150)
        { using namespace ParamIDs; return set (gateOn, 1).set (gateThresh, thr).set (gateRange, rangeDb).set (gateHold, hold).set (gateRelease, rel); }
        Builder& tune (bool on, float speed = 20, float amount = 100, float human = 0, int scale = 1)
        { using namespace ParamIDs; return set (tuneOn, on ? 1.0f : 0.0f).set (tuneSpeed, speed).set (tuneAmount, amount).set (tuneHumanize, human).set (tuneScale, (float) scale); }
        Builder& comp (float thr, float ratio, float atk, float rel, float makeup)
        { using namespace ParamIDs; return set (compOn, 1).set (compThresh, thr).set (compRatio, ratio).set (compAttack, atk).set (compRelease, rel).set (compMakeup, makeup); }
        Builder& sat (float drive, float mix) { using namespace ParamIDs; return set (satOn, mix > 0 ? 1.0f : 0.0f).set (satDrive, drive).set (satMix, mix); }
        Builder& dbl (float a)  { using namespace ParamIDs; return set (dblOn, a > 0 ? 1.0f : 0.0f).set (dblAmount, a); }
        Builder& wide (float a) { using namespace ParamIDs; return set (wideOn, a > 0 ? 1.0f : 0.0f).set (wideAmount, a); }
        Builder& delay (int div, int feel, float fb, float mix, float duck, bool pingPong = false, float lowCut = 250, float highCut = 6000)
        {
            using namespace ParamIDs;
            return set (dlyOn, mix > 0 ? 1.0f : 0.0f).set (dlySync, 1).set (dlyDiv, (float) div).set (dlyFeel, (float) feel).set (dlyFeedback, fb)
                  .set (dlyMix, mix).set (dlyDuck, duck).set (dlyMode, pingPong ? 1.0f : 0.0f).set (dlyLowCut, lowCut).set (dlyHighCut, highCut);
        }
        Builder& reverb (int type, float decay, float mix, float duck, float pre = 20, float damp = 40)
        {
            using namespace ParamIDs;
            return set (revOn, mix > 0 ? 1.0f : 0.0f).set (revType, (float) type).set (revDecay, decay).set (revMix, mix).set (revDuck, duck)
                  .set (revPreDelay, pre).set (revDamp, damp);
        }
        Builder& limit (float ceiling) { using namespace ParamIDs; return set (limOn, 1).set (limCeiling, ceiling); }
        operator PresetInfo() const { return p; }
    };

    String unofficial (const String& artist) { return artist + "-Inspired (Unofficial)"; }
}

//==============================================================================
// Generated bank: every genre has a recipe; roles and flavours are applied on top of it.
namespace
{
    struct Recipe
    {
        String genre;
        float hpf = 90, lsF = 200, lsG = 0, mudF = 350, mudG = -2, mudQ = 1.2f, presF = 3000, presG = 2, presQ = 0.9f, essF = 7000, essG = -1.5f, airF = 11000, airG = 2;
        float gate = -50;
        bool  tuneOn = false; float tSpeed = 25, tAmount = 80, tHuman = 30; int scale = 1;
        float cThr = -22, cRatio = 3.5f, cAtk = 8, cRel = 120, cMk = 5;
        float satDrive = 5, satMix = 20, dbl = 0, wide = 0;
        int dDiv = d8, dFeel = Straight; float dFb = 25, dMix = 14, dDuck = 55; bool ping = false; float dLc = 300, dHc = 5500;
        int rType = Plate; float rDecay = 1.8f, rMix = 18, rDuck = 55, rPre = 20, rDamp = 40;
        float ceiling = -1.0f;
        bool hiCutTop = false;              // vintage / lo-fi: band 6 becomes a high cut
        String order = "GTEMCS";            // G gate, T tune, E eq, M match eq, C compressor, S saturation
    };

    PresetInfo fromRecipe (const String& name, const Recipe& r)
    {
        auto lim = [] (float v, float lo, float hi) { return jlimit (lo, hi, v); };
        Builder b (name, r.genre);
        b.gate (lim (r.gate, -80, 0))
         .tune (r.tuneOn, lim (r.tSpeed, 0, 400), lim (r.tAmount, 0, 100), lim (r.tHuman, 0, 100), r.scale)
         .band (0, LoCut, lim (r.hpf, 20, 1000), 0, 0.71f)
         .band (1, LoShelf, r.lsF, lim (r.lsG, -18, 18), 0.71f)
         .band (2, Bell, r.mudF, lim (r.mudG, -18, 18), r.mudQ)
         .band (3, Bell, r.presF, lim (r.presG, -18, 18), r.presQ)
         .band (4, Bell, r.essF, lim (r.essG, -18, 18), 3.5f);
        if (r.hiCutTop) b.band (5, HiCut, lim (r.airF, 2000, 20000), 0, 0.71f);
        else            b.band (5, HiShelf, r.airF, lim (r.airG, -18, 18), 0.71f);
        b.comp (lim (r.cThr, -60, 0), lim (r.cRatio, 1, 20), lim (r.cAtk, 0.1f, 100), lim (r.cRel, 10, 1000), lim (r.cMk, 0, 24))
         .sat (lim (r.satDrive, 0, 24), lim (r.satMix, 0, 100)).dbl (lim (r.dbl, 0, 100)).wide (lim (r.wide, 0, 100))
         .delay (r.dDiv, r.dFeel, lim (r.dFb, 0, 95), lim (r.dMix, 0, 100), lim (r.dDuck, 0, 100), r.ping, lim (r.dLc, 20, 2000), lim (r.dHc, 1000, 20000))
         .reverb (r.rType, lim (r.rDecay, 0.2f, 10), lim (r.rMix, 0, 100), lim (r.rDuck, 0, 100), lim (r.rPre, 0, 250), lim (r.rDamp, 0, 100))
         .limit (lim (r.ceiling, -12, 0));
        PresetInfo info = b; info.chainOrder = r.order;
        return info;
    }

    std::vector<Recipe> genreRecipes()
    {
        std::vector<Recipe> g;
        Recipe r;
        r = {}; r.genre = "Rap"; r.hpf = 100; r.lsG = -1; r.mudG = -2.5f; r.presF = 3000; r.presG = 2.5f; r.essG = -2; r.airG = 2;
        r.gate = -46; r.cThr = -24; r.cRatio = 4.5f; r.cAtk = 4; r.cRel = 80; r.cMk = 6; r.satDrive = 8; r.satMix = 25;
        r.dDiv = d8; r.dFb = 12; r.dMix = 8; r.dDuck = 75; r.rType = Room; r.rDecay = 0.8f; r.rMix = 9; r.rDuck = 75; g.push_back (r);

        r = {}; r.genre = "Melodic Rap"; r.hpf = 120; r.lsG = -1; r.mudF = 400; r.mudG = -3; r.presF = 3500; r.presG = 3; r.airG = 3;
        r.tuneOn = true; r.tSpeed = 8; r.tAmount = 95; r.tHuman = 10; r.cThr = -24; r.cRatio = 4; r.cAtk = 3; r.cRel = 60; r.cMk = 6;
        r.satDrive = 6; r.dbl = 20; r.wide = 10; r.dDiv = d4; r.dFeel = Dotted; r.dFb = 32; r.dMix = 20; r.dDuck = 55; r.ping = true;
        r.rType = Plate; r.rDecay = 2.2f; r.rMix = 20; r.rDuck = 60; g.push_back (r);

        r = {}; r.genre = "Trap"; r.hpf = 130; r.lsG = -2; r.mudF = 420; r.mudG = -2.5f; r.presF = 3200; r.presG = 3; r.essG = -1; r.airG = 3.5f;
        r.tuneOn = true; r.tSpeed = 0; r.tAmount = 100; r.tHuman = 0; r.cThr = -26; r.cRatio = 5; r.cAtk = 3; r.cRel = 70; r.cMk = 7;
        r.satDrive = 10; r.satMix = 30; r.dbl = 20; r.wide = 20; r.dDiv = d8; r.dFeel = Triplet; r.dFb = 30; r.dMix = 16; r.dDuck = 55; r.ping = true;
        r.rType = Plate; r.rDecay = 1.6f; r.rMix = 15; r.rDuck = 60; g.push_back (r);

        r = {}; r.genre = "Drill"; r.hpf = 120; r.lsG = -1.5f; r.mudF = 380; r.mudG = -2; r.presF = 2200; r.presG = 3; r.essG = -2; r.airG = 1.5f;
        r.gate = -44; r.cThr = -26; r.cRatio = 6; r.cAtk = 2; r.cRel = 60; r.cMk = 8; r.satDrive = 12; r.satMix = 35;
        r.dDiv = d16; r.dFeel = Dotted; r.dFb = 18; r.dMix = 10; r.dDuck = 70; r.rType = Room; r.rDecay = 0.9f; r.rMix = 10; r.rDuck = 70; r.rDamp = 55; r.order = "GTCEMS"; g.push_back (r);

        r = {}; r.genre = "Trap-Soul"; r.hpf = 85; r.lsG = 1.5f; r.lsF = 150; r.mudF = 300; r.mudG = -2; r.presF = 2500; r.presG = 1.5f; r.presQ = 0.8f;
        r.essF = 6500; r.essG = -2; r.airF = 10000; r.airG = 1.5f; r.gate = -54; r.tuneOn = true; r.tSpeed = 15; r.tAmount = 85; r.tHuman = 30;
        r.cThr = -26; r.cRatio = 3; r.cAtk = 12; r.cRel = 150; r.satDrive = 4; r.wide = 20; r.dDiv = d4; r.dFb = 30; r.dMix = 18; r.dDuck = 60;
        r.dLc = 400; r.dHc = 4500; r.rType = Hall; r.rDecay = 3.4f; r.rMix = 26; r.rDuck = 65; r.rPre = 35; r.rDamp = 60; g.push_back (r);

        r = {}; r.genre = "R&B"; r.hpf = 85; r.lsG = 1; r.lsF = 180; r.mudF = 320; r.mudG = -2; r.presF = 2200; r.presG = 1.5f; r.essF = 6800;
        r.essG = -1.5f; r.airG = 3; r.gate = -54; r.tuneOn = true; r.tSpeed = 30; r.tAmount = 75; r.tHuman = 40; r.cThr = -22; r.cRatio = 3;
        r.cAtk = 10; r.cRel = 140; r.satDrive = 4; r.wide = 15; r.dDiv = d4; r.dFb = 30; r.dMix = 20; r.dDuck = 50; r.rType = Plate;
        r.rDecay = 2.6f; r.rMix = 26; r.rDuck = 60; r.rPre = 25; g.push_back (r);

        r = {}; r.genre = "Boom Bap"; r.hpf = 90; r.lsG = 0.5f; r.mudF = 300; r.mudG = -1.5f; r.presF = 1800; r.presG = 2.5f; r.essG = -1.5f;
        r.airF = 9000; r.airG = 0.5f; r.gate = -46; r.cThr = -22; r.cRatio = 4; r.cAtk = 6; r.cRel = 100; r.satDrive = 9; r.satMix = 35;
        r.dDiv = d8; r.dFb = 15; r.dMix = 10; r.dDuck = 70; r.rType = Room; r.rDecay = 1.0f; r.rMix = 12; r.rDuck = 60; r.rDamp = 60; r.order = "GTCEMS"; g.push_back (r);

        r = {}; r.genre = "Pop"; r.hpf = 100; r.lsG = -0.5f; r.mudF = 350; r.mudG = -2.5f; r.presF = 3500; r.presG = 2; r.essG = -2; r.airF = 12000;
        r.airG = 4.5f; r.tuneOn = true; r.tSpeed = 20; r.tAmount = 90; r.tHuman = 25; r.scale = 0; r.cThr = -24; r.cRatio = 4; r.cAtk = 5;
        r.cRel = 90; r.cMk = 6; r.satDrive = 4; r.dbl = 15; r.wide = 20; r.dDiv = d8; r.dFeel = Dotted; r.dFb = 25; r.dMix = 15; r.dDuck = 55;
        r.rType = Plate; r.rDecay = 2.0f; r.rMix = 20; r.rDuck = 55; r.rDamp = 25; g.push_back (r);

        r = {}; r.genre = "Afrobeats"; r.hpf = 110; r.lsG = -0.5f; r.mudF = 380; r.mudG = -2; r.presF = 2800; r.presG = 2.5f; r.airG = 3.5f;
        r.tuneOn = true; r.tSpeed = 5; r.tAmount = 100; r.tHuman = 10; r.scale = 0; r.cThr = -24; r.cRatio = 4; r.cAtk = 4; r.cRel = 80;
        r.satDrive = 5; r.dbl = 25; r.wide = 25; r.dDiv = d8; r.dFeel = Dotted; r.dFb = 35; r.dMix = 20; r.dDuck = 45; r.ping = true;
        r.rType = Plate; r.rDecay = 1.8f; r.rMix = 20; r.rDuck = 50; g.push_back (r);

        r = {}; r.genre = "Dancehall"; r.hpf = 120; r.lsG = -1; r.mudF = 400; r.mudG = -2; r.presF = 3200; r.presG = 3; r.airG = 3;
        r.tuneOn = true; r.tSpeed = 0; r.tAmount = 100; r.tHuman = 0; r.scale = 0; r.cThr = -26; r.cRatio = 5; r.cAtk = 3; r.cRel = 70; r.cMk = 7;
        r.satDrive = 10; r.satMix = 30; r.dbl = 20; r.wide = 20; r.dDiv = d8; r.dFeel = Dotted; r.dFb = 40; r.dMix = 22; r.dDuck = 45; r.ping = true;
        r.rType = Room; r.rDecay = 1.2f; r.rMix = 14; r.rDuck = 55; g.push_back (r);

        r = {}; r.genre = "Latin / Reggaeton"; r.hpf = 110; r.lsG = -1; r.mudF = 380; r.mudG = -2.5f; r.presF = 3000; r.presG = 2.5f; r.airG = 4;
        r.tuneOn = true; r.tSpeed = 3; r.tAmount = 100; r.tHuman = 5; r.scale = 1; r.cThr = -25; r.cRatio = 4.5f; r.cAtk = 3; r.cRel = 70; r.cMk = 7;
        r.satDrive = 6; r.dbl = 30; r.wide = 25; r.dDiv = d8; r.dFeel = Dotted; r.dFb = 30; r.dMix = 18; r.dDuck = 50; r.ping = true;
        r.rType = Plate; r.rDecay = 1.8f; r.rMix = 18; r.rDuck = 55; g.push_back (r);

        r = {}; r.genre = "Gospel"; r.hpf = 80; r.lsG = 1; r.mudF = 300; r.mudG = -1.5f; r.presF = 2500; r.presG = 2; r.airG = 3; r.gate = -56;
        r.tuneOn = true; r.tSpeed = 60; r.tAmount = 50; r.tHuman = 60; r.scale = 0; r.cThr = -20; r.cRatio = 3; r.cAtk = 12; r.cRel = 160;
        r.satDrive = 3; r.wide = 10; r.dDiv = d4; r.dFb = 20; r.dMix = 10; r.dDuck = 50; r.rType = Hall; r.rDecay = 3.2f; r.rMix = 28;
        r.rDuck = 45; r.rPre = 40; r.rDamp = 35; g.push_back (r);

        r = {}; r.genre = "Country"; r.hpf = 90; r.lsG = 0; r.mudF = 400; r.mudG = -2; r.presF = 2500; r.presG = 2; r.airF = 10000; r.airG = 2;
        r.tuneOn = true; r.tSpeed = 50; r.tAmount = 60; r.tHuman = 50; r.scale = 0; r.cThr = -22; r.cRatio = 3.5f; r.cAtk = 8; r.cRel = 120;
        r.satDrive = 5; r.dDiv = d8; r.dFb = 20; r.dMix = 12; r.dDuck = 50; r.rType = Plate; r.rDecay = 1.6f; r.rMix = 16; r.rDuck = 45; g.push_back (r);

        r = {}; r.genre = "Rock"; r.hpf = 120; r.lsG = -1; r.mudF = 450; r.mudG = -2; r.presF = 2000; r.presG = 3; r.essG = -1; r.airF = 9000; r.airG = 1.5f;
        r.gate = -44; r.cThr = -26; r.cRatio = 6; r.cAtk = 2; r.cRel = 70; r.cMk = 8; r.satDrive = 14; r.satMix = 40; r.dbl = 20;
        r.dDiv = d8; r.dFb = 20; r.dMix = 12; r.dDuck = 60; r.rType = Room; r.rDecay = 1.2f; r.rMix = 14; r.rDuck = 50; r.order = "GTCEMS"; g.push_back (r);

        r = {}; r.genre = "Indie / Alt"; r.hpf = 110; r.lsG = 0.5f; r.mudF = 350; r.mudG = -1; r.presF = 2000; r.presG = 1; r.airF = 9000; r.airG = 0;
        r.cThr = -22; r.cRatio = 3; r.cAtk = 10; r.cRel = 150; r.satDrive = 10; r.satMix = 30; r.dbl = 30; r.wide = 20; r.dDiv = d4;
        r.dFb = 35; r.dMix = 18; r.dDuck = 40; r.dHc = 4000; r.rType = Hall; r.rDecay = 2.8f; r.rMix = 24; r.rDuck = 40; r.rDamp = 55; g.push_back (r);

        r = {}; r.genre = "EDM"; r.hpf = 150; r.lsG = -2; r.mudF = 400; r.mudG = -3; r.presF = 3500; r.presG = 3; r.essG = -2; r.airF = 12000; r.airG = 5;
        r.tuneOn = true; r.tSpeed = 10; r.tAmount = 100; r.tHuman = 10; r.scale = 1; r.cThr = -28; r.cRatio = 6; r.cAtk = 3; r.cRel = 60; r.cMk = 9;
        r.satDrive = 6; r.dbl = 30; r.wide = 40; r.dDiv = d8; r.dFeel = Dotted; r.dFb = 40; r.dMix = 22; r.dDuck = 60; r.ping = true;
        r.rType = Hall; r.rDecay = 3.0f; r.rMix = 25; r.rDuck = 65; r.rDamp = 25; r.ceiling = -1.5f; g.push_back (r);

        r = {}; r.genre = "Lo-Fi"; r.hpf = 200; r.lsG = 1; r.mudF = 500; r.mudG = 1; r.presF = 1500; r.presG = 2; r.essG = -2; r.airF = 6500;
        r.hiCutTop = true; r.cThr = -22; r.cRatio = 4; r.cAtk = 10; r.cRel = 150; r.satDrive = 14; r.satMix = 45; r.wide = 10;
        r.dDiv = d4; r.dFb = 40; r.dMix = 20; r.dDuck = 40; r.dLc = 500; r.dHc = 2500; r.rType = Room; r.rDecay = 1.4f; r.rMix = 18;
        r.rDuck = 40; r.rDamp = 75; r.order = "GTSEMC"; g.push_back (r);

        r = {}; r.genre = "Podcast / Voice"; r.hpf = 80; r.lsG = 1; r.lsF = 150; r.mudF = 300; r.mudG = -2; r.presF = 3000; r.presG = 2; r.essG = -3;
        r.airF = 10000; r.airG = 1.5f; r.gate = -48; r.cThr = -24; r.cRatio = 4; r.cAtk = 8; r.cRel = 120; r.cMk = 6; r.satDrive = 2; r.satMix = 10;
        r.dMix = 0; r.rType = Room; r.rDecay = 0.4f; r.rMix = 4; r.rDuck = 80; r.ceiling = -1.0f; g.push_back (r);
        return g;
    }

    enum Variant { Lead, Hook, Adlibs, Backgrounds, Doubles, Clean, Dark, Bright, Spacey, HardTune, Vintage, Aggressive,
                   AdlibPhone, AdlibEcho, Harmonies, BgPad, HookStadium, Intro, NumVariants };
    const char* variantName[] = { "Lead", "Hook", "Ad-libs", "Backgrounds", "Doubles", "Lead (Clean)", "Lead (Dark)", "Lead (Bright)",
                                  "Lead (Spacey)", "Lead (Hard Tune)", "Lead (Vintage)", "Lead (Aggressive)",
                                  "Ad-libs (Phone Filter)", "Ad-libs (Big Echo)", "Harmonies (Wide)", "Backgrounds (Airy Pad)",
                                  "Hook (Stadium)", "Intro / Whisper" };

    Recipe applyVariant (Recipe r, int v)
    {
        switch (v)
        {
            case Hook:        r.presG += 1; r.airG += 1; r.dbl += 30; r.wide += 20; r.cRatio += 1; r.rMix += 5; r.dMix += 4; break;
            case Adlibs:      r.hpf = std::max (r.hpf, 250.0f); r.lsG -= 2; r.satDrive += 6; r.satMix += 15; r.wide += 40; r.dMix += 15; r.dFb += 15;
                              r.ping = true; r.rMix += 10; r.dDuck = std::max (0.0f, r.dDuck - 30); r.rDuck = std::max (0.0f, r.rDuck - 25);
                              if (r.tuneOn) r.tSpeed = 0; break;
            case Backgrounds: r.hpf *= 1.8f; r.presG -= 1; r.airG += 1.5f; r.dbl = 55; r.wide = 65; r.cRatio += 1.5f; r.rMix += 15; r.dMix += 5;
                              r.dDuck = 20; r.rDuck = 20; break;
            case Doubles:     r.hpf *= 1.5f; r.presG -= 1.5f; r.essG -= 1.5f; r.dbl = 0; r.wide = 30; r.satMix *= 0.5f; r.rMix *= 0.6f; r.dMix *= 0.4f; break;
            case Clean:       r.satMix = 0; r.dbl = 0; r.wide = 0; r.dMix *= 0.6f; r.rMix *= 0.7f; break;
            case Dark:        r.airG = r.hiCutTop ? r.airG : -2.5f; r.presG -= 1; r.lsG += 1; r.rDamp += 25; r.dHc = 3500; break;
            case Bright:      r.airG += 3; r.presG += 1.5f; r.mudG -= 1; r.essG -= 1; r.rDamp = std::max (0.0f, r.rDamp - 20); break;
            case Spacey:      r.rMix += 15; r.rDecay *= 1.6f; r.dMix += 12; r.dFb += 15; r.dDuck += 15; r.rDuck += 15; r.wide += 20; r.ping = true; break;
            case HardTune:    r.tuneOn = true; r.tSpeed = 0; r.tAmount = 100; r.tHuman = 0; r.dbl += 15; r.presG += 1; r.airG += 1; r.satDrive += 3; break;
            case Vintage:     r.hiCutTop = true; r.airF = 9000; r.hpf = std::max (r.hpf, 150.0f); r.satDrive += 8; r.satMix += 20; r.rType = Room;
                              r.rDamp += 25; r.dHc = 3000; r.order = "GTSEMC"; break;
            case Aggressive:  r.cRatio += 2; r.cThr -= 4; r.cAtk = std::max (0.5f, r.cAtk * 0.5f); r.cMk += 2; r.satDrive += 10; r.satMix += 15;
                              r.presG += 2; r.rMix *= 0.5f; r.dMix *= 0.7f; r.order = "GTCEMS"; break;
            // ---- role presets: each keeps its genre's character and adds the role's treatment
            case AdlibPhone:  r.hpf = 600; r.hiCutTop = true; r.airF = 3600; r.presF = 1700; r.presG += 3; r.mudG = 0; r.lsG = 0;
                              r.satDrive += 8; r.satMix = std::max (r.satMix, 35.0f) + 10; r.cRatio += 2; r.wide = std::max (r.wide, 30.0f);
                              r.dDiv = d8; r.dFeel = r.dFeel; r.dMix = 18; r.dFb = 30; r.ping = true; r.dDuck = 20; r.rType = Room; r.rDecay = 0.8f; r.rMix = 10;
                              if (r.tuneOn) r.tSpeed = std::min (r.tSpeed, 5.0f); r.order = "GTSEMC"; break;
            case AdlibEcho:   r.hpf = std::max (r.hpf, 220.0f); r.lsG -= 2; r.dDiv = d4; r.dFeel = Dotted; r.dFb = 55; r.dMix = 36; r.dDuck = 0; r.ping = true;
                              r.dLc = 450; r.dHc = 4200; r.rType = Hall; r.rDecay = std::max (r.rDecay, 2.5f); r.rMix += 12; r.rDuck = 15;
                              r.wide = std::max (r.wide, 45.0f); r.satDrive += 4; r.satMix += 10; if (r.tuneOn) r.tSpeed = 0; break;
            case Harmonies:   r.hpf *= 1.7f; r.lsG -= 2; r.presG -= 1; r.airG += 2; r.dbl = 70; r.wide = 70; r.cRatio += 1.5f; r.cThr -= 3;
                              r.tuneOn = true; r.tSpeed = std::min (r.tSpeed, 10.0f); r.tAmount = 100; r.tHuman = std::min (r.tHuman, 15.0f);
                              r.rMix += 10; r.dMix *= 0.6f; r.dDuck = 20; r.rDuck = 20; break;
            case BgPad:       r.hpf = std::max (r.hpf * 2.2f, 300.0f); r.lsG -= 3; r.mudG -= 1; r.presG -= 1.5f; r.airG += 4; r.dbl = 50; r.wide = 85;
                              r.cRatio += 2; r.cThr -= 4; r.tuneOn = true; r.tSpeed = std::min (r.tSpeed, 8.0f); r.tAmount = 100;
                              r.rType = Hall; r.rDecay = std::max (r.rDecay * 1.5f, 3.2f); r.rMix += 25; r.rDuck = 10; r.rPre = 40; r.dMix *= 0.5f; r.dDuck = 10; break;
            case HookStadium: r.presG += 1.5f; r.airG += 1.5f; r.dbl = std::max (r.dbl, 55.0f); r.wide = std::max (r.wide, 50.0f); r.cRatio += 1.5f; r.cMk += 1;
                              r.dDiv = d4; r.dFeel = Dotted; r.dMix += 8; r.dFb += 10; r.ping = true; r.rType = Hall; r.rDecay = std::max (r.rDecay, 2.6f);
                              r.rMix += 8; r.rDuck = std::max (r.rDuck, 60.0f); r.ceiling = -1.5f; break;
            case Intro:       r.hpf = std::max (r.hpf, 150.0f); r.lsG -= 2; r.airG += 5; r.essG -= 2; r.cRatio += 3; r.cThr -= 8; r.cMk += 4;
                              r.gate = -60; r.tuneOn = true; r.tSpeed = std::max (r.tSpeed, 35.0f); r.tAmount = 70; r.tHuman = 50;
                              r.rType = Plate; r.rMix += 14; r.rDecay = std::max (r.rDecay, 2.2f); r.rDuck = 30; r.dMix += 6; r.dDuck = 30; r.satMix = 0; break;
            default: break;
        }
        return r;
    }

    std::vector<PresetInfo> creativeFx()
    {
        std::vector<PresetInfo> v; Recipe r;
        auto add = [&] (const String& n) { r.genre = "FX & Creative"; v.push_back (fromRecipe (n, r)); r = {}; };
        r = {}; r.hpf = 450; r.hiCutTop = true; r.airF = 3200; r.presF = 1500; r.presG = 6; r.mudG = 0; r.satDrive = 16; r.satMix = 60; r.dMix = 0; r.rMix = 0; add ("Telephone");
        r.hpf = 300; r.hiCutTop = true; r.airF = 5000; r.presF = 2000; r.presG = 5; r.satDrive = 22; r.satMix = 80; r.cRatio = 8; r.dMix = 0; r.rType = Room; r.rDecay = 0.5f; r.rMix = 8; add ("Megaphone");
        r.hpf = 250; r.hiCutTop = true; r.airF = 6500; r.satDrive = 10; r.satMix = 40; r.rType = Room; r.rDecay = 0.6f; r.rMix = 10; r.dMix = 0; add ("AM Radio");
        r.hiCutTop = true; r.airF = 900; r.lsG = 3; r.wide = 40; r.rType = Hall; r.rDecay = 4.5f; r.rMix = 45; r.rDuck = 10; r.dMix = 0; add ("Underwater");
        r.tuneOn = true; r.tSpeed = 0; r.tAmount = 100; r.tHuman = 0; r.scale = 6; r.satDrive = 8; r.dbl = 30; r.dMix = 15; add ("Robot (Chromatic Hard Tune)");
        r.hpf = 200; r.lsG = -4; r.airG = 7; r.essG = 0; r.cRatio = 8; r.cThr = -32; r.cMk = 10; r.wide = 30; r.rMix = 25; add ("Whisper Air");
        r.rType = Hall; r.rDecay = 6.0f; r.rMix = 40; r.rDuck = 70; r.rPre = 80; r.dDiv = d4; r.dFeel = Dotted; r.dFb = 55; r.dMix = 30; r.dDuck = 70; r.wide = 50; add ("Stadium Throw");
        r.dDiv = d8; r.dFb = 75; r.dMix = 45; r.dDuck = 0; r.ping = true; r.dLc = 600; r.dHc = 2500; r.rMix = 10; add ("Echo Out (Dub)");
        r.satDrive = 24; r.satMix = 90; r.cRatio = 10; r.cThr = -30; r.presG = 4; r.hpf = 200; r.dMix = 8; r.rMix = 8; add ("Distorted Scream");
        r.dbl = 100; r.wide = 100; r.tuneOn = true; r.tSpeed = 5; r.rType = Hall; r.rDecay = 3.5f; r.rMix = 35; r.hpf = 250; add ("Choir Stack");
        r.rType = Hall; r.rDecay = 10.0f; r.rMix = 70; r.rDuck = 0; r.rPre = 0; r.rDamp = 20; r.dMix = 0; r.hpf = 300; add ("Infinite Wash");
        r.hpf = 180; r.hiCutTop = true; r.airF = 7000; r.satDrive = 12; r.satMix = 50; r.dDiv = d4; r.dFb = 50; r.dMix = 30; r.dHc = 2200; r.rType = Room; r.rDamp = 80; add ("Vinyl Tape Echo");
        return v;
    }
}

std::vector<PresetInfo> makeGeneratedPresets()
{
    std::vector<PresetInfo> v;
    for (const auto& base : genreRecipes())
        for (int k = 0; k < NumVariants; ++k)
        {
            if (base.genre == "Podcast / Voice" && ! (k == Lead || k == Clean || k == Dark || k == Bright || k == Vintage || k == Aggressive || k == Intro)) continue;
            v.push_back (fromRecipe (base.genre + " - " + variantName[k], applyVariant (base, k)));
        }
    for (auto& p : creativeFx()) v.push_back (p);
    return v;
}

//==============================================================================
// Artist-inspired starting points (UNOFFICIAL): every artist is tagged with the genres they fit, and gets a
// style profile that is applied on top of their main genre's recipe.
namespace
{
    struct Style
    {
        int tune;     // 0 off, 1 natural, 2 polished, 3 hard
        int bright;   // -2 dark .. +2 airy
        int body;     // -1 thin .. +2 warm/full
        int grit;     // 0 clean .. 3 gritty
        int comp;     // 1 light .. 3 heavy
        int space;    // 0 dry .. 4 huge
        int echo;     // 0 none .. 3 lots
        int width;    // 0 mono .. 3 wide stacks
        int div, feel;
        const char* order;
    };
    struct ArtistSpec { const char* name; std::vector<const char*> genres; Style s; };

    // chain orders
    #define DEF   "GTEMCS"   // EQ -> Match -> Comp -> Saturation (classic)
    #define PUNCH "GTCEMS"   // Comp first, then EQ shapes the compressed tone (punchy rap)
    #define VINT  "GTSEMC"   // Saturation first, EQ, then glue compression (vintage / lo-fi)
    #define SMOOTH "GTEMSC"  // Saturation before the compressor smooths the grit (R&B / trap-soul)

    const std::vector<ArtistSpec>& specs()
    {
        static const std::vector<ArtistSpec> v = {
            // ---- Rap / Trap (main genres as tagged)
            { "Drake",            { "Rap", "Trap", "Melodic Rap" }, { 2, -1,  1, 1, 2, 1, 1, 0, d8,  Straight, DEF } },
            { "J. Cole",          { "Rap", "Boom Bap" },            { 0,  0,  1, 2, 2, 0, 0, 0, d8,  Straight, PUNCH } },
            { "Rylo Rodriguez",   { "Rap", "Trap", "Melodic Rap" }, { 2,  0,  0, 2, 3, 0, 1, 0, d8,  Straight, PUNCH } },
            { "Hunxho",           { "Trap", "Rap", "Melodic Rap" }, { 3,  1,  0, 1, 3, 1, 1, 1, d8,  Triplet,  DEF } },
            { "Kendrick Lamar",   { "Rap", "Boom Bap" },            { 0,  1,  0, 2, 3, 0, 0, 1, d16, Straight, PUNCH } },
            { "Lil Baby",         { "Trap", "Rap" },                { 2,  1, -1, 1, 3, 0, 1, 0, d8,  Straight, PUNCH } },
            { "Future",           { "Trap", "Melodic Rap" },        { 3, -1,  0, 2, 3, 2, 2, 1, d4,  Dotted,   DEF } },
            { "Travis Scott",     { "Trap", "Melodic Rap" },        { 3, -1,  0, 3, 2, 4, 3, 3, d4,  Dotted,   SMOOTH } },
            { "21 Savage",        { "Trap", "Rap" },                { 0, -2,  1, 1, 2, 1, 1, 0, d4,  Straight, DEF } },
            { "Lil Durk",         { "Trap", "Melodic Rap", "Drill" }, { 3, 0,  0, 2, 3, 1, 1, 1, d8,  Dotted,   DEF } },
            { "Gunna",            { "Trap", "Melodic Rap" },        { 3,  1, -1, 0, 2, 2, 2, 1, d8,  Triplet,  DEF } },
            { "Young Thug",       { "Trap", "Melodic Rap" },        { 3,  2, -1, 2, 2, 2, 2, 2, d8,  Triplet,  DEF } },
            { "Lil Yachty",       { "Trap" },                       { 3,  2,  0, 3, 3, 1, 2, 1, d8,  Triplet,  DEF } },
            { "NBA YoungBoy",     { "Rap", "Trap" },                { 2,  0,  0, 3, 3, 1, 1, 0, d8,  Straight, PUNCH } },
            { "Moneybagg Yo",     { "Trap", "Rap" },                { 1, -1,  1, 1, 2, 0, 1, 0, d8,  Straight, PUNCH } },
            { "Lil Wayne",        { "Rap", "Trap" },                { 2,  1,  0, 2, 2, 1, 1, 1, d8,  Dotted,   DEF } },
            { "Don Toliver",      { "Melodic Rap", "Trap" },        { 3,  0,  0, 1, 2, 4, 3, 2, d4,  Dotted,   SMOOTH } },
            { "Rod Wave",         { "Melodic Rap", "Trap-Soul", "Rap" }, { 2, -1, 2, 1, 2, 2, 1, 1, d4, Straight, SMOOTH } },
            { "Polo G",           { "Melodic Rap", "Rap" },         { 2,  0,  1, 1, 2, 1, 1, 0, d8,  Straight, DEF } },
            { "Post Malone",      { "Pop", "Melodic Rap" },         { 2,  1,  1, 1, 2, 2, 1, 2, d8,  Dotted,   DEF } },
            // ---- Drill
            { "Pop Smoke",        { "Drill", "Rap" },               { 0, -2,  2, 3, 3, 1, 1, 0, d16, Dotted,   PUNCH } },
            { "Fivio Foreign",    { "Drill", "Rap" },               { 0,  0,  1, 2, 3, 0, 1, 0, d16, Straight, PUNCH } },
            { "Central Cee",      { "Drill", "Rap" },               { 1,  1,  0, 1, 2, 0, 0, 0, d16, Straight, PUNCH } },
            { "Kay Flock",        { "Drill" },                      { 0,  0,  0, 3, 3, 0, 1, 0, d16, Dotted,   PUNCH } },
            { "Chief Keef",       { "Drill", "Trap" },              { 3,  0,  0, 2, 3, 2, 2, 1, d8,  Straight, DEF } },
            { "King Von",         { "Drill", "Rap" },               { 0, -1,  1, 2, 3, 0, 0, 0, d16, Straight, PUNCH } },
            // ---- R&B / Trap-Soul
            { "Bryson Tiller",    { "R&B", "Trap-Soul" },           { 2, -1,  1, 0, 2, 3, 1, 1, d4,  Straight, SMOOTH } },
            { "Tory Lanez",       { "R&B", "Melodic Rap" },         { 3,  2, -1, 1, 3, 2, 1, 2, d4,  Straight, DEF } },
            { "Chris Brown",      { "R&B", "Pop" },                 { 2,  2, -1, 0, 3, 2, 1, 2, d8,  Straight, DEF } },
            { "SZA",              { "R&B", "Trap-Soul" },           { 1,  1,  1, 1, 2, 3, 2, 2, d4,  Dotted,   SMOOTH } },
            { "Summer Walker",    { "R&B", "Trap-Soul" },           { 1,  0,  1, 0, 2, 3, 1, 1, d4,  Straight, SMOOTH } },
            { "PARTYNEXTDOOR",    { "R&B", "Trap-Soul" },           { 2, -2,  1, 2, 2, 4, 2, 2, d4,  Dotted,   SMOOTH } },
            { "Trey Songz",       { "R&B" },                        { 1,  2,  0, 0, 2, 3, 1, 1, d8,  Dotted,   DEF } },
            { "Blxst",            { "R&B", "Melodic Rap" },         { 1,  0,  1, 1, 2, 1, 1, 0, d8,  Straight, DEF } },
            { "The Weeknd",       { "R&B", "Pop" },                 { 2,  1,  0, 1, 2, 4, 2, 2, d4,  Dotted,   DEF } },
            { "Brent Faiyaz",     { "R&B", "Trap-Soul" },           { 1, -1,  1, 2, 2, 2, 1, 1, d4,  Straight, VINT } },
            { "Usher",            { "R&B", "Pop" },                 { 1,  2,  0, 0, 2, 2, 1, 1, d8,  Straight, DEF } },
            { "H.E.R.",           { "R&B" },                        { 0,  1,  1, 1, 1, 2, 1, 0, d4,  Straight, DEF } },
            { "Jhene Aiko",       { "R&B", "Trap-Soul" },           { 1,  2, -1, 0, 1, 4, 2, 2, d4,  Dotted,   SMOOTH } },
            { "6LACK",            { "Trap-Soul", "R&B" },           { 2, -2,  1, 1, 2, 3, 2, 1, d4,  Straight, SMOOTH } },
            { "Giveon",           { "R&B" },                        { 0, -1,  2, 0, 2, 3, 1, 0, d4,  Straight, DEF } },
            { "Kehlani",          { "R&B", "Pop" },                 { 1,  1,  0, 0, 2, 2, 1, 2, d8,  Dotted,   DEF } },
            // ---- Pop
            { "Ariana Grande",    { "Pop", "R&B" },                 { 2,  2, -1, 0, 3, 2, 1, 3, d8,  Dotted,   DEF } },
            { "Taylor Swift",     { "Pop", "Country" },             { 1,  1,  0, 0, 2, 2, 1, 1, d8,  Straight, DEF } },
            { "Billie Eilish",    { "Pop", "Indie / Alt" },         { 1, -1,  2, 0, 2, 1, 0, 0, d4,  Straight, DEF } },
            { "Dua Lipa",         { "Pop", "EDM" },                 { 2,  2,  0, 1, 3, 2, 1, 2, d8,  Dotted,   DEF } },
            { "Justin Bieber",    { "Pop", "R&B" },                 { 2,  1,  0, 0, 2, 2, 1, 2, d8,  Dotted,   DEF } },
            // ---- Afrobeats
            { "Burna Boy",        { "Afrobeats", "Dancehall" },     { 2,  0,  1, 1, 2, 2, 2, 1, d8,  Dotted,   DEF } },
            { "Wizkid",           { "Afrobeats" },                  { 2,  1,  0, 0, 2, 2, 2, 2, d8,  Dotted,   SMOOTH } },
            { "Rema",             { "Afrobeats", "Pop" },           { 3,  2,  0, 1, 2, 2, 2, 2, d8,  Triplet,  DEF } },
            { "Tems",             { "Afrobeats", "R&B" },           { 1, -1,  2, 1, 2, 2, 1, 1, d4,  Straight, SMOOTH } },
            { "Asake",            { "Afrobeats" },                  { 3,  1,  0, 1, 3, 2, 2, 3, d8,  Dotted,   DEF } },
            { "Davido",           { "Afrobeats", "Pop" },           { 3,  1,  0, 1, 3, 1, 2, 2, d8,  Dotted,   DEF } },
            // ---- Dancehall
            { "Vybz Kartel",      { "Dancehall" },                  { 2,  1, -1, 2, 3, 1, 2, 1, d8,  Dotted,   PUNCH } },
            { "Popcaan",          { "Dancehall" },                  { 3,  1,  0, 1, 2, 2, 2, 1, d8,  Dotted,   DEF } },
            { "Shenseea",         { "Dancehall", "Pop" },           { 2,  2, -1, 1, 3, 1, 2, 2, d8,  Dotted,   DEF } },
            { "Skillibeng",       { "Dancehall", "Drill" },         { 2,  0,  0, 3, 3, 1, 2, 0, d16, Dotted,   PUNCH } },
            // ---- Latin / Reggaeton
            { "Bad Bunny",        { "Latin / Reggaeton", "Trap" },  { 3, -1,  1, 2, 3, 2, 2, 1, d8,  Dotted,   DEF } },
            { "Rauw Alejandro",   { "Latin / Reggaeton", "R&B" },   { 3,  2,  0, 0, 3, 2, 2, 2, d8,  Dotted,   DEF } },
            { "Karol G",          { "Latin / Reggaeton", "Pop" },   { 3,  2, -1, 1, 3, 2, 1, 2, d8,  Dotted,   DEF } },
            { "Feid",             { "Latin / Reggaeton" },          { 3,  0,  1, 1, 2, 3, 2, 2, d4,  Dotted,   SMOOTH } },
            { "J Balvin",         { "Latin / Reggaeton" },          { 3,  1,  0, 1, 3, 1, 2, 2, d8,  Dotted,   DEF } },
            // ---- Gospel
            { "Kirk Franklin",    { "Gospel" },                     { 0,  1,  1, 1, 2, 2, 0, 1, d4,  Straight, DEF } },
            { "Maverick City Music", { "Gospel" },                  { 0,  1,  1, 0, 2, 4, 1, 3, d4,  Straight, DEF } },
            { "Tasha Cobbs Leonard", { "Gospel" },                  { 1,  0,  2, 0, 2, 3, 0, 1, d4,  Straight, DEF } },
            { "CeCe Winans",      { "Gospel" },                     { 1,  1,  1, 0, 1, 3, 0, 1, d4,  Straight, DEF } },
            // ---- Country
            { "Morgan Wallen",    { "Country" },                    { 1,  0,  1, 2, 2, 1, 1, 0, d8,  Straight, DEF } },
            { "Luke Combs",       { "Country" },                    { 1,  0,  2, 2, 2, 1, 1, 0, d8,  Straight, PUNCH } },
            { "Chris Stapleton",  { "Country", "Rock" },            { 0, -1,  2, 3, 2, 1, 0, 0, d8,  Straight, VINT } },
            { "Zach Bryan",       { "Country", "Indie / Alt" },     { 0, -1,  1, 1, 1, 1, 0, 0, d4,  Straight, VINT } },
            // ---- Rock
            { "Linkin Park",      { "Rock" },                       { 1,  1,  0, 3, 3, 1, 1, 2, d8,  Straight, PUNCH } },
            { "Paramore",         { "Rock", "Pop" },                { 1,  2,  0, 2, 3, 1, 1, 1, d8,  Straight, PUNCH } },
            { "Imagine Dragons",  { "Rock", "Pop" },                { 1,  1,  1, 2, 3, 3, 1, 2, d8,  Dotted,   PUNCH } },
            { "Foo Fighters",     { "Rock" },                       { 0,  1,  1, 3, 3, 1, 0, 2, d8,  Straight, PUNCH } },
            // ---- Indie / Alt
            { "Frank Ocean",      { "Indie / Alt", "R&B" },         { 1,  0,  1, 1, 1, 2, 1, 1, d4,  Straight, VINT } },
            { "Steve Lacy",       { "Indie / Alt", "Lo-Fi", "R&B" }, { 0, -1, 1, 2, 2, 1, 1, 1, d8,  Straight, VINT } },
            { "Tame Impala",      { "Indie / Alt" },                { 0,  0,  0, 2, 2, 3, 3, 2, d4,  Dotted,   VINT } },
            { "Lana Del Rey",     { "Indie / Alt", "Pop" },         { 0, -2,  2, 1, 2, 4, 2, 1, d4,  Straight, VINT } },
            { "Daniel Caesar",    { "Indie / Alt", "R&B" },         { 0,  0,  2, 0, 1, 2, 1, 1, d4,  Straight, DEF } },
            // ---- EDM
            { "Calvin Harris",    { "EDM", "Pop" },                 { 2,  2,  0, 1, 3, 2, 2, 2, d8,  Dotted,   DEF } },
            { "Fred again..",     { "EDM" },                        { 1,  0,  0, 2, 3, 3, 2, 2, d4,  Dotted,   VINT } },
            { "Kygo",             { "EDM", "Pop" },                 { 2,  2,  0, 0, 2, 3, 2, 2, d4,  Dotted,   DEF } },
            { "Swedish House Mafia", { "EDM" },                     { 2,  2, -1, 1, 3, 3, 2, 3, d8,  Dotted,   DEF } },
            // ---- Lo-Fi
            { "Joji",             { "Lo-Fi", "Indie / Alt" },       { 1, -2,  1, 2, 2, 3, 2, 1, d4,  Straight, VINT } },
            { "Mac Miller",       { "Lo-Fi", "Rap" },               { 0, -1,  1, 2, 2, 1, 1, 0, d8,  Straight, VINT } },
            { "Nujabes Style",    { "Lo-Fi", "Boom Bap" },          { 0, -2,  1, 3, 2, 2, 1, 0, d8,  Straight, VINT } },
            // ---- Boom Bap
            { "Nas",              { "Boom Bap", "Rap" },            { 0,  0,  1, 2, 2, 0, 0, 0, d8,  Straight, PUNCH } },
            { "Joey Bada$$",      { "Boom Bap", "Rap" },            { 0, -1,  1, 2, 2, 1, 0, 0, d8,  Straight, VINT } },
            { "MF DOOM",          { "Boom Bap", "Lo-Fi" },          { 0, -2,  2, 3, 3, 0, 0, 0, d8,  Straight, VINT } },
        };
        return v;
    }
    #undef DEF
    #undef PUNCH
    #undef VINT
    #undef SMOOTH

    Recipe applyStyle (Recipe r, const Style& s, const String& name)
    {
        switch (s.tune)
        {
            case 0: r.tuneOn = false; break;
            case 1: r.tuneOn = true; r.tSpeed = 45; r.tAmount = 65; r.tHuman = 50; break;
            case 2: r.tuneOn = true; r.tSpeed = 18; r.tAmount = 88; r.tHuman = 25; break;
            default: r.tuneOn = true; r.tSpeed = 0; r.tAmount = 100; r.tHuman = 0; break;
        }
        r.airG += 1.6f * (float) s.bright; r.presG += 0.8f * (float) s.bright; r.rDamp = jlimit (0.0f, 95.0f, r.rDamp - 12.0f * (float) s.bright);
        r.dHc = jlimit (1500.0f, 12000.0f, r.dHc + 900.0f * (float) s.bright);
        r.lsG += 1.2f * (float) s.body; r.hpf = std::max (60.0f, r.hpf - 12.0f * (float) s.body); r.mudG += 0.4f * (float) s.body;
        r.satDrive = 3.0f + 4.0f * (float) s.grit; r.satMix = s.grit == 0 ? 0.0f : 8.0f + 12.0f * (float) s.grit;
        r.cRatio = 2.5f + 1.3f * (float) s.comp; r.cThr = -18.0f - 3.5f * (float) s.comp; r.cAtk = std::max (1.0f, 14.0f - 4.0f * (float) s.comp);
        r.cMk = 3.0f + 1.5f * (float) s.comp;
        static const float rMix[5] = { 7, 13, 20, 27, 36 }, rDec[5] = { 0.7f, 1.2f, 2.0f, 3.0f, 4.2f }, rDuck[5] = { 75, 70, 62, 55, 45 };
        r.rMix = rMix[s.space]; r.rDecay = rDec[s.space]; r.rDuck = rDuck[s.space]; r.rPre = 10.0f + 8.0f * (float) s.space;
        r.rType = s.space <= 1 ? Room : (s.space == 2 ? Plate : Hall);
        static const float dMix[4] = { 5, 12, 20, 32 }, dFb[4] = { 12, 25, 35, 50 };
        r.dMix = dMix[s.echo]; r.dFb = dFb[s.echo]; r.dDiv = s.div; r.dFeel = s.feel; r.ping = s.echo >= 2;
        static const float w[4] = { 0, 15, 35, 60 };
        r.dbl = w[s.width]; r.wide = w[s.width];
        r.order = s.order;
        // a small per-artist signature so two artists with the same profile never share identical settings
        int h = 0; for (auto c : name) h = (h * 31 + (int) c) & 0xffff;
        r.presF = jlimit (1500.0f, 5000.0f, r.presF + (float) ((h % 9) - 4) * 120.0f);
        r.essF  = jlimit (5000.0f, 9000.0f, r.essF + (float) ((h / 9 % 7) - 3) * 150.0f);
        return r;
    }
}

namespace PresetTags
{
    const std::vector<ArtistDef>& artistRoster()
    {
        static const std::vector<ArtistDef> roster = []
        {
            std::vector<ArtistDef> r;
            for (auto& a : specs()) { ArtistDef d; d.name = a.name; for (auto* g : a.genres) d.genres.add (g); r.push_back (d); }
            return r;
        }();
        return roster;
    }
    StringArray artistsForGenre (const String& genre)
    {
        StringArray out;
        for (auto& a : artistRoster()) if (genre.isEmpty() || a.genres.contains (genre)) out.add (a.name);
        out.sortNatural();
        return out;
    }
    bool artistInGenre (const String& artist, const String& genre)
    {
        for (auto& a : artistRoster()) if (a.name == artist) return a.genres.contains (genre);
        return false;
    }
}

static std::vector<PresetInfo> makeArtistPresets()
{
    std::vector<PresetInfo> v;
    const auto bases = genreRecipes();
    for (auto& a : specs())
    {
        Recipe base;
        for (auto& b : bases) if (b.genre == a.genres.front()) base = b;
        PresetInfo p = fromRecipe (unofficial (a.name), applyStyle (base, a.s, a.name));
        p.genre = a.genres.front(); p.artist = a.name;
        v.push_back (p);
    }
    return v;
}

std::vector<PresetInfo> makeFactoryPresets()
{
    std::vector<PresetInfo> v;

    // ------------------------------------------------------------------ genre presets
    v.push_back (Builder ("R&B Vocal - Polished", "R&B")
        .gate (-52).tune (true, 30, 75, 40)
        .band (0, LoCut, 90, 0, 0.71f).band (1, LoShelf, 180, 1.0f, 0.71f).band (2, Bell, 320, -2.0f, 1.4f)
        .band (3, Bell, 2200, 1.5f, 0.9f).band (4, Bell, 6800, -1.5f, 3.5f).band (5, HiShelf, 11000, 3.0f, 0.71f)
        .comp (-22, 3.0f, 10, 140, 5).sat (5, 20).dbl (0).wide (15)
        .delay (d4, Straight, 35, 25, 45, false, 300, 5500).reverb (Plate, 2.6f, 28, 60, 25, 40).limit (-1));

    v.push_back (Builder ("Dry Up-Front Rap", "Rap")
        .gate (-46, -40, 40, 120).tune (false)
        .band (0, LoCut, 100, 0, 0.71f).band (1, LoShelf, 180, -1.5f, 0.71f).band (2, Bell, 350, -2.5f, 1.2f)
        .band (3, Bell, 3000, 2.5f, 1.0f).band (4, Bell, 7500, -2.0f, 4.0f).band (5, HiShelf, 11000, 2.0f, 0.71f)
        .comp (-24, 4.5f, 4, 80, 6).sat (8, 25).dbl (0).wide (0)
        .delay (d8, Straight, 12, 7, 75).reverb (Room, 0.7f, 8, 75, 5, 50).limit (-1));

    v.push_back (Builder ("Melodic Trap Lead", "Melodic Rap")
        .gate (-50).tune (true, 0, 100, 0)
        .band (0, LoCut, 120, 0, 0.71f).band (1, LoShelf, 200, -1.0f, 0.71f).band (2, Bell, 400, -3.0f, 1.2f)
        .band (3, Bell, 3500, 3.0f, 1.0f).band (4, Bell, 7000, -1.5f, 4.0f).band (5, HiShelf, 12000, 3.0f, 0.71f)
        .comp (-24, 4, 3, 60, 6).sat (6, 20).dbl (25).wide (10)
        .delay (d4, Dotted, 35, 22, 55, true, 350, 5000).reverb (Plate, 2.2f, 22, 60, 20, 35).limit (-1));

    v.push_back (Builder ("Intimate Trap-Soul", "Trap-Soul")
        .gate (-54, -30, 80, 200).tune (true, 15, 85, 30)
        .band (0, LoCut, 85, 0, 0.71f).band (1, LoShelf, 150, 1.5f, 0.71f).band (2, Bell, 300, -2.0f, 1.2f)
        .band (3, Bell, 2500, 1.5f, 0.8f).band (4, Bell, 6500, -2.0f, 4.0f).band (5, HiShelf, 10000, 1.5f, 0.71f)
        .comp (-26, 3, 12, 150, 5).sat (4, 20).dbl (0).wide (20)
        .delay (d4, Straight, 30, 18, 60, false, 400, 4500).reverb (Hall, 3.4f, 26, 65, 35, 60).limit (-1));

    v.push_back (Builder ("Smooth R&B Lead", "R&B")
        .gate (-55, -25, 100, 250).tune (true, 40, 65, 55)
        .band (0, LoCut, 80, 0, 0.71f).band (1, LoShelf, 220, 1.0f, 0.71f).band (2, Bell, 450, -1.5f, 1.0f)
        .band (3, Bell, 1800, 1.0f, 0.7f).band (4, Bell, 6000, -1.5f, 3.0f).band (5, HiShelf, 9000, 2.0f, 0.71f)
        .comp (-20, 2.5f, 15, 180, 4).sat (3, 15).dbl (0).wide (10)
        .delay (d8, Dotted, 25, 15, 50, false, 300, 5000).reverb (Hall, 2.8f, 30, 55, 30, 50).limit (-1));

    v.push_back (Builder ("Wide Hook Stack", "Melodic Rap")
        .gate (-50).tune (true, 5, 100, 10)
        .band (0, LoCut, 150, 0, 0.71f).band (1, LoShelf, 220, -2.0f, 0.71f).band (2, Bell, 380, -2.5f, 1.0f)
        .band (3, Bell, 3200, 2.0f, 0.9f).band (4, Bell, 7500, -1.5f, 4.0f).band (5, HiShelf, 12000, 3.5f, 0.71f)
        .comp (-28, 5, 3, 70, 8).sat (6, 25).dbl (65).wide (55)
        .delay (d8, Dotted, 25, 15, 45, true).reverb (Hall, 2.4f, 25, 50, 20, 40).limit (-1.5f));

    v.push_back (Builder ("Atmospheric Ad-libs", "Trap")
        .gate (-48, -50, 30, 100).tune (true, 0, 100, 0)
        .band (0, LoCut, 300, 0, 0.71f).band (1, LoShelf, 250, -3.0f, 0.71f).band (2, Bell, 500, -2.0f, 1.0f)
        .band (3, Bell, 2500, 2.0f, 1.0f).band (4, Bell, 7000, -2.0f, 3.0f).band (5, HiCut, 7500, 0, 0.71f)
        .comp (-26, 4, 5, 90, 6).sat (12, 40).dbl (30).wide (70)
        .delay (d4, Dotted, 55, 40, 20, true, 500, 4000).reverb (Hall, 4.5f, 40, 30, 40, 55).limit (-1.5f));

    for (auto& p : makeGeneratedPresets()) v.push_back (p);
    for (auto& p : makeArtistPresets()) v.push_back (p);
    return v;
}
