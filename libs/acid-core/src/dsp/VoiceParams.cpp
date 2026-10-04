#include "acid/dsp/Voice.h"

namespace acid::dsp
{
VoiceParams voiceParamsFrom (const ParamValues& values) noexcept
{
    const auto f = [&values] (ParamId id) noexcept { return static_cast<double> (values[static_cast<std::size_t> (id)]); };
    const auto b = [&values] (ParamId id) noexcept { return paramAsBool (values, id); };
    const auto i = [&values] (ParamId id) noexcept { return paramAsInt (values, id); };

    VoiceParams p;

    p.wave = i (kWave);
    p.tuneExt = b (kTuneExt);
    p.tune = f (kTune);
    p.tuneCoarse = i (kTuneCoarse);
    p.tuneFine = f (kTuneFine);
    p.cutoff = f (kCutoff);
    p.resonance = f (kResonance);
    p.envMod = f (kEnvMod);
    p.decay = f (kDecay);
    p.accent = f (kAccent);
    p.slideTime = f (kSlideTime);
    p.sweepCouple = b (kSweepCouple);
    p.sweepAmt = f (kSweepAmt);
    p.envAttack = f (kEnvAttack);
    p.accDecay = f (kAccDecay);
    p.accVolCouple = b (kAccVolCouple);
    p.accVol = f (kAccVol);
    p.vibSpeed = f (kVibSpeed);
    p.vibDepth = f (kVibDepth);

    p.calTm3 = f (kCalTm3);
    p.calC21 = f (kCalC21);
    p.calAccEnv = f (kCalAccEnv);
    p.calAccAmp = f (kCalAccAmp);
    p.calC13 = f (kCalC13);
    p.calTm5 = f (kCalTm5);
    p.calPw = f (kCalPw);
    p.calVcaClicks = b (kCalVcaClicks);
    p.calVcaNoise = f (kCalVcaNoise);
    p.calRb = f (kCalRb);
    p.calKmax = f (kCalKmax);
    p.calCvScale = f (kCalCvScale);
    p.calGateDelay = f (kCalGateDelay);
    p.calDrift = f (kCalDrift);
    p.calCpuJitter = b (kCalCpuJitter);

    p.modOn = b (kModOn);
    p.modOverdrive = f (kModOverdrive);
    p.modFilterFm = f (kModFilterFm);
    p.modTracking = f (kModTracking);
    p.modAmpDecay = f (kModAmpDecay);
    p.modAmpSustain = f (kModAmpSustain);
    p.modSoftAttack = f (kModSoftAttack);
    p.modHiRes = b (kModHiRes);
    p.modSweepSpeed = i (kModSweepSpeed);
    p.modMuffler = i (kModMuffler);
    p.modExtRange = b (kModExtRange);
    p.modSubLevel = f (kModSubLevel);
    p.modSubOct = i (kModSubOct);

    return p;
}
}
