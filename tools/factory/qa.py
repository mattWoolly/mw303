#!/usr/bin/env python3
"""QA for the mw12F factory library: builds it, checks every file, renders and measures it.

    python3 tools/factory/qa.py               build, verify, render, measure, report
    python3 tools/factory/qa.py --calibrate   measure the level of synth presets, scenes and the
                                              effect presets that have an output trim, rewrite
                                              levels.tsv, rebuild, and repeat until the trims
                                              settle; then run every check
    python3 tools/factory/qa.py --quick       skip the pattern and effect renders

Level is measured on both channels: the RMS of L and R together (mean of L^2 and R^2), in
dBFS. Peaks are true peaks (4x oversampled), in dBTP. Effect presets are compared with the
sound they are applied to in integrated loudness (ITU-R BS.1770-4, LUFS), because an EQ or a
distortion changes the spectrum and RMS alone would not say how loud it sounds.

Checks (a failure makes the exit status 1):
  files      factory-build --verify: every file parses with zero adjustments and zero ignored
             items, writes back to the same bytes, and a distortion model carries its voicing
  text       the branding audit, no em dashes, the minimum count of every type
  synth      each preset on three lines (SYNTH_LINES: the reference line, an all-accent anthem
             line and a sparse line, each with its own target): the mean distance from the
             targets within TOLERANCE, each line within LINE_TOLERANCE of its target, the true
             peak of every line at or under
             PEAK_LIMIT unless its limiter is on (then under 0 dBTP), never silent, no more
             than MONO_LOSS dB lost when summed to mono
  neighbour  each synth preset on the reference line against every other: the distance
             between their 40-band log spectrograms (level removed) at least NEIGHBOUR_MIN dB,
             unless the pair is in NEIGHBOUR_ALLOW
  scene      A1 at the scene's tempo: RMS within TOLERANCE of the target, the peak rule, the
             mono rule; every other used slot not silent, under the peak rule and within
             SLOT_WINDOW of A1 (BREAKDOWN_WINDOW for breakdowns, half time and builds, which
             thin the line out on purpose); the MIDI output of A1 has the accents and slides
             its steps call for
  pattern    each pattern file over a neutral sound at its tempo: not silent, no clip, MIDI
             events as written
  effect     each effect preset over EFFECT_BASE on the reference line: integrated loudness
             within EFFECT_WINDOW LU of EFFECT_BASE alone; calibration units (which have no
             output trim and change the voice itself) within CAL_WINDOW; nothing clips
Measurements also reported: spectral centroid, how much brighter accented notes are than
plain ones, and the prominence of the resonant peak in the first 60 ms of each note. Results
go to build-factory/qa/: report.txt, results.json, measurements.tsv, neighbours.tsv and PNGs
(spectrograms of a sample of scenes, the level map).
"""
import argparse
import concurrent.futures as cf
import json
import math
import os
import struct
import subprocess
import sys

import numpy as np
import scipy.io.wavfile as wavfile
import scipy.signal as signal

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.path.join(ROOT, "build-factory")
RENDER = os.path.join(BUILD, "tools", "acid-render", "acid-render")
FACTORY = os.path.join(BUILD, "tools", "factory", "factory-build")
LIBRARY = os.path.join(ROOT, "assets", "factory")
LEVELS = os.path.join(ROOT, "tools", "factory", "levels.tsv")
OUT = os.path.join(BUILD, "qa")

# The lines every synth preset is measured on, in C: (name, steps, BPM). The reference line is
# the first-load line (three accents in sixteen, three slides, a tie, an octave jump, a rest);
# the anthem line has an accent on every step; the sparse line is low, with rests and ties.
REFERENCE_LINE = "C C C+:A C D#:S F C:A . C C+:S C:A A#- C ~ G-:S C"
ANTHEM_LINE = "C:A C:A C:A C:AS C:A C:A C:A C:AS C:AS C:A C:A C:A C:A C:AS C:AS C:A"
SPARSE_LINE = "C ~ . . . . C:A . . . G- ~ . . C:S D#"
# (name, steps, BPM, offset): the offset is the line's own target against TARGET_RMS. A line
# with an accent on every step is denser, and a sparse line holds more silence, so the same
# note level reads differently in RMS; the offsets are the library's median per line (measured
# once, then fixed here so they do not drift with the presets).
SYNTH_LINES = [("ref", REFERENCE_LINE, 125, 0.0), ("anthem", ANTHEM_LINE, 130, 2.0), ("sparse", SPARSE_LINE, 120, -4.0)]
LINE_OFFSET = {name: off for name, _, _, off in SYNTH_LINES}
REFERENCE_BPM = 125
REFERENCE_BARS = 4
NEUTRAL_SOUND = "synth/rubber-floor.mw12f"      # patterns are played through this
EFFECT_BASE = "synth/init-line.mw12f"           # effect and calibration presets are applied over this

TARGET_RMS = -24.0      # dBFS RMS over both channels
TOLERANCE = 1.5         # +- dB: synth presets (mean of the three lines) and scene A1s
LINE_TOLERANCE = 3.0    # +- dB: each synth line on its own
PEAK_LIMIT = -1.0       # dBTP, unless the preset's limiter is on
SILENT = -45.0          # dBFS RMS: below this a render counts as silent
CAL_MARGIN = 0.3        # dB kept under the peak limit when calibrating
MONO_LOSS = 3.0         # dB a synth preset or scene may lose when summed to mono
EFFECT_WINDOW = 1.0     # +- LU: an effect preset against the sound without it
CAL_WINDOW = 8.0        # +- LU: a calibration unit against the reference unit
SLOT_WINDOW = (-3.0, 3.0)           # dB: a scene's other slots against its A1
BREAKDOWN_WINDOW = (-10.0, 3.0)     # dB: breakdowns, half time and builds, which thin the line out
THINNING = ("Breakdown", "Half Time", "Build")
# dB: smallest spectrogram distance between two synth presets. For scale, on this measure a
# 10-point resonance move reads 1.3 to 1.6 dB and a 5-point cutoff move 0.9 to 1.6 dB, so two
# presets have to differ by clearly more than one small knob move.
NEIGHBOUR_MIN = 2.0
# Pairs that may sit closer than NEIGHBOUR_MIN, with the reason. Empty: none needed.
NEIGHBOUR_ALLOW = {}
CAL_ROUNDS = 8          # most calibration rounds before giving up
CAL_SETTLED = 0.15      # dB: calibration stops when no trim moves by more than this

MINIMUM_COUNTS = {"scene": 100, "synth": 120, "pattern": 250, "arp": 48, "calibration": 12, "distortion": 8,
                  "eq": 8, "delay": 8, "reverb": 8, "chorus": 8, "limiter": 8}
MINIMUM_TOTAL = 800
EFFECT_TYPES = ("distortion", "eq", "delay", "reverb", "chorus", "limiter")


# ---- Helpers ------------------------------------------------------------------------------

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def db(x):
    return 20.0 * math.log10(max(float(x), 1e-12))


def read_levels():
    trims = {}
    if os.path.exists(LEVELS):
        for line in open(LEVELS):
            if line.startswith("#") or not line.strip():
                continue
            k, v = line.rstrip("\n").split("\t")
            trims[k] = float(v)
    return trims


def write_levels(trims):
    with open(LEVELS, "w") as f:
        f.write("# Level trims in dB, measured by tools/factory/qa.py --calibrate. Synth presets and scenes:\n")
        f.write("# added to master_volume (target %.1f dBFS RMS over both channels; synth presets on the mean of\n" % TARGET_RMS)
        f.write("# the %s lines, scenes on A1 at their tempo). chorus/, distortion/ and limiter/ keys: added to\n"
                % ", ".join(n for n, _, _, _ in SYNTH_LINES))
        f.write("# cho_volume, dist_volume and lim_gain, so the preset matches the sound without it in loudness.\n")
        for k in sorted(trims):
            f.write("%s\t%.1f\n" % (k, trims[k]))


def build_library():
    os.makedirs(OUT, exist_ok=True)
    r = run([FACTORY, "--out", LIBRARY, "--levels", LEVELS, "--manifest", os.path.join(OUT, "manifest.json"),
             "--rejects", os.path.join(OUT, "generator-rejects.txt"), "--prune"])
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        raise SystemExit("factory-build failed")
    return json.load(open(os.path.join(OUT, "manifest.json")))["entries"]


# ---- MIDI ---------------------------------------------------------------------------------

def read_varlen(data, p):
    v = 0
    while True:
        b = data[p]
        p += 1
        v = (v << 7) | (b & 0x7F)
        if b < 0x80:
            return v, p


def read_midi(path):
    """Notes of a Standard MIDI File as (start_tick, end_tick, key, velocity), and the resolution."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise ValueError("not a MIDI file")
    hlen = struct.unpack(">I", data[4:8])[0]
    _, ntracks, division = struct.unpack(">HHH", data[8:14])
    pos = 8 + hlen
    notes = []
    for _ in range(ntracks):
        tlen = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        p, end, tick, status, held = pos + 8, pos + 8 + tlen, 0, 0, {}
        while p < end:
            delta, p = read_varlen(data, p)
            tick += delta
            b = data[p]
            if b == 0xFF:
                length, p = read_varlen(data, p + 2)
                p += length
                continue
            if b in (0xF0, 0xF7):
                length, p = read_varlen(data, p + 1)
                p += length
                continue
            if b & 0x80:
                status = b
                p += 1
            kind = status & 0xF0
            n = 1 if kind in (0xC0, 0xD0) else 2
            d = data[p:p + n]
            p += n
            if kind == 0x90 and d[1] > 0:
                held.setdefault(d[0], []).append((tick, d[1]))
            elif kind == 0x80 or (kind == 0x90 and d[1] == 0):
                if held.get(d[0]):
                    start, vel = held[d[0]].pop(0)
                    notes.append((start, tick, d[0], vel))
        pos = end
    notes.sort()
    return division, notes


STEP_BEATS = {"1/64": 1 / 16, "1/32": 1 / 8, "1/16": 0.25, "1/8": 0.5, "1/4": 1.0, "1/2": 2.0, "1/1": 4.0}


def step_beats(pat):
    b = STEP_BEATS[pat["div"]]
    if pat["mod"] == "dotted":
        b *= 1.5
    elif pat["mod"] == "triplet":
        b *= 2.0 / 3.0
    return b


def expected_events(pat, beats):
    """What the sequencer should send for a pattern played from its first step for `beats`:
    note-ons and accented note-ons (as ranges, since a slide into the same pitch may merge),
    and slides between different pitches (heard as overlapping notes)."""
    steps = pat["steps"]
    n = int(math.ceil(beats / step_beats(pat) - 1e-9))
    lo = hi = acc_lo = acc_hi = slides = 0
    uncertain = False
    last = None   # (pitch, slide) of the sounding note, carried over ties
    for k in range(n):
        state, pitch, accent, slide, ratchet, prob, _ = steps[k % len(steps)]
        if prob < 100:
            uncertain = True
        if state == "N":
            merged = last is not None and last[1] and last[0] == pitch
            if last is not None and last[1] and last[0] != pitch:
                slides += 1
            hi += ratchet
            acc_hi += ratchet * accent
            lo += ratchet - (1 if merged else 0)
            acc_lo += (ratchet - (1 if merged else 0)) * accent
            last = (pitch, slide)
        elif state == "R":
            last = None
    return {"notes": (lo, hi), "accents": (acc_lo, acc_hi), "slides": slides, "uncertain": uncertain}


def measured_events(midi_path, accent_velocity=96):
    division, notes = read_midi(midi_path)
    ons = len(notes)
    accents = sum(1 for n in notes if n[3] >= accent_velocity)
    # A slide holds the old note until the new one starts (the renderer overlaps them by one
    # sample, which the file's tick grid may round to touching); a plain note ends earlier.
    slides = sum(1 for a, b in zip(notes, notes[1:]) if b[0] <= a[1] and b[2] != a[2])
    return {"notes": ons, "accents": accents, "slides": slides}


def compare_events(exp, got):
    problems = []
    if exp["uncertain"]:
        return problems
    if not exp["notes"][0] <= got["notes"] <= exp["notes"][1]:
        problems.append("notes %d, expected %d..%d" % (got["notes"], *exp["notes"]))
    if not exp["accents"][0] <= got["accents"] <= exp["accents"][1]:
        problems.append("accents %d, expected %d..%d" % (got["accents"], *exp["accents"]))
    if got["slides"] != exp["slides"]:
        problems.append("slides %d, expected %d" % (got["slides"], exp["slides"]))
    return problems


# ---- Audio analysis -----------------------------------------------------------------------

def load(path):
    rate, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim == 1:
        x = x[:, None]
    return rate, x


def k_weighting(rate):
    """The two BS.1770 K-weighting biquads (high shelf, then high pass) for any sample rate."""
    f0, gain, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = math.tan(math.pi * f0 / rate)
    vh = 10.0 ** (gain / 20.0)
    vb = vh ** 0.4996667741545416
    a0 = 1.0 + k / q + k * k
    shelf = ([(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0],
             [1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0])
    f0, q = 38.13547087602444, 0.5003270373238773
    k = math.tan(math.pi * f0 / rate)
    a0 = 1.0 + k / q + k * k
    highpass = ([1.0, -2.0, 1.0], [1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0])
    return shelf, highpass


def integrated_loudness(x, rate):
    """ITU-R BS.1770-4 integrated loudness of a stereo signal, in LUFS (gated, 400 ms blocks)."""
    shelf, highpass = k_weighting(rate)
    y = signal.lfilter(shelf[0], shelf[1], x, axis=0)
    y = signal.lfilter(highpass[0], highpass[1], y, axis=0)
    block, hop = int(0.4 * rate), int(0.1 * rate)
    if len(y) < block:
        return -120.0
    sq = y ** 2
    csum = np.concatenate([np.zeros((1, sq.shape[1])), np.cumsum(sq, axis=0)])
    starts = np.arange(0, len(y) - block + 1, hop)
    z = (csum[starts + block] - csum[starts]) / block           # mean square per block and channel
    power = z.sum(axis=1)
    loud = -0.691 + 10.0 * np.log10(np.maximum(power, 1e-20))
    keep = loud > -70.0
    if not keep.any():
        return -120.0
    relative = -0.691 + 10.0 * math.log10(power[keep].mean()) - 10.0
    keep &= loud > relative
    if not keep.any():
        return -120.0
    return -0.691 + 10.0 * math.log10(power[keep].mean())


def true_peak(x):
    """Highest sample of the signal oversampled 4x, as a linear value."""
    return float(np.abs(signal.resample_poly(x, 4, 1, axis=0)).max())


NB_BANDS = 40
NB_LO, NB_HI = 40.0, 16000.0
NB_FLOOR = -90.0        # dB, per band, after the level is removed


def band_spectrogram(x, rate):
    """40-band log-frequency spectrogram in dB (both channels' power summed), with the
    render's own RMS level removed, so two presets compare by timbre and articulation."""
    f, _, z = signal.stft(x.T, fs=rate, nperseg=2048, noverlap=1024)
    power = (np.abs(z) ** 2).sum(axis=0)                        # bins x frames
    edges = np.geomspace(NB_LO, NB_HI, NB_BANDS + 1)
    idx = np.digitize(f, edges) - 1
    bands = np.zeros((NB_BANDS, power.shape[1]))
    for b in range(NB_BANDS):
        sel = idx == b
        if sel.any():
            bands[b] = power[sel].sum(axis=0)
    level = np.mean(x ** 2)
    out = 10.0 * np.log10(np.maximum(bands, 1e-20)) - 10.0 * math.log10(max(level, 1e-20))
    return np.maximum(out, NB_FLOOR).astype(np.float32)


def analyse(path, midi_path=None, bpm=120.0, bands=False):
    rate, x = load(path)
    mono = x.mean(axis=1)
    res = {"rms": db(np.sqrt(np.mean(x ** 2))), "rms_mono": db(np.sqrt(np.mean(mono ** 2))),
           "peak": db(np.abs(x).max()), "tp": db(true_peak(x)), "lufs": integrated_loudness(x, rate)}
    if bands:
        res["bands"] = band_spectrogram(x, rate)

    f, _, z = signal.stft(mono, fs=rate, nperseg=2048, noverlap=1536)
    mag = np.abs(z)
    energy = (mag ** 2).sum(axis=0)
    loud = energy > energy.max() * 1e-5 if energy.max() > 0 else np.zeros_like(energy, bool)
    if loud.any():
        cen = (mag[:, loud] * f[:, None]).sum(axis=0) / np.maximum(mag[:, loud].sum(axis=0), 1e-12)
        res["centroid"] = float(np.average(cen, weights=energy[loud]))
    else:
        res["centroid"] = 0.0

    # Per note, over the first 60 ms: the centroid (accented against plain notes) and the
    # resonant peak. The peak is read off the harmonics: each harmonic's level is divided by
    # the saw's own 1/k slope, so a flat line means no resonance, and the prominence is the
    # highest harmonic above the third against the median of the harmonics up to it.
    if midi_path and os.path.exists(midi_path):
        division, notes = read_midi(midi_path)
        samples_per_tick = rate * 60.0 / (bpm * division)
        acc, plain, prom, peak_hz_acc, peak_hz_plain = [], [], [], [], []
        win = int(0.06 * rate)
        hann = np.hanning(win)
        freqs = np.fft.rfftfreq(win, 1 / rate)
        band = (freqs > 30) & (freqs < 12000)
        for start, _, key, vel in notes:
            s = int(start * samples_per_tick + 0.003 * rate)
            seg = mono[s:s + win]
            if len(seg) < win or np.sqrt(np.mean(seg ** 2)) < 1e-4:
                continue
            spec = np.abs(np.fft.rfft(seg * hann))
            c = float((spec[band] * freqs[band]).sum() / max(spec[band].sum(), 1e-12))
            (acc if vel >= 96 else plain).append(c)
            f0 = 440.0 * 2 ** ((key - 69) / 12.0)
            kmax = int(min(40, 8000 / f0))
            if kmax < 6:
                continue
            levels = []
            for k in range(1, kmax + 1):
                sel = np.abs(freqs - k * f0) <= f0 * 0.3
                levels.append(db(spec[sel].max() * k) if sel.any() else -240.0)
            levels = np.array(levels)
            top = int(np.argmax(levels[3:])) + 3
            prom.append(float(levels[top] - np.median(levels[:top + 1])))
            (peak_hz_acc if vel >= 96 else peak_hz_plain).append((top + 1) * f0)
        if acc and plain:
            res["accent_brightness"] = float(np.median(acc) / max(np.median(plain), 1e-9))
        if prom:
            res["res_peak_db"] = float(np.median(prom))
        if peak_hz_acc and peak_hz_plain:
            res["res_peak_hz_accent"] = float(np.median(peak_hz_acc))
            res["res_peak_hz_plain"] = float(np.median(peak_hz_plain))
    return res


# ---- Render jobs --------------------------------------------------------------------------

def render(job):
    """job: dict with key, presets (paths relative to the library), bpm, bars, slot, steps, midi."""
    wav = os.path.join(OUT, "wav", job["key"].replace("/", "__").replace("#", "__") + ".wav")
    mid = wav[:-4] + ".mid" if job.get("midi") else None
    cmd = [RENDER, "--quiet", "--bpm", "%g" % job["bpm"], "--bars", "%g" % job["bars"], "--out", wav]
    for p in job["presets"]:
        cmd += ["--preset", os.path.join(LIBRARY, p)]
    if job.get("slot"):
        cmd += ["--slot", job["slot"]]
    if job.get("steps"):
        cmd += ["--steps", job["steps"]]
    if mid:
        cmd += ["--midi-out", mid]
    r = run(cmd)
    if r.returncode != 0:
        return job, {"error": (r.stderr or r.stdout).strip()}
    res = analyse(wav, mid, job["bpm"], bands=job.get("bands", False))
    if mid:
        res["midi"] = measured_events(mid)
    res["wav"] = wav
    return job, res


def loop_bars(pat):
    return len(pat["steps"]) * step_beats(pat) / 4.0


def make_jobs(entries, quick, levels_only=False):
    """Render jobs. levels_only keeps what calibration measures: synth presets, scenes and
    (unless quick) the effect presets."""
    jobs = []
    for e in entries:
        t, path = e["type"], e["path"]
        if t == "synth":
            for line, steps, bpm, _ in SYNTH_LINES:
                ref = line == "ref"
                jobs.append({"key": path if ref else "%s#%s" % (path, line), "kind": "synth", "line": line, "presets": [path],
                             "bpm": bpm, "bars": REFERENCE_BARS, "steps": steps, "midi": ref, "bands": ref, "entry": e})
        elif t == "scene":
            for pat in e["seq"]:
                slot = "%s%d" % ("ABCD"[pat["slot"] // 12], pat["slot"] % 12 + 1)
                main = pat["slot"] == 0
                bars = max(4.0, min(8.0, 2 * loop_bars(pat))) if main else max(2.0, min(4.0, loop_bars(pat)))
                jobs.append({"key": "%s@%s" % (path, slot), "kind": "scene" if main else "slot", "presets": [path],
                             "bpm": e["bpm"], "bars": bars, "slot": slot, "midi": main and not levels_only, "entry": e,
                             "pattern": pat})
        elif t == "pattern" and not quick and not levels_only:
            pat = e["pattern"]
            bars = max(2.0, min(4.0, loop_bars(pat)))
            jobs.append({"key": path, "kind": "pattern", "presets": [NEUTRAL_SOUND, path], "bpm": e["bpm"], "bars": bars,
                         "slot": "A1", "midi": True, "entry": e, "pattern": pat})
        elif (t in EFFECT_TYPES or t == "calibration") and not quick:
            jobs.append({"key": path, "kind": "effect", "presets": [EFFECT_BASE, path], "bpm": REFERENCE_BPM,
                         "bars": REFERENCE_BARS, "steps": REFERENCE_LINE, "entry": e})
    if not quick:
        jobs.append({"key": "effect-base", "kind": "effect-base", "presets": [EFFECT_BASE], "bpm": REFERENCE_BPM,
                     "bars": REFERENCE_BARS, "steps": REFERENCE_LINE})
    return jobs


def render_all(jobs):
    os.makedirs(os.path.join(OUT, "wav"), exist_ok=True)
    results = {}
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for job, res in ex.map(render, jobs):
            results[job["key"]] = (job, res)
    return results


# ---- Checks -------------------------------------------------------------------------------

def synth_groups(results):
    """Synth renders grouped by preset path: {path: {line: (job, res)}}."""
    groups = {}
    for job, res in results.values():
        if job["kind"] == "synth":
            groups.setdefault(job["entry"]["path"], {})[job["line"]] = (job, res)
    return groups


def line_target(line):
    return TARGET_RMS + LINE_OFFSET[line]


def mean_error(lines):
    """Mean of (RMS - the line's target) over a synth preset's lines."""
    return float(np.mean([r["rms"] - line_target(line) for line, (_, r) in lines.items()]))


def slot_window(pattern_name):
    return BREAKDOWN_WINDOW if any(pattern_name.endswith(" " + t) for t in THINNING) else SLOT_WINDOW


def peak_rule(key, res, limiter, fails):
    if res["tp"] > PEAK_LIMIT and not limiter:
        fails.append("%s: true peak %.2f dBTP with no limiter" % (key, res["tp"]))
    if res["tp"] > 0.0 and limiter:
        fails.append("%s: true peak %.2f dBTP through its limiter" % (key, res["tp"]))


def check(results):
    fails, rows = [], []
    base = results.get("effect-base", (None, {}))[1]
    a1 = {job["entry"]["path"]: res["rms"] for job, res in results.values() if job["kind"] == "scene" and "rms" in res}
    for key, (job, res) in sorted(results.items()):
        kind = job["kind"]
        if "error" in res:
            fails.append("%s: render failed: %s" % (key, res["error"]))
            continue
        e = job.get("entry", {})
        limiter = e.get("limiter", False)
        if res["rms"] < SILENT:
            fails.append("%s: silent (%.1f dBFS RMS)" % (key, res["rms"]))
        if kind in ("synth", "scene", "slot"):
            peak_rule(key, res, limiter, fails)
        if kind == "synth" and abs(res["rms"] - line_target(job["line"])) > LINE_TOLERANCE + 1e-6:
            fails.append("%s: RMS %.1f dBFS on the %s line, outside %.1f +- %.1f"
                         % (key, res["rms"], job["line"], line_target(job["line"]), LINE_TOLERANCE))
        if (kind == "scene" or (kind == "synth" and job["line"] == "ref")) and res["rms"] - res["rms_mono"] > MONO_LOSS:
            fails.append("%s: loses %.1f dB summed to mono" % (key, res["rms"] - res["rms_mono"]))
        if kind == "scene" and abs(res["rms"] - TARGET_RMS) > TOLERANCE + 1e-6:
            fails.append("%s: RMS %.1f dBFS, outside %.1f +- %.1f" % (key, res["rms"], TARGET_RMS, TOLERANCE))
        if kind == "slot" and e.get("path") in a1:
            d = res["rms"] - a1[e["path"]]
            lo, hi = slot_window(job["pattern"]["name"])
            if d > hi or d < lo:
                fails.append("%s (%s): %.1f dB against the scene's A1, window %+.0f..%+.0f"
                             % (key, job["pattern"]["name"], d, lo, hi))
        if kind in ("pattern", "effect") and res["tp"] > 0.0:
            fails.append("%s: clips at %.2f dBTP" % (key, res["tp"]))
        if kind == "effect" and base and "lufs" in base:
            d = res["lufs"] - base["lufs"]
            window = CAL_WINDOW if e["type"] == "calibration" else EFFECT_WINDOW
            if abs(d) > window + 1e-6:
                fails.append("%s: %+.1f LU against the sound without it, window +-%.1f" % (key, d, window))
        if "midi" in res and job.get("pattern"):
            exp = expected_events(job["pattern"], job["bars"] * 4.0)
            for p in compare_events(exp, res["midi"]):
                fails.append("%s: MIDI %s" % (key, p))
        rows.append((key, kind, res))

    for path, lines in sorted(synth_groups(results).items()):
        if any("error" in r for _, r in lines.values()):
            continue
        err = mean_error(lines)
        if abs(err) > TOLERANCE + 1e-6:
            fails.append("%s: %+.1f dB from the line targets on average, outside +- %.1f" % (path, err, TOLERANCE))
    return fails, rows


def neighbours(results):
    """Nearest neighbour of every synth preset on the reference line, by the RMS difference of
    their 40-band log spectrograms. Returns ({path: (other, distance)}, [(a, b, distance)] for
    every pair under 2 dB)."""
    items = [(job["entry"]["path"], res["bands"]) for job, res in results.values()
             if job["kind"] == "synth" and job["line"] == "ref" and "bands" in res]
    items.sort()
    if len(items) < 2:
        return {}, []
    frames = min(b.shape[1] for _, b in items)
    stack = np.stack([b[:, :frames].ravel() for _, b in items]).astype(np.float64)
    sq = (stack ** 2).sum(axis=1)
    d2 = (sq[:, None] + sq[None, :] - 2.0 * stack @ stack.T) / stack.shape[1]
    dist = np.sqrt(np.maximum(d2, 0.0))
    np.fill_diagonal(dist, np.inf)
    nearest, close = {}, []
    for i, (a, _) in enumerate(items):
        j = int(np.argmin(dist[i]))
        nearest[a] = (items[j][0], float(dist[i, j]))
        for k in range(i + 1, len(items)):
            if dist[i, k] < 2.0:
                close.append((a, items[k][0], float(dist[i, k])))
    close.sort(key=lambda t: t[2])
    return nearest, close


def neighbour_fails(close):
    fails = []
    for a, b, d in close:
        pair = tuple(sorted((os.path.basename(a)[:-6], os.path.basename(b)[:-6])))
        if d < NEIGHBOUR_MIN and pair not in NEIGHBOUR_ALLOW:
            fails.append("%s and %s: %.2f dB apart on the reference line, under %.1f" % (a, b, d, NEIGHBOUR_MIN))
    return fails


# ---- Calibration --------------------------------------------------------------------------

def calibrate(results, trims, history):
    """New trims and the largest move. Synth presets: the trim that minimises the squared RMS
    error over the three lines, held so the worst line's true peak stays under the limit.
    Scenes: A1 to the target, held so no used slot peaks over the limit. Effect presets with an
    output trim: to the loudness of the sound without them.

    Not every trim moves the level one for one (a chorus's wet level, a limiter's makeup gain
    into its soft clip), so where the previous round measured the same key at another trim
    the move is divided by the slope seen between the two. A trim is held inside the range
    the build can give its parameter (level_min..level_max in the manifest), so a clamped
    trim stops moving. history maps key -> (trim, measured) and is updated in place."""
    new = dict(trims)
    moves = {}

    def move(entry, measured, want, cap=None):
        key = entry["level_key"]
        old = trims.get(key, 0.0)
        slope = 1.0
        if key in history:
            t0, m0 = history[key]
            if abs(old - t0) >= 0.1:
                slope = min(max((measured - m0) / (old - t0), 0.2), 1.5)
        history[key] = (old, measured)
        step = want / slope
        if cap is not None:
            step = min(step, cap)
        step = min(max(step, -12.0), 12.0)
        lo, hi = -60.0, 24.0
        if "level_value" in entry:
            lo = old + entry["level_min"] - entry["level_value"]
            hi = old + entry["level_max"] - entry["level_value"]
        new[key] = round(min(max(old + step, lo), hi), 1)
        moves[key] = new[key] - old

    for path, lines in synth_groups(results).items():
        rs = [r for _, r in lines.values()]
        if any("error" in r for r in rs):
            continue
        entry = next(iter(lines.values()))[0]["entry"]
        err = mean_error(lines)
        cap = None if entry.get("limiter", False) else PEAK_LIMIT - CAL_MARGIN - max(r["tp"] for r in rs)
        move(entry, err, -err, cap)

    slot_peak = {}
    for job, res in results.values():
        if job["kind"] in ("scene", "slot") and "error" not in res:
            lk = job["entry"]["level_key"]
            slot_peak[lk] = max(slot_peak.get(lk, -200.0), res["tp"])
    base = results.get("effect-base", (None, {}))[1]
    for key, (job, res) in results.items():
        if "error" in res:
            continue
        entry = job.get("entry", {})
        lk = entry.get("level_key", "")
        if job["kind"] == "scene":
            cap = None if entry.get("limiter", False) else PEAK_LIMIT - CAL_MARGIN - slot_peak.get(lk, res["tp"])
            move(entry, res["rms"], TARGET_RMS - res["rms"], cap)
        elif job["kind"] == "effect" and lk and base and "lufs" in base:
            move(entry, res["lufs"] - base["lufs"], base["lufs"] - res["lufs"])
    largest = max((abs(v) for v in moves.values()), default=0.0)
    worst = sorted(moves.items(), key=lambda kv: -abs(kv[1]))[:3]
    return new, largest, worst


# ---- Text checks --------------------------------------------------------------------------

def text_checks(entries):
    fails = []
    r = run([FACTORY, "--verify", LIBRARY])
    summary = r.stdout.strip().splitlines()
    if r.returncode != 0:
        fails += [l for l in summary if l.startswith("FAIL")] or ["factory-build --verify failed"]
    r = run([os.path.join(ROOT, "scripts", "branding-audit.sh")])
    if r.returncode != 0:
        fails += ["branding: " + l for l in r.stdout.strip().splitlines()]
    for base in (LIBRARY, os.path.join(ROOT, "tools", "factory")):
        for dirpath, _, files in os.walk(base):
            for name in files:
                p = os.path.join(dirpath, name)
                try:
                    if chr(0x2014) in open(p, encoding="utf-8").read():
                        fails.append("em dash in %s" % os.path.relpath(p, ROOT))
                except (UnicodeDecodeError, OSError):
                    pass
    counts = {}
    for e in entries:
        if e["path"] != "default.mw12f":
            counts[e["type"]] = counts.get(e["type"], 0) + 1
    for t, n in MINIMUM_COUNTS.items():
        if counts.get(t, 0) < n:
            fails.append("only %d %s files, the brief asks for %d" % (counts.get(t, 0), t, n))
    total = sum(counts.values())
    if total < MINIMUM_TOTAL:
        fails.append("only %d files in total" % total)
    return fails, counts, summary[-1] if summary else ""


# ---- Plots --------------------------------------------------------------------------------

INK, INK2, SURFACE = "#0b0b0b", "#52514e", "#fcfcfb"
SERIES = ["#2a78d6", "#eb6834", "#1f9e74", "#8a5cc2"]


def plot_levels(rows, path):
    fig, ax = plt.subplots(figsize=(8, 5.5), facecolor=SURFACE)
    ax.set_facecolor(SURFACE)
    ax.axvspan(TARGET_RMS - TOLERANCE, TARGET_RMS + TOLERANCE, color="#f0efec", zorder=0)
    ax.axhline(PEAK_LIMIT, color=INK2, lw=1, ls="--", zorder=1)
    series = [("synth, %s line" % n, lambda k, r, n=n: k == "synth" and r.get("line") == n) for n, _, _, _ in SYNTH_LINES]
    series.append(("scene A1", lambda k, r: k == "scene"))
    for i, (label, pick) in enumerate(series):
        pts = [(r["rms"], r["tp"]) for _, k, r in rows if pick(k, r)]
        if pts:
            a = np.array(pts)
            ax.scatter(a[:, 0], a[:, 1], s=22, color=SERIES[i % len(SERIES)], edgecolor=SURFACE, linewidth=0.8,
                       label="%s (%d)" % (label, len(pts)), zorder=2)
    ax.set_xlabel("RMS over both channels, dBFS", color=INK2)
    ax.set_ylabel("True peak, dBTP", color=INK2)
    ax.set_title("Factory levels: target band %.1f +- %.1f dB, peak limit %.0f dBTP" % (TARGET_RMS, TOLERANCE, PEAK_LIMIT),
                 color=INK, loc="left")
    ax.legend(frameon=False, labelcolor=INK2)
    ax.grid(color="#e4e3df", lw=0.6)
    for s in ax.spines.values():
        s.set_color("#d6d5d0")
    ax.tick_params(colors=INK2)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def plot_spectrograms(results, keys, path, title):
    n = len(keys)
    cols = 2
    rowsn = (n + cols - 1) // cols
    fig, axes = plt.subplots(rowsn, cols, figsize=(13, 2.6 * rowsn), facecolor=SURFACE, squeeze=False)
    for ax in axes.flat:
        ax.set_visible(False)
    for ax, key in zip(axes.flat, keys):
        job, res = results[key]
        rate, x = load(res["wav"])
        mono = x.mean(axis=1)
        f, t, z = signal.stft(mono, fs=rate, nperseg=2048, noverlap=1792)
        s = 20 * np.log10(np.abs(z) + 1e-9)
        keep = (f >= 30) & (f <= 8000)
        ax.set_visible(True)
        ax.pcolormesh(t, f[keep], s[keep], cmap="Blues_r", vmin=s.max() - 80, vmax=s.max(), shading="auto", rasterized=True)
        ax.set_yscale("log")
        ax.set_ylim(30, 8000)
        e = job["entry"]
        ax.set_title("%s  (%s, %g BPM, rms %.1f, centroid %.0f Hz)" % (e["name"], e["group"], job["bpm"], res["rms"], res["centroid"]),
                     fontsize=9, color=INK, loc="left")
        ax.tick_params(labelsize=7, colors=INK2)
    fig.suptitle(title, color=INK, x=0.01, ha="left")
    fig.tight_layout()
    fig.savefig(path, dpi=100)
    plt.close(fig)


# ---- Main ---------------------------------------------------------------------------------

def summary_line(label, sel):
    rms = np.array([r["rms"] for r in sel])
    tp = np.array([r["tp"] for r in sel])
    cen = np.array([r["centroid"] for r in sel])
    return ("%-15s n=%3d  rms %.1f..%.1f (spread %.1f dB, median %.1f)  true peak max %.2f  centroid %.0f..%.0f Hz (median %.0f)"
            % (label, len(sel), rms.min(), rms.max(), rms.max() - rms.min(), np.median(rms), tp.max(), cen.min(), cen.max(),
               np.median(cen)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--calibrate", action="store_true", help="rewrite levels.tsv from measurement, rebuild and re-check")
    ap.add_argument("--quick", action="store_true", help="skip the pattern and effect renders")
    args = ap.parse_args()

    for tool in (RENDER, FACTORY):
        if not os.path.exists(tool):
            raise SystemExit("missing %s: cmake --build build-factory --target acid-render factory-build" % tool)

    entries = build_library()
    if args.calibrate:
        history = {}
        for rnd in range(1, CAL_ROUNDS + 1):
            jobs = make_jobs(entries, args.quick, levels_only=True)
            print("calibration round %d: rendering %d jobs ..." % (rnd, len(jobs)), flush=True)
            trims, largest, worst = calibrate(render_all(jobs), read_levels(), history)
            write_levels(trims)
            print("wrote %d level trims to %s, largest move %.1f dB (%s)" % (len(trims), os.path.relpath(LEVELS, ROOT), largest,
                  ", ".join("%s %+.1f" % kv for kv in worst)), flush=True)
            entries = build_library()
            if largest <= CAL_SETTLED:
                break

    jobs = make_jobs(entries, args.quick)
    print("rendering %d jobs ..." % len(jobs), flush=True)
    results = render_all(jobs)

    fails, rows = check(results)
    nearest, close = neighbours(results)
    fails += neighbour_fails(close)
    tfails, counts, verify_line = text_checks(entries)
    fails = tfails + fails

    os.makedirs(OUT, exist_ok=True)
    plot_levels([(k, kind, dict(r, line=results[k][0].get("line"))) for k, kind, r in rows], os.path.join(OUT, "levels.png"))
    scene_keys = sorted(k for k, (j, _) in results.items() if j["kind"] == "scene")
    by_style = {}
    for k in scene_keys:
        by_style.setdefault(results[k][0]["entry"]["group"], []).append(k)
    sample = [v[len(v) // 2] for _, v in sorted(by_style.items())]
    half = (len(sample) + 1) // 2
    plot_spectrograms(results, sample[:half], os.path.join(OUT, "spectrograms-1.png"), "Scenes, A1, one per style (1 of 2)")
    plot_spectrograms(results, sample[half:], os.path.join(OUT, "spectrograms-2.png"), "Scenes, A1, one per style (2 of 2)")

    # Report
    lines = []
    lines.append("factory QA: %s" % verify_line)
    lines.append("counts: " + ", ".join("%s %d" % kv for kv in sorted(counts.items())) + ", total %d" % sum(counts.values()))
    lines.append("levels: RMS over both channels, true peak 4x oversampled; target %.1f dBFS" % TARGET_RMS)
    groups = synth_groups(results)
    for name, _, bpm, _ in SYNTH_LINES:
        sel = [r for key, k, r in rows if k == "synth" and results[key][0]["line"] == name]
        if sel:
            lines.append(summary_line("synth %s" % name, sel) + "  (%d BPM)" % bpm)
    means = [mean_error(g) for g in groups.values() if all("rms" in r for _, r in g.values())]
    if means:
        lines.append("synth mean      mean distance from the line targets %+.1f..%+.1f dB (line targets: %s)"
                     % (min(means), max(means), ", ".join("%s %.1f" % (n, line_target(n)) for n, _, _, _ in SYNTH_LINES)))
    for kind in ("scene", "slot", "pattern", "effect"):
        sel = [r for _, k, r in rows if k == kind]
        if sel:
            lines.append(summary_line(kind, sel))
    mono = [r["rms"] - r["rms_mono"] for key, k, r in rows if k == "scene" or (k == "synth" and results[key][0]["line"] == "ref")]
    if mono:
        lines.append("mono sum        synth presets and scene A1s lose %.2f..%.2f dB summed to mono (median %.2f)"
                     % (min(mono), max(mono), float(np.median(mono))))
    base = results.get("effect-base", (None, {}))[1]
    if base and "lufs" in base:
        lines.append("effect base     %s alone: %.1f LUFS" % (EFFECT_BASE, base["lufs"]))
        for t in EFFECT_TYPES + ("calibration",):
            ds = [r["lufs"] - base["lufs"] for key, k, r in rows if k == "effect" and results[key][0]["entry"]["type"] == t]
            if ds:
                lines.append("  %-12s n=%2d  %+.1f..%+.1f LU against the sound without it" % (t, len(ds), min(ds), max(ds)))
    if nearest:
        nd = sorted(d for _, d in nearest.values())
        lines.append("neighbours      synth nearest-neighbour distance %.2f..%.2f dB (median %.2f); %d presets within 2 dB of another, "
                     "%d pairs under 2 dB" % (nd[0], nd[-1], float(np.median(nd)), sum(1 for d in nd if d < 2.0), len(close)))
    for kind in ("synth", "scene", "pattern"):
        sel = [r for key, k, r in rows if k == kind and (kind != "synth" or results[key][0]["line"] == "ref")]
        ab = [r["accent_brightness"] for r in sel if "accent_brightness" in r]
        rp = [r["res_peak_db"] for r in sel if "res_peak_db" in r]
        hz = [(r["res_peak_hz_plain"], r["res_peak_hz_accent"]) for r in sel if "res_peak_hz_plain" in r]
        if hz:
            lines.append("%-15s resonant peak on accented notes higher than on plain notes in %d of %d (median %.0f Hz plain, %.0f Hz accented)"
                         % (kind, sum(1 for p, a in hz if a > p), len(hz), np.median([p for p, _ in hz]), np.median([a for _, a in hz])))
        if ab:
            lines.append("%-15s accented notes brighter than plain in %d of %d (median ratio %.2f); resonant peak prominence median %.1f dB"
                         % ("", sum(1 for a in ab if a > 1.0), len(ab), np.median(ab), np.median(rp) if rp else float("nan")))
    lines.append("")
    lines.append("%d failure(s)" % len(fails))
    lines += ["  " + f for f in fails]
    report = "\n".join(lines) + "\n"
    open(os.path.join(OUT, "report.txt"), "w").write(report)
    detail = {k: {kk: vv for kk, vv in r.items() if kk not in ("wav", "bands")} for k, (_, r) in results.items()}
    json.dump({"failures": fails, "results": detail}, open(os.path.join(OUT, "results.json"), "w"), indent=1, sort_keys=True)
    with open(os.path.join(OUT, "measurements.tsv"), "w") as f:
        f.write("key\tkind\tline\trms\trms_mono\tpeak\ttrue_peak\tlufs\tcentroid\taccent_brightness\tres_peak_db\n")
        for key, kind, r in rows:
            f.write("%s\t%s\t%s\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.0f\t%s\t%s\n" % (
                key, kind, results[key][0].get("line", ""), r["rms"], r["rms_mono"], r["peak"], r["tp"], r["lufs"], r["centroid"],
                "%.2f" % r["accent_brightness"] if "accent_brightness" in r else "",
                "%.1f" % r["res_peak_db"] if "res_peak_db" in r else ""))
    with open(os.path.join(OUT, "neighbours.tsv"), "w") as f:
        f.write("preset\tnearest\tdistance_db\n")
        for a, (b, d) in sorted(nearest.items(), key=lambda kv: kv[1][1]):
            f.write("%s\t%s\t%.2f\n" % (a, b, d))
    sys.stdout.write(report)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
