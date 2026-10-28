---
name: performance-tuning
description: Find and fix performance problems by measuring first, locating the real hotspot, optimizing it and proving the improvement. Use for slow pages, endpoints, queries, builds or high memory and CPU use.
---

# Performance tuning

## Steps

1. **Define the target.** Which operation, how slow it is now, how fast it needs to be, on what data size and hardware.
2. **Measure before changing anything.** Use a repeatable benchmark and a profiler suited to the stack (browser Performance panel and Lighthouse, `perf`, `py-spy`, `node --prof` or clinic, `pprof`, `EXPLAIN ANALYZE`). Record the baseline numbers.
3. **Find the hotspot** that dominates the time or memory. Typical culprits: N+1 queries, missing indexes, repeated work inside loops, unbounded data loaded into memory, synchronous I/O on hot paths, oversized bundles and images, chatty network calls.
4. **Fix the biggest cause first** with the simplest change: better algorithm or data structure, batching, caching with a clear invalidation rule, pagination or streaming, moving work off the hot path.
5. **Measure again** under the same conditions and report before and after numbers. Keep the change only if it helps and the tests still pass.

## Rules

- No speculative micro-optimizations without numbers.
- Note any trade-off you introduce (memory for speed, staleness from caching, added complexity).
