import argparse
import csv
import json
import sys
from pathlib import Path


DB_FILTERS = [
    "adaptive",
    "nonAdaptive",
    "skiQF",
    "randomizedSkiQF",
    "hybridSkiQF",
]
REVERSE_MAP_FILTERS = [
    "adaptive",
    "skiQF",
    "randomizedSkiQF",
    "hybridSkiQF",
]

STAT_FIELDS = {
    "block_manager_blocks_read": ("block-manager", "blocks read"),
    "block_manager_bytes_read": ("block-manager", "bytes read"),
    "cache_bytes_read": ("cache", "bytes read into cache"),
    "cache_pages_requested_from_cache": (
        "cache",
        "pages requested from the cache",
    ),
    "capacity_bytes_read": ("capacity", "bytes read"),
    "connection_read_ios": ("connection", "total read I/Os"),
}


def parse_json_file(path):
    """Return the final cumulative snapshot from a WiredTiger statistics log."""
    data = path.read_text()
    decoder = json.JSONDecoder()
    offset = 0
    final_snapshot = None

    while offset < len(data):
        while offset < len(data) and data[offset].isspace():
            offset += 1
        if offset == len(data):
            break
        final_snapshot, offset = decoder.raw_decode(data, offset)

    if final_snapshot is None:
        raise ValueError(f"No JSON statistics found in {path}")
    return final_snapshot


def summarize_stats(path, filter_name):
    snapshot = parse_json_file(path)
    wiredtiger_stats = snapshot["wiredTiger"]
    summary = {"name": filter_name}
    for output_name, (category, metric) in STAT_FIELDS.items():
        summary[output_name] = wiredtiger_stats[category][metric]
    return summary


def write_group_summary(test_dir, filters, input_suffix, output_name):
    input_paths = {
        filter_name: test_dir / f"{filter_name}_{input_suffix}.json"
        for filter_name in filters
    }
    present_paths = {
        filter_name: path
        for filter_name, path in input_paths.items()
        if path.is_file()
    }

    if not present_paths:
        return None

    missing_filters = sorted(set(filters) - set(present_paths))
    if missing_filters:
        raise FileNotFoundError(
            f"Missing {input_suffix} statistics for: {', '.join(missing_filters)}"
        )

    rows = [
        summarize_stats(present_paths[filter_name], filter_name)
        for filter_name in filters
    ]
    output_path = test_dir / output_name
    with output_path.open("w", newline="") as output_file:
        writer = csv.DictWriter(
            output_file,
            fieldnames=["name", *STAT_FIELDS.keys()],
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {output_path}")
    return output_path


def parse_filter_names(value):
    return [name for name in value.split(",") if name]


def parse_args(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("test_dir", type=Path)
    parser.add_argument(
        "--db-filters",
        type=parse_filter_names,
        default=DB_FILTERS,
    )
    parser.add_argument(
        "--reverse-map-filters",
        type=parse_filter_names,
        default=REVERSE_MAP_FILTERS,
    )
    return parser.parse_args(argv[1:])


def main(argv):
    args = parse_args(argv)

    test_dir = args.test_dir
    if not test_dir.is_dir():
        raise NotADirectoryError(test_dir)

    db_output = write_group_summary(
        test_dir,
        args.db_filters,
        "db_stats",
        "db_stats.csv",
    )
    reverse_map_output = write_group_summary(
        test_dir,
        args.reverse_map_filters,
        "rm_stats",
        "rm_stats.csv",
    )

    if db_output is None and reverse_map_output is None:
        raise FileNotFoundError(f"No WiredTiger statistics found in {test_dir}")


if __name__ == "__main__":
    main(sys.argv)
