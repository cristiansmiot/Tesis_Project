"""Capture a serial evidence log for a bounded duration."""

import argparse
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=int, default=900)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.seconds
    with serial.Serial(args.port, args.baud, timeout=0.25) as port, args.out.open(
        "w", encoding="utf-8", newline="\n"
    ) as log:
        while time.monotonic() < deadline:
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            stamp = time.strftime("%Y-%m-%d %H:%M:%S")
            record = f"{stamp} {line}"
            print(record, flush=True)
            log.write(record + "\n")
            log.flush()


if __name__ == "__main__":
    main()
