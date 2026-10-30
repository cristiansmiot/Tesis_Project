"""Export an immutable, bounded Objetivo 4 pilot selection from the read-only API.

Usage: python tools/export_pilot_backend.py --out evidence/backend_pilot.json
The source database is never modified. All pages are fetched before writing.
"""

import argparse
import json
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import urlopen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--base-url", default="https://tesisproject-production.up.railway.app/api/v1")
    parser.add_argument("--device-id", default="SM-348518435344")
    parser.add_argument("--boot-id", type=int, default=1259199348)
    parser.add_argument("--seq-first", type=int, default=238)
    parser.add_argument("--seq-last", type=int, default=957)
    args = parser.parse_args()
    page_size = 250
    selected = []
    pages = 0
    for skip in range(0, 10000, page_size):
        query = urlencode({"device_id": args.device_id, "skip": skip, "limit": page_size})
        url = f"{args.base_url.rstrip('/')}/mediciones/?{query}"
        with urlopen(url, timeout=30) as response:
            page = json.load(response)
        if not isinstance(page, list):
            raise RuntimeError("Unexpected API response: expected a list")
        pages += 1
        selected.extend(
            row for row in page
            if row.get("boot_id") == args.boot_id
            and args.seq_first <= (row.get("seq") or -1) <= args.seq_last
        )
        if len(page) < page_size:
            break
    else:
        raise RuntimeError("Page limit reached; export not written")

    expected = args.seq_last - args.seq_first + 1
    if len(selected) != expected or len({row["seq"] for row in selected}) != expected:
        raise RuntimeError(
            f"Expected {expected} distinct readings; got {len(selected)} rows and "
            f"{len({row['seq'] for row in selected})} distinct sequences; export not written"
        )
    selected.sort(key=lambda row: row["seq"])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(selected, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Exported {len(selected)} readings from {pages} API pages to {args.out}")


if __name__ == "__main__":
    main()
