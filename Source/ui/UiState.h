#pragma once
#include "../PluginProcessor.h"
#include <array>

/** Meter values polled once per UI frame from the processor's atomics, plus short histories for the module graphs. */
struct UiState
{
    static constexpr int kHist = 180;                       // ~6 s at 30 fps
    float inDb[2] { -100, -100 }, outDb[2] { -100, -100 };
    float gateGainDb = 0, compGrDb = 0, limGrDb = 0, deessGrDb = 0, popGrDb = 0, dlyRet = 0, revRet = 0, dlyDuckDb = 0, revDuckDb = 0;
    float tuneHz = 0, tuneTarget = -1, tuneCorr = 0, leadDb = -100; bool voiced = false;
    std::array<float, kHist> hIn {}, hGate {}, hGr {}, hLim {}, hPitch {}, hTarget {}, hOut {}, hDeess {}, hPop {};
    int pos = 0;

    UiState()
    {
        hIn.fill (-100.0f); hOut.fill (-100.0f); hPitch.fill (-1.0f); hTarget.fill (-1.0f);
    }

    void poll (SauceMeters& m)
    {
        auto db = [] (float v) { return sauce::gainToDb (v, -100.0f); };
        inDb[0] = db (m.inL.exchange (0)); inDb[1] = db (m.inR.exchange (0));
        outDb[0] = db (m.outL.exchange (0)); outDb[1] = db (m.outR.exchange (0));
        gateGainDb = m.gateGainDb.load(); compGrDb = m.compGrDb.load(); limGrDb = m.limGrDb.load(); deessGrDb = m.deessGrDb.load(); popGrDb = m.popGrDb.load();
        dlyRet = m.delayReturn.load(); revRet = m.reverbReturn.load(); dlyDuckDb = m.delayDuckDb.load(); revDuckDb = m.reverbDuckDb.load();
        tuneHz = m.tuneHz.load(); tuneTarget = m.tuneTarget.load(); tuneCorr = m.tuneCorrection.load(); voiced = m.tuneVoiced.load();
        leadDb = m.leadLevelDb.load();
        pos = (pos + 1) % kHist;
        hIn[(size_t) pos] = juce::jmax (inDb[0], inDb[1]); hGate[(size_t) pos] = gateGainDb; hGr[(size_t) pos] = compGrDb;
        hLim[(size_t) pos] = limGrDb; hDeess[(size_t) pos] = deessGrDb; hPop[(size_t) pos] = popGrDb; hOut[(size_t) pos] = juce::jmax (outDb[0], outDb[1]);
        hPitch[(size_t) pos] = voiced && tuneHz > 0 ? 69.0f + 12.0f * std::log2 (tuneHz / 440.0f) : -1.0f;
        hTarget[(size_t) pos] = voiced ? tuneTarget : -1.0f;
    }
    /** i = 0 oldest .. kHist-1 newest */
    float at (const std::array<float, kHist>& h, int i) const { return h[(size_t) ((pos + 1 + i) % kHist)]; }
};
