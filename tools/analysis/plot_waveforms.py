"""Waveforms: the raw oscillator at the waveform switch (volts) and the voice's output with
the filter open and at its default setting. Reads oscillator.csv and voice_waveforms.csv."""
import os

from common import SERIES, parse_args, plt, read_csv, save, style


def main():
    args = parse_args(__doc__)
    style()
    osc = read_csv(os.path.join(args.data_dir, "oscillator.csv"))
    voice = read_csv(os.path.join(args.data_dir, "voice_waveforms.csv"))

    fig, axes = plt.subplots(2, 2, figsize=(13, 7), constrained_layout=True)
    for ax, suffix, span, title in ((axes[0][0], "c2", 0.040, "Oscillator at C2 (65.4 Hz)"),
                                    (axes[0][1], "c4", 0.010, "Oscillator at C4 (261.6 Hz)")):
        m = osc["t"] <= span
        ax.plot(osc["t"][m] * 1e3, osc["saw_" + suffix][m], color=SERIES[0], label="saw")
        ax.plot(osc["t"][m] * 1e3, osc["square_" + suffix][m], color=SERIES[1], label="square")
        ax.set_title(title + ": slow fall, fast rise; the square is high before each reset")
        ax.set_xlabel("ms")
        ax.set_ylabel("volts at the waveform switch")
        ax.set_ylim(4.5, 12.5)
        ax.legend(loc="upper right", ncol=2)

    ms = voice["t"] * 1e3
    ax = axes[1][0]
    ax.plot(ms, voice["saw_open"], color=SERIES[0], label="saw")
    ax.plot(ms, voice["square_open"], color=SERIES[1], label="square")
    ax.set_title("Voice output at C2, filter wide open, no resonance")
    ax.set_xlabel("ms")
    ax.set_ylabel("full scale = 1")
    ax.legend(loc="upper right", ncol=2)

    ax = axes[1][1]
    ax.plot(ms, voice["saw_default"], color=SERIES[0], label="saw")
    ax.plot(ms, voice["square_default"], color=SERIES[1], label="square")
    ax.set_title("Voice output at C2, default cutoff and resonance, envelope at rest")
    ax.set_xlabel("ms")
    ax.set_ylabel("full scale = 1")
    ax.legend(loc="upper right", ncol=2)

    save(fig, args.out, "waveforms.png")


if __name__ == "__main__":
    main()
