#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

SauceProcessor::SauceProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)
                        .withInput  ("Instrumental", AudioChannelSet::stereo(), false)),
      apvts (*this, &undo, "PARAMS", createParameterLayout()),
      reader (apvts),
      analysis (*this),
      presets (*this)
{
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (p))
            apvts.addParameterListener (r->getParameterID(), this);

    // First launch: a neutral genre preset, no genre/artist filter selected.
    const int idx = presets.indexOf ("R&B Vocal - Polished");
    if (idx >= 0) presets.load (idx);
    presetDirty = false;
    undo.clearUndoHistory();
#if ! SAUCE_TEST_BUILD
    startTimerHz (2);
#endif
}

SauceProcessor::~SauceProcessor()
{
    stopTimer();
    analysis.stop();
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (p))
            apvts.removeParameterListener (r->getParameterID(), this);
}

//==============================================================================
bool SauceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != AudioChannelSet::mono() && sc != AudioChannelSet::stereo()) return false;
    }
    return true;
}

void SauceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSr = sampleRate;
    preparedBlock = jmax (32, samplesPerBlock);
    chain.prepare (sampleRate, preparedBlock);
    { auto p0 = reader.read (hostBpm.load()); chain.setParams (p0); }
    for (auto* v : { &tapIn, &tapEq, &tapLead, &tapOut, &scMono }) v->assign ((size_t) preparedBlock, 0.0f);
    setLatencySamples (chain.getLatency());
    analysis.prepare (sampleRate, preparedBlock);
#if ! SAUCE_TEST_BUILD
    analysis.start();
#endif
}

void SauceProcessor::releaseResources() {}

void SauceProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);

    const int n = buffer.getNumSamples();
    for (int start = 0; start < n; start += preparedBlock)
        processChunk (buffer, start, jmin (preparedBlock, n - start));
}

void SauceProcessor::processChunk (AudioBuffer<float>& buffer, int start, int n)
{
    auto mainBus = getBusBuffer (buffer, true, 0);
    const int numCh = jmin (2, mainBus.getNumChannels());
    if (numCh == 0) return;
    float* io[2] = { mainBus.getWritePointer (0, start), numCh > 1 ? mainBus.getWritePointer (1, start) : nullptr };

    // ---- sidechain: analysis only. It is mixed to a private mono scratch buffer and never written to the output. ----
    bool scPresent = false;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        const int scCh = sc.getNumChannels();
        if (scCh > 0)
        {
            scPresent = true;
            for (int i = 0; i < n; ++i)
            {
                float s = 0; for (int c = 0; c < scCh; ++c) s += sc.getReadPointer (c, start)[i];
                scMono[(size_t) i] = s / (float) scCh;
            }
        }
    }
    if (! scPresent) std::fill (scMono.begin(), scMono.begin() + n, 0.0f);
    sidechainConnected.store (scPresent);

    auto p = reader.read (hostBpm.load());
    { const uint32_t packed = chainOrderPacked.load (std::memory_order_acquire);
      for (int i = 0; i < 6; ++i) p.order[(size_t) i] = (uint8_t) ((packed >> (4 * i)) & 0xF); }
    p.matchGains = matchBuf[matchIdx.load (std::memory_order_acquire)];
    chain.setParams (p);
    if (chain.getLatency() != getLatencySamples()) setLatencySamples (chain.getLatency());   // Mix <-> Tracking switch
    chain.process (io, numCh, n, { tapIn.data(), tapEq.data(), tapLead.data(), tapOut.data() });

    // Any extra output channels (never expected) are silenced so nothing but the vocal leaves the plugin.
    for (int c = numCh; c < getTotalNumOutputChannels(); ++c)
        if (c < buffer.getNumChannels() && c >= mainBus.getNumChannels()) buffer.clear (c, start, n);

    analysis.push (tapIn.data(), tapEq.data(), tapLead.data(), tapOut.data(), scMono.data(), scPresent, n);

    const auto& m = chain.meters;
    // peaks accumulate until the UI reads them with exchange(0), so no peak is missed between repaints
    auto peakHold = [] (std::atomic<float>& a, float v) { a.store (jmax (v, a.load())); };
    peakHold (meters.inL, m.inPeak[0]); peakHold (meters.inR, m.inPeak[1]);
    peakHold (meters.outL, m.outPeak[0]); peakHold (meters.outR, m.outPeak[1]);
    meters.gateGainDb.store (m.gateGainDb); meters.compGrDb.store (m.compGrDb); meters.limGrDb.store (m.limGrDb);
    meters.delayReturn.store (m.delayReturnRms); meters.reverbReturn.store (m.reverbReturnRms);
    meters.delayDuckDb.store (m.delayDuckDb); meters.reverbDuckDb.store (m.reverbDuckDb);
    meters.tuneHz.store (m.tuneHz); meters.tuneTarget.store (m.tuneTargetMidi); meters.tuneCorrection.store (m.tuneCorrection);
    meters.tuneVoiced.store (m.tuneVoiced);
    double e = 0; for (int i = 0; i < n; ++i) e += tapLead[(size_t) i] * tapLead[(size_t) i];
    meters.leadLevelDb.store (sauce::gainToDb ((float) std::sqrt (e / jmax (1, n)), -100.0f));
}

//==============================================================================
void SauceProcessor::setMatchGains (const std::array<float, sauce::MatchFilterbank::kBands>& g)
{
    const int next = 1 - matchIdx.load();
    matchBuf[next] = g;
    matchIdx.store (next, std::memory_order_release);
}

std::array<float, sauce::MatchFilterbank::kBands> SauceProcessor::getMatchGains() const { return matchBuf[matchIdx.load()]; }

AudioProcessorParameter* SauceProcessor::getBypassParameter() const { return apvts.getParameter (ParamIDs::bypass); }

int SauceProcessor::getNumPrograms() { return 1; }
int SauceProcessor::getCurrentProgram() { return 0; }
void SauceProcessor::setCurrentProgram (int) {}
const String SauceProcessor::getProgramName (int) { return presetName; }

void SauceProcessor::parameterChanged (const String& id, float)
{
    if (loadingPreset.load() || ParamIDs::isWorkflowSetting (id) || autoKeyApplying.load()) return;
    if (! presetDirty) { presetDirty = true; stateChanged.sendChangeMessage(); }
}

void SauceProcessor::setFilters (const String& genre, const String& artist)
{
    genreFilter = genre; artistFilter = artist;
    stateChanged.sendChangeMessage();
}

void SauceProcessor::resetAllParametersToDefault()
{
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<RangedAudioParameter*> (p))
            if (! ParamIDs::isWorkflowSetting (r->getParameterID()))
                r->setValueNotifyingHost (r->getDefaultValue());
}

void SauceProcessor::setParam (const String& id, float realValue)
{
    if (auto* r = apvts.getParameter (id))
        r->setValueNotifyingHost (r->convertTo0to1 (realValue));
    else
        jassertfalse;
}

bool SauceProcessor::isValidChainOrder (const String& o)
{
    if (o.length() != 6) return false;
    for (auto c : defaultChainOrder()) if (o.indexOfChar (c) < 0) return false;
    return true;
}

bool SauceProcessor::setChainOrder (const String& order, bool markEdited)
{
    if (! isValidChainOrder (order)) return false;       // refuse bad orders, keep the current one
    const String o = order;
    const String letters = defaultChainOrder();
    uint32_t packed = 0;
    for (int i = 0; i < 6; ++i) packed |= (uint32_t) letters.indexOfChar (o[i]) << (4 * i);
    const bool changed = o != chainOrder;
    chainOrder = o;
    chainOrderPacked.store (packed, std::memory_order_release);
    if (changed && markEdited && ! loadingPreset.load()) presetDirty = true;
    if (changed) stateChanged.sendChangeMessage();
    return true;
}

void SauceProcessor::applyAutoKey()
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto est = analysis.getKeyEstimate();
    const bool on = reader.get (ParamIDs::autoKey) > 0.5f;
    if (! est.valid) { autoKeyStatus = on ? "Auto Key: listening..." : String(); return; }
    const String keyName = String (names[est.key]) + (est.minor ? " minor" : " major");
    autoKeyStatus = (on ? "Auto Key: " : "Detected: ") + keyName + "  (" + String (roundToInt (est.confidence * 100)) + "%)";
    if (! on || ! est.stable) return;
    const int scale = est.minor ? 1 : 0;
    if ((int) reader.get (ParamIDs::tuneKey) == est.key && (int) reader.get (ParamIDs::tuneScale) == scale) return;
    autoKeyApplying = true;
    setParam (ParamIDs::tuneKey, (float) est.key);
    setParam (ParamIDs::tuneScale, (float) scale);
    autoKeyApplying = false;
}

//==============================================================================
ValueTree SauceProcessor::captureFullState() const
{
    ValueTree root ("SAUCE_STATE");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (const_cast<AudioProcessorValueTreeState&> (apvts).copyState(), nullptr);
    root.setProperty ("presetName", presetName, nullptr);
    root.setProperty ("presetDirty", presetDirty, nullptr);
    root.setProperty ("genreFilter", genreFilter, nullptr);
    root.setProperty ("artistFilter", artistFilter, nullptr);
    String raw; for (float v : analysis.getAppliedRaw()) raw << String (v, 3) << ' ';
    root.setProperty ("matchApplied", raw.trim(), nullptr);
    root.setProperty ("matchCaptures", analysis.captureStateToString(), nullptr);
    root.setProperty ("chainOrder", chainOrder, nullptr);
    return root;
}

void SauceProcessor::restoreFullState (const ValueTree& root)
{
    if (! root.hasType ("SAUCE_STATE")) return;
    loadingPreset = true;
    if (auto params = root.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params.createCopy());
    presetName   = root.getProperty ("presetName", presetName).toString();
    presetDirty  = (bool) root.getProperty ("presetDirty", false);
    genreFilter  = root.getProperty ("genreFilter", "").toString();
    artistFilter = root.getProperty ("artistFilter", "").toString();
    std::vector<float> raw;
    for (auto& t : StringArray::fromTokens (root.getProperty ("matchApplied", "").toString(), " ", ""))
        if (t.isNotEmpty()) raw.push_back (t.getFloatValue());
    analysis.setAppliedRaw (raw);
    analysis.captureStateFromString (root.getProperty ("matchCaptures", "").toString());
    if (! setChainOrder (root.getProperty ("chainOrder", defaultChainOrder()).toString(), false)) setChainOrder (defaultChainOrder(), false);
    loadingPreset = false;
    undo.clearUndoHistory();
    stateChanged.sendChangeMessage();
}

void SauceProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = captureFullState().createXml()) copyXmlToBinary (*xml, dest);
}

void SauceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        restoreFullState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* SauceProcessor::createEditor() { return new SauceEditor (*this); }

#if ! SAUCE_TEST_BUILD
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SauceProcessor(); }
#endif
