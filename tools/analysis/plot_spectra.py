"""Spectrogram and long-term spectrum of the rendered WAVs: the pattern with saw and with
square, and the cutoff sweep. Reads acid_pattern_saw.wav, acid_pattern_square.wav and
acid_cutoff_sweep.wav."""
import os

import numpy as np
from scipy import signal

from common import SERIES, parse_args, plt, read_wav, save, style


def main():
    args = parse_args(__doc__)
    style()
    names = ["acid_pattern_saw.wav", "acid_pattern_square.wav", "acid_cutoff_sweep.wav"]
    titles = ["Pattern, saw (first two bars)", "Pattern, square (first two bars)", "Cutoff sweep, saw (eight bars)"]

    fig, axes = plt.subplots(3, 1, figsize=(13, 11), constrained_layout=True)
    for ax, name, title in zip(axes, names, titles):
        rate, x = read_wav(os.path.join(args.data_dir, name))
        if "sweep" not in name:
            x = x[: int(3.84 * rate)]
        f, t, s = signal.spectrogram(x, fs=rate, window="hann", nperseg=2048, noverlap=1536, scaling="spectrum")
        level = 10.0 * np.log10(np.maximum(s, 1e-14))
        ax.grid(False)
        mesh = ax.pcolormesh(t, f, level, shading="auto", cmap="Blues", vmin=-110, vmax=-20)
        ax.set_yscale("log")
        ax.set_ylim(40, 20000)
        ax.set_title(title)
        ax.set_ylabel("Hz")
        ax.grid(False)
        fig.colorbar(mesh, ax=ax, label="dB re full scale", pad=0.01)
    axes[-1].set_xlabel("seconds")
    save(fig, args.out, "spectrogram.png")

    fig, ax = plt.subplots(figsize=(11, 4.4), constrained_layout=True)
    for i, (name, label) in enumerate(zip(names[:2], ("saw", "square"))):
        rate, x = read_wav(os.path.join(args.data_dir, name))
        f, p = signal.welch(x, fs=rate, window="hann", nperseg=16384, scaling="spectrum")
        ax.semilogx(f[1:], 10.0 * np.log10(np.maximum(p[1:], 1e-16)), color=SERIES[i], label=label, linewidth=1.0)
    ax.set_xlim(20, 24000)
    ax.set_ylim(-140, -20)
    ax.set_title("Long-term spectrum of the pattern renders")
    ax.set_xlabel("Hz")
    ax.set_ylabel("dB re full scale")
    ax.legend(loc="upper right", ncol=2)
    save(fig, args.out, "spectrum.png")


if __name__ == "__main__":
    main()
