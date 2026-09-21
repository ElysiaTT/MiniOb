# Coursework verification

This branch implements the coursework topics listed in `docs/docs/game/miniob_topics.md`.
Local regression results are not a claim of passing the competition's hidden grader.

## TEXT storage

TEXT values are truncated to 4096 bytes on assignment. A fixed 16-byte locator
(offset, length, reserved) is stored in the heap record; the body is appended to
the table's `.lob` file. Multiple TEXT columns can therefore exceed one page in
logical size without forcing a heap record to span pages. LOB bodies are synced
before their locators are published. Append-only updates retain the old body for
transaction rollback. Deleting a table also removes its LOB file.

TEXT uses tuple execution, since the current vectorized columns have no LOB
decoding or NULL bitmap. Ordinary indexes on other columns remain usable;
indexes directly on TEXT are explicitly rejected. Deleted/obsolete LOB bodies
are not compacted until the table is dropped.

The plain protocol grows request buffers up to 16 MiB and streams result cells.
MySQL row encoding grows its buffer and rejects payloads beyond a single packet.

## Added regression coverage

- `text.py`: empty values, 4095/4096/4097-byte inputs, two large TEXT columns,
  truncating updates, NULL, indexed lookup, string filtering, restart and recreate.
- `coursework_audit.py`: metadata rejection, mixed wildcard projection, date
  validation and date indexes, indexed updates, Cartesian joins, aggregates,
  division by zero, NULL arithmetic, malformed UPDATE rejection, restart/drop.
- `BufferPool.lru_pinning_and_flush_failure`: LRU order, pinned-frame protection,
  retaining frames after failed writeback. LRU itself is inherited from upstream.

## Verification status (2026-09-21)

Passed: TEXT in default and MVCC modes; NULL in MVCC mode; default-mode NULL,
insert, unique, multi-index, order-by, group-by, expression, join-tables,
simple-sub-query, complex-sub-query; the added LRU unit test and the two enabled
parser tests. Two upstream parser tests remain disabled.

Also passed: the extended coursework audit, including rejection of malformed
UPDATE without changing any rows, and all three disk-buffer-pool unit tests.

Both double-write tests (normal writes and exception recovery) also passed.
Existing MySQL-reference runtime tests require a reference server and are not
covered by the local static-case runner. The static audit checks mandatory
features independently, but does not replace hidden grading.
