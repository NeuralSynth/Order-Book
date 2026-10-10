# Design Decisions

Why each design was chosen. Every entry here must be defensible in the viva.

---

## Shared observations (apply to all three designs)

**Only level totals are needed, not per-order queues.** There is no matching, so time
priority never matters. Each level stores `{qty, count}`; each live order is kept in an
`id -> {side, tick, qty}` store. Cancel = look up the order, subtract its qty from its level.

**Hypothesis: the book saturates early.** ~700k adds and ~200k cancels leave ~500k live
orders, i.e. ~250k per side over 5,000 levels (~50 orders/level). With uniform prices,
every level is filled after ~5000 * ln(5000) ≈ 42k adds per side (coupon collector),
and an emptied level is then very rare (P(empty) ~ e^-50 in steady state).
**Checked by deterministic replay** (`bench --verify` prints these; they are counts from
replaying the op list through the oracle, not timings):

| Graded workload (seed 42, 1M ops)         | Value |
|-------------------------------------------|-------|
| Live orders at end                        | 501,168 |
| Non-empty levels at end                   | 5000/5000 bid, 5000/5000 ask |
| Level created (empty → non-empty)         | 13,845 |
| Level emptied (non-empty → empty)         | 3,845, **all before op 160,755**; 0 in the second half |
| Best level emptied by a cancel            | 1 (bid), 5 (ask) in 1M ops |

So the hypothesis holds, and more strongly than the initial estimate: after ~16% of the
run the level structure never changes shape again. Consequences:
- A allocates/frees level tree nodes only during warm-up.
- B's O(L) vector insert/erase shifts happen only during warm-up.
- C's bitmap "find next best" path runs about 6 times in the whole run.
- So the **order store** (id lookup) likely dominates cancel cost, and the level structure
  mostly differs in lookup cost: tree (A) vs binary search (B) vs direct index (C).
  TODO(human): confirm or refute with perf (perf report hot spots for cancel).

This is a property of the graded workload, not of the designs, and it must be stated as a
limitation in the report.

**It also weakened verification.** A planted bug (an emptied level at 150.00 never erased)
passed `--verify` on the graded workload alone, because that code path barely runs. So
`--verify` also replays a **thin-book stress workload** (verification only, never
benchmarked): 200k ops, seed 43, 45% add / 45% cancel / 10% BBO. That book stays at a few
hundred live orders: 87,749 level emptyings and 1,513 best-level emptyings. It caught the
planted bug. The same mix could serve as the optional "stress cancel-of-best" benchmark
variant for the report, if run clearly separately from the graded numbers.

## Verification design

- **Oracle** (`src/oracle.h`): deliberately naive and independent. `std::map` of orders,
  a plain array of level totals, a linear scan for best/depth on every query, and its own
  price→tick conversion (`std::round`), so a bug in the shared `price_to_tick` cannot hide.
  It checks preconditions in every build and aborts, so it also validates the generator.
  It runs the same edge-test suite as the books.
- **Per op**: BBO, `get_bids(10)`, `get_asks(10)`, and every cancel's bool, compared exactly
  against the oracle (exact `==` on prices is valid; see `types.h`).
- **Every 10k ops (graded) / 1k ops (thin-book), and at the end**: full depth of both sides
  plus the book's own `check_invariants()`.
- Each impl is compared with the same oracle, so all three passing means they are
  identical to each other too (exact equality is transitive).
- The **thin-book** workload uses **scrambled ids** (a bijective 64-bit mix of 1, 2, 3, ...,
  so unique but spread over the whole range), and C is verified a second time starting
  from a **16-slot hash table** (`BookCSmallTable`). Sequential ids into C's pre-sized 2M
  table almost never collide, so without these two, `--verify` could not see bugs in C's
  probe-chain / deletion code. `tests/edge_tests.cpp` adds a 50k-op random test of C from
  a 2-slot table against the oracle, plus tests at bitmap word / summary-word boundaries.
- **Verifier tested by planted bugs** in scratch copies (never committed). Every bug below
  makes both `edge_tests` and `--verify` exit 1:

  | Impl | Planted bug | Caught by `--verify` in |
  |------|-------------|-------------------------|
  | A | emptied level at 150.00 never erased | thin-book |
  | A | cancel subtracts 1 too few when qty is 37 | graded (deep check) |
  | A | best bid read from second level | graded |
  | B | best level never erased | graded |
  | B | depth walks worst-first | graded |
  | B | `lower_bound` → `upper_bound` | graded |
  | C | next best ask skips the adjacent tick | thin-book |
  | C | summary bit never cleared | graded (invariants) |
  | C | backward shift ignores table wraparound | thin-book, 16-slot table only |
  | C | no backward shift at all | thin-book |
  | C | new better bid not cached | graded |

---

## Implementation A: tree levels + chained hash orders (baseline)

| Aspect             | Choice |
|--------------------|--------|
| Order storage      | `std::unordered_map<uint64_t, OrderInfo>` (node-based, separate chaining) |
| Level storage      | `std::map<int32_t tick, LevelData>` per side; bids use `std::greater` |
| Best price         | `begin()` of each map: O(1) |
| Cancel lookup      | hash lookup -> `map::find(tick)` -> subtract qty; erase level if count hits 0 |

Why: the textbook design, built from standard containers. It is the reference point.
Expected costs: one heap allocation per order (hash node), one per new level (tree node),
and pointer chasing on every level lookup.

Big-O (L = levels on a side ≤ 5,000; N = live orders; d = min(depth, L)), drafted from
the code in `include/book_a.h`:

| Operation    | Complexity | Notes / flags for the human to verify |
|--------------|------------|----------------------------------------|
| add_order    | O(log L) + O(1) avg | Tree `operator[]` is O(log L). Hash insert is O(1) **amortised average**: a rehash makes one insert O(N), and bad collisions make it O(N) worst case. Nothing is `reserve()`d, so rehashes do happen during the run. |
| cancel_order | O(1) avg + O(log L) | Hash find/erase average O(1); tree `find` O(log L); tree `erase(iterator)` is amortised O(1) (rebalancing). |
| get_bbo      | O(1) | `std::map::begin()` is O(1) because libstdc++ caches the leftmost node. The standard only guarantees constant-time `begin()` for all containers, so this holds regardless. |
| get_bids/asks| O(d) | Iterator increments are amortised O(1); allocates one vector of d elements. |

Choice made in A: the hash table is **not** pre-reserved, deliberately. A is the "plain
standard containers" baseline. Rehash spikes and per-node allocation are part of what it
represents, and they should show up in page faults and tail latency.

## Implementation B: flat sorted levels + tree orders

| Aspect             | Choice |
|--------------------|--------|
| Order storage      | `std::map<uint64_t, OrderInfo>` (red-black tree keyed by id) |
| Level storage      | `std::vector<{tick, qty, count}>` per side, sorted; best level at the **back** (bids ascending, asks descending) |
| Best price         | `back()` of each vector: O(1) |
| Cancel lookup      | tree lookup by id -> binary search tick in vector -> subtract qty; `erase` if empty (O(L) shift) |

Why: the levels are contiguous (cache-friendly binary search, trivial depth walk from the
back), whereas the orders sit in a tree (~19 dependent pointer hops at ~500k nodes).
Keeping the best level at the back makes adding or removing levels near the best cheap
(a short shift). The two halves have opposite memory behaviour, which should show
clearly in the cache counters.
Rejected alternative: hash of levels + heap of prices with lazy deletion. `get_bids(depth)`
would need a copy + partial sort, and stale heap entries add complexity.

Implementation notes:
- One template `worse<Side>(a, b)` defines the sort order for both sides (bids: lower is
  worse; asks: higher is worse), so a single `std::lower_bound` finds a level or its
  insertion point on either side. The side is a template parameter, so there is no
  runtime branch inside the comparator.
- Both vectors `reserve(5000)` in the constructor (a side can never hold more), so they
  never reallocate during a run. That is a deliberate choice, and it affects the
  page-fault analysis.

Big-O (L ≤ 5,000 levels on a side; N live orders; d = min(depth, L)):

| Operation    | Complexity | Notes / flags for the human to verify |
|--------------|------------|----------------------------------------|
| add_order    | O(log N) + O(log L) + O(L) if new level | Tree insert O(log N) (plus a node allocation). Inserting a NEW level shifts every better level: O(number of better levels), O(L) worst case. In the graded run new levels appear only during warm-up (13,845 times, all before op ~161k). |
| cancel_order | O(log N) + O(log L) + O(L) if level empties | Tree find is ~19 dependent node visits at N ≈ 500k, likely the dominant cost (TODO(human): confirm with perf). Erasing an emptied level shifts every better level. |
| get_bbo      | O(1) | `back()` of each vector. |
| get_bids/asks| O(d) | Contiguous reverse walk. |

## Implementation C: direct-indexed levels + bitmap + open-addressing orders

| Aspect             | Choice |
|--------------------|--------|
| Order storage      | Custom open-addressing hash table: linear probing, power-of-two capacity pre-sized at construction, backward-shift deletion (no tombstones) |
| Level storage      | Fixed array of 10,001 `{qty, count}` slots indexed by `tick - 5000` |
| Best price         | Cached best-bid / best-ask tick. When the best level empties, find the next one with a two-level bitmap using `__builtin_clzll` / `__builtin_ctzll` |
| Cancel lookup      | hash probe -> direct array index -> subtract qty; if level emptied, clear bitmap bit and, if it was best, recompute via bitmap |

Why: no allocation in the hot path, everything flat and pre-allocated. The bitmap
bounds the "next best" worst case to a few word operations instead of a 5,000-slot
scan, which matters for tail latency/predictability (report question 11.5) even
though the graded workload rarely hits it.
Notes to defend:
- Builtins are GCC/Clang-specific (C++20 `std::countl_zero` is unavailable in C++17).
  `__builtin_clzll(0)` is undefined behaviour, so always check the word is non-zero first.
- Open addressing works for **arbitrary** IDs. Direct indexing by ID was rejected
  because it assumes dense, sequential IDs.
- Pre-allocation (16 B/slot × 2^21 slots = 32 MB, zeroed in the constructor) moves the
  order store's page faults to construction time. This must be stated when analysing
  page faults.
- **Capacity decision: grow.** If an insert would push the load factor above 0.5, the
  table doubles and rehashes. In the graded run the load peaks around 0.24, so growth never
  happens there, but the book stays correct for any input. The alternative (assert)
  would make C silently wrong in release builds on a bigger workload.
- **Slot layout (16 B):** id (8) + qty as `int32_t` (4) + tick (4). An empty slot is
  marked by qty == 0 (live orders have qty ≥ 1), so no id value is reserved; ids 0 and
  UINT64_MAX work. Side is not stored, because tick < 10000 ⇔ Buy. Cost: an extra
  precondition, qty ≤ INT32_MAX, asserted in debug builds and documented in the README.
- **Fibonacci hashing** (`id × 2^64/φ`, keep the top bits): cheap and spreads sequential
  ids evenly. **Backward-shift deletion**: no tombstones, so lookups don't degrade over
  a long run. The subtle part is the condition for moving an entry back into the hole:
  it may move only if its home slot is NOT cyclically within (hole, j]. Two planted bugs
  in exactly this code are caught only by the scrambled-id / small-table verification.
- **Bitmap:** 10,001 bits → 157 words; a summary bit per word → 3 summary words. Bids
  search downward from a bid index and asks upward from an ask index, so the shared
  bitmap never crosses sides (the mid slot is always empty). Each search is ≤ 1 masked
  word + ≤ 3 summary words + 1 word, each resolved with one clz/ctz.

Big-O (N live orders; d = min(depth, levels on that side)):

| Operation    | Complexity | Notes / flags for the human to verify |
|--------------|------------|----------------------------------------|
| add_order    | O(1) expected | Hash insert (expected O(1) at load ≤ 0.5; worst case O(N) if ids cluster badly). Plus O(N) once per doubling. Array update and bit set are O(1). |
| cancel_order | O(1) expected | Hash find + backward shift (bounded by the probe-chain length). If the best level empties, the bitmap search is O(words) = O(1) for a fixed 10,001-tick range. Strictly that is O(range / 64), a constant here. |
| get_bbo      | O(1) | Cached indices. |
| get_bids/asks| O(d) | One bitmap search per level returned. |

**Hash-friendliness confound (for the report).** The graded ids are sequential. libstdc++'s
`std::hash<uint64_t>` is the identity function, so A's buckets get consecutive ids, and C's
Fibonacci hashing spreads consecutive ids almost perfectly. In the graded run both hash
tables therefore see very few collisions, and C's backward-shift deletion barely runs.
Real exchange ids are often sequential too, so this is not unrealistic, but the report
should not claim the hash tables were stressed.

---

## Differentiation check (all four aspects must differ pairwise)

| Aspect        | A                     | B                        | C                          |
|---------------|-----------------------|--------------------------|----------------------------|
| Orders        | chained hash (nodes)  | red-black tree           | open-addressing flat hash  |
| Levels        | red-black tree        | sorted contiguous vector | direct-indexed array       |
| Best price    | tree `begin()`        | vector `back()`          | cached tick + bitmap       |
| Cancel lookup | hash -> tree find     | tree -> binary search    | probe -> array index       |

---

## Harness and interface decisions

**Public API keeps `double`; internals use integer ticks.** The spec's signatures are
unchanged, so nothing about the interface needs defending as a deviation. Conversion
happens once per call with `llround`. Prices going out are produced by one shared
`tick_to_price`, so every implementation returns bit-identical doubles and the verifier
can compare them with `==`.

**Depth queries are extra to the 1M ops, timed as their own category.** The 1M stream
stays exactly 70/20/10 by construction. Folding depth queries in would distort the
ratios and mix a heavier operation (building a vector of 10 levels) into the totals.

**Clock overhead: report both.** Total wall time per run (one clock pair around the whole
loop, so overhead is negligible) plus per-op averages from individually timed ops minus
a calibrated `steady_clock::now()` cost (measured with back-to-back calls). The calibrated
overhead is printed so the reader can judge how close per-op numbers are to the noise floor.

**Invalid input is a precondition, asserted in debug builds only.** Duplicate IDs,
out-of-band prices, 100.00, and qty <= 0 are excluded by the spec and never produced by the
generator. Checking them in release builds would add branches to every add in the timed
path, identically for all three impls, for no benefit in this assignment. A production book
would validate and reject instead.

**No virtual dispatch.** Benchmark and verifier are templates on the book type, and
conformance is checked at compile time (`implements_order_book<T>()` static_asserts).

**Build flags.** Graded: `-O2 -DNDEBUG`. CMake's default Release is `-O3`, so
`CMakeLists.txt` overrides it explicitly. Perf builds add only `-g -fno-omit-frame-pointer`.

## Benchmark harness decisions (phase 6)

- **Total pass + per-op pass per run** (AGENT.md option 3). The total pass has one clock
  pair, so it is the clean number. The per-op pass subtracts a calibrated empty-pair cost.
  Both passes replay the identical stream on a fresh book, and their checksums must match.
- **Clock granularity is printed.** On the Windows/MinGW dev machine `steady_clock` ticks in
  100 ns steps (an empty pair reads 0 most of the time; mean ≈ 35–40 ns). Means over
  hundreds of thousands of ops still average out the quantisation, but percentiles do not.
  Graded numbers come from the Linux perf machine. TODO(human): record its granularity
  from the `Clock:` line.
- **Separate processes over in-process repeats.** A first smoke run on the dev machine
  (not a result; those numbers are discarded) showed A and B slowing down sharply in later
  in-process runs while C stayed flat. Hypothesis: heap state carried over from earlier
  runs; A and B allocate per order, C does not. That is unconfirmed, and it may also be
  machine noise. So:
  `bench` prints the min/max spread and warns above 1.5×, and `scripts/run_bench.sh` runs
  every repetition in a fresh process. TODO(human): check whether the effect appears on
  the Linux machine. If it does, it is itself worth a sentence in the report
  (allocator behaviour as part of A's and B's cost).
- **`--total-only` for perf**, so the counters measure the book plus workload generation,
  not 2M extra clock calls from the per-op pass.
- **Checksum** = FNV-style fold of every query result (prices as raw bits) and every cancel
  bool. Identical for A, B and C on the graded workload (`cfa9e743f13cd8a8` with
  libstdc++), consistent with `--verify`.

## Still to decide
- [ ] Which machine runs perf (native Linux recommended; Windows/WSL counters are limited)?
- [x] C order store: grow when load would exceed 0.5 (decided in phase 5, see C notes above). Revisit if you prefer assert.
