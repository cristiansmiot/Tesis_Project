"""Reconcile the physical SD audit, SD history and backend pilot export.

This tool reads evidence only. It never touches the mounted SD card.
"""

import argparse
import csv
import hashlib
import json
import math
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "energia-iot-backend" / "tools"))
from reconcile_obj4 import load_records, reconcile  # noqa: E402


FIELDS = {
    "vrms": "vrms", "irms": "irms_a", "p_act": "p_w",
    "p_react": "q_var", "p_ap": "s_va", "pf": "pf",
    "freq": "f_hz", "e_act": "e_wh", "temp": "temp_c", "pq": "pq",
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd-dir", required=True, type=Path)
    parser.add_argument("--backend", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--boot-id", type=int, default=1259199348)
    parser.add_argument("--seq-first", type=int, default=238)
    parser.add_argument("--seq-last", type=int, default=957)
    parser.add_argument("--start-utc", default="2026-10-25T02:11:30Z")
    parser.add_argument("--end-utc", default="2026-10-25T14:11:30Z")
    args = parser.parse_args()

    audit_path = args.sd_dir / "telemetry_audit_v1.jsonl"
    history_path = args.sd_dir / "history_v1.csv"
    events_path = args.sd_dir / "events_v1.jsonl"
    within = lambda row: row.get("boot_id") == args.boot_id and args.seq_first <= row.get("seq", -1) <= args.seq_last
    audit_all = load_records(audit_path)
    audit = [row for row in audit_all if within(row)]
    backend = [row for row in load_records(args.backend) if within(row)]
    telemetry = reconcile(audit, backend)
    mismatch_count = len(telemetry.pop("value_mismatches"))
    telemetry["value_mismatch_count"] = mismatch_count

    with history_path.open(encoding="utf-8-sig", newline="") as source:
        history_all = list(csv.DictReader(source))
    history = [row for row in history_all if row.get("boot_id") == str(args.boot_id)
               and row.get("sequence", "").isdigit()
               and args.seq_first <= int(row["sequence"]) <= args.seq_last]
    audit_by_seq = {row["seq"]: row for row in audit}
    history_by_seq = {int(row["sequence"]): row for row in history}
    history_mismatches = [
        {"seq": seq, "field": field, "audit": audit_by_seq[seq][field], "history": history_by_seq[seq][column]}
        for seq in audit_by_seq.keys() & history_by_seq.keys()
        for field, column in FIELDS.items()
        if not math.isclose(float(audit_by_seq[seq][field]), float(history_by_seq[seq][column]),
                            rel_tol=1e-9, abs_tol=1e-7)
    ]

    start = datetime.fromisoformat(args.start_utc.replace("Z", "+00:00")).timestamp()
    end = datetime.fromisoformat(args.end_utc.replace("Z", "+00:00")).timestamp()
    events_all = [json.loads(line) for line in events_path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    events = [row for row in events_all if row.get("boot") == args.boot_id
              and start <= row.get("ts_utc", 0) <= end]
    late = [
        {"seq": row["seq"], "latency_s": (datetime.fromisoformat(row["ts_server"].replace("Z", "+00:00"))
                                        - datetime.fromisoformat(row["ts_device"].replace("Z", "+00:00"))).total_seconds()}
        for row in backend if row.get("ts_server") and row.get("ts_device")
    ]
    late = sorted((row for row in late if row["latency_s"] > 60), key=lambda row: row["seq"])
    result = {
        "selection": {"boot_id": args.boot_id, "seq_first": args.seq_first,
                      "seq_last": args.seq_last, "start_utc": args.start_utc, "end_utc": args.end_utc},
        "source_sha256": {path.name: sha256(path) for path in
                          (audit_path, history_path, events_path, args.backend)},
        "physical_sd": {"audit_total_rows": len(audit_all), "history_total_rows": len(history_all),
                        "events_total_rows": len(events_all)},
        "telemetry": telemetry,
        "history": {"pilot_rows": len(history), "pilot_unique_seq": len(history_by_seq),
                    "missing_from_history": sorted(audit_by_seq.keys() - history_by_seq.keys()),
                    "extra_in_history": sorted(history_by_seq.keys() - audit_by_seq.keys()),
                    "value_mismatch_count": len(history_mismatches),
                    "value_mismatches": history_mismatches[:10]},
        "events_on_sd": {"count": len(events), "by_module_code":
                         {f"{module}:{code}": count for (module, code), count in
                          Counter((row.get("module"), row.get("code")) for row in events).items()},
                         "identities": [{"seq": row.get("seq"), "module": row.get("module"),
                                         "code": row.get("code"), "utc": row.get("utc")}
                                        for row in events]},
        "late_over_60_s": late,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.out}: {telemetry['db_matched_unique']}/{telemetry['sd_unique']} matched; "
          f"{mismatch_count} backend value mismatches; {len(history_mismatches)} history mismatches")


if __name__ == "__main__":
    main()
