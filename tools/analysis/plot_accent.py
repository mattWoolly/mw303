"""Accent sweep curves against the research document's tables: a single accent at five
resonance settings, and a run of six accents followed by four plain notes.
Reads accent_single.csv and accent_train.csv (voice at the schematic's scale).

The tables were simulated with a 72 ms envelope; the voice's accented decay is 80 ms, so
its curves sit a few percent above the table points from about 40 ms on. The unit tests
check the tables with a 72 ms envelope and match within 3 %."""
import os

import numpy as np

from common import RAMP, SERIES, MUTED, parse_args, plt, read_csv, save, style

TABLE_T = np.array([0.2, 5, 10, 20, 30, 40, 60, 80, 120, 200, 300, 400])
TABLE = {
    "r0": [3.04, 2.84, 2.62, 2.20, 1.81, 1.44, 0.81, 0.67, 0.52, 0.30, 0.16, 0.08],
    "r50": [1.60, 1.61, 1.60, 1.53, 1.40, 1.24, 0.88, 0.75, 0.54, 0.29, 0.13, 0.06],
    "r100": [0.01, 0.34, 0.61, 0.98, 1.16, 1.22, 1.08, 0.89, 0.60, 0.27, 0.10, 0.04],
}
TRAIN_PEAKS = [1.22, 1.51, 1.56, 1.57, 1.57, 1.57, 0.73, 0.23, 0.07, 0.02]


def main():
    args = parse_args(__doc__)
    style()
    single = read_csv(os.path.join(args.data_dir, "accent_single.csv"))
    train = read_csv(os.path.join(args.data_dir, "accent_train.csv"))

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.4), constrained_layout=True)

    ax = axes[0]
    ms = single["t"] * 1e3
    for i, (key, label) in enumerate((("r0", "0 %"), ("r25", "25 %"), ("r50", "50 %"), ("r75", "75 %"), ("r100", "100 %"))):
        ax.plot(ms, single[key], color=RAMP[i], label="resonance " + label)
        if key in TABLE:
            ax.plot(TABLE_T, TABLE[key], linestyle="none", marker="o", markersize=5,
                    markerfacecolor="none", markeredgecolor=RAMP[i], markeredgewidth=1.2)
    ax.plot([], [], linestyle="none", marker="o", markerfacecolor="none", markeredgecolor=MUTED, label="research table")
    ax.set_xlim(0, 400)
    ax.set_title("Single accent: cutoff offset from the sweep network")
    ax.text(0.98, 0.42, "model: 80 ms envelope\ntable: 72 ms envelope", transform=ax.transAxes, ha="right",
            fontsize=8, color=MUTED)
    ax.set_xlabel("ms from the gate edge")
    ax.set_ylabel("octaves")
    ax.legend(loc="upper right")

    ax = axes[1]
    step = 60.0 / 125.0 / 4.0
    ax.plot(train["t"] * 1e3, train["accent_oct"], color=SERIES[0], label="model")
    peak_t = []
    for k, value in enumerate(TRAIN_PEAKS):
        m = (train["t"] >= k * step) & (train["t"] < (k + 1) * step)
        peak_t.append(train["t"][m][np.argmax(train["accent_oct"][m])] * 1e3)
    ax.plot(peak_t, TRAIN_PEAKS, linestyle="none", marker="o", markersize=6, markerfacecolor="none",
            markeredgecolor=SERIES[1], markeredgewidth=1.4, label="research table (peak per step)")
    for k in range(11):
        ax.axvline(k * step * 1e3, color=MUTED, linewidth=0.4, alpha=0.5)
    ax.set_title("Six accents then four plain notes, 125 BPM, full resonance")
    ax.set_xlabel("ms")
    ax.set_ylabel("octaves")
    ax.legend(loc="upper right")

    ax = axes[2]
    ax.plot(ms, single["vca_accent_ua"], color=SERIES[0], label="model")
    ax.plot([0.5, 2.5, 10, 20, 40, 60, 100, 150], [60, 121, 109, 93, 66, 46, 21, 4], linestyle="none", marker="o",
            markersize=6, markerfacecolor="none", markeredgecolor=SERIES[1], markeredgewidth=1.4,
            label="research simulation (72 ms envelope)")
    ax.set_xlim(0, 250)
    ax.set_title("Accent current into the amplifier, Accent at maximum")
    ax.set_xlabel("ms from the gate edge")
    ax.set_ylabel("microamps")
    ax.legend(loc="upper right")

    save(fig, args.out, "accent.png")


if __name__ == "__main__":
    main()
