# Coursework verification

This branch implements the coursework topics listed in `docs/docs/game/miniob_topics.md`.
Local regression results are not a claim of passing the competition's hidden grader.

## Topic matrix

| Topic | Kind | Implementation commit | Local regression |
| --- | --- | --- | --- |
| buffer pool LRU | required | upstream LRU + `81e292f` audit | `disk_buffer_pool_test` |
| select-meta | required | binder validation + `81e292f` audit | `coursework-audit` |
| drop-table | required | `020c67a` | `coursework-audit` |
| update | required | `8c211f2` | `coursework-audit` |
| date | required | `234c52d` | `coursework-audit` |
| select-tables | required | multi-table planner in `77b56b9` | `join-tables`, `coursework-audit` |
| aggregation-func | required | `ae825fc` | `coursework-audit`, `null` |
| join-tables | optional | `77b56b9` | `join-tables` |
| insert | optional | `6b98192` | `insert` |
| unique | optional | `9bc8532` | `unique` |
| null | optional | `9acb278` | `null` (default and MVCC) |
| simple-sub-query | optional | `44dd89b` | `simple-sub-query` |
| multi-index | optional | `a814253` | `multi-index` |
| text | optional | `81e292f` | `text` (default and MVCC) |
| expression | optional | `7227026`, `81e292f` | `expression`, `coursework-audit` |
| complex-sub-query | optional | `666e3c6` | `complex-sub-query` |
| order-by | optional | `868447f` | `order-by` |
| group-by | optional | `9dd3b4f` | `group-by` |

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

The final default-transaction and MVCC runs each passed all 12 static coursework
cases: coursework-audit, text, null, insert, unique, multi-index, order-by,
group-by, expression, join-tables, simple-sub-query and complex-sub-query. The
extended audit includes malformed and invalid-type UPDATE rejection, no-match,
whole-table, indexed and multi-condition UPDATEs; two- and three-table queries;
aggregate variants; date validation; restart; indexed drop/recreate; and basic
CRUD.

After rebuilding every target, parser, expression, record-manager, all three
disk-buffer-pool tests and both double-write tests passed. Two upstream parser
tests remain disabled in the repository.

The unrelated `BplusTreeLog.concurrency` stress test is still flaky: its base
recovery test passes, but the randomized concurrent subtest can assert on a
pre-existing frame pin-count race. The coursework explicitly excludes
concurrency, the relevant buffer/index sources are unchanged by this stage, and
the unique/multi-index coursework cases pass in both transaction modes.

Existing MySQL-reference runtime tests require a reference server and are not
covered by the local static-case runner. The static audit checks mandatory
features independently, but does not replace hidden grading.
