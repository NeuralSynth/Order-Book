// bench.cpp — the single benchmark/verification binary.
//
//   bench --impl A|B|C [--runs N] [--total-only] [--latency]
//   bench --verify [--impl ABC]
//   bench --generate-only           build the workload and exit (perf baseline)
//
//   --runs N        timed runs per invocation (default 5); medians are reported
//   --total-only    skip the per-op pass (use under perf, see below)
//   --latency       also print per-op latency percentiles (from the last run)
//   --ops N --seed S  override the workload (defaults 1,000,000 / 42; graded results use the defaults)
//
// Timing methodology (see README "Benchmark methodology"):
//   - The whole op stream is generated before any timing. Every impl replays the same
//     vector (its hash is printed).
//   - Each run has two passes over the stream, each on a freshly constructed book
//     (construction is not timed):
//       1. total pass: one clock pair around the whole loop -> total wall time. Clock
//          overhead is negligible here.
//       2. per-op pass: a clock pair around every op -> mean ns per op type, minus the
//          calibrated cost of an empty clock pair.
//   - Depth queries (get_bids(10) + get_asks(10) after every 100th op) are extra to the
//     1M ops. They are included in the total time and reported as their own category.
//   - Medians over the runs are reported for every number.
//   - A checksum folds in every query result and cancel result, so the compiler cannot
//     drop the work. It must be identical across runs, passes and implementations.
//
// The harness is a template on the book type (no virtual dispatch).
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "book_a.h"
#include "book_b.h"
#include "book_c.h"
#include "verify.h"
#include "workload.h"

namespace {

using Clock = std::chrono::steady_clock;

int64_t elapsed_ns(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from).count();
}

// --- checksum ---------------------------------------------------------------------

// Prices are folded in as raw bits. Every impl produces prices through the same
// tick_to_price(), so equal results give bit-identical doubles.
uint64_t bits_of(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof u);
    return u;
}

struct Checksum {
    uint64_t value = 14695981039346656037ull;
    void mix(uint64_t x) { value = (value ^ x) * 1099511628211ull; }
};

void fold_bbo(Checksum& cs, const BBO& bbo) {
    cs.mix(bbo.has_bid);
    if (bbo.has_bid) {
        cs.mix(bits_of(bbo.bid_price));
        cs.mix(static_cast<uint64_t>(bbo.bid_qty));
    }
    cs.mix(bbo.has_ask);
    if (bbo.has_ask) {
        cs.mix(bits_of(bbo.ask_price));
        cs.mix(static_cast<uint64_t>(bbo.ask_qty));
    }
}

void fold_levels(Checksum& cs, const std::vector<Level>& levels) {
    cs.mix(levels.size());
    for (const Level& l : levels) {
        cs.mix(bits_of(l.price));
        cs.mix(static_cast<uint64_t>(l.qty));
    }
}

// --- one op -----------------------------------------------------------------------

template <typename Book>
inline void apply(Book& book, const Op& op, Checksum& cs) {
    switch (op.type) {
        case OpType::Add: book.add_order(op.id, op.side, op.price, op.qty); break;
        case OpType::Cancel: cs.mix(book.cancel_order(op.id)); break;
        case OpType::Bbo: fold_bbo(cs, book.get_bbo()); break;
    }
}

template <typename Book>
inline void depth_query(const Book& book, Checksum& cs) {
    fold_levels(cs, book.get_bids(kDepthQueryLevels));
    fold_levels(cs, book.get_asks(kDepthQueryLevels));
}

inline bool is_depth_point(std::size_t i) { return (i + 1) % kDepthQueryInterval == 0; }

// --- clock calibration ------------------------------------------------------------

struct ClockInfo {
    double pair_overhead_ns;  // mean cost of back-to-back now() calls (median of 5 batches)
    int64_t granularity_ns;   // smallest non-zero difference observed
};

ClockInfo calibrate_clock() {
    constexpr int kBatches = 5;
    constexpr int kPairsPerBatch = 1'000'000;
    std::vector<double> means;
    int64_t granularity = INT64_MAX;
    for (int b = 0; b < kBatches; ++b) {
        int64_t sum = 0;
        for (int k = 0; k < kPairsPerBatch; ++k) {
            const Clock::time_point t0 = Clock::now();
            const Clock::time_point t1 = Clock::now();
            const int64_t d = elapsed_ns(t0, t1);
            sum += d;
            if (d > 0 && d < granularity) granularity = d;
        }
        means.push_back(static_cast<double>(sum) / kPairsPerBatch);
    }
    std::sort(means.begin(), means.end());
    return ClockInfo{means[kBatches / 2], granularity};
}

// --- passes -----------------------------------------------------------------------

enum Category { kAdd = 0, kCancel = 1, kBbo = 2, kDepth = 3, kNumCategories = 4 };
const char* const kCategoryNames[kNumCategories] = {"add", "cancel", "bbo", "depth"};

struct TotalResult {
    int64_t total_ns;
    uint64_t checksum;
    BBO final_bbo;
};

template <typename Book>
TotalResult run_total_pass(const std::vector<Op>& ops) {
    const auto book = std::make_unique<Book>();  // constructed (and later destroyed) outside the timed region
    Checksum cs;

    const Clock::time_point start = Clock::now();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        apply(*book, ops[i], cs);
        if (is_depth_point(i)) depth_query(*book, cs);
    }
    const Clock::time_point end = Clock::now();

    return TotalResult{elapsed_ns(start, end), cs.value, book->get_bbo()};
}

struct PerOpResult {
    int64_t sum_ns[kNumCategories] = {};
    std::size_t count[kNumCategories] = {};
    uint64_t checksum = 0;
};

// If `latencies` is non-null, every raw per-op duration (clock overhead included) is
// also recorded, per category. Storing them costs a little extra per op, which is why
// it is opt-in (--latency).
template <typename Book>
PerOpResult run_per_op_pass(const std::vector<Op>& ops, std::vector<int64_t>* latencies) {
    const auto book = std::make_unique<Book>();
    Checksum cs;
    PerOpResult r;

    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        const int category = static_cast<int>(op.type);

        const Clock::time_point t0 = Clock::now();
        apply(*book, op, cs);
        const Clock::time_point t1 = Clock::now();
        const int64_t d = elapsed_ns(t0, t1);
        r.sum_ns[category] += d;
        r.count[category] += 1;
        if (latencies) latencies[category].push_back(d);

        if (is_depth_point(i)) {
            const Clock::time_point d0 = Clock::now();
            depth_query(*book, cs);
            const Clock::time_point d1 = Clock::now();
            const int64_t dd = elapsed_ns(d0, d1);
            r.sum_ns[kDepth] += dd;
            r.count[kDepth] += 1;
            if (latencies) latencies[kDepth].push_back(dd);
        }
    }
    r.checksum = cs.value;
    return r;
}

// --- reporting --------------------------------------------------------------------

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

int64_t percentile(std::vector<int64_t>& v, double p) {
    const std::size_t k = std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size())));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

void print_latency_percentiles(std::vector<int64_t>* latencies) {
    std::printf("Per-op latency, last run (raw ns, clock overhead NOT subtracted):\n");
    std::printf("  %-7s %8s %8s %8s %8s\n", "op", "p50", "p99", "p99.9", "max");
    for (int c = 0; c < kNumCategories; ++c) {
        std::vector<int64_t>& v = latencies[c];
        if (v.empty()) continue;
        const int64_t p50 = percentile(v, 0.50);
        const int64_t p99 = percentile(v, 0.99);
        const int64_t p999 = percentile(v, 0.999);
        const int64_t max = *std::max_element(v.begin(), v.end());
        std::printf("  %-7s %8" PRId64 " %8" PRId64 " %8" PRId64 " %8" PRId64 "\n", kCategoryNames[c], p50, p99, p999,
                    max);
    }
}

void print_bbo(const BBO& bbo) {
    std::printf("Final BBO:\n");
    if (bbo.has_bid) std::printf("  Bid: %.2f x %ld\n", bbo.bid_price, bbo.bid_qty);
    else std::printf("  Bid: none\n");
    if (bbo.has_ask) std::printf("  Ask: %.2f x %ld\n", bbo.ask_price, bbo.ask_qty);
    else std::printf("  Ask: none\n");
}

struct BenchOptions {
    int runs = 5;
    bool total_only = false;
    bool latency = false;
};

template <typename Book>
int run_benchmark(char impl, const WorkloadSpec& spec, const BenchOptions& opt) {
    const std::vector<Op> ops = generate_workload(spec);  // before any timing
    const WorkloadStats stats = count_ops(ops);
    std::size_t depth_queries = 0;
    for (std::size_t i = 0; i < ops.size(); ++i) depth_queries += is_depth_point(i);

    std::printf("Impl: %c\n", impl);
    std::printf("Build: compiler %s, optimised %s, NDEBUG %s\n", __VERSION__,
#ifdef __OPTIMIZE__
                "yes",
#else
                "no",
#endif
#ifdef NDEBUG
                "on");
#else
                "off");
#endif
#ifndef NDEBUG
    std::printf("WARNING: built without -DNDEBUG (asserts on). Not a valid timing build.\n");
#endif
#ifndef __OPTIMIZE__
    std::printf("WARNING: built without optimisation. Not a valid timing build.\n");
#endif
    std::printf("Ops: %zu (adds=%zu, cancels=%zu, bbo=%zu) + %zu depth queries (get_bids(%d)+get_asks(%d))\n",
                ops.size(), stats.adds, stats.cancels, stats.bbos, depth_queries, kDepthQueryLevels,
                kDepthQueryLevels);
    std::printf("Workload: seed %" PRIu32 ", hash %016" PRIx64 "\n", spec.seed, hash_workload(ops));

    ClockInfo clock{0.0, 0};
    if (!opt.total_only) {
        clock = calibrate_clock();
        std::printf("Clock: steady_clock pair overhead %.1f ns (subtracted from per-op means), granularity %" PRId64
                    " ns\n",
                    clock.pair_overhead_ns, clock.granularity_ns);
    }

    std::vector<double> totals_ms;
    std::vector<double> per_op_ns[kNumCategories];
    uint64_t checksum = 0;
    BBO final_bbo{};
    bool checksum_ok = true;

    std::vector<int64_t> latencies[kNumCategories];
    if (opt.latency) {
        latencies[kAdd].reserve(stats.adds);
        latencies[kCancel].reserve(stats.cancels);
        latencies[kBbo].reserve(stats.bbos);
        latencies[kDepth].reserve(depth_queries);
    }

    for (int run = 1; run <= opt.runs; ++run) {
        const TotalResult total = run_total_pass<Book>(ops);
        totals_ms.push_back(static_cast<double>(total.total_ns) / 1e6);
        if (run == 1) {
            checksum = total.checksum;
            final_bbo = total.final_bbo;
        }
        checksum_ok &= total.checksum == checksum;
        std::printf("Run %d: total %8.2f ms", run, totals_ms.back());

        if (!opt.total_only) {
            const bool record = opt.latency && run == opt.runs;
            if (record) {
                for (auto& v : latencies) v.clear();
            }
            const PerOpResult per_op = run_per_op_pass<Book>(ops, record ? latencies : nullptr);
            checksum_ok &= per_op.checksum == checksum;
            std::printf(" |");
            for (int c = 0; c < kNumCategories; ++c) {
                const double mean = per_op.count[c] == 0
                                        ? 0.0
                                        : static_cast<double>(per_op.sum_ns[c]) / static_cast<double>(per_op.count[c]);
                per_op_ns[c].push_back(mean - clock.pair_overhead_ns);
                std::printf(" ns/%s %7.1f", kCategoryNames[c], per_op_ns[c].back());
            }
        }
        std::printf("\n");
    }

    std::printf("Median total: %.2f ms (%d runs)\n", median(totals_ms), opt.runs);
    if (opt.runs > 1) {
        // Runs in one process share heap state (and the allocator's history), so they are
        // not fully independent. Show the spread so a median cannot hide a drifting run.
        const auto [lo, hi] = std::minmax_element(totals_ms.begin(), totals_ms.end());
        std::printf("Spread total: min %.2f ms, max %.2f ms, max/min %.2fx\n", *lo, *hi, *hi / *lo);
        if (*hi / *lo > 1.5) {
            std::printf("WARNING: runs differ by more than 1.5x. Prefer separate processes "
                        "(scripts/run_bench.sh) and check for background load.\n");
        }
    }
    if (!opt.total_only) {
        std::printf("Median");
        for (int c = 0; c < kNumCategories; ++c) std::printf("  ns/%s: %.1f", kCategoryNames[c], median(per_op_ns[c]));
        std::printf("\n");
    }
    if (opt.latency && !opt.total_only) print_latency_percentiles(latencies);
    print_bbo(final_bbo);
    std::printf("Checksum: %016" PRIx64 "\n", checksum);

    if (!checksum_ok) {
        std::printf("ERROR: checksum differed between runs or passes; results are not deterministic.\n");
        return 1;
    }
    return 0;
}

void print_usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage:\n"
                 "  %s --impl A|B|C [--runs N] [--total-only] [--latency] [--ops N] [--seed S]\n"
                 "  %s --verify [--impl ABC] [--ops N] [--seed S]\n"
                 "  %s --generate-only [--ops N] [--seed S]\n",
                 prog, prog, prog);
}

}  // namespace

int main(int argc, char** argv) {
    std::string impl;
    bool verify = false;
    bool generate_only = false;
    BenchOptions opt;
    WorkloadSpec spec;

    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        if (std::strcmp(argv[i], "--verify") == 0) {
            verify = true;
        } else if (std::strcmp(argv[i], "--generate-only") == 0) {
            generate_only = true;
        } else if (std::strcmp(argv[i], "--total-only") == 0) {
            opt.total_only = true;
        } else if (std::strcmp(argv[i], "--latency") == 0) {
            opt.latency = true;
        } else if (std::strcmp(argv[i], "--impl") == 0 && has_value) {
            impl = argv[++i];
        } else if (std::strcmp(argv[i], "--runs") == 0 && has_value) {
            opt.runs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--ops") == 0 && has_value) {
            spec.num_ops = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--seed") == 0 && has_value) {
            spec.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (verify) return run_verify(impl.empty() ? "ABC" : impl, spec.num_ops, spec.seed);

    if (generate_only) {
        // Baseline for perf: process start + workload generation, no book at all.
        const std::vector<Op> ops = generate_workload(spec);
        std::printf("Generated %zu ops, hash %016" PRIx64 "\n", ops.size(), hash_workload(ops));
        return 0;
    }

    if (opt.runs < 1 || impl.size() != 1 || spec.num_ops == 0) {
        print_usage(argv[0]);
        return 2;
    }
    switch (impl[0]) {
        case 'A': return run_benchmark<BookA>('A', spec, opt);
        case 'B': return run_benchmark<BookB>('B', spec, opt);
        case 'C': return run_benchmark<BookC>('C', spec, opt);
        default: print_usage(argv[0]); return 2;
    }
}
