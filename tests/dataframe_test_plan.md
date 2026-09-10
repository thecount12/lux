# DataFrame Test Plan

Goal: Verify the DataFrame module in `lib/dataframe.lux` (quoted CSV, API, numeric helpers). Native `parseCSV` is the default path; `parseCSVQuoted` remains as a Lux fallback.

Test categories
- Parser correctness
  - Simple CSV (no quotes)
  - Quoted fields containing separators
  - Escaped quotes inside quoted fields ("" -> ")
  - Multiline quoted fields (newlines preserved)
  - CRLF and CR normalization
  - Empty and missing fields, whitespace trimming behavior
  - Trailing newline does not add an extra row

- DataFrame API
  - Header detection and column alignment
  - head(), select(), filter(), mapColumn(), toNumber(), sortBy()
  - groupBy() count/sum/mean/min/max
  - unique(), valueCounts(), describe(), to_csv()
  - fromRecords() from arrays and instance rows (getField)
  - Row access via rowAsObj()
  - Quoted to_csv round-trip

- Integration
  - read_csv_file() with file IO

Status
- Parser tests: `tests/test_dataframe.lux`
- API tests: `tests/test_dataframe_api.lux` (split / wrap in `fun` to stay under 256 identifier slots per chunk; see SPEC.md §13)
- Native CSV: `parseCSV` on POSIX and Plan 9; `read_csv_text` uses it and falls back to `parseCSVQuoted`.

Out of scope
- Joins, BLAS, Plan 9 Float64Array, Float64-backed columns.
