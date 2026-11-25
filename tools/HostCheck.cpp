// Loads the BUILT VST3 bundle through JUCE's VST3 hosting code (the same path a DAW uses),
// then checks scanning, bus layouts, processing, latency, editor creation and state round-trip.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
using namespace juce;

static int fails = 0;
static void expect (bool ok, const String& what) { if (! ok) ++fails; std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: SauceHostCheck <path/to/The Sauce Suite.vst3>\n"); return 2; }
    VST3PluginFormat fmt; OwnedArray<PluginDescription> found;
    fmt.findAllTypesForFile (found, argv[1]);
    std::printf ("Host check: %s\n", argv[1]);
    expect (found.size() == 1, "VST3 scanned: " + (found.isEmpty() ? String ("nothing") : found[0]->name + " by " + found[0]->manufacturerName + ", " + found[0]->category));
    if (found.isEmpty()) return 1;
    String err;
    auto inst = fmt.createInstanceFromDescription (*found[0], 48000.0, 512, err);
    expect (inst != nullptr, "instantiated" + (err.isNotEmpty() ? " (" + err + ")" : String()));
    if (! inst) return 1;
    std::printf ("     buses: %d in / %d out, parameters: %d\n", inst->getBusCount (true), inst->getBusCount (false), inst->getParameters().size());
    expect (inst->getBusCount (true) == 2, "main input + optional sidechain input exposed");
    auto layout = inst->getBusesLayout();
    layout.inputBuses.getReference (1) = AudioChannelSet::stereo();
    expect (inst->setBusesLayout (layout), "host can enable the stereo sidechain");
    inst->prepareToPlay (48000.0, 512);
    std::printf ("     reported latency: %d samples\n", inst->getLatencySamples());
    expect (inst->getLatencySamples() > 0, "latency reported to the host");
    AudioBuffer<float> buf (4, 512); MidiBuffer midi; float outPk = 0;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float t = (float) (b * 512 + i) / 48000.0f;
            const float v = 0.3f * std::sin (6.2831853f * 220.0f * t) * (b < 100 ? 1.0f : 0.0f);
            buf.setSample (0, i, v); buf.setSample (1, i, v); buf.setSample (2, i, 0.0f); buf.setSample (3, i, 0.0f);
        }
        inst->processBlock (buf, midi);
        outPk = jmax (outPk, buf.getMagnitude (0, 0, 512));
    }
    expect (outPk > 0.01f && outPk <= 1.0f, "processes audio (output peak " + String (Decibels::gainToDecibels (outPk), 1) + " dBFS, limiter holds it under 0 dBFS)");
    // sidechain only -> silence (after tails from the first test fade)
    inst->reset();
    auto fresh = fmt.createInstanceFromDescription (*found[0], 48000.0, 512, err);
    fresh->setBusesLayout (layout); fresh->prepareToPlay (48000.0, 512);
    float leak = 0;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < 512; ++i) { buf.setSample (0, i, 0); buf.setSample (1, i, 0); const float s = 0.5f * std::sin (0.07f * (float) (b * 512 + i)); buf.setSample (2, i, s); buf.setSample (3, i, s); }
        fresh->processBlock (buf, midi);
        leak = jmax (leak, buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512));
    }
    expect (leak == 0.0f, "sidechain-only input produces a silent output in the host");
    // editor
    const bool skipEditor = argc > 2 && String (argv[2]) == "--no-editor";
    std::unique_ptr<AudioProcessorEditor> ed (skipEditor ? nullptr : inst->createEditorIfNeeded());
    if (skipEditor) std::printf ("  [SKIP] editor (no GPU in this environment)\n");
    else
    expect (ed != nullptr && ed->getWidth() > 600, "editor opens (" + (ed ? String (ed->getWidth()) + "x" + String (ed->getHeight()) : String ("none")) + ")");
    if (ed) { ed->setSize (1720, 920); expect (ed->getWidth() == 1720, "editor resizes"); }
    ed.reset();
    // state round trip through the VST3 wrapper
    // move several parameters to legal (on-grid) values, as a user or automation would
    for (int i : { 5, 12, 20, 40, 60 })
        if (auto* rp = dynamic_cast<HostedAudioProcessorParameter*> (inst->getParameters()[i]))
            rp->setValueNotifyingHost (rp->getValueForText (rp->getText (0.37f + 0.1f * (float) (i % 3), 64)));
    MemoryBlock mb; inst->getStateInformation (mb);
    auto other = fmt.createInstanceFromDescription (*found[0], 48000.0, 512, err);
    other->setStateInformation (mb.getData(), (int) mb.getSize());
    int mism = 0;
    for (int i = 0; i < inst->getParameters().size(); ++i)
        if (std::abs (inst->getParameters()[i]->getValue() - other->getParameters()[i]->getValue()) > 1e-5f)
        { ++mism; std::printf ("     mismatch: %s %f vs %f\n", inst->getParameters()[i]->getName (64).toRawUTF8(), inst->getParameters()[i]->getValue(), other->getParameters()[i]->getValue()); }
    expect (mism == 0, "state saved and restored through the VST3 wrapper (" + String (inst->getParameters().size()) + " parameters, " + String (mism) + " mismatches)");
    inst->releaseResources();
    std::printf ("%s\n", fails == 0 ? "HOST CHECK PASSED" : "HOST CHECK FAILED");
    return fails == 0 ? 0 : 1;
}
