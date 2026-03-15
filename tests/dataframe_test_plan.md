# DataFrame Prototype Test Plan

Goal: Verify the pure-Lux DataFrame prototype correctness and CSV parsing (including quoted fields). Start with unit tests and small benchmarks before porting hot paths to native code.

Test categories
- Parser correctness
  - Simple CSV (no quotes)
  - Quoted fields containing separators
  - Escaped quotes inside quoted fields ("" -> ")
  - Multiline quoted fields (newlines preserved)
  - CRLF and CR normalization
  - Empty and missing fields, whitespace trimming behavior

- DataFrame API
  - Header detection and column alignment
  - head(), select(), filter(), mapColumn(), to_csv(), describe()
  - Row access via rowAsObj()
  - Preservation of order and types (strings by default)

- Edge cases
  - Large files (performance baseline)
  - Rows with varying column counts
  - Unterminated quoted fields (lenient behavior)

- Integration
  - read_csv_file() with file IO
  - Round-trip: read_csv -> to_csv should be stable for simple inputs

Test plan steps
1. Unit tests: add tests in `tests/test_dataframe.lux` covering parser correctness (done).
2. Add negative tests: malformed CSV inputs and expected behavior (warnings or graceful handling).
3. Benchmarks: reuse `benchmark/` framework to measure parse time for increasing sizes (1k, 10k, 100k rows).
4. API tests: exercises select/filter/groupby (start with basic aggregations).
5. Cross-platform runs: run tests on POSIX build and Plan 9 if supported; validate float coercion behaviors.
6. Fuzzing: generate random CSV with quotes to surface parser bugs.

Acceptance criteria
- All unit tests pass in luxtest.
- Parser handles quoted separators, escaped quotes, and multiline fields.
- Performance baseline established; identify hotspots for native porting (parsing and numeric ops).

Next steps after tests
- If parser correctness is validated and performance is insufficient, implement native CSV reader and typed Float64-backed columns.
- Consider integrating SQLite or subprocess pandas for heavy workloads as interim solution.

