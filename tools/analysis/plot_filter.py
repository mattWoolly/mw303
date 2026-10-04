"""Small-signal response of the diode ladder at two resonant frequencies and five
resonance settings (pot rotation, with the wiper's loading). Reads filter_response.csv."""
import os

from common import RAMP, MUTED, parse_args, plt, read_csv, save, style


def main():
    args = parse_args(__doc__)
    style()
    d = read_csv(os.path.join(args.data_dir, "filter_response.csv"))

    fig, axes = plt.subplots(1, 2, figsize=(13, 4.6), constrained_layout=True, sharey=True)
    for ax, f_res in ((axes[0], 500), (axes[1], 2000)):
        for i, r in enumerate((0, 25, 50, 75, 100)):
            ax.semilogx(d["hz"], d["f%d_r%d" % (f_res, r)], color=RAMP[i], label="resonance %d %%" % r)
        ax.axvline(f_res, color=MUTED, linewidth=0.8, linestyle=":")
        ax.set_title("Resonant frequency %d Hz (dotted)" % f_res)
        ax.set_xlabel("Hz")
        ax.set_xlim(10, 20000)
        ax.set_ylim(-70, 20)
        ax.legend(loc="lower left")
    axes[0].set_ylabel("dB, filter output over input")
    save(fig, args.out, "filter_response.png")


if __name__ == "__main__":
    main()
