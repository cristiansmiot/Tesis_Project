"""Export a read-only backend selection for SD reconciliation.

The script pages through the public measurements endpoint and writes only rows
matching the requested device and, optionally, an exact firmware version.  It
does not modify the API or the database.
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
    parser.add_argument("--firmware")
    parser.add_argument("--page-size", type=int, default=250)
    parser.add_argument("--max-rows", type=int, default=50000)
    args = parser.parse_args()

    selected = []
    pages = 0
    scanned = 0
    for skip in range(0, args.max_rows, args.page_size):
        query = urlencode({"device_id": args.device_id, "skip": skip, "limit": args.page_size})
        url = f"{args.base_url.rstrip('/')}/mediciones/?{query}"
        with urlopen(url, timeout=30) as response:
            page = json.load(response)
        if not isinstance(page, list):
            raise RuntimeError("Unexpected API response: expected a list")
        pages += 1
        scanned += len(page)
        selected.extend(
            row for row in page
            if args.firmware is None or row.get("fw_version") == args.firmware
        )
        if len(page) < args.page_size:
            break
    else:
        raise RuntimeError("Row limit reached; export not written")

    selected.sort(key=lambda row: (row.get("boot_id") or -1, row.get("seq") or -1))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(selected, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        f"Exported {len(selected)} rows after scanning {scanned} rows "
        f"from {pages} API pages to {args.out}"
    )


if __name__ == "__main__":
    main()
