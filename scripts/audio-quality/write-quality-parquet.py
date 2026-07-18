#!/usr/bin/env python3
"""Write and verify the pinned per-case Parquet qualification artifact.

This helper deliberately has a very small surface.  The trusted campaign runs
it with the Python interpreter from the hash-pinned metrics runtime, so the
Parquet implementation is part of the same protected payload as DNSMOS,
eSTOI, and WER.  A missing ``pyarrow`` dependency is a hard error; silently
renaming JSON or CSV to ``.parquet`` would make the release evidence false.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Mapping, MutableMapping, Sequence


class ParquetError(RuntimeError):
	pass


def _load_rows(path: Path) -> list[Mapping[str, Any]]:
	def reject_duplicates(pairs: Sequence[tuple[str, Any]]) -> MutableMapping[str, Any]:
		value: MutableMapping[str, Any] = {}
		for key, item in pairs:
			if key in value:
				raise ParquetError(f"duplicate JSON key in {path}: {key!r}")
			value[key] = item
		return value

	try:
		value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
	except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
		raise ParquetError(f"unable to load per-case rows {path}: {error}") from error
	if not isinstance(value, list) or not value:
		raise ParquetError("per-case rows must be a non-empty JSON array")
	rows: list[Mapping[str, Any]] = []
	columns: tuple[str, ...] | None = None
	for index, row in enumerate(value):
		if not isinstance(row, dict):
			raise ParquetError(f"row {index} is not an object")
		current = tuple(sorted(row))
		if columns is None:
			columns = current
		elif current != columns:
			raise ParquetError(f"row {index} does not have the canonical column set")
		for key, item in row.items():
			if not isinstance(key, str) or not key:
				raise ParquetError(f"row {index} contains an invalid column name")
			if isinstance(item, (dict, list)):
				raise ParquetError(f"row {index}.{key} is nested; per-case Parquet must stay flat")
		rows.append({key: row[key] for key in current})
	return rows


def write_parquet(rows_path: Path, output: Path) -> None:
	try:
		import pyarrow as pa  # type: ignore[import-not-found]
		import pyarrow.parquet as pq  # type: ignore[import-not-found]
	except ImportError as error:
		raise ParquetError(
			"the pinned metrics runtime does not contain pyarrow; formal qualification cannot emit Parquet"
		) from error

	rows = _load_rows(rows_path)
	output = output.resolve()
	if output.exists():
		raise ParquetError(f"refusing to replace an existing artifact: {output}")
	output.parent.mkdir(parents=True, exist_ok=True)
	temporary = output.with_name(f".{output.name}.{os.getpid()}.tmp")
	try:
		table = pa.Table.from_pylist(rows)
		pq.write_table(
			table,
			temporary,
			compression="zstd",
			version="2.6",
			write_statistics=True,
			use_dictionary=True,
		)
		if temporary.read_bytes()[:4] != b"PAR1" or temporary.read_bytes()[-4:] != b"PAR1":
			raise ParquetError("writer did not produce a Parquet container")
		verified = pq.read_table(temporary)
		if verified.num_rows != len(rows) or verified.column_names != list(rows[0]):
			raise ParquetError("Parquet read-back differs from the requested rows/schema")
		os.replace(temporary, output)
	finally:
		if temporary.exists():
			temporary.unlink()


def run_self_test() -> None:
	import tempfile

	with tempfile.TemporaryDirectory(prefix="mumble-parquet-self-test-") as raw:
		root = Path(raw)
		rows = root / "rows.json"
		rows.write_text('[{"case_id":"a","value":1.5},{"case_id":"b","value":2.5}]\n', encoding="utf-8")
		try:
			import pyarrow  # type: ignore[import-not-found]  # noqa: F401
		except ImportError:
			try:
				write_parquet(rows, root / "cases.parquet")
			except ParquetError as error:
				if "does not contain pyarrow" not in str(error):
					raise
			else:
				raise AssertionError("missing pyarrow did not fail closed")
			return
		output = root / "cases.parquet"
		write_parquet(rows, output)
		if output.read_bytes()[:4] != b"PAR1" or output.read_bytes()[-4:] != b"PAR1":
			raise AssertionError("Parquet self-test produced an invalid container")


def main(argv: Sequence[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--rows-json", type=Path)
	parser.add_argument("--output", type=Path)
	parser.add_argument("--self-test", action="store_true")
	args = parser.parse_args(argv)
	try:
		if args.self_test:
			run_self_test()
			print("quality Parquet writer self-test: ok")
			return 0
		if args.rows_json is None or args.output is None:
			raise ParquetError("--rows-json and --output are required")
		write_parquet(args.rows_json, args.output)
		print(f"quality Parquet writer: wrote {args.output}")
		return 0
	except (AssertionError, OSError, ParquetError) as error:
		print(f"quality Parquet writer: error: {error}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
