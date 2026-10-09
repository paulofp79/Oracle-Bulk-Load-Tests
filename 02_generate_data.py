#!/usr/bin/env python3
"""Generate the deterministic 10-million-row delimited benchmark file."""

import argparse
from datetime import date, timedelta
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rows", type=int, default=10_000_000)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).with_name("bulk_test_10m.dat"),
    )
    args = parser.parse_args()
    if args.rows < 1:
        parser.error("--rows must be at least 1")

    # Precompute ISO dates to keep generation fast and output locale-independent.
    date_count = 3650
    start_date = date(2020, 1, 1)
    dates = [(start_date + timedelta(days=i)).isoformat() for i in range(date_count)]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    block_rows = 50_000
    with args.output.open("w", encoding="ascii", newline="\n", buffering=8 * 1024 * 1024) as out:
        first = 1
        while first <= args.rows:
            stop = min(first + block_rows, args.rows + 1)
            block = []
            for row_id in range(first, stop):
                customer_id = (row_id * 97) % 1_000_000 + 1
                cents = (row_id * 7919) % 100_000_000
                amount = f"{cents // 100}.{cents % 100:02d}"
                load_date = dates[row_id % date_count]
                payload = f"LOAD-{row_id:015d}"
                block.append(
                    f"{row_id}|{customer_id}|{amount}|{load_date}|{payload}\n"
                )
            out.write("".join(block))
            first = stop

    print(f"Wrote {args.rows:,} rows to {args.output}")


if __name__ == "__main__":
    main()
