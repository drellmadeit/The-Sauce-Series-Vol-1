#include "Parameters.h"

using namespace juce;
using APF = AudioParameterFloat; using APB = AudioParameterBool; using APC = AudioParameterChoice;

namespace
{
    NormalisableRange<float> skewed (float lo, float hi, float centre, float interval = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, interval); r.setSkewForCentre (centre); return r;
    }
    AudioParameterFloatAttributes unit (const char* u, int decimals = 1)
    {
        return AudioParameterFloatAttributes().withLabel (u).withStringFromValueFunction (
            [decimals] (float v, int) { return String (v, decimals); });
    }
    AudioParameterFloatAttributes hzUnit()
    {
        return AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
            [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " k" : String (roundToInt (v)); });
    }
    std::unique_ptr<RangedAudioParameter> f (const String& id, const String& name, NormalisableRange<float> r, float def, AudioParameterFloatAttributes a)
    { return std::make_unique<APF> (ParameterID { id, 1 }, name, r, def, a); }
    std::unique_ptr<RangedAudioParameter> b (const String& id, const String& name, bool def)
    { return std::make_unique<APB> (ParameterID { id, 1 }, name, def); }
    std::unique_ptr<RangedAudioParameter> c (const String& id, const String& name, const StringArray& items, int def)
    { return std::make_unique<APC> (ParameterID { id, 1 }, name, items, def); }
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace ParamIDs; using namespace ParamChoices;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;

    v.push_back (f (inGain,  "Input Gain",  { -24.0f, 24.0f, 0.1f }, 0.0f, unit ("dB")));
    v.push_back (f (outGain, "Output Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, unit ("dB")));
    v.push_back (f (dryWet,  "Dry/Wet",     { 0.0f, 100.0f, 0.1f }, 100.0f, unit ("%", 0)));
    v.push_back (b (bypass,  "Bypass", false));

    v.push_back (b (gateOn, "Gate On", true));
    v.push_back (f (gateThresh,  "Gate Threshold", { -80.0f, 0.0f, 0.1f }, -50.0f, unit ("dB")));
    v.push_back (f (gateAttack,  "Gate Attack",  skewed (0.1f, 50.0f, 5.0f), 2.0f, unit ("ms")));
    v.push_back (f (gateHold,    "Gate Hold",    skewed (0.0f, 500.0f, 80.0f), 60.0f, unit ("ms", 0)));
    v.push_back (f (gateRelease, "Gate Release", skewed (5.0f, 1000.0f, 150.0f), 150.0f, unit ("ms", 0)));
    v.push_back (f (gateRange,   "Gate Range",   { -80.0f, 0.0f, 0.1f }, -30.0f, unit ("dB")));

    v.push_back (b (tuneOn, "Tune On", false));
    v.push_back (c (tuneKey,   "Key", keys, 0));
    v.push_back (c (tuneScale, "Scale", scales, 0));
    v.push_back (f (tuneSpeed,    "Retune Speed", skewed (0.0f, 400.0f, 50.0f), 25.0f, unit ("ms", 0)));
    v.push_back (f (tuneAmount,   "Correction Amount", { 0.0f, 100.0f, 0.1f }, 100.0f, unit ("%", 0)));
    v.push_back (f (tuneHumanize, "Humanize", { 0.0f, 100.0f, 0.1f }, 0.0f, unit ("%", 0)));
    v.push_back (f (tuneSmooth,   "Smoothness", { 0.0f, 100.0f, 0.1f }, 35.0f, unit ("%", 0)));

    v.push_back (b (popOn, "Plosive Filter On", true));
    v.push_back (f (popSens,  "Plosive Sensitivity", { 0.0f, 100.0f, 0.1f }, 50.0f, unit ("%", 0)));
    v.push_back (f (popFreq,  "Plosive Frequency", skewed (60.0f, 400.0f, 150.0f), 150.0f, hzUnit()));
    v.push_back (f (popDepth, "Plosive Max Reduction", { 0.0f, 30.0f, 0.1f }, 14.0f, unit ("dB")));

    v.push_back (b (deessOn, "De-Esser On", true));
    v.push_back (f (deessFreq,   "De-Ess Frequency", skewed (2000.0f, 14000.0f, 6500.0f), 6500.0f, hzUnit()));
    v.push_back (f (deessThresh, "De-Ess Threshold", { -60.0f, 0.0f, 0.1f }, -30.0f, unit ("dB")));
    v.push_back (f (deessRange,  "De-Ess Max Reduction", { 0.0f, 24.0f, 0.1f }, 8.0f, unit ("dB")));
    v.push_back (b (deessListen, "De-Ess Listen", false));

    v.push_back (b (eqOn, "EQ On", true));
    // bands 1-6 are the classic layout every preset uses; 7-12 start switched off (double-click the graph to add one)
    const int   defType[12] = { 3, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0 };
    const float defFreq[12] = { 90, 200, 320, 2500, 7000, 12000, 150, 500, 1000, 2000, 4000, 9000 };
    const float defQ[12] = { 0.71f, 0.71f, 1.4f, 1.0f, 2.0f, 0.71f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 12; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        v.push_back (b (band (i, "on"), n + "On", i < 6));
        v.push_back (c (band (i, "type"), n + "Type", bandTypes, defType[i]));
        v.push_back (f (band (i, "freq"), n + "Freq", skewed (20.0f, 20000.0f, 1000.0f), defFreq[i], hzUnit()));
        v.push_back (f (band (i, "gain"), n + "Gain", { -18.0f, 18.0f, 0.1f }, 0.0f, unit ("dB")));
        v.push_back (f (band (i, "q"),    n + "Q",    skewed (0.1f, 18.0f, 1.5f), defQ[i], unit ("", 2)));
        v.push_back (f (band (i, "dyn"),  n + "Dynamic Range", { -18.0f, 18.0f, 0.1f }, 0.0f, unit ("dB")));
        v.push_back (f (band (i, "thr"),  n + "Dynamic Threshold", { -60.0f, 0.0f, 0.1f }, -30.0f, unit ("dB")));
    }
    v.push_back (b (resOn, "Resonance Suppressor On", false));
    v.push_back (f (resDepth,  "Resonance Depth", { 0.0f, 12.0f, 0.1f }, 5.0f, unit ("dB")));
    v.push_back (f (resSharp,  "Resonance Sharpness", { 0.0f, 100.0f, 0.1f }, 50.0f, unit ("%", 0)));
    v.push_back (f (resSelect, "Resonance Selectivity", { 0.0f, 100.0f, 0.1f }, 50.0f, unit ("%", 0)));
    v.push_back (f (resSpeed,  "Resonance Speed", { 0.0f, 100.0f, 0.1f }, 50.0f, unit ("%", 0)));
    v.push_back (f (resLo,     "Resonance Low", skewed (125.0f, 16000.0f, 1500.0f), 400.0f, hzUnit()));
    v.push_back (f (resHi,     "Resonance High", skewed (125.0f, 16000.0f, 1500.0f), 12000.0f, hzUnit()));
    v.push_back (b (resDelta,  "Resonance Delta (listen)", false));

    v.push_back (b (matchOn, "Match EQ On", true));
    v.push_back (f (matchStrength, "Match Strength", { 0.0f, 100.0f, 0.1f }, 70.0f, unit ("%", 0)));
    v.push_back (f (matchSmooth,   "Match Smoothing", { 0.08f, 1.0f, 0.01f }, 0.33f, unit ("oct", 2)));
    v.push_back (f (matchLimit,    "Match Max Boost/Cut", { 1.0f, 12.0f, 0.1f }, 6.0f, unit ("dB")));

    v.push_back (b (compOn, "Compressor On", true));
    v.push_back (f (compThresh,  "Comp Threshold", { -60.0f, 0.0f, 0.1f }, -18.0f, unit ("dB")));
    v.push_back (f (compRatio,   "Comp Ratio",   skewed (1.0f, 20.0f, 4.0f), 3.0f, unit (":1")));
    v.push_back (f (compAttack,  "Comp Attack",  skewed (0.1f, 100.0f, 10.0f), 8.0f, unit ("ms")));
    v.push_back (f (compRelease, "Comp Release", skewed (10.0f, 1000.0f, 120.0f), 120.0f, unit ("ms", 0)));
    v.push_back (f (compMakeup,  "Comp Makeup",  { 0.0f, 24.0f, 0.1f }, 3.0f, unit ("dB")));

    v.push_back (b (satOn, "Saturation On", false));
    v.push_back (f (satDrive, "Saturation Drive", { 0.0f, 24.0f, 0.1f }, 6.0f, unit ("dB")));
    v.push_back (f (satMix,   "Saturation Mix",   { 0.0f, 100.0f, 0.1f }, 35.0f, unit ("%", 0)));
    v.push_back (b (dblOn, "Doubler On", false));
    v.push_back (f (dblAmount, "Doubler Amount", { 0.0f, 100.0f, 0.1f }, 35.0f, unit ("%", 0)));
    v.push_back (b (wideOn, "Widener On", false));
    v.push_back (f (wideAmount, "Widener Amount", { 0.0f, 100.0f, 0.1f }, 30.0f, unit ("%", 0)));

    v.push_back (b (dlyOn, "Delay On", true));
    v.push_back (b (dlySync, "Delay Sync", true));
    v.push_back (f (dlyTime, "Delay Time", skewed (1.0f, 2000.0f, 300.0f), 375.0f, unit ("ms", 0)));
    v.push_back (c (dlyDiv,  "Delay Division", divisions, 3));
    v.push_back (c (dlyFeel, "Delay Feel", feels, 0));
    v.push_back (f (dlyFeedback, "Delay Feedback", { 0.0f, 95.0f, 0.1f }, 35.0f, unit ("%", 0)));
    v.push_back (f (dlyLowCut,  "Delay Low Cut",  skewed (20.0f, 2000.0f, 250.0f), 250.0f, hzUnit()));
    v.push_back (f (dlyHighCut, "Delay High Cut", skewed (1000.0f, 20000.0f, 6000.0f), 6000.0f, hzUnit()));
    v.push_back (c (dlyMode, "Delay Mode", dlyModes, 0));
    v.push_back (f (dlyMix,  "Delay Mix",  { 0.0f, 100.0f, 0.1f }, 25.0f, unit ("%", 0)));
    v.push_back (f (dlyDuck, "Delay Ducking", { 0.0f, 100.0f, 0.1f }, 45.0f, unit ("%", 0)));

    v.push_back (b (revOn, "Reverb On", true));
    v.push_back (c (revType, "Reverb Type", revTypes, 1));
    v.push_back (f (revPreDelay, "Reverb Pre-Delay", { 0.0f, 250.0f, 0.1f }, 20.0f, unit ("ms", 0)));
    v.push_back (f (revDecay,    "Reverb Decay", skewed (0.2f, 10.0f, 2.0f), 2.6f, unit ("s")));
    v.push_back (f (revDamp,     "Reverb Damping", { 0.0f, 100.0f, 0.1f }, 40.0f, unit ("%", 0)));
    v.push_back (f (revMix,      "Reverb Mix", { 0.0f, 100.0f, 0.1f }, 28.0f, unit ("%", 0)));
    v.push_back (f (revDuck,     "Reverb Ducking", { 0.0f, 100.0f, 0.1f }, 60.0f, unit ("%", 0)));

    v.push_back (b (limOn, "Limiter On", true));
    v.push_back (f (limCeiling, "Limiter Ceiling", { -30.0f, 0.0f, 0.1f }, -1.0f, unit ("dB")));
    v.push_back (f (limDrive,   "Limiter Drive", { 0.0f, 24.0f, 0.1f }, 0.0f, unit ("dB")));
    v.push_back (c (limCharacter, "Limiter Character", limCharacters, 0));
    v.push_back (f (limRelease, "Limiter Release", skewed (1.0f, 1000.0f, 80.0f), 80.0f, unit ("ms", 0)));

    v.push_back (c (scRole, "Sidechain Input", scRoles, 0));
    v.push_back (c (latencyMode, "Latency Mode", latencyModes, 0));
    v.push_back (b (autoKey, "Auto Key", false));
    return { v.begin(), v.end() };
}

//==============================================================================
ParamReader::ParamReader (AudioProcessorValueTreeState& s) : state (s)
{
    for (auto* param : s.processor.getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (param))
            p[r->getParameterID()] = s.getRawParameterValue (r->getParameterID());
    const char* what[7] = { "on", "type", "freq", "gain", "q", "dyn", "thr" };
    for (int b = 0; b < sauce::ParametricEQ::kBands; ++b) for (int k = 0; k < 7; ++k) bandPtr[(size_t) b][(size_t) k] = p.at (ParamIDs::band (b, what[k]));
}

float ParamReader::get (const String& id) const { return raw (id)->load (std::memory_order_relaxed); }

double ParamReader::delayTimeMs (bool sync, int div, int feel, float freeMs, double bpm)
{
    if (! sync) return freeMs;
    static const double beats[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };       // in quarter notes
    double q = beats[jlimit (0, 5, div)];
    if (feel == 1) q *= 1.5; else if (feel == 2) q *= 2.0 / 3.0;
    return jlimit (1.0, 4000.0, 60000.0 / jlimit (30.0, 300.0, bpm) * q);
}

sauce::ChainParams ParamReader::read (double bpm) const
{
    using namespace ParamIDs;
    sauce::ChainParams c;
    auto on = [this] (const String& id) { return get (id) > 0.5f; };
    auto pc = [this] (const String& id) { return get (id) * 0.01f; };
    c.inGainDb = get (inGain); c.outGainDb = get (outGain); c.dryWet = pc (dryWet); c.bypass = on (bypass);
    c.gateOn = on (gateOn); c.gateThresh = get (gateThresh); c.gateAttack = get (gateAttack); c.gateHold = get (gateHold);
    c.gateRelease = get (gateRelease); c.gateRange = get (gateRange);
    c.tuneOn = on (tuneOn); c.tuneKey = (int) get (tuneKey); c.tuneScale = (int) get (tuneScale); c.tuneSpeed = get (tuneSpeed);
    c.tuneAmount = pc (tuneAmount); c.tuneHumanize = pc (tuneHumanize); c.tuneSmooth = pc (tuneSmooth);
    c.deessOn = on (deessOn); c.deessFreq = get (deessFreq); c.deessThresh = get (deessThresh); c.deessRange = get (deessRange);
    c.deessListen = on (deessListen);
    c.eqOn = on (eqOn);
    for (int i = 0; i < sauce::ParametricEQ::kBands; ++i)
    {
        auto& bp = c.bands[(size_t) i];
        const auto& bpp = bandPtr[(size_t) i];
        bp.on = bpp[0]->load() > 0.5f; bp.type = (int) bpp[1]->load(); bp.freq = bpp[2]->load();
        bp.gainDb = bpp[3]->load(); bp.q = bpp[4]->load(); bp.dynDb = bpp[5]->load(); bp.dynThreshDb = bpp[6]->load();
    }
    c.resOn = on (resOn); c.resDelta = on (resDelta); c.resDepth = get (resDepth); c.resSharp = pc (resSharp); c.resSelect = pc (resSelect);
    c.resSpeed = pc (resSpeed); c.resLo = get (resLo); c.resHi = get (resHi);
    c.popOn = on (popOn); c.popSens = pc (popSens); c.popFreq = get (popFreq); c.popDepth = get (popDepth);
    c.matchOn = on (matchOn);
    c.compOn = on (compOn); c.compThresh = get (compThresh); c.compRatio = get (compRatio); c.compAttack = get (compAttack);
    c.compRelease = get (compRelease); c.compMakeup = get (compMakeup);
    c.satOn = on (satOn); c.satDrive = get (satDrive); c.satMix = pc (satMix);
    c.dblOn = on (dblOn); c.dblAmount = pc (dblAmount); c.wideOn = on (wideOn); c.wideAmount = pc (wideAmount);
    c.dlyOn = on (dlyOn);
    c.dlyTimeMs = (float) delayTimeMs (on (dlySync), (int) get (dlyDiv), (int) get (dlyFeel), get (dlyTime), bpm);
    c.dlyFeedback = pc (dlyFeedback); c.dlyLowCut = get (dlyLowCut); c.dlyHighCut = get (dlyHighCut);
    c.dlyPingPong = (int) get (dlyMode) == 1; c.dlyMix = pc (dlyMix); c.dlyDuck = pc (dlyDuck);
    c.revOn = on (revOn); c.revType = (int) get (revType); c.revPreDelay = get (revPreDelay); c.revDecay = get (revDecay);
    c.revDamp = pc (revDamp); c.revMix = pc (revMix); c.revDuck = pc (revDuck);
    c.limOn = on (limOn); c.limCeiling = get (limCeiling); c.limRelease = get (limRelease);
    c.limDrive = get (limDrive); c.limCharacter = (int) get (limCharacter);
    c.lowLatency = (int) get (latencyMode) == 1;
    return c;
}
