"""Create a bounded read-only telemetry selection for reconciliation."""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "energia-iot-backend" / "tools"))
from reconcile_obj4 import load_records  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--firmware")
    parser.add_argument("--boot-id", type=int)
    parser.add_argument("--seq-first", type=int)
    parser.add_argument("--seq-last", type=int)
    args = parser.parse_args()

    records = load_records(args.input)
    def selected_row(row):
        firmware = row.get("fw", row.get("fw_version"))
        boot = row.get("boot_id", row.get("boot"))
        seq = row.get("seq", row.get("sequence"))
        return (
            (args.firmware is None or firmware == args.firmware)
            and (args.boot_id is None or int(boot) == args.boot_id)
            and (args.seq_first is None or int(seq) >= args.seq_first)
            and (args.seq_last is None or int(seq) <= args.seq_last)
        )

    selected = [row for row in records if selected_row(row)]
    selected.sort(key=lambda row: (int(row.get("boot_id", -1)), int(row.get("seq", -1))))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(selected, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Selected {len(selected)} of {len(records)} SD audit rows into {args.out}")


if __name__ == "__main__":
    main()
