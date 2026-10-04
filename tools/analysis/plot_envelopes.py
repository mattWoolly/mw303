"""Control signals of the voice over the 16-step test pattern: main envelope, the cutoff's
two modulation sources in octaves, the resulting resonant frequency, the amplifier's two
control currents, the pitch CV and the audio. Reads control_pattern.csv."""
import os

import numpy as np

from common import SERIES, MUTED, parse_args, plt, read_csv, save, style


def main():
    args = parse_args(__doc__)
    style()
    d = read_csv(os.path.join(args.data_dir, "control_pattern.csv"))
    t = d["t"]
    step = 60.0 / 125.0 / 4.0

    fig, axes = plt.subplots(6, 1, figsize=(12, 13), sharex=True, constrained_layout=True)

    ax = axes[0]
    ax.plot(t, d["meg_volts"], color=SERIES[0])
    ax.axhline(3.3, color=MUTED, linewidth=0.8, linestyle=":")
    ax.text(t[-1], 3.45, "3.3 V bus", ha="right", fontsize=8, color=MUTED)
    ax.set_title("Main envelope (short decay while the accent latch is set, no retrigger on slid notes)")
    ax.set_ylabel("volts")

    ax = axes[1]
    ax.plot(t, d["env_oct"], color=SERIES[0], label="Env Mod")
    ax.plot(t, d["accent_oct"], color=SERIES[1], label="accent sweep")
    ax.set_title("Cutoff modulation")
    ax.set_ylabel("octaves")
    ax.legend(loc="upper right", ncol=2)

    ax = axes[2]
    ax.semilogy(t, d["cutoff_hz"], color=SERIES[0])
    ax.set_title("Resonant frequency sent to the filter")
    ax.set_ylabel("Hz")

    ax = axes[3]
    ax.plot(t, d["gate_current"], color=SERIES[0], label="gated envelope")
    ax.plot(t, d["accent_current"], color=SERIES[1], label="accent path")
    ax.set_title("Amplifier control current (1 = a plain note at onset)")
    ax.set_ylabel("relative current")
    ax.legend(loc="upper right", ncol=2)

    ax = axes[4]
    ax.plot(t, d["pitch_volts"], color=SERIES[0])
    ax.set_title("Pitch CV after the slide lag (1 V per octave)")
    ax.set_ylabel("volts")

    ax = axes[5]
    ax.plot(t, d["audio"], color=SERIES[0], linewidth=0.5)
    ax.set_title("Audio output")
    ax.set_ylabel("full scale = 1")
    ax.set_xlabel("seconds")

    for ax in axes:
        for k in range(17):
            ax.axvline(k * step, color=MUTED, linewidth=0.4, alpha=0.5)
    labels = ["A", "", "S", "A", "", "rest", "A", "A", "A", "A", "S", "S", "", "", "AS", ""]
    for k, label in enumerate(labels):
        axes[0].text((k + 0.5) * step, 10.6, label, ha="center", fontsize=8, color=MUTED)
    axes[0].set_ylim(0, 11.5)
    axes[0].set_xlim(0, np.max(t))
    save(fig, args.out, "envelopes.png")

    # Close-up of one plain and one accented onset: gate delay, rise, hold and fall.
    fig, axes = plt.subplots(1, 2, figsize=(12, 3.6), constrained_layout=True)
    for ax, start, title in ((axes[0], 1 * step, "Plain note (step 2)"), (axes[1], 6 * step, "Accented note (step 7)")):
        m = (t >= start - 0.005) & (t <= start + 0.115)
        ms = (t[m] - start) * 1e3
        ax.plot(ms, d["gate_current"][m], color=SERIES[0], label="gated envelope")
        ax.plot(ms, d["accent_current"][m], color=SERIES[1], label="accent path")
        ax.plot(ms, d["vca_gain"][m], color=SERIES[2], label="total", linewidth=1.0)
        ax.axvline(0.0, color=MUTED, linewidth=0.8, linestyle=":")
        ax.axvline(61.35, color=MUTED, linewidth=0.8, linestyle=":")
        ax.set_title(title)
        ax.set_xlabel("ms from the gate edge (dotted: gate on, gate off)")
        ax.set_ylabel("relative current")
        ax.legend(loc="upper right")
    save(fig, args.out, "gate_shape.png")


if __name__ == "__main__":
    main()
