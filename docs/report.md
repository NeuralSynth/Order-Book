# Order Book: Results and Analysis

**Authors:** Shreyas S [shreyas.24bcs10401@sst.scaler.com](mailto:shreyas.24bcs10401@sst.scaler.com) 24BCS10401

<!--
SCAFFOLD NOTES: delete every block marked "scaffold" before submission.

This file was set up by the coding agent as a skeleton. Per AGENT.md ground rules:
  - every number comes from a file in results/ produced on YOUR machine;
  - the analysis and conclusions are written by you;
  - the "hypothesis" lines below were written BEFORE any measurement. They are
    predictions to test, not findings. Keep the ones the data supports, and say plainly
    where the data contradicts them (that is the interesting part).
Target length for the final text: 1–2 pages, excluding the tables.

Update 2026-10-10: Sections 1–2 were filled in from results/wsl/ (measured on the authors'
machine under WSL2, at the authors' request). Section 2.3 adds substitute experiments for
the missing hardware counters (code inspection, a clock-perturbed pass, a failed block fit,
non-graded workload variants). The "Data now available" notes in Section 4 are factual
pointers into those files. Section 3 and each question in Section 4 now carry an
agent-written point-form **outline** (evidence + file references), and Section 5 has draft
claim rows. At the authors' request the outlines were then replaced by **agent-drafted
prose** (overriding AGENT.md rule 6); each answer is marked with a scaffold comment. The
authors must rewrite and verify every answer against Section 5 before submission.
-->

## 1. Setup

|                        |                                                                                                                                                                                                                                                                                  |
|------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Machine / CPU          | Intel Core i7-13650HX (13th gen, 20 logical CPUs; L2 12.5 MiB, L3 24 MiB), laptop                                                                                                                                                                                                |
| OS / kernel            | Ubuntu 26.04 LTS under **WSL2**, kernel `6.18.33.2-microsoft-standard-WSL2`                                                                                                                                                                                                      |
| perf                   | perf 7.0.14, `perf_event_paranoid = 2` (user-space events only). **Software counters only**: the WSL2 kernel has no PMU driver for this CPU, so every hardware counter is `<not supported>` (`results/wsl/perf_unsupported.txt`)                                                 |
| Compiler, flags        | g++ 15.2.0 (Ubuntu), `-std=c++17 -O2 -DNDEBUG`; `perf record` used the same plus `-g -fno-omit-frame-pointer`                                                                                                                                                                    |
| Clock                  | `steady_clock` granularity 12 ns, empty-pair overhead 14.2–14.7 ns across the 15 processes (`Clock:` lines in `results/wsl/bench_*.txt`)                                                                                                                                         |
| Transparent huge pages | `madvise` (not `always`), so the program's allocations use 4 KB pages unless it requests huge pages; it does not                                                                                                                                                                 |
| Workload               | 1,000,000 ops, `mt19937(42)`, hash `27f23594045a1e72`; 700,471 add / 199,303 cancel / 100,226 BBO; + 10,000 depth-query pairs every 100 ops (extra, timed separately)                                                                                                            |
| Repetition             | Timing: median of 5 separate processes (`scripts/run_bench.sh`). Counters: `perf stat -r 5`.                                                                                                                                                                                     |
| Correctness            | `results/wsl/verify.txt`: A, B, C (and C from a 16-slot table) identical to the oracle on every op, graded + thin-book workloads. `results/wsl/sanitizer.txt`: same checks under AddressSanitizer + UBSan, 0 errors. All 15 timed processes printed checksum `cfa9e743f13cd8a8`. |

Methodology details: README "Benchmark methodology" and "Profiling".

## 2. Results

Timing from `results/wsl/bench_summary.txt` (raw: `bench_{A,B,C}.txt`); counters from
`results/wsl/perf_summary.md` (raw: `perf_stat_*.txt`). Counters are per process and
**include workload generation, book construction and destruction**. The baseline column
(`bench --generate-only`) shows the generation share.

| Metric                                      | Baseline (generation only) | A               | B               | C               |
|---------------------------------------------|----------------------------|-----------------|-----------------|-----------------|
| Total time (ms), median of 5                | n/a                        | 160.89          | 183.65          | 27.39           |
| Total time range (min–max, ms)              | n/a                        | 156.59–169.74   | 178.70–186.75   | 26.31–29.54     |
| ns / add                                    | n/a                        | 134.8           | 113.2           | 95.0            |
| ns / cancel                                 | n/a                        | 302.9           | 463.2           | 82.5            |
| ns / BBO                                    | n/a                        | 15.2            | 17.8            | 14.2            |
| ns / depth pair (bids+asks, 10 levels each) | n/a                        | 126.7           | 53.3            | 81.6            |
| task-clock (ms, whole process, mean of 5)   | 68.46 (±2.20%)             | 239.95 (±1.38%) | 295.08 (±1.04%) | 112.73 (±1.29%) |
| IPC (instructions / cycles)                 | n/s                        | n/s             | n/s             | n/s             |
| Branch misses (% of branches)               | n/s                        | n/s             | n/s             | n/s             |
| L1-dcache-load-misses                       | n/s                        | n/s             | n/s             | n/s             |
| Cache misses (% of cache refs)              | n/s                        | n/s             | n/s             | n/s             |
| dTLB-load-misses                            | n/s                        | n/s             | n/s             | n/s             |
| Page faults                                 | 8,932                      | 16,356          | 16,823          | 17,166          |
| Page faults minus baseline                  | n/a                        | 7,424           | 7,891           | 8,234           |

n/s = not supported. Counters not supported on this machine: `cycles`, `instructions`,
`branches`, `branch-misses`, `cache-references`, `cache-misses`, `L1-dcache-load-misses`,
`dTLB-load-misses` (all of them under WSL2; `results/wsl/perf_unsupported.txt`).
No native Linux machine was available, and the WSL2 kernel exposes software events only
(`Performance Events: … no PMU driver, software events only`). These rows are therefore
reported as not supported rather than estimated, as AGENT.md §6 requires. Section 2.3 lists
the substitute evidence used instead.

Big-O per implementation: README → `docs/decisions.md`.

### 2.1 Data-quality notes (facts about the measurements)

<!-- scaffold: these are checks the agent ran on the files. Decide which belong in the
     final report and in what words; the interpretation is yours. -->

1. **Per-op vs total consistency.** (median ns/op × op counts, summed over add, cancel, BBO
   and depth) divided by the median total time:

   |   | Per-op × counts (ms) | Measured total (ms) | Ratio    |
   |---|----------------------|---------------------|----------|
   | A | 157.6                | 160.89              | 0.98     |
   | B | 173.9                | 183.65              | 0.95     |
   | C | 85.2                 | 27.39               | **3.11** |

   The same check on the Windows dry run (100 ns clock, `results/windows-dryrun/`) gave A 1.03,
   B 0.92, C 2.72. C's per-op pass and total pass disagree on both machines and both clocks.
   The per-op pass puts a clock pair around every op; the total pass does not.
2. **task-clock ≠ total time.** perf counts the whole process: generation (baseline 68.46 ms),
   the workload hash, `count_ops`, book construction (C zero-fills its 32 MB table here) and
   destruction (A and B free ~500k nodes). Use `bench_summary.txt` for the timed region.
3. **The BBO numbers sit near the noise floor:** 14–18 ns per op against a 12 ns clock tick
   and a ~14 ns overhead that has already been subtracted.
4. **Profiles are coarse:** sampled with the software CPU clock (A 908, B 1,171, C 416
   samples). They also include the harness: `hash_workload` + `generate_workload` account
   for 21.6% of A's samples, 16.0% of B's and 51.0% of C's.

### 2.2 Profile hot spots (from `results/wsl/perf_report_*`)

Percentages are of all samples in the process. "Incl." values come from the call-graph
reports, where most book code is inlined into its caller.

| Impl | Where the book's samples land                                                                                                                                                                                                                                                                                                                                                                                              |
|------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| A    | `apply<BookA>` 28.4% self, mostly the add path: level-map lookups `std::map<int,…>::operator[]` → `_M_lower_bound` 12.0% + 9.9% incl. (bid and ask maps). `BookA::cancel_order` 26.2% self, of which hash bucket search `_M_find_before_node` 10.8% incl. `_Hashtable::_M_insert_unique_node` 7.1%. `malloc` 0.55%, `cfree` 0.66%, plus 1.87% in unresolved libc addresses (only entries ≥ 0.5% are listed in the report). |
| B    | `BookB::cancel_order` 35.6% self, of which `std::map<id>::find` 32.5% incl. `apply<BookB>` 19.9% (add: `lower_bound` on the level vectors 5.5% + 6.1% incl., order-tree `emplace` 6.4% incl.). `_Rb_tree_rebalance_for_erase` 7.9%. `_Rb_tree::_M_erase` 3.2% is the tree destructor (outside the timed region). `cfree` 1.28%, unresolved libc addresses 2.65%.                                                           |
| C    | Book code is inlined into `main` (25.7% self), of which `BookC::insert_order` (add path) 22.8% incl. `BookC::cancel_order` 6.3% self, of which `find_slot` 4.6% incl. `BookC::reset_table` 2.2% is the constructor's 32 MB fill (outside the timed region).                                                                                                                                                                |

To resolve the unnamed libc addresses (they may include allocator internals), install
`libc6-dbg` in WSL and re-run `scripts/run_perf.sh`.

### 2.3 Substitute experiments (no hardware counters available)

<!-- scaffold: the agent ran these at the authors' request, because WSL2 exposes no
     hardware counters. They are facts from results/wsl/; what they mean is for you to argue.
     Decide which ones belong in the final report. -->

**(a) Compiled code of the hot loops** (`results/wsl/annotate_{A,B,C}.txt`: `perf annotate`
of the `-O2` `bench_perf` binary, with software-clock samples per instruction). With
sampling, a sample is usually charged to the instruction just *after* the one that stalled.

| Hot loop                                                   | Share of the function's samples                                                                  | Loop control                         |
|------------------------------------------------------------|--------------------------------------------------------------------------------------------------|--------------------------------------|
| B: `std::map<id>::find` in `cancel_order`                  | 83% on the instruction after the load of each tree node (`mov 0x18(%rax)`)                       | `cmp` key, `jae`: conditional branch |
| B: `lower_bound` on the level vector                       | under 2%                                                                                         | `cmp`, `jg`: conditional branch      |
| A: level `std::map::lower_bound` on add (bid and ask maps) | 29.5% and 32.2% on the instruction after each node load                                          | `cmp`, `jge`: conditional branch     |
| A: `unordered_map` lookup in `cancel_order`                | 57% around the bucket-pointer load and the first chain-node load; bucket index via integer `div` | conditional branches                 |
| C: slot probe in `insert_order` (inlined into `main`)      | 88% on the instruction after the first load of the hashed slot                                   | `test`, `je`                         |
| C: slot probe in `cancel_order` (`find_slot`)              | 73% on the instruction after the first load of the home slot                                     | `test`, `je`                         |

None of these loops compiled to conditional moves (`cmov`); all are compare-and-branch.

**(b) Clock-perturbed pass** (`bench --batch`, `results/wsl/bench_batch_*.txt`, 5 processes
per impl). The same loop as the total pass, plus one `steady_clock::now()` after every op and
depth query (1,010,000 reads), each process compared with its own total pass:

|   | Unexplained slowdown per process (ms) | Median   | Per op   |
|---|---------------------------------------|----------|----------|
| A | −17.32, −3.58, 3.33, 5.15, 16.99      | 3.33 ms  | +3.3 ns  |
| B | −32.12, −11.73, −4.56, 3.37, 8.86     | −4.56 ms | −4.5 ns  |
| C | 55.21, 56.31, 71.57, 72.39, 74.72     | 71.57 ms | +70.9 ns |

"Unexplained" = perturbed total − plain total − (reads × calibrated cost per read). For A and
B it scatters around zero; for C it is positive in every process. This is the same direction
and size as note 2.1 (1) (C's per-op pass vs total pass).

**(c) Block-timed per-type estimates: failed, do not use.** `--batch` also timed 50-op blocks
and fitted per-type costs by least squares. The model explains almost none of the
block-to-block variation (R² per process: A 0.000 ×5; B 0.000–0.125; C 0.000–0.002), and it
gives impossible values (A: ns/BBO −298.4). Per-type costs therefore remain unresolved for C.
The plain per-op pass inflates them (note 2.1 (1)) and the block fit cannot separate them.

**(d) Non-graded workload variants** (`results/wsl/bench_summary_{scrambled,thin}.txt`;
5 fresh processes each; reported separately from the graded numbers):

| Total time, median ms (range)      | A                      | B                      | C                   |
|------------------------------------|------------------------|------------------------|---------------------|
| Graded (Section 2)                 | 160.89 (156.59–169.74) | 183.65 (178.70–186.75) | 27.39 (26.31–29.54) |
| Scrambled ids (`--ids scrambled`)  | 214.16 (192.52–238.54) | 508.65 (432.07–634.36) | 32.09 (30.88–34.37) |
| Thin book (`--mix thin`, 45/45/10) | 69.38 (67.26–85.86)    | 93.90 (92.24–95.89)    | 31.71 (30.33–34.89) |

- Scrambled ids replay the *same* book evolution (identical checksum `cfa9e743f13cd8a8`); only
  the id values differ, so the difference from graded is attributable to how each order store
  handles non-sequential ids.
- The thin-book run is a different workload (≈450k adds, ≈450k cancels, a book of a few
  hundred orders; checksum `4c8f102f23e21457`), so compare A, B and C *within* that row only.
- Per-op values for the variants are in the same files. C's per-op pass shows the same
  inflation as in 2.1 (1) (scrambled: ns/add 112.7 vs a total of 32.09 ms).

**(e) Per-op latency percentiles** (`./build/bench --impl X --runs 1 --latency`,
`results/wsl/latency_{A,B,C}.txt`; graded workload, one run each; non-graded bonus).
Raw ns per op with the clock overhead (~15 ns) **not** subtracted; clock granularity 12 ns.

| op         | percentile        | A                 | B                    | C               |
|------------|-------------------|-------------------|----------------------|-----------------|
| add        | p50 / p99 / p99.9 | 127 / 273 / 625   | 119 / 336 / 1,156    | 112 / 266 / 440 |
| add        | max               | 8,331,002         | 1,104,477            | 156,783         |
| cancel     | p50 / p99 / p99.9 | 264 / 629 / 4,744 | 571 / 1,366 / 13,064 | 116 / 266 / 394 |
| cancel     | max               | 788,355           | 306,778              | 46,853          |
| BBO        | p50 / p99 / p99.9 | 29 / 37 / 63      | 28 / 39 / 61         | 28 / 36 / 62    |
| depth pair | p50 / p99 / p99.9 | 131 / 396 / 1,364 | 70 / 169 / 362       | 94 / 371 / 750  |

Caveats for using these:
- They come from the per-op pass, which 2.1 (1) and 2.3 (b) show inflates C relative to its
  uninterrupted speed. Compare shapes (p50 → p99.9 → max) more than absolute values across
  implementations.
- A single run under WSL2 on a laptop: a max can include an interrupt, a scheduler preemption
  or a page fault, not only the book's own worst case. The max column says that one op took
  that long, not why. The run-to-run variability of these tails was not measured.
- Checksums matched (`cfa9e743f13cd8a8`), so these runs did the same work as the graded runs.

## 3. What the graded workload does and does not exercise

These are exact counts from replaying the op list through the oracle
(`bench --verify` prints them). They are not timings and do not depend on the machine.

| Graded workload property            | Value                                               |
|-------------------------------------|-----------------------------------------------------|
| Live orders at end                  | 501,168                                             |
| Non-empty price levels at end       | 5,000 / 5,000 bid, 5,000 / 5,000 ask                |
| Levels emptied during the run       | 3,845, all before op 160,755 (0 in the second half) |
| Cancels that emptied the best level | 6 (1 bid, 5 ask) in 1,000,000 ops                   |
| Order ids                           | sequential 1, 2, 3, …                               |

<!-- scaffold: answer drafted by the coding agent at the authors' request (overrides AGENT.md rule 6). Rewrite in your own words, check every claim against Section 5, then delete this comment. -->

The graded workload saturates the book early. By operation ~161k every one of the 5,000
price levels on each side holds at least one order, and from then on no level empties
again: none of the 3,845 level-emptying events falls in the second half of the run, and a
cancel removed the last order at the best price only 6 times in 1,000,000 operations. The
code paths that distinguish the level structures under churn (A's tree-node insertion and
erasure, B's vector shifts and C's bitmap search for the next best level) are therefore
almost never timed. The benchmark mainly compares lookups: the level lookup on add and the
order lookup on cancel. Profiles support this for cancel. B's `std::map<id>::find` takes
32.5% of its samples against under 2% for its level search, and A's hash bucket search takes
10.8% against 3.4% for its level lookup. They do not fully support it for add: A's level-map
lookups take 21.9% of its samples, more than its hash insert (7.1%). Order ids are also
sequential, which is kind to trees and hash tables. Replaying the identical book evolution
with scrambled ids (same checksum) slows A by ×1.33, B by ×2.77 and C by ×1.17. The
scrambled-id and thin-book runs are non-graded variants, reported separately in §2.3.

## 4. Analysis

<!-- scaffold: for each question, the "Hypothesis" and "Evidence to cite" lines are
     agent-written prompts. Replace them with your own answer. The "Data now available"
     lines were added after the WSL run and only point at facts in results/wsl/. -->

### 11.1 Which implementation is fastest per operation, and why?

> *Hypothesis (pre-measurement):* C is fastest for add and cancel, because there is no
> allocation, a direct array index, and usually a single hash probe. B's cancel is the
> slowest: `std::map<id>` with ~500k nodes is about 19 dependent pointer loads, and the
> lower levels of the tree are unlikely to be cache-resident. A's add pays a heap
> allocation per order plus occasional rehashes. BBO is O(1) in all three, so expect similar
> times there, near the clock noise floor.
>
> *Evidence to cite:* ns/add, ns/cancel and ns/BBO, plus the top symbols in
> `results/wsl/perf_report_{A,B,C}.txt`. For example, red-black tree functions
> (`_Rb_tree_*`, `lower_bound`) for B; `malloc`/`free`/`_Hashtable` for A;
> `BookC::find_slot`/`insert_order` for C.
>
> *Data now available:* Section 2 timing table; Section 2.2 hot spots. Note 2.1 (1) before
> comparing C's per-op numbers with A's and B's: C's per-op values and its total time
> disagree by 3.11×. Section 2.3 (b): a clock read between ops adds ~71 ns/op to C and ~0
> to A and B. Section 2.3 (c): the block-timed split failed, so C's per-type costs are not
> resolved. Section 2.3 (d): the scrambled-id variant changes B's total far more than A's or C's.

**Answer:**

<!-- scaffold: answer drafted by the coding agent at the authors' request (overrides AGENT.md rule 6). Rewrite in your own words, check every claim against Section 5, then delete this comment. -->

By total time, C is fastest: 27.39 ms for the graded workload against 160.89 ms for A
(5.9×) and 183.65 ms for B (6.7×). We treat total time as the primary result because the
per-op measurement distorts C. Summing C's per-op means over the operation counts predicts
85.2 ms, three times its measured total, whereas A and B agree within 5% (§2.1). A control
run that only adds a clock read after each operation slows C by about 71 ns per operation
beyond the cost of the reads, and leaves A and B essentially unchanged (§2.3 (b)). C's per-op
figures are therefore upper bounds.

Cancel separates the designs most: B 463 ns, A 303 ns, C at most 83 ns. B's cancel is
dominated by its order-tree search: `std::map<id>::find` accounts for 32.5% of all B samples,
and 83% of `cancel_order`'s samples sit behind the instruction that loads each tree node. For
add, A (135 ns) is slower than B (113 ns) and C (at most 95 ns). Contrary to our expectation,
A's add is not allocation-bound. `malloc` and `free` together take about 1.2% of samples
(plus 1.9% in unnamed libc code), while the two `std::map` level lookups take 21.9%. BBO
costs 14–18 ns in all three, within a few nanoseconds of the 12 ns clock resolution, so we do
not rank it. Depth queries favour B (53 ns per bids+asks pair), whose levels are contiguous,
over C (82 ns, one bitmap search per level) and A (127 ns, tree iteration).

C is fastest because each operation does one hash probe that almost always resolves at the
first slot, then indexes the level array directly, with no allocation. Its exact per-type
costs remain unresolved: our attempt to separate them with block timing failed (R² ≈ 0,
§2.3 (c)).

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
>
> *Data now available:* no branch counters (`branches` and `branch-misses` are not supported
> under WSL2). Section 2.3 (a): every hot lookup loop in A, B and C compiled to
> compare-and-branch, none to `cmov`, and it shows where samples concentrate inside those
> loops. That says where mispredictions *can* happen, not how many do. State that limit.

**Answer:**

<!-- scaffold: answer drafted by the coding agent at the authors' request (overrides AGENT.md rule 6). Rewrite in your own words, check every claim against Section 5, then delete this comment. -->

We could not measure branch misses. The `branches` and `branch-misses` counters are not
supported under WSL2, so this answer identifies where mispredictions can occur rather than
how many occur. In the compiled `-O2` code every hot lookup loop is a compare-and-branch;
none was turned into a conditional move (§2.3 (a)). A's level lookup descends a red-black
tree of up to 5,000 nodes (about 13 levels) with a `jge` at each node. The target price is
uniformly random, so each left/right decision is close to unpredictable. B's cancel descends
a tree of about 500k nodes (about 19 levels) with a `jae` at each step, along a path set by a
randomly chosen live id. B's binary search over its level vector is equally data-dependent
(about 12 `jg` steps) but takes under 2% of samples, so its mispredictions cannot cost much.
C's probe tests whether the first slot is occupied. At a load factor of at most 0.24 it almost
always exits at once, which a branch predictor should handle well. We therefore expect A and
B to suffer several unpredictable branches per operation and C very few. This is an
expectation from the code, not a measurement. A future counter run must also subtract the
workload generator's own branches (the baseline column).

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
>
> *Data now available:* no cache counters (not supported under WSL2). Indirect evidence
> only: Section 2.2 (where time goes); Section 2.3 (a) (which instructions the samples sit
> behind, mostly the first load of a node or slot); 2.3 (b) (clock reads between ops slow C
> but not A or B); 2.3 (d) (scrambled vs sequential ids). None of these measures cache misses
> directly; argue from them carefully and say so. On this machine L3 is 24 MiB.

**Answer:**

<!-- scaffold: answer drafted by the coding agent at the authors' request (overrides AGENT.md rule 6). Rewrite in your own words, check every claim against Section 5, then delete this comment. -->

No cache counters were available, so the evidence here is indirect. The structures that
matter are larger than this CPU's 24 MiB L3. B's order tree holds about 501k nodes of 56 bytes
(the size the compiled code passes to `operator delete`), roughly 28–32 MB, and C's hash table
is 2^21 × 16 B = 32 MiB. The level structures are small (B at most 240 KB, C 160 KB, A about
10,000 tree nodes) and plausibly stay in cache.

The access patterns differ. A cancel in B makes about 19 dependent loads down the tree: each
address comes from the previous load, so they cannot overlap, and 83% of `cancel_order`'s
samples sit behind a node load. C makes one load at a hash-scattered slot, and 88% (add) and
73% (cancel) of its probe samples sit behind that load. Both patterns are consistent with
time spent waiting on memory, though software sampling cannot prove a cache miss.

The clock-perturbed experiment adds a second piece of evidence. A clock read between
operations slows C by about 71 ns per operation but leaves A and B unchanged. Our
interpretation is that C's operations are independent, so when they run back-to-back the CPU
overlaps one operation's memory access with the next (memory-level parallelism), and the
clock read prevents that overlap. A's and B's costs are dominated by dependent chains that
could not overlap in the first place. This is an inference from timing, not a measured miss
count.

Id order matters most for B. With scrambled ids and an otherwise identical run, B slows by
×2.77, A by ×1.33 and C by ×1.17. A plausible explanation, which we did not test, is that
sequential ids place B's tree nodes in memory in key order, giving locality that scrambling
destroys, while C's hashing already scatters ids.

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
>
> *Data now available:* page faults and differences from the baseline in Section 2. THP is
> `madvise`. Reference arithmetic: 2^21 slots × 16 B = 32 MiB = 8,192 pages of 4 KiB. perf
> stat counts faults for the whole process; it does not split them into timed vs untimed.

**Answer:**

<!-- scaffold: answer drafted by the coding agent at the authors' request (overrides AGENT.md rule 6). Rewrite in your own words, check every claim against Section 5, then delete this comment. -->

Page-fault counts barely distinguish the designs. Workload generation alone causes 8,932
faults. Above that baseline A adds 7,424, B 7,891 and C 8,234, within about 11% of each other.
Transparent huge pages are in `madvise` mode, so these are 4 KiB pages, and the counts match
the memory each design needs. C's 8,234 extra faults correspond almost exactly to its
2^21 × 16 B hash table (8,192 pages), which is allocated and zero-filled in the constructor
(`BookC::reset_table`, 2.2% of samples) before timing starts. B's extra faults come to
64.5 bytes per live order, matching a 56-byte tree node rounded up to a 64-byte allocation.
A's come to 60.7 bytes per order (hash nodes plus the bucket array). The important
difference is when the faults occur: C pays them up front, while A and B fault as their heaps
grow during the timed run, which adds to their operation costs and tail latency. perf stat
counts faults for the whole process, so this timed-versus-untimed split is inferred from the
code. It could be measured with `perf record -e page-faults`, which works under WSL2, but we
did not run it.

### 11.5 Which implementation would you deploy, and why? (not just lowest latency)

<!-- scaffold: prompts only. The decision is yours. -->
> Cover each of these and cite evidence:
> - **Latency:** mean ns/op (Section 2).
> - **Predictability / tail:** what can make one op slow? A: a rehash (O(N) once), a
>   `malloc` slow path. B: `vector::insert`/`erase` shifting up to 5,000 levels, and a tree
>   node allocation. C: a table doubling (never happens at this load), and a bitmap search
>   bounded to a few words. With the 12 ns WSL clock, `bench --latency` gives
>   p99/p99.9/max. That is a non-graded bonus; state that it comes from a single run.
> - **Memory:** C reserves 32 MB (hash) + ~160 KB (levels) regardless of load. A and B grow with the
>   number of live orders. Which matters more for a deployed book?
> - **Cache behaviour:** from 11.3.
> - **Complexity / maintainability:** `book_a.h` is 163 lines of standard containers (with
>   comments); `book_c.h` is 345, with custom hashing, backward-shift deletion and bit tricks. That code had the subtlest bugs in
>   testing: two planted bugs were caught only by the scrambled-id / small-table verification
>   (see `docs/decisions.md`).
> - **Assumptions:** C relies on a fixed, small price band (10,001 ticks) and GCC/Clang
>   builtins. What happens if the band were 100× wider?
>
> *Data now available:* run-to-run ranges in Section 2 (total time); tail latency in
> Section 2.3 (e) (single run per implementation; caveats there).

**Answer:**

<!-- scaffold: answer drafted by the coding agent at the authors' request (overrides AGENT.md rule 6). Rewrite in your own words, check every claim against Section 5, then delete this comment. -->

We would deploy C for this problem, under conditions. It is about six times faster in total
time (27.39 ms against 160.89 and 183.65 ms). On our single-run tail measurements it is also
the most predictable. Its 99.9th-percentile add and cancel latencies are 440 and 394 ns,
against 625 and 4,744 ns for A and 1,156 and 13,064 ns for B; these come from the per-op pass,
which inflates C's absolute values. C's slowest single operation took 0.16 ms, compared with
8.3 ms for an A add and 1.1 ms for a B add. A's 8.3 ms outlier is consistent with an
`unordered_map` rehash, since A never reserves its table, but one sample under WSL2 on a
laptop cannot prove the cause. C's own slow paths are bounded or absent here. Its table never
needs to grow at a load of 0.24, and the bitmap search for a new best price touches at most
five words. C was also the least sensitive to id order (×1.17 with scrambled ids, against
×1.33 for A and ×2.77 for B), and it stayed fastest on the thin-book variant.

The costs are real. C reserves 32 MiB plus 160 KB even for an empty book, while A and B use
about 61–65 bytes per live order. It is twice the code of A (345 lines against 163), and its
backward-shift deletion had bugs that only our scrambled-id and 16-slot-table verification
caught, so it needs that testing to be trusted. It also assumes a fixed band of 10,001 ticks
and GCC/Clang builtins; a band 100 times wider would need a million-slot level array. If those
assumptions did not hold (an unknown or very wide price range, or a team that cannot maintain
a custom hash table), we would deploy A instead, with its hash table reserved, as the
simplest correct design. B is hard to justify in either case: its contiguous levels help only
depth queries, while its `std::map<id>` slows every cancel.

### 11.6 What would you try next?

<!-- scaffold: ideas to pick from or ignore. Only include what you can justify. -->
> - Benchmark variants that the graded workload hides: the thin-book mix (frequent
>   empty/best-level changes), cancel-at-best, scrambled ids, or a book larger than cache.
> - Allocation: a pool / `std::pmr::monotonic_buffer_resource` for A's and B's nodes; how
>   much of their cost is `malloc`?
> - B with a flat hash for orders instead of `std::map` (isolates the level-vector effect).
> - C: huge pages for the hash table, or a smaller slot via 32-bit ids if the id space allows.
> - Tail-latency measurement on a fine-grained clock (`rdtsc`), with CPU pinning.
> - Measurement method: the per-op pass and the block fit both failed to give clean
>   per-type costs for C (2.1 (1), 2.3 (c)). What design would? E.g. type-pure op streams
>   with the book state held fixed, or hardware-counter attribution.
> - Hardware counters on native Linux (everything marked n/s above).

**Answer:**

<!-- scaffold: answer drafted by the coding agent at the authors' request (overrides AGENT.md rule 6). Rewrite in your own words, check every claim against Section 5, then delete this comment. -->

Our next steps follow from what we could not measure. First, we would rerun the profile on
native Linux to get the hardware counters missing here (IPC, branch misses, and L1, LLC and
dTLB misses), and use `perf record -e page-faults` to confirm where faults occur. Second, C's
per-type costs need a better method, since both per-op timing and block regression failed.
Type-pure operation streams with the book state held fixed are one option. For A, we would
reserve the hash table to test whether the 8.3 ms outlier disappears, and try a power-of-two
table so the bucket index needs no integer division. For B, replacing `std::map<id>` with a
flat hash table would isolate what its sorted level vectors cost. For C, huge pages for the
32 MiB table (THP is in `madvise` mode) should reduce TLB pressure, and prefetching the next
operation's slot could exploit the overlap suggested by §2.3 (b). Finally, the thin-book and
cancel-at-best workloads deserve their own labelled benchmarks, since the graded workload
never empties a price level.

## 5. Claim → evidence checklist


| Claim in the text                                            | Evidence (file + counter / symbol)                                                                    | Supported?                                                             |
|--------------------------------------------------------------|-------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------|
| C is fastest overall (total time)                            | `wsl/bench_summary.txt`: 27.39 vs 160.89 / 183.65 ms                                                  | Supported                                                              |
| C's per-op values are inflated by per-op timing              | report 2.1 (1): ratio 3.11; `wsl/bench_summary_batch.txt`: +71 ns/op perturbed                        | Supported (timing evidence)                                            |
| B's cancel cost is the order-tree search                     | `wsl/perf_report_B_callgraph.txt`: `std::map<id>::find` 32.5%; `annotate_B.txt`: 83% behind node load | Supported (software sampling)                                          |
| A's add is dominated by level-map lookups, not allocation    | `wsl/perf_report_A_callgraph.txt`: map `lower_bound` 21.9% vs malloc 0.55% / cfree 0.66%              | Supported; unnamed libc 1.87% (refutes the pre-measurement hypothesis) |
| Hot lookup loops compile to branches, not cmov               | `wsl/annotate_{A,B,C}.txt`                                                                            | Supported (code); miss counts not measured                             |
| Branch misses: how many                                      | none: `wsl/perf_unsupported.txt`                                                                      | Not measurable here                                                    |
| C's speed comes from overlapping independent memory accesses | `wsl/bench_summary_batch.txt` (perturbed) + `annotate_C.txt`                                          | Indirect only: inference, no cache counters                            |
| B depends on id order much more than A or C                  | `wsl/bench_summary_scrambled.txt`: ×2.77 / ×1.33 / ×1.17, same checksum                               | Supported; the locality explanation is a hypothesis                    |
| Page-fault counts are similar; C's are its 32 MiB table      | `wsl/perf_summary.md`: deltas 7,424 / 7,891 / 8,234 vs 8,192 pages                                    | Supported; timed vs untimed split inferred, not measured               |
| A's 8.3 ms max add is a rehash                               | `wsl/latency_A.txt`: one sample                                                                       | Plausible, unproven                                                    |
| Depth queries are cheapest in B (contiguous levels)          | `wsl/bench_summary.txt`: ns/depth A 126.7 / B 53.3 / C 81.6                                           | Supported (timing); the cause is an inference from the data structure  |
| C has the best tails (p99.9 add/cancel, max)                 | `wsl/latency_{A,B,C}.txt`: p99.9 cancel 4,744 / 13,064 / 394 ns; max add 8.3 / 1.1 / 0.16 ms          | Supported for one run each; per-op pass (C inflated); WSL noise        |
| A and B fault during the timed run, C before it              | code (`BookC::reset_table` in the constructor; heap growth in A/B) + `wsl/perf_summary.md`            | Inferred, not measured (no `perf record -e page-faults`)               |
| C stays fastest on the thin-book variant                     | `wsl/bench_summary_thin.txt`: 69.38 / 93.90 / 31.71 ms                                                | Supported (non-graded)                                                 |
| Deploy C under a fixed narrow band, otherwise A              | combines the rows above                                                                               | Authors' judgement: check that you agree                               |
