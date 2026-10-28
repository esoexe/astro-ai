---
name: sql-helper
description: Write, fix and optimize SQL queries and schemas, explain query plans and suggest indexes. Use for anything involving SQL databases such as PostgreSQL, MySQL, SQLite or SQL Server.
---

# SQL helper

## Steps

1. **Confirm the dialect and schema.** Ask for or read the table definitions (columns, types, keys, indexes) and the database engine and version. Dialects differ on upserts, limits, JSON, dates and window functions.
2. **Write the query clearly.** Explicit column lists, explicit `JOIN ... ON`, CTEs for readability, and always parameters (`$1`, `?`, `:name`) for values that come from users.
3. **Check correctness on the tricky parts:** `NULL` semantics (`NOT IN` with NULLs, `COUNT(col)` vs `COUNT(*)`), duplicate rows from joins, `GROUP BY` completeness, time zones, integer division, and ties when using `LIMIT`.
4. **Performance.** Read `EXPLAIN` (or `EXPLAIN ANALYZE` on a safe copy). Look for sequential scans on large tables, sorts that spill, and nested loops over big inputs. Suggest indexes that match the `WHERE` and `ORDER BY` order, and say what each index costs on writes.
5. **Changes to data or schema** come as migrations, with a rollback, wrapped in a transaction where the engine supports it. For large tables, mention locking and how to batch.

## Rules

- Never run `UPDATE` or `DELETE` without a `WHERE` that was checked with a `SELECT` first.
- Show a small example of input rows and the expected output for non-trivial queries.
