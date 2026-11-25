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
    sendSlot.store (-1); receiveSlot.store (-1);
    SauceLink::get().closeSlot (linkSendSlot);
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
    if (const int ss = sendSlot.load(); ss >= 0)
    {
        // Track Link sender: share this track's audio with other instances and pass it through untouched
        auto mainBus = getBusBuffer (buffer, true, 0);
        if (mainBus.getNumChannels() > 0)
            SauceLink::get().write (ss, mainBus.getArrayOfReadPointers(), jmin (2, mainBus.getNumChannels()), 0, n);
        for (int c = mainBus.getNumChannels(); c < getTotalNumOutputChannels(); ++c)
            if (c < buffer.getNumChannels()) buffer.clear (c, 0, n);
        return;
    }
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
    // Track Link: a chosen sender replaces the DAW sidechain (no routing needed)
    bool linked = false;
    if (const int rs = receiveSlot.load(); rs >= 0)
    {
        if (linkReader.slot != rs) { linkReader = {}; linkReader.slot = rs; }
        linked = SauceLink::get().read (linkReader, scMono.data(), n);
        scPresent = scPresent || linked;
    }
    linkReceiving.store (linked);
    if (! scPresent) std::fill (scMono.begin(), scMono.begin() + n, 0.0f);
    sidechainConnected.store (scPresent);

    auto p = reader.read (hostBpm.load());
    { const uint64_t packed = chainOrderPacked.load (std::memory_order_acquire);
      for (int i = 0; i < sauce::kNumModules; ++i) p.order[(size_t) i] = (uint8_t) ((packed >> (4 * i)) & 0xF); }
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
    meters.deessGrDb.store (m.deessGrDb); meters.popGrDb.store (m.popGrDb);
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

String SauceProcessor::upgradeChainOrder (const String& o)
{
    // 1.4 orders had six modules (no plosive filter or de-esser; delay, reverb and limiter fixed at the end).
    // The plosive filter goes right after the gate and the de-esser right after the compressor.
    if (o.length() == 6 && o.containsOnly ("GTEMCS"))
    {
        String u = o;
        u = u.substring (0, u.indexOfChar ('G') + 1) + "P" + u.substring (u.indexOfChar ('G') + 1);
        u = u.substring (0, u.indexOfChar ('C') + 1) + "D" + u.substring (u.indexOfChar ('C') + 1);
        return u + "YRL";
    }
    return o;
}

bool SauceProcessor::isValidChainOrder (const String& o)
{
    if (o.length() != sauce::kNumModules) return false;
    for (auto c : defaultChainOrder()) if (o.indexOfChar (c) < 0) return false;
    return true;
}

bool SauceProcessor::setChainOrder (const String& order, bool markEdited)
{
    const String o = upgradeChainOrder (order);
    if (! isValidChainOrder (o)) return false;           // refuse bad orders, keep the current one
    const String letters (sauce::kModuleLetters);
    uint64_t packed = 0;
    for (int i = 0; i < sauce::kNumModules; ++i) packed |= (uint64_t) letters.indexOfChar (o[i]) << (4 * i);
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
    root.setProperty ("linkSend", linkSendMode, nullptr);
    root.setProperty ("roleOverride", roleOverride, nullptr);
    root.setProperty ("linkSource", linkSourceName, nullptr);
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
    roleOverride = (int) root.getProperty ("roleOverride", -1);
    updateRole();
    if (root.hasProperty ("linkSend")) setLinkSend ((int) root.getProperty ("linkSend", 0));
    if (root.hasProperty ("linkSource")) setLinkSource (root.getProperty ("linkSource", "").toString());
    undo.clearUndoHistory();
    stateChanged.sendChangeMessage();
}

//==============================================================================
void SauceProcessor::updateRole()
{
    auto fromText = [] (const String& raw) -> int
    {
        const String t = raw.toLowerCase();
        if (t.contains ("ad-lib") || t.contains ("adlib") || t.contains ("ad lib") || t.contains ("adl")) return 2;
        if (t.contains ("background") || t.contains ("backing") || t.contains ("bgv") || t.contains ("bg ") || t.endsWith ("bg") || t.contains (" bv")) return 3;
        if (t.contains ("harmon") || t.contains ("harm ") || t.endsWith ("harm") || t.contains ("stack")) return 4;
        if (t.contains ("double") || t.contains ("dbl") || t.contains ("dub ")) return 5;
        if (t.contains ("hook") || t.contains ("chorus")) return 1;
        if (t.contains ("lead") || t.contains ("main vox") || t.contains ("verse")) return 0;
        return -1;
    };
    int r = -1; String why;
    if (roleOverride >= 0) { r = roleOverride; why = "set by you"; }
    if (r < 0) { const String tn = getTrackName(); r = fromText (tn); if (r >= 0) why = "from the track name \"" + tn + "\""; }
    if (r < 0) { r = fromText (presetName); if (r >= 0) why = "from the preset"; }
    if (r < 0)
    {
        // settings: a high low-cut plus width/doubler and a lower level reads as a supporting part
        const bool stereo = reader.get (ParamIDs::dblOn) > 0.5f || reader.get (ParamIDs::wideOn) > 0.5f;
        const float hp = reader.get (ParamIDs::band (0, "on")) > 0.5f && (int) reader.get (ParamIDs::band (0, "type")) == 3 ? reader.get (ParamIDs::band (0, "freq")) : 20.0f;
        if (hp >= 220.0f && stereo && reader.get (ParamIDs::outGain) < -2.0f) { r = 3; why = "from the settings (high low-cut, wide, lower level)"; }
        else if (hp >= 300.0f) { r = 2; why = "from the settings (thin, filtered sound)"; }
        else { r = 0; why = "default"; }
    }
    currentRole.store (r);
    roleReason = why;
}

String SauceProcessor::linkDisplayName() const
{
    const String t = getTrackName();
    if (t.isNotEmpty()) return t;
    return "Sauce Suite " + String (linkSendSlot + 1);
}

void SauceProcessor::setLinkSend (int mode)
{
    mode = jlimit (0, 2, mode);
    if (mode == linkSendMode && (mode == LinkOff || linkSendSlot >= 0)) return;
    auto& link = SauceLink::get();
    if (mode == LinkOff)
    {
        sendSlot.store (-1);
        link.closeSlot (linkSendSlot); linkSendSlot = -1;
        linkSendMode = LinkOff;
        setLatencySamples (chain.getLatency());
    }
    else
    {
        if (linkSendSlot < 0) linkSendSlot = link.openSlot ({}, mode);
        linkSendMode = linkSendSlot >= 0 ? mode : LinkOff;
        link.rename (linkSendSlot, linkDisplayName(), mode);
        setLinkSource ({});                                   // a sender doesn't listen to anything itself
        setLatencySamples (0);                                // pass-through: no delay on the beat track
        sendSlot.store (linkSendSlot);
    }
    stateChanged.sendChangeMessage();
}

void SauceProcessor::setLinkSource (const String& name)
{
    linkSourceName = name;
    receiveSlot.store (name.isEmpty() ? -1 : SauceLink::get().findByName (name));
    stateChanged.sendChangeMessage();
}

void SauceProcessor::refreshLink()
{
    auto& link = SauceLink::get();
    if (linkSendSlot >= 0) link.rename (linkSendSlot, linkDisplayName(), linkSendMode);
    if (linkSourceName.isNotEmpty())
    {
        const int s = link.findByName (linkSourceName);
        if (s != receiveSlot.load()) receiveSlot.store (s);
    }
}

void SauceProcessor::updateTrackProperties (const TrackProperties& props)
{
    const SpinLock::ScopedLockType l (trackNameLock);
    if (props.name.has_value()) trackName = *props.name;
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
