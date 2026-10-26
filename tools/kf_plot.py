#!/usr/bin/env python3
"""
kf_plot.py - live plot (or log) of the Kalman filter demo's serial output.

The firmware prints CSV lines `t_ms,adc,meas_c,est_c,p,k,innov` plus
informational lines starting with '#'. This script reads them and either

  * plots them live (needs matplotlib):
        top:    raw measurement (dots), estimate (line), +-2 sigma band
        bottom: Kalman gain K
    Keys pressed in the plot window are forwarded to the board, so
    + - r R z n h work exactly as in a serial terminal.

  * or, with --no-plot, just echoes and (optionally) logs to CSV.

Examples
    python3 tools/kf_plot.py                          # live plot, /dev/ttyACM0
    python3 tools/kf_plot.py --port COM5 --log run.csv
    python3 tools/kf_plot.py --no-plot --log run.csv  # headless capture
    python3 tools/kf_plot.py --replay run.csv         # plot a saved capture

Requirements
    pip install pyserial matplotlib
"""
import argparse
import collections
import csv
import math
import sys
import time

FIELDS = ("t_ms", "adc", "meas_c", "est_c", "p", "k", "innov")


def parse_line(line):
    """Return a dict for a CSV data line, None for anything else."""
    if not line or line.startswith("#") or line.startswith("t_ms"):
        return None
    parts = line.split(",")
    if len(parts) != len(FIELDS):
        return None  # partial line (e.g. we connected mid-transmission)
    try:
        return {name: float(v) for name, v in zip(FIELDS, parts)}
    except ValueError:
        return None


class Source:
    """Yields decoded text lines from a serial port or a replay file."""

    def __init__(self, args):
        self.serial = None
        self.replay = None
        if args.replay:
            self.replay = open(args.replay, newline="")
        else:
            import serial  # pyserial; imported here so --replay works without it
            # Short timeout keeps the GUI responsive while waiting for data.
            self.serial = serial.Serial(args.port, args.baud, timeout=0.05)

    def lines(self):
        """Return all complete lines available right now."""
        if self.replay:
            out = [l.rstrip("\r\n") for l in self.replay]
            self.replay = None
            return out
        out = []
        while self.serial.in_waiting or not out:
            raw = self.serial.readline()
            if not raw:
                break
            out.append(raw.decode("ascii", errors="replace").rstrip("\r\n"))
        return out

    def send(self, text):
        if self.serial:
            self.serial.write(text.encode("ascii"))


def open_log(path):
    if not path:
        return None, None
    f = open(path, "w", newline="")
    w = csv.writer(f)
    w.writerow(FIELDS)
    return f, w


def run_headless(src, writer, log_file):
    """Echo everything, log the data lines. Ctrl-C to stop."""
    while True:
        for line in src.lines():
            print(line, flush=True)
            if parse_line(line) and writer:
                writer.writerow(line.split(","))
                log_file.flush()  # keep the capture if we're killed


def run_plot(src, writer, log_file, window_s):
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation

    n_max = int(window_s * 10) + 1  # firmware samples at 10 Hz
    data = {name: collections.deque(maxlen=n_max) for name in FIELDS}

    fig, (ax_t, ax_k) = plt.subplots(
        2, 1, sharex=True, figsize=(10, 6),
        gridspec_kw={"height_ratios": [3, 1]})
    fig.canvas.manager.set_window_title("STM32F429 Kalman filter demo")
    meas_plot, = ax_t.plot([], [], ".", ms=3, alpha=0.5, label="measurement z")
    est_plot, = ax_t.plot([], [], "-", lw=2, label="Kalman estimate x")
    band = [None]
    k_plot, = ax_k.plot([], [], "-", label="gain K")
    ax_t.set_ylabel("die temperature (C)")
    ax_k.set_ylabel("K")
    ax_k.set_xlabel("time (s)")
    ax_t.legend(loc="upper left")
    ax_t.grid(alpha=0.3)
    ax_k.grid(alpha=0.3)
    status = ax_t.text(0.99, 0.02, "", transform=ax_t.transAxes,
                       ha="right", va="bottom", fontsize=8, family="monospace")

    def on_key(event):
        # Forward the demo's command keys to the board.
        if event.key in ("+", "-", "r", "R", "z", "n", "h", "?"):
            src.send(event.key)
    fig.canvas.mpl_connect("key_press_event", on_key)

    def update(_frame):
        for line in src.lines():
            if line.startswith("#"):
                print(line)
                if line.startswith("# Q="):
                    status.set_text(line[2:].split(" -> ")[0])
                continue
            row = parse_line(line)
            if row is None:
                continue
            for name in FIELDS:
                data[name].append(row[name])
            if writer:
                writer.writerow(line.split(","))
                log_file.flush()
        if not data["t_ms"]:
            return ()

        t = [v / 1000.0 for v in data["t_ms"]]
        est = list(data["est_c"])
        sigma2 = [2.0 * math.sqrt(max(p, 0.0)) for p in data["p"]]
        meas_plot.set_data(t, list(data["meas_c"]))
        est_plot.set_data(t, est)
        k_plot.set_data(t, list(data["k"]))
        if band[0] is not None:
            band[0].remove()
        band[0] = ax_t.fill_between(
            t, [e - s for e, s in zip(est, sigma2)],
            [e + s for e, s in zip(est, sigma2)],
            alpha=0.2, color=est_plot.get_color(), label="+-2 sigma")
        ax_t.relim()
        ax_t.autoscale_view()
        ax_k.set_ylim(0, 1.05)
        ax_k.set_xlim(t[0], max(t[-1], t[0] + 1))
        return ()

    _anim = FuncAnimation(fig, update, interval=100, cache_frame_data=False)
    plt.tight_layout()
    plt.show()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0", help="serial port (default %(default)s)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--log", help="also write data rows to this CSV file")
    ap.add_argument("--window", type=float, default=60.0,
                    help="seconds of history to show (default %(default)s)")
    ap.add_argument("--no-plot", action="store_true", help="print/log only, no GUI")
    ap.add_argument("--replay", help="plot a CSV file saved earlier with --log")
    args = ap.parse_args()
    if args.replay:
        args.window = 1e9  # show the whole file

    src = Source(args)
    log_file, writer = open_log(args.log)
    try:
        if args.no_plot:
            run_headless(src, writer, log_file)
        else:
            run_plot(src, writer, log_file, args.window)
    except KeyboardInterrupt:
        pass
    finally:
        if log_file:
            log_file.close()


if __name__ == "__main__":
    sys.exit(main())
