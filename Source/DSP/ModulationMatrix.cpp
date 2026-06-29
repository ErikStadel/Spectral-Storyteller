#include "ModulationMatrix.h"

juce::ValueTree ModulationMatrix::toValueTree() const
{
    juce::ValueTree root("ModMatrix");
    for (int i = 0; i < NUM_LFOS; ++i)
    {
        juce::ValueTree n("LFO");
        n.setProperty("idx", i, nullptr);
        n.setProperty("rate", lfos[i].rateIndex.load(), nullptr);
        n.setProperty("shape", lfos[i].shape.load(), nullptr);
        n.setProperty("amount", lfos[i].amount.load(), nullptr);
        n.setProperty("phaseOffset", lfos[i].phaseOffset.load(), nullptr);
        Target t; { juce::ScopedLock sl(lfos[i].targetLock); t = lfos[i].target; }
        n.setProperty("tObj", t.objectId, nullptr);
        n.setProperty("tFx",  t.fxName, nullptr);
        n.setProperty("tP",   t.paramName, nullptr);
        root.appendChild(n, nullptr);
    }
    for (int i = 0; i < NUM_XY; ++i)
    {
        juce::ValueTree n("XY");
        n.setProperty("idx", i, nullptr);
        n.setProperty("x", xys[i].x.load(), nullptr);
        n.setProperty("y", xys[i].y.load(), nullptr);
        Target tx, ty;
        { juce::ScopedLock sl(xys[i].targetLock); tx = xys[i].targetX; ty = xys[i].targetY; }
        n.setProperty("xObj", tx.objectId, nullptr); n.setProperty("xFx", tx.fxName, nullptr); n.setProperty("xP", tx.paramName, nullptr);
        n.setProperty("yObj", ty.objectId, nullptr); n.setProperty("yFx", ty.fxName, nullptr); n.setProperty("yP", ty.paramName, nullptr);
        root.appendChild(n, nullptr);
    }
    return root;
}

void ModulationMatrix::fromValueTree(const juce::ValueTree& root)
{
    if (! root.isValid()) return;
    for (auto n : root)
    {
        if (n.hasType("LFO"))
        {
            const int i = juce::jlimit(0, NUM_LFOS-1, (int) n["idx"]);
            lfos[i].rateIndex.store((int) n["rate"]);
            lfos[i].shape.store((int) n["shape"]);
            lfos[i].amount.store((float) n["amount"]);
            lfos[i].phaseOffset.store((float) n.getProperty("phaseOffset", 0.0f));
            juce::ScopedLock sl(lfos[i].targetLock);
            lfos[i].target = { (int) n["tObj"], n["tFx"].toString(), n["tP"].toString() };
        }
        else if (n.hasType("XY"))
        {
            const int i = juce::jlimit(0, NUM_XY-1, (int) n["idx"]);
            xys[i].x.store((float) n["x"]);
            xys[i].y.store((float) n["y"]);
            juce::ScopedLock sl(xys[i].targetLock);
            xys[i].targetX = { (int) n["xObj"], n["xFx"].toString(), n["xP"].toString() };
            xys[i].targetY = { (int) n["yObj"], n["yFx"].toString(), n["yP"].toString() };
        }
    }
}
