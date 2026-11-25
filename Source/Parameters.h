#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/VocalChain.h"

namespace ParamIDs
{
    inline const juce::String inGain = "in_gain", outGain = "out_gain", dryWet = "dry_wet", bypass = "bypass";
    inline const juce::String gateOn = "gate_on", gateThresh = "gate_thresh", gateAttack = "gate_attack", gateHold = "gate_hold",
                              gateRelease = "gate_release", gateRange = "gate_range";
    inline const juce::String tuneOn = "tune_on", tuneKey = "tune_key", tuneScale = "tune_scale", tuneSpeed = "tune_speed",
                              tuneAmount = "tune_amount", tuneHumanize = "tune_humanize", tuneSmooth = "tune_smooth";
    inline const juce::String popOn = "pop_on", popSens = "pop_sens", popFreq = "pop_freq", popDepth = "pop_depth";
    inline const juce::String resOn = "res_on", resDepth = "res_depth", resSharp = "res_sharp", resSelect = "res_select", resSpeed = "res_speed",
                              resLo = "res_lo", resHi = "res_hi", resDelta = "res_delta";
    inline const juce::String deessOn = "deess_on", deessFreq = "deess_freq", deessThresh = "deess_thresh", deessRange = "deess_range",
                              deessListen = "deess_listen";
    inline const juce::String eqOn = "eq_on";
    inline juce::String band (int b, const char* what) { return "eq" + juce::String (b + 1) + "_" + what; }   // on,type,freq,gain,q,dyn,thr
    inline const juce::String matchOn = "match_on", matchStrength = "match_strength", matchSmooth = "match_smooth", matchLimit = "match_limit";
    inline const juce::String compOn = "comp_on", compThresh = "comp_thresh", compRatio = "comp_ratio", compAttack = "comp_attack",
                              compRelease = "comp_release", compMakeup = "comp_makeup";
    inline const juce::String satOn = "sat_on", satDrive = "sat_drive", satMix = "sat_mix", dblOn = "dbl_on", dblAmount = "dbl_amount",
                              wideOn = "wide_on", wideAmount = "wide_amount";
    inline const juce::String dlyOn = "dly_on", dlySync = "dly_sync", dlyTime = "dly_time", dlyDiv = "dly_div", dlyFeel = "dly_feel",
                              dlyFeedback = "dly_fb", dlyLowCut = "dly_lowcut", dlyHighCut = "dly_highcut", dlyMode = "dly_mode",
                              dlyMix = "dly_mix", dlyDuck = "dly_duck";
    inline const juce::String revOn = "rev_on", revType = "rev_type", revPreDelay = "rev_predelay", revDecay = "rev_decay",
                              revDamp = "rev_damp", revMix = "rev_mix", revDuck = "rev_duck";
    inline const juce::String limOn = "lim_on", limCeiling = "lim_ceiling", limRelease = "lim_release", limDrive = "lim_drive",
                              limCharacter = "lim_character";
    inline const juce::String scRole = "sc_role";
    inline const juce::String latencyMode = "latency_mode", autoKey = "tune_autokey";
    /** Workflow settings that presets never change. */
    inline bool isWorkflowSetting (const juce::String& id) { return id == bypass || id == scRole || id == latencyMode || id == autoKey || id == deessListen || id == resDelta; }
}

namespace ParamChoices
{
    inline const juce::StringArray keys    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    inline const juce::StringArray scales  { "Major", "Minor", "Harmonic Minor", "Pentatonic Major", "Pentatonic Minor", "Blues", "Chromatic" };
    inline const juce::StringArray bandTypes { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    inline const juce::StringArray divisions { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
    inline const juce::StringArray feels   { "Straight", "Dotted", "Triplet" };
    inline const juce::StringArray dlyModes { "Stereo", "Ping-Pong" };
    inline const juce::StringArray revTypes { "Room", "Plate", "Hall" };
    inline const juce::StringArray scRoles { "Instrumental", "Reference Vocal" };
    inline const juce::StringArray latencyModes { "Mix", "Tracking" };
    inline const juce::StringArray limCharacters { "Clean", "Punchy", "Loud" };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw parameter pointers -> plain ChainParams for the audio thread (no string lookups per block). */
class ParamReader
{
public:
    explicit ParamReader (juce::AudioProcessorValueTreeState& s);
    sauce::ChainParams read (double bpm) const;
    float get (const juce::String& id) const;
    static double delayTimeMs (bool sync, int div, int feel, float freeMs, double bpm);
private:
    juce::AudioProcessorValueTreeState& state;
    std::map<juce::String, std::atomic<float>*> p;
    std::array<std::array<std::atomic<float>*, 7>, sauce::ParametricEQ::kBands> bandPtr {};    // on,type,freq,gain,q,dyn,thr - cached so the audio thread never builds strings
    std::atomic<float>* raw (const juce::String& id) const { return p.at (id); }
};
