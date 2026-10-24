"""Compare the immutable SD audit with an API JSON or DB CSV export.

python tools/reconcile_obj4.py --sd telemetry_audit_v1.jsonl --db mediciones.json
No network access; all input files remain unchanged.
"""
import argparse
import csv
import json
import math
from datetime import datetime, timezone
from pathlib import Path
from statistics import median


def flatten(record):
    if isinstance(record, dict) and "payload" in record:
        return flatten(record["payload"])
    if not isinstance(record, list):
        return record
    result = {}
    for field in record:
        if "bn" in field:
            result["device_id"] = field["bn"].removeprefix("urn:dev:serial:").removesuffix(":/")
        if "n" in field:
            result[field["n"]] = next((field[k] for k in ("v", "vb", "vs") if k in field), None)
    return result


def load_records(path):
    path = Path(path)
    if path.suffix.lower() == ".csv":
        with path.open(encoding="utf-8-sig", newline="") as source:
            return list(csv.DictReader(source))
    text = path.read_text(encoding="utf-8-sig")
    try:
        data = json.loads(text)
        if isinstance(data, list) and data and isinstance(data[0], dict) and ("bn" in data[0] or "n" in data[0]):
            return [flatten(data)]
        if isinstance(data, dict):
            data = data.get("mediciones", data.get("registros", [data]))
        return [flatten(r) for r in data]
    except json.JSONDecodeError:
        return [flatten(json.loads(line)) for line in text.splitlines() if line.strip()]


def identity(record):
    device = record.get("device_id", record.get("device"))
    boot = record.get("boot_id", record.get("boot"))
    seq = record.get("seq", record.get("sequence"))
    if device is None or boot is None or seq is None:
        return None
    return str(device).upper(), int(boot), int(seq)


def epoch(value):
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (ValueError, TypeError):
        stamp = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
        if stamp.tzinfo is None:
            stamp = stamp.replace(tzinfo=timezone.utc)  # DB UTC export
        return stamp.timestamp()


def percentile(values, probability):
    position = (len(values) - 1) * probability
    lo, hi = math.floor(position), math.ceil(position)
    return values[lo] + (values[hi] - values[lo]) * (position - lo)


def reconcile(sd_records, db_records, live_deadline_s=60):
    unidentified_sd = sum(identity(r) is None for r in sd_records)
    sd = {identity(r): r for r in sd_records if identity(r) is not None}
    db = {identity(r): r for r in db_records if identity(r) is not None}
    matched = sd.keys() & db.keys()
    missing = sd.keys() - db.keys()
    latencies, bad_clock, mismatches = [], 0, []
    fields = {"vrms": "voltaje_rms", "irms": "corriente_rms",
        "p_act": "potencia_activa", "p_react": "potencia_reactiva",
        "p_ap": "potencia_aparente", "pf": "factor_potencia",
        "freq": "frecuencia", "e_act": "energia_activa"}
    for key in matched:
        source, received = sd[key], db[key]
        valid = source.get("time_valid") in (True, "true", "True", "1")
        ts_source, ts_server = epoch(source.get("ts_device")), epoch(received.get("ts_server"))
        if valid and ts_source is not None and ts_server is not None and ts_server >= ts_source:
            latencies.append(ts_server - ts_source)
        else:
            bad_clock += 1
        for source_name, db_name in fields.items():
            left, right = source.get(source_name), received.get(db_name)
            if left is None or right is None or not math.isclose(float(left), float(right), rel_tol=1e-9, abs_tol=1e-7):
                mismatches.append({"identity": key, "field": source_name, "sd": left, "db": right})
    latencies.sort()
    source_clock_bad = sum(r.get("time_valid") not in (True, "true", "True", "1") for r in sd.values())
    timely = sum(value <= live_deadline_s for value in latencies)
    return {
        "sd_unique": len(sd), "db_matched_unique": len(matched),
        "pdr_eventual_percent": 100 * len(matched) / len(sd) if sd and not unidentified_sd else None,
        "live_deadline_s": live_deadline_s,
        "pdr_live_percent": 100 * timely / len(sd) if sd and not bad_clock and not source_clock_bad else None,
        "missing": sorted(missing), "db_outside_sd_selection": len(db.keys() - sd.keys()),
        "sd_duplicate_identities": sum(identity(r) is not None for r in sd_records) - len(sd),
        "db_duplicate_receptions": sum(identity(r) is not None for r in db_records) - len(db) + sum(int(r.get("duplicate_count") or 0) for r in db.values()),
        "identity_conflicts": sum(int(r.get("identity_conflict_count") or 0) for r in db.values()),
        "sd_without_identity": unidentified_sd,
        "db_without_identity": sum(identity(r) is None for r in db_records),
        "invalid_or_negative_latency_records": bad_clock,
        "source_invalid_clock_records": source_clock_bad,
        "latency_s": {"n": len(latencies), "median": median(latencies),
            "p95": percentile(latencies, .95), "p99": percentile(latencies, .99)} if latencies else None,
        "value_mismatches": mismatches,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd", required=True)
    parser.add_argument("--db", required=True)
    parser.add_argument("--live-deadline-s", type=float, default=60)
    args = parser.parse_args()
    print(json.dumps(reconcile(load_records(args.sd), load_records(args.db), args.live_deadline_s), indent=2))
