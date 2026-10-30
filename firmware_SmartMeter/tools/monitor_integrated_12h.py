"""Capture passive evidence for an integrated meter stability run.

Reads the existing USB serial stream and public, read-only backend endpoints.
It sends no AT commands and does not alter the meter, broker or database.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import threading
import time
from datetime import datetime, timedelta, timezone
from urllib.error import URLError
from urllib.request import urlopen

import serial


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def append_jsonl(path: Path, data: dict) -> None:
    with path.open("a", encoding="utf-8", buffering=1) as stream:
        stream.write(json.dumps(data, ensure_ascii=False) + "\n")


def get_json(url: str) -> dict:
    with urlopen(url, timeout=20) as response:
        return json.load(response)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--hours", type=float, default=12.0)
    parser.add_argument("--poll-seconds", type=int, default=300)
    parser.add_argument("--device", default="SM-348518435344")
    parser.add_argument("--api", default="https://tesisproject-production.up.railway.app/api/v1")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.hours <= 0 or args.poll_seconds <= 0:
        parser.error("--hours and --poll-seconds must be positive")

    args.output.mkdir(parents=True, exist_ok=True)
    serial_log = args.output / "serial.log"
    api_log = args.output / "api_snapshots.jsonl"
    monitor_log = args.output / "monitor_events.jsonl"
    started = utc_now()
    finish = started + timedelta(hours=args.hours)
    base = f"{args.api.rstrip('/')}/salud/{args.device}"
    measurement = f"{args.api.rstrip('/')}/mediciones/{args.device}/ultima"
    write_json(args.output / "run.json", {
        "device_id": args.device,
        "start_utc": started.isoformat(),
        "planned_end_utc": finish.isoformat(),
        "duration_hours": args.hours,
        "serial_port": args.port,
        "poll_seconds": args.poll_seconds,
        "monitor_pid": os.getpid(),
        "status": "running",
        "note": "Passive capture; SD reconciliation requires reading the card after the run.",
    })

    stop = threading.Event()

    def poll_api() -> None:
        while not stop.is_set():
            sample = {"captured_utc": utc_now().isoformat()}
            for name, url in (
                ("health", base + "/metricas?horas=12"),
                ("latest_measurement", measurement),
            ):
                try:
                    sample[name] = get_json(url)
                except (OSError, URLError, ValueError) as exc:
                    sample[name + "_error"] = str(exc)
            append_jsonl(api_log, sample)
            stop.wait(args.poll_seconds)

    poller = threading.Thread(target=poll_api, name="api-poller", daemon=True)
    poller.start()
    port = None
    try:
        port = serial.Serial()
        port.port = args.port
        port.baudrate = 115200
        port.timeout = 0.2
        # These levels were verified on this board without resetting the ESP32.
        port.dtr = True
        port.rts = False
        port.open()
        append_jsonl(monitor_log, {"at_utc": utc_now().isoformat(), "event": "serial_open"})
        with serial_log.open("a", encoding="utf-8", buffering=1) as log:
            while utc_now() < finish:
                raw = port.readline()
                if raw:
                    log.write(f"{utc_now().isoformat()} {raw.decode('utf-8', 'replace').rstrip()}\n")
    except (OSError, serial.SerialException) as exc:
        append_jsonl(monitor_log, {
            "at_utc": utc_now().isoformat(), "event": "serial_error", "detail": str(exc)
        })
        while utc_now() < finish:
            time.sleep(min(10, max(0.1, (finish - utc_now()).total_seconds())))
    finally:
        stop.set()
        poller.join(timeout=25)
        ended = utc_now()
        if port is not None and port.is_open:
            port.close()
        query = (f"{base}/estabilidad?desde={started.strftime('%Y-%m-%dT%H:%M:%SZ')}"
                 f"&hasta={min(ended, finish).strftime('%Y-%m-%dT%H:%M:%SZ')}")
        try:
            write_json(args.output / "backend_final.json", get_json(query))
        except (OSError, URLError, ValueError) as exc:
            append_jsonl(monitor_log, {
                "at_utc": utc_now().isoformat(), "event": "final_api_error", "detail": str(exc)
            })
        run = json.loads((args.output / "run.json").read_text(encoding="utf-8"))
        run["actual_end_utc"] = ended.isoformat()
        run["status"] = "finished" if ended >= finish else "interrupted"
        write_json(args.output / "run.json", run)


if __name__ == "__main__":
    main()
