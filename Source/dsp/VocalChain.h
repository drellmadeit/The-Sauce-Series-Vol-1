#pragma once
#include "Dynamics.h"
#include "PitchCorrector.h"
#include "Equalizer.h"
#include "Effects.h"

namespace sauce
{
/** Plain parameter snapshot handed to the chain at the start of every block. */
struct ChainParams
{
    float inGainDb = 0, outGainDb = 0, dryWet = 1; bool bypass = false;
    // gate
    bool gateOn = true; float gateThresh = -50, gateAttack = 2, gateHold = 60, gateRelease = 150, gateRange = -40;
    // tune
    bool tuneOn = false; int tuneKey = 0, tuneScale = 0; float tuneSpeed = 20, tuneAmount = 1, tuneHumanize = 0;
    // eq
    bool eqOn = true; std::array<EqBandParams, ParametricEQ::kBands> bands;
    // match
    bool matchOn = true; std::array<float, MatchFilterbank::kBands> matchGains {};
    // comp
    bool compOn = true; float compThresh = -18, compRatio = 3, compAttack = 10, compRelease = 120, compMakeup = 0;
    // fx
    bool satOn = false; float satDrive = 6, satMix = 0.5f; bool dblOn = false; float dblAmount = 0.4f; bool wideOn = false; float wideAmount = 0.3f;
    // delay
    bool dlyOn = true; float dlyTimeMs = 375, dlyFeedback = 0.3f, dlyLowCut = 250, dlyHighCut = 6000, dlyMix = 0.2f, dlyDuck = 0.5f; bool dlyPingPong = false;
    // reverb
    bool revOn = true; int revType = 1; float revPreDelay = 20, revDecay = 2, revDamp = 0.4f, revMix = 0.2f, revDuck = 0.5f;
    // limiter
    bool limOn = true; float limCeiling = -1, limRelease = 80;
    bool lowLatency = false;      // Tracking mode
    /** Serial module order: indices of Module (Gate, Tune, Eq, Match, Comp, Sat). Doubler/width, delay, reverb
        and the limiter always come after these (they need the finished, centred lead). */
    std::array<uint8_t, 6> order { 0, 1, 2, 3, 4, 5 };
};

enum ChainModule : uint8_t { MGate = 0, MTune, MEq, MMatch, MComp, MSat };

/** Read-outs for meters (audio thread writes, processor publishes through atomics). */
struct ChainMeters
{
    float inPeak[2] {}, outPeak[2] {}, gateGainDb = 0, compGrDb = 0, limGrDb = 0;
    float delayReturnRms = 0, reverbReturnRms = 0, delayDuckDb = 0, reverbDuckDb = 0;
    float tuneHz = 0, tuneTargetMidi = -1, tuneCorrection = 0; bool tuneVoiced = false;
};

/** Analysis taps (one mono sample per processed sample), filled per block for the analysis thread. */
struct ChainTaps { float* in = nullptr; float* eq = nullptr; float* lead = nullptr; float* out = nullptr; };

class VocalChain
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        gate.prepare (sr); tune.prepare (sr); eq.prepare (sr); match.prepare (sr); comp.prepare (sr);
        sat.prepare (sr); dbl.prepare (sr); wide.prepare (sr); delay.prepare (sr); reverb.prepare (sr);
        va.prepare (sr); duckDelay.prepare (sr); duckReverb.prepare (sr); limiter.prepare (sr);
        const int maxLatency = tune.getMaxLatency() + limiter.L;
        dryAlign.prepare (tune.getMaxLatency() + 4);
        for (auto& b : bypassLine) b.prepare (maxLatency + 4);
        latency = tune.getLatency() + limiter.latency();
        inGain.reset (sr, 30, 1); outGain.reset (sr, 30, 1); dryWet.reset (sr, 30, 1); bypassMix.reset (sr, 30, 0);
        dlyRms = revRms = 0; rmsCoef = tcCoef (300.0f, sr);
    }
    int getLatency() const { return latency; }

    void setParams (const ChainParams& p)
    {
        order = p.order;
        if (p.lowLatency != tune.isLowLatency())
        {
            tune.setLowLatency (p.lowLatency); limiter.setLowLatency (p.lowLatency);
            dryAlign.clear(); for (auto& b : bypassLine) b.clear();
            latency = tune.getLatency() + limiter.latency();
        }
        inGain.setTarget (dbToGain (p.inGainDb)); outGain.setTarget (dbToGain (p.outGainDb));
        dryWet.setTarget (p.dryWet); bypassMix.setTarget (p.bypass ? 1.0f : 0.0f);
        gate.enabled = p.gateOn; gate.threshDb = p.gateThresh; gate.attackMs = p.gateAttack; gate.holdMs = p.gateHold;
        gate.releaseMs = p.gateRelease; gate.rangeDb = p.gateRange; gate.update();
        tune.enabled = p.tuneOn; tune.key = p.tuneKey; tune.scale = p.tuneScale; tune.retuneMs = p.tuneSpeed;
        tune.amount = p.tuneAmount; tune.humanize = p.tuneHumanize; tune.update();
        eq.enabled = p.eqOn; eq.bands = p.bands;
        match.enabled = p.matchOn; match.gainsDb = p.matchGains;
        comp.enabled = p.compOn; comp.threshDb = p.compThresh; comp.ratio = p.compRatio; comp.attackMs = p.compAttack;
        comp.releaseMs = p.compRelease; comp.makeupDb = p.compMakeup; comp.update();
        sat.enabled = p.satOn; sat.driveDb = p.satDrive; sat.mix = p.satMix; sat.update();
        dbl.enabled = p.dblOn; dbl.amount = p.dblAmount; dbl.update();
        wide.enabled = p.wideOn; wide.amount = p.wideAmount; wide.update();
        delay.enabled = p.dlyOn; delay.timeMs = p.dlyTimeMs; delay.feedback = p.dlyFeedback; delay.lowCutHz = p.dlyLowCut;
        delay.highCutHz = p.dlyHighCut; delay.mix = p.dlyMix; delay.pingPong = p.dlyPingPong; delay.update();
        reverb.enabled = p.revOn; reverb.type = p.revType; reverb.preDelayMs = p.revPreDelay; reverb.decayS = p.revDecay;
        reverb.damping = p.revDamp; reverb.mix = p.revMix; reverb.update();
        duckDelay.depth = p.dlyDuck; duckReverb.depth = p.revDuck;
        limiter.enabled = p.limOn; limiter.ceilingDb = p.limCeiling; limiter.releaseMs = p.limRelease; limiter.update();
    }

    /** Processes the MAIN vocal bus in place (1 or 2 channels). The sidechain never enters this function. */
    void process (float* const* io, int numCh, int n, ChainTaps taps)
    {
        float inPk[2] {}, outPk[2] {}, gateMin = 1, compMin = 0, limMin = 0, dlyDuckMin = 1, revDuckMin = 1;
        for (int i = 0; i < n; ++i)
        {
            const float rawL = io[0][i], rawR = numCh > 1 ? io[1][i] : rawL;
            inPk[0] = std::max (inPk[0], std::abs (rawL)); inPk[1] = std::max (inPk[1], std::abs (rawR));
            bypassLine[0].push (rawL); bypassLine[1].push (rawR);

            const float x = 0.5f * (rawL + rawR) * inGain.next();     // stereo vocal -> centred mono lead
            dryAlign.push (x);
            float v = x, eqTap = x;
            for (const uint8_t m : order)        // user-arrangeable serial chain
            {
                switch (m)
                {
                    case MGate:  v = gate.process (v); gateMin = std::min (gateMin, gate.lastGain); break;
                    case MTune:  v = tune.process (v); break;
                    case MEq:    v = eq.process (v); eqTap = v; break;
                    case MMatch: v = match.process (v); break;
                    case MComp:  v = comp.process (v); compMin = std::min (compMin, comp.currentGrDb); break;
                    case MSat:   v = sat.process (v); break;
                    default: break;
                }
            }
            const float lead = v;                 // finished dry lead: drives both duckers
            const float act = va.process (lead);

            const float s = lead;
            float L = s, R = s;
            dbl.process (s, L, R);
            wide.process (s, L, R);

            float dl, dr, rl, rr;
            delay.process (L, R, dl, dr);
            reverb.process (L, R, rl, rr);
            const float gd = duckDelay.process (act), gr = duckReverb.process (act);
            dlyDuckMin = std::min (dlyDuckMin, gd); revDuckMin = std::min (revDuckMin, gr);
            dl *= gd; dr *= gd; rl *= gr; rr *= gr;
            dlyRms = (dl * dl + dr * dr) * 0.5f + rmsCoef * (dlyRms - (dl * dl + dr * dr) * 0.5f);
            revRms = (rl * rl + rr * rr) * 0.5f + rmsCoef * (revRms - (rl * rl + rr * rr) * 0.5f);

            float oL = L + dl + rl, oR = R + dr + rr;
            const float dry = dryAlign.readInt (tune.getLatency());
            const float w = dryWet.next();
            oL = dry + w * (oL - dry); oR = dry + w * (oR - dry);
            const float og = outGain.next();
            oL *= og; oR *= og;
            limiter.process (oL, oR);            limMin = std::min (limMin, limiter.grDb);

            const float b = bypassMix.next();
            if (b > 0.0f)
            {
                oL += b * (bypassLine[0].readInt (latency) - oL);
                oR += b * (bypassLine[1].readInt (latency) - oR);
            }
            flushDenormal (oL); flushDenormal (oR);
            if (numCh > 1) { io[0][i] = oL; io[1][i] = oR; }
            else io[0][i] = 0.5f * (oL + oR);
            outPk[0] = std::max (outPk[0], std::abs (oL)); outPk[1] = std::max (outPk[1], std::abs (oR));

            if (taps.in)   taps.in[i]   = 0.5f * (rawL + rawR);
            if (taps.eq)   taps.eq[i]   = eqTap;
            if (taps.lead) taps.lead[i] = lead;
            if (taps.out)  taps.out[i]  = 0.5f * (oL + oR);
        }
        meters.inPeak[0] = inPk[0]; meters.inPeak[1] = numCh > 1 ? inPk[1] : inPk[0];
        meters.outPeak[0] = outPk[0]; meters.outPeak[1] = numCh > 1 ? outPk[1] : outPk[0];
        meters.gateGainDb = gainToDb (gateMin, -60.0f); meters.compGrDb = compMin; meters.limGrDb = limMin;
        meters.delayReturnRms = std::sqrt (dlyRms); meters.reverbReturnRms = std::sqrt (revRms);
        meters.delayDuckDb = gainToDb (dlyDuckMin, -60.0f); meters.reverbDuckDb = gainToDb (revDuckMin, -60.0f);
        meters.tuneHz = tune.lastHz; meters.tuneTargetMidi = tune.lastTargetMidi; meters.tuneCorrection = tune.lastCorrection;
        meters.tuneVoiced = tune.isVoiced();
    }

    ChainMeters meters;
    ParametricEQ eq;   // exposed for band-solo

private:
    double sr = 44100; int latency = 0;
    NoiseGate gate; PitchCorrector tune; MatchFilterbank match; Compressor comp;
    Saturation sat; Doubler dbl; Widener wide; StereoDelay delay; FdnReverb reverb;
    VoiceActivity va; Ducker duckDelay, duckReverb; Limiter limiter;
    DelayLine dryAlign, bypassLine[2];
    std::array<uint8_t, 6> order { 0, 1, 2, 3, 4, 5 };
    Smoothed inGain, outGain, dryWet, bypassMix; float dlyRms = 0, revRms = 0, rmsCoef = 0;
};
} // namespace sauce
