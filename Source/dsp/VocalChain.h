#pragma once
#include "Dynamics.h"
#include "PitchCorrector.h"
#include "Equalizer.h"
#include "Effects.h"

namespace sauce
{
/** The ten movable modules, processed strictly first to last in the user's order. */
enum ChainModule : uint8_t { MGate = 0, MDeess, MTune, MEq, MMatch, MComp, MSat, MDelay, MReverb, MLimiter, MPlosive, kNumModules };
/** One letter per module, in ChainModule order (used for saved chain orders). */
inline constexpr const char* kModuleLetters = "GDTEMCSYRLP";

/** Plain parameter snapshot handed to the chain at the start of every block. */
struct ChainParams
{
    float inGainDb = 0, outGainDb = 0, dryWet = 1; bool bypass = false;
    // gate
    bool gateOn = true; float gateThresh = -50, gateAttack = 2, gateHold = 60, gateRelease = 150, gateRange = -40;
    // plosive filter
    bool popOn = true; float popSens = 0.5f, popFreq = 150, popDepth = 12;
    // resonance suppressor (inside the EQ module)
    bool resOn = false, resDelta = false; float resDepth = 5, resSharp = 0.5f, resSelect = 0.5f, resSpeed = 0.5f, resLo = 400, resHi = 12000;
    // de-esser
    bool deessOn = true, deessListen = false; float deessFreq = 6500, deessThresh = -30, deessRange = 10;
    // tune
    bool tuneOn = false; int tuneKey = 0, tuneScale = 0; float tuneSpeed = 20, tuneAmount = 1, tuneHumanize = 0, tuneSmooth = 0.35f;
    // eq
    bool eqOn = true; std::array<EqBandParams, ParametricEQ::kBands> bands;
    // match
    bool matchOn = true; std::array<float, MatchFilterbank::kBands> matchGains {};
    // comp
    bool compOn = true; float compThresh = -18, compRatio = 3, compAttack = 10, compRelease = 120, compMakeup = 0;
    // vocal fx (saturation -> doubler -> widener)
    bool satOn = false; float satDrive = 6, satMix = 0.5f; bool dblOn = false; float dblAmount = 0.4f; bool wideOn = false; float wideAmount = 0.3f;
    // delay
    bool dlyOn = true; float dlyTimeMs = 375, dlyFeedback = 0.3f, dlyLowCut = 250, dlyHighCut = 6000, dlyMix = 0.2f, dlyDuck = 0.5f; bool dlyPingPong = false;
    // reverb
    bool revOn = true; int revType = 1; float revPreDelay = 20, revDecay = 2, revDamp = 0.4f, revMix = 0.2f, revDuck = 0.5f;
    // limiter
    bool limOn = true; float limCeiling = -1, limRelease = 80, limDrive = 0; int limCharacter = 0;
    bool lowLatency = false;      // Tracking mode
    std::array<uint8_t, kNumModules> order { 0, 10, 2, 3, 4, 5, 1, 6, 7, 8, 9 };
};

/** Read-outs for meters (audio thread writes, processor publishes through atomics). */
struct ChainMeters
{
    float inPeak[2] {}, outPeak[2] {}, gateGainDb = 0, compGrDb = 0, limGrDb = 0, deessGrDb = 0, popGrDb = 0;
    float delayReturnRms = 0, reverbReturnRms = 0, delayDuckDb = 0, reverbDuckDb = 0;
    float tuneHz = 0, tuneTargetMidi = -1, tuneCorrection = 0; bool tuneVoiced = false;
};

/** Analysis taps (one mono sample per processed sample), filled per block for the analysis thread. */
struct ChainTaps { float* in = nullptr; float* eq = nullptr; float* lead = nullptr; float* out = nullptr; };

/** Serial vocal chain. The signal runs through the modules in exactly the order the user arranged them:
    every module processes the output of the one before it (so reverb placed after delay reverberates the echoes,
    a compressor placed after the reverb squeezes the tails, and so on).
    The lead starts as a centred mono signal; from Vocal FX (doubler/width), Delay or Reverb onwards it is stereo,
    and any module placed after that point processes both channels (dynamics are stereo-linked).
    Total latency is fixed (Auto Tune + limiter look-ahead) whatever the order, so DAW compensation never shifts. */
class VocalChain
{
public:
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        gate.prepare (sr); deess.prepare (sr); plosive.prepare (sr); comp.prepare (sr); limiter.prepare (sr);
        for (int c = 0; c < 2; ++c) { tune[c].prepare (sr); eqs[c].prepare (sr); match[c].prepare (sr); sat[c].prepare (sr); }
        dbl.prepare (sr); wide.prepare (sr); delay.prepare (sr); reverb.prepare (sr);
        va.prepare (sr); duckDelay.prepare (sr); duckReverb.prepare (sr);
        const int maxLatency = tune[0].getMaxLatency() + limiter.L;
        dryAlign.prepare (maxLatency + 4);
        for (auto& b : bypassLine) b.prepare (maxLatency + 4);
        latency = tune[0].getLatency() + limiter.latency();
        inGain.reset (sr, 30, 1); outGain.reset (sr, 30, 1); dryWet.reset (sr, 30, 1); bypassMix.reset (sr, 30, 0);
        dlyRms = revRms = 0; rmsCoef = tcCoef (300.0f, sr);
    }
    int getLatency() const { return latency; }
    ParametricEQ& eq() { return eqs[0]; }
    void setBandSolo (int b) { eqs[0].soloBand.store (b); eqs[1].soloBand.store (b); }

    void setParams (const ChainParams& p)
    {
        order = p.order;
        if (p.lowLatency != tune[0].isLowLatency())
        {
            for (auto& t : tune) t.setLowLatency (p.lowLatency);
            limiter.setLowLatency (p.lowLatency);
            dryAlign.clear(); for (auto& b : bypassLine) b.clear();
            latency = tune[0].getLatency() + limiter.latency();
        }
        inGain.setTarget (dbToGain (p.inGainDb)); outGain.setTarget (dbToGain (p.outGainDb));
        dryWet.setTarget (p.dryWet); bypassMix.setTarget (p.bypass ? 1.0f : 0.0f);
        gate.enabled = p.gateOn; gate.threshDb = p.gateThresh; gate.attackMs = p.gateAttack; gate.holdMs = p.gateHold;
        gate.releaseMs = p.gateRelease; gate.rangeDb = p.gateRange; gate.update();
        deess.enabled = p.deessOn; deess.listen = p.deessListen; deess.freqHz = p.deessFreq; deess.threshDb = p.deessThresh;
        deess.rangeDb = p.deessRange; deess.update();
        plosive.enabled = p.popOn; plosive.sensitivity = p.popSens; plosive.freqHz = p.popFreq; plosive.depthDb = p.popDepth; plosive.update();
        for (int c = 0; c < 2; ++c)
        {
            auto& t = tune[c];
            t.enabled = p.tuneOn; t.key = p.tuneKey; t.scale = p.tuneScale; t.retuneMs = p.tuneSpeed;
            t.amount = p.tuneAmount; t.humanize = p.tuneHumanize; t.smooth = p.tuneSmooth; t.update();
            eqs[c].enabled = p.eqOn; eqs[c].bands = p.bands;
            auto& r = eqs[c].res;
            r.enabled = p.resOn; r.delta = p.resDelta; r.depthDb = p.resDepth; r.sharpness = p.resSharp; r.selectivity = p.resSelect;
            r.speed = p.resSpeed; r.loHz = p.resLo; r.hiHz = p.resHi; r.update();
            match[c].enabled = p.matchOn; match[c].gainsDb = p.matchGains;
            sat[c].enabled = p.satOn; sat[c].driveDb = p.satDrive; sat[c].mix = p.satMix; sat[c].update();
        }
        comp.enabled = p.compOn; comp.threshDb = p.compThresh; comp.ratio = p.compRatio; comp.attackMs = p.compAttack;
        comp.releaseMs = p.compRelease; comp.makeupDb = p.compMakeup; comp.update();
        dbl.enabled = p.dblOn; dbl.amount = p.dblAmount; dbl.update();
        wide.enabled = p.wideOn; wide.amount = p.wideAmount; wide.update();
        delay.enabled = p.dlyOn; delay.timeMs = p.dlyTimeMs; delay.feedback = p.dlyFeedback; delay.lowCutHz = p.dlyLowCut;
        delay.highCutHz = p.dlyHighCut; delay.mix = p.dlyMix; delay.pingPong = p.dlyPingPong; delay.update();
        reverb.enabled = p.revOn; reverb.type = p.revType; reverb.preDelayMs = p.revPreDelay; reverb.decayS = p.revDecay;
        reverb.damping = p.revDamp; reverb.mix = p.revMix; reverb.update();
        duckDelay.depth = p.dlyDuck; duckReverb.depth = p.revDuck;
        limiter.enabled = p.limOn; limiter.ceilingDb = p.limCeiling; limiter.releaseMs = p.limRelease;
        limiter.driveDb = p.limDrive; limiter.character = p.limCharacter; limiter.update();
    }

    /** Processes the MAIN vocal bus in place (1 or 2 channels). The sidechain never enters this function. */
    void process (float* const* io, int numCh, int n, ChainTaps taps)
    {
        float inPk[2] {}, outPk[2] {}, gateMin = 1, compMin = 0, limMin = 0, deessMin = 0, popMin = 0, dlyDuckMin = 1, revDuckMin = 1;
        const bool limiterLast = order[kNumModules - 1] == MLimiter;
        const int tuneLat = tune[0].getLatency();
        const int dryDelay = limiterLast ? tuneLat : latency;           // dry/wet blend point
        for (int i = 0; i < n; ++i)
        {
            const float rawL = io[0][i], rawR = numCh > 1 ? io[1][i] : rawL;
            inPk[0] = std::max (inPk[0], std::abs (rawL)); inPk[1] = std::max (inPk[1], std::abs (rawR));
            bypassLine[0].push (rawL); bypassLine[1].push (rawR);

            const float x = 0.5f * (rawL + rawR) * inGain.next();     // stereo vocal -> centred mono lead
            dryAlign.push (x);
            float L = x, R = x, eqTap = x, lead = x, act = 0;
            bool stereo = false, vaDone = false;
            auto voice = [&]
            {
                if (vaDone) return;
                lead = 0.5f * (L + R); act = va.process (lead); vaDone = true;
            };
            for (int k = 0; k < kNumModules; ++k)
            {
                const uint8_t m = order[(size_t) k];
                if (m == MLimiter && limiterLast) break;                  // the final limiter runs after the output stage
                switch (m)
                {
                    case MGate:
                    {
                        const float g = gate.gainFor (std::max (std::abs (L), std::abs (R)));
                        L *= g; R *= g; gateMin = std::min (gateMin, g); break;
                    }
                    case MDeess: deess.process (L, R, stereo); deessMin = std::min (deessMin, deess.lastGrDb); break;
                    case MPlosive: plosive.process (L, R, stereo); popMin = std::min (popMin, plosive.lastGrDb); break;
                    case MTune:  L = tune[0].process (L); R = stereo ? tune[1].process (R) : L; break;
                    case MEq:    L = eqs[0].process (L); R = stereo ? eqs[1].process (R) : L; eqTap = 0.5f * (L + R); break;
                    case MMatch: L = match[0].process (L); R = stereo ? match[1].process (R) : L; break;
                    case MComp:
                    {
                        const float g = comp.gainFor (std::max (std::abs (L), std::abs (R)));
                        L *= g; R *= g; compMin = std::min (compMin, comp.currentGrDb); break;
                    }
                    case MSat:
                    {
                        L = sat[0].process (L); R = stereo ? sat[1].process (R) : L;
                        const float mid = 0.5f * (L + R);
                        dbl.process (mid, L, R); wide.process (mid, L, R);
                        stereo = true; break;
                    }
                    case MDelay:
                    {
                        voice();
                        float dl, dr; delay.process (L, R, dl, dr);
                        const float gd = duckDelay.process (act); dlyDuckMin = std::min (dlyDuckMin, gd);
                        dl *= gd; dr *= gd;
                        dlyRms = (dl * dl + dr * dr) * 0.5f + rmsCoef * (dlyRms - (dl * dl + dr * dr) * 0.5f);
                        L += dl; R += dr; stereo = true; break;
                    }
                    case MReverb:
                    {
                        voice();
                        float rl, rr; reverb.process (L, R, rl, rr);
                        const float gr = duckReverb.process (act); revDuckMin = std::min (revDuckMin, gr);
                        rl *= gr; rr *= gr;
                        revRms = (rl * rl + rr * rr) * 0.5f + rmsCoef * (revRms - (rl * rl + rr * rr) * 0.5f);
                        L += rl; R += rr; stereo = true; break;
                    }
                    case MLimiter: limiter.process (L, R); limMin = std::min (limMin, limiter.grDb); break;
                    default: break;
                }
            }
            voice();

            const float dry = dryAlign.readInt (dryDelay);
            const float w = dryWet.next();
            float oL = dry + w * (L - dry), oR = dry + w * (R - dry);
            const float og = outGain.next();
            oL *= og; oR *= og;
            if (limiterLast) { limiter.process (oL, oR); limMin = std::min (limMin, limiter.grDb); }

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
        meters.gateGainDb = gainToDb (gateMin, -60.0f); meters.compGrDb = compMin; meters.limGrDb = limMin; meters.deessGrDb = deessMin; meters.popGrDb = popMin;
        meters.delayReturnRms = std::sqrt (dlyRms); meters.reverbReturnRms = std::sqrt (revRms);
        meters.delayDuckDb = gainToDb (dlyDuckMin, -60.0f); meters.reverbDuckDb = gainToDb (revDuckMin, -60.0f);
        meters.tuneHz = tune[0].lastHz; meters.tuneTargetMidi = tune[0].lastTargetMidi; meters.tuneCorrection = tune[0].lastCorrection;
        meters.tuneVoiced = tune[0].isVoiced();
    }

    ChainMeters meters;

private:
    double sr = 44100; int latency = 0;
    NoiseGate gate; DeEsser deess; PlosiveFilter plosive; PitchCorrector tune[2]; ParametricEQ eqs[2]; MatchFilterbank match[2]; Compressor comp;
    Saturation sat[2]; Doubler dbl; Widener wide; StereoDelay delay; FdnReverb reverb;
    VoiceActivity va; Ducker duckDelay, duckReverb; Limiter limiter;
    DelayLine dryAlign, bypassLine[2];
    std::array<uint8_t, kNumModules> order { 0, 10, 2, 3, 4, 5, 1, 6, 7, 8, 9 };
    Smoothed inGain, outGain, dryWet, bypassMix; float dlyRms = 0, revRms = 0, rmsCoef = 0;
};
} // namespace sauce
