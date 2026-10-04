"""Runs every plot script on one data directory.

    python3 tools/analysis/plot_all.py [data_dir] [--out DIR]
"""
import subprocess
import sys
import os

SCRIPTS = ["plot_envelopes.py", "plot_accent.py", "plot_waveforms.py", "plot_filter.py", "plot_spectra.py"]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    failed = 0
    for script in SCRIPTS:
        result = subprocess.run([sys.executable, os.path.join(here, script)] + sys.argv[1:])
        failed += result.returncode != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
