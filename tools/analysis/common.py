"""Shared helpers for the voice analysis plots.

The plots read what the acid_dsp_tests render and dump tests write into the build's
test-output directory (CSV dumps and WAV renders) and write PNGs next to them, or into
--out. Run the tests first:

    build-dsp/tests/acid_dsp_tests "[render]"
    python3 tools/analysis/plot_all.py build-dsp/test-output
"""
import argparse
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from scipy.io import wavfile  # noqa: E402

# Categorical series colours in a fixed order, and a one-hue ramp for ordered series
# (light to dark = low to high).
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
RAMP = ["#86b6ef", "#5598e7", "#2a78d6", "#1c5cab", "#0d366b"]
SURFACE = "#fcfcfb"
TEXT = "#1a1a19"
MUTED = "#6f6e68"
GRID = "#e4e3dc"


def style():
    plt.rcParams.update({
        "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
        "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "axes.titlecolor": TEXT,
        "xtick.color": MUTED, "ytick.color": MUTED, "text.color": TEXT,
        "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.6,
        "axes.spines.top": False, "axes.spines.right": False,
        "axes.titlesize": 10, "axes.titleweight": "bold", "axes.titlelocation": "left",
        "axes.labelsize": 9, "xtick.labelsize": 8, "ytick.labelsize": 8,
        "legend.fontsize": 8, "legend.frameon": False, "lines.linewidth": 1.6,
        "font.family": "DejaVu Sans", "figure.dpi": 110,
    })


def parse_args(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("data_dir", nargs="?", default=default_data_dir(),
                        help="directory holding the test output (default: build-dsp/test-output)")
    parser.add_argument("--out", default=None, help="directory for the PNGs (default: the data directory)")
    args = parser.parse_args()
    args.out = args.out or args.data_dir
    os.makedirs(args.out, exist_ok=True)
    return args


def default_data_dir():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "build-dsp", "test-output"))


def read_csv(path):
    """Returns a dict of column name to numpy array."""
    data = np.genfromtxt(path, delimiter=",", names=True)
    return {name: data[name] for name in data.dtype.names}


def read_wav(path):
    rate, samples = wavfile.read(path)
    samples = np.asarray(samples, dtype=np.float64)
    if samples.ndim > 1:
        samples = samples[:, 0]
    return rate, samples


def save(fig, out_dir, name):
    path = os.path.join(out_dir, name)
    fig.savefig(path, dpi=110)
    plt.close(fig)
    print("wrote", path)
    return path
