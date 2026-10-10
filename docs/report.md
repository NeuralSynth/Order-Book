# Order Book: Results and Analysis

**Authors:** TODO(human) (name, SST email, roll number; both teammates if a team)

<!--
SCAFFOLD NOTES: delete every block marked "scaffold" before submission.

This file was set up by the coding agent as a skeleton. Per AGENT.md ground rules:
  - every number comes from a file in results/ produced on YOUR machine;
  - the analysis and conclusions are written by you;
  - the "hypothesis" lines below were written BEFORE any measurement. They are
    predictions to test, not findings. Keep the ones the data supports, and say plainly
    where the data contradicts them (that is the interesting part).
Target length for the final text: 1–2 pages, excluding the tables.
-->

## 1. Setup

| | |
|---|---|
| Machine / CPU | TODO(human): from `results/perf_env.txt` |
| Kernel, perf version | TODO(human) |
| Compiler, flags | TODO(human): `g++ … -std=c++17 -O2 -DNDEBUG` (perf record: `+ -g -fno-omit-frame-pointer`) |
| Clock granularity / pair overhead | TODO(human): from the `Clock:` line of `results/bench_*.txt` |
| Workload | 1,000,000 ops, `mt19937(42)`, hash `27f23594045a1e72`; 700,471 add / 199,303 cancel / 100,226 BBO; + 10,000 depth-query pairs every 100 ops (extra, timed separately) |
| Repetition | Timing: median of 5 separate processes (`scripts/run_bench.sh`). Counters: `perf stat -r 5`. |
| Correctness | `results/verify.txt`: A, B, C identical to the oracle on every op (graded + thin-book workloads) |

Methodology details: README "Benchmark methodology" and "Profiling".

## 2. Results

All cells from `results/bench_summary.txt` (timing) and `results/perf_summary.md` (counters).
Counters are per process and **include workload generation**. The baseline column
(`bench --generate-only`) shows that share.

| Metric | Baseline (generation only) | A | B | C |
|---|---|---|---|---|
| Total time (ms) | n/a | TODO(human) | TODO(human) | TODO(human) |
| ns / add | n/a | TODO(human) | TODO(human) | TODO(human) |
| ns / cancel | n/a | TODO(human) | TODO(human) | TODO(human) |
| ns / BBO | n/a | TODO(human) | TODO(human) | TODO(human) |
| ns / depth pair | n/a | TODO(human) | TODO(human) | TODO(human) |
| IPC (instructions / cycles) | TODO(human) | TODO(human) | TODO(human) | TODO(human) |
| Branch misses (% of branches) | TODO(human) | TODO(human) | TODO(human) | TODO(human) |
| L1-dcache-load-misses | TODO(human) | TODO(human) | TODO(human) | TODO(human) |
| Cache misses (% of cache refs) | TODO(human) | TODO(human) | TODO(human) | TODO(human) |
| dTLB-load-misses | TODO(human) | TODO(human) | TODO(human) | TODO(human) |
| Page faults | TODO(human) | TODO(human) | TODO(human) | TODO(human) |

Counters not supported on this machine: TODO(human), from `results/perf_unsupported.txt`
(write "none" if empty).

Big-O per implementation: README → `docs/decisions.md`.

## 3. What the graded workload does and does not exercise

These are exact counts from replaying the op list through the oracle
(`bench --verify` prints them). They are not timings and do not depend on the machine.

| Graded workload property | Value |
|---|---|
| Live orders at end | 501,168 |
| Non-empty price levels at end | 5,000 / 5,000 bid, 5,000 / 5,000 ask |
| Levels emptied during the run | 3,845, all before op 160,755 (0 in the second half) |
| Cancels that emptied the best level | 6 (1 bid, 5 ask) in 1,000,000 ops |
| Order ids | sequential 1, 2, 3, … |

<!-- scaffold: prompt for this section -->
> **Write (human):** what this means for interpreting the results. Prompts:
> - After ~16% of the run, no price level is ever created or removed. Which code paths of
>   A, B and C are therefore almost never timed? (A: tree node insert/erase; B: vector
>   insert/erase shift; C: bitmap "find next best".)
> - Sequential ids are kind to hashing. libstdc++'s `std::hash<uint64_t>` is the identity,
>   and C's Fibonacci hashing spreads consecutive ids evenly. How much did the hash tables
>   actually get stressed?
> - Is the result therefore mostly a comparison of **order stores** (id lookup) rather than
>   of **level structures**? Use perf report hot spots to support or refute this.
> - Optional, clearly separated from graded numbers: the verification-only `thin-book` mix
>   (45/45/10) could be benchmarked as a stress variant. Say so if you do.

## 4. Analysis

<!-- scaffold: for each question, the "Hypothesis" and "Evidence to cite" lines are
     agent-written prompts. Replace them with your own answer. -->

### 11.1 Which implementation is fastest per operation, and why?

> *Hypothesis (pre-measurement):* C is fastest for add and cancel, because there is no
> allocation, a direct array index, and usually a single hash probe. B's cancel is the
> slowest: `std::map<id>` with ~500k nodes is about 19 dependent pointer loads, and the
> lower levels of the tree are unlikely to be cache-resident. A's add pays a heap
> allocation per order plus occasional rehashes. BBO is O(1) in all three, so expect similar
> times there, near the clock noise floor.
>
> *Evidence to cite:* ns/add, ns/cancel and ns/BBO, plus the top symbols in
> `results/perf_report_{A,B,C}.txt`. For example, red-black tree functions
> (`_Rb_tree_*`, `lower_bound`) for B; `malloc`/`free`/`_Hashtable` for A;
> `BookC::find_slot`/`insert_order` for C.

**Answer:** TODO(human)

### 11.2 Branch misses: how many, and what causes them?

> *Hypothesis:* the misses come from data-dependent comparisons. These are tree descents
> (A's level map, B's order map) and B's binary search over up to 5,000 levels (about 12
> comparisons whose outcomes depend on the random price). The compiler may turn
> `lower_bound` into conditional moves (no branch), so check with `perf annotate`. C's hot
> path is mostly straight-line code. Its main data-dependent branches are the probe loop
> and "did this level become empty".
> Watch out: the counters include workload generation (`mt19937` and the 70/20/10 decision
> are branchy too). Compare against the baseline column.
>
> *Evidence to cite:* branch-miss %, the baseline column, and `perf annotate` on the hottest
> function of each implementation.

**Answer:** TODO(human)

### 11.3 Cache behaviour: contiguous data vs pointer chasing vs many small allocations

> *Hypothesis:* B's order tree and A's hash nodes are pointer-chasing structures of tens of
> MB, larger than L2 and probably larger than L3. Expect the most L1/LLC misses per cancel
> in B, then A. On Linux x86-64, B's levels (≤ 2 × 5,000 × 24 B ≈ 240 KB) and C's level
> array (10,001 × 16 B ≈ 160 KB) are small enough for L2 or at least L3, depending on the CPU.
> A possible surprise: C's 32 MB hash table is also far larger than cache, and Fibonacci
> hashing deliberately **scatters** consecutive ids. So each add or cancel in C likely touches
> a random cache line (one miss, not ~19). Meanwhile A's identity hash keeps consecutive ids
> in consecutive buckets, and consecutive `malloc`s are often adjacent in memory, so A's adds
> may have better locality than expected. Check whether the counters agree.
>
> *Evidence to cite:* L1-dcache-load-misses, cache-misses and dTLB-load-misses (A vs B vs C
> vs baseline), plus the perf report symbols that carry the misses (if you record a
> cache-miss event with `perf record -e`).

**Answer:** TODO(human)

### 11.4 Page faults: what do they say about memory behaviour?

> *Hypothesis:* page-fault counts may **not** differ much between implementations; *when*
> they happen differs. Every process first builds the 1M-op vector (32 B per `Op` ≈ 32 MB),
> which shows up in the baseline. C then zero-fills its 32 MB hash table in the constructor,
> before timing starts. A and B grow tens of MB of small heap nodes throughout the run, so
> their faults land inside the timed region. B's level vectors are reserved up front. A's
> hash table rehashes as it grows.
> Rough page arithmetic: 32 MB / 4 KB ≈ 8,000 faults. Transparent huge pages would reduce
> that a lot, so check `/sys/kernel/mm/transparent_hugepage/enabled`.
>
> *Evidence to cite:* page-faults for the baseline and for A, B and C, with each
> implementation's difference from the baseline.

**Answer:** TODO(human)

### 11.5 Which implementation would you deploy, and why? (not just lowest latency)

<!-- scaffold: prompts only. The decision is yours. -->
> Cover each of these and cite evidence:
> - **Latency:** mean ns/op (Section 2).
> - **Predictability / tail:** what can make one op slow? A: a rehash (O(N) once), a
>   `malloc` slow path. B: `vector::insert`/`erase` shifting up to 5,000 levels, and a tree
>   node allocation. C: a table doubling (never happens at this load), and a bitmap search
>   bounded to a few words. If the measurement clock has fine granularity, `bench --latency`
>   gives p99/p99.9/max. That is a non-graded bonus; state that it comes from a single run.
> - **Memory:** C reserves 32 MB (hash) + ~160 KB (levels) regardless of load. A and B grow with the
>   number of live orders. Which matters more for a deployed book?
> - **Cache behaviour:** from 11.3.
> - **Complexity / maintainability:** `book_a.h` is 163 lines of standard containers (with
>   comments); `book_c.h` is 345, with custom hashing, backward-shift deletion and bit tricks. That code had the subtlest bugs in
>   testing: two planted bugs were caught only by the scrambled-id / small-table verification
>   (see `docs/decisions.md`).
> - **Assumptions:** C relies on a fixed, small price band (10,001 ticks) and GCC/Clang
>   builtins. What happens if the band were 100× wider?

**Answer:** TODO(human)

### 11.6 What would you try next?

<!-- scaffold: ideas to pick from or ignore. Only include what you can justify. -->
> - Benchmark variants that the graded workload hides: the thin-book mix (frequent
>   empty/best-level changes), cancel-at-best, scrambled ids, or a book larger than cache.
> - Allocation: a pool / `std::pmr::monotonic_buffer_resource` for A's and B's nodes; how
>   much of their cost is `malloc`?
> - B with a flat hash for orders instead of `std::map` (isolates the level-vector effect).
> - C: huge pages for the hash table, or a smaller slot via 32-bit ids if the id space allows.
> - Tail-latency measurement on a fine-grained clock (`rdtsc`), with CPU pinning.

**Answer:** TODO(human)

## 5. Claim → evidence checklist

<!-- scaffold: fill this in while writing; delete the table before submission if you like.
     Every claim in Section 4 should map to a row. The agent will check this table when
     critiquing the draft. -->

| Claim in the text | Evidence (file + counter / symbol) | Supported? |
|---|---|---|
| TODO(human) | | |
